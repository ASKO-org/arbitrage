#include "core/OrderRouter.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <utility>

#include "core/ReportPublisher.h"
#include "models/ExecutionReport.h"
#include "models/Fill.h"

namespace {
ExecutionReport reportFor(const Order& order, OrderStatus status, double filledQty, double avgPrice,
                           std::string errorMessage) {
    return ExecutionReport{order.orderId,
                            order.strategyId,
                            order.venue,
                            order.symbol,
                            status,
                            filledQty,
                            avgPrice,
                            std::move(errorMessage),
                            std::chrono::system_clock::now()};
}
}  // namespace

OrderRouter::OrderRouter(DatabaseRepository& repository, ReportPublisher& reportPublisher,
                         std::vector<std::unique_ptr<IExecutionConnector>> connectors, double maxAbsPosition)
    : repository_(repository), reportPublisher_(reportPublisher), maxAbsPosition_(maxAbsPosition) {
    for (auto& connector : connectors) {
        connectorsByVenue_[connector->exchangeName()] = std::move(connector);
    }
}

void OrderRouter::route(const Order& order) {
    const auto existingStatus = repository_.findOrderStatus(order.orderId);
    if (existingStatus && (*existingStatus == OrderStatus::Filled ||
                            *existingStatus == OrderStatus::Rejected ||
                            *existingStatus == OrderStatus::Cancelled)) {
        std::cout << "[OrderRouter] order " << order.orderId << " already " << toString(*existingStatus)
                  << ", skipping redelivered entry\n";
        return;
    }

    const auto connectorIt = connectorsByVenue_.find(order.venue);
    if (connectorIt == connectorsByVenue_.end()) {
        repository_.upsertOrder(order, OrderStatus::Rejected);
        reportPublisher_.publish(
            reportFor(order, OrderStatus::Rejected, 0.0, 0.0, "no execution connector for venue " + order.venue));
        return;
    }

    // Every strategy shares this account, so this is the one place that
    // sees every order regardless of source and can enforce an
    // account-wide exposure limit.
    const double signedQty = order.side == OrderSide::Buy ? order.qty : -order.qty;
    const double prospectivePosition = repository_.netPosition(order.venue, order.symbol) + signedQty;
    if (std::abs(prospectivePosition) > maxAbsPosition_) {
        repository_.upsertOrder(order, OrderStatus::Rejected);
        reportPublisher_.publish(reportFor(order, OrderStatus::Rejected, 0.0, 0.0,
                                            "would exceed max aggregate position for " + order.venue + " " +
                                                order.symbol));
        return;
    }

    // Recorded before placeOrder() so a crash mid-call still leaves a
    // non-terminal record findOrderStatus() sees next attempt — retrying
    // placeOrder() itself is safe because the exchange dedupes on
    // order.orderId as the client order id.
    repository_.upsertOrder(order, OrderStatus::Acked);
    reportPublisher_.publish(reportFor(order, OrderStatus::Acked, 0.0, 0.0, ""));

    const OrderResult result = connectorIt->second->placeOrder(order);

    if (!result.accepted) {
        repository_.upsertOrder(order, OrderStatus::Rejected);
        reportPublisher_.publish(reportFor(order, OrderStatus::Rejected, 0.0, 0.0, result.errorMessage));
        return;
    }

    if (result.filledQty > 0.0) {
        repository_.insertFill(
            Fill{order.orderId, result.filledQty, result.avgPrice, 0.0, std::chrono::system_clock::now()});
    }

    const OrderStatus finalStatus = result.fullyFilled          ? OrderStatus::Filled
                                     : result.filledQty > 0.0 ? OrderStatus::PartiallyFilled
                                                               : OrderStatus::Acked;
    repository_.upsertOrder(order, finalStatus);
    reportPublisher_.publish(reportFor(order, finalStatus, result.filledQty, result.avgPrice, ""));
}
