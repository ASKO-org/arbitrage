#include "connectors/MexcFutConnector.h"

#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kContractDetailUrl = "https://contract.mexc.com/api/v1/contract/detail";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key) || obj.at(key).is_null()) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

std::string MexcFutConnector::exchangeName() const { return "MEXC_Fut"; }

std::vector<Instrument> MexcFutConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kContractDetailUrl);
    const auto json = nlohmann::json::parse(body);

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json.at("data")) {
        Instrument instrument;
        instrument.exchangeName = exchangeName();
        instrument.nativeSymbol = symbolInfo.at("symbol").get<std::string>();
        instrument.baseAsset = symbolInfo.at("baseCoin").get<std::string>();
        instrument.quoteAsset = symbolInfo.at("quoteCoin").get<std::string>();
        instrument.isActive =
            symbolInfo.value("state", -1) == 0 && symbolInfo.value("apiAllowed", true);

        instrument.tickSize = parseDoubleOr(symbolInfo, "priceUnit", 0.0);
        // minVol/volUnit are in contract-volume units, not base-asset units;
        // no minNotional field exists for this endpoint.
        instrument.stepSize = parseDoubleOr(symbolInfo, "volUnit", 0.0);
        instrument.minQty = parseDoubleOr(symbolInfo, "minVol", 0.0);

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
