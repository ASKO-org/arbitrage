#include "execution/HmacSigner.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {

// Shared by both encodings below — computes the raw HMAC-SHA256 digest and
// hands back its length; each public function encodes it differently.
unsigned int hmacSha256Digest(const std::string& secret, const std::string& message, unsigned char* digest) {
    unsigned int digestLength = 0;
    const auto* result =
        HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
             reinterpret_cast<const unsigned char*>(message.data()), message.size(), digest, &digestLength);
    if (!result) {
        throw std::runtime_error("HmacSigner: HMAC computation failed");
    }
    return digestLength;
}

}  // namespace

std::string HmacSigner::hmacSha256Hex(const std::string& secret, const std::string& message) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    const unsigned int digestLength = hmacSha256Digest(secret, message, digest);

    std::ostringstream hex;
    hex << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digestLength; ++i) {
        hex << std::setw(2) << static_cast<int>(digest[i]);
    }
    return hex.str();
}

std::string HmacSigner::hmacSha256Base64(const std::string& secret, const std::string& message) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    const unsigned int digestLength = hmacSha256Digest(secret, message, digest);

    // EVP_EncodeBlock NUL-terminates its output and returns the encoded
    // length excluding that terminator.
    std::string encoded(4 * ((digestLength + 2) / 3) + 1, '\0');
    const int encodedLength =
        EVP_EncodeBlock(reinterpret_cast<unsigned char*>(encoded.data()), digest, static_cast<int>(digestLength));
    encoded.resize(static_cast<size_t>(encodedLength));
    return encoded;
}
