#include "connectors/KrakenSpotConnector.h"

#include <cmath>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kAssetPairsUrl = "https://api.kraken.com/0/public/AssetPairs";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key) || obj.at(key).is_null()) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}

// Kraken prefixes many legacy assets with X (crypto) or Z (fiat), e.g.
// "XETH", "ZUSD" — without stripping this, canonical symbol codes never
// match the same asset on any other exchange. Bitcoin is the odd one out:
// stripping the prefix gives "XBT", Bitcoin's own legacy ticker, which
// still needs mapping to "BTC" to match every other venue.
std::string normalizeKrakenAsset(const std::string& code) {
    std::string result = code;
    if (result.size() == 4 && (result[0] == 'X' || result[0] == 'Z')) {
        result = result.substr(1);
    }
    if (result == "XBT") result = "BTC";
    return result;
}
}  // namespace

std::string KrakenSpotConnector::exchangeName() const { return "KRAKEN_Spot"; }

std::vector<Instrument> KrakenSpotConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kAssetPairsUrl);
    const auto json = nlohmann::json::parse(body);

    if (!json.value("error", nlohmann::json::array()).empty()) {
        throw std::runtime_error("KrakenSpotConnector: API error: " + json.at("error").dump());
    }

    // "result" is a JSON object keyed by Kraken's internal pair ID, not an
    // array — iterate its entries.
    std::vector<Instrument> instruments;
    for (const auto& [pairId, symbolInfo] : json.at("result").items()) {
        Instrument instrument;
        instrument.exchangeName = exchangeName();
        instrument.baseAsset = normalizeKrakenAsset(symbolInfo.at("base").get<std::string>());
        instrument.quoteAsset = normalizeKrakenAsset(symbolInfo.at("quote").get<std::string>());
        // altname (e.g. "XBTUSD") is Kraken's REST-only legacy spelling and
        // doesn't match what the WS v2 ticker channel expects ("BTC/USD");
        // store the normalized slash form so it's directly usable for both.
        instrument.nativeSymbol = instrument.baseAsset + "/" + instrument.quoteAsset;
        instrument.isActive = symbolInfo.value("status", "") == "online";

        instrument.tickSize = parseDoubleOr(symbolInfo, "tick_size", 0.0);
        // lot_decimals is a decimal-place count, not a literal step value.
        instrument.stepSize = std::pow(10.0, -parseDoubleOr(symbolInfo, "lot_decimals", 0.0));
        instrument.minQty = parseDoubleOr(symbolInfo, "ordermin", 0.0);
        instrument.minNotional = parseDoubleOr(symbolInfo, "costmin", 0.0);

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
