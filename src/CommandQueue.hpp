#pragma once

#include <mutex>
#include <optional>
#include <queue>

#include <windows.h>

#include "../third_party/nlohmann/json.hpp"

// ---------------------------------------------------------------------------
// CommandQueue
//
// Thread-safe FIFO queue for passing DAP requests from the IO thread to the
// main thread.
//
// Threading contract (SPEC §Architecture — Internal threading model):
//   - push()   — called exclusively from the IO thread (stdin reader).
//   - tryPop() — called exclusively from the main thread (dbgeng event loop).
//   - empty()  — may be called from either thread for diagnostic purposes.
//
// Design rationale:
//   No condition variable is used. The main thread polls via tryPop() before
//   each WaitForEvent() call. WaitForEvent() itself acts as the heartbeat:
//   its timeout (see kWaitForEventTimeoutMs) bounds the maximum command
//   dispatch latency without requiring a separate wake mechanism.
//   WaitForEvent() is NOT re-entrant — no COM calls may occur during the
//   wait, so all dequeuing and dispatch must happen before the call.
//
// Lifetime contract:
//   - The CommandQueue instance must outlive both the IO thread and the main
//     thread event loop.
//   - No cleanup is required on destruction; std::queue and std::mutex
//     destroy cleanly.
// ---------------------------------------------------------------------------

// Maximum latency (ms) between a command being enqueued and the main thread
// dequeuing it. Equals the WaitForEvent timeout — defined here so the event
// loop and this header share a single source of truth.
static constexpr DWORD kWaitForEventTimeoutMs { 100 };

class CommandQueue
{
public:
    // Push a command onto the queue.
    // Called from the IO thread.
    void push (nlohmann::json command)
    {
        std::lock_guard<std::mutex> lock (mutex);
        queue.push (std::move (command));
    }

    // Attempt to pop the front command from the queue.
    // Returns std::nullopt if the queue is empty — non-blocking.
    // Called from the main thread.
    std::optional<nlohmann::json> tryPop ()
    {
        std::lock_guard<std::mutex> lock (mutex);
        if (queue.empty () == true)
        {
            return std::nullopt;
        }

        auto cmd { std::move (queue.front ()) };
        queue.pop ();
        return cmd;
    }

    // Returns true if the queue contains no pending commands.
    // May be called from either thread.
    bool empty () const
    {
        std::lock_guard<std::mutex> lock (mutex);
        return queue.empty ();
    }

private:
    std::queue<nlohmann::json> queue;
    mutable std::mutex         mutex;
};
