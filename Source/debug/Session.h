#pragma once
#include <JuceHeader.h>
#include "State.h"

#if JUCE_WINDOWS
#include <windows.h>
#include <wrl/client.h>
#include <dbgeng.h>
#include "Loader.h"
#include "Callbacks.h"
#endif

#if JUCE_MAC
#include <lldb/API/LLDB.h>
#endif

namespace debug
{

/** Mode for ending a debug session.
 *
 *  Cross-platform intent. Choose based on target state:
 *  - terminate: target is alive, kill it on detach
 *  - detach:    target is alive, leave it running
 *  - passive:   target has already exited; release session state only
 *
 *  Windows maps this directly to a dbgeng DEBUG_END_* flag passed to
 *  IDebugClient5::EndSession.
 */
enum class EndMode
{
    terminate,  ///< Target is alive; kill it on detach.
    detach,     ///< Target is alive; leave it running.
    passive     ///< Target has already exited; release session state only.
};

/** Packed (engineId, resolvedLine) pair returned by getBreakpoint.
 *
 *  engineId is 0 when the engine has not assigned a breakpoint ID.
 *  resolvedLine is 0 when the engine has not resolved a source line.
 */
using BreakpointLocation = jam::Union<std::int32_t,     // engineId
                                      std::uint16_t>;   // resolvedLine

/** Cross-platform debug engine wrapper used by Whatdbg and BreakpointManager to
 *  drive a debug session: launching/attaching processes, polling for events,
 *  setting breakpoints, inspecting symbols, and stepping.
 *
 *  Two independent implementations share this one declaration, selected at
 *  compile time by JUCE_WINDOWS / JUCE_MAC:
 *  - Windows: COM wrapper around dbgeng (IDebugClient5 and friends).
 *  - macOS:   wrapper around liblldb's SB API (SBDebugger, SBTarget, SBProcess).
 *
 *  Ownership (Windows):
 *  - Owns the Loader that loaded dbgeng.dll.
 *  - Owns OutputCallbacks and EventCallbacks registered with IDebugClient5.
 *  - Owns all COM interface ComPtrs; releases them in ~Session.
 *
 *  Ownership (macOS):
 *  - Owns the SBDebugger, SBListener, SBTarget, and SBProcess instances for
 *    the lifetime of the session.
 *
 *  @note All methods must be called on the main thread. On Windows, COM is initialized
 *        in multithreaded mode by initialize(). Calling any method before initialize()
 *        succeeds will assert or produce undefined behavior.
 */
class Session
{
public:
    Session () = default;
    ~Session ();

    /** Prepare the debug engine for use. Must succeed before any other Session method.
     *
     *  Windows: CoInitializeEx (multithreaded), loads dbgeng.dll from sidecarDir via
     *  Loader, calls DebugCreate to obtain IDebugClient5, QueryInterface for
     *  IDebugControl4/IDebugSymbols3/IDebugDataSpaces4/IDebugSystemObjects, registers
     *  OutputCallbacks and EventCallbacks, and sets source-level code level for stepping.
     *
     *  macOS: SBDebugger::Initialize/Create (async mode), redirects the debugger's own
     *  stdin to /dev/null so liblldb does not compete with the DAP reader thread for
     *  whatdbg's stdin, and captures the SBListener.
     *
     *  @param sidecarDir  Directory containing extracted dbgeng sidecar DLLs. Unused on macOS.
     *  @return true if the engine is ready for launch/attach.
     *
     *  @note Must be called on the main thread before any other Session method.
     */
    bool initialize (const juce::File& sidecarDir) noexcept;

    /** Launch a process for debugging, stopped before user code runs.
     *
     *  Windows calls IDebugClient5::CreateProcess2 and returns immediately — it does
     *  NOT call WaitForEvent, so the caller must poll via pollEvents() to receive the
     *  initial CreateProcess and loader-breakpoint events.
     *
     *  macOS creates the target, starts listening for its events, then calls
     *  SBTarget::Launch with eLaunchFlagDebug | eLaunchFlagStopAtEntry; on success the
     *  target/process members are already populated and State::targetProcessId is set
     *  here. The caller still polls via pollEvents() for the resulting stop event.
     *
     *  @param program  Full path to the executable to launch.
     *  @return true if the launch call succeeded.
     *
     *  @note The process is stopped before running any code until the first pollEvents call.
     */
    bool launch (const juce::String& program) noexcept;

    /** Attach to a running process by OS process ID.
     *
     *  Windows calls IDebugClient5::AttachProcess and returns immediately — it does
     *  NOT call WaitForEvent, so the caller must poll via pollEvents() to receive the
     *  initial attach-break event.
     *
     *  macOS creates an empty target, starts listening for its events, then calls
     *  SBTarget::AttachToProcessWithID. State::targetProcessId is set by the caller
     *  once this returns success — the same single-writer contract Windows relies on.
     *
     *  @param processId  OS process ID of the target process.
     *  @return true if the attach call succeeded.
     */
    bool attach (std::uint32_t processId) noexcept;

    /** Resume execution after a break.
     *
     *  Windows calls IDebugControl4::SetExecutionStatus(DEBUG_STATUS_GO). macOS calls
     *  SBProcess::Continue.
     *
     *  @note No-op if the engine is already running.
     */
    void resume () noexcept;

    /** Poll for debug events with a timeout.
     *
     *  Should be called repeatedly from the main loop while the target is running
     *  or stopping. Windows calls IDebugControl4::WaitForEvent with the specified
     *  millisecond timeout directly. macOS calls SBListener::WaitForEvent, which
     *  only accepts whole-second timeouts, so timeoutMs is rounded up to the next
     *  second before the wait — a timeoutMs of 0 still blocks for one second rather
     *  than busy-spinning; a received event is dispatched to the matching
     *  onProcessEvent / onTargetEvent / onBreakpointEvent handler before returning.
     *
     *  @param timeoutMs  Maximum time to wait in milliseconds. Pass 0 for non-blocking poll.
     *  @return true if an event was consumed; false on timeout with no event or on error.
     */
    bool pollEvents (std::uint32_t timeoutMs) noexcept;

    /** Detach from the target and release all engine resources.
     *
     *  Idempotent — safe to call more than once. run() calls shutdown() explicitly
     *  at loop exit, and ~Session() calls it again unconditionally; the second call
     *  is a no-op because the first already cleared the guard it checks (client on
     *  Windows, debugger validity on macOS).
     *
     *  @param mode  How to end the session:
     *               - EndMode::terminate — target is alive; kill it on detach
     *               - EndMode::detach    — target is alive; leave it running after detach
     *               - EndMode::passive   — target has already exited; release state only
     *
     *  Windows calls IDebugClient5::EndSession with the dbgeng flag corresponding
     *  to mode, unregisters OutputCallbacks and EventCallbacks, releases all COM
     *  interface pointers, and calls CoUninitialize if this Session owns the COM
     *  apartment. macOS signals or detaches the process per mode, then destroys
     *  and terminates the SBDebugger.
     *
     *  @note Callers must pass an explicit mode. The destructor passes EndMode::passive.
     *        Passing terminate or detach for a target that has already exited will hang;
     *        check ExecutionState before calling.
     */
    void shutdown (EndMode mode) noexcept;

    // ── Breakpoint API (used by BreakpointManager) ─────────────────────

    /** Resolve a source file path and line number to a resolution status.
     *
     *  Windows calls IDebugSymbols3::GetOffsetByLine, first against the full path
     *  then against the basename with a stored-path match, and requires symbols to
     *  already be loaded for the module containing the source file. Distinguishes a
     *  transient engine error (E_UNEXPECTED, engineBusy) from a genuine miss.
     *
     *  macOS searches every compile unit of every loaded module for a matching line
     *  entry (FindLineEntryIndex); liblldb has no transient-busy signal, so this side
     *  never returns engineBusy.
     *
     *  @param filePath  Path to the source file (backslash-form on Windows, forward-slash on macOS).
     *  @param line      One-based source line number.
     *  @return OffsetStatus::found      — symbol found; getOffset() returns the address.
     *          OffsetStatus::notFound   — no code at this line or module not loaded.
     *          OffsetStatus::engineBusy — Windows only: engine transient error; retry later.
     */
    debug::OffsetStatus getOffsetStatus (const juce::String& filePath,
                                         std::uint16_t       line) noexcept;

    /** Resolve a source file path and line number to a target virtual address.
     *
     *  Windows performs the same lookup as getOffsetStatus and returns the resolved
     *  offset. macOS performs the same compile-unit search as getOffsetStatus and
     *  returns SBLineEntry::GetStartAddress resolved to a load address via the target.
     *
     *  @param filePath  Path to the source file (backslash-form on Windows, forward-slash on macOS).
     *  @param line      One-based source line number.
     *  @return Resolved virtual address, or 0 when there is none.
     */
    std::uint64_t getOffset (const juce::String& filePath,
                             std::uint16_t       line) noexcept;

    /** Create a code breakpoint at the given virtual address.
     *
     *  Windows calls IDebugControl4::AddBreakpoint2 and enables the resulting
     *  breakpoint. macOS calls SBTarget::BreakpointCreateByAddress. Both return the
     *  engine-assigned breakpoint ID, used for subsequent removeBreakpoint calls and
     *  to match incoming breakpoint-hit notifications.
     *
     *  @param offset  Virtual address at which to set the breakpoint.
     *  @return Engine-assigned breakpoint ID on success, or 0 on failure.
     */
    std::int32_t addBreakpoint (std::uint64_t offset) noexcept;

    /** Create a breakpoint by source file and line.
     *
     *  Cross-platform entry: Windows wraps the existing
     *  `getOffsetStatus` / `getOffset` → `addBreakpoint (offset)` chain; macOS
     *  delegates to `SBTarget::BreakpointCreateByLocation`, which lets
     *  liblldb track pending locations itself and auto-resolve as modules
     *  load (no adapter-side retry loop).
     *
     *  @param filePath  Source file path, in the path form the target platform
     *                   expects (backslash-form on Windows, forward-slash on macOS).
     *  @param line      Requested line (1-based, matching DAP).
     *  @return BreakpointLocation packing (engineId, resolvedLine).
     *          engineId is 0 only when BP creation itself errored.
     *          resolvedLine is 0 when the BP has no location yet (caller
     *          treats 0 as pending — liblldb will auto-resolve).
     */
    BreakpointLocation addBreakpointByLocation (const juce::String& filePath,
                                                std::uint16_t       line) noexcept;

    /** Remove a previously created breakpoint by its engine ID.
     *
     *  Windows calls IDebugControl4::GetBreakpointById2 then RemoveBreakpoint2 and
     *  fails explicitly if the ID is not found. macOS calls SBTarget::BreakpointDelete,
     *  which reports the same not-found case as a plain false. The engine ID is no
     *  longer valid after a successful removal.
     *
     *  @param engineId  The engine-assigned breakpoint ID returned by addBreakpoint.
     *  @return juce::Result::ok() on success, or juce::Result::fail(...) if the ID is not found.
     */
    juce::Result removeBreakpoint (std::int32_t engineId) noexcept;

    /** Force-reload symbols for a specific module.
     *
     *  Windows executes ".reload /f <imageName>" via IDebugControl4::Execute after a
     *  new module loads, so its PDB symbols are available before breakpoint resolution
     *  is attempted; loading may still fail silently if the PDB is not locatable.
     *  macOS is a no-op — liblldb loads a module's symbols automatically as part of
     *  module-load handling, so this always returns ok().
     *
     *  @param imageName  The image file name (basename or full path) as reported
     *                    by the LoadModule event. Unused on macOS.
     *  @return juce::Result::ok() if the reload executed without error, or always on macOS.
     */
    juce::Result loadModuleSymbols (const juce::String& imageName) noexcept;

    /** Force-reload symbols for all currently loaded modules.
     *
     *  Windows executes ".reload /f" via IDebugControl4::Execute as a fallback when
     *  per-module reload is insufficient; can be slow when many modules are loaded,
     *  so loadModuleSymbols is preferred for targeted reloads. macOS is a no-op for
     *  the same reason as loadModuleSymbols and always returns ok().
     *
     *  @return juce::Result::ok() if the reload executed without error, or always on macOS.
     */
    juce::Result forceReloadAllSymbols () noexcept;

    /** Step over one source line without entering function calls.
     *
     *  Windows calls IDebugControl4::SetExecutionStatus(DEBUG_STATUS_STEP_OVER); requires
     *  source-level code level, set during initialize. macOS calls
     *  SBThread::StepOver(eOnlyDuringStepping) on the selected thread.
     *
     *  @note The step completes asynchronously. Windows' EventCallbacks sets
     *        State::hasStepCompleted via ChangeEngineState; macOS sets it from the
     *        stop-reason dispatch in pollEvents.
     */
    void stepOver () noexcept;

    /** Step into the next function call on the current source line.
     *
     *  Windows calls IDebugControl4::SetExecutionStatus(DEBUG_STATUS_STEP_INTO); requires
     *  source-level code level, set during initialize. macOS calls
     *  SBThread::StepInto on the selected thread.
     *
     *  @note The step completes asynchronously, as described for stepOver.
     */
    void stepInto () noexcept;

    /** Step out of the current function to its call site.
     *
     *  Windows executes the "gu" (go up) command via IDebugControl4::Execute. macOS
     *  calls SBThread::StepOutOfFrame on the thread's selected frame.
     *
     *  @note The step completes asynchronously, as described for stepOver.
     */
    void stepOut () noexcept;

    /** Interrupt a running target process.
     *
     *  Windows opens the process with the minimum rights DebugBreakProcess needs and
     *  calls DebugBreakProcess on it. macOS calls SBProcess::SendAsyncInterrupt on the
     *  already-bound process; processId is unused there since the Session already
     *  knows its target. The resulting break is reported as a normal stop event.
     *
     *  @param processId  OS process ID of the target to interrupt. Unused on macOS.
     */
    void interrupt (std::uint32_t processId) noexcept;

    /** Request debuggee termination without blocking the caller.
     *
     *  Sends a kill request to the target process and returns immediately.
     *  The resulting process-exited event is observed later via pollEvents
     *  and drained through the normal deferred-event path. Windows calls
     *  IDebugClient5::TerminateProcesses, which targets the process this
     *  client is already debugging. macOS sends SIGKILL via SBProcess::Signal
     *  and, on success, resumes the process so it can act on the pending
     *  signal and exit; if Signal fails (the process may already be in a
     *  state where a signal cannot be delivered) it falls back to
     *  SBProcess::Kill, which asks LLDB to terminate the process directly.
     *
     *  @param processId  OS process ID of the target to terminate. Unused on macOS —
     *                     the Session already knows its own bound process.
     *  @note Callers must keep polling until State::hasProcessExited fires.
     */
    void terminateDebuggee (std::uint32_t processId) noexcept;

    /** Append a path to the symbol search path.
     *
     *  Windows calls IDebugSymbols3::AppendSymbolPath — the appended path is searched
     *  in addition to any path set at initialization time. macOS runs the LLDB command
     *  "settings append target.debug-file-search-paths <path>" via HandleCommand, which
     *  is likewise additive.
     *
     *  @param path  Semicolon-delimited symbol path to append (e.g. "C:\\Symbols;srv*") on
     *               Windows; a single filesystem path on macOS.
     */
    void appendSymbolPath (const juce::String& path) noexcept;

    /** Append a path to the source search path.
     *
     *  Windows calls IDebugSymbols3::AppendSourcePath, which is additive. macOS has no
     *  additive source search path — it runs the LLDB command
     *  "settings set target.source-map \".\" \"<path>\"", which remaps "." to path and
     *  replaces the full source map on every call; current callers invoke it once.
     *
     *  @param path  Semicolon-delimited source path to append on Windows; a single
     *               filesystem path to remap "." to on macOS.
     */
    void appendSourcePath (const juce::String& path) noexcept;

    /** Return stack frames for the current thread as a DAP-formatted array.
     *
     *  Windows walks the call stack up to maxFrames deep using
     *  IDebugControl4::GetStackTrace and resolves each frame to source file, line, and
     *  function name via IDebugSymbols3::GetNameByOffset and GetLineByOffset. macOS
     *  reads SBThread::GetNumFrames and each SBFrame's function name and SBLineEntry
     *  directly, clamped to maxFrames.
     *
     *  Each element in the returned array is a DynamicObject with fields:
     *  - id          (int)    — DAP frame ID (allocated by Whatdbg, not here)
     *  - name        (String) — demangled function name, or "??" (macOS) when unavailable
     *  - source      (Object) — { name, path } if source info is available
     *  - line        (int)    — one-based source line number
     *  - column      (int)    — always 1 (column not resolved; 1-based per DAP convention)
     *
     *  @param maxFrames  Maximum number of frames to return. Clamped to stack depth.
     *  @return Array of frame objects. Empty if the stack cannot be walked.
     *
     *  @note Must be called while the target is stopped.
     */
    juce::Array<juce::var> getStackTrace (int maxFrames) noexcept;

    /** Return local variables for a given stack frame as a DAP-formatted array.
     *
     *  Windows obtains the frame's symbol group via IDebugSymbols3::GetScopeSymbolGroup2
     *  (cached — see getOrCreateSymbolGroup) and iterates its top-level symbols,
     *  delegating value formatting to debug::prettyPrint / debug::formatSymbolValue.
     *  macOS reads SBFrame::GetVariables into cachedFrameVariables (see
     *  cacheFrameVariables) and iterates it, delegating to the macOS debug::prettyPrint
     *  overload for SBValue. Both sides skip symbols matched by shouldSkipSymbol.
     *
     *  Each element in the returned array is a DynamicObject with fields:
     *  - name               (String) — symbol name
     *  - value              (String) — formatted value string
     *  - type               (String) — type name from the debug engine
     *  - hasChildren/variablesReference — non-zero/true if the variable can be expanded
     *  - symbolIndex        (int)    — index in the frame's symbol list (for child expansion)
     *
     *  @param frameIndex  Zero-based stack frame index (0 = innermost frame).
     *  @return Array of variable objects. Empty if the frame has no locals or an error occurs.
     *
     *  @note Must be called while the target is stopped.
     */
    juce::Array<juce::var> getLocals (int frameIndex) noexcept;

    /** Return child variables of an expanded composite variable.
     *
     *  Windows calls IDebugSymbolGroup2::ExpandSymbol on the symbol at symbolIndex,
     *  then iterates the newly exposed children. macOS reads
     *  SBValue::GetNumChildren/GetChildAtIndex directly on the cached parent value —
     *  liblldb builds children on demand, with no separate expand step. Used to
     *  implement DAP variable expansion (struct fields, array elements, pointer
     *  dereferences).
     *
     *  Each element in the returned array has the same schema as getLocals entries.
     *
     *  @param frameIndex    Zero-based stack frame index containing the parent variable.
     *  @param symbolIndex   Index of the parent symbol in the frame's symbol list.
     *  @return Array of child variable objects. Empty if the symbol has no children or errors.
     *
     *  @note Must be called while the target is stopped.
     */
    juce::Array<juce::var> getVariableChildren (int frameIndex, int symbolIndex) noexcept;

    /** Evaluate a C++ expression in the context of a specific stack frame.
     *
     *  Windows sets the debug scope to frameIndex, then executes "?? <expression>" on a
     *  secondary IDebugClient/IDebugControl4 pair with captured output (so the
     *  evaluation's own output does not pollute the primary client), passing the
     *  result through debug::formatSymbolValue; a juce::String result additionally
     *  resolves through a dedicated `.text.data` expression and readTargetString.
     *  macOS calls SBFrame::EvaluateExpression directly on the requested frame and
     *  formats the resulting SBValue the same way as getLocals, or returns the SBError
     *  description on failure.
     *
     *  @param expression  C++ expression string to evaluate (e.g. "myVar->field").
     *  @param frameIndex  Zero-based stack frame index for symbol resolution context.
     *  @return Formatted result string, or an error description if evaluation fails.
     *
     *  @note Must be called while the target is stopped. Complex expressions may cause
     *        the engine to execute target code (function calls in expression).
     */
    juce::String evaluateExpression (const juce::String& expression, int frameIndex) noexcept;

    /** Enumerate all threads in the target process.
     *
     *  Windows uses IDebugSystemObjects::GetNumberThreads and GetThreadIdsByIndex to
     *  list all threads, resolving each to its OS thread ID and, where available, its
     *  thread description via GetThreadDescription. macOS uses
     *  SBProcess::GetNumThreads/GetThreadAtIndex, resolving each to its SBThread ID
     *  and name.
     *
     *  Each element in the returned array is a DynamicObject with fields:
     *  - id   (int)    — OS thread ID (system TID, not the engine's internal index)
     *  - name (String) — thread description/name when available, otherwise empty
     *
     *  @return Array of thread objects. Empty if enumeration fails.
     *
     *  @note Must be called while the target is stopped.
     */
    juce::Array<juce::var> getThreads () noexcept;

    /** Return the OS thread ID of the thread that triggered the last debug event.
     *
     *  Windows calls IDebugSystemObjects::GetEventThread then
     *  GetCurrentThreadSystemId, restoring the previously current thread afterward;
     *  valid only immediately after a WaitForEvent call returns an event (i.e., while
     *  stopped). macOS reads SBProcess::GetSelectedThread's SBThread ID directly — the
     *  stop-reason dispatch in pollEvents already selects the reporting thread.
     *
     *  @return OS system thread ID, or 0 if the query fails.
     */
    std::uint32_t getEventThreadSystemId () noexcept;

    /** Set the current debug context to the thread with the given OS thread ID.
     *
     *  Windows looks up the dbgeng internal thread index for systemId and calls
     *  IDebugSystemObjects::SetCurrentThreadId. macOS calls
     *  SBProcess::SetSelectedThreadByID directly with systemId. All subsequent symbol
     *  queries (getLocals, getStackTrace, evaluateExpression) operate on this thread's
     *  context.
     *
     *  @param systemId  OS system thread ID to make current.
     *
     *  @note Must be called while the target is stopped. Invalid systemId is silently ignored.
     */
    void setCurrentThreadBySystemId (std::uint32_t systemId) noexcept;

    /** Invalidate the cached symbol group so the next getLocals call rebuilds it.
     *
     *  The symbol group cache is keyed on frame index. After a stop event the
     *  program counter changes, so the cached group is stale and must be discarded.
     *  Call this at the start of every stopped event before any variable queries.
     */
    void resetSymbolGroupCache () noexcept;

private:
#if JUCE_WINDOWS
    /** Return the symbol group for the given frame, creating or updating it as needed.
     *
     *  If the cached group matches frameIndex it is returned directly. Otherwise a new
     *  group is created via IDebugSymbols3::GetScopeSymbolGroup2.
     *
     *  @param frameIndex  Zero-based stack frame index.
     *  @return Pointer to the symbol group, or nullptr on failure. Lifetime is managed
     *          by the cachedSymbolGroup ComPtr.
     */
    IDebugSymbolGroup2* getOrCreateSymbolGroup (int frameIndex) noexcept;

    /** Load dbgeng.dll via loader and obtain an IDebugClient5 pointer.
     *
     *  Calls CoInitializeEx (multithreaded) first, recording in isComOwned
     *  whether this call newly initialized the apartment, then loads the
     *  sidecar DLL and resolves DebugCreate through it.
     *
     *  @param sidecarDir  Directory containing the extracted dbgeng sidecar DLLs.
     *  @return the IDebugClient5 pointer on success, or nullptr on failure.
     */
    IDebugClient5* getOrCreateDebugClient (const juce::File& sidecarDir) noexcept;

    /** QueryInterface client for the remaining dbgeng interfaces this Session needs.
     *
     *  Populates control, symbols, dataSpaces, and systemObjects from client.
     *
     *  @return true if every interface was obtained successfully.
     */
    bool getOrCreateDebugInterfaces () noexcept;

    /** Register callbacks and apply the engine options initialize() requires.
     *
     *  Registers outputCallbacks and eventCallbacks with client, sets the output
     *  mask, enables source-line symbol loading, sets source-level code level for
     *  stepping, and enables the initial-break engine option.
     */
    void setDebugInterfaces () noexcept;

    /** Loads dbgeng.dll from the sidecar directory and exposes DebugCreate. */
    Loader loader;

    /** Registered with client to receive OutputDebugString text from the target. */
    OutputCallbacks outputCallbacks;

    /** Registered with client to receive breakpoint, exception, module, and
     *  process lifecycle events.
     */
    EventCallbacks eventCallbacks;

    /** Root dbgeng interface obtained from DebugCreate; source of every other interface. */
    Microsoft::WRL::ComPtr<IDebugClient5> client;

    /** Execution control — resume, step, WaitForEvent, engine options. */
    Microsoft::WRL::ComPtr<IDebugControl4> control;

    /** Symbol resolution — offsets, symbol groups, search paths. */
    Microsoft::WRL::ComPtr<IDebugSymbols3> symbols;

    /** Target virtual-memory access — used to read target strings. */
    Microsoft::WRL::ComPtr<IDebugDataSpaces4> dataSpaces;

    /** Thread and event-thread queries. */
    Microsoft::WRL::ComPtr<IDebugSystemObjects> systemObjects;

    /** Symbol group for the frame identified by cachedFrameIndex, rebuilt by
     *  getOrCreateSymbolGroup when the requested frame index changes.
     */
    Microsoft::WRL::ComPtr<IDebugSymbolGroup2> cachedSymbolGroup;

    /** Frame index cachedSymbolGroup was built for, or -1 when no group is cached. */
    int cachedFrameIndex { -1 };

    /** True if this Session called CoInitializeEx and therefore owns the COM apartment.
     *
     *  When true, shutdown() calls CoUninitialize. When false (COM was already
     *  initialized by the caller), shutdown() skips CoUninitialize.
     */
    bool isComOwned { false };
#endif

#if JUCE_MAC
    /** Refresh cachedFrameVariables for the given frame if stale.
     *
     *  Mirrors Windows getOrCreateSymbolGroup — keyed on frame index; regenerates
     *  the SBValueList when the cached index does not match. Callers may rely on
     *  cachedFrameVariables being valid for frameIndex after this returns.
     *
     *  @param frameIndex  Zero-based stack frame index (0 = innermost).
     */
    void cacheFrameVariables (int frameIndex) noexcept;

    /** Return a DAP variable object built from an SBValue.
     *
     *  Constructs a juce::var wrapping a DynamicObject with the schema:
     *  name, value, type, hasChildren, symbolIndex. Null-safe on every
     *  const char* return from SBValue. Used by getLocals and getVariableChildren
     *  to share a single source of truth for the variable schema.
     *
     *  @param value         The SBValue to format.
     *  @param symbolIndex   Index to embed as the DAP symbolIndex field.
     *  @return juce::var holding the variable object.
     */
    juce::var getVariableObject (lldb::SBValue& value, int symbolIndex) noexcept;

    /** The liblldb debugger instance owning target and process for this session's lifetime. */
    lldb::SBDebugger debugger;

    /** Listens for process, target, and breakpoint events; polled by pollEvents. */
    lldb::SBListener listener;

    /** The debug target created by launch or attach. */
    lldb::SBTarget target;

    /** The debuggee process bound to target once launch or attach succeeds. */
    lldb::SBProcess process;

    /** Variable list for the frame identified by cachedFrameIndex, rebuilt by
     *  cacheFrameVariables when the requested frame index changes.
     */
    lldb::SBValueList cachedFrameVariables;

    /** Frame index cachedFrameVariables was built for, or -1 when no cache is valid. */
    int cachedFrameIndex { -1 };
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
};

} // namespace debug
