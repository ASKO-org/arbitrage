#include "connectors/OkxFutConnector.h"

#include <stdexcept>

#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kInstrumentsUrl = "https://www.okx.com/api/v5/public/instruments?instType=SWAP";

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

std::string OkxFutConnector::exchangeName() const { return "OKX_Fut"; }

std::vector<Instrument> OkxFutConnector::fetchInstruments() const {
    const std::string body = HttpClient::get(kInstrumentsUrl);
    const auto json = nlohmann::json::parse(body);

    if (json.value("code", "") != "0") {
        throw std::runtime_error("OkxFutConnector: API error: " + json.value("msg", "unknown"));
    }

    std::vector<Instrument> instruments;
    for (const auto& symbolInfo : json.at("data")) {
        // instType=SWAP mixes linear (USDT-margined) and inverse contracts;
        // keep only linear. SWAP has no baseCcy/quoteCcy fields, so they're
        // parsed out of instId (format "BASE-QUOTE-SWAP").
        if (symbolInfo.value("ctType", "") != "linear") continue;

        const std::string instId = symbolInfo.at("instId").get<std::string>();
        const auto firstDash = instId.find('-');
        const auto secondDash = instId.find('-', firstDash + 1);
        if (firstDash == std::string::npos || secondDash == std::string::npos) continue;

        Instrument instrument;
        instrument.exchangeName = exchangeName();
        instrument.nativeSymbol = instId;
        instrument.baseAsset = instId.substr(0, firstDash);
        instrument.quoteAsset = instId.substr(firstDash + 1, secondDash - firstDash - 1);
        instrument.isActive = symbolInfo.value("state", "") == "live";

        instrument.tickSize = parseDoubleOr(symbolInfo, "tickSz", 0.0);
        // lotSz/minSz here are counted in contracts, not base-asset units;
        // OKX's instruments endpoint gives no minNotional field either way.
        instrument.stepSize = parseDoubleOr(symbolInfo, "lotSz", 0.0);
        instrument.minQty = parseDoubleOr(symbolInfo, "minSz", 0.0);

        instruments.push_back(std::move(instrument));
    }

    return instruments;
}
