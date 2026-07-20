#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "core/SpreadSeriesBuilder.h"

namespace {

[[noreturn]] void printUsageAndExit() {
    std::cerr << "Usage: spread_builder --dir <snapshotDir> --symbol <code> "
                 "--exchange1 <name> --exchange2 <name> --from <ISO8601> --to <ISO8601> "
                 "--output <path.json> [--interval-ms <ms>]\n"
                 "Example: spread_builder --dir ./data/quote_snapshots --symbol BTCUSDT "
                 "--exchange1 BINANCE_Spot --exchange2 BYBIT_Spot "
                 "--from 2026-07-14T00:00:00Z --to 2026-07-15T00:00:00Z --output spread.json\n";
    std::exit(1);
}

std::string requireArg(int argc, char** argv, int& i) {
    if (i + 1 >= argc) printUsageAndExit();
    return argv[++i];
}

// Parses "YYYY-MM-DDTHH:MM:SS[Z]" as a UTC timestamp (a trailing 'Z', if
// present, is simply left unconsumed by get_time and ignored).
std::chrono::system_clock::time_point parseIso8601(const std::string& text) {
    std::tm tm{};
    std::istringstream in(text);
    in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (in.fail()) {
        std::cerr << "Invalid timestamp (expected e.g. 2026-07-14T00:00:00Z): " << text << "\n";
        std::exit(1);
    }
    return std::chrono::system_clock::from_time_t(timegm(&tm));
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string dir, symbol, exchange1, exchange2, fromText, toText, outputPath;
        std::chrono::milliseconds interval{1000};
        bool haveDir = false, haveSymbol = false, haveExchange1 = false, haveExchange2 = false,
             haveFrom = false, haveTo = false, haveOutput = false;

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--dir") {
                dir = requireArg(argc, argv, i);
                haveDir = true;
            } else if (arg == "--symbol") {
                symbol = requireArg(argc, argv, i);
                haveSymbol = true;
            } else if (arg == "--exchange1") {
                exchange1 = requireArg(argc, argv, i);
                haveExchange1 = true;
            } else if (arg == "--exchange2") {
                exchange2 = requireArg(argc, argv, i);
                haveExchange2 = true;
            } else if (arg == "--from") {
                fromText = requireArg(argc, argv, i);
                haveFrom = true;
            } else if (arg == "--to") {
                toText = requireArg(argc, argv, i);
                haveTo = true;
            } else if (arg == "--output") {
                outputPath = requireArg(argc, argv, i);
                haveOutput = true;
            } else if (arg == "--interval-ms") {
                interval = std::chrono::milliseconds(std::stoi(requireArg(argc, argv, i)));
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                printUsageAndExit();
            }
        }

        if (!haveDir || !haveSymbol || !haveExchange1 || !haveExchange2 || !haveFrom || !haveTo ||
            !haveOutput) {
            printUsageAndExit();
        }
        if (exchange1 == exchange2) {
            std::cerr << "--exchange1 and --exchange2 must be different\n";
            return 1;
        }

        const auto from = parseIso8601(fromText);
        const auto to = parseIso8601(toText);

        SpreadSeriesBuilder builder(dir);
        const auto points = builder.build(symbol, exchange1, exchange2, from, to, interval);

        nlohmann::json pointsJson = nlohmann::json::array();
        for (const auto& point : points) {
            const auto timeSeconds =
                std::chrono::duration_cast<std::chrono::seconds>(point.time.time_since_epoch())
                    .count();
            pointsJson.push_back({
                {"time", timeSeconds},
                {"entry_spread_bps", point.entrySpreadBps},
                {"entry_buy_price", point.entryBuyPrice},
                {"entry_sell_price", point.entrySellPrice},
                {"exit_spread_bps", point.exitSpreadBps},
                {"exit_buy_price", point.exitBuyPrice},
                {"exit_sell_price", point.exitSellPrice},
            });
        }

        const nlohmann::json output = {
            {"symbol", symbol},
            {"exchange1", exchange1},
            {"exchange2", exchange2},
            {"interval_ms", interval.count()},
            {"points", pointsJson},
        };

        std::ofstream out(outputPath);
        if (!out.is_open()) {
            std::cerr << "Failed to open output file: " << outputPath << "\n";
            return 1;
        }
        out << output.dump(2);

        std::cout << "Wrote " << points.size() << " spread points to " << outputPath << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
