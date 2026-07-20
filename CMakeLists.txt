cmake_minimum_required(VERSION 3.20)
project(InstrumentLoader CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Поиск системных библиотек Linux
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)
# find_package(Hiredis REQUIRED) without a version fails on Ubuntu's package:
# HiredisConfigVersion.cmake does string(REPLACE ... ${PACKAGE_FIND_VERSION})
# which needs PACKAGE_FIND_VERSION set, so a version must be passed here.
find_package(Hiredis 1.2.0 REQUIRED)
find_package(ZLIB REQUIRED)

# Дистрибутивный libpqxx-dev собран без std::source_location (ABI не
# совпадает с заголовками при сборке в C++20), поэтому libpqxx собирается
# из исходников той же версией компилятора/стандарта, что и весь проект.
include(FetchContent)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
set(SKIP_BUILD_TEST ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    libpqxx
    GIT_REPOSITORY https://github.com/jtv/libpqxx.git
    GIT_TAG 8.0.1
)
FetchContent_MakeAvailable(libpqxx)

# WebSocket-клиент для arbitrage_scanner (потоковые котировки Binance/Bybit).
set(USE_TLS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG v11.4.5
)
FetchContent_MakeAvailable(ixwebsocket)

# Общий код: конфигурация, модели, доступ к Postgres, WebSocket-коннекторы
# рыночных данных и QuoteStore. Используется всеми исполняемыми файлами,
# компилируется один раз.
add_library(common STATIC
    common/database/DatabaseRepository.cpp
    common/network/HttpClient.cpp
    common/marketdata/HeartbeatTimer.cpp
    common/marketdata/Gzip.cpp
    common/marketdata/MexcProto.cpp
    common/marketdata/BinanceMarketDataConnector.cpp
    common/marketdata/BinanceFutMarketDataConnector.cpp
    common/marketdata/BybitMarketDataConnector.cpp
    common/marketdata/BybitFutMarketDataConnector.cpp
    common/marketdata/KucoinSpotMarketDataConnector.cpp
    common/marketdata/KucoinFutMarketDataConnector.cpp
    common/marketdata/BitgetSpotMarketDataConnector.cpp
    common/marketdata/BitgetFutMarketDataConnector.cpp
    common/marketdata/HtxSpotMarketDataConnector.cpp
    common/marketdata/HtxFutMarketDataConnector.cpp
    common/marketdata/OkxSpotMarketDataConnector.cpp
    common/marketdata/OkxFutMarketDataConnector.cpp
    common/marketdata/MexcSpotMarketDataConnector.cpp
    common/marketdata/MexcFutMarketDataConnector.cpp
    common/marketdata/KrakenSpotMarketDataConnector.cpp
    common/marketdata/KrakenFutMarketDataConnector.cpp
    common/marketdata/CoinexSpotMarketDataConnector.cpp
    common/marketdata/CoinexFutMarketDataConnector.cpp
    common/marketdata/GateioSpotMarketDataConnector.cpp
    common/marketdata/GateioFutMarketDataConnector.cpp
    common/marketdata/BingxSpotMarketDataConnector.cpp
    common/marketdata/BingxFutMarketDataConnector.cpp
    common/core/QuoteStore.cpp
    common/core/QuoteSnapshotFileReader.cpp
    common/marketdata/QuoteFeedPublisher.cpp
    common/marketdata/QuoteFeedSubscriber.cpp
    common/execution/HmacSigner.cpp
    common/execution/OrderSubmitter.cpp
    common/execution/BinanceExecutionConnector.cpp
    common/execution/BybitExecutionConnector.cpp
)
target_include_directories(common PUBLIC common ${HIREDIS_INCLUDE_DIRS})
target_link_libraries(common PUBLIC
    libpqxx::pqxx
    ixwebsocket
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
    ZLIB::ZLIB
    ${HIREDIS_LIBRARIES}
)

# instrument_loader — разовая загрузка списков инструментов в Postgres.
add_executable(instrument_loader
    src/main.cpp
    src/connectors/BinanceConnector.cpp
    src/connectors/BinanceFutConnector.cpp
    src/connectors/BingxSpotConnector.cpp
    src/connectors/BingxFutConnector.cpp
    src/connectors/BitgetSpotConnector.cpp
    src/connectors/BitgetFutConnector.cpp
    src/connectors/BybitConnector.cpp
    src/connectors/BybitFutConnector.cpp
    src/connectors/CoinexSpotConnector.cpp
    src/connectors/CoinexFutConnector.cpp
    src/connectors/GateioSpotConnector.cpp
    src/connectors/GateioFutConnector.cpp
    src/connectors/HtxSpotConnector.cpp
    src/connectors/HtxFutConnector.cpp
    src/connectors/KrakenSpotConnector.cpp
    src/connectors/KrakenFutConnector.cpp
    src/connectors/KucoinSpotConnector.cpp
    src/connectors/KucoinFutConnector.cpp
    src/connectors/MexcSpotConnector.cpp
    src/connectors/MexcFutConnector.cpp
    src/connectors/OkxSpotConnector.cpp
    src/connectors/OkxFutConnector.cpp
)
target_include_directories(instrument_loader PRIVATE src)
target_link_libraries(instrument_loader PRIVATE
    common
    nlohmann_json::nlohmann_json
)

# arbitrage_scanner — долгоживущий сервис, слушает потоковые котировки Binance
# и Bybit по WebSocket и ищет межбиржевой спред.
add_executable(arbitrage_scanner
    arbitrage_scanner/src/main.cpp
    arbitrage_scanner/src/core/ArbitrageDetector.cpp
    arbitrage_scanner/src/core/RedisOpportunitySink.cpp
)
target_include_directories(arbitrage_scanner PRIVATE arbitrage_scanner/src)
target_link_libraries(arbitrage_scanner PRIVATE
    common
    nlohmann_json::nlohmann_json
)

# market_data_feed — долгоживущий сервис, единственный держатель всех
# WebSocket-коннекторов рыночных данных; публикует каждую котировку в Redis
# (pub/sub) для quote_recorder и любых будущих подписчиков.
add_executable(market_data_feed
    market_data_feed/src/main.cpp
)
target_include_directories(market_data_feed PRIVATE market_data_feed/src)
target_link_libraries(market_data_feed PRIVATE
    common
    nlohmann_json::nlohmann_json
)

# quote_recorder — долгоживущий сервис, подписывается на котировки из Redis
# (market_data_feed) и периодически сохраняет best bid/ask для инструментов
# из recorder_watchlist в бинарные файлы (по одному на биржу и день).
add_executable(quote_recorder
    quote_recorder/src/main.cpp
    quote_recorder/src/core/QuoteSnapshotWriter.cpp
    quote_recorder/src/core/QuoteSnapshotFileWriter.cpp
)
target_include_directories(quote_recorder PRIVATE quote_recorder/src)
target_link_libraries(quote_recorder PRIVATE
    common
    nlohmann_json::nlohmann_json
)

# execution_service — долгоживущий сервис, читает Order-запросы из
# execution:orders (Redis Stream, consumer group), размещает их на бирже и
# публикует исход в execution:reports.
add_executable(execution_service
    execution_service/src/main.cpp
    execution_service/src/core/OrderIntake.cpp
    execution_service/src/core/OrderRouter.cpp
    execution_service/src/core/ReportPublisher.cpp
)
target_include_directories(execution_service PRIVATE execution_service/src)
target_link_libraries(execution_service PRIVATE
    common
    nlohmann_json::nlohmann_json
)

# execution_cli — командная утилита: собирает Order из аргументов и
# отправляет его в execution:orders через OrderSubmitter, не дожидаясь
# интеграции со стратегиями.
add_executable(execution_cli
    execution_cli/src/main.cpp
)
target_include_directories(execution_cli PRIVATE execution_service/src)
target_link_libraries(execution_cli PRIVATE
    common
    nlohmann_json::nlohmann_json
)

# spread_builder — офлайн-утилита: реплеит записанные бинарные снапшоты
# котировок для пары бирж и строит два ряда сырого (без комиссий и
# проскальзывания) спреда с фиксированным интервалом — entry (вход:
# купить на exchange1, продать на exchange2) и exit (выход: обратная
# сделка) — в JSON-файл для последующей визуализации.
add_executable(spread_builder
    spread_builder/src/main.cpp
    spread_builder/src/core/SpreadSeriesBuilder.cpp
)
target_include_directories(spread_builder PRIVATE spread_builder/src)
target_link_libraries(spread_builder PRIVATE
    common
    nlohmann_json::nlohmann_json
)
