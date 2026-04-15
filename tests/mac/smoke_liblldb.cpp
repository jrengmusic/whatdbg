#ifndef SMOKE_TARGET_PATH
#error "SMOKE_TARGET_PATH must be defined by build system"
#endif

#include <lldb/API/LLDB.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

int main() {
    lldb::SBDebugger::Initialize();

    auto debugger = lldb::SBDebugger::Create(false);
    if (not debugger.IsValid()) {
        std::cout << "smoke_liblldb: debugger invalid\n";
        return EXIT_FAILURE;
    }

    auto target = debugger.CreateTarget(SMOKE_TARGET_PATH);
    if (not target.IsValid()) {
        std::cout << "smoke_liblldb: target invalid\n";
        return EXIT_FAILURE;
    }

    lldb::SBLaunchInfo launchInfo{nullptr};

    lldb::SBError error;
    auto process = target.Launch(launchInfo, error);
    if (error.Fail()) {
        std::cout << "smoke_liblldb: launch error: " << error.GetCString() << "\n";
        return EXIT_FAILURE;
    }

    auto listener = debugger.GetListener();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool exited{false};
    int exitStatus{-1};

    while (not exited and std::chrono::steady_clock::now() < deadline) {
        lldb::SBEvent event;
        bool got = listener.WaitForEvent(1, event);
        if (got) {
            lldb::StateType state = lldb::SBProcess::GetStateFromEvent(event);
            if (state == lldb::eStateExited) {
                exitStatus = process.GetExitStatus();
                exited = true;
            }
        }
    }

    if (not exited) {
        std::cout << "smoke_liblldb: TIMEOUT\n";
        lldb::SBDebugger::Destroy(debugger);
        return 2;
    }

    std::cout << "smoke_liblldb: dylib loaded OK\n";
    std::cout << "smoke_liblldb: process exit status = " << exitStatus << "\n";
    std::cout << "smoke_liblldb: PASS\n";

    lldb::SBDebugger::Destroy(debugger);
    return 0;
}
