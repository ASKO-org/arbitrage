#include "connectors/BinanceConnector.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "network/HttpClient.h"

namespace {
constexpr const char* kExchangeInfoUrl = "https://api.binance.com/api/v3/exchangeInfo";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key)) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

std::string BinanceConnector::exchangeName() const { return "BINANCE_Spot"; }

std::vector<Instrument> BinanceConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kExchangeInfoUrl);
    const auto json = nlohmann::json::parse(body);

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json.at("symbols")) {
        Instrument instrument;
        instrument.exchangeName = exchangeName();
        instrument.nativeSymbol = symbolInfo.at("symbol").get<std::string>();
        instrument.baseAsset = symbolInfo.at("baseAsset").get<std::string>();
        instrument.quoteAsset = symbolInfo.at("quoteAsset").get<std::string>();
        instrument.isActive = symbolInfo.value("status", "") == "TRADING";

        for (const auto& filter : symbolInfo.value("filters", nlohmann::json::array())) {
            const std::string filterType = filter.value("filterType", "");
            if (filterType == "PRICE_FILTER") {
                instrument.tickSize = parseDoubleOr(filter, "tickSize", 0.0);
            } else if (filterType == "LOT_SIZE") {
                instrument.stepSize = parseDoubleOr(filter, "stepSize", 0.0);
                instrument.minQty = parseDoubleOr(filter, "minQty", 0.0);
            } else if (filterType == "NOTIONAL" || filterType == "MIN_NOTIONAL") {
                instrument.minNotional = parseDoubleOr(filter, "minNotional", 0.0);
            }
        }

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
