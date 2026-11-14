#pragma once
#include <JuceHeader.h>
#include "../generated/Generated.h"
#include "../WHATDBGModel.h"

namespace debug
{

/** Coarse execution state of the debug target, stored in the executionState parameter.
 *
 *  The engine context writes the state. Processor::process reads it to decide whether to
 *  poll the engine.
 */
enum class ExecutionState : int
{
    idle      = 0,  ///< No target attached. Initial state.
    launching = 1,  ///< launch or attach request sent; the first stop has not arrived yet.
    running   = 2,  ///< Target is executing. Resume/step have been issued.
    stopped   = 3,  ///< Target is paused at a breakpoint, step completion, or exception.
    exited    = 4   ///< Target process has exited.
};

/** Cause of the current stop, stored in the stopReason parameter.
 *
 *  StopManager::onStop reads stopReason and runs the drain for that value. The drains
 *  set it back to none when they run. drainInitialBreak does so only after
 *  configurationDone is set.
 *
 *  Writers, by value:
 *  - none:         The default, and the value that the drains store.
 *  - initialBreak: Callbacks.cpp onBreakpoint while launching; Session_mac
 *                  onProcessStateStopped while launching; Processor::setAttachedState on macOS.
 *  - moduleLoad:   Callbacks.cpp LoadModule, when no step is pending.
 *  - breakpoint:   Callbacks.cpp Breakpoint; Session_mac onBreakpointStop.
 *  - step:         Session::pollEvents on Windows, and Session_mac onStepStop. StopManager
 *                  stores it only when stopReason is none.
 *  - pause:        Callbacks.cpp onBreakpoint while isPausePending; Session_mac onInterruptStop.
 *  - exception:    Callbacks.cpp onBreakpoint and onUnknownException (second chance);
 *                  Session_mac onSignalStop and onExceptionStop.
 */
enum class StopReason
{
    none,
    initialBreak,
    moduleLoad,
    breakpoint,
    step,
    pause,
    exception
};

/** Outcome of a Session::getOffsetStatus call.
 *
 *  - found      — symbol found; Session::getOffset returns the resolved address.
 *  - notFound   — symbol not resolvable (no code at this line, or module not loaded).
 *  - engineBusy — Windows only: dbgeng transient error (E_UNEXPECTED); caller should
 *                 retry later. liblldb resolves synchronously and never returns this.
 */
enum class OffsetStatus
{
    found,      ///< Symbol found; Session::getOffset returns the resolved address.
    notFound,   ///< Symbol not resolvable (no code at this line, or module not loaded).
    engineBusy  ///< Windows only: dbgeng transient error (E_UNEXPECTED); caller should
                ///< retry later. liblldb resolves synchronously and never returns this.
};

/** Dispatch map of the engine events, keyed by juce::Identifier.
 *
 *  Session fires an event with Session::events.get. Processor and StopManager register
 *  handlers with add. The payload of an event is one of the argument lists in the type.
 */
using Events = jam::Function::Map<juce::Identifier, jam::Function::pointerCapacity, void (), void (int), void (int, int), void (juce::int64, juce::int64, int), void (const juce::String&)>;

/** Translate a platform-native exception or signal code to a name.
 *
 *  Windows: code is an NTSTATUS or SEH exception code, for example 0xC0000005 gives
 *           ACCESS_VIOLATION. isMachException is ignored.
 *  macOS:   code is a POSIX signal number when isMachException is false, or a Mach
 *           exception type when it is true. Signal 11 gives EXC_BAD_ACCESS when
 *           isMachException is false. Mach exception type 1 gives EXC_BAD_ACCESS when
 *           it is true.
 *
 *  Implementations live in platform-specific files: Callbacks.cpp on Windows,
 *  Session_mac.cpp on macOS. Called by StopManager::drainExceptionStopped and
 *  Processor::onExceptionInfo to build the DAP exceptionId field.
 *
 *  @param code             Platform-native exception or signal code, from Id::exceptionCode.
 *  @param isMachException  Value of Id::isMachException. Selects the Mach table on macOS.
 *  @return The name, or a fallback of the form 0x\<hex\> when the code is not in the table.
 */
juce::String getExceptionName (std::uint32_t code, bool isMachException) noexcept;

} // namespace debug
