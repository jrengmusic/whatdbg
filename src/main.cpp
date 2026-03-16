#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <string>
#include <thread>

#include <windows.h>

#include "BreakpointManager.hpp"
#include "CommandQueue.hpp"
#include "DapDispatcher.hpp"
#include "DapProtocol.hpp"
#include "DapTypes.hpp"
#include "DbgEngCallbacks.hpp"
#include "DbgEngSession.hpp"

// ---------------------------------------------------------------------------
// Version string — single definition, used in startup banner
// ---------------------------------------------------------------------------

static constexpr const char* kWhatdbgVersion { "0.1.0" };

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main ()
{
    // Step 1 — Binary mode for stdin/stdout.
    // \r\n translation must not corrupt the DAP Content-Length framing.
    _setmode (_fileno (stdin),  _O_BINARY);
    _setmode (_fileno (stdout), _O_BINARY);

    // Redirect stderr to a log file for diagnostics.
    // stdout is reserved for DAP JSON — stderr is the only diagnostic channel,
    // but nvim-dap doesn't surface it.  Writing to a file lets us read it
    // after the session.  The file is truncated on each launch.
    //
    // Hardcoded path next to the executable for reliability.
    {
        FILE* logFile { nullptr };
        errno_t err { freopen_s (
            &logFile,
            "C:\\Users\\jreng\\Documents\\Poems\\dev\\whatdbg\\whatdbg.log",
            "w",
            stderr) };

        if (err == 0 and logFile != nullptr)
        {
            setvbuf (stderr, nullptr, _IONBF, 0);
        }
    }

    fprintf (stderr, "WHATDBG v%s\n", kWhatdbgVersion);

    // Step 2 — Create shared objects.
    // Construction order matters: DapDispatcher holds references to both
    // DapProtocol and DbgEngSession, so those must be constructed first.
    // All three objects must outlive the event loop (Step 6).
    DapProtocol   protocol;
    CommandQueue  commandQueue;

    // Step 3 — Initialize dbgeng session.
    // Failure is non-fatal: we continue running as a DAP stub so the client
    // receives well-formed error responses rather than a dead connection.
    DbgEngSession session;
    bool isDbgengReady { session.initialize () };

    if (isDbgengReady == true)
    {
        fprintf (stderr, "WHATDBG: dbgeng initialized\n");
    }
    else
    {
        fprintf (stderr, "WHATDBG: dbgeng init failed — running without debug engine\n");
    }

    // DapDispatcher is constructed after session so it can hold a valid
    // reference to session for the launch/configurationDone handlers.
    DapDispatcher dispatcher (session, protocol);

    // Step 4 — Create and register callbacks (only when dbgeng is available).
    //
    // Both objects start with refCount = 1 (owner reference).
    // SetEventCallbacks / SetOutputCallbacks will AddRef internally.
    // We Release our owner reference during cleanup (Step 7).
    //
    // OutputCallbacks implements IDebugOutputCallbacks2, which inherits from
    // IDebugOutputCallbacks.  SetOutputCallbacks takes IDebugOutputCallbacks*.
    // static_cast is safe: the inheritance chain is direct, and dbgeng will
    // immediately QueryInterface the registered object for
    // IID_IDebugOutputCallbacks2; our QueryInterface returns the correct
    // pointer, so dbgeng routes all output through Output2.
    EventCallbacks*  eventCb  { nullptr };
    OutputCallbacks* outputCb { nullptr };

    if (isDbgengReady == true)
    {
        eventCb  = new EventCallbacks (protocol);
        outputCb = new OutputCallbacks (protocol);

        // OutputCallbacks implements IDebugOutputCallbacks2 directly.
        // registerCallbacks accepts IDebugOutputCallbacks2* and handles the
        // reinterpret_cast to IDebugOutputCallbacks* internally when calling
        // SetOutputCallbacks — keeping the unsafe cast in one place.
        bool isCbRegistered { session.registerCallbacks (eventCb, outputCb) };

        if (isCbRegistered == true)
        {
            fprintf (stderr, "WHATDBG: callbacks registered\n");
            eventCb->setBreakpointManager (dispatcher.breakpointManager ());
            eventCb->setSystemObjects (session.systemObjects ());
            eventCb->setSession (&session);
        }
        else
        {
            fprintf (stderr, "WHATDBG: callback registration failed — continuing without callbacks\n");
        }
    }

    // Step 5 — Start IO thread.
    // The IO thread owns stdin exclusively.  It blocks on readMessage() and
    // pushes each parsed request onto the command queue for the main thread.
    // isIoRunning is set to false when stdin closes (EOF or parse error), which
    // signals the main loop to stop waiting for new commands from this thread.
    // The thread is detached because it blocks on stdin indefinitely; when
    // main() exits the process terminates and the thread dies with it.
    std::atomic<bool> isIoRunning { true };
    std::thread ioThread ([&protocol, &commandQueue, &isIoRunning] ()
    {
        while (isIoRunning.load () == true)
        {
            auto msg { protocol.readMessage (std::cin) };
            if (msg.has_value () == false)
            {
                // EOF or parse error — stop reading.
                isIoRunning.store (false);
                break;
            }
            commandQueue.push (std::move (*msg));
        }
    });
    ioThread.detach ();

    // Step 6 — Main event loop.
    //
    // Three states (Phase 2: always Idle — no target launched yet):
    //   Idle    — no target attached; WaitForEvent returns E_UNEXPECTED immediately.
    //   Running — target executing; WaitForEvent polls with kWaitForEventTimeoutMs.
    //   Stopped — target broken in; commands processed, then WaitForEvent resumes.
    //
    // All COM calls (dispatch, WaitForEvent) happen on this thread.
    // The IO thread only touches the CommandQueue.

    while (dispatcher.isRunning () == true)
    {
        // Step 6a — Drain ALL pending commands before calling WaitForEvent.
        // WaitForEvent is NOT re-entrant; no COM calls may occur during the wait.
        while (auto cmd = commandQueue.tryPop ())
        {
            std::string command { cmd->value ("command", std::string {}) };

            auto response { dispatcher.dispatch (*cmd) };
            protocol.writeMessage (std::cout, response);

            // Send the initialized event immediately after the initialize
            // response.  nvim-dap waits for this event before sending
            // setBreakpoints / launch.
            if (command == "initialize")
            {
                protocol.writeMessage (std::cout, dap::makeEvent ("initialized"));
            }
        }

        // Step 6b — Poll for debug events.
        // In Phase 2 no target is attached, so WaitForEvent returns
        // E_UNEXPECTED immediately (no targets can generate events).
        // S_OK      = event occurred; callbacks already fired during the wait.
        // S_FALSE   = timeout; no event within kWaitForEventTimeoutMs ms.
        // E_UNEXPECTED = no targets; expected in Idle state.
        // E_PENDING    = exit interrupt issued.
        // All return codes are handled by callbacks or are expected non-errors.
        if (isDbgengReady == true and session.hasTarget () == true)
        {
            // WaitForEvent must be called even while waiting for configuration.
            // After CreateProcess, dbgeng queues module load events and the
            // initial breakpoint.  If we skip WaitForEvent, those events are
            // never processed and the symbol engine stays in a "partially
            // initialized" state — GetOffsetByLine returns E_UNEXPECTED.
            HRESULT hr { session.control ()->WaitForEvent (0, kWaitForEventTimeoutMs) };

            if (hr != S_OK and hr != S_FALSE and hr != E_UNEXPECTED and hr != E_PENDING)
            {
                fprintf (stderr, "WHATDBG: WaitForEvent returned unexpected hr=0x%08lX\n",
                    static_cast<unsigned long> (hr));
            }

            // After WaitForEvent: if new modules loaded, re-resolve pending breakpoints.
            //
            // LoadModule now returns DEBUG_STATUS_BREAK when pending BPs exist, so
            // WaitForEvent returns S_OK with execStatus == DEBUG_STATUS_BREAK — the
            // target is already stopped here.  No SetInterrupt polling needed.
            //
            // Steps:
            //   1. consumeModuleLoadFlag() — clears the flag set by LoadModule callback.
            //   2. Check execStatus — only resolve if the target is actually stopped.
            //   3. Reload("") — flush the dbgeng symbol engine for newly loaded modules.
            //      GetOffsetByLine returns E_UNEXPECTED if called before the symbol
            //      engine has finished parsing the PDB; Reload("") blocks until ready.
            //   4. onModuleLoad — retry GetOffsetByLine + AddBreakpoint2 for all pending.
            //   5. Resume — SetExecutionStatus(GO) unless waiting for configurationDone.
            if (eventCb != nullptr and eventCb->consumeModuleLoadFlag () == true)
            {
                auto* bpMgr { dispatcher.breakpointManager () };

                if (bpMgr != nullptr and bpMgr->hasPending () == true)
                {
                    ULONG execStatus { 0 };
                    session.control ()->GetExecutionStatus (&execStatus);

                    if (execStatus == DEBUG_STATUS_BREAK)
                    {
                        // Flush the symbol engine — ensures the PDB for the newly
                        // loaded module is fully parsed before GetOffsetByLine is called.
                        HRESULT hrReload { session.symbols ()->Reload ("") };
                        fprintf (stderr,
                                "WHATDBG: Reload(\"\") hr=0x%08lX — resolving pending breakpoints\n",
                                static_cast<unsigned long> (hrReload));

                        auto events { bpMgr->onModuleLoad ("*") };
                        for (const auto& evt : events)
                        {
                            protocol.writeMessage (std::cout, evt);
                        }

                        // Resume execution unless we are still in the configuration
                        // window (between launch and configurationDone).
                        if (session.isWaitingForConfiguration () == false)
                        {
                            session.control ()->SetExecutionStatus (DEBUG_STATUS_GO);
                        }
                    }
                    else
                    {
                        fprintf (stderr,
                                "WHATDBG: module load flag set but target not stopped "
                                "(execStatus=%lu) — BPs remain pending\n",
                                static_cast<unsigned long> (execStatus));
                    }
                }
            }
        }
        else
        {
            Sleep (kWaitForEventTimeoutMs);
        }
    }

    // Step 7 — Cleanup.
    // Unregister callbacks before releasing our owner references so dbgeng
    // does not call into freed objects during the Release sequence.
    if (isDbgengReady == true)
    {
        session.unregisterCallbacks ();

        // Release our owner references (refCount 1 → 0 → delete this).
        if (eventCb  != nullptr) { eventCb->Release ();  }
        if (outputCb != nullptr) { outputCb->Release (); }

        session.shutdown ();
    }

    fprintf (stderr, "WHATDBG: session ended\n");
    return EXIT_SUCCESS;
}
