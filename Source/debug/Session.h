#pragma once
#include <JuceHeader.h>
#include <cstdint>
#include "State.h"

#if JUCE_WINDOWS
#include <windows.h>
#include <wrl/client.h>
#include <dbgeng.h>
#include "Loader.h"
#include "Callbacks.h"
#endif

#if JUCE_MAC
// macOS-specific includes land here at Phase 2+
#endif

namespace debug
{

/** COM wrapper around the dbgeng debug engine interfaces.
 *
 *  Session owns and manages the lifetime of all IDebug* COM interfaces acquired
 *  from dbgeng.dll. It provides a high-level API used by Whatdbg and BreakpointManager
 *  to drive a debug session: launching/attaching processes, polling for events,
 *  setting breakpoints, inspecting symbols, and stepping.
 *
 *  Ownership:
 *  - Owns the Loader that loaded dbgeng.dll.
 *  - Owns OutputCallbacks and EventCallbacks registered with IDebugClient5.
 *  - Owns all COM interface ComPtrs; releases them in ~Session.
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

    /** Initialize COM, load dbgeng DLLs, create the client, QI all interfaces, and register callbacks.
     *
     *  Performs the full COM setup sequence:
     *  1. CoInitializeEx (multithreaded)
     *  2. Loads dbgeng.dll from sidecarDir via Loader
     *  3. Calls DebugCreate to obtain IDebugClient5
     *  4. QueryInterface for IDebugControl4, IDebugSymbols3, IDebugDataSpaces4, IDebugSystemObjects
     *  5. Registers OutputCallbacks and EventCallbacks
     *  6. Sets source-level code level for stepping
     *
     *  @param sidecarDir  Directory containing extracted dbgeng sidecar DLLs.
     *  @return true if all COM interfaces initialized successfully.
     *
     *  @note Must be called on the main thread before any other Session method.
     */
    bool initialize (const juce::File& sidecarDir) noexcept;

    /** Launch a process for debugging.
     *
     *  Calls IDebugClient5::CreateProcess2 with the given program path. Does NOT
     *  call WaitForEvent — the caller must poll via pollEvents() to receive the
     *  initial CreateProcess and loader-breakpoint events.
     *
     *  @param program  Full path to the executable to launch.
     *  @return true if CreateProcess2 succeeded.
     *
     *  @note The process is launched suspended until the first pollEvents call.
     */
    bool launch (const juce::String& program) noexcept;

    /** Attach to a running process by OS process ID.
     *
     *  Calls IDebugClient5::AttachProcess. Does NOT call WaitForEvent — the caller
     *  must poll via pollEvents() to receive the initial attach-break event.
     *
     *  @param processId  OS process ID of the target process.
     *  @return true if AttachProcess succeeded.
     */
    bool attach (std::uint32_t processId) noexcept;

    /** Resume execution after a break.
     *
     *  Calls IDebugControl4::SetExecutionStatus(DEBUG_STATUS_GO) to release
     *  the engine from a stopped state.
     *
     *  @note No-op if the engine is already running.
     */
    void resume () noexcept;

    /** Poll for debug events with a timeout.
     *
     *  Calls IDebugControl4::WaitForEvent with the specified timeout. Should be
     *  called repeatedly from the main loop while the target is running or stopping.
     *
     *  @param timeoutMs   Maximum time to wait in milliseconds. Pass 0 for non-blocking poll.
     *  @param outHadEvent Set to true if an event was consumed; false on timeout with no event.
     *  @return juce::Result::ok() on S_OK or S_FALSE (timeout is not a failure);
     *          juce::Result::fail(...) on genuine errors.
     */
    juce::Result pollEvents (std::uint32_t timeoutMs, bool& outHadEvent) noexcept;

    /** Detach from the target and release all COM resources.
     *
     *  Calls IDebugClient5::EndSession with either DEBUG_END_ACTIVE_TERMINATE or
     *  DEBUG_END_ACTIVE_DETACH depending on shouldTerminate. Releases all COM
     *  interface pointers and calls CoUninitialize if this Session owns the STA.
     *
     *  @param shouldTerminate  When true, the target process is killed on detach.
     *                          When false, the target continues running after detach.
     */
    void shutdown (bool shouldTerminate = false) noexcept;

    // ── Breakpoint API (used by BreakpointManager) ─────────────────────

    /** Resolve a source file path and line number to a target virtual address.
     *
     *  Calls IDebugSymbols3::GetOffsetByLine. Requires that symbols are loaded
     *  for the module containing the specified source file.
     *
     *  @param filePath   Full Windows path to the source file.
     *  @param line       One-based source line number.
     *  @param outOffset  Receives the resolved virtual address when resolved is returned.
     *  @return ResolveStatus::resolved   — symbol found; *outOffset populated.
     *          ResolveStatus::notFound   — no code at this line or module not loaded.
     *          ResolveStatus::engineBusy — engine transient error (E_UNEXPECTED); retry later.
     */
    debug::ResolveStatus getOffsetByLine (const juce::String& filePath,
                                          std::uint32_t       line,
                                          std::uint64_t*      outOffset) noexcept;

    /** Reverse-map a virtual address to its source file path and line number.
     *
     *  Calls IDebugSymbols3::GetLineByOffset. Used to record the resolved (actual)
     *  line of a breakpoint, which may differ from the requested line.
     *
     *  @param offset       Virtual address to look up.
     *  @param outFilePath  Receives the source file path on success.
     *  @param outLine      Receives the one-based source line number on success.
     *  @return juce::Result::ok() on success, or juce::Result::fail(...) if the address has no source info.
     */
    juce::Result getLineByOffset (std::uint64_t offset, juce::String& outFilePath, std::uint32_t* outLine) noexcept;

    /** Create a code breakpoint at the given virtual address.
     *
     *  Calls IDebugControl4::AddBreakpoint to insert an execution breakpoint.
     *  The returned engine ID is used for subsequent removeBreakpoint calls and
     *  to match incoming Breakpoint callback notifications.
     *
     *  @param offset      Virtual address at which to set the breakpoint.
     *  @param outEngineId Receives the dbgeng-assigned breakpoint ID on success.
     *  @return juce::Result::ok() on success, or juce::Result::fail(...) on failure.
     */
    juce::Result addBreakpoint (std::uint64_t offset, std::uint32_t* outEngineId) noexcept;

    /** Remove a previously created breakpoint by its engine ID.
     *
     *  Calls IDebugControl4::RemoveBreakpoint. The engine ID is no longer valid
     *  after a successful removal.
     *
     *  @param engineId  The engine-assigned breakpoint ID returned by addBreakpoint.
     *  @return juce::Result::ok() on success, or juce::Result::fail(...) if the ID is not found.
     */
    juce::Result removeBreakpoint (std::uint32_t engineId) noexcept;

    /** Force-reload PDB symbols for a specific module.
     *
     *  Executes ".reload /f <imageName>" via IDebugControl4::Execute. Used after
     *  a new module loads to ensure its symbols are available before breakpoint
     *  resolution is attempted.
     *
     *  @param imageName  The image file name (basename or full path) as reported
     *                    by the LoadModule event.
     *  @return juce::Result::ok() if the command executed without error.
     *
     *  @note Symbol loading may still fail silently if the PDB is not locatable.
     */
    juce::Result loadModuleSymbols (const juce::String& imageName) noexcept;

    /** Force-reload PDB symbols for all currently loaded modules.
     *
     *  Executes ".reload /f" via IDebugControl4::Execute. Intended as a fallback
     *  when per-module reload is insufficient.
     *
     *  @return juce::Result::ok() if the command executed without error.
     *
     *  @note Can be slow when many modules are loaded. Prefer loadModuleSymbols
     *        for targeted reloads after individual module-load events.
     */
    juce::Result forceReloadAllSymbols () noexcept;

    /** Step over one source line without entering function calls.
     *
     *  Calls IDebugControl4::SetExecutionStatus(DEBUG_STATUS_STEP_OVER).
     *  Requires source-level code level (set during initialize).
     *
     *  @note The step completes asynchronously. EventCallbacks sets
     *        State::hasStepCompleted when the step is done.
     */
    void stepOver () noexcept;

    /** Step into the next function call on the current source line.
     *
     *  Calls IDebugControl4::SetExecutionStatus(DEBUG_STATUS_STEP_INTO).
     *  Requires source-level code level (set during initialize).
     *
     *  @note The step completes asynchronously. EventCallbacks sets
     *        State::hasStepCompleted when the step is done.
     */
    void stepInto () noexcept;

    /** Step out of the current function to its call site.
     *
     *  Calls IDebugControl4::SetExecutionStatus(DEBUG_STATUS_STEP_BRANCH) to
     *  execute until the current function returns.
     *
     *  @note The step completes asynchronously. EventCallbacks sets
     *        State::hasStepCompleted when the step is done.
     */
    void stepOut () noexcept;

    /** Interrupt a running target process.
     *
     *  Calls DebugBreakProcess on the target to force it into a stopped state.
     *  The resulting break is reported via EventCallbacks as a normal stop event.
     *
     *  @param processId  OS process ID of the target to interrupt.
     */
    void interrupt (std::uint32_t processId) noexcept;

    /** Append a path to the dbgeng symbol search path.
     *
     *  Calls IDebugSymbols3::AppendSymbolPath. The appended path is searched
     *  in addition to any path set at initialization time.
     *
     *  @param path  Semicolon-delimited symbol path to append (e.g. "C:\\Symbols;srv*").
     */
    void appendSymbolPath (const juce::String& path) noexcept;

    /** Append a path to the dbgeng source search path.
     *
     *  Calls IDebugSymbols3::AppendSourcePath. Used to help dbgeng locate source
     *  files when resolving line information.
     *
     *  @param path  Semicolon-delimited source path to append.
     */
    void appendSourcePath (const juce::String& path) noexcept;

    /** Return stack frames for the current thread as a DAP-formatted array.
     *
     *  Walks the call stack up to maxFrames deep using IDebugControl4::GetStackTrace
     *  and resolves each frame to source file, line, and function name via
     *  IDebugSymbols3::GetLineByOffset and GetNameByOffset.
     *
     *  Each element in the returned array is a DynamicObject with fields:
     *  - id          (int)    — DAP frame ID (allocated by Whatdbg, not here)
     *  - name        (String) — demangled function name or "??"
     *  - source      (Object) — { name, path } if source info is available
     *  - line        (int)    — one-based source line number
     *  - column      (int)    — always 0 (column not resolved)
     *
     *  @param maxFrames  Maximum number of frames to return. Clamped to stack depth.
     *  @return Array of frame objects. Empty if the stack cannot be walked.
     *
     *  @note Must be called while the target is stopped.
     */
    juce::Array<juce::var> getStackTrace (int maxFrames) noexcept;

    /** Return local variables for a given stack frame as a DAP-formatted array.
     *
     *  Uses IDebugSymbols3::GetScopeSymbolGroup2 to obtain the symbol group for
     *  the specified frame, then iterates top-level symbols to build the variable list.
     *  Delegates value formatting to debug::detail::prettyPrint and
     *  debug::detail::formatSymbolValue.
     *
     *  Each element in the returned array is a DynamicObject with fields:
     *  - name               (String) — symbol name
     *  - value              (String) — formatted value string
     *  - type               (String) — type name from dbgeng
     *  - variablesReference (int)    — non-zero if the variable can be expanded
     *  - symbolIndex        (int)    — index in the symbol group (for child expansion)
     *
     *  @param frameIndex  Zero-based stack frame index (0 = innermost frame).
     *  @return Array of variable objects. Empty if the frame has no locals or an error occurs.
     *
     *  @note Must be called while the target is stopped.
     */
    juce::Array<juce::var> getLocals (int frameIndex) noexcept;

    /** Return child variables of an expanded composite variable.
     *
     *  Calls IDebugSymbolGroup2::ExpandSymbol on the symbol at symbolIndex, then
     *  iterates the newly exposed children. Used to implement DAP variable expansion
     *  (struct fields, array elements, pointer dereferences).
     *
     *  Each element in the returned array has the same schema as getLocals entries.
     *
     *  @param frameIndex    Zero-based stack frame index containing the parent variable.
     *  @param symbolIndex   Index of the parent symbol in the frame's symbol group.
     *  @return Array of child variable objects. Empty if the symbol has no children or errors.
     *
     *  @note Must be called while the target is stopped.
     */
    juce::Array<juce::var> getVariableChildren (int frameIndex, int symbolIndex) noexcept;

    /** Evaluate a C++ expression in the context of a specific stack frame.
     *
     *  Sets the debug scope to frameIndex, then executes "?? <expression>" via
     *  IDebugControl4::Execute with captured output. The raw output is passed through
     *  debug::detail::formatSymbolValue before being returned.
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
     *  Uses IDebugSystemObjects::GetNumberThreads and GetThreadIdsByIndex to list
     *  all threads, resolving each to its OS thread ID.
     *
     *  Each element in the returned array is a DynamicObject with fields:
     *  - id   (int)    — OS thread ID (system TID, not dbgeng internal index)
     *  - name (String) — always empty (thread naming not implemented)
     *
     *  @return Array of thread objects. Empty if enumeration fails.
     *
     *  @note Must be called while the target is stopped.
     */
    juce::Array<juce::var> getThreads () noexcept;

    /** Return the OS thread ID of the thread that triggered the last debug event.
     *
     *  Calls IDebugSystemObjects::GetCurrentThreadSystemId. Valid only immediately
     *  after a WaitForEvent call returns an event (i.e., while stopped).
     *
     *  @return OS system thread ID, or 0 if the query fails.
     */
    std::uint32_t getEventThreadSystemId () noexcept;

    /** Set the current debug context to the thread with the given OS thread ID.
     *
     *  Looks up the dbgeng internal thread index for systemId and calls
     *  IDebugSystemObjects::SetCurrentThreadId. All subsequent symbol queries
     *  (getLocals, getStackTrace, evaluateExpression) operate on this thread's context.
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

    Loader           loader;
    OutputCallbacks  outputCallbacks;
    EventCallbacks   eventCallbacks;

    Microsoft::WRL::ComPtr<IDebugClient5>       client;
    Microsoft::WRL::ComPtr<IDebugControl4>      control;
    Microsoft::WRL::ComPtr<IDebugSymbols3>      symbols;
    Microsoft::WRL::ComPtr<IDebugDataSpaces4>   dataSpaces;
    Microsoft::WRL::ComPtr<IDebugSystemObjects> systemObjects;

    Microsoft::WRL::ComPtr<IDebugSymbolGroup2> cachedSymbolGroup;
    int cachedFrameIndex { -1 };

    /** True if this Session called CoInitializeEx and therefore owns the COM apartment.
     *
     *  When true, shutdown() calls CoUninitialize. When false (COM was already
     *  initialized by the caller), shutdown() skips CoUninitialize.
     */
    bool isComOwned { false };
#endif

#if JUCE_MAC
    // macOS session members land here at Phase 3+
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
};

} // namespace debug
