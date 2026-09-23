#pragma once

#include "BestFitTnaSolver.h"
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace alice2 {

// The worker owns its solver. The UI consumes independent snapshots; it never
// reads mutable worker state or accesses rendering objects from this thread.
class BestFitTnaWorker {
public:
    ~BestFitTnaWorker() { discard(); }

    void start(BestFitTnaSolver solver) {
        discard();
        m_thread = std::jthread([this, solver = std::move(solver)](std::stop_token stop) mutable {
            try {
                while (solver.stage() == BestFitTnaStage::Fitting) {
                    solver.step(stop);
                    // Copy outside the lock so polling never waits for a copy.
                    auto snapshot = solver;
                    std::lock_guard lock(m_mutex);
                    m_latest = std::move(snapshot);
                }
            } catch (...) {
                std::lock_guard lock(m_mutex);
                m_error = std::current_exception();
            }
        });
    }

    void requestStop() { m_thread.request_stop(); }

    // On reload/restart/cleanup, join before dropping the old snapshot, so an
    // obsolete job cannot publish over a newer design. Inner solves cooperate.
    void discard() {
        requestStop();
        if (m_thread.joinable()) m_thread.join();
        std::lock_guard lock(m_mutex);
        m_latest.reset();
        m_error = nullptr;
    }

    std::optional<BestFitTnaSolver> poll() {
        std::lock_guard lock(m_mutex);
        if (m_error) {
            auto error = std::exchange(m_error, nullptr);
            std::rethrow_exception(error);
        }
        return std::exchange(m_latest, std::nullopt);
    }

private:
    std::mutex m_mutex;
    std::optional<BestFitTnaSolver> m_latest;
    std::exception_ptr m_error;
    std::jthread m_thread;
};

} // namespace alice2
