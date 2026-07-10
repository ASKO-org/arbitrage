#include "network/HttpClient.h"

#include <curl/curl.h>
#include <stdexcept>

namespace {

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string perform(CURL* curl, const std::string& url) {
    std::string body;
    char errorBuffer[CURL_ERROR_SIZE] = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "instrument_loader/1.0");
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);

    CURLcode res = curl_easy_perform(curl);
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);

    if (res != CURLE_OK) {
        const std::string reason = errorBuffer;
        curl_easy_cleanup(curl);
        throw std::runtime_error("HttpClient: request to " + url + " failed: " + reason);
    }
    if (statusCode < 200 || statusCode >= 300) {
        curl_easy_cleanup(curl);
        throw std::runtime_error("HttpClient: request to " + url + " returned status " +
                                  std::to_string(statusCode));
    }

    curl_easy_cleanup(curl);
    return body;
}

}  // namespace

std::string HttpClient::get(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("HttpClient: failed to initialize curl handle");
    }
    return perform(curl, url);
}

std::string HttpClient::post(const std::string& url, const std::string& body) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("HttpClient: failed to initialize curl handle");
    }
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    return perform(curl, url);
}
