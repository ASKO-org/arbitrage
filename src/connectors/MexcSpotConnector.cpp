#include "connectors/MexcSpotConnector.h"

#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kExchangeInfoUrl = "https://api.mexc.com/api/v3/exchangeInfo";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key)) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

std::string MexcSpotConnector::exchangeName() const { return "MEXC_Spot"; }

std::vector<Instrument> MexcSpotConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kExchangeInfoUrl);
    const auto json = nlohmann::json::parse(body);

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json.at("symbols")) {
        Instrument instrument;
        instrument.exchangeName = exchangeName();
        instrument.nativeSymbol = symbolInfo.at("symbol").get<std::string>();
        instrument.baseAsset = symbolInfo.at("baseAsset").get<std::string>();
        instrument.quoteAsset = symbolInfo.at("quoteAsset").get<std::string>();
        instrument.isActive = symbolInfo.value("status", "") == "1";

        // MEXC spot has no tick-size field at all. baseSizePrecision and
        // quoteAmountPrecision are MEXC's own (oddly named) min-qty/min-notional
        // fields, per their docs, despite the "Precision" naming.
        instrument.stepSize = parseDoubleOr(symbolInfo, "baseSizePrecision", 0.0);
        instrument.minQty = instrument.stepSize;
        instrument.minNotional = parseDoubleOr(symbolInfo, "quoteAmountPrecision", 0.0);

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
