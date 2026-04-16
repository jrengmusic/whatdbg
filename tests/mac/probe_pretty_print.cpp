#ifndef PROBE_TARGET_PATH
#error "PROBE_TARGET_PATH must be defined by build system"
#endif

#include <lldb/API/LLDB.h>
#include <chrono>
#include <iostream>

static void printValue (lldb::SBValue val) noexcept
{
    const char* name    { val.GetName() };
    const char* type    { val.GetTypeName() };
    const char* value   { val.GetValue() };
    const char* summary { val.GetSummary() };

    std::cout
        << "name="    << (name    ? name    : "NULL")
        << " | type=" << (type    ? type    : "NULL")
        << " | value=" << (value  ? value   : "NULL")
        << " | summary=" << (summary ? summary : "NULL")
        << "\n";
}

static bool waitForStop (lldb::SBProcess& process,
                         lldb::SBListener& listener) noexcept
{
    const auto deadline { std::chrono::steady_clock::now()
                          + std::chrono::seconds (10) };
    bool stopped { false };

    while (not stopped and std::chrono::steady_clock::now() < deadline)
    {
        lldb::SBEvent event;
        const bool got { listener.WaitForEvent (1, event) };

        if (got and lldb::SBProcess::EventIsProcessEvent (event))
        {
            const lldb::StateType st { lldb::SBProcess::GetStateFromEvent (event) };

            if (st == lldb::eStateStopped
                and not lldb::SBProcess::GetRestartedFromEvent (event))
            {
                stopped = true;
            }
            else if (st == lldb::eStateExited or st == lldb::eStateCrashed)
            {
                stopped = true;
            }
        }
    }

    return stopped;
}

int main (int argc, char* argv[])
{
    const char* fixturePath { (argc > 1) ? argv[1] : PROBE_TARGET_PATH };

    lldb::SBDebugger::Initialize ();

    lldb::SBDebugger debugger { lldb::SBDebugger::Create (false) };

    if (not debugger.IsValid ())
    {
        std::cout << "probe_pretty_print: debugger invalid\n";
        lldb::SBDebugger::Terminate ();
        return 1;
    }

    debugger.SetAsync (false);

    lldb::SBTarget target { debugger.CreateTarget (fixturePath) };

    if (not target.IsValid ())
    {
        std::cout << "probe_pretty_print: target invalid — path: " << fixturePath << "\n";
        lldb::SBDebugger::Destroy (debugger);
        lldb::SBDebugger::Terminate ();
        return 1;
    }

    lldb::SBError   launchError;
    lldb::SBProcess process { target.LaunchSimple (nullptr, nullptr, nullptr) };

    if (not process.IsValid ())
    {
        std::cout << "probe_pretty_print: launch failed\n";
        lldb::SBDebugger::Destroy (debugger);
        lldb::SBDebugger::Terminate ();
        return 1;
    }

    // SetAsync(false) means LaunchSimple blocks until first stop.
    // Confirm the process is actually stopped before inspecting.
    const lldb::StateType initialState { process.GetState () };
    const bool alreadyStopped { initialState == lldb::eStateStopped };

    lldb::SBListener listener { debugger.GetListener () };
    bool reachedStop { alreadyStopped };

    if (not alreadyStopped)
    {
        reachedStop = waitForStop (process, listener);
    }

    if (not reachedStop)
    {
        std::cout << "probe_pretty_print: TIMEOUT waiting for __builtin_debugtrap stop\n";
        process.Kill ();
        lldb::SBDebugger::Destroy (debugger);
        lldb::SBDebugger::Terminate ();
        return 1;
    }

    lldb::SBThread thread { process.GetSelectedThread () };
    lldb::SBFrame  frame  { thread.GetSelectedFrame () };

    // args=true, locals=true, statics=true, inScopeOnly=true
    lldb::SBValueList vars { frame.GetVariables (true, true, true, true) };

    const uint32_t count { vars.GetSize () };

    for (uint32_t i { 0 }; i < count; ++i)
    {
        printValue (vars.GetValueAtIndex (i));
    }

    lldb::SBDebugger::Destroy (debugger);
    lldb::SBDebugger::Terminate ();
    return 0;
}
