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
    // Execution states:
    //   Idle    — no target attached; skip WaitForEvent entirely.
    //   Running — target executing; call WaitForEvent to poll for events.
    //   Stopped — target stopped at user breakpoint; DO NOT call WaitForEvent
    //             (it resumes the target).  Only resume on continue/step.
    //
    // CRITICAL: WaitForEvent on a stopped target RESUMES execution.
    // dbgeng calls ContinueDebugEvent internally.  We must not call
    // WaitForEvent unless the target is actually running.
    //
    // All COM calls (dispatch, WaitForEvent) happen on this thread.
    // The IO thread only touches the CommandQueue.

    bool isTargetStopped { false };

    while (dispatcher.isRunning () == true)
    {
        // Step 6a — Drain ALL pending commands before calling WaitForEvent.
        // WaitForEvent is NOT re-entrant; no COM calls may occur during the wait.
        while (auto cmd = commandQueue.tryPop ())
        {
            std::string command { cmd->value ("command", std::string {}) };

            auto response { dispatcher.dispatch (*cmd) };
            protocol.writeMessage (std::cout, response);

            // Commands that resume the target clear the stopped flag so
            // WaitForEvent is called again on the next loop iteration.
            if (command == "continue" or command == "next"
                or command == "stepIn" or command == "stepOut"
                or command == "configurationDone")
            {
                isTargetStopped = false;
            }

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
        if (isDbgengReady == true and session.hasTarget () == true
            and isTargetStopped == false
            and session.isWaitingForConfiguration () == false)
        {
            // Only call WaitForEvent when the target is RUNNING.
            //
            // WaitForEvent on a stopped target resumes execution (dbgeng calls
            // ContinueDebugEvent internally).  We must skip it when:
            //   - isTargetStopped: target at user breakpoint, waiting for continue/step
            //   - isWaitingForConfiguration: target at initial breakpoint, waiting for
            //     configurationDone.  Symbols are force-loaded in launch(), so
            //     setBreakpoints resolves immediately — no WaitForEvent needed.
            HRESULT hr { session.control ()->WaitForEvent (0, kWaitForEventTimeoutMs) };

            if (hr != S_OK and hr != S_FALSE and hr != E_UNEXPECTED and hr != E_PENDING)
            {
                fprintf (stderr, "WHATDBG: WaitForEvent returned unexpected hr=0x%08lX\n",
                    static_cast<unsigned long> (hr));
            }

            // After WaitForEvent: check for pending events in priority order.
            //
            // 1. Breakpoint stop — emit stopped event, discard module load flag.
            // 2. Module load — re-resolve pending breakpoints, resume target.
            // 3. Process exit — emit exited + terminated events.
            //
            // If a breakpoint fires in the same WaitForEvent cycle as a module
            // load, the breakpoint wins — the target stays stopped.
            if (eventCb != nullptr)
            {
                auto stoppedBody { eventCb->consumeBreakpointStop () };

                if (stoppedBody.has_value () == true)
                {
                    protocol.writeMessage (std::cout,
                        dap::makeEvent ("stopped", *stoppedBody));

                    isTargetStopped = true;
                    eventCb->consumeModuleLoadFlag ();
                }
                else if (eventCb->consumeModuleLoadFlag () == true)
                {
                    auto* bpMgr { dispatcher.breakpointManager () };

                    if (bpMgr != nullptr and bpMgr->hasPending () == true)
                    {
                        ULONG execStatus { 0 };
                        session.control ()->GetExecutionStatus (&execStatus);

                        if (execStatus == DEBUG_STATUS_BREAK)
                        {
                            HRESULT hrReload { session.symbols ()->Reload ("/f") };
                            fprintf (stderr,
                                    "WHATDBG: Reload(\"/f\") hr=0x%08lX — resolving pending breakpoints\n",
                                    static_cast<unsigned long> (hrReload));

                            auto events { bpMgr->onModuleLoad ("*") };
                            for (const auto& evt : events)
                            {
                                protocol.writeMessage (std::cout, evt);
                            }

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

                auto exitCode { eventCb->consumeExitEvent () };

                if (exitCode.has_value () == true)
                {
                    protocol.writeMessage (std::cout, dap::makeEvent ("exited",
                    {
                        { "exitCode", *exitCode }
                    }));

                    protocol.writeMessage (std::cout, dap::makeEvent ("terminated"));
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
