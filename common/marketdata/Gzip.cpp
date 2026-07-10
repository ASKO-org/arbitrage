#include "marketdata/Gzip.h"

#include <stdexcept>

#include <zlib.h>

std::string gunzip(const std::string& compressed) {
    z_stream stream{};
    // 16 + MAX_WBITS tells zlib to expect a gzip header specifically, rather
    // than raw deflate or zlib-wrapped data.
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
        throw std::runtime_error("gunzip: inflateInit2 failed");
    }

    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());

    std::string output;
    char buffer[8192];
    int result;
    do {
        stream.next_out = reinterpret_cast<Bytef*>(buffer);
        stream.avail_out = sizeof(buffer);
        result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) {
            inflateEnd(&stream);
            throw std::runtime_error("gunzip: inflate failed");
        }
        output.append(buffer, sizeof(buffer) - stream.avail_out);
    } while (result != Z_STREAM_END);

    inflateEnd(&stream);
    return output;
}
