#include "core/QuoteSnapshotFileReader.h"

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "core/QuoteSnapshotFileFormat.h"

QuoteSnapshotFileReader::QuoteSnapshotFileReader(std::string snapshotDir)
    : snapshotDir_(std::move(snapshotDir)) {}

void QuoteSnapshotFileReader::parseStream(std::istream& in, const std::string& path,
                                           const std::string& exchangeName,
                                           const std::string& symbolCode,
                                           std::chrono::system_clock::time_point from,
                                           std::chrono::system_clock::time_point to,
                                           std::vector<QuoteSnapshotRow>& out) const {
    char magic[4];
    in.read(magic, sizeof(magic));
    if (in.gcount() != sizeof(magic) || std::memcmp(magic, kQuoteSnapshotMagic, sizeof(magic)) != 0) {
        std::cerr << "QuoteSnapshotFileReader: " << path << " has an invalid header, skipping\n";
        return;
    }

    std::uint16_t version = 0;
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    std::uint16_t exchangeNameLen = 0;
    in.read(reinterpret_cast<char*>(&exchangeNameLen), sizeof(exchangeNameLen));
    if (in.gcount() != sizeof(exchangeNameLen)) {
        std::cerr << "QuoteSnapshotFileReader: " << path << " has a truncated header, skipping\n";
        return;
    }
    in.seekg(exchangeNameLen, std::ios::cur);
    if (!in) {
        std::cerr << "QuoteSnapshotFileReader: " << path << " has a truncated header, skipping\n";
        return;
    }

    while (in) {
        std::uint16_t symbolCodeLen = 0;
        in.read(reinterpret_cast<char*>(&symbolCodeLen), sizeof(symbolCodeLen));
        if (in.gcount() == 0) break;  // clean EOF at a record boundary
        if (in.gcount() != sizeof(symbolCodeLen)) {
            std::cerr << "QuoteSnapshotFileReader: " << path << " has a truncated trailing record, "
                       << "stopping\n";
            break;
        }

        std::string readSymbolCode(symbolCodeLen, '\0');
        in.read(readSymbolCode.data(), symbolCodeLen);
        std::int64_t quoteTimeMicros = 0;
        double bidPrice = 0.0, bidQty = 0.0, askPrice = 0.0, askQty = 0.0;
        in.read(reinterpret_cast<char*>(&quoteTimeMicros), sizeof(quoteTimeMicros));
        in.read(reinterpret_cast<char*>(&bidPrice), sizeof(bidPrice));
        in.read(reinterpret_cast<char*>(&bidQty), sizeof(bidQty));
        in.read(reinterpret_cast<char*>(&askPrice), sizeof(askPrice));
        in.read(reinterpret_cast<char*>(&askQty), sizeof(askQty));
        if (!in) {
            std::cerr << "QuoteSnapshotFileReader: " << path << " has a truncated trailing record, "
                       << "stopping\n";
            break;
        }

        if (readSymbolCode != symbolCode) continue;

        const auto quoteTime = std::chrono::system_clock::time_point{
            std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::microseconds{quoteTimeMicros})};
        if (quoteTime < from || quoteTime > to) continue;

        out.push_back(QuoteSnapshotRow{exchangeName, readSymbolCode, bidPrice, bidQty, askPrice,
                                        askQty, quoteTime});
    }
}

void QuoteSnapshotFileReader::readDayFile(const std::string& path, const std::string& exchangeName,
                                           const std::string& symbolCode,
                                           std::chrono::system_clock::time_point from,
                                           std::chrono::system_clock::time_point to,
                                           std::vector<QuoteSnapshotRow>& out) const {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "QuoteSnapshotFileReader: failed to open " << path << "\n";
        return;
    }
    parseStream(file, path, exchangeName, symbolCode, from, to, out);
}

void QuoteSnapshotFileReader::readCompressedDayFile(const std::string& compressedPath,
                                                     const std::string& exchangeName,
                                                     const std::string& symbolCode,
                                                     std::chrono::system_clock::time_point from,
                                                     std::chrono::system_clock::time_point to,
                                                     std::vector<QuoteSnapshotRow>& out) const {
    int pipeFds[2];
    if (pipe(pipeFds) != 0) {
        std::cerr << "QuoteSnapshotFileReader: pipe() failed decompressing " << compressedPath << "\n";
        return;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "QuoteSnapshotFileReader: fork() failed decompressing " << compressedPath << "\n";
        close(pipeFds[0]);
        close(pipeFds[1]);
        return;
    }
    if (pid == 0) {
        // Child: stdout -> the pipe's write end, then replace this process
        // image with zstd -d -c (decompress to stdout, leave the .zst file
        // untouched).
        close(pipeFds[0]);
        dup2(pipeFds[1], STDOUT_FILENO);
        close(pipeFds[1]);
        execlp("zstd", "zstd", "-d", "-c", compressedPath.c_str(), static_cast<char*>(nullptr));
        _exit(127);  // only reached if execlp itself failed
    }

    close(pipeFds[1]);
    std::string decompressed;
    std::array<char, 65536> chunk{};
    ssize_t bytesRead = 0;
    while ((bytesRead = ::read(pipeFds[0], chunk.data(), chunk.size())) > 0) {
        decompressed.append(chunk.data(), static_cast<std::size_t>(bytesRead));
    }
    close(pipeFds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::cerr << "QuoteSnapshotFileReader: zstd decompression failed for " << compressedPath
                   << ", skipping\n";
        return;
    }

    std::istringstream decompressedStream(decompressed);
    parseStream(decompressedStream, compressedPath, exchangeName, symbolCode, from, to, out);
}

std::vector<QuoteSnapshotRow> QuoteSnapshotFileReader::read(
    const std::string& exchangeName, const std::string& symbolCode,
    std::chrono::system_clock::time_point from, std::chrono::system_clock::time_point to) const {
    std::vector<QuoteSnapshotRow> rows;
    if (from > to) return rows;

    auto dayCursor = from;
    const std::string lastDay = quoteSnapshotDayKey(to);
    while (true) {
        const std::string day = quoteSnapshotDayKey(dayCursor);
        const std::string path = quoteSnapshotFilePath(snapshotDir_, exchangeName, day);
        const std::string compressedPath = path + ".zst";
        if (std::filesystem::exists(path)) {
            readDayFile(path, exchangeName, symbolCode, from, to, rows);
        } else if (std::filesystem::exists(compressedPath)) {
            readCompressedDayFile(compressedPath, exchangeName, symbolCode, from, to, rows);
        }
        if (day == lastDay) break;
        dayCursor += std::chrono::hours(24);
    }

    return rows;
}
