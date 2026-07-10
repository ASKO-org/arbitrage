#pragma once
#include <string>

// Decompresses a gzip byte stream. Throws std::runtime_error on failure.
// Used by HTX's WS connectors, which gzip every push frame even though the
// payload is plain JSON.
std::string gunzip(const std::string& compressed);
