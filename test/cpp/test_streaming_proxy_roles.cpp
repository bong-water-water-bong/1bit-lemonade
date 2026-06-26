#include "lemon/streaming_proxy.h"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

static json parse_first_data_json(const std::string& sse) {
    const std::string prefix = "data: ";
    auto start = sse.find(prefix);
    assert(start != std::string::npos);
    start += prefix.size();
    auto end = sse.find('\n', start);
    assert(end != std::string::npos);
    return json::parse(sse.substr(start, end - start));
}

static void test_null_role_is_normalized() {
    std::string input =
        "data: {\"object\":\"chat.completion.chunk\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"hi\",\"role\":null},\"finish_reason\":null}]}\n\n";

    std::string output = lemon::StreamingProxy::normalize_chat_completion_chunk_roles(input);
    auto chunk = parse_first_data_json(output);

    assert(chunk["choices"][0]["delta"]["role"] == "assistant");
    assert(chunk["choices"][0]["delta"]["content"] == "hi");
}

static void test_missing_role_on_content_delta_is_added() {
    std::string input =
        "data: {\"object\":\"chat.completion.chunk\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"hi\"},\"finish_reason\":null}]}\n\n";

    std::string output = lemon::StreamingProxy::normalize_chat_completion_chunk_roles(input);
    auto chunk = parse_first_data_json(output);

    assert(chunk["choices"][0]["delta"]["role"] == "assistant");
}

static void test_done_marker_and_non_chat_chunks_are_preserved() {
    std::string done = "data: [DONE]\n\n";
    assert(lemon::StreamingProxy::normalize_chat_completion_chunk_roles(done) == done);

    std::string completion =
        "data: {\"object\":\"text_completion\",\"choices\":[{\"index\":0,\"text\":\"hi\"}]}\n\n";
    assert(lemon::StreamingProxy::normalize_chat_completion_chunk_roles(completion) == completion);
}

static void test_empty_delta_chunk_is_not_mutated() {
    // A finish-reason-only delta (no assistant payload) should not get a role added.
    std::string input =
        "data: {\"object\":\"chat.completion.chunk\",\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n";
    assert(lemon::StreamingProxy::normalize_chat_completion_chunk_roles(input) == input);
}

static void test_carriage_return_and_thinking_payloads_are_normalized() {
    std::string input =
        "data: {\"object\":\"chat.completion.chunk\",\"choices\":[{\"index\":0,\"delta\":{\"reasoning_content\":\"hmm\",\"role\":null}}]}\r\n\r\n";
    std::string output = lemon::StreamingProxy::normalize_chat_completion_chunk_roles(input);
    auto chunk = parse_first_data_json(output);
    assert(chunk["choices"][0]["delta"]["role"] == "assistant");
    assert(chunk["choices"][0]["delta"]["reasoning_content"] == "hmm");
    assert(output.size() >= 2 && output[output.size() - 2] == '\\r');
}

int main() {
    test_null_role_is_normalized();
    test_missing_role_on_content_delta_is_added();
    test_done_marker_and_non_chat_chunks_are_preserved();
    test_empty_delta_chunk_is_not_mutated();
    test_carriage_return_and_thinking_payloads_are_normalized();
    std::cout << "streaming proxy role normalization tests passed\n";
    return 0;
}
