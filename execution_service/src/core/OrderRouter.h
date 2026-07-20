#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "database/DatabaseRepository.h"
#include "execution/IExecutionConnector.h"
#include "models/Order.h"

class ReportPublisher;

// The one chokepoint every order passes through regardless of which
// strategy or connector produced it: checks idempotency and aggregate
// exposure, dispatches to the right venue connector, then persists the
// outcome (common/database/DatabaseRepository.h) and publishes it
// (ReportPublisher).
class OrderRouter {
public:
    OrderRouter(DatabaseRepository& repository, ReportPublisher& reportPublisher,
                std::vector<std::unique_ptr<IExecutionConnector>> connectors, double maxAbsPosition);

    // Idempotent: a redelivered entry whose order is already in a terminal
    // status (filled/rejected/cancelled) is skipped; anything else is
    // (re)routed to its venue connector.
    void route(const Order& order);

private:
    DatabaseRepository& repository_;
    ReportPublisher& reportPublisher_;
    std::unordered_map<std::string, std::unique_ptr<IExecutionConnector>> connectorsByVenue_;
    double maxAbsPosition_;
};
