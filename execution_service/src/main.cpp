#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <vector>

#include "config/Config.h"
#include "config/ExecutionConfig.h"
#include "core/OrderIntake.h"
#include "core/OrderRouter.h"
#include "core/ReportPublisher.h"
#include "database/DatabaseRepository.h"
#include "execution/BinanceExecutionConnector.h"
#include "execution/BybitExecutionConnector.h"
#include "execution/IExecutionConnector.h"
#include "models/Fill.h"
#include "security/SecretsStore.h"

namespace {
std::atomic<bool> shutdownRequested{false};
void handleSignal(int) { shutdownRequested = true; }

// Resolves every order left non-terminal by a prior crash against live
// exchange state, before OrderIntake starts consuming new entries — so a
// crash between "order placed on the exchange" and "fill recorded" doesn't
// leave the database silently wrong (or, worse, get retried and
// double-executed) on restart.
void reconcile(DatabaseRepository& repository, const std::vector<std::unique_ptr<IExecutionConnector>>& connectors) {
    const auto openOrders = repository.loadOpenOrders();
    if (openOrders.empty()) {
        std::cout << "[execution_service] no open orders to reconcile\n";
        return;
    }
    std::cout << "[execution_service] reconciling " << openOrders.size() << " open order(s) from a prior run\n";

    for (const auto& [order, status] : openOrders) {
        const auto connectorIt =
            std::find_if(connectors.begin(), connectors.end(),
                         [&order](const auto& connector) { return connector->exchangeName() == order.venue; });
        if (connectorIt == connectors.end()) {
            std::cout << "  " << order.orderId << ": no connector for venue " << order.venue
                      << ", left as " << toString(status) << "\n";
            continue;
        }

        const OrderResult result = (*connectorIt)->getOrderStatus(order.orderId, order.symbol);
        if (!result.accepted) {
            std::cout << "  " << order.orderId << ": exchange has no record of it, marking rejected\n";
            repository.upsertOrder(order, OrderStatus::Rejected);
            continue;
        }

        // The exchange reports cumulative filled quantity, so only the
        // delta over what's already stored gets inserted — otherwise a fill
        // recorded before the crash (or a second reconciliation pass) would
        // be double-counted, corrupting netPosition().
        const double alreadyRecorded = repository.filledQtyForOrder(order.orderId);
        const double delta = result.filledQty - alreadyRecorded;
        if (delta > 0.0) {
            repository.insertFill(Fill{order.orderId, delta, result.avgPrice, 0.0, std::chrono::system_clock::now()});
        }

        const OrderStatus resolvedStatus = result.fullyFilled            ? OrderStatus::Filled
                                            : result.filledQty > 0.0 ? OrderStatus::PartiallyFilled
                                                                      : status;
        repository.upsertOrder(order, resolvedStatus);
        std::cout << "  " << order.orderId << ": reconciled to " << toString(resolvedStatus) << "\n";
    }
}
}  // namespace

int main() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    try {
        DatabaseRepository repository(Config::postgresConnectionString());
        repository.ensureSchema();

        SecretsStore secrets(ExecutionConfig::secretsMasterKeyPath(), ExecutionConfig::secretsFilePath());

        std::vector<std::unique_ptr<IExecutionConnector>> connectors;
        connectors.push_back(std::make_unique<BinanceExecutionConnector>(
            secrets.get("binance_api_key"), secrets.get("binance_api_secret"),
            ExecutionConfig::binanceBaseUrl()));
        connectors.push_back(std::make_unique<BybitExecutionConnector>(
            secrets.get("bybit_api_key"), secrets.get("bybit_api_secret"), ExecutionConfig::bybitBaseUrl()));

        reconcile(repository, connectors);

        ReportPublisher reportPublisher(ExecutionConfig::redisHost(), ExecutionConfig::redisPort(),
                                          ExecutionConfig::reportsStream());

        OrderRouter router(repository, reportPublisher, std::move(connectors), ExecutionConfig::maxAbsPosition());

        OrderIntake intake(ExecutionConfig::redisHost(), ExecutionConfig::redisPort(),
                            ExecutionConfig::ordersStream(), ExecutionConfig::consumerGroup(),
                            ExecutionConfig::consumerName());
        intake.ensureGroup();

        std::cout << "[execution_service] consuming " << ExecutionConfig::ordersStream() << " as group "
                  << ExecutionConfig::consumerGroup() << " / consumer " << ExecutionConfig::consumerName() << "\n";

        intake.run([&router](const Order& order) { router.route(order); },
                   [] { return shutdownRequested.load(); });

        std::cout << "[execution_service] shutting down\n";
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
