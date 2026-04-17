-- Scenario 05: Feature 6 — variable inspection + pretty-print parity
--
-- Exercises: setBreakpoints → stopped(breakpoint) → stackTrace → scopes
--            → variables → assert juce::String / std::string / std::unique_ptr /
--            std::vector pretty-printed values match Windows parity.

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

local function findVariable (varList, name)
    for _, v in ipairs (varList or {}) do
        if v.name == name then return v end
    end
    return nil
end

return {
    name = "05_variables",

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
                threadId = threadId, startFrame = 0, levels = 20,
            })
            local stResp = client:waitForResponse (stSeq, 5000)
            assertStep (result,
                        stResp and stResp.body and stResp.body.stackFrames
                            and #stResp.body.stackFrames >= 1,
                        "stackTrace returned >=1 frame")

            local frameId = stResp.body.stackFrames[1].id

            local scopeSeq = client:send ("scopes", { frameId = frameId })
            local scopeResp = client:waitForResponse (scopeSeq, 5000)
            assertStep (result,
                        scopeResp and scopeResp.body and scopeResp.body.scopes
                            and #scopeResp.body.scopes >= 1,
                        "scopes returned >=1 scope")

            local localsRef = scopeResp.body.scopes[1].variablesReference

            local varSeq = client:send ("variables", { variablesReference = localsRef })
            local varResp = client:waitForResponse (varSeq, 5000)
            assertStep (result,
                        varResp and varResp.body and varResp.body.variables,
                        "variables returned")

            local vars = varResp.body.variables or {}

            local greeting = findVariable (vars, "greeting")
            assertStep (result, greeting ~= nil, "local 'greeting' present")
            assertStep (result,
                        greeting and greeting.value and greeting.value:find ("hello from juce::String", 1, true),
                        "greeting value contains juce::String content")

            local name = findVariable (vars, "name")
            assertStep (result, name ~= nil, "local 'name' present")
            assertStep (result,
                        name and name.value and name.value:find ("hello from std::string", 1, true),
                        "name value contains std::string content")

            local counter = findVariable (vars, "counter")
            assertStep (result, counter ~= nil, "local 'counter' present")
            assertStep (result,
                        counter and counter.value
                            and counter.value ~= ""
                            and counter.value ~= "<unavailable>",
                        "counter has a non-empty value: " .. tostring (counter and counter.value))

            local numbers = findVariable (vars, "numbers")
            assertStep (result, numbers ~= nil, "local 'numbers' present")
            assertStep (result,
                        numbers and numbers.value and numbers.value:find ("size=", 1, true),
                        "numbers value contains 'size=' prefix")

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
