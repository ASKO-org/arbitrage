#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

#include "config/ExecutionConfig.h"
#include "execution/OrderSubmitter.h"
#include "models/Order.h"

namespace {

// Not cryptographic — just needs to be unique enough to serve as the
// idempotency key both this stream entry and the exchange's client-order-id
// dedupe on.
std::string generateOrderId() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream oss;
    oss << "cli-" << std::hex << dist(rng) << dist(rng);
    return oss.str();
}

[[noreturn]] void printUsageAndExit() {
    std::cerr << "Usage: execution_cli --venue <name> --symbol <sym> --side <buy|sell> "
                 "--type <market|limit> --qty <qty> [--price <price>] [--strategy-id <id>]\n";
    std::exit(1);
}

std::string requireArg(int argc, char** argv, int& i) {
    if (i + 1 >= argc) printUsageAndExit();
    return argv[++i];
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Order order;
        order.orderId = generateOrderId();
        order.strategyId = "manual-cli";
        order.submittedAt = std::chrono::system_clock::now();

        bool haveVenue = false, haveSymbol = false, haveSide = false, haveType = false, haveQty = false;

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--venue") {
                order.venue = requireArg(argc, argv, i);
                haveVenue = true;
            } else if (arg == "--symbol") {
                order.symbol = requireArg(argc, argv, i);
                haveSymbol = true;
            } else if (arg == "--side") {
                order.side = orderSideFromString(requireArg(argc, argv, i));
                haveSide = true;
            } else if (arg == "--type") {
                order.type = orderTypeFromString(requireArg(argc, argv, i));
                haveType = true;
            } else if (arg == "--qty") {
                order.qty = std::stod(requireArg(argc, argv, i));
                haveQty = true;
            } else if (arg == "--price") {
                order.price = std::stod(requireArg(argc, argv, i));
            } else if (arg == "--strategy-id") {
                order.strategyId = requireArg(argc, argv, i);
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                printUsageAndExit();
            }
        }

        if (!haveVenue || !haveSymbol || !haveSide || !haveType || !haveQty) printUsageAndExit();
        if (order.type == OrderType::Limit && order.price <= 0.0) {
            std::cerr << "--price is required (and must be positive) for limit orders\n";
            return 1;
        }

        OrderSubmitter submitter(ExecutionConfig::redisHost(), ExecutionConfig::redisPort(),
                                  ExecutionConfig::ordersStream());
        const std::string entryId = submitter.submit(order);
        std::cout << "Submitted order " << order.orderId << " as stream entry " << entryId << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
