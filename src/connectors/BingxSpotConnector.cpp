#include "connectors/BingxSpotConnector.h"

#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kSymbolsUrl = "https://open-api.bingx.com/openApi/spot/v1/common/symbols";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key) || obj.at(key).is_null()) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

std::string BingxSpotConnector::exchangeName() const { return "BINGX_Spot"; }

std::vector<Instrument> BingxSpotConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kSymbolsUrl);
    const auto json = nlohmann::json::parse(body);

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json.at("data").at("symbols")) {
        // BingX spot has no baseAsset/quoteAsset field; both are derived from
        // "symbol" (format "BASE-QUOTE").
        const std::string symbol = symbolInfo.at("symbol").get<std::string>();
        const auto dash = symbol.find('-');
        if (dash == std::string::npos) continue;

        Instrument instrument;
        instrument.exchangeName = exchangeName();
        instrument.nativeSymbol = symbol;
        instrument.baseAsset = symbol.substr(0, dash);
        instrument.quoteAsset = symbol.substr(dash + 1);
        // status alone disagrees with apiState flags on a meaningful number of
        // symbols; matching ccxt's production behavior, require all three.
        instrument.isActive = symbolInfo.value("status", 0) == 1 &&
                               symbolInfo.value("apiStateBuy", false) &&
                               symbolInfo.value("apiStateSell", false);

        instrument.tickSize = parseDoubleOr(symbolInfo, "tickSize", 0.0);
        instrument.stepSize = parseDoubleOr(symbolInfo, "stepSize", 0.0);
        instrument.minQty = parseDoubleOr(symbolInfo, "minQty", 0.0);
        instrument.minNotional = parseDoubleOr(symbolInfo, "minNotional", 0.0);

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
