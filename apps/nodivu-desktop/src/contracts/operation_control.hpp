#pragma once
#include <QtGlobal>
#include <atomic>

namespace nodivu {
// One atomic transition arbitrates cancellation against publishing a transaction.
struct OperationControl {
    std::atomic<qint64> received{0};
    std::atomic<qint64> expected{0};
    std::atomic_bool cancelled{false};
    std::atomic_int phase{0}; // 0: cancellable, 1: cancelled, 2: committing
    bool cancel() {
        int running = 0;
        if (!phase.compare_exchange_strong(running, 1))
            return false;
        cancelled.store(true);
        return true;
    }
    bool beginCommit() {
        int running = 0;
        return phase.compare_exchange_strong(running, 2);
    }
};
} // namespace nodivu
