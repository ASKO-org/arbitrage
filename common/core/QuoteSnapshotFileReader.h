#pragma once
#include <chrono>
#include <istream>
#include <string>
#include <vector>

#include "models/QuoteSnapshotRow.h"

// Reads back quote snapshots previously written by QuoteSnapshotFileWriter
// from "<snapshotDir>/<exchangeName>_<YYYYMMDD>.bin" (today's, always plain)
// or "<snapshotDir>/<exchangeName>_<YYYYMMDD>.bin.zst" (older, rotated days —
// see QuoteSnapshotFileWriter::compressRotatedFile) files. See
// QuoteSnapshotFileFormat.h for the shared on-disk format.
//
// Not thread-safe by design: each read() opens/closes its own file handles,
// so concurrent calls touching different files are fine, but concurrent
// calls racing the same file are out of scope.
class QuoteSnapshotFileReader {
public:
    explicit QuoteSnapshotFileReader(std::string snapshotDir);

    // Returns every recorded row for exchangeName/symbolCode whose quoteTime
    // falls within [from, to] (inclusive), in chronological (file) order.
    // Days with no file (compressed or plain) are silently skipped. A
    // missing/malformed header, a truncated trailing record (e.g. process
    // killed mid-write), or a corrupt/undecodable .zst is logged to
    // std::cerr and only cuts off that one day's data, not the whole read.
    std::vector<QuoteSnapshotRow> read(const std::string& exchangeName,
                                        const std::string& symbolCode,
                                        std::chrono::system_clock::time_point from,
                                        std::chrono::system_clock::time_point to) const;

private:
    void readDayFile(const std::string& path, const std::string& exchangeName,
                      const std::string& symbolCode, std::chrono::system_clock::time_point from,
                      std::chrono::system_clock::time_point to,
                      std::vector<QuoteSnapshotRow>& out) const;

    // Decompresses compressedPath (a "*.bin.zst") in-memory via the system
    // `zstd` CLI and parses it the same way as an uncompressed day file. A
    // failed/corrupt decompression logs to std::cerr and leaves out
    // unchanged for this day.
    void readCompressedDayFile(const std::string& compressedPath, const std::string& exchangeName,
                                const std::string& symbolCode,
                                std::chrono::system_clock::time_point from,
                                std::chrono::system_clock::time_point to,
                                std::vector<QuoteSnapshotRow>& out) const;

    // Shared parse loop (magic check, header skip, per-record read) used by
    // both readDayFile() (over a real ifstream) and readCompressedDayFile()
    // (over an istringstream of decompressed bytes) — std::ifstream and
    // std::istringstream both satisfy std::istream, and every operation
    // used here (.read(), .gcount(), .seekg(), the `while (in)` check)
    // works identically on either.
    void parseStream(std::istream& in, const std::string& path, const std::string& exchangeName,
                      const std::string& symbolCode, std::chrono::system_clock::time_point from,
                      std::chrono::system_clock::time_point to,
                      std::vector<QuoteSnapshotRow>& out) const;

    std::string snapshotDir_;
};
