local Report = require ("report")
local ScenarioRunner = require ("scenario_runner")
local BreakpointScenarios = require ("scenario_breakpoint")

local function scenarioStructChildren (binaryPath, fixturePath, fixtureSourcePath, breakpointLine, exitTimeoutMs)
    local report = Report.new ("12_struct_children")

    return ScenarioRunner.runScenario (report, binaryPath, function (client)
        local stoppedEvent = BreakpointScenarios.runToStoppedBreakpoint (client, report, { program = fixturePath }, fixtureSourcePath, breakpointLine)
        local threadId = stoppedEvent.body.threadId

        client:send ("stackTrace", { threadId = threadId })
        local stackTraceResponse = client:waitForResponse ("stackTrace")
        report:check ("stackTrace returned >=1 frame", stackTraceResponse ~= nil and #stackTraceResponse.body.stackFrames >= 1)
        local frameId = stackTraceResponse.body.stackFrames[1].id

        client:send ("scopes", { frameId = frameId })
        local scopesResponse = client:waitForResponse ("scopes")
        report:check ("scopes returned >=1 scope", scopesResponse ~= nil and #scopesResponse.body.scopes >= 1)
        local variablesReference = scopesResponse.body.scopes[1].variablesReference

        client:send ("variables", { variablesReference = variablesReference })
        local variablesResponse = client:waitForResponse ("variables")
        report:check ("variables returned", variablesResponse ~= nil and variablesResponse.body ~= nil)

        local pointVariable = BreakpointScenarios.findVariable (variablesResponse.body.variables, "point")
        report:check ("local 'point' present", pointVariable ~= nil)
        report:check ("point variablesReference > 0", pointVariable ~= nil and pointVariable.variablesReference > 0)

        client:send ("variables", { variablesReference = pointVariable.variablesReference })
        local childrenResponse = client:waitForResponse ("variables")
        local children = childrenResponse.body.variables

        local xVariable = BreakpointScenarios.findVariable (children, "x")
        report:check ("child 'x' present", xVariable ~= nil)
        report:check ("x value contains 3", xVariable ~= nil and tostring (xVariable.value):find ("3", 1, true) ~= nil)

        local yVariable = BreakpointScenarios.findVariable (children, "y")
        report:check ("child 'y' present", yVariable ~= nil)
        report:check ("y value contains 4", yVariable ~= nil and tostring (yVariable.value):find ("4", 1, true) ~= nil)

        client:send ("continue", { threadId = threadId })
        client:waitForResponse ("continue")
        client:waitForEvent ("exited", exitTimeoutMs)
    end)
end

local function scenarioThreadFrames (binaryPath, fixturePath, fixtureSourcePath, breakpointLine, exitTimeoutMs)
    local report = Report.new ("13_thread_frames")

    return ScenarioRunner.runScenario (report, binaryPath, function (client)
        local stoppedEvent = BreakpointScenarios.runToStoppedBreakpoint (client, report, { program = fixturePath }, fixtureSourcePath, breakpointLine)
        local stoppedThreadId = stoppedEvent.body.threadId

        client:send ("threads", {})
        local threadsResponse = client:waitForResponse ("threads")
        report:check ("threads returned >=2 threads", threadsResponse ~= nil and #threadsResponse.body.threads >= 2)

        local otherThreadId = nil

        for threadIndex, thread in ipairs (threadsResponse.body.threads) do
            if otherThreadId == nil and thread.id ~= stoppedThreadId then
                otherThreadId = thread.id
            end
        end

        client:send ("stackTrace", { threadId = otherThreadId })
        local stackTraceResponse = client:waitForResponse ("stackTrace")
        report:check ("stackTrace of other thread success", stackTraceResponse ~= nil and stackTraceResponse.success == true)
        report:check ("stackTrace of other thread returned >=1 frame", stackTraceResponse ~= nil and #stackTraceResponse.body.stackFrames >= 1)
        local frameId = stackTraceResponse.body.stackFrames[1].id

        client:send ("scopes", { frameId = frameId })
        local scopesResponse = client:waitForResponse ("scopes")
        report:check ("scopes of other thread frame success", scopesResponse ~= nil and scopesResponse.success == true)
        local variablesReference = scopesResponse.body.scopes[1].variablesReference

        client:send ("variables", { variablesReference = variablesReference })
        local variablesResponse = client:waitForResponse ("variables")
        report:check ("variables of other thread frame success", variablesResponse ~= nil and variablesResponse.success == true)

        client:send ("continue", { threadId = stoppedThreadId })
        client:waitForResponse ("continue")
        client:waitForEvent ("exited", exitTimeoutMs)
    end)
end

local function scenarioEvaluateFail (binaryPath, fixturePath, fixtureSourcePath, breakpointLine, exitTimeoutMs)
    local report = Report.new ("17_evaluate_fail")

    return ScenarioRunner.runScenario (report, binaryPath, function (client)
        local stoppedEvent = BreakpointScenarios.runToStoppedBreakpoint (client, report, { program = fixturePath }, fixtureSourcePath, breakpointLine)
        local threadId = stoppedEvent.body.threadId

        client:send ("stackTrace", { threadId = threadId })
        local stackTraceResponse = client:waitForResponse ("stackTrace")
        local frameId = stackTraceResponse.body.stackFrames[1].id

        client:send ("evaluate", { expression = "notAVariable", frameId = frameId, context = "repl" })
        local evaluateResponse = client:waitForResponse ("evaluate")
        report:check ("evaluate 'notAVariable' success=false", evaluateResponse ~= nil and evaluateResponse.success == false)
        report:check ("evaluate message contains 'Could not evaluate'", evaluateResponse ~= nil and tostring (evaluateResponse.message):find ("Could not evaluate", 1, true) ~= nil)

        client:send ("continue", { threadId = threadId })
        client:waitForResponse ("continue")
        client:waitForEvent ("exited", exitTimeoutMs)
    end)
end

local function scenarioInnerFrame (binaryPath, fixturePath, fixtureSourcePath, innerLine, exitTimeoutMs)
    local report = Report.new ("18_inner_frame")

    return ScenarioRunner.runScenario (report, binaryPath, function (client)
        local stoppedEvent = BreakpointScenarios.runToStoppedBreakpoint (client, report, { program = fixturePath }, fixtureSourcePath, innerLine)
        local threadId = stoppedEvent.body.threadId

        client:send ("stackTrace", { threadId = threadId })
        local stackTraceResponse = client:waitForResponse ("stackTrace")
        local stackFrames = stackTraceResponse.body.stackFrames
        report:check ("stackTrace returned >=2 frames", stackTraceResponse ~= nil and #stackFrames >= 2)
        report:check ("frame 1 is computeSum", tostring (stackFrames[1].name):find ("computeSum", 1, true) ~= nil)
        report:check ("frame 2 is main", tostring (stackFrames[2].name):find ("main", 1, true) ~= nil)
        local outerFrameId = stackFrames[2].id

        client:send ("scopes", { frameId = outerFrameId })
        local scopesResponse = client:waitForResponse ("scopes")
        local variablesReference = scopesResponse.body.scopes[1].variablesReference

        client:send ("variables", { variablesReference = variablesReference })
        local variablesResponse = client:waitForResponse ("variables")
        local counterVariable = BreakpointScenarios.findVariable (variablesResponse.body.variables, "counter")
        report:check ("frame 2 local 'counter' present", counterVariable ~= nil)
        report:check ("counter value contains 42", counterVariable ~= nil and tostring (counterVariable.value):find ("42", 1, true) ~= nil)

        client:send ("evaluate", { expression = "counter", frameId = outerFrameId, context = "repl" })
        local evaluateResponse = client:waitForResponse ("evaluate")
        report:check ("evaluate 'counter' in frame 2 result contains '42'", evaluateResponse ~= nil and tostring (evaluateResponse.body.result):find ("42", 1, true) ~= nil)

        client:send ("setBreakpoints", {
            source = { path = fixtureSourcePath },
            breakpoints = {},
        })
        local removeResponse = client:waitForResponse ("setBreakpoints")
        report:check ("setBreakpoints with empty list response", removeResponse ~= nil and removeResponse.success == true)

        client:send ("continue", { threadId = threadId })
        client:waitForResponse ("continue")
        client:waitForEvent ("exited", exitTimeoutMs)
    end)
end

return {
    scenarioStructChildren = scenarioStructChildren,
    scenarioThreadFrames = scenarioThreadFrames,
    scenarioEvaluateFail = scenarioEvaluateFail,
    scenarioInnerFrame = scenarioInnerFrame,
}
