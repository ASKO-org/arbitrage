#include <iostream>
#include <memory>
#include <vector>

#include "config/Config.h"
#include "connectors/BinanceConnector.h"
#include "connectors/BinanceFutConnector.h"
#include "connectors/BingxFutConnector.h"
#include "connectors/BingxSpotConnector.h"
#include "connectors/BitgetFutConnector.h"
#include "connectors/BitgetSpotConnector.h"
#include "connectors/BybitConnector.h"
#include "connectors/BybitFutConnector.h"
#include "connectors/CoinexFutConnector.h"
#include "connectors/CoinexSpotConnector.h"
#include "connectors/GateioFutConnector.h"
#include "connectors/GateioSpotConnector.h"
#include "connectors/HtxFutConnector.h"
#include "connectors/HtxSpotConnector.h"
#include "connectors/IExchangeConnector.h"
#include "connectors/KrakenFutConnector.h"
#include "connectors/KrakenSpotConnector.h"
#include "connectors/KucoinFutConnector.h"
#include "connectors/KucoinSpotConnector.h"
#include "connectors/MexcFutConnector.h"
#include "connectors/MexcSpotConnector.h"
#include "connectors/OkxFutConnector.h"
#include "connectors/OkxSpotConnector.h"
#include "database/DatabaseRepository.h"

int main() {
    std::vector<std::unique_ptr<IExchangeConnector>> connectors;
    connectors.push_back(std::make_unique<BinanceConnector>());
    connectors.push_back(std::make_unique<BinanceFutConnector>());
    connectors.push_back(std::make_unique<BingxSpotConnector>());
    connectors.push_back(std::make_unique<BingxFutConnector>());
    connectors.push_back(std::make_unique<BitgetSpotConnector>());
    connectors.push_back(std::make_unique<BitgetFutConnector>());
    connectors.push_back(std::make_unique<BybitConnector>());
    connectors.push_back(std::make_unique<BybitFutConnector>());
    connectors.push_back(std::make_unique<CoinexSpotConnector>());
    connectors.push_back(std::make_unique<CoinexFutConnector>());
    connectors.push_back(std::make_unique<GateioSpotConnector>());
    connectors.push_back(std::make_unique<GateioFutConnector>());
    connectors.push_back(std::make_unique<HtxSpotConnector>());
    connectors.push_back(std::make_unique<HtxFutConnector>());
    connectors.push_back(std::make_unique<KrakenSpotConnector>());
    connectors.push_back(std::make_unique<KrakenFutConnector>());
    connectors.push_back(std::make_unique<KucoinSpotConnector>());
    connectors.push_back(std::make_unique<KucoinFutConnector>());
    connectors.push_back(std::make_unique<MexcSpotConnector>());
    connectors.push_back(std::make_unique<MexcFutConnector>());
    connectors.push_back(std::make_unique<OkxSpotConnector>());
    connectors.push_back(std::make_unique<OkxFutConnector>());

    try {
        DatabaseRepository repository(Config::postgresConnectionString());
        repository.ensureSchema();

        for (const auto& connector : connectors) {
            try {
                std::cout << "Fetching instruments from " << connector->exchangeName() << "...\n";
                const auto instruments = connector->fetchInstruments();
                repository.saveInstruments(instruments);
                std::cout << "  saved " << instruments.size() << " instruments\n";
            } catch (const std::exception& ex) {
                std::cerr << "  failed to load " << connector->exchangeName() << ": " << ex.what()
                          << "\n";
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
