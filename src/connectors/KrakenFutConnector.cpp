#include "connectors/KrakenFutConnector.h"

#include <cmath>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kInstrumentsUrl = "https://futures.kraken.com/derivatives/api/v3/instruments";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key) || obj.at(key).is_null()) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

std::string KrakenFutConnector::exchangeName() const { return "KRAKEN_Fut"; }

std::vector<Instrument> KrakenFutConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kInstrumentsUrl);
    const auto json = nlohmann::json::parse(body);

    if (json.value("result", "") != "success") {
        throw std::runtime_error("KrakenFutConnector: API error");
    }

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json.at("instruments")) {
        // Dated/expiring contracts carry lastTradingTime; true perpetuals
        // don't, and that's the only reliable way to tell them apart here.
        if (symbolInfo.contains("lastTradingTime")) continue;
        if (!symbolInfo.value("tradeable", false)) continue;

        Instrument instrument;
        instrument.exchangeName = exchangeName();
        instrument.nativeSymbol = symbolInfo.at("symbol").get<std::string>();
        instrument.baseAsset = symbolInfo.at("base").get<std::string>();
        instrument.quoteAsset = symbolInfo.at("quote").get<std::string>();
        instrument.isActive = true;  // already filtered to tradeable above

        instrument.tickSize = parseDoubleOr(symbolInfo, "tickSize", 0.0);
        // No minQty/minNotional field exists on this endpoint.
        instrument.stepSize =
            std::pow(10.0, -parseDoubleOr(symbolInfo, "contractValueTradePrecision", 0.0));

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
