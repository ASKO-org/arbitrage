#pragma once
#include "connectors/IExchangeConnector.h"

class KrakenSpotConnector : public IExchangeConnector {
public:
    std::string exchangeName() const override;
    std::vector<Instrument> fetchInstruments() const override;
};
