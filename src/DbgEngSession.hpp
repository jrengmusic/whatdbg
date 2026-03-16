#pragma once

#include <string>
#include <vector>

#include <windows.h>
#include <dbgeng.h>

// ---------------------------------------------------------------------------
// DbgEngSession
//
// Owns the dbgeng COM interface lifetime for one debug session.
// Initialized once on the main thread (MTA COM apartment).
// All interface pointers are valid between initialize() and shutdown().
//
// Lifetime contract:
//   - Call initialize() once before any other method.
//   - Call shutdown() exactly once when the session ends.
//   - Do NOT call initialize() again after shutdown() — create a new instance.
// ---------------------------------------------------------------------------

class DbgEngSession
{
public:
    // Initialize COM (MTA) + DebugCreate + QueryInterface for all interfaces.
    // Returns false if any step fails; logs HRESULT to stderr.
    bool initialize ();

    // Clean shutdown: EndSession(DETACH) + Release all interfaces in reverse
    // acquisition order + CoUninitialize.
    void shutdown ();

    // Raw interface accessors — dbgeng owns the object lifetime.
    // Valid only while isInitialized() == true.
    IDebugClient5*        client ()        const;
    IDebugControl4*       control ()       const;
    IDebugSymbols3*       symbols ()       const;
    IDebugRegisters2*     registers ()     const;
    IDebugDataSpaces4*    dataSpaces ()    const;
    IDebugSystemObjects4* systemObjects () const;

    bool isInitialized () const;

    // Returns true once a debug target has been attached or launched.
    // Used by shutdown() to guard the EndSession call — EndSession fails
    // with E_UNEXPECTED when no target has ever been attached.
    // Set to true by Phase 3 launch/attach handlers.
    bool hasTarget () const;
    void setHasTarget ();
    void clearTarget ();

    // True while the process is held suspended between launch/attach and configurationDone.
    // The main event loop must NOT call WaitForEvent during this window.
    bool isWaitingForConfiguration () const;
    void setWaitingForConfiguration (bool isWaiting);

    // Append a directory to the symbol search path.
    // Call BEFORE attach/launch so dbgeng knows where to find PDB files.
    void appendSymbolPath (const std::string& path);

    // Append a directory to the source search path.
    // dbgeng uses the source path to resolve relative paths stored in PDBs.
    // When a PDB contains "..\..\Source\File.cpp", dbgeng tries overlap
    // matching against each source path directory to find the actual file.
    // Call with the project root (cwd / workspaceFolder) so GetOffsetByLine
    // can resolve source-line breakpoints regardless of PDB path format.
    void appendSourcePath (const std::string& path);

    // Force-reload symbols for all loaded modules.
    // Call AFTER the target is fully initialized (e.g. in configurationDone).
    void reloadSymbols ();

    // Register event and output callbacks with the debug client.
    // Must be called AFTER initialize() and BEFORE any debug operations.
    // Returns false if either SetEventCallbacks or SetOutputCallbacks fails.
    //
    // SetOutputCallbacks takes IDebugOutputCallbacks*, but our OutputCallbacks
    // implements IDebugOutputCallbacks2 (a separate COM interface — NOT a
    // subclass of IDebugOutputCallbacks in the dbgeng headers).  We accept
    // IDebugOutputCallbacks2* here and reinterpret_cast internally when
    // calling SetOutputCallbacks.  dbgeng will immediately QueryInterface the
    // registered object for IID_IDebugOutputCallbacks2; our QueryInterface
    // returns the correct pointer, so dbgeng routes all output through Output2.
    bool registerCallbacks (IDebugEventCallbacks*   eventCb,
                            IDebugOutputCallbacks2* outputCb);

    // Unregister callbacks by passing nullptr to both SetEventCallbacks and
    // SetOutputCallbacks.  Call before releasing the callback objects so
    // dbgeng does not invoke freed memory during its own Release sequence.
    void unregisterCallbacks ();

    // Launch a process under the debugger.
    // Builds the command line from program + args, calls CreateProcess2,
    // then waits up to 10 seconds for the initial process-creation event.
    // Returns true on success and sets hasTarget.
    // Logs HRESULT to stderr on failure.
    bool launch (const std::string& program,
                 const std::string& cwd,
                 const std::vector<std::string>& args);

    // Attach to a running process by PID.
    // Calls AttachProcess then waits up to 10 seconds for the attach event.
    // Returns true on success and sets hasTarget.
    // Logs HRESULT to stderr on failure.
    bool attach (ULONG pid);

    // Resume execution (called after configurationDone when stopOnEntry is false).
    // Calls SetExecutionStatus(DEBUG_STATUS_GO).
    // Returns true if the call succeeded.
    bool resume ();

private:
    IDebugClient5*        clientPtr        { nullptr };
    IDebugControl4*       controlPtr       { nullptr };
    IDebugSymbols3*       symbolsPtr       { nullptr };
    IDebugRegisters2*     registersPtr     { nullptr };
    IDebugDataSpaces4*    dataSpacesPtr    { nullptr };
    IDebugSystemObjects4* systemObjectsPtr { nullptr };
    bool                  isInitialized_        { false };
    bool                  isHasTarget           { false };
    bool                  isWaitingForConfig    { false };
};
