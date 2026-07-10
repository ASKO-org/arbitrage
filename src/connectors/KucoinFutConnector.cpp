#include "connectors/KucoinFutConnector.h"

#include <stdexcept>

#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kActiveContractsUrl = "https://api-futures.kucoin.com/api/v1/contracts/active";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key) || obj.at(key).is_null()) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

std::string KucoinFutConnector::exchangeName() const { return "KUCOIN_Fut"; }

std::vector<Instrument> KucoinFutConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kActiveContractsUrl);
    const auto json = nlohmann::json::parse(body);

    if (json.value("code", "") != "200000") {
        throw std::runtime_error("KucoinFutConnector: API error, code=" + json.value("code", "?"));
    }

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json.at("data")) {
        if (symbolInfo.value("quoteCurrency", "") != "USDT") continue;

        Instrument instrument;
        instrument.exchangeName = exchangeName();
        instrument.nativeSymbol = symbolInfo.at("symbol").get<std::string>();
        instrument.baseAsset = symbolInfo.at("baseCurrency").get<std::string>();
        instrument.quoteAsset = symbolInfo.at("quoteCurrency").get<std::string>();
        // This endpoint only ever returns currently-open contracts anyway.
        instrument.isActive = symbolInfo.value("status", "") == "Open";

        instrument.tickSize = parseDoubleOr(symbolInfo, "tickSize", 0.0);
        // lotSize doubles as both step size and minimum order size here —
        // KuCoin exposes no separate minOrderQty/minNotional field for futures.
        instrument.stepSize = parseDoubleOr(symbolInfo, "lotSize", 0.0);
        instrument.minQty = instrument.stepSize;

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
