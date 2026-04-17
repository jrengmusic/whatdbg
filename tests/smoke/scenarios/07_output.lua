-- Scenario 07: Feature 8 — debuggee stdout / stderr capture as DAP output events
--
-- Exercises: launch without BP → run to completion → assert DAP `output`
-- events were emitted with category=console and contain both BREAKPOINT_TARGET_A
-- (stdout) and BREAKPOINT_TARGET_B (stderr) markers. Windows parity: both
-- channels merge into category=console.

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

local function collectOutputText (eventsTable)
    local combined = ""
    for _, e in ipairs (eventsTable or {}) do
        if e.event == "output" and e.body
            and e.body.category == "console"
            and type (e.body.output) == "string"
        then
            combined = combined .. e.body.output
        end
    end
    return combined
end

return {
    name = "07_output",

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

            -- No breakpoints — let target run to completion.
            local cfgSeq = client:send ("configurationDone", {})
            client:waitForResponse (cfgSeq, 10000)

            assertStep (result,
                        client:waitForEvent ("exited", 15000) ~= nil,
                        "exited event received")

            local combined = collectOutputText (client.events)

            assertStep (result,
                        combined:find ("BREAKPOINT_TARGET_A", 1, true) ~= nil,
                        "stdout line captured (BREAKPOINT_TARGET_A in console output)")

            assertStep (result,
                        combined:find ("BREAKPOINT_TARGET_B", 1, true) ~= nil,
                        "stderr line captured (BREAKPOINT_TARGET_B in console output)")

            local discSeq = client:send ("disconnect", { terminateDebuggee = false })
            client:waitForResponse (discSeq, 3000)
        end)

        client:shutdown ()
        if not ok then error (err, 0) end
    end,
}
