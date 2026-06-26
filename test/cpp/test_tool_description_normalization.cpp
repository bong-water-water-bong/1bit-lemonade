#include "lemon/backends/llamacpp_server.h"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace lemon::backends;

static void test_description_added_when_missing() {
    json tools = json::parse(R"([
        {"type": "function", "function": {"name": "lpnext", "parameters": {"type": "object", "properties": {}, "required": []}}}
    ])");

    json request;
    request["tools"] = tools;
    request["messages"] = json::array();

    LlamaCppServer::normalize_tools(request);

    assert(request["tools"][0]["function"]["description"] == "");
    assert(request["tools"][0]["function"]["name"] == "lpnext");
    std::cout << "PASS: description added when missing" << std::endl;
}

static void test_existing_description_preserved() {
    json tools = json::parse(R"([
        {"type": "function", "function": {"name": "get_weather", "description": "Gets the weather", "parameters": {"type": "object", "properties": {}, "required": []}}}
    ])");

    json request;
    request["tools"] = tools;
    request["messages"] = json::array();

    LlamaCppServer::normalize_tools(request);

    assert(request["tools"][0]["function"]["description"] == "Gets the weather");
    std::cout << "PASS: existing description preserved" << std::endl;
}

static void test_null_description_filled() {
    // Explicit description: null should be replaced with empty string
    json tools = json::parse(R"([
        {"type": "function", "function": {"name": "lpnext", "description": null, "parameters": {}}}
    ])");

    json request;
    request["tools"] = tools;
    request["messages"] = json::array();

    LlamaCppServer::normalize_tools(request);
    assert(request["tools"][0]["function"]["description"] == "");
    std::cout << "PASS: null description filled with empty string" << std::endl;
}

static void test_non_array_tools_not_crash() {
    json request;
    request["tools"] = "not-an-array";
    request["messages"] = json::array();

    LlamaCppServer::normalize_tools(request); // Should not throw
    assert(request["tools"] == "not-an-array");
    std::cout << "PASS: non-array tools key not crash" << std::endl;
}

static void test_non_object_tool_in_array_not_crash() {
    json request;
    request["tools"] = json::parse(R"(["string-tool", 42, null])");
    request["messages"] = json::array();

    LlamaCppServer::normalize_tools(request); // Should not throw
    std::cout << "PASS: non-object tool entries not crash" << std::endl;
}

static void test_tool_without_function_not_crash() {
    json request;
    request["tools"] = json::parse(R"([
        {"type": "function", "function": {"name": "valid", "parameters": {}}},
        {"type": "function"}
    ])");
    request["messages"] = json::array();

    LlamaCppServer::normalize_tools(request); // Should not throw
    assert(request["tools"][0]["function"]["description"] == "");
    // Second tool without "function" should be left alone
    assert(!request["tools"][1].contains("function"));
    std::cout << "PASS: tool without function key not crash" << std::endl;
}

static void test_tool_without_parameters_not_crash() {
    json request;
    request["tools"] = json::parse(R"([
        {"type": "function", "function": {"name": "minimal"}}
    ])");
    request["messages"] = json::array();

    LlamaCppServer::normalize_tools(request);
    assert(request["tools"][0]["function"]["description"] == "");
    assert(request["tools"][0]["function"]["name"] == "minimal");
    std::cout << "PASS: tool without parameters field handled" << std::endl;
}

static void test_empty_tools_unaffected() {
    json request;
    request["tools"] = json::array();
    request["messages"] = json::array();

    LlamaCppServer::normalize_tools(request);

    assert(request["tools"].empty());
    std::cout << "PASS: empty tools unaffected" << std::endl;
}

static void test_no_tools_unaffected() {
    json request;
    request["messages"] = json::array();

    LlamaCppServer::normalize_tools(request);

    assert(!request.contains("tools"));
    std::cout << "PASS: no tools key unaffected" << std::endl;
}

static void test_multiple_tools_mixed() {
    json tools = json::parse(R"([
        {"type": "function", "function": {"name": "a", "description": "Has desc", "parameters": {}}},
        {"type": "function", "function": {"name": "b", "parameters": {}}}
    ])");

    json request;
    request["tools"] = tools;
    request["messages"] = json::array();

    LlamaCppServer::normalize_tools(request);

    assert(request["tools"][0]["function"]["description"] == "Has desc");
    assert(request["tools"][1]["function"]["description"] == "");
    std::cout << "PASS: mixed tools handled correctly" << std::endl;
}

int main() {
    test_description_added_when_missing();
    test_existing_description_preserved();
    test_null_description_filled();
    test_non_array_tools_not_crash();
    test_non_object_tool_in_array_not_crash();
    test_tool_without_function_not_crash();
    test_tool_without_parameters_not_crash();
    test_empty_tools_unaffected();
    test_no_tools_unaffected();
    test_multiple_tools_mixed();
    std::cout << "All tool description normalization tests PASSED" << std::endl;
    return 0;
}
