#include "connectors/BitgetSpotConnector.h"

#include <cmath>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kSymbolsUrl = "https://api.bitget.com/api/v2/spot/public/symbols";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key)) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

std::string BitgetSpotConnector::exchangeName() const { return "BITGET_Spot"; }

std::vector<Instrument> BitgetSpotConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kSymbolsUrl);
    const auto json = nlohmann::json::parse(body);

    if (json.value("code", "") != "00000") {
        throw std::runtime_error("BitgetSpotConnector: API error: " + json.value("msg", "unknown"));
    }

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json.at("data")) {
        Instrument instrument;
        instrument.exchangeName = exchangeName();
        instrument.nativeSymbol = symbolInfo.at("symbol").get<std::string>();
        instrument.baseAsset = symbolInfo.at("baseCoin").get<std::string>();
        instrument.quoteAsset = symbolInfo.at("quoteCoin").get<std::string>();
        instrument.isActive = symbolInfo.value("status", "") == "online";

        // Bitget spot has no direct tick/step-size field; both are derived
        // from decimal-place precision fields.
        instrument.tickSize = std::pow(10.0, -parseDoubleOr(symbolInfo, "pricePrecision", 0.0));
        instrument.stepSize = std::pow(10.0, -parseDoubleOr(symbolInfo, "quantityPrecision", 0.0));
        // minTradeAmount is documented by Bitget as obsolete; minTradeUSDT is
        // the real enforced minimum order value, so minQty is left unset.
        instrument.minNotional = parseDoubleOr(symbolInfo, "minTradeUSDT", 0.0);

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
