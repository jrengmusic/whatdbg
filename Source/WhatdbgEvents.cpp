/** @file WhatdbgEvents.cpp
 *  @brief DAP event builders — breakpoint(changed), stopped(exception),
 *         output(stderr), and process event bodies.
 */

#include "Whatdbg.h"

using dap::DynObj;

juce::var Whatdbg::getBreakpointChangedEvent (int dapId, std::uint32_t resolvedLine) const
{
    DynObj bpObj { new juce::DynamicObject () };
    bpObj->setProperty ("id",       dapId);
    bpObj->setProperty ("verified", true);
    bpObj->setProperty ("line",     static_cast<int> (resolvedLine));

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("reason",     "changed");
    body->setProperty ("breakpoint", juce::var (bpObj));

    return dap::getEvent ("breakpoint", juce::var (body));
}

juce::var Whatdbg::getExceptionStoppedEvent (const juce::String& exceptionName,
                                             const juce::String& description,
                                             int threadId) const
{
    DynObj stoppedBody { new juce::DynamicObject () };
    stoppedBody->setProperty ("reason",            "exception");
    stoppedBody->setProperty ("text",              exceptionName);
    stoppedBody->setProperty ("description",       description);
    stoppedBody->setProperty ("threadId",          threadId);
    stoppedBody->setProperty ("allThreadsStopped", true);

    return dap::getEvent ("stopped", juce::var (stoppedBody));
}

juce::var Whatdbg::getExceptionOutputEvent (const juce::String& exceptionName,
                                            const juce::String& description) const
{
    DynObj outputBody { new juce::DynamicObject () };
    outputBody->setProperty ("category", "stderr");
    outputBody->setProperty ("output",   "Unhandled exception: " + exceptionName + " " + description + "\n");

    return dap::getEvent ("output", juce::var (outputBody));
}

juce::var Whatdbg::getProcessEvent (const juce::String& name,
                                    std::uint32_t processId,
                                    const juce::String& startMethod) const
{
    DynObj body { new juce::DynamicObject () };
    body->setProperty ("name",            name);
    body->setProperty ("systemProcessId", static_cast<int> (processId));
    body->setProperty ("isLocalProcess",  true);
    body->setProperty ("startMethod",     startMethod);

    return dap::getEvent ("process", juce::var (body));
}
