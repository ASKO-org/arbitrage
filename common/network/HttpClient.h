#pragma once
#include <string>
#include <vector>

// Minimal synchronous HTTP client backed by libcurl.
class HttpClient {
public:
    // Performs an HTTP GET and returns the response body.
    // Throws std::runtime_error on transport failure or non-2xx status.
    static std::string get(const std::string& url);

    // Performs an HTTP POST with an optional body and returns the response
    // body. Throws std::runtime_error on transport failure or non-2xx status.
    static std::string post(const std::string& url, const std::string& body = "");

    // Performs an arbitrary HTTP method (GET/POST/DELETE/...) with a body and
    // request headers (each a full "Name: value" string) — what authenticated
    // exchange trading endpoints need (e.g. an API-key header) that plain
    // get()/post() don't support. Throws std::runtime_error on transport
    // failure or non-2xx status.
    static std::string request(const std::string& method, const std::string& url, const std::string& body,
                                const std::vector<std::string>& headers);
};
