#pragma once
#include <JuceHeader.h>
#include "State.h"

#if JUCE_WINDOWS
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
 *  - Owns all COM interface pointers, held as juce::ComSmartPtr. shutdown releases them,
 *    and ~Session calls shutdown.
 *
 *  Ownership (macOS):
 *  - Owns the SBDebugger, SBListener, SBTarget, and SBProcess instances for
 *    the lifetime of the session.
 *
 *  @note The engine context runs every method, except wake. Processor calls cancel on the
 *        engine context, from applyPendingRequests. The readiness callbacks call wake.
 *        Processor calls it from its stdin callback. On macOS, the standard error stream calls
 *        it. On Windows, the output streams call it. On Windows, initialize() initializes COM
 *        in multithreaded mode.
 */
class Session
{
public:
    /** Creates a session with no engine objects. initialize creates them. */
    Session ();

    /** Calls shutdown with EndMode::passive. */
    ~Session ();

    /** Longest period, in milliseconds, of the readiness clock of each jam::AsyncOutputStream.
     *
     *  Session passes it to each stream that it opens. Processor passes it to StdioServer::open.
     */
    static constexpr double maximumPeriodMilliseconds { 1.0 };

    /** Prepare the debug engine for use. Must succeed before any other Session method.
     *
     *  Windows: initializes COM through jam::ScopedComInitialiser (multithreaded), loads
     *  dbgeng.dll from sidecarDir via Loader, calls DebugCreate to obtain IDebugClient5,
     *  QueryInterface for IDebugControl4/IDebugSymbols3/IDebugDataSpaces4/IDebugSystemObjects,
     *  registers OutputCallbacks and EventCallbacks, sets the output mask, adds source-line
     *  symbol loading, sets source-level code level for stepping, appends the srv* symbol
     *  path, and enables the initial-break engine option. On failure it calls shutdown(EndMode::passive).
     *
     *  macOS: SBDebugger::Initialize and Create, then SetAsync(true). It sets
     *  target.process.stop-on-exec to false. It redirects the debugger input to /dev/null,
     *  so liblldb does not read the DAP frames on stdin. It captures the SBListener and adds
     *  the wake broadcaster to it.
     *
     *  @param sidecarDir  Directory containing extracted dbgeng sidecar DLLs. Unused on macOS.
     *  @return true if the engine is ready for launch/attach.
     *
     *  @note Called on the engine context, by Processor::onPrepare.
     */
    bool initialize (const juce::File& sidecarDir) noexcept;

    /** Launch a process for debugging. macOS stops it at its entry point. Windows resumes it on attach.
     *
     *  Windows creates the process suspended, with its standard output and error on pipes
     *  (see launchCaptured), then attaches to it with the resume flag and no initial break.
     *  It does not call WaitForEvent. The caller polls with pollEvents() to receive the
     *  engine events.
     *
     *  macOS creates the target and starts listening for its events. It creates a FIFO at a
     *  nonexistent child of the temporary folder, named whatdbg-stderr. It opens the FIFO and
     *  starts standardError on it. It redirects the target standard error to the FIFO with an
     *  open-file action. Then it calls SBTarget::Launch with eLaunchFlagDebug | eLaunchFlagStopAtEntry.
     *  On success the target and process members are populated, and the session fires the
     *  targetProcessId event. The caller still polls via pollEvents() for the resulting stop event.
     *
     *  @param program  Full path to the executable to launch. Windows resolves it to a native
     *                  path and quotes it when it contains a space.
     *  @return true if the launch call succeeded.
     *
     *  @note On macOS the process stays at its entry point until resume is called. On Windows the
     *        process runs once the attach completes.
     */
    bool launch (const juce::String& program) noexcept;

    /** Creates the channel that the Launcher uses to send its process ID and to get the attach reply.
     *
     *  Windows creates a duplex overlapped named pipe, and keeps a duplicate of it in launcherInput.
     *  It starts an overlapped ConnectNamedPipe on the pipe. connectSource watches connectEvent for the connect.
     *  launcherOutput opens on the pipe only after the Launcher connects.
     *  A readiness source on a pipe with no client stops for good.
     *  The Windows name is the pipe name followed by the process ID.
     *  macOS creates two FIFOs in the temporary folder: the channel, and the channel with the
     *  didAttach extension. It opens launcherOutput on the channel. On both platforms, the
     *  runInTerminal event fires when the channel has bytes to read.
     *
     *  @return The channel name. The adapter passes it to the Launcher as the comm file argument.
     */
    juce::String createCommFile () noexcept;

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
     *  Windows calls disableBreakpointAtInstruction, then IDebugControl4::SetExecutionStatus with
     *  DEBUG_STATUS_STEP_INTO when a breakpoint was disabled, and with DEBUG_STATUS_GO otherwise.
     *  macOS calls SBProcess::Continue.
     */
    void resume () noexcept;

    /** Poll for debug events with a timeout.
     *
     *  Processor::process calls it on the engine context while the target runs or
     *  launches, or while a terminate deadline is pending. Windows skips the wait when
     *  isWakePending is set. Otherwise it calls IDebugControl4::WaitForEvent, with timeoutMs
     *  while a terminate deadline is set and an infinite timeout otherwise. dbgeng invokes the
     *  callbacks in Callbacks.cpp during that call. Afterwards, E_UNEXPECTED from the wait fires
     *  the terminated event. S_OK with a stepped breakpoint calls applySteppedBreakpoint. S_OK
     *  with a step pending fires the step event. macOS calls
     *  SBListener::WaitForEvent, which only accepts whole-second timeouts, so timeoutMs
     *  is rounded up to the next second before the wait. Each timeoutMs from 1 to 1000
     *  waits one second. A process event goes to the stop-reason dispatch. A breakpoint
     *  event of type LocationsAdded or LocationsResolved fires breakpointLocationFound.
     *
     *  @param timeoutMs  Maximum time to wait in milliseconds. Windows uses it only while a
     *                    terminate deadline is set. On macOS, 0 is a non-blocking poll.
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
     *  Windows closes standardOutput and standardError, then connectSource, then launcherOutput.
     *  When a connect has not completed, it cancels that connect and waits for it to end.
     *  Then it resets standardOutputPipe, standardErrorPipe, launcherInput, launcherPipe, and connectEvent.
     *  It calls IDebugClient5::EndSession with the dbgeng flag that corresponds to mode.
     *  It unregisters OutputCallbacks and EventCallbacks, and releases all COM interface pointers.
     *  It resets the symbol group cache, then resets comInitialiser, which uninitializes COM.
     *  macOS closes launcherOutput and standardError, and closes their file descriptors.
     *  It deletes the channel files and the standard error FIFO.
     *  When the debugger is valid, detach detaches the process.
     *  For terminate, it sends SIGKILL and resumes the process.
     *  For passive, it does nothing to the process.
     *  Then it destroys and terminates the SBDebugger.
     *
     *  @note Callers must pass an explicit mode. The destructor passes EndMode::passive.
     *        Processor::getEndModeForExit returns EndMode::passive when the target has exited.
     */
    void shutdown (EndMode mode) noexcept;

    /** Wakes a blocked pollEvents.
     *
     *  Processor calls it from its stdin callback. The output streams call it when they report readiness.
     *  On macOS, cancel calls it too.
     *
     *  Windows sets isWakePending. When isWaiting is set, it calls IDebugControl4::SetInterrupt
     *  with DEBUG_INTERRUPT_EXIT. macOS broadcasts the wake event on the listener.
     */
    void wake () noexcept;

    /** Requests interruption of the engine wait.
     *
     *  Processor calls it on the engine context, from applyPendingRequests, for disconnect and terminate requests.
     *
     *  Windows calls IDebugControl4::SetInterrupt with DEBUG_INTERRUPT_EXIT. macOS calls wake.
     */
    void cancel () noexcept;

    /** Called on the engine context when a disconnect or terminate request starts.
     *
     *  Windows calls IDebugControl4::GetInterrupt. macOS does nothing.
     */
    void resetCancel () noexcept;

    /** Sends the output of the target to the client as output events.
     *
     *  Windows reads the standard output stream and the standard error stream until each one
     *  returns no bytes. macOS reads the process standard output and the standard error FIFO
     *  until each one returns no bytes. On Windows, pollEvents calls it before and after each wait.
     *  On macOS, pollEvents calls it after the wait. The exited event handler of Processor also calls it.
     */
    void drainOutput () noexcept;

    /** Reads the first message from the launch channel, acts on it, and closes the channel.
     *
     *  A pid message calls attachLaunchTarget. An error message fires the error event with its text.
     *  On Windows, it closes connectSource first. The close waits for a connect callback that still opens launcherOutput.
     *  The channel closes after that message. macOS also deletes the channel files.
     *
     *  Processor::process calls it when the engine does not poll.
     */
    void drainLauncherOutput () noexcept;

    /** Dispatch map that reports engine events to Processor and StopManager.
     *
     *  Both register their handlers in their constructors. The engine context fires most
     *  events with events.get. The launch channel fires runInTerminal from its stream
     *  callback, on both platforms.
     */
    Events events;

    // ── Breakpoint API (used by BreakpointManager) ─────────────────────

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
     *  Cross-platform entry: Windows calls IDebugSymbols3::GetSourceEntriesByLine with
     *  DEBUG_GSEL_ALLOW_HIGHER and DEBUG_GSEL_NEAREST_ONLY, then calls addBreakpoint with the
     *  offset of the first entry; macOS
     *  delegates to `SBTarget::BreakpointCreateByLocation`, which lets
     *  liblldb track pending locations itself and auto-resolve as modules
     *  load (no adapter-side retry loop).
     *
     *  @param filePath  Source file path as the DAP client sent it. Windows resolves it to a
     *                   full native path against the working directory, macOS uses it as is.
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
     *  loads. macOS ignores imageName and returns ok().
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
     *  Windows first disables a breakpoint at the current instruction, then calls
     *  IDebugControl4::SetExecutionStatus(DEBUG_STATUS_STEP_OVER); requires
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
     *  Windows first disables a breakpoint at the current instruction, then calls
     *  IDebugControl4::SetExecutionStatus(DEBUG_STATUS_STEP_INTO); requires
     *  source-level code level, set during initialize. macOS calls
     *  SBThread::StepInto on the selected thread.
     *
     *  @note The step completes asynchronously, as described for stepOver.
     */
    void stepInto () noexcept;

    /** Step out of the current function to its call site.
     *
     *  Windows first disables a breakpoint at the current instruction, then executes the
     *  "gu" (go up) command via IDebugControl4::Execute. macOS
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
     *  Windows resolves path to a full native path against the working directory, then passes
     *  it to IDebugSymbols3::AppendSymbolPath. macOS appends path to the LLDB command
     *  "settings append target.debug-file-search-paths" and runs it through HandleCommand.
     *
     *  @param path  Windows: a path, resolved to a full native path against the working directory.
     *               macOS: a path, appended to the LLDB command as given.
     */
    void appendSymbolPath (const juce::String& path) noexcept;

    /** Append a path to the source search path.
     *
     *  Windows resolves path to a full native path against the working directory, then passes
     *  it to IDebugSymbols3::AppendSourcePath. macOS runs the LLDB command
     *  "settings set target.source-map" through HandleCommand, with "." and path as its two arguments.
     *
     *  @param path  Windows: a path, resolved to a full native path against the working directory.
     *               macOS: a path, used as the target of the remap of ".".
     */
    void appendSourcePath (const juce::String& path) noexcept;

    /** Appends the stack frames of the current thread to json, as DAP stack frame objects.
     *
     *  Windows walks the call stack up to maxFrames deep using
     *  IDebugControl4::GetStackTrace, or GetContextStackTrace for an unreported thread. It
     *  resolves each frame to source file, line, and
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
     *  SBValue is formatted the same way as applyToLocals. A result is returned only
     *  when the SBValue is valid and its SBError reports success.
     *
     *  @param expression  C++ expression string to evaluate (e.g. "myVar->field").
     *  @param frameIndex  Zero-based stack frame index for symbol resolution context.
     *  @param isHover     True when the DAP request context is "hover". Windows ignores it.
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
     *  no description is named "Thread " followed by its id. Then Windows appends the threads
     *  that dbgeng does not report, see appendUnreportedThreads. macOS uses
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
     *  Windows looks up the dbgeng internal thread index for systemId. When dbgeng reports the
     *  thread, Windows calls IDebugSystemObjects::SetCurrentThreadId. Otherwise it records systemId
     *  as the unreported thread, which setScope and appendStackFrames read. macOS calls
     *  SBProcess::SetSelectedThreadByID directly with systemId. All subsequent symbol
     *  queries (applyToLocals, appendStackFrames, evaluateExpression) operate on this
     *  thread's context.
     *
     *  @param systemId  OS system thread ID to make current.
     *
     *  @note Must be called while the target is stopped. On Windows, 0 fails an assert.
     */
    void setCurrentThreadBySystemId (std::uint32_t systemId) noexcept;

    /** Discards the cached symbol group (Windows) or the cached frame variables (macOS).
     *
     *  The cache is keyed on frame index, and a stop changes the program counter, so the
     *  cache must be discarded. VariableManager::resetVariablesState calls it. On Windows,
     *  shutdown calls it too.
     */
    void resetSymbolGroupCache () noexcept;

private:
    /** Writes the didAttach message to the launch channel. The Launcher reads it as the adapter's reply.
     *
     *  Windows writes the line through launcherInput, and waits when the write is pending. macOS
     *  opens the didAttach FIFO without blocking, writes the line, and closes the FIFO.
     */
    void sendDidAttach () noexcept;

    /** Attaches to the target that the Launcher reported, then sends the reply.
     *
     *  Windows calls attachSuspended. On success it calls sendDidAttach and fires the didAttach event.
     *  macOS sets the debugger to synchronous mode, attaches, and calls sendDidAttach. It fires
     *  didAttach, then sets the debugger back to asynchronous mode. It does not resume the process. On failure,
     *  both platforms fire the error event with the text "Failed to attach to the target process".
     *
     *  @param processId  OS process ID of the target that the Launcher reported.
     */
    void attachLaunchTarget (std::uint32_t processId) noexcept;

    /** Reports when the launch channel has bytes to read. It reads no bytes.
     *
     *  macOS createCommFile opens it.
     *  Windows connectSource opens it when the Launcher connects.
     *  Its callback fires runInTerminal.
     *  drainLauncherOutput closes it after the first message.
     *  shutdown closes it.
     */
    jam::AsyncOutputStream launcherOutput;

    /** Holds the bytes that drainLauncherOutput reads from the launch channel.
     *
     *  The bytes stay in the ring until a newline arrives.
     */
    jam::TerminalFIFO launcherBytes;

    /** Reports when the standard error of the target has bytes to read. It reads no bytes.
     *
     *  launch on macOS and launchCaptured on Windows set its onBytesReady callback to call wake.
     *  drainOutput reads the bytes through standardErrorBytes.
     */
    jam::AsyncOutputStream standardError;

    /** Holds the bytes of the standard error of the target. drainOutput forwards them as standardError events. */
    jam::TerminalFIFO standardErrorBytes;

#if JUCE_MAC
    /** File descriptor of the launch channel. createCommFile opens it. drainLauncherOutput and shutdown close it. */
    std::optional<int> launcherHandle;

    /** File descriptor of the standard error FIFO. launch opens it. drainOutput reads it. shutdown closes it. */
    std::optional<int> standardErrorHandle;
#endif

#if JUCE_WINDOWS
    /** Return the symbol group for the given frame, creating or updating it as needed.
     *
     *  If the cached group matches frameIndex it is returned directly. Otherwise a new
     *  group is created via IDebugSymbols3::GetScopeSymbolGroup2.
     *
     *  @param frameIndex  Zero-based stack frame index.
     *  @return Pointer to the symbol group, or nullptr on failure. Lifetime is managed
     *          by cachedSymbolGroup, a juce::ComSmartPtr.
     */
    IDebugSymbolGroup2* getOrCreateSymbolGroup (int frameIndex) noexcept;

    /** Sets the debug scope to one stack frame.
     *
     *  When unreportedThreadId is set, the call walks the stack of that thread from its CONTEXT
     *  records, and sets the scope from the record of the frame. When the walk does not reach the
     *  frame, the scope does not change. Otherwise the call sets the scope by frame index.
     *
     *  @param frameIndex  Zero-based stack frame index (0 = innermost frame).
     */
    void setScope (int frameIndex) noexcept;

    /** Returns the engine ID of the enabled breakpoint at the instruction of the innermost frame.
     *
     *  Scans every breakpoint. A match has the instruction offset of the innermost frame and has
     *  the enabled flag set.
     *
     *  @return The engine breakpoint ID, or an empty optional when no frame or no enabled breakpoint matches.
     */
    std::optional<std::int32_t> getBreakpointAtInstruction () noexcept;

    /** Disables the enabled breakpoint at the current instruction, and records its engine ID.
     *
     *  Makes the event thread current and resets the scope. When an enabled breakpoint sits at the
     *  instruction, it clears the enabled flag and stores the engine ID in steppedBreakpointEngineId.
     *  resume, stepOver, stepInto, and stepOut call it.
     */
    void disableBreakpointAtInstruction () noexcept;

    /** Re-enables the breakpoint that disableBreakpointAtInstruction disabled.
     *
     *  Clears steppedBreakpointEngineId. When no stop reason is set and no step is pending, it
     *  resumes the target with IDebugControl4::SetExecutionStatus and DEBUG_STATUS_GO. pollEvents
     *  calls it when a wait returns S_OK.
     */
    void applySteppedBreakpoint () noexcept;

    /** Appends the threads of the target process that dbgeng does not report, as DAP thread objects.
     *
     *  Walks a Toolhelp thread snapshot. Each thread of the target process that dbgeng does not find
     *  is named "Thread " followed by its ID. appendThreads calls it after the reported threads.
     *
     *  @param json  Output text that receives the thread objects.
     */
    void appendUnreportedThreads (std::string& json) noexcept;

    void appendStackFrame (std::string& json, const DEBUG_STACK_FRAME& stackFrame, int frameId) noexcept;

    juce::String getThreadName (std::uint32_t systemId) noexcept;

    juce::String getFrameName (std::uint64_t instructionOffset) noexcept;

    /** Load dbgeng.dll via loader and obtain an IDebugClient5 pointer.
     *
     *  Loads the sidecar DLL and resolves DebugCreate through it.
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
     *  stepping, appends the srv* symbol path, and enables the initial-break engine option.
     */
    void setDebugInterfaces () noexcept;

    /** Starts the target suspended, with its standard output and error on pipes, and attaches to it.
     *
     *  Sets each output stream to call wake when bytes are ready. For each pipe, it creates the server end,
     *  then the client end, then opens the stream on the server end.
     *  Each client end exists before its stream opens, since a readiness source with no client stops for good.
     *  Creates the target with the client ends of the pipes as its standard handles. Calls
     *  attachSuspended on the target. When attachSuspended fails, terminates the target with exit code 1.
     *
     *  @param commandLine  Native command line of the target.
     *  @return The HRESULT of attachSuspended, or E_FAIL when the target was not created.
     */
    HRESULT launchCaptured (const juce::String& commandLine) noexcept;

    /** Attaches the engine to a process with the resume-process flag and without an initial break.
     *
     *  @param processId  OS process ID of the target.
     *  @return The HRESULT of IDebugClient5::AttachProcess.
     */
    HRESULT attachSuspended (std::uint32_t processId) noexcept;

    /** The COM apartment that initialize opens. shutdown resets it, which uninitializes COM. */
    std::optional<jam::ScopedComInitialiser> comInitialiser;

    /** True while pollEvents waits in WaitForEvent. wake reads it to decide whether to interrupt the wait. */
    std::atomic<bool> isWaiting { false };

    /** Set by wake. pollEvents clears it, and skips WaitForEvent when it was set. */
    std::atomic<bool> isWakePending { false };

    /** Reports when the standard output of the target has bytes to read. It reads no bytes.
     *
     *  launchCaptured sets its onBytesReady callback to call wake. drainOutput reads the bytes through standardOutputBytes.
     */
    jam::AsyncOutputStream standardOutput;

    /** Holds the bytes of the standard output of the target. drainOutput forwards them as standardOutput events. */
    jam::TerminalFIFO standardOutputBytes;

    std::unique_ptr<void, decltype (&CloseHandle)> standardOutputPipe { nullptr, &CloseHandle };

    std::unique_ptr<void, decltype (&CloseHandle)> standardErrorPipe { nullptr, &CloseHandle };

    std::unique_ptr<void, decltype (&CloseHandle)> launcherPipe { nullptr, &CloseHandle };

    /** Write end of the launch channel, a duplicate of the pipe that createCommFile creates.
     *  sendDidAttach writes to it. drainLauncherOutput resets it.
     */
    std::unique_ptr<void, decltype (&CloseHandle)> launcherInput { nullptr, &CloseHandle };

    /** Manual-reset event that the system signals when the Launcher connects to launcherPipe.
     *
     *  createCommFile creates it, and sets it as the hEvent of connectOverlapped. It is manual reset,
     *  so it stays signaled after the connect. drainLauncherOutput and shutdown reset it.
     */
    std::unique_ptr<void, decltype (&CloseHandle)> connectEvent { nullptr, &CloseHandle };

    /** Overlapped record of the ConnectNamedPipe call on launcherPipe. Its hEvent is connectEvent.
     *
     *  createCommFile sets it up. The system updates it when the connect completes. shutdown reads it
     *  to find a connect that has not completed. drainLauncherOutput does not change it.
     */
    OVERLAPPED connectOverlapped {};

    /** Watches connectEvent, and opens launcherOutput on launcherPipe when the Launcher connects.
     *
     *  createCommFile arms it once. Each onReady needs one arm, so onReady runs once. open passes a
     *  nullptr context, so onReady does not run on the engine context. drainLauncherOutput and shutdown
     *  close it before they close launcherOutput.
     */
    jam::AsyncUpdater::Source connectSource;

    /** Loads dbgeng.dll from the sidecar directory and exposes DebugCreate. */
    Loader loader;

    /** Registered with client to receive OutputDebugString text from the target. */
    OutputCallbacks outputCallbacks { events };

    /** Registered with client to receive breakpoint, exception, module, and
     *  process lifecycle events.
     */
    EventCallbacks eventCallbacks { events };

    /** Root dbgeng interface obtained from DebugCreate; source of every other interface. */
    juce::ComSmartPtr<IDebugClient5> client;

    /** Execution control — resume, step, WaitForEvent, engine options. */
    juce::ComSmartPtr<IDebugControl4> control;

    /** Symbol resolution — offsets, symbol groups, search paths. */
    juce::ComSmartPtr<IDebugSymbols3> symbols;

    /** Target virtual-memory access — used to read target strings. */
    juce::ComSmartPtr<IDebugDataSpaces4> dataSpaces;

    /** Thread and event-thread queries. */
    juce::ComSmartPtr<IDebugSystemObjects> systemObjects;

    /** Symbol group for the frame identified by cachedFrameIndex, rebuilt by
     *  getOrCreateSymbolGroup when the requested frame index changes.
     */
    juce::ComSmartPtr<IDebugSymbolGroup2> cachedSymbolGroup;

    /** Frame index cachedSymbolGroup was built for, or -1 when no group is cached. */
    int cachedFrameIndex { -1 };

    /** System ID of a thread that dbgeng does not report, set by setCurrentThreadBySystemId.
     *  Zero when dbgeng reports the current thread.
     */
    std::uint32_t unreportedThreadId { 0 };

    /** Engine ID of the breakpoint that disableBreakpointAtInstruction disabled. Empty when none.
     *  applySteppedBreakpoint re-enables it and clears it.
     */
    std::optional<std::int32_t> steppedBreakpointEngineId;
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

    /** Carries the wake event. initialize adds it to the listener. wake broadcasts it, which wakes a blocked pollEvents. */
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

    /** FIFO of the launch channel, created in the temporary folder by createCommFile.
     *
     *  sendDidAttach writes to the FIFO with the didAttach extension. shutdown and drainLauncherOutput delete both FIFOs.
     */
    juce::File commFile;

    /** FIFO that carries the standard error of the target. launch creates it, and shutdown deletes it. */
    juce::File standardErrorFile;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
};

} // namespace debug
