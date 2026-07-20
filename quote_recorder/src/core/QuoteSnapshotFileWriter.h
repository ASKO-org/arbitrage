#pragma once
#include <cstddef>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "models/QuoteSnapshotRow.h"

// Buffers recorded quote snapshots in memory, one bucket per exchange, and
// appends each bucket's buffer to "<outputDir>/<exchangeName>_<YYYYMMDD>.bin"
// (the day derived from each row's own quoteTime, in UTC) once the buffer
// reaches flushThreshold rows, or on flushAll()/destruction.
//
// This is an internal, single-machine binary format (native/little-endian,
// not designed for cross-endian portability): each file starts with a
// header ("QSNP" magic, uint16 version, uint16 exchangeNameLen, exchange
// name bytes), followed by fixed-layout records — uint16 symbolCodeLen,
// symbolCode bytes, int64 quoteTimeMicros (UTC epoch), then bidPrice,
// bidQty, askPrice, askQty as raw doubles.
//
// Not internally synchronized: every method must be called from a single
// thread. record()/recordAll()/flushAll() never throw; on any I/O failure
// they log to std::cerr and drop the affected batch, matching the "log and
// drop" convention previously used around DatabaseRepository failures.
class QuoteSnapshotFileWriter {
public:
    QuoteSnapshotFileWriter(std::string outputDir, std::size_t flushThreshold);
    ~QuoteSnapshotFileWriter();

    QuoteSnapshotFileWriter(const QuoteSnapshotFileWriter&) = delete;
    QuoteSnapshotFileWriter& operator=(const QuoteSnapshotFileWriter&) = delete;

    // Buffers one row into its (exchangeName, day-of(row.quoteTime)) bucket;
    // flushes that bucket's file if the buffer has reached flushThreshold.
    void record(const QuoteSnapshotRow& row);

    // Convenience: record() for every row in rows.
    void recordAll(const std::vector<QuoteSnapshotRow>& rows);

    // Writes every bucket's currently-buffered rows to their files and
    // clears the buffers (files stay open). Safe to call repeatedly.
    void flushAll();

private:
    struct Bucket {
        std::ofstream file;
        std::string path;
        std::string day;  // "YYYYMMDD" the currently-open file covers; empty if none open yet
        std::vector<QuoteSnapshotRow> buffer;
    };

    std::string filePath(const std::string& exchangeName, const std::string& day) const;
    void openBucketFile(Bucket& bucket, const std::string& exchangeName, const std::string& day);
    void flushBucket(Bucket& bucket);
    static void writeRecord(std::ofstream& out, const QuoteSnapshotRow& row);

    // Compresses a just-rotated (no longer appended-to) day file in place
    // via the system `zstd` CLI (`zstd -3 --rm -f`), replacing
    // "<path>" with "<path>.zst". Runs on its own detached thread (started
    // by record() on rotation) so the ~200ms recording cadence is never
    // blocked waiting on it. Logs to std::cerr and leaves the source file
    // untouched on failure, matching this class's existing "log and drop"
    // convention.
    static void compressRotatedFile(std::string path);

    std::string outputDir_;
    std::size_t flushThreshold_;
    std::unordered_map<std::string, Bucket> buckets_;  // keyed by exchangeName
};
