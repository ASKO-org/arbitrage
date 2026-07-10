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
        instrument.nativeSymbol = symbolInfo.at("altname").get<std::string>();
        instrument.baseAsset = symbolInfo.at("base").get<std::string>();
        instrument.quoteAsset = symbolInfo.at("quote").get<std::string>();
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
