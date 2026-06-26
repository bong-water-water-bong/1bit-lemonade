#include "lemon/utils/github_rate_limit.h"
#include "lemon/utils/http_client.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    assert(argc == 2);

    auto response = lemon::utils::HttpClient::get(argv[1]);
    assert(response.status_code == 403);

    const auto now = std::chrono::system_clock::from_time_t(1700000000);
    auto until = lemon::utils::github_rate_limit_backoff_until(response, now);

    assert(until.has_value());
    assert(std::chrono::system_clock::to_time_t(*until) == 1700000120);

    std::cout << "HttpClient header capture test passed\n";
    return 0;
}
