-- Scenario 08: Feature 11 — target crash / exception info surfacing
--
-- Flow:
--   1. Launch smoke_fixture_crash via adapter
--   2. configurationDone → target runs, dereferences null → SIGSEGV / EXC_BAD_ACCESS
--   3. Adapter emits stopped(reason=exception) + output(category=stderr)
--   4. Send exceptionInfo → verify exceptionId + breakMode=unhandled
--   5. Disconnect with terminateDebuggee=true

local function recordStep (result, status, detail)
    table.insert (result.steps, { status = status, detail = detail })
end

local function assertStep (result, ok, detail)
    if ok then
        recordStep (result, "pass", detail)
    else
        recordStep (result, "fail", detail)
        error (detail, 0)
    end
end

return {
    name = "08_crash",

    run = function (context, result)
        local client = context.Client.new (context.whatdbgBinary)

        local ok, err = pcall (function ()
            client:send ("initialize", {
                clientID = "whatdbg-smoke", adapterID = "whatdbg",
                linesStartAt1 = true, columnsStartAt1 = true, pathFormat = "path",
                supportsExceptionInfoRequest = true,
            })
            assertStep (result, client:waitForResponse (1, 5000) ~= nil, "initialize response")
            assertStep (result, client:waitForEvent ("initialized", 5000) ~= nil, "initialized event")

            local launchSeq = client:send ("launch", {
                program = context.crashFixtureBinary,
                cwd     = vim.fn.fnamemodify (context.crashFixtureBinary, ":h"),
            })
            assertStep (result,
                        (client:waitForResponse (launchSeq, 30000) or {}).success == true,
                        "launch response")

            local cfgSeq = client:send ("configurationDone", {})
            assertStep (result,
                        (client:waitForResponse (cfgSeq, 10000) or {}).success == true,
                        "configurationDone response")

            -- Wait for crash-induced stop. May take multiple events to reach exception.
            local stoppedEvt = client:waitForEvent ("stopped", 15000)
            assertStep (result, stoppedEvt ~= nil, "stopped event received")
            assertStep (result,
                        stoppedEvt and stoppedEvt.body.reason == "exception",
                        "stopped reason == 'exception'")

            local threadId = stoppedEvt.body.threadId or 1

            local excSeq = client:send ("exceptionInfo", { threadId = threadId })
            local excResp = client:waitForResponse (excSeq, 5000)
            assertStep (result, excResp and excResp.success == true,
                        "exceptionInfo response success")
            assertStep (result,
                        excResp and excResp.body and excResp.body.exceptionId
                            and #excResp.body.exceptionId > 0,
                        "exceptionInfo exceptionId non-empty: "
                            .. tostring (excResp and excResp.body and excResp.body.exceptionId))
            assertStep (result,
                        excResp and excResp.body and excResp.body.breakMode == "unhandled",
                        "exceptionInfo breakMode == 'unhandled'")

            local discSeq = client:send ("disconnect", { terminateDebuggee = true })
            client:waitForResponse (discSeq, 5000)
        end)

        client:shutdown ()
        if not ok then error (err, 0) end
    end,
}
