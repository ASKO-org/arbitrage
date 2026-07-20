#include "core/QuoteSnapshotFileWriter.h"

#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>
#include <iostream>
#include <thread>

#include "core/QuoteSnapshotFileFormat.h"

QuoteSnapshotFileWriter::QuoteSnapshotFileWriter(std::string outputDir, std::size_t flushThreshold)
    : outputDir_(std::move(outputDir)), flushThreshold_(flushThreshold) {
    std::filesystem::create_directories(outputDir_);
}

QuoteSnapshotFileWriter::~QuoteSnapshotFileWriter() { flushAll(); }

std::string QuoteSnapshotFileWriter::filePath(const std::string& exchangeName,
                                               const std::string& day) const {
    return quoteSnapshotFilePath(outputDir_, exchangeName, day);
}

void QuoteSnapshotFileWriter::openBucketFile(Bucket& bucket, const std::string& exchangeName,
                                              const std::string& day) {
    const std::string path = filePath(exchangeName, day);
    const bool isNewFile = !std::filesystem::exists(path);

    bucket.file.open(path, std::ios::binary | std::ios::out | std::ios::app);
    if (!bucket.file.is_open()) {
        throw std::runtime_error("failed to open " + path);
    }
    bucket.file.exceptions(std::ios::failbit | std::ios::badbit);

    if (isNewFile) {
        const std::uint16_t exchangeNameLen = static_cast<std::uint16_t>(exchangeName.size());
        bucket.file.write(kQuoteSnapshotMagic, sizeof(kQuoteSnapshotMagic));
        bucket.file.write(reinterpret_cast<const char*>(&kQuoteSnapshotFormatVersion),
                           sizeof(kQuoteSnapshotFormatVersion));
        bucket.file.write(reinterpret_cast<const char*>(&exchangeNameLen), sizeof(exchangeNameLen));
        bucket.file.write(exchangeName.data(), static_cast<std::streamsize>(exchangeName.size()));
        bucket.file.flush();
    }

    bucket.path = path;
    bucket.day = day;
}

void QuoteSnapshotFileWriter::writeRecord(std::ofstream& out, const QuoteSnapshotRow& row) {
    const std::uint16_t symbolCodeLen = static_cast<std::uint16_t>(row.symbolCode.size());
    const std::int64_t quoteTimeMicros = std::chrono::duration_cast<std::chrono::microseconds>(
                                              row.quoteTime.time_since_epoch())
                                              .count();

    out.write(reinterpret_cast<const char*>(&symbolCodeLen), sizeof(symbolCodeLen));
    out.write(row.symbolCode.data(), static_cast<std::streamsize>(row.symbolCode.size()));
    out.write(reinterpret_cast<const char*>(&quoteTimeMicros), sizeof(quoteTimeMicros));
    out.write(reinterpret_cast<const char*>(&row.bidPrice), sizeof(row.bidPrice));
    out.write(reinterpret_cast<const char*>(&row.bidQty), sizeof(row.bidQty));
    out.write(reinterpret_cast<const char*>(&row.askPrice), sizeof(row.askPrice));
    out.write(reinterpret_cast<const char*>(&row.askQty), sizeof(row.askQty));
}

void QuoteSnapshotFileWriter::flushBucket(Bucket& bucket) {
    if (bucket.buffer.empty()) return;

    try {
        for (const auto& row : bucket.buffer) writeRecord(bucket.file, row);
        bucket.file.flush();
    } catch (const std::exception& ex) {
        std::cerr << "QuoteSnapshotFileWriter: failed to write " << bucket.path << ": " << ex.what()
                   << "\n";
    }

    bucket.buffer.clear();
}

void QuoteSnapshotFileWriter::compressRotatedFile(std::string path) {
    // Defense in depth beyond the forward-only rotation check in record():
    // never fork a zstd process for a file that's already gone (compressed
    // by an earlier call, or removed some other way).
    if (!std::filesystem::exists(path)) return;

    const pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "QuoteSnapshotFileWriter: fork() failed compressing " << path << "\n";
        return;
    }
    if (pid == 0) {
        // Child: replace this process image with zstd; -f lets a retry
        // overwrite a stale partial .zst left by an earlier interrupted
        // attempt, --rm only deletes the source after successful
        // compression (so a crash mid-compression leaves it intact).
        execlp("zstd", "zstd", "-3", "--rm", "-f", path.c_str(), static_cast<char*>(nullptr));
        _exit(127);  // only reached if execlp itself failed
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::cerr << "QuoteSnapshotFileWriter: zstd compression failed for " << path << "\n";
    }
}

void QuoteSnapshotFileWriter::record(const QuoteSnapshotRow& row) {
    const std::string day = quoteSnapshotDayKey(row.quoteTime);
    Bucket& bucket = buckets_[row.exchangeName];

    // Forward-only: a quote can arrive with a day *earlier* than the
    // currently-open bucket if some symbol simply hasn't updated since
    // before the rotation (QuoteStore only tracks "latest received," not
    // "most recent for today" — a stale-but-still-fresh-enough-for-TTL
    // quote from just before midnight keeps reporting yesterday's
    // timestamp). Using `!=` here treated that as "rotate back to
    // yesterday," and the next fresh quote would then trigger rotating
    // forward again — an unbounded oscillation, observed in production
    // spawning dozens of racing zstd compressions on the same file.
    // `>` makes rotation strictly forward: a late/stale row just lands in
    // whatever's currently open instead of reopening an old day.
    if (day > bucket.day) {
        if (bucket.file.is_open()) {
            flushBucket(bucket);
            bucket.file.close();
            std::thread(&QuoteSnapshotFileWriter::compressRotatedFile, bucket.path).detach();
        }
        try {
            openBucketFile(bucket, row.exchangeName, day);
        } catch (const std::exception& ex) {
            std::cerr << "QuoteSnapshotFileWriter: failed to open snapshot file for "
                       << row.exchangeName << ": " << ex.what() << "\n";
            return;
        }
    }

    bucket.buffer.push_back(row);
    if (bucket.buffer.size() >= flushThreshold_) flushBucket(bucket);
}

void QuoteSnapshotFileWriter::recordAll(const std::vector<QuoteSnapshotRow>& rows) {
    for (const auto& row : rows) record(row);
}

void QuoteSnapshotFileWriter::flushAll() {
    for (auto& [exchangeName, bucket] : buckets_) flushBucket(bucket);
}
