#pragma once
#include "connectors/IExchangeConnector.h"

class GateioFutConnector : public IExchangeConnector {
public:
    std::string exchangeName() const override;
    std::vector<Instrument> fetchInstruments() const override;
};
