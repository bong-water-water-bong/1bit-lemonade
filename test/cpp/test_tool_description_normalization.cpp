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
    test_empty_tools_unaffected();
    test_no_tools_unaffected();
    test_multiple_tools_mixed();
    std::cout << "All tool description normalization tests PASSED" << std::endl;
    return 0;
}
