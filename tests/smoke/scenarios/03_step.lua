-- Scenario 03: Feature 4 — stepping (step over)
--
-- Exercises: launch → setBreakpoints → configurationDone → stopped(breakpoint)
--            → next → stopped(step) → continue → exited

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
    name = "03_step",

    run = function (context, result)
        local client = context.Client.new (context.whatdbgBinary)

        local ok, err = pcall (function ()
            client:send ("initialize", {
                clientID = "whatdbg-smoke", adapterID = "whatdbg",
                linesStartAt1 = true, columnsStartAt1 = true, pathFormat = "path",
            })
            assertStep (result, client:waitForResponse (1, 5000) ~= nil, "initialize response")
            assertStep (result, client:waitForEvent ("initialized", 5000) ~= nil, "initialized event")

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
            assertStep (result,
                        (client:waitForResponse (bpSeq, 10000) or {}).success == true,
                        "setBreakpoints response")

            local cfgSeq = client:send ("configurationDone", {})
            assertStep (result,
                        (client:waitForResponse (cfgSeq, 10000) or {}).success == true,
                        "configurationDone response")

            local stoppedEvt = client:waitForEvent ("stopped", 15000)
            assertStep (result, stoppedEvt ~= nil, "stopped(breakpoint) received")
            assertStep (result,
                        stoppedEvt and stoppedEvt.body.reason == "breakpoint",
                        "stopped reason == 'breakpoint'")

            local threadId = stoppedEvt and stoppedEvt.body.threadId or 1

            local nextSeq = client:send ("next", { threadId = threadId })
            assertStep (result,
                        (client:waitForResponse (nextSeq, 5000) or {}).success == true,
                        "next response")

            local stepStopped = client:waitForEvent ("stopped", 10000)
            assertStep (result, stepStopped ~= nil, "stopped(step) received")
            assertStep (result,
                        stepStopped and stepStopped.body.reason == "step",
                        "stopped reason == 'step'")

            -- lldb may surface residual step-plan events after continue; loop
            -- continue until the process truly exits. Bounded to 5 iterations
            -- to prevent runaway if behavior is buggy.
            local exited = nil
            for attempt = 1, 5 do
                local contSeq = client:send ("continue", { threadId = threadId })
                if (client:waitForResponse (contSeq, 5000) or {}).success ~= true then
                    break
                end
                exited = client:waitForEvent ("exited", 5000)
                if exited ~= nil then break end
                -- Consume the unexpected stopped event so the next wait starts clean.
                client:waitForEvent ("stopped", 2000)
            end
            assertStep (result, exited ~= nil, "exited event (after continue loop)")

            local discSeq = client:send ("disconnect", { terminateDebuggee = false })
            client:waitForResponse (discSeq, 3000)
        end)

        client:shutdown ()
        if not ok then error (err, 0) end
    end,
}
