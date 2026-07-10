#include "connectors/HtxSpotConnector.h"

#include <cmath>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
// The older /v1/common/symbols is marked deprecated by HTX's own docs;
// this is the current documented equivalent (abbreviated field names).
constexpr const char* kMarketSymbolsUrl = "https://api.huobi.pro/v1/settings/common/market-symbols";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key) || obj.at(key).is_null()) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

std::string HtxSpotConnector::exchangeName() const { return "HTX_Spot"; }

std::vector<Instrument> HtxSpotConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kMarketSymbolsUrl);
    const auto json = nlohmann::json::parse(body);

    if (json.value("status", "") != "ok") {
        throw std::runtime_error("HtxSpotConnector: API error: " + json.value("err-msg", "unknown"));
    }

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json.at("data")) {
        Instrument instrument;
        instrument.exchangeName = exchangeName();
        instrument.nativeSymbol = symbolInfo.at("symbol").get<std::string>();
        instrument.baseAsset = symbolInfo.at("bc").get<std::string>();
        instrument.quoteAsset = symbolInfo.at("qc").get<std::string>();
        instrument.isActive = symbolInfo.value("state", "") == "online";

        // pp/ap are decimal-place precision counts, not literal tick/step
        // values.
        instrument.tickSize = std::pow(10.0, -parseDoubleOr(symbolInfo, "pp", 0.0));
        instrument.stepSize = std::pow(10.0, -parseDoubleOr(symbolInfo, "ap", 0.0));
        instrument.minQty = parseDoubleOr(symbolInfo, "lominoa", 0.0);
        instrument.minNotional = parseDoubleOr(symbolInfo, "minov", 0.0);

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
