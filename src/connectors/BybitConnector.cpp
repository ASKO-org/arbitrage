#include "connectors/BybitConnector.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "network/HttpClient.h"

namespace {
constexpr const char* kInstrumentsInfoUrl =
    "https://api.bybit.com/v5/market/instruments-info?category=spot&limit=1000";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key)) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

std::string BybitConnector::exchangeName() const { return "BYBIT_Spot"; }

std::vector<Instrument> BybitConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kInstrumentsInfoUrl);
    const auto json = nlohmann::json::parse(body);

    if (json.value("retCode", -1) != 0) {
        throw std::runtime_error("BybitConnector: API error: " + json.value("retMsg", "unknown"));
    }

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json.at("result").at("list")) {
        Instrument instrument;
        instrument.exchangeName = exchangeName();
        instrument.nativeSymbol = symbolInfo.at("symbol").get<std::string>();
        instrument.baseAsset = symbolInfo.at("baseCoin").get<std::string>();
        instrument.quoteAsset = symbolInfo.at("quoteCoin").get<std::string>();
        instrument.isActive = symbolInfo.value("status", "") == "Trading";

        if (symbolInfo.contains("priceFilter")) {
            instrument.tickSize = parseDoubleOr(symbolInfo.at("priceFilter"), "tickSize", 0.0);
        }
        if (symbolInfo.contains("lotSizeFilter")) {
            const auto& lotSize = symbolInfo.at("lotSizeFilter");
            instrument.stepSize = parseDoubleOr(lotSize, "basePrecision", 0.0);
            instrument.minQty = parseDoubleOr(lotSize, "minOrderQty", 0.0);
            instrument.minNotional = parseDoubleOr(lotSize, "minOrderAmt", 0.0);
        }

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
