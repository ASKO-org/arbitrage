#include "connectors/BitgetFutConnector.h"

#include <cmath>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kContractsUrl =
    "https://api.bitget.com/api/v2/mix/market/contracts?productType=usdt-futures";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key)) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

std::string BitgetFutConnector::exchangeName() const { return "BITGET_Fut"; }

std::vector<Instrument> BitgetFutConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kContractsUrl);
    const auto json = nlohmann::json::parse(body);

    if (json.value("code", "") != "00000") {
        throw std::runtime_error("BitgetFutConnector: API error: " + json.value("msg", "unknown"));
    }

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json.at("data")) {
        Instrument instrument;
        instrument.exchangeName = exchangeName();
        instrument.nativeSymbol = symbolInfo.at("symbol").get<std::string>();
        instrument.baseAsset = symbolInfo.at("baseCoin").get<std::string>();
        instrument.quoteAsset = symbolInfo.at("quoteCoin").get<std::string>();
        instrument.isActive = symbolInfo.value("symbolStatus", "") == "normal";

        const double priceEndStep = parseDoubleOr(symbolInfo, "priceEndStep", 1.0);
        instrument.tickSize = priceEndStep * std::pow(10.0, -parseDoubleOr(symbolInfo, "pricePlace", 0.0));
        instrument.stepSize = parseDoubleOr(symbolInfo, "sizeMultiplier", 0.0);
        instrument.minQty = parseDoubleOr(symbolInfo, "minTradeNum", 0.0);
        instrument.minNotional = parseDoubleOr(symbolInfo, "minTradeUSDT", 0.0);

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
