#include "connectors/KucoinSpotConnector.h"

#include <stdexcept>

#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kSymbolsUrl = "https://api.kucoin.com/api/v2/symbols";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key) || obj.at(key).is_null()) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

std::string KucoinSpotConnector::exchangeName() const { return "KUCOIN_Spot"; }

std::vector<Instrument> KucoinSpotConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kSymbolsUrl);
    const auto json = nlohmann::json::parse(body);

    if (json.value("code", "") != "200000") {
        throw std::runtime_error("KucoinSpotConnector: API error, code=" + json.value("code", "?"));
    }

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json.at("data")) {
        Instrument instrument;
        instrument.exchangeName = exchangeName();
        instrument.nativeSymbol = symbolInfo.at("symbol").get<std::string>();
        instrument.baseAsset = symbolInfo.at("baseCurrency").get<std::string>();
        instrument.quoteAsset = symbolInfo.at("quoteCurrency").get<std::string>();
        instrument.isActive = symbolInfo.value("enableTrading", false);

        instrument.tickSize = parseDoubleOr(symbolInfo, "priceIncrement", 0.0);
        instrument.stepSize = parseDoubleOr(symbolInfo, "baseIncrement", 0.0);
        instrument.minQty = parseDoubleOr(symbolInfo, "baseMinSize", 0.0);
        instrument.minNotional = parseDoubleOr(symbolInfo, "minFunds", 0.0);

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
