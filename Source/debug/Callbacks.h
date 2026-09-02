#pragma once
#include <JuceHeader.h>
#include <windows.h>
#include <dbgeng.h>

namespace debug
{

/** Receives debug output from the target process via dbgeng.
 *
 *  Registered with IDebugClient5::SetOutputCallbacks. Implements
 *  IDebugOutputCallbacks2 because dbgeng routes output through Output2 when
 *  the callback object QIs for IDebugOutputCallbacks2.
 *
 *  When the target writes to OutputDebugString, Output2 is called with
 *  DEBUG_OUTCBI_DML or DEBUG_OUTCBI_ANY_FORMAT. The text is forwarded to
 *  State::debuggeeOutputText and State::hasDebuggeeOutput is set so that
 *  the main loop can emit a DAP output event.
 *
 *  @note Registered and unregistered by Session::initialize and Session::shutdown.
 */
class OutputCallbacks : public IDebugOutputCallbacks2
{
public:
    OutputCallbacks () = default;

    /** @name IUnknown */
    ///@{

    /** Increment the reference count.
     *
     *  @return Updated reference count.
     */
    STDMETHOD_ (ULONG, AddRef) () override;

    /** Decrement the reference count.
     *
     *  @return Updated reference count.
     *
     *  @note This object is stack-allocated inside Session; the reference count
     *        is managed manually but the object is never deleted via Release.
     */
    STDMETHOD_ (ULONG, Release) () override;

    /** Return a pointer to the requested interface.
     *
     *  Supports IUnknown, IDebugOutputCallbacks, and IDebugOutputCallbacks2.
     *
     *  @param interfaceId  IID of the requested interface.
     *  @param outInterface Receives the interface pointer on success.
     *  @return S_OK on success, E_NOINTERFACE if the IID is not supported.
     */
    STDMETHOD (QueryInterface) (REFIID interfaceId, PVOID* outInterface) override;

    ///@}

    /** @name IDebugOutputCallbacks */
    ///@{

    /** Receive plain-text debug output (legacy callback path).
     *
     *  Called by dbgeng for output routed through the non-DML path. Writes
     *  the text to debug::Log; does not forward it to State.
     *
     *  @param mask  Output mask bits (e.g. DEBUG_OUTPUT_NORMAL, DEBUG_OUTPUT_ERROR).
     *  @param text  Null-terminated output text.
     *  @return S_OK always.
     */
    STDMETHOD (Output) (ULONG mask, PCSTR text) override;

    ///@}

    /** @name IDebugOutputCallbacks2 */
    ///@{

    /** Report which output categories this callback is interested in.
     *
     *  Sets the mask to DEBUG_OUTCBI_ANY_FORMAT so that all output types are
     *  routed through Output2, bypassing the legacy Output callback.
     *
     *  @param mask  Receives the interest mask bitmask.
     *  @return S_OK always.
     */
    STDMETHOD (GetInterestMask) (PULONG mask) override;

    /** Receive rich debug output including OutputDebugString from the target.
     *
     *  Called by dbgeng when the callback QIs for IDebugOutputCallbacks2 and
     *  has set an interest mask. Captures target output into State.
     *
     *  @param which  Output type (DEBUG_OUTCBI_*).
     *  @param flags  Output flags.
     *  @param arg    Type-specific argument (e.g. address for memory output).
     *  @param text   Null-terminated wide output text.
     *  @return S_OK always.
     */
    STDMETHOD (Output2) (ULONG which, ULONG flags, ULONG64 arg, PCWSTR text) override;

    ///@}

private:
    /** COM reference count, tracked by AddRef/Release. Never reaches zero in
     *  practice — this object is stack-allocated inside Session and never deleted.
     */
    ULONG refCount { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputCallbacks)
};

//==============================================================================

/** Receives debug lifecycle events from dbgeng.
 *
 *  Registered with IDebugClient5::SetEventCallbacks. Handles the full set of
 *  IDebugEventCallbacks methods. Each callback writes deferred-event flags into
 *  debug::State rather than acting immediately, so that the main thread processes
 *  all state changes in processDeferredEvents() without COM re-entrancy concerns.
 *
 *  Key events and their State effects:
 *  - Breakpoint     → sets hasBreakpointHit / breakpointEngineId. The initial
 *                     loader breakpoint is a first-chance EXCEPTION_BREAKPOINT
 *                     handled separately, in Exception, which sets initialBreakPhase::pending.
 *  - LoadModule     → sets hasNewModuleLoaded, lastLoadedImageName
 *  - ExitProcess    → sets hasProcessExited, processExitCode
 *  - ChangeEngineState → sets hasStepCompleted when step finishes
 *
 *  @note Registered and unregistered by Session::initialize and Session::shutdown.
 */
class EventCallbacks : public IDebugEventCallbacks
{
public:
    EventCallbacks () = default;

    /** @name IUnknown */
    ///@{

    /** Increment the reference count.
     *
     *  @return Updated reference count.
     */
    STDMETHOD_ (ULONG, AddRef) () override;

    /** Decrement the reference count.
     *
     *  @return Updated reference count.
     */
    STDMETHOD_ (ULONG, Release) () override;

    /** Return a pointer to the requested interface.
     *
     *  Supports IUnknown and IDebugEventCallbacks.
     *
     *  @param interfaceId  IID of the requested interface.
     *  @param outInterface Receives the interface pointer on success.
     *  @return S_OK on success, E_NOINTERFACE if the IID is not supported.
     */
    STDMETHOD (QueryInterface) (REFIID interfaceId, PVOID* outInterface) override;

    ///@}

    /** @name IDebugEventCallbacks */
    ///@{

    /** Report which event categories this callback is interested in.
     *
     *  Sets the mask to cover exactly the events this class implements and
     *  acts on: breakpoints, exceptions, process creation/exit, module load,
     *  and engine state changes (step-completion detection). Every other
     *  IDebugEventCallbacks method is a required override of the COM interface
     *  that dbgeng will never invoke, because its event bit is not in this mask.
     *
     *  @param mask  Receives the interest mask bitmask (DEBUG_EVENT_*).
     *  @return S_OK always.
     */
    STDMETHOD (GetInterestMask) (PULONG mask) override;

    /** Called when a user-registered breakpoint is hit.
     *
     *  Always sets hasBreakpointHit and breakpointEngineId. The initial loader
     *  breakpoint is reported separately, as a first-chance EXCEPTION_BREAKPOINT
     *  through Exception, before any user breakpoint exists to trigger this method.
     *
     *  @param bp  Pointer to the IDebugBreakpoint that was hit.
     *  @return DEBUG_STATUS_BREAK to keep the target stopped.
     */
    STDMETHOD (Breakpoint) (PDEBUG_BREAKPOINT bp) override;

    /** Called when an exception occurs in the target.
     *
     *  Dispatches on exception code: the initial loader breakpoint sets
     *  initialBreakPhase::pending, a first-chance exception is passed to the
     *  target, and a second-chance exception sets hasExceptionStopped.
     *
     *  @param exception    Exception record describing the exception.
     *  @param firstChance  Non-zero if this is the first-chance notification.
     *  @return DEBUG_STATUS_BREAK to stop the target, or DEBUG_STATUS_GO_NOT_HANDLED
     *          to pass a first-chance exception through to the target.
     */
    STDMETHOD (Exception) (PEXCEPTION_RECORD64 exception, ULONG firstChance) override;

    /** Required IDebugEventCallbacks override; never invoked. DEBUG_EVENT_CREATE_THREAD
     *  is not in the GetInterestMask result, so dbgeng never calls this method.
     *
     *  @param handle       Handle to the new thread.
     *  @param dataOffset   Address of the TEB for the new thread.
     *  @param startOffset  Start address of the new thread.
     *  @return DEBUG_STATUS_NO_CHANGE.
     */
    STDMETHOD (CreateThread) (ULONG64 handle, ULONG64 dataOffset, ULONG64 startOffset) override;

    /** Required IDebugEventCallbacks override; never invoked. DEBUG_EVENT_EXIT_THREAD
     *  is not in the GetInterestMask result, so dbgeng never calls this method.
     *
     *  @param exitCode  Exit code of the exiting thread.
     *  @return DEBUG_STATUS_NO_CHANGE.
     */
    STDMETHOD (ExitThread) (ULONG exitCode) override;

    /** Called when the target process is created (after a launch or attach).
     *
     *  @param imageFileHandle    Handle to the process image file.
     *  @param handle            Handle to the new process.
     *  @param baseOffset        Base load address of the main executable.
     *  @param moduleSize        Size of the main module image.
     *  @param moduleName        Short module name.
     *  @param imageName         Full path to the executable image.
     *  @param checkSum          PE checksum of the image.
     *  @param timeDateStamp     PE timestamp of the image.
     *  @param initialThreadHandle Handle to the initial thread.
     *  @param threadDataOffset  Address of the initial thread's TEB.
     *  @param startOffset       Start address of the initial thread.
     *  @return DEBUG_STATUS_NO_CHANGE.
     */
    STDMETHOD (CreateProcess) (ULONG64 imageFileHandle, ULONG64 handle, ULONG64 baseOffset,
                               ULONG moduleSize, PCSTR moduleName, PCSTR imageName,
                               ULONG checkSum, ULONG timeDateStamp,
                               ULONG64 initialThreadHandle, ULONG64 threadDataOffset,
                               ULONG64 startOffset) override;

    /** Called when the target process exits.
     *
     *  Sets State::hasProcessExited and State::processExitCode so that
     *  processDeferredEvents can emit DAP exited and terminated events.
     *
     *  @param exitCode  Exit code of the process.
     *  @return DEBUG_STATUS_NO_CHANGE.
     */
    STDMETHOD (ExitProcess) (ULONG exitCode) override;

    /** Called when a DLL or EXE is loaded into the target.
     *
     *  Unconditionally records the image name into State and sets
     *  hasNewModuleLoaded to trigger deferred BP resolution. Additionally stops
     *  the target when State::hasPendingBreakpoints is set, so the main loop can
     *  safely resolve pending breakpoints against the newly loaded module.
     *
     *  @param imageFileHandle  Handle to the module image file.
     *  @param baseOffset       Base load address of the module.
     *  @param moduleSize       Size of the module image.
     *  @param moduleName       Short module name.
     *  @param imageName        Full path to the module image file.
     *  @param checkSum         PE checksum of the module.
     *  @param timeDateStamp    PE timestamp of the module.
     *  @return DEBUG_STATUS_BREAK if breakpoints are pending, otherwise DEBUG_STATUS_NO_CHANGE.
     */
    STDMETHOD (LoadModule) (ULONG64 imageFileHandle, ULONG64 baseOffset,
                            ULONG moduleSize, PCSTR moduleName, PCSTR imageName,
                            ULONG checkSum, ULONG timeDateStamp) override;

    /** Required IDebugEventCallbacks override; never invoked. DEBUG_EVENT_UNLOAD_MODULE
     *  is not in the GetInterestMask result, so dbgeng never calls this method.
     *  Breakpoints in unloaded modules become unresolved automatically and will
     *  be re-resolved when the module reloads.
     *
     *  @param imageBaseName  Base name of the unloaded module.
     *  @param baseOffset     Base address the module was loaded at.
     *  @return DEBUG_STATUS_NO_CHANGE.
     */
    STDMETHOD (UnloadModule) (PCSTR imageBaseName, ULONG64 baseOffset) override;

    /** Required IDebugEventCallbacks override; never invoked. DEBUG_EVENT_SYSTEM_ERROR
     *  is not in the GetInterestMask result, so dbgeng never calls this method.
     *
     *  @param error  Win32 error code.
     *  @param level  Severity level (DEBUG_LEVEL_*).
     *  @return DEBUG_STATUS_NO_CHANGE.
     */
    STDMETHOD (SystemError) (ULONG error, ULONG level) override;

    /** Required IDebugEventCallbacks override; never invoked. DEBUG_EVENT_SESSION_STATUS
     *  is not in the GetInterestMask result, so dbgeng never calls this method.
     *
     *  @param status  New session status (DEBUG_SESSION_*).
     *  @return S_OK.
     */
    STDMETHOD (SessionStatus) (ULONG status) override;

    /** Required IDebugEventCallbacks override; never invoked.
     *  DEBUG_EVENT_CHANGE_DEBUGGEE_STATE is not in the GetInterestMask result,
     *  so dbgeng never calls this method.
     *
     *  @param flags     Flags indicating which state changed (DEBUG_CDS_*).
     *  @param argument  Additional argument depending on flags.
     *  @return S_OK.
     */
    STDMETHOD (ChangeDebuggeeState) (ULONG flags, ULONG64 argument) override;

    /** Called when the engine execution state changes.
     *
     *  Used to detect step completion: when flags include DEBUG_CES_EXECUTION_STATUS
     *  and the new status is DEBUG_STATUS_BREAK after a step was issued, sets
     *  State::hasStepCompleted.
     *
     *  @param flags     Flags indicating which engine state changed (DEBUG_CES_*).
     *  @param argument  New execution status when flags include DEBUG_CES_EXECUTION_STATUS.
     *  @return S_OK.
     */
    STDMETHOD (ChangeEngineState) (ULONG flags, ULONG64 argument) override;

    /** Required IDebugEventCallbacks override; never invoked. DEBUG_EVENT_CHANGE_SYMBOL_STATE
     *  is not in the GetInterestMask result, so dbgeng never calls this method.
     *
     *  @param flags     Flags indicating which symbol state changed (DEBUG_CSS_*).
     *  @param argument  Additional argument depending on flags.
     *  @return S_OK.
     */
    STDMETHOD (ChangeSymbolState) (ULONG flags, ULONG64 argument) override;

    ///@}

private:
    /** COM reference count, tracked by AddRef/Release. Never reaches zero in
     *  practice — this object is stack-allocated inside Session and never deleted.
     */
    ULONG refCount { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EventCallbacks)
};

} // namespace debug
