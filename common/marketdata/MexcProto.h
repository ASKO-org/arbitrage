#pragma once
#include <cstdint>
#include <string>

// A minimal length-delimited protobuf wire-format field reader — just
// enough to pull specific string fields out of MEXC's known push-message
// shapes without pulling in a full protobuf dependency for one exchange.
// See https://protobuf.dev/programming-guides/encoding/ for the wire format.
class ProtoFieldReader {
public:
    explicit ProtoFieldReader(const std::string& bytes) : bytes_(bytes) {}

    // Advances to the next field; returns false once the buffer is exhausted.
    bool next();

    int fieldNumber() const { return fieldNumber_; }
    int wireType() const { return wireType_; }

    // Valid after next() when wireType() == 2 (length-delimited: string,
    // bytes, or an embedded submessage).
    std::string bytesValue() const;

private:
    const std::string& bytes_;
    size_t pos_ = 0;
    int fieldNumber_ = 0;
    int wireType_ = 0;
    size_t valueStart_ = 0;
    size_t valueLen_ = 0;

    uint64_t readVarint();
};
