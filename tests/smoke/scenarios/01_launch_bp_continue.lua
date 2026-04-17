-- Scenario 01: launch → setBreakpoints → configurationDone → stopped(bp) →
--               stackTrace → continue → exited
--
-- Exercises: Feature 1 (Launch), Feature 3 (Breakpoints, deferred resolution),
-- Feature 9 (Threads), Feature 10 (Terminate via natural exit).
-- Fixture: tests/smoke/fixture.cpp BREAKPOINT_TARGET_A / _B lines.

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
    name = "01_launch_bp_continue",

    run = function (context, result)
        local client = context.Client.new (context.whatdbgBinary)

        local ok, err = pcall (function ()
            -- 1. initialize
            local initSeq = client:send ("initialize", {
                clientID       = "whatdbg-smoke",
                adapterID      = "whatdbg",
                linesStartAt1  = true,
                columnsStartAt1 = true,
                pathFormat     = "path",
            })
            local initResp = client:waitForResponse (initSeq)
            assertStep (result, initResp and initResp.success,
                        "initialize response success=" .. tostring (initResp and initResp.success))

            local initializedEvt = client:waitForEvent ("initialized")
            assertStep (result, initializedEvt ~= nil, "received 'initialized' event")

            -- 2. launch
            local launchSeq = client:send ("launch", {
                program = context.fixtureBinary,
                cwd     = vim.fn.fnamemodify (context.fixtureSource, ":h"),
            })
            local launchResp = client:waitForResponse (launchSeq, 30000)
            assertStep (result, launchResp and launchResp.success,
                        "launch response success=" .. tostring (launchResp and launchResp.success))

            -- 3. setBreakpoints at BREAKPOINT_TARGET_A
            local bpLine = locateBreakpointLine (context.fixtureSource, "BREAKPOINT_TARGET_A")
            local bpSeq = client:send ("setBreakpoints", {
                source      = { path = context.fixtureSource, name = "fixture.cpp" },
                breakpoints = { { line = bpLine } },
                lines       = { bpLine },
            })
            local bpResp = client:waitForResponse (bpSeq, 10000)
            assertStep (result, bpResp and bpResp.success,
                        "setBreakpoints response (line=" .. bpLine .. ")")

            -- 4. configurationDone
            local cfgSeq = client:send ("configurationDone", {})
            local cfgResp = client:waitForResponse (cfgSeq, 10000)
            assertStep (result, cfgResp and cfgResp.success, "configurationDone response")

            -- 5. wait for stopped event (breakpoint) OR exited (if BP missed)
            local stoppedEvt = client:waitForEvent ("stopped", 15000)
            assertStep (result, stoppedEvt ~= nil, "received 'stopped' event")

            if stoppedEvt then
                recordStep (result, "info", "stopped reason=" .. tostring (stoppedEvt.body.reason))
                assertStep (result, stoppedEvt.body.reason == "breakpoint",
                            "stopped reason == 'breakpoint'")
            end

            -- 6. threads
            local threadsSeq = client:send ("threads", {})
            local threadsResp = client:waitForResponse (threadsSeq)
            assertStep (result,
                        threadsResp and threadsResp.success
                            and threadsResp.body and threadsResp.body.threads
                            and #threadsResp.body.threads >= 1,
                        "threads returned >=1 thread")

            -- 7. stackTrace on stopped thread
            local stopThreadId = stoppedEvt and stoppedEvt.body.threadId or 1
            local stSeq = client:send ("stackTrace", {
                threadId   = stopThreadId,
                startFrame = 0,
                levels     = 20,
            })
            local stResp = client:waitForResponse (stSeq)
            assertStep (result,
                        stResp and stResp.success
                            and stResp.body and stResp.body.stackFrames
                            and #stResp.body.stackFrames >= 1,
                        "stackTrace returned >=1 frame")

            -- 8. continue to exit
            local contSeq = client:send ("continue", { threadId = stopThreadId })
            local contResp = client:waitForResponse (contSeq)
            assertStep (result, contResp and contResp.success, "continue response")

            local exitedEvt = client:waitForEvent ("exited", 10000)
            assertStep (result, exitedEvt ~= nil, "received 'exited' event")

            -- 9. disconnect
            local discSeq = client:send ("disconnect", { terminateDebuggee = false })
            client:waitForResponse (discSeq, 3000)
        end)

        client:shutdown ()

        if not ok then error (err, 0) end
    end,
}
