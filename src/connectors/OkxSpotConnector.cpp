#include "connectors/OkxSpotConnector.h"

#include <stdexcept>

#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kInstrumentsUrl = "https://www.okx.com/api/v5/public/instruments?instType=SPOT";

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key)) return fallback;
    const auto& value = obj.at(key);
    if (value.is_string()) {
        const auto& str = value.get_ref<const std::string&>();
        return str.empty() ? fallback : std::stod(str);
    }
    return value.get<double>();
}
}  // namespace

std::string OkxSpotConnector::exchangeName() const { return "OKX_Spot"; }

std::vector<Instrument> OkxSpotConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kInstrumentsUrl);
    const auto json = nlohmann::json::parse(body);

    if (json.value("code", "") != "0") {
        throw std::runtime_error("OkxSpotConnector: API error: " + json.value("msg", "unknown"));
    }

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json.at("data")) {
        Instrument instrument;
        instrument.exchangeName = exchangeName();
        instrument.nativeSymbol = symbolInfo.at("instId").get<std::string>();
        instrument.baseAsset = symbolInfo.at("baseCcy").get<std::string>();
        instrument.quoteAsset = symbolInfo.at("quoteCcy").get<std::string>();
        instrument.isActive = symbolInfo.value("state", "") == "live";

        instrument.tickSize = parseDoubleOr(symbolInfo, "tickSz", 0.0);
        instrument.stepSize = parseDoubleOr(symbolInfo, "lotSz", 0.0);
        instrument.minQty = parseDoubleOr(symbolInfo, "minSz", 0.0);
        // OKX's public instruments endpoint has no minimum-notional field for
        // spot; minNotional is intentionally left unset.

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
