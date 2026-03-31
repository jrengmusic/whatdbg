#include "DapDispatcher.hpp"
#include "BreakpointManager.hpp"
#include "DapProtocol.hpp"
#include "DapTypes.hpp"
#include "DbgEngSession.hpp"

#include <iostream>
#include <string>
#include <vector>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// safeGetStackTrace — SEH wrapper so an AV on a corrupted stack does not
// terminate the process.  Must be a free function because __try/__except
// cannot coexist with C++ objects that have destructors in the same scope.
// ---------------------------------------------------------------------------

static HRESULT safeGetStackTrace (IDebugControl4* control,
                                   DEBUG_STACK_FRAME* frames,
                                   ULONG maxFrames,
                                   ULONG* frameCount)
{
    __try
    {
        return control->GetStackTrace (0, 0, 0, frames, maxFrames, frameCount);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        fprintf (stderr, "WHATDBG: GetStackTrace SEH exception — returning empty frames\n");
        *frameCount = 0;
        return E_FAIL;
    }
}

// ---------------------------------------------------------------------------
// Frame symbol resolution with SEH protection.
// Split into two functions: rawResolve (POD only, SEH safe) and
// safeResolveFrame (builds std::string result outside __try).
// ---------------------------------------------------------------------------

struct RawFrameData
{
    char  funcName[512];
    char  fileName[512];
    ULONG line;
    bool  hasName;
    bool  hasSource;
};

static RawFrameData rawResolve (IDebugSymbols3* symbols, ULONG64 offset)
{
    RawFrameData raw;
    raw.funcName[0] = '\0';
    raw.fileName[0] = '\0';
    raw.line        = 0;
    raw.hasName     = false;
    raw.hasSource   = false;

    __try
    {
        raw.hasName = SUCCEEDED (symbols->GetNameByOffset (
            offset, raw.funcName, sizeof (raw.funcName), nullptr, nullptr));

        raw.hasSource = SUCCEEDED (symbols->GetLineByOffset (
            offset, &raw.line, raw.fileName, sizeof (raw.fileName), nullptr, nullptr));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        fprintf (stderr,
            "WHATDBG: rawResolve SEH at offset 0x%llX\n",
            static_cast<unsigned long long> (offset));
    }

    return raw;
}

struct FrameSymbol
{
    std::string name;
    std::string filePath;
    ULONG       line      { 0 };
    bool        hasSource { false };
};

static FrameSymbol safeResolveFrame (IDebugSymbols3* symbols, ULONG64 offset)
{
    RawFrameData raw { rawResolve (symbols, offset) };

    FrameSymbol result;
    result.name      = raw.hasName ? std::string (raw.funcName) : "0x" + std::to_string (offset);
    result.filePath  = raw.hasSource ? std::string (raw.fileName) : "";
    result.line      = raw.line;
    result.hasSource = raw.hasSource;

    return result;
}

// ---------------------------------------------------------------------------
// Constructor — register all handlers
// ---------------------------------------------------------------------------

DapDispatcher::DapDispatcher (DbgEngSession& dbgSession, DapProtocol& dapProtocol)
    : session (dbgSession)
    , protocol (dapProtocol)
    , bpManager (std::make_unique<BreakpointManager> (dbgSession.control (),
                                                       dbgSession.symbols ()))
{
    // initialize — capabilities handshake
    registerHandler ("initialize", [this] (const json& req) -> json
    {
        int seq { req.value ("seq", 0) };
        return dap::makeResponse (seq, "initialize", true, dap::makeCapabilities ());
    });

    // launch — spawn a process under the debugger
    registerHandler ("launch", [this] (const json& req) -> json
    {
        int seq { req.value ("seq", 0) };
        json launchArgs { req.value ("arguments", json::object ()) };

        std::string program { launchArgs.value ("program", "") };
        std::string cwd     { launchArgs.value ("cwd", "") };
        bool isStopOnEntry  { launchArgs.value ("stopOnEntry", false) };

        json response;

        if (program.empty () == true)
        {
            response = dap::makeErrorResponse (seq, "launch",
                "WHATDBG: 'program' field is required");
        }
        else
        {
            std::vector<std::string> programArgs;

            if (launchArgs.contains ("args") == true)
            {
                for (const auto& arg : launchArgs.at ("args"))
                {
                    programArgs.push_back (arg.get<std::string> ());
                }
            }

            // Set source and symbol paths BEFORE launch.
            //
            // Source path: enables dbgeng to resolve relative paths in PDBs
            // (e.g. "..\..\Source\File.cpp") via overlap matching.
            //
            // Symbol path: tells dbgeng where to find PDB files.  The PDB
            // for a JUCE plugin lives in the build artefacts directory, not
            // next to the installed DLL.  We add both the build root and
            // the artefacts subdirectories so dbgeng can find it.
            if (cwd.empty () == false)
            {
                std::string cwdWin { cwd };
                std::replace (cwdWin.begin (), cwdWin.end (), '/', '\\');

                session.appendSourcePath (cwdWin);
                session.appendSymbolPath (cwdWin);
                session.appendSymbolPath (cwdWin + "\\Builds\\Ninja");

                // Scan Builds\Ninja for artefact directories containing PDB files.
                // JUCE/CMake layout: Builds/Ninja/<target>_artefacts/Debug/<format>/
                // dbgeng doesn't search symbol paths recursively, so we must add
                // each PDB directory explicitly.
                {
                    std::string searchPattern { cwdWin + "\\Builds\\Ninja\\*_artefacts" };
                    WIN32_FIND_DATAA findData {};
                    HANDLE hFind { FindFirstFileA (searchPattern.c_str (), &findData) };

                    if (hFind != INVALID_HANDLE_VALUE)
                    {
                        do
                        {
                            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                            {
                                std::string artefactBase { cwdWin + "\\Builds\\Ninja\\"
                                    + findData.cFileName };

                                // Add Debug subdirectories for each format (VST3, VST, AAX, Standalone)
                                static const char* formats[] { "VST3", "VST", "AAX", "Standalone" };

                                for (const char* fmt : formats)
                                {
                                    std::string fmtDir { artefactBase + "\\Debug\\" + fmt };
                                    DWORD attrs { GetFileAttributesA (fmtDir.c_str ()) };

                                    if (attrs != INVALID_FILE_ATTRIBUTES
                                        and (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
                                    {
                                        session.appendSymbolPath (fmtDir);
                                    }
                                }
                            }
                        }
                        while (FindNextFileA (hFind, &findData) != 0);

                        FindClose (hFind);
                    }
                }
            }

            // Add the program's parent directory to both paths.
            std::string programDir { program };
            auto lastSep { programDir.rfind ('\\') };

            if (lastSep == std::string::npos)
            {
                lastSep = programDir.rfind ('/');
            }

            if (lastSep != std::string::npos)
            {
                programDir = programDir.substr (0, lastSep);
                session.appendSourcePath (programDir);
                session.appendSymbolPath (programDir);
            }

            bool isLaunched { session.launch (program, cwd, programArgs) };

            if (isLaunched == true)
            {
                protocol.writeMessage (std::cout, dap::makeEvent ("process",
                {
                    { "name",           program },
                    { "isLocalProcess", true    },
                    { "startMethod",    "launch" }
                }));

                shouldStopOnEntry = isStopOnEntry;

                response = dap::makeResponse (seq, "launch", true);
            }
            else
            {
                response = dap::makeErrorResponse (seq, "launch",
                    "WHATDBG: failed to launch process: " + program);
            }
        }

        return response;
    });

    // attach — attach to a running process by PID (Phase 3b)
    registerHandler ("attach", [this] (const json& args) -> json
    {
        int seq { args.value ("seq", 0) };
        json attachArgs { args.value ("arguments", json::object ()) };

        int pid { attachArgs.value ("pid", 0) };
        std::string program { attachArgs.value ("program", "") };

        json response;

        if (pid == 0)
        {
            response = dap::makeErrorResponse (seq, "attach",
                "WHATDBG: 'pid' field is required");
        }
        else if (program.empty () == true)
        {
            response = dap::makeErrorResponse (seq, "attach",
                "WHATDBG: 'program' field is required for symbol loading");
        }
        else
        {
            // Set symbol path BEFORE attach so dbgeng can find PDB files.
            std::string cwd { attachArgs.value ("cwd", "") };

            fprintf (stderr, "WHATDBG: program = %s\n", program.c_str ());
            fprintf (stderr, "WHATDBG: cwd = %s\n", cwd.c_str ());

            // Add the program's parent directory
            std::string programDir { program };
            auto lastSep { programDir.rfind ('\\') };
            if (lastSep == std::string::npos)
            {
                lastSep = programDir.rfind ('/');
            }
            if (lastSep != std::string::npos)
            {
                programDir = programDir.substr (0, lastSep);
            }
            session.appendSymbolPath (programDir);

            // Add cwd/Builds/Ninja — where the PDB lives after build
            if (cwd.empty () == false)
            {
                // Convert ${workspaceFolder} forward slashes to backslashes
                std::string cwdWin { cwd };
                std::replace (cwdWin.begin (), cwdWin.end (), '/', '\\');
                session.appendSymbolPath (cwdWin + "\\Builds\\Ninja");
                session.appendSymbolPath (cwdWin);

                // Source path — enables dbgeng to resolve relative paths in
                // PDBs (e.g. "..\..\Source\File.cpp") via overlap matching.
                session.appendSourcePath (cwdWin);
            }

            // Also add the program's parent directory to the source path.
            session.appendSourcePath (programDir);

            bool isAttached { session.attach (static_cast<ULONG> (pid)) };

            if (isAttached == true)
            {
                protocol.writeMessage (std::cout, dap::makeEvent ("process",
                {
                    { "name",           program  },
                    { "isLocalProcess", true     },
                    { "startMethod",    "attach" }
                }));

                response = dap::makeResponse (seq, "attach", true);
            }
            else
            {
                response = dap::makeErrorResponse (seq, "attach",
                    "WHATDBG: failed to attach to PID " + std::to_string (pid));
            }
        }

        return response;
    });

    // disconnect — signal shutdown, return success
    registerHandler ("disconnect", [this] (const json& req) -> json
    {
        int seq { req.value ("seq", 0) };
        isRunning_ = false;
        return dap::makeResponse (seq, "disconnect", true);
    });

    // terminate — same as disconnect (nvim-dap sends this on session end)
    registerHandler ("terminate", [this] (const json& req) -> json
    {
        int seq { req.value ("seq", 0) };
        isRunning_ = false;
        return dap::makeResponse (seq, "terminate", true);
    });

    // configurationDone — clear the config hold, then resume unless stopOnEntry
    registerHandler ("configurationDone", [this] (const json& req) -> json
    {
        int seq { req.value ("seq", 0) };

        session.setWaitingForConfiguration (false);

        if (shouldStopOnEntry == false and session.hasTarget () == true)
        {
            session.resume ();
        }

        return dap::makeResponse (seq, "configurationDone", true);
    });

    // setBreakpoints — Phase 3c
    registerHandler ("setBreakpoints", [this] (const json& req) -> json
    {
        int seq { req.value ("seq", 0) };

        nlohmann::json bpArray { bpManager->handleSetBreakpoints (req) };

        json body;
        body["breakpoints"] = bpArray;

        return dap::makeResponse (seq, "setBreakpoints", true, body);
    });

    // setExceptionBreakpoints — stub success with empty filters
    registerHandler ("setExceptionBreakpoints", [] (const json& req) -> json
    {
        int seq { req.value ("seq", 0) };
        json body;
        body["filters"] = json::array ();
        return dap::makeResponse (seq, "setExceptionBreakpoints", true, body);
    });

    // threads — enumerate all threads in the target process
    registerHandler ("threads", [this] (const json& req) -> json
    {
        int seq { req.value ("seq", 0) };

        json response;

        if (session.hasTarget () == false)
        {
            response = dap::makeErrorResponse (seq, "threads",
                "WHATDBG: no active debug session");
        }
        else
        {
            json threadsArray { json::array () };

            ULONG threadCount { 0 };
            HRESULT hr { E_FAIL };

            if (session.systemObjects () != nullptr)
            {
                hr = session.systemObjects ()->GetNumberThreads (&threadCount);
            }

            if (SUCCEEDED (hr))
            {
                for (ULONG i { 0 }; i < threadCount; ++i)
                {
                    ULONG engineId { 0 };
                    ULONG systemId { 0 };
                    HRESULT hrThread { session.systemObjects ()->GetThreadIdsByIndex (
                        i, 1, &engineId, &systemId) };

                    if (SUCCEEDED (hrThread))
                    {
                        json thread;
                        thread["id"]   = static_cast<int> (engineId);
                        thread["name"] = "Thread " + std::to_string (systemId);
                        threadsArray.push_back (thread);
                    }
                }
            }
            else
            {
                fprintf (stderr, "WHATDBG: GetNumberThreads failed hr=0x%08lX\n",
                    static_cast<unsigned long> (hr));
            }

            json body;
            body["threads"] = threadsArray;
            response = dap::makeResponse (seq, "threads", true, body);
        }

        return response;
    });

    // stackTrace — walk the call stack via IDebugControl4::GetStackTrace
    registerHandler ("stackTrace", [this] (const json& req) -> json
    {
        int seq { req.value ("seq", 0) };

        json response;

        if (session.hasTarget () == false)
        {
            response = dap::makeErrorResponse (seq, "stackTrace",
                "WHATDBG: no active debug session");
        }
        else
        {
            json requestArgs { req.value ("arguments", json::object ()) };
            int startFrame { requestArgs.value ("startFrame", 0) };
            int levels     { requestArgs.value ("levels", 20) };

            // Fix 1: restore the thread that hit the breakpoint before walking
            // its stack.  The "threads" handler may have changed the current
            // thread context inside dbgeng.
            int requestedThreadId { requestArgs.value ("threadId", 0) };
            if (session.systemObjects () != nullptr)
            {
                session.systemObjects ()->SetCurrentThreadId (static_cast<ULONG> (requestedThreadId));
            }

            static constexpr ULONG kMaxFrames { 100 };
            DEBUG_STACK_FRAME frames[kMaxFrames];
            ULONG frameCount { 0 };

            // Fix 2: use SEH wrapper — dbgeng can AV on corrupted/unmapped stacks
            // and C++ try/catch does not catch 0xC0000005.
            HRESULT hr { safeGetStackTrace (session.control (), frames, kMaxFrames, &frameCount) };

            json stackFrames { json::array () };

            if (SUCCEEDED (hr))
            {
                ULONG end { static_cast<ULONG> (startFrame + levels) };
                if (end > frameCount)
                {
                    end = frameCount;
                }

                for (ULONG i { static_cast<ULONG> (startFrame) }; i < end; ++i)
                {
                    json frame;
                    frame["id"] = static_cast<int> (i);

                    FrameSymbol sym { safeResolveFrame (session.symbols (), frames[i].InstructionOffset) };
                    frame["name"] = sym.name;

                    if (sym.hasSource == true)
                    {
                        json src;
                        src["path"]     = sym.filePath;
                        frame["source"] = src;
                        frame["line"]   = static_cast<int> (sym.line);
                        frame["column"] = 1;
                    }

                    stackFrames.push_back (frame);
                }
            }
            else
            {
                fprintf (stderr, "WHATDBG: GetStackTrace failed hr=0x%08lX\n",
                    static_cast<unsigned long> (hr));
            }

            json body;
            body["stackFrames"] = stackFrames;
            body["totalFrames"] = static_cast<int> (frameCount);
            response = dap::makeResponse (seq, "stackTrace", true, body);
        }

        return response;
    });

    // scopes — return empty scope list so nvim-dap does not abort
    registerHandler ("scopes", [this] (const json& req) -> json
    {
        int seq { req.value ("seq", 0) };
        json body;
        body["scopes"] = json::array ();
        return dap::makeResponse (seq, "scopes", true, body);
    });

    // variables — no active session
    registerHandler ("variables", [] (const json& req) -> json
    {
        int seq { req.value ("seq", 0) };
        return dap::makeErrorResponse (seq, "variables",
            "WHATDBG: no active debug session");
    });

    // continue — resume execution and emit a continued event
    registerHandler ("continue", [this] (const json& req) -> json
    {
        int seq { req.value ("seq", 0) };

        json response;

        if (session.hasTarget () == true)
        {
            session.resume ();

            protocol.writeMessage (std::cout, dap::makeEvent ("continued",
            {
                { "threadId",            0    },
                { "allThreadsContinued", true }
            }));

            response = dap::makeResponse (seq, "continue", true,
            {
                { "allThreadsContinued", true }
            });
        }
        else
        {
            response = dap::makeErrorResponse (seq, "continue",
                "WHATDBG: no active debug session");
        }

        return response;
    });

    // next — step over one source line
    registerHandler ("next", [this] (const json& req) -> json
    {
        int seq { req.value ("seq", 0) };

        json response;

        if (session.hasTarget () == true)
        {
            session.control ()->SetExecutionStatus (DEBUG_STATUS_STEP_OVER);
            response = dap::makeResponse (seq, "next", true);
        }
        else
        {
            response = dap::makeErrorResponse (seq, "next",
                "WHATDBG: no active debug session");
        }

        return response;
    });

    // stepIn — step into the next call
    registerHandler ("stepIn", [this] (const json& req) -> json
    {
        int seq { req.value ("seq", 0) };

        json response;

        if (session.hasTarget () == true)
        {
            session.control ()->SetExecutionStatus (DEBUG_STATUS_STEP_INTO);
            response = dap::makeResponse (seq, "stepIn", true);
        }
        else
        {
            response = dap::makeErrorResponse (seq, "stepIn",
                "WHATDBG: no active debug session");
        }

        return response;
    });

    // stepOut — dbgeng has no DEBUG_STATUS_STEP_OUT; use STEP_BRANCH (next branch/return)
    registerHandler ("stepOut", [this] (const json& req) -> json
    {
        int seq { req.value ("seq", 0) };

        json response;

        if (session.hasTarget () == true)
        {
            session.control ()->SetExecutionStatus (DEBUG_STATUS_STEP_BRANCH);
            response = dap::makeResponse (seq, "stepOut", true);
        }
        else
        {
            response = dap::makeErrorResponse (seq, "stepOut",
                "WHATDBG: no active debug session");
        }

        return response;
    });

    // pause — interrupt the running target
    registerHandler ("pause", [this] (const json& req) -> json
    {
        int seq { req.value ("seq", 0) };

        json response;

        if (session.hasTarget () == true)
        {
            session.control ()->SetInterrupt (DEBUG_INTERRUPT_ACTIVE);
            response = dap::makeResponse (seq, "pause", true);
        }
        else
        {
            response = dap::makeErrorResponse (seq, "pause",
                "WHATDBG: no active debug session");
        }

        return response;
    });

    // evaluate — no active session
    registerHandler ("evaluate", [] (const json& req) -> json
    {
        int seq { req.value ("seq", 0) };
        return dap::makeErrorResponse (seq, "evaluate",
            "WHATDBG: no active debug session");
    });
}

// ---------------------------------------------------------------------------
// Destructor — defined here so BreakpointManager is a complete type when
// std::unique_ptr<BreakpointManager>::~unique_ptr runs.
// ---------------------------------------------------------------------------

DapDispatcher::~DapDispatcher () = default;

// ---------------------------------------------------------------------------
// breakpointManager
// ---------------------------------------------------------------------------

BreakpointManager* DapDispatcher::breakpointManager () const
{
    return bpManager.get ();
}

// ---------------------------------------------------------------------------
// registerHandler
// ---------------------------------------------------------------------------

void DapDispatcher::registerHandler (const std::string& command, Handler handler)
{
    handlers[command] = std::move (handler);
}

// ---------------------------------------------------------------------------
// dispatch
// ---------------------------------------------------------------------------

json DapDispatcher::dispatch (const json& request)
{
    std::string command { request.value ("command", std::string {}) };
    int seq             { request.value ("seq", 0) };

    auto it { handlers.find (command) };
    if (it != handlers.end ())
    {
        return it->second (request);
    }

    return dap::makeErrorResponse (seq, command,
        "WHATDBG: unsupported command: " + command);
}
