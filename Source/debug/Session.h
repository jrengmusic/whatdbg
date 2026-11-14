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

/** Packed (engineId, resolvedLine) pair returned by addBreakpointByLocation.
 *
 *  engineId is the engine-assigned breakpoint ID. resolvedLine is 0 when the
 *  engine has not resolved a source line yet.
 */
using BreakpointLocation = jam::Union<std::int32_t,     // engineId
                                      std::uint16_t>;   // resolvedLine

/** Cross-platform debug engine wrapper used by Processor, BreakpointManager,
 *  VariableManager, and StopManager to drive a debug session: launching and attaching
 *  processes, polling for events, setting breakpoints, inspecting symbols, and stepping.
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
 *  @note The engine context runs every method, except wake and cancel. The reader thread
 *        calls wake and cancel through Processor::addRequest. On Windows, COM is initialized
 *        in multithreaded mode by initialize().
 */
class Session
{
public:
    /** Creates a session with no engine objects. initialize creates them. */
    Session () = default;

    /** Calls shutdown with EndMode::passive. */
    ~Session ();

    /** Prepare the debug engine for use. Must succeed before any other Session method.
     *
     *  Windows: CoInitializeEx (multithreaded), loads dbgeng.dll from sidecarDir via
     *  Loader, calls DebugCreate to obtain IDebugClient5, QueryInterface for
     *  IDebugControl4/IDebugSymbols3/IDebugDataSpaces4/IDebugSystemObjects, registers
     *  OutputCallbacks and EventCallbacks, sets source-level code level for stepping,
     *  and enables the initial-break engine option. On failure it calls shutdown(EndMode::passive).
     *
     *  macOS: SBDebugger::Initialize/Create (async mode), redirects the debugger's own
     *  stdin to /dev/null so liblldb does not compete with the DAP reader thread for
     *  whatdbg's stdin, and captures the SBListener.
     *
     *  @param sidecarDir  Directory containing extracted dbgeng sidecar DLLs. Unused on macOS.
     *  @return true if the engine is ready for launch/attach.
     *
     *  @note Called on the engine context, by Processor::onPrepare.
     */
    bool initialize (const juce::File& sidecarDir) noexcept;

    /** Launch a process for debugging, stopped before user code runs.
     *
     *  Windows calls IDebugClient5::CreateProcess2 and returns immediately — it does
     *  NOT call WaitForEvent, so the caller must poll via pollEvents() to receive the
     *  initial CreateProcess and loader-breakpoint events.
     *
     *  macOS creates the target, starts listening for its events, then calls
     *  SBTarget::Launch with eLaunchFlagDebug | eLaunchFlagStopAtEntry. On success the
     *  target and process members are populated, and the session fires the targetProcessId
     *  event. The caller still polls via pollEvents() for the resulting stop event.
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
     *  SBTarget::AttachToProcessWithID. Processor::onAttach stores the target process id
     *  after this call returns true.
     *
     *  @param processId  OS process ID of the target process.
     *  @return true if the attach call succeeded.
     */
    bool attach (std::uint32_t processId) noexcept;

    /** Resume execution after a break.
     *
     *  Windows calls IDebugControl4::SetExecutionStatus(DEBUG_STATUS_GO). macOS calls
     *  SBProcess::Continue.
     */
    void resume () noexcept;

    /** Poll for debug events with a timeout.
     *
     *  Processor::process calls it on the engine context while the target runs or
     *  launches, or while a terminate deadline is pending. Windows calls
     *  IDebugControl4::WaitForEvent with timeoutMs. dbgeng invokes the callbacks in
     *  Callbacks.cpp during that call. Afterwards, E_UNEXPECTED from the wait fires the
     *  terminated event, and S_OK with a step pending fires the step event. macOS calls
     *  SBListener::WaitForEvent, which only accepts whole-second timeouts, so timeoutMs
     *  is rounded up to the next second before the wait. Each timeoutMs from 1 to 1000
     *  waits one second. A process event goes to the stop-reason dispatch. A breakpoint
     *  event of type LocationsAdded or LocationsResolved fires breakpointLocationFound.
     *
     *  @param timeoutMs  Maximum time to wait in milliseconds. On macOS, 0 is a
     *                    non-blocking poll.
     */
    void pollEvents (std::uint32_t timeoutMs) noexcept;

    /** Detach from the target and release all engine resources.
     *
     *  Safe to call more than once. Processor::onReleaseResources, initialize on failure
     *  (Windows), and ~Session call it. On Windows a second call finds client null and skips
     *  EndSession. On macOS a second call finds the debugger invalid and does nothing.
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
     *        Processor::getEndModeForExit returns EndMode::passive when the target has exited.
     */
    void shutdown (EndMode mode) noexcept;

    /** Wakes a blocked pollEvents. Called on the reader thread, from Processor::addRequest.
     *
     *  macOS broadcasts the wake event on the listener. Windows does nothing.
     */
    void wake () noexcept;

    /** Requests interruption of the engine wait. Called on the reader thread, for disconnect and terminate requests.
     *
     *  Windows calls IDebugControl4::SetInterrupt with DEBUG_INTERRUPT_EXIT. macOS calls wake.
     */
    void cancel () noexcept;

    /** Called on the engine context when a disconnect or terminate request starts.
     *
     *  Windows calls IDebugControl4::GetInterrupt. macOS does nothing.
     */
    void resetCancel () noexcept;

    /** Dispatch map that reports engine events to Processor and StopManager.
     *
     *  Both register their handlers in their constructors. The session fires an event
     *  with events.get, on the engine context.
     */
    Events events;

    // ── Breakpoint API (used by BreakpointManager) ─────────────────────

    /** Resolve a source file path and line number to a resolution status.
     *
     *  Windows calls IDebugSymbols3::GetOffsetByLine, first against the full path,
     *  then against the basename with a stored-path match. It distinguishes a
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
     *  @return The engine-assigned breakpoint ID, or an empty optional when creation fails.
     */
    std::optional<std::int32_t> addBreakpoint (std::uint64_t offset) noexcept;

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
     *  @return BreakpointLocation packing (engineId, resolvedLine), or an empty
     *          optional when no location was found or BP creation failed.
     *          On macOS resolvedLine is 0 when the BP has no location yet; the
     *          caller then treats it as pending, and liblldb resolves it later.
     */
    std::optional<BreakpointLocation> addBreakpointByLocation (const juce::String& filePath,
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
     *  Windows executes ".reload /f" with the basename of imageName, via
     *  IDebugControl4::Execute. BreakpointManager::onModuleLoad calls it after a module
     *  loads. macOS is a no-op. liblldb loads a module's symbols during module-load
     *  handling, so this always returns ok().
     *
     *  @param imageName  The image file name (basename or full path) as reported
     *                    by the moduleLoad event. Unused on macOS.
     *  @return juce::Result::ok() if the reload executed without error, or always on macOS.
     */
    juce::Result loadModuleSymbols (const juce::String& imageName) noexcept;

    /** Force-reload symbols for all currently loaded modules.
     *
     *  Windows executes ".reload /f" with no module name, via IDebugControl4::Execute.
     *  BreakpointManager::onReload calls it. macOS is a no-op for the same reason as
     *  loadModuleSymbols and always returns ok().
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
     *  @note The step completes asynchronously. Windows fires the step event from
     *        pollEvents when a wait returns S_OK with a step pending. macOS fires it
     *        from the stop-reason dispatch.
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
     *  Windows opens the process with PROCESS_VM_OPERATION, PROCESS_VM_WRITE, and
     *  PROCESS_CREATE_THREAD, then calls DebugBreakProcess on it. macOS calls SBProcess::SendAsyncInterrupt on the
     *  already-bound process; processId is unused there since the Session already
     *  knows its target. The resulting break is reported as a normal stop event.
     *
     *  @param processId  OS process ID of the target to interrupt. Unused on macOS.
     */
    void interrupt (std::uint32_t processId) noexcept;

    /** Request debuggee termination without blocking the caller.
     *
     *  Sends a kill request to the target process and returns immediately.
     *  The exit is observed later through pollEvents. Processor handles the
     *  exited and terminated events. Windows calls
     *  IDebugClient5::TerminateProcesses, which targets the process this
     *  client is already debugging. macOS sends SIGKILL via SBProcess::Signal
     *  and, on success, resumes the process so it can act on the pending
     *  signal and exit. If Signal fails, it falls back to
     *  SBProcess::Kill, which asks LLDB to terminate the process directly.
     *
     *  @param processId  OS process ID of the target to terminate. Unused on both
     *                    platforms. The session already knows its bound process.
     *  @note Callers must keep polling until the exit is observed. Processor::process
     *        keeps polling while terminateDeadlineMs is set.
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

    /** Appends the stack frames of the current thread to json, as DAP stack frame objects.
     *
     *  Windows walks the call stack up to maxFrames deep using
     *  IDebugControl4::GetStackTrace and resolves each frame to source file, line, and
     *  function name via IDebugSymbols3::GetNameByOffset and GetLineByOffset. macOS
     *  reads SBThread::GetNumFrames and each SBFrame's function name and SBLineEntry
     *  directly, clamped to the real stack depth.
     *
     *  addFrame is called once for each frame, in frame order. Its return value is the
     *  id that the frame object carries. VariableManager assigns the ids.
     *
     *  Each appended frame object has these fields:
     *  - id          (int)    — the value that addFrame returned for the frame
     *  - name        (String) — function name, or "frame" (Windows) or "??" (macOS) when unavailable
     *  - source      (Object) — { name, path }; present only when line information is available
     *  - line        (int)    — one-based source line number; present only with source
     *  - column      (int)    — always 1; present only with source
     *
     *  @param json       Output text that receives the frame objects.
     *  @param maxFrames  Maximum number of frames to request. Windows requests maxFrames
     *                    frames and appends those that GetStackTrace fills. macOS appends
     *                    at most the stack depth.
     *  @param addFrame   Called with the frame index. Returns the DAP id of the frame.
     *  @return Number of frame objects appended.
     *
     *  @note Must be called while the target is stopped.
     */
    int appendStackFrames (std::string& json, int maxFrames, const std::function<int (int frameIndex)>& addFrame) noexcept;

    /** Calls function for each local variable of a stack frame, in symbol order.
     *
     *  Windows iterates the top-level symbols of the frame's symbol group, obtained through
     *  getOrCreateSymbolGroup. Value formatting uses debug::prettyPrint and
     *  debug::formatSymbolValue. macOS iterates SBFrame::GetVariables, cached by
     *  cacheFrameVariables. Value formatting uses the macOS debug::prettyPrint overload. Both
     *  sides skip the names that shouldSkipSymbol matches.
     *
     *  function receives the symbol name, the formatted value, the type name, whether the
     *  symbol has children, and the symbol index in the frame's list.
     *
     *  @param frameIndex  Zero-based stack frame index (0 = innermost frame).
     *  @param function    Called once for each local variable.
     *
     *  @note Must be called while the target is stopped.
     */
    void applyToLocals (int frameIndex, const std::function<void (const juce::String& name, const juce::String& value, const juce::String& type, bool hasChildren, int symbolIndex)>& function) noexcept;

    /** Calls function for each child of a variable, to expand it in DAP.
     *
     *  Windows calls IDebugSymbolGroup2::ExpandSymbol on the symbol at symbolIndex,
     *  then iterates the newly exposed children. macOS reads
     *  SBValue::GetNumChildren and GetChildAtIndex directly on the cached parent value.
     *  liblldb builds children on demand, with no separate expand step.
     *
     *  function receives the same arguments as in applyToLocals.
     *
     *  @param frameIndex   Zero-based stack frame index containing the parent variable.
     *  @param symbolIndex  Index of the parent symbol in the frame's symbol list.
     *  @param function     Called once for each child.
     *
     *  @note Must be called while the target is stopped.
     */
    void applyToVariableChildren (int frameIndex, int symbolIndex, const std::function<void (const juce::String& name, const juce::String& value, const juce::String& type, bool hasChildren, int symbolIndex)>& function) noexcept;

    /** Evaluate a C++ expression in the context of a specific stack frame.
     *
     *  Windows gets the frame's symbol group, adds the expression to that group with
     *  AddSymbol while SYMOPT_NO_PUBLICS is set, and formats the new symbol. The
     *  previous symbol options are restored on exit, and the added symbol is removed
     *  from the group. A result is returned only when AddSymbol returns S_OK.
     *  macOS first reads the expression as a variable path with
     *  SBFrame::GetValueForVariablePath, which needs no expression parser. Only when that
     *  fails, and the request is not a hover, it calls SBFrame::EvaluateExpression. The
     *  parser looks up an unknown name in every loaded module, which took seconds in a
     *  large host, and an editor sends a hover for each word under the cursor. The
     *  SBValue is formatted the same way as applyToLocals. A result is returned only
     *  when the SBValue is valid and its SBError reports success.
     *
     *  @param expression  C++ expression string to evaluate (e.g. "myVar->field").
     *  @param frameIndex  Zero-based stack frame index for symbol resolution context.
     *  @param isHover     True when the DAP request context is "hover". Windows ignores
     *                     it, because AddSymbol never runs an expression parser.
     *  @return Formatted result string, or an empty string when the evaluation fails.
     *          Processor::onEvaluate sends the error response for an empty string.
     *
     *  @note Must be called while the target is stopped. Complex expressions may cause
     *        the engine to execute target code (function calls in expression).
     */
    juce::String evaluateExpression (const juce::String& expression, int frameIndex, bool isHover) noexcept;

    /** Appends the threads of the target process to json, as DAP thread objects.
     *
     *  Windows uses IDebugSystemObjects::GetNumberThreads and GetThreadIdsByIndex to
     *  list all threads, and resolves each name with GetThreadDescription. A thread with
     *  no description is named "Thread " followed by its id. macOS uses
     *  SBProcess::GetNumThreads and GetThreadAtIndex, and reads each SBThread ID and name.
     *
     *  Each appended thread object has these fields:
     *  - id   (int)    — OS thread ID (system TID, not the engine's internal index)
     *  - name (String) — thread name; empty on macOS when the thread has none
     *
     *  @param json  Output text that receives the thread objects.
     *
     *  @note Must be called while the target is stopped.
     */
    void appendThreads (std::string& json) noexcept;

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
     *  queries (applyToLocals, appendStackFrames, evaluateExpression) operate on this
     *  thread's context.
     *
     *  @param systemId  OS system thread ID to make current.
     *
     *  @note Must be called while the target is stopped. On Windows, an id that dbgeng
     *        does not know is ignored, and 0 fails an assert.
     */
    void setCurrentThreadBySystemId (std::uint32_t systemId) noexcept;

    /** Discards the cached symbol group (Windows) or the cached frame variables (macOS).
     *
     *  The cache is keyed on frame index, and a stop changes the program counter, so the
     *  cache must be discarded. VariableManager::resetVariablesState calls it. shutdown calls
     *  it too.
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

    void appendStackFrame (std::string& json, const DEBUG_STACK_FRAME& stackFrame, int frameId) noexcept;

    juce::String getThreadName (std::uint32_t systemId) noexcept;

    juce::String getFrameName (std::uint64_t instructionOffset) noexcept;

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
    OutputCallbacks outputCallbacks { events };

    /** Registered with client to receive breakpoint, exception, module, and
     *  process lifecycle events.
     */
    EventCallbacks eventCallbacks { events };

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

    void appendStackFrame (std::string& json, lldb::SBFrame frame, int frameId) noexcept;

    /** The liblldb debugger instance owning target and process for this session's lifetime. */
    lldb::SBDebugger debugger;

    /** Listens for process, target, and breakpoint events; polled by pollEvents. */
    lldb::SBListener listener;

    lldb::SBBroadcaster broadcaster { "whatdbg" };

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
