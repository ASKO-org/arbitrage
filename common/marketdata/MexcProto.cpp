#include "marketdata/MexcProto.h"

#include <stdexcept>

uint64_t ProtoFieldReader::readVarint() {
    uint64_t result = 0;
    int shift = 0;
    while (pos_ < bytes_.size()) {
        const uint8_t byte = static_cast<uint8_t>(bytes_[pos_++]);
        result |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) return result;
        shift += 7;
    }
    throw std::runtime_error("ProtoFieldReader: truncated varint");
}

bool ProtoFieldReader::next() {
    if (pos_ >= bytes_.size()) return false;

    const uint64_t tag = readVarint();
    fieldNumber_ = static_cast<int>(tag >> 3);
    wireType_ = static_cast<int>(tag & 0x7);

    switch (wireType_) {
        case 0:  // varint — value consumed and discarded unless needed later
            readVarint();
            break;
        case 1:  // 64-bit fixed
            valueStart_ = pos_;
            valueLen_ = 8;
            pos_ += 8;
            break;
        case 2: {  // length-delimited: string, bytes, or embedded submessage
            const uint64_t length = readVarint();
            valueStart_ = pos_;
            valueLen_ = length;
            pos_ += length;
            break;
        }
        case 5:  // 32-bit fixed
            valueStart_ = pos_;
            valueLen_ = 4;
            pos_ += 4;
            break;
        default:
            throw std::runtime_error("ProtoFieldReader: unsupported wire type");
    }

    if (pos_ > bytes_.size()) throw std::runtime_error("ProtoFieldReader: field runs past buffer");
    return true;
}

std::string ProtoFieldReader::bytesValue() const { return bytes_.substr(valueStart_, valueLen_); }
