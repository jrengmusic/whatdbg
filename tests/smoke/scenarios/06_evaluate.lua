-- Scenario 06: Feature 7 — expression evaluation
--
-- Exercises: stopped at BP → evaluate simple arithmetic + local variable ref.
-- Adapter idiom on Mac uses SBFrame::EvaluateExpression.

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

local function locateBreakpointLine (sourcePath, marker)
    local file = io.open (sourcePath, "r")
    assert (file, "cannot read " .. sourcePath)

    local lineNumber = 0
    local foundLine
    for line in file:lines () do
        lineNumber = lineNumber + 1
        if not line:match ("^%s*//") and line:find (marker, 1, true) then
            foundLine = lineNumber
            break
        end
    end
    file:close ()

    assert (foundLine, "marker '" .. marker .. "' not found in " .. sourcePath)
    return foundLine
end

return {
    name = "06_evaluate",

    run = function (context, result)
        local client = context.Client.new (context.whatdbgBinary)

        local ok, err = pcall (function ()
            client:send ("initialize", {
                clientID = "whatdbg-smoke", adapterID = "whatdbg",
                linesStartAt1 = true, columnsStartAt1 = true, pathFormat = "path",
            })
            client:waitForResponse (1, 5000)
            client:waitForEvent ("initialized", 5000)

            local launchSeq = client:send ("launch", {
                program = context.fixtureBinary,
                cwd     = vim.fn.fnamemodify (context.fixtureSource, ":h"),
            })
            assertStep (result,
                        (client:waitForResponse (launchSeq, 30000) or {}).success == true,
                        "launch response")

            local bpLine = locateBreakpointLine (context.fixtureSource, "BREAKPOINT_TARGET_A")
            local bpSeq = client:send ("setBreakpoints", {
                source      = { path = context.fixtureSource, name = "fixture.cpp" },
                breakpoints = { { line = bpLine } },
                lines       = { bpLine },
            })
            client:waitForResponse (bpSeq, 10000)

            local cfgSeq = client:send ("configurationDone", {})
            client:waitForResponse (cfgSeq, 10000)

            local stoppedEvt = client:waitForEvent ("stopped", 15000)
            assertStep (result, stoppedEvt ~= nil, "stopped(breakpoint) received")

            local threadId = stoppedEvt and stoppedEvt.body.threadId or 1

            local stSeq = client:send ("stackTrace", {
                threadId = threadId, startFrame = 0, levels = 5,
            })
            local stResp = client:waitForResponse (stSeq, 5000)
            local frameId = stResp and stResp.body and stResp.body.stackFrames
                                and stResp.body.stackFrames[1]
                                and stResp.body.stackFrames[1].id or 0

            -- Arithmetic expression
            local arithSeq = client:send ("evaluate", {
                expression = "1 + 2", frameId = frameId,
            })
            local arithResp = client:waitForResponse (arithSeq, 5000)
            assertStep (result,
                        arithResp and arithResp.success
                            and arithResp.body and arithResp.body.result
                            and arithResp.body.result:find ("3", 1, true),
                        "evaluate '1 + 2' result contains '3'")

            -- Local variable dereference
            local countSeq = client:send ("evaluate", {
                expression = "*counter", frameId = frameId,
            })
            local countResp = client:waitForResponse (countSeq, 5000)
            assertStep (result,
                        countResp and countResp.success
                            and countResp.body and countResp.body.result
                            and countResp.body.result:find ("42", 1, true),
                        "evaluate '*counter' result contains '42'")

            local contSeq = client:send ("continue", { threadId = threadId })
            client:waitForResponse (contSeq, 5000)
            client:waitForEvent ("exited", 10000)

            local discSeq = client:send ("disconnect", { terminateDebuggee = false })
            client:waitForResponse (discSeq, 3000)
        end)

        client:shutdown ()
        if not ok then error (err, 0) end
    end,
}
