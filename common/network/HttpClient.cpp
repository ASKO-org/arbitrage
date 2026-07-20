#include "network/HttpClient.h"

#include <curl/curl.h>
#include <stdexcept>

namespace {

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string perform(CURL* curl, const std::string& url, const std::string& method, const std::string& body,
                     const std::vector<std::string>& headers) {
    std::string responseBody;
    char errorBuffer[CURL_ERROR_SIZE] = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "instrument_loader/1.0");
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    } else if (method != "GET") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        if (!body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        }
    }

    curl_slist* headerList = nullptr;
    for (const auto& header : headers) {
        headerList = curl_slist_append(headerList, header.c_str());
    }
    if (headerList) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }

    CURLcode res = curl_easy_perform(curl);
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);

    if (headerList) curl_slist_free_all(headerList);

    if (res != CURLE_OK) {
        const std::string reason = errorBuffer;
        curl_easy_cleanup(curl);
        throw std::runtime_error("HttpClient: request to " + url + " failed: " + reason);
    }
    if (statusCode < 200 || statusCode >= 300) {
        curl_easy_cleanup(curl);
        throw std::runtime_error("HttpClient: request to " + url + " returned status " +
                                  std::to_string(statusCode) + ": " + responseBody);
    }

    curl_easy_cleanup(curl);
    return responseBody;
}

}  // namespace

std::string HttpClient::get(const std::string& url) { return request("GET", url, "", {}); }

std::string HttpClient::post(const std::string& url, const std::string& body) {
    return request("POST", url, body, {});
}

std::string HttpClient::request(const std::string& method, const std::string& url, const std::string& body,
                                 const std::vector<std::string>& headers) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("HttpClient: failed to initialize curl handle");
    }
    return perform(curl, url, method, body, headers);
}
