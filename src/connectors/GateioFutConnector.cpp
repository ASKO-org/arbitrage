#include "connectors/GateioFutConnector.h"

#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kContractsUrl = "https://api.gateio.ws/api/v4/futures/usdt/contracts";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key) || obj.at(key).is_null()) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

std::string GateioFutConnector::exchangeName() const { return "GATEIO_Fut"; }

std::vector<Instrument> GateioFutConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kContractsUrl);
    const auto json = nlohmann::json::parse(body);  // bare array at the document root

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json) {
        // Gate.io futures has no base/quote field; both are derived from
        // "name" (format "BASE_QUOTE").
        const std::string name = symbolInfo.at("name").get<std::string>();
        const auto underscore = name.find('_');
        if (underscore == std::string::npos) continue;

        Instrument instrument;
        instrument.exchangeName = exchangeName();
        instrument.nativeSymbol = name;
        instrument.baseAsset = name.substr(0, underscore);
        instrument.quoteAsset = name.substr(underscore + 1);
        instrument.isActive = symbolInfo.value("status", "") == "trading";

        instrument.tickSize = parseDoubleOr(symbolInfo, "order_price_round", 0.0);
        // order_size_min is a count of contracts, not base-asset units; no
        // literal step-size or minNotional field exists for futures here.
        instrument.minQty = parseDoubleOr(symbolInfo, "order_size_min", 0.0);

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
