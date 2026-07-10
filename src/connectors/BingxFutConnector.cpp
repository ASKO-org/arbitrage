#include "connectors/BingxFutConnector.h"

#include <cmath>

#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kContractsUrl = "https://open-api.bingx.com/openApi/swap/v2/quote/contracts";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key) || obj.at(key).is_null()) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

std::string BingxFutConnector::exchangeName() const { return "BINGX_Fut"; }

std::vector<Instrument> BingxFutConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kContractsUrl);
    const auto json = nlohmann::json::parse(body);

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json.at("data")) {
        Instrument instrument;
        instrument.exchangeName = exchangeName();
        instrument.nativeSymbol = symbolInfo.at("symbol").get<std::string>();
        instrument.baseAsset = symbolInfo.at("asset").get<std::string>();
        instrument.quoteAsset = symbolInfo.at("currency").get<std::string>();
        // apiStateOpen/apiStateClose are quoted strings ("true"/"false"), not
        // JSON booleans, on this endpoint.
        instrument.isActive = symbolInfo.value("apiStateOpen", "") == "true" &&
                               symbolInfo.value("apiStateClose", "") == "true";

        // No literal tick-size field; only pricePrecision (decimal places).
        instrument.tickSize = std::pow(10.0, -parseDoubleOr(symbolInfo, "pricePrecision", 0.0));
        instrument.stepSize = parseDoubleOr(symbolInfo, "size", 0.0);
        instrument.minQty = parseDoubleOr(symbolInfo, "tradeMinQuantity", 0.0);
        instrument.minNotional = parseDoubleOr(symbolInfo, "tradeMinUSDT", 0.0);

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
