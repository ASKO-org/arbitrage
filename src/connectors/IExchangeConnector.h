#pragma once
#include <string>
#include <vector>
#include "models/Instrument.h"

// Base interface every exchange connector implements.
class IExchangeConnector {
public:
    virtual ~IExchangeConnector() = default;

    virtual std::string exchangeName() const = 0;

    // Fetches the full list of tradable instruments from the exchange.
    // Throws std::runtime_error on network or parsing failure.
    virtual std::vector<Instrument> fetchInstruments() const = 0;
};
