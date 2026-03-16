#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "../third_party/nlohmann/json.hpp"

class BreakpointManager;
class DbgEngSession;
class DapProtocol;

class DapDispatcher
{
public:
    using Handler = std::function<nlohmann::json (const nlohmann::json&)>;

    DapDispatcher (DbgEngSession& session, DapProtocol& protocol);

    // Destructor must be defined in DapDispatcher.cpp where BreakpointManager
    // is a complete type — required by std::unique_ptr with a forward-declared type.
    ~DapDispatcher ();

    // Route request to registered handler.
    // Returns a fully-formed DAP response JSON object.
    nlohmann::json dispatch (const nlohmann::json& request);

    // True until a disconnect request is handled.
    bool isRunning () const
    {
        return isRunning_ == true;
    }

    // Accessor for the BreakpointManager — used by main.cpp to wire
    // the manager into EventCallbacks after session initialization.
    BreakpointManager* breakpointManager () const;

private:
    void registerHandler (const std::string& command, Handler handler);

    DbgEngSession& session;
    DapProtocol&   protocol;

    std::unique_ptr<BreakpointManager> bpManager;

    std::unordered_map<std::string, Handler> handlers;
    bool isRunning_        { true };
    bool shouldStopOnEntry { false };
};
