#include "connectors/BinanceFutConnector.h"

#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kExchangeInfoUrl = "https://fapi.binance.com/fapi/v1/exchangeInfo";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key)) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

std::string BinanceFutConnector::exchangeName() const { return "BINANCE_Fut"; }

std::vector<Instrument> BinanceFutConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kExchangeInfoUrl);
    const auto json = nlohmann::json::parse(body);

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json.at("symbols")) {
        // fapi also returns dated quarterly contracts and tokenized-stock
        // perpetuals; keep plain USDⓈ perpetual swaps only.
        if (symbolInfo.value("contractType", "") != "PERPETUAL") continue;

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
            } else if (filterType == "MIN_NOTIONAL") {
                // Futures uses filterType MIN_NOTIONAL with key "notional",
                // unlike spot's NOTIONAL filter with key "minNotional".
                instrument.minNotional = parseDoubleOr(filter, "notional", 0.0);
            }
        }

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
