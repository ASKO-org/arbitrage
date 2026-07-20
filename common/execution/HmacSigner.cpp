#include "execution/HmacSigner.h"

#include <openssl/hmac.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>

std::string HmacSigner::hmacSha256Hex(const std::string& secret, const std::string& message) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLength = 0;

    const auto* result =
        HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
             reinterpret_cast<const unsigned char*>(message.data()), message.size(), digest, &digestLength);
    if (!result) {
        throw std::runtime_error("HmacSigner: HMAC computation failed");
    }

    std::ostringstream hex;
    hex << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digestLength; ++i) {
        hex << std::setw(2) << static_cast<int>(digest[i]);
    }
    return hex.str();
}
