#pragma once

#include <atomic>
#include <string>

#include "../third_party/nlohmann/json.hpp"

namespace dap {

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Sequence number generator — monotonically increasing, thread-safe
// ---------------------------------------------------------------------------

inline int nextSeq ()
{
    static std::atomic<int> seq { 1 };
    return seq.fetch_add (1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Response builders
// ---------------------------------------------------------------------------

inline json makeResponse (int requestSeq,
                          const std::string& command,
                          bool isSuccess,
                          const json& body = json::object ())
{
    json response;
    response["seq"]         = nextSeq ();
    response["type"]        = "response";
    response["request_seq"] = requestSeq;
    response["command"]     = command;
    response["success"]     = isSuccess;
    if (body.empty () == false)
    {
        response["body"] = body;
    }
    return response;
}

inline json makeErrorResponse (int requestSeq,
                                const std::string& command,
                                const std::string& message)
{
    json response;
    response["seq"]         = nextSeq ();
    response["type"]        = "response";
    response["request_seq"] = requestSeq;
    response["command"]     = command;
    response["success"]     = false;
    response["message"]     = message;
    return response;
}

// ---------------------------------------------------------------------------
// Event builder
// ---------------------------------------------------------------------------

inline json makeEvent (const std::string& event,
                       const json& body = json::object ())
{
    json eventObj;
    eventObj["seq"]   = nextSeq ();
    eventObj["type"]  = "event";
    eventObj["event"] = event;
    if (body.empty () == false)
    {
        eventObj["body"] = body;
    }
    return eventObj;
}

// ---------------------------------------------------------------------------
// Capabilities — codelldb-compatible surface
// ---------------------------------------------------------------------------

inline json makeCapabilities ()
{
    json caps;
    caps["supportsConfigurationDoneRequest"]   = true;
    caps["supportsFunctionBreakpoints"]        = false;
    caps["supportsConditionalBreakpoints"]     = true;
    caps["supportsHitConditionalBreakpoints"]  = false;
    caps["supportsEvaluateForHovers"]          = true;
    caps["supportsSetVariable"]                = false;
    caps["supportsStepBack"]                   = false;
    caps["supportsRestartFrame"]               = false;
    caps["supportsGotoTargetsRequest"]         = false;
    caps["supportsStepInTargetsRequest"]       = false;
    caps["supportsCompletionsRequest"]         = false;
    caps["supportsModulesRequest"]             = false;
    caps["supportsExceptionOptions"]           = false;
    caps["supportsValueFormattingOptions"]     = false;
    caps["supportsExceptionInfoRequest"]       = false;
    caps["supportTerminateDebuggee"]           = true;
    caps["supportsDelayedStackTraceLoading"]   = true;
    caps["supportsLoadedSourcesRequest"]       = false;
    caps["supportsLogPoints"]                  = true;
    caps["supportsTerminateThreadsRequest"]    = false;
    caps["supportsSetExpression"]              = false;
    caps["supportsTerminateRequest"]           = true;
    caps["supportsDataBreakpoints"]            = true;
    caps["supportsReadMemoryRequest"]          = false;
    caps["supportsDisassembleRequest"]         = false;
    caps["supportsCancelRequest"]              = false;
    caps["supportsBreakpointLocationsRequest"] = false;
    caps["supportsClipboardContext"]           = false;
    caps["supportsSteppingGranularity"]        = false;
    caps["supportsInstructionBreakpoints"]     = false;
    caps["supportsExceptionFilterOptions"]     = false;
    return caps;
}

} // namespace dap
