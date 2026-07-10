#include "connectors/HtxFutConnector.h"

#include <stdexcept>

#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kContractInfoUrl =
    "https://api.hbdm.com/linear-swap-api/v1/swap_contract_info?business_type=swap";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key) || obj.at(key).is_null()) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

std::string HtxFutConnector::exchangeName() const { return "HTX_Fut"; }

std::vector<Instrument> HtxFutConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kContractInfoUrl);
    const auto json = nlohmann::json::parse(body);

    if (json.value("status", "") != "ok") {
        throw std::runtime_error("HtxFutConnector: API error: " + json.value("err_msg", "unknown"));
    }

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json.at("data")) {
        Instrument instrument;
        instrument.exchangeName = exchangeName();
        instrument.nativeSymbol = symbolInfo.at("contract_code").get<std::string>();
        instrument.baseAsset = symbolInfo.at("symbol").get<std::string>();
        instrument.quoteAsset = symbolInfo.value("trade_partition", "USDT");
        instrument.isActive = symbolInfo.value("contract_status", -1) == 1;

        // swap_contract_info has no stepSize/minQty/minNotional fields at all;
        // contracts trade in whole-contract units this endpoint doesn't expose
        // a base-asset-denominated step for.
        instrument.tickSize = parseDoubleOr(symbolInfo, "price_tick", 0.0);

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
