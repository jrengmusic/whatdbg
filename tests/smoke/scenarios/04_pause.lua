-- Scenario 04: Feature 5 — pause / interrupt running target
--
-- Flow:
--   1. Launch smoke_fixture_wait via adapter (no breakpoints)
--   2. configurationDone → target runs (30s sleep)
--   3. Send `pause` → adapter calls SBProcess::SendAsyncInterrupt
--   4. Expect stopped(pause)
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
    name = "04_pause",

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
                program = context.waitFixtureBinary,
                cwd     = vim.fn.fnamemodify (context.waitFixtureBinary, ":h"),
            })
            assertStep (result,
                        (client:waitForResponse (launchSeq, 30000) or {}).success == true,
                        "launch response")

            local cfgSeq = client:send ("configurationDone", {})
            assertStep (result,
                        (client:waitForResponse (cfgSeq, 10000) or {}).success == true,
                        "configurationDone response")

            local threadEvt = client:waitForEvent ("thread", 10000)
            assertStep (result,
                        threadEvt and threadEvt.body.reason == "started",
                        "thread(started) event received")

            -- Let the target run for a short time so the pause lands inside sleep().
            vim.wait (500, function () return false end, 20)

            local threadId = threadEvt.body.threadId or 1

            local pauseSeq = client:send ("pause", { threadId = threadId })
            assertStep (result,
                        (client:waitForResponse (pauseSeq, 5000) or {}).success == true,
                        "pause response")

            local stoppedEvt = client:waitForEvent ("stopped", 10000)
            assertStep (result, stoppedEvt ~= nil, "stopped event received after pause")
            assertStep (result,
                        stoppedEvt and stoppedEvt.body.reason == "pause",
                        "stopped reason == 'pause'")

            local discSeq = client:send ("disconnect", { terminateDebuggee = true })
            client:waitForResponse (discSeq, 5000)
        end)

        client:shutdown ()
        if not ok then error (err, 0) end
    end,
}
