#pragma once
#include <JuceHeader.h>
#include <windows.h>
#include <dbgeng.h>
#include "State.h"

namespace debug
{

/** Receives debug output from dbgeng: the target's console text and the engine's own text.
 *
 *  Registered with IDebugClient5::SetOutputCallbacks. Implements IDebugOutputCallbacks2,
 *  and its GetInterestMask requests DEBUG_OUTCBI_ANY_FORMAT, so dbgeng calls Output2.
 *  Output2 fires Id::console on Session::events for text that carries the
 *  DEBUG_OUTPUT_DEBUGGEE mask bit.
 *
 *  @note Owned by Session as a member. setDebugInterfaces registers it, and shutdown
 *        unregisters it.
 */
class OutputCallbacks : public IDebugOutputCallbacks,
                        public IDebugOutputCallbacks2
{
public:
    explicit OutputCallbacks (Events& newEvents);

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
     *  @note This object is a member of Session. The reference count is managed
     *        manually, and the object is never deleted through Release.
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

    /** Receives plain-text debug output.
     *
     *  Writes the text to debug::Log in debug builds. Fires no event.
     *
     *  @param mask  Output mask bits (e.g. DEBUG_OUTPUT_NORMAL, DEBUG_OUTPUT_ERROR).
     *  @param text  Null-terminated output text.
     *  @return S_OK always.
     */
    STDMETHOD (Output) (ULONG mask, PCSTR text) override;

    ///@}

    /** @name IDebugOutputCallbacks2 */
    ///@{

    /** Reports which output formats this callback is interested in.
     *
     *  Sets the mask to DEBUG_OUTCBI_ANY_FORMAT.
     *
     *  @param mask  Receives the interest mask bitmask.
     *  @return S_OK always.
     */
    STDMETHOD (GetInterestMask) (PULONG mask) override;

    /** Receives output in every format. Fires Id::console for debuggee text.
     *
     *  When which is DEBUG_OUTCB_TEXT and the mask in arg has DEBUG_OUTPUT_DEBUGGEE,
     *  the text is fired as Id::console on Session::events. Other output is ignored.
     *
     *  @param which  Output type (DEBUG_OUTCB_*). Only DEBUG_OUTCB_TEXT is handled.
     *  @param flags  Output flags. Not used.
     *  @param arg    Type-specific argument. For DEBUG_OUTCB_TEXT, the output mask.
     *  @param text   Null-terminated wide output text.
     *  @return S_OK always.
     */
    STDMETHOD (Output2) (ULONG which, ULONG flags, ULONG64 arg, PCWSTR text) override;

    ///@}

private:
    /** COM reference count, tracked by AddRef/Release. This object is a member of Session and is never deleted. */
    ULONG refCount { 1 };

    Events& events;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputCallbacks)
};

//==============================================================================

/** Receives process, exception, breakpoint, and module-load events from dbgeng.
 *
 *  Registered with IDebugClient5::SetEventCallbacks. Each handled callback fires an
 *  event on Session::events and returns a status that tells dbgeng whether to keep
 *  the target stopped. Callbacks run on the engine context, inside WaitForEvent.
 *
 *  Events that each callback fires:
 *  - Breakpoint     → breakpoint, with the engine breakpoint id. Returns DEBUG_STATUS_BREAK.
 *  - Exception      → EXCEPTION_BREAKPOINT fires initialBreak while launching, pause while
 *                     isPausePending, and exception otherwise. Other codes fire exception
 *                     on a second-chance notification only.
 *  - LoadModule     → moduleLoad, with the image name, unless a step is pending.
 *  - ExitProcess    → exited, with the exit code.
 *  - CreateProcess  → targetProcessId, with the process id.
 *
 *  GetInterestMask selects the breakpoint, exception, create-process, exit-process, and
 *  load-module events. dbgeng never invokes the other methods.
 *
 *  @note Owned by Session as a member. setDebugInterfaces registers it, and shutdown
 *        unregisters it.
 */
class EventCallbacks : public IDebugEventCallbacks
{
public:
    explicit EventCallbacks (Events& newEvents);

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
     *  Sets the mask to breakpoints, exceptions, process creation and exit, and
     *  module load. Every other IDebugEventCallbacks method is a required override
     *  of the COM interface that dbgeng never invokes, because its event bit is not
     *  in this mask.
     *
     *  @param mask  Receives the interest mask bitmask (DEBUG_EVENT_*).
     *  @return S_OK always.
     */
    STDMETHOD (GetInterestMask) (PULONG mask) override;

    /** Called when a user-registered breakpoint is hit. Fires breakpoint with the engine id.
     *
     *  EXCEPTION_BREAKPOINT events, including the initial loader break, go to Exception instead.
     *
     *  @param bp  Pointer to the IDebugBreakpoint that was hit.
     *  @return DEBUG_STATUS_BREAK to keep the target stopped.
     */
    STDMETHOD (Breakpoint) (PDEBUG_BREAKPOINT bp) override;

    /** Called when an exception occurs in the target.
     *
     *  Dispatches on exception code. EXCEPTION_BREAKPOINT goes to onBreakpoint: while
     *  launching it fires initialBreak, while isPausePending it fires pause, and otherwise
     *  (an int3 after launch) it fires exception with the code and the address. The MSVC
     *  thread-name exception is passed to the target. Other codes go to onUnknownException,
     *  which passes a first-chance exception to the target and fires exception on a
     *  second-chance exception.
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

    /** Called when the target process is created (after a launch or attach). Fires targetProcessId with the process id.
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

    /** Called when the target process exits. Fires exited with the exit code.
     *
     *  @param exitCode  Exit code of the process.
     *  @return DEBUG_STATUS_NO_CHANGE.
     */
    STDMETHOD (ExitProcess) (ULONG exitCode) override;

    /** Called when a DLL or EXE is loaded into the target.
     *
     *  While a step is pending the method fires nothing and returns DEBUG_STATUS_NO_CHANGE.
     *  Otherwise it fires moduleLoad with the image name and returns DEBUG_STATUS_BREAK.
     *  StopManager then resolves pending breakpoints against the new module and resumes the
     *  target.
     *
     *  @param imageFileHandle  Handle to the module image file.
     *  @param baseOffset       Base load address of the module.
     *  @param moduleSize       Size of the module image.
     *  @param moduleName       Short module name.
     *  @param imageName        Full path to the module image file.
     *  @param checkSum         PE checksum of the module.
     *  @param timeDateStamp    PE timestamp of the module.
     *  @return DEBUG_STATUS_BREAK when the module load stops the target, otherwise DEBUG_STATUS_NO_CHANGE.
     */
    STDMETHOD (LoadModule) (ULONG64 imageFileHandle, ULONG64 baseOffset,
                            ULONG moduleSize, PCSTR moduleName, PCSTR imageName,
                            ULONG checkSum, ULONG timeDateStamp) override;

    /** Required IDebugEventCallbacks override; never invoked. DEBUG_EVENT_UNLOAD_MODULE
     *  is not in the GetInterestMask result, so dbgeng never calls this method.
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

    /** Required IDebugEventCallbacks override; never invoked. DEBUG_EVENT_CHANGE_ENGINE_STATE
     *  is not in the GetInterestMask result, so dbgeng never calls this method.
     *
     *  @param flags     Flags indicating which engine state changed (DEBUG_CES_*).
     *  @param argument  Additional argument depending on flags.
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
    /** COM reference count, tracked by AddRef/Release. This object is a member of Session and is never deleted. */
    ULONG refCount { 1 };

    Events& events;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EventCallbacks)
};

} // namespace debug
