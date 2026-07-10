#include "connectors/BybitFutConnector.h"

#include <stdexcept>

#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kInstrumentsInfoUrl =
    "https://api.bybit.com/v5/market/instruments-info?category=linear&limit=1000";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key)) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

std::string BybitFutConnector::exchangeName() const { return "BYBIT_Fut"; }

std::vector<Instrument> BybitFutConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kInstrumentsInfoUrl);
    const auto json = nlohmann::json::parse(body);

    if (json.value("retCode", -1) != 0) {
        throw std::runtime_error("BybitFutConnector: API error: " + json.value("retMsg", "unknown"));
    }

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json.at("result").at("list")) {
        // category=linear also returns USDC-margined and dated-expiry contracts
        // alongside USDT perpetuals; keep only the latter.
        if (symbolInfo.value("settleCoin", "") != "USDT") continue;
        if (symbolInfo.value("contractType", "") != "LinearPerpetual") continue;

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
            instrument.stepSize = parseDoubleOr(lotSize, "qtyStep", 0.0);
            instrument.minQty = parseDoubleOr(lotSize, "minOrderQty", 0.0);
            instrument.minNotional = parseDoubleOr(lotSize, "minNotionalValue", 0.0);
        }

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
