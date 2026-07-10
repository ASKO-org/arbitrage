#pragma once
#include "connectors/IExchangeConnector.h"

class CoinexSpotConnector : public IExchangeConnector {
public:
    std::string exchangeName() const override;
    std::vector<Instrument> fetchInstruments() const override;
};
