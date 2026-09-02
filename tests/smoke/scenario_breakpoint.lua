local DapClient = require ("dap_client")
local Report = require ("report")
local ScenarioRunner = require ("scenario_runner")

local function runToStoppedBreakpoint (client, report, launchArguments, fixtureSourcePath, breakpointLine)
    client:send ("initialize", { adapterID = "whatdbg-smoke" })
    local initializeResponse = client:waitForResponse ("initialize")
    report:check ("initialize response success=true", initializeResponse ~= nil and initializeResponse.success == true)

    local initializedEvent = client:waitForEvent ("initialized")
    report:check ("received 'initialized' event", initializedEvent ~= nil)

    client:send ("launch", launchArguments)
    local launchResponse = client:waitForResponse ("launch")
    report:check ("launch response success=true", launchResponse ~= nil and launchResponse.success == true)

    client:send ("setBreakpoints", {
        source = { path = fixtureSourcePath },
        breakpoints = { { line = breakpointLine } },
    })
    local setBreakpointsResponse = client:waitForResponse ("setBreakpoints")
    report:check ("setBreakpoints response (line=" .. breakpointLine .. ")", setBreakpointsResponse ~= nil and setBreakpointsResponse.success == true)

    client:send ("configurationDone", {})
    local configurationDoneResponse = client:waitForResponse ("configurationDone")
    report:check ("configurationDone response", configurationDoneResponse ~= nil and configurationDoneResponse.success == true)

    local stoppedEvent = client:waitForEvent ("stopped")
    report:check ("received 'stopped' event", stoppedEvent ~= nil)
    report:info ("stopped reason=" .. tostring (stoppedEvent and stoppedEvent.body and stoppedEvent.body.reason))
    report:check ("stopped reason == 'breakpoint'", stoppedEvent ~= nil and stoppedEvent.body.reason == "breakpoint")

    return stoppedEvent
end

local function scenarioLaunchBpContinue (binaryPath, fixturePath, fixtureSourcePath, breakpointLine, exitTimeoutMs)
    local report = Report.new ("01_launch_bp_continue")

    return ScenarioRunner.runScenario (report, function ()
        local client = DapClient.spawn (binaryPath, {})
        runToStoppedBreakpoint (client, report, { program = fixturePath }, fixtureSourcePath, breakpointLine)

        client:send ("threads", {})
        local threadsResponse = client:waitForResponse ("threads")
        report:check ("threads returned >=1 thread", threadsResponse ~= nil and #threadsResponse.body.threads >= 1)
        local threadId = threadsResponse.body.threads[1].id

        client:send ("stackTrace", { threadId = threadId })
        local stackTraceResponse = client:waitForResponse ("stackTrace")
        report:check ("stackTrace returned >=1 frame", stackTraceResponse ~= nil and #stackTraceResponse.body.stackFrames >= 1)

        client:send ("continue", { threadId = threadId })
        local continueResponse = client:waitForResponse ("continue")
        report:check ("continue response", continueResponse ~= nil and continueResponse.success == true)

        local exitedEvent = client:waitForEvent ("exited", exitTimeoutMs)
        report:check ("received 'exited' event", exitedEvent ~= nil)

        local terminatedEvent = client:waitForEvent ("terminated", exitTimeoutMs)
        report:check ("received 'terminated' event", terminatedEvent ~= nil)

        client:close ()
    end)
end

local function scenarioStep (binaryPath, fixturePath, fixtureSourcePath, breakpointLine, exitTimeoutMs)
    local report = Report.new ("03_step")

    return ScenarioRunner.runScenario (report, function ()
        local client = DapClient.spawn (binaryPath, {})
        local stoppedEvent = runToStoppedBreakpoint (client, report, { program = fixturePath }, fixtureSourcePath, breakpointLine)
        local threadId = stoppedEvent.body.threadId

        client:send ("next", { threadId = threadId })
        local nextResponse = client:waitForResponse ("next")
        report:check ("next response", nextResponse ~= nil and nextResponse.success == true)

        local stepEvent = client:waitForEvent ("stopped")
        report:check ("stopped(step) received", stepEvent ~= nil)
        report:check ("stopped reason == 'step'", stepEvent ~= nil and stepEvent.body.reason == "step")

        client:send ("continue", { threadId = threadId })
        local continueAck = client:waitForResponse ("continue")
        report:info ("continue after step response success=" .. tostring (continueAck and continueAck.success))
        local exitedEvent = client:waitForEvent ("exited", exitTimeoutMs)

        if exitedEvent == nil then
            local logSummary = {}

            for messageIndex, message in ipairs (client.log) do
                table.insert (logSummary, tostring (message.type) .. ":" .. tostring (message.event or message.command))
            end

            report:info ("post-continue message log: " .. table.concat (logSummary, ", "))
        end

        report:check ("exited event (after continue loop)", exitedEvent ~= nil)

        client:close ()
    end)
end

local function findVariable (variables, variableName)
    for variableIndex, variable in ipairs (variables) do
        if variable.name == variableName then
            return variable
        end
    end

    return nil
end

local function scenarioVariables (binaryPath, fixturePath, fixtureSourcePath, breakpointLine, exitTimeoutMs)
    local report = Report.new ("05_variables")

    return ScenarioRunner.runScenario (report, function ()
        local client = DapClient.spawn (binaryPath, {})
        local stoppedEvent = runToStoppedBreakpoint (client, report, { program = fixturePath }, fixtureSourcePath, breakpointLine)
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
        local variables = variablesResponse.body.variables

        report:info ("juce::String local dropped: fixture uses no juce_core dependency")

        local nameVariable = findVariable (variables, "name")
        report:check ("local 'name' present", nameVariable ~= nil)
        report:check ("name value contains std::string content", nameVariable ~= nil and tostring (nameVariable.value):find ("whatdbg", 1, true) ~= nil)

        local counterVariable = findVariable (variables, "counter")
        report:check ("local 'counter' present", counterVariable ~= nil)
        report:check ("counter has a non-empty value: " .. tostring (counterVariable and counterVariable.value), counterVariable ~= nil and tostring (counterVariable.value):find ("42", 1, true) ~= nil)

        local numbersVariable = findVariable (variables, "numbers")
        report:check ("local 'numbers' present", numbersVariable ~= nil)
        report:check ("numbers value contains 'size=' prefix", numbersVariable ~= nil and tostring (numbersVariable.value):find ("size=", 1, true) ~= nil)

        client:send ("continue", { threadId = threadId })
        client:waitForResponse ("continue")
        client:waitForEvent ("exited", exitTimeoutMs)
        client:close ()
    end)
end

local function scenarioEvaluate (binaryPath, fixturePath, fixtureSourcePath, breakpointLine, exitTimeoutMs)
    local report = Report.new ("06_evaluate")

    return ScenarioRunner.runScenario (report, function ()
        local client = DapClient.spawn (binaryPath, {})
        local stoppedEvent = runToStoppedBreakpoint (client, report, { program = fixturePath }, fixtureSourcePath, breakpointLine)
        local threadId = stoppedEvent.body.threadId

        client:send ("stackTrace", { threadId = threadId })
        local stackTraceResponse = client:waitForResponse ("stackTrace")
        local frameId = stackTraceResponse.body.stackFrames[1].id

        client:send ("evaluate", { expression = "1 + 2", frameId = frameId, context = "repl" })
        local sumResponse = client:waitForResponse ("evaluate")
        report:check ("evaluate '1 + 2' result contains '3'", sumResponse ~= nil and tostring (sumResponse.body.result):find ("3", 1, true) ~= nil)

        client:send ("evaluate", { expression = "*counterPtr", frameId = frameId, context = "repl" })
        local derefResponse = client:waitForResponse ("evaluate")
        report:check ("evaluate '*counterPtr' result contains '42'", derefResponse ~= nil and tostring (derefResponse.body.result):find ("42", 1, true) ~= nil)

        client:send ("continue", { threadId = threadId })
        client:waitForResponse ("continue")
        client:waitForEvent ("exited", exitTimeoutMs)
        client:close ()
    end)
end

return {
    runToStoppedBreakpoint = runToStoppedBreakpoint,
    scenarioLaunchBpContinue = scenarioLaunchBpContinue,
    scenarioStep = scenarioStep,
    scenarioVariables = scenarioVariables,
    scenarioEvaluate = scenarioEvaluate,
}
