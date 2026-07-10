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
    common/marketdata/BinanceMarketDataConnector.cpp
    common/marketdata/BybitMarketDataConnector.cpp
    common/marketdata/KucoinSpotMarketDataConnector.cpp
    common/marketdata/KucoinFutMarketDataConnector.cpp
    common/marketdata/BitgetSpotMarketDataConnector.cpp
    common/marketdata/BitgetFutMarketDataConnector.cpp
    common/marketdata/HtxSpotMarketDataConnector.cpp
    common/marketdata/HtxFutMarketDataConnector.cpp
    common/core/QuoteStore.cpp
)
target_include_directories(common PUBLIC common)
target_link_libraries(common PUBLIC
    libpqxx::pqxx
    ixwebsocket
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
    ZLIB::ZLIB
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
target_include_directories(arbitrage_scanner PRIVATE arbitrage_scanner/src ${HIREDIS_INCLUDE_DIRS})
target_link_libraries(arbitrage_scanner PRIVATE
    common
    nlohmann_json::nlohmann_json
    ${HIREDIS_LIBRARIES}
)

# quote_recorder — долгоживущий сервис, периодически сохраняет best bid/ask
# для инструментов из recorder_watchlist в Postgres.
add_executable(quote_recorder
    quote_recorder/src/main.cpp
    quote_recorder/src/core/QuoteSnapshotWriter.cpp
)
target_include_directories(quote_recorder PRIVATE quote_recorder/src)
target_link_libraries(quote_recorder PRIVATE
    common
    nlohmann_json::nlohmann_json
)
