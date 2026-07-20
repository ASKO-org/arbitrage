#pragma once
#include <string>

// HMAC-SHA256 signing shared by every venue's authenticated trading
// connector (Binance/Bybit both sign a request string with the account's
// API secret).
namespace HmacSigner {

// Returns the lowercase hex-encoded HMAC-SHA256 of `message` keyed by `secret`.
std::string hmacSha256Hex(const std::string& secret, const std::string& message);

}  // namespace HmacSigner
