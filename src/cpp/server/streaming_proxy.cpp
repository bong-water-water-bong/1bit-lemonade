#include "lemon/streaming_proxy.h"
#include <sstream>
#include <iostream>
#include <lemon/utils/aixlog.hpp>

namespace lemon {

namespace {

std::string normalize_data_line(const std::string& line) {
    const std::string prefix = "data: ";
    if (line.rfind(prefix, 0) != 0) {
        return line;
    }

    std::string suffix;
    std::string payload = line.substr(prefix.size());
    if (!payload.empty() && payload.back() == '\r') {
        suffix = "\r";
        payload.pop_back();
    }

    if (payload.empty() || payload == "[DONE]") {
        return line;
    }

    try {
        auto chunk = json::parse(payload);
        if (!chunk.is_object() ||
            !chunk.contains("object") ||
            !chunk["object"].is_string() ||
            chunk["object"].get<std::string>() != "chat.completion.chunk" ||
            !chunk.contains("choices") ||
            !chunk["choices"].is_array()) {
            return line;
        }

        bool changed = false;
        for (auto& choice : chunk["choices"]) {
            if (!choice.is_object() || !choice.contains("delta") || !choice["delta"].is_object()) {
                continue;
            }

            auto& delta = choice["delta"];
            const bool role_is_null = delta.contains("role") && delta["role"].is_null();
            const bool role_is_missing = !delta.contains("role");
            const bool has_assistant_delta =
                delta.contains("content") ||
                delta.contains("reasoning_content") ||
                delta.contains("thinking") ||
                delta.contains("tool_calls") ||
                delta.contains("function_call");

            if (role_is_null || (role_is_missing && has_assistant_delta)) {
                delta["role"] = "assistant";
                changed = true;
            }
        }

        if (!changed) {
            return line;
        }

        return prefix + chunk.dump() + suffix;
    } catch (...) {
        return line;
    }
}

} // namespace

std::string StreamingProxy::normalize_chat_completion_chunk_roles(const std::string& sse_chunk) {
    std::string output;
    size_t pos = 0;

    while (pos < sse_chunk.size()) {
        size_t newline = sse_chunk.find('\n', pos);
        if (newline == std::string::npos) {
            output += normalize_data_line(sse_chunk.substr(pos));
            break;
        }

        output += normalize_data_line(sse_chunk.substr(pos, newline - pos));
        output.push_back('\n');
        pos = newline + 1;
    }

    return output;
}

void StreamingProxy::forward_sse_stream(
    const std::string& backend_url,
    const std::string& request_body,
    httplib::DataSink& sink,
    std::function<void(const TelemetryData&)> on_complete,
    long timeout_seconds) {

    std::string telemetry_buffer;
    bool stream_error = false;
    bool has_done_marker = false;

    // libcurl may deliver an SSE line split across multiple write callbacks, so
    // accumulate partial input and only normalize complete lines (terminated by
    // '\n') before forwarding them to the client.
    std::string line_buffer;

    // Use HttpClient to stream from backend
    auto result = utils::HttpClient::post_stream(
        backend_url,
        request_body,
        [&sink, &telemetry_buffer, &has_done_marker, &line_buffer](const char* data, size_t length) {
            // Buffer for telemetry parsing
            telemetry_buffer.append(data, length);

            // Check if this chunk contains [DONE]
            std::string chunk(data, length);
            if (chunk.find("[DONE]") != std::string::npos) {
                has_done_marker = true;
            }

            // Accumulate bytes and flush only complete (newline-terminated) lines
            // so normalization can safely parse each `data: {...}` payload.
            line_buffer.append(chunk);
            std::string output;
            size_t pos = 0;
            size_t newline;
            while ((newline = line_buffer.find('\n', pos)) != std::string::npos) {
                output.append(
                    StreamingProxy::normalize_chat_completion_chunk_roles(
                        line_buffer.substr(pos, newline - pos + 1)));
                pos = newline + 1;
            }
            line_buffer.erase(0, pos);

            if (!output.empty()) {
                if (!sink.write(output.data(), output.size())) {
                    return false; // Client disconnected
                }
            }

            return true; // Continue streaming
        },
        {}, // Empty headers map
        timeout_seconds
    );

    if (result.status_code != 200) {
        stream_error = true;
        LOG(ERROR, "StreamingProxy") << "Backend returned error: " << result.status_code << std::endl;
    }

    if (!stream_error) {
        // Flush any trailing partial line (e.g., backend that omitted a final
        // newline before closing the connection).
        if (!line_buffer.empty()) {
            std::string tail = StreamingProxy::normalize_chat_completion_chunk_roles(line_buffer);
            sink.write(tail.data(), tail.size());
            line_buffer.clear();
        }

        // Ensure [DONE] marker is sent if backend didn't send it
        if (!has_done_marker) {
            LOG(WARNING, "StreamingProxy") << "WARNING: Backend did not send [DONE] marker, adding it" << std::endl;
            const char* done_marker = "data: [DONE]\n\n";
            sink.write(done_marker, strlen(done_marker));
        }

        // Explicitly flush and signal completion
        sink.done();

        LOG(INFO, "Server") << "Streaming completed - 200 OK" << std::endl;

        // Parse telemetry from buffered data
        auto telemetry = parse_telemetry(telemetry_buffer);
        telemetry.print();

        if (on_complete) {
            on_complete(telemetry);
        }
    } else {
        // Properly terminate the chunked response even on error
        sink.done();
    }
}

void StreamingProxy::forward_byte_stream(
    const std::string& backend_url,
    const std::string& request_body,
    httplib::DataSink& sink,
    long timeout_seconds) {

    bool stream_error = false;

    // Use HttpClient to stream from backend
    auto result = utils::HttpClient::post_stream(
        backend_url,
        request_body,
        [&sink](const char* data, size_t length) {
            // Forward chunk to client immediately
            if (!sink.write(data, length)) {
                return false; // Client disconnected
            }

            return true; // Continue streaming
        },
        {}, // Empty headers map
        timeout_seconds
    );

    if (result.status_code != 200) {
        stream_error = true;
        LOG(ERROR, "StreamingProxy") << "Backend returned error: " << result.status_code << std::endl;
    }

    if (!stream_error) {
        // Explicitly flush and signal completion
        sink.done();
        LOG(INFO, "Server") << "Streaming completed - 200 OK" << std::endl;
    } else {
        // Properly terminate the chunked response even on error
        sink.done();
    }
}

StreamingProxy::TelemetryData StreamingProxy::parse_telemetry(const std::string& buffer) {
    TelemetryData telemetry;

    std::istringstream stream(buffer);
    std::string line;
    json last_chunk_with_usage;

    while (std::getline(stream, line)) {
        // Handle SSE format (data: ...)
        std::string json_str;
        if (line.find("data: ") == 0) {
            json_str = line.substr(6); // Remove "data: " prefix
        } else if (line.find("ChatCompletionChunk: ") == 0) {
            // FLM debug format
            json_str = line.substr(21); // Remove "ChatCompletionChunk: " prefix
        }

        if (!json_str.empty() && json_str != "[DONE]") {
            try {
                auto chunk = json::parse(json_str);
                // Look for usage or timings in the chunk
                if (chunk.contains("usage") || chunk.contains("timings")) {
                    last_chunk_with_usage = chunk;
                }
            } catch (...) {
                // Skip invalid JSON
            }
        }
    }

    // Extract telemetry from the last chunk with usage data
    if (!last_chunk_with_usage.empty()) {
        try {
            if (last_chunk_with_usage.contains("usage")) {
                auto usage = last_chunk_with_usage["usage"];

                if (usage.contains("prompt_tokens")) {
                    telemetry.input_tokens = usage["prompt_tokens"].get<int>();
                }
                if (usage.contains("completion_tokens")) {
                    telemetry.output_tokens = usage["completion_tokens"].get<int>();
                }

                // FLM format
                if (usage.contains("prefill_duration_ttft")) {
                    telemetry.time_to_first_token = usage["prefill_duration_ttft"].get<double>();
                }
                if (usage.contains("decoding_speed_tps")) {
                    telemetry.tokens_per_second = usage["decoding_speed_tps"].get<double>();
                }
            }

            // Alternative format (timings)
            if (last_chunk_with_usage.contains("timings")) {
                auto timings = last_chunk_with_usage["timings"];

                if (timings.contains("prompt_n")) {
                    telemetry.input_tokens = timings["prompt_n"].get<int>();
                }
                if (timings.contains("predicted_n")) {
                    telemetry.output_tokens = timings["predicted_n"].get<int>();
                }
                if (timings.contains("prompt_ms")) {
                    telemetry.time_to_first_token = timings["prompt_ms"].get<double>() / 1000.0;
                }
                if (timings.contains("predicted_per_second")) {
                    telemetry.tokens_per_second = timings["predicted_per_second"].get<double>();
                }
            }
        } catch (const std::exception& e) {
            LOG(ERROR, "StreamingProxy") << "Error parsing telemetry: " << e.what() << std::endl;
        }
    }

    return telemetry;
}

} // namespace lemon
