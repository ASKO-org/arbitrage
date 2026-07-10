#include "connectors/CoinexFutConnector.h"

#include <cmath>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kMarketUrl = "https://api.coinex.com/v2/futures/market";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key) || obj.at(key).is_null()) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

std::string CoinexFutConnector::exchangeName() const { return "COINEX_Fut"; }

std::vector<Instrument> CoinexFutConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kMarketUrl);
    const auto json = nlohmann::json::parse(body);

    if (json.value("code", -1) != 0) {
        throw std::runtime_error("CoinexFutConnector: API error: " + json.value("message", "unknown"));
    }

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json.at("data")) {
        // CoinEx currently only offers linear USDT-margined perpetuals here,
        // but filter explicitly rather than assuming that stays true.
        if (symbolInfo.value("quote_ccy", "") != "USDT") continue;
        if (symbolInfo.value("contract_type", "") != "linear") continue;

        Instrument instrument;
        instrument.exchangeName = exchangeName();
        instrument.nativeSymbol = symbolInfo.at("market").get<std::string>();
        instrument.baseAsset = symbolInfo.at("base_ccy").get<std::string>();
        instrument.quoteAsset = symbolInfo.at("quote_ccy").get<std::string>();
        instrument.isActive = symbolInfo.value("status", "") == "online";

        // Unlike spot, futures does have a literal tick_size field. Step size
        // still has no literal field; no minNotional field exists either way.
        instrument.tickSize = parseDoubleOr(symbolInfo, "tick_size", 0.0);
        instrument.stepSize = std::pow(10.0, -parseDoubleOr(symbolInfo, "base_ccy_precision", 0.0));
        instrument.minQty = parseDoubleOr(symbolInfo, "min_amount", 0.0);

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
