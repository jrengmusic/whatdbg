# WHATDBG: WYSIWYG Hybrid Abstraction Translator for DAP Debug Adapter

**Version 0.0.1**

You see this guide because you ran `whatdbg --help`.

DAP debug adapter for native C/C++ targets.

## USAGE

```
  whatdbg            start the adapter; it speaks DAP on stdin/stdout
  whatdbg --help     print this text and exit
```

## PROTOCOL

```
  whatdbg is a Debug Adapter Protocol (DAP) server over stdio. Each message
  is JSON, framed with a Content-Length header:

    Content-Length: <byte count>\r\n
    \r\n
    {"seq":1,"type":"request","command":"initialize","arguments":{}}

  Engines: Windows uses dbgeng (the WinDbg engine); macOS uses liblldb.
  Both are embedded and self-extracted — no install, no Developer-Tools
  permission dialog, safe for headless and scripted use.
```

## SESSION SEQUENCE — LAUNCH

```
  -> initialize                          <- capabilities, initialized event
  -> launch {"program":"/path/to/bin","cwd":"/src/root"}
     (cwd is optional; it seeds symbol and source search paths)
  -> setBreakpoints {"source":{"path":...},"breakpoints":[{"line":N}]}
  -> configurationDone                   <- process event (name,
     systemProcessId, startMethod "launch"), thread event ("started");
     the target runs
```

## SESSION SEQUENCE — ATTACH

```
  -> attach {"pid":N,"cwd":"/src/root"}  <- process event, startMethod
     "attach", systemProcessId = the debuggee pid
```

## WHILE RUNNING

```
  <- stopped    reason "breakpoint" | "step" | "pause" | "exception"
  <- output     debuggee stdout/stderr (macOS); OutputDebugString/DBG
                (Windows)
  <- exited, terminated   when the target ends
```

## INSPECTION (while stopped)

```
  -> threads, stackTrace, scopes, variables
     stacks and locals; juce::String, std::string, std::unique_ptr,
     std::vector pretty-printed (per platform)
  -> evaluate {"expression":"...","frameId":N}
     expressions in frame context (member access, arithmetic, casts)
  -> next, stepIn, stepOut, continue, pause
```

## CRASH TRIAGE

```
  On an unhandled exception or signal (for example SIGSEGV) whatdbg emits:
    stopped   reason "exception", text = exception/signal name,
              description = "0x<code> at 0x<address>"
    output    category "stderr" with a crash summary
  -> exceptionInfo  returns exceptionId, description, breakMode
     "unhandled"
  -> stackTrace on the stopped thread gives the crash backtrace
```

## ENDING

```
  -> terminate, or disconnect {"terminateDebuggee":true}   kill the target
  -> disconnect                                 detach; target keeps running
```

## FILES

```
  Windows sidecar: ~/.config/whatdbg/dbgeng/
  macOS sidecar:   ~/Library/Application Support/whatdbg/liblldb/
                   (first run re-execs with DYLD_LIBRARY_PATH set)
```

## REFERENCE CLIENT

```
  tests/smoke/dap_client.lua in the whatdbg repository is a complete
  scriptable DAP client (nvim --headless -l); tests/smoke/run_smoke.lua
  shows full sessions: launch, breakpoints, crash triage, attach,
  terminate.
```
