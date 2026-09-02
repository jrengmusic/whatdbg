local DapClient = require ("dap_client")
local Report = require ("report")
local ScenarioRunner = require ("scenario_runner")
local BreakpointScenarios = require ("scenario_breakpoint")
local TerminateScenarios = require ("scenario_terminate")

local function runToLaunched (client, report, launchArguments)
    client:send ("initialize", { adapterID = "whatdbg-smoke" })
    local initializeResponse = client:waitForResponse ("initialize")
    report:check ("initialize response success=true", initializeResponse ~= nil and initializeResponse.success == true)

    local initializedEvent = client:waitForEvent ("initialized")
    report:check ("received 'initialized' event", initializedEvent ~= nil)

    client:send ("launch", launchArguments)
    local launchResponse = client:waitForResponse ("launch")
    report:check ("launch response success=true", launchResponse ~= nil and launchResponse.success == true)

    client:send ("configurationDone", {})
    local configurationDoneResponse = client:waitForResponse ("configurationDone")
    report:check ("configurationDone response", configurationDoneResponse ~= nil and configurationDoneResponse.success == true)
end

local function scenarioAttach (binaryPath, fixtureWaitPath)
    local report = Report.new ("02_attach")

    return ScenarioRunner.runScenario (report, function ()
        local waitProcess = vim.system ({ fixtureWaitPath }, { stdout = false, stderr = false })
        vim.wait (500)
        local pid = waitProcess.pid
        report:check ("spawned fixture_wait pid=" .. tostring (pid), pid ~= nil and pid > 0)

        local client = DapClient.spawn (binaryPath, {})
        client:send ("initialize", { adapterID = "whatdbg-smoke" })
        local initializeResponse = client:waitForResponse ("initialize")
        report:check ("initialize response", initializeResponse ~= nil and initializeResponse.success == true)

        local initializedEvent = client:waitForEvent ("initialized")
        report:check ("initialized event", initializedEvent ~= nil)

        client:send ("attach", { pid = pid })
        local attachResponse = client:waitForResponse ("attach")
        report:check ("attach response success (pid=" .. tostring (pid) .. ")", attachResponse ~= nil and attachResponse.success == true)

        client:send ("configurationDone", {})
        local configurationDoneResponse = client:waitForResponse ("configurationDone")
        report:check ("configurationDone response", configurationDoneResponse ~= nil and configurationDoneResponse.success == true)

        local threadEvent = client:waitForEvent ("thread")
        report:check ("thread event received", threadEvent ~= nil)
        report:check ("thread reason == 'started'", threadEvent ~= nil and threadEvent.body.reason == "started")

        client:send ("terminate", {})
        client:waitForResponse ("terminate")
        client:close ()
        pcall (function () waitProcess:kill (9) end)
    end)
end

local function scenarioPause (binaryPath, fixtureWaitPath)
    local report = Report.new ("04_pause")

    return ScenarioRunner.runScenario (report, function ()
        local client = DapClient.spawn (binaryPath, {})
        runToLaunched (client, report, { program = fixtureWaitPath })

        local threadEvent = client:waitForEvent ("thread")
        report:check ("thread(started) event received", threadEvent ~= nil and threadEvent.body.reason == "started")
        local threadId = threadEvent.body.threadId

        client:send ("pause", { threadId = threadId })
        local pauseResponse = client:waitForResponse ("pause")
        report:check ("pause response", pauseResponse ~= nil and pauseResponse.success == true)

        local stoppedEvent = client:waitForEvent ("stopped")
        report:check ("stopped event received after pause", stoppedEvent ~= nil)
        report:info ("stopped reason=" .. tostring (stoppedEvent and stoppedEvent.body and stoppedEvent.body.reason))
        report:check ("stopped reason == 'pause'", stoppedEvent ~= nil and stoppedEvent.body.reason == "pause")

        client:send ("terminate", {})
        client:waitForResponse ("terminate")
        client:close ()
    end)
end

local function scenarioOutput (binaryPath, fixturePath, exitTimeoutMs)
    local report = Report.new ("07_output")

    return ScenarioRunner.runScenario (report, function ()
        local client = DapClient.spawn (binaryPath, {})
        runToLaunched (client, report, { program = fixturePath })

        local exitedEvent = client:waitForEvent ("exited", exitTimeoutMs)
        report:check ("exited event received", exitedEvent ~= nil)

        local stdoutFound = false
        local stderrFound = false

        for messageIndex, message in ipairs (client.log) do
            if message.type == "event" and message.event == "output" and message.body ~= nil then
                local outputText = tostring (message.body.output)

                if outputText:find ("BREAKPOINT_TARGET_A", 1, true) ~= nil then
                    stdoutFound = true
                end

                if outputText:find ("BREAKPOINT_TARGET_B", 1, true) ~= nil then
                    stderrFound = true
                end
            end
        end

        report:check ("stdout line captured (BREAKPOINT_TARGET_A in console output)", stdoutFound)
        report:check ("stderr line captured (BREAKPOINT_TARGET_B in console output)", stderrFound)

        client:close ()
    end)
end

local function scenarioCrash (binaryPath, fixtureCrashPath, exitTimeoutMs)
    local report = Report.new ("08_crash")

    return ScenarioRunner.runScenario (report, function ()
        local client = DapClient.spawn (binaryPath, {})
        client:send ("initialize", { adapterID = "whatdbg-smoke" })
        local initializeResponse = client:waitForResponse ("initialize")
        report:check ("initialize response", initializeResponse ~= nil and initializeResponse.success == true)

        local initializedEvent = client:waitForEvent ("initialized")
        report:check ("initialized event", initializedEvent ~= nil)

        client:send ("launch", { program = fixtureCrashPath })
        local launchResponse = client:waitForResponse ("launch")
        report:check ("launch response", launchResponse ~= nil and launchResponse.success == true)

        client:send ("configurationDone", {})
        local configurationDoneResponse = client:waitForResponse ("configurationDone")
        report:check ("configurationDone response", configurationDoneResponse ~= nil and configurationDoneResponse.success == true)

        local stoppedEvent = client:waitForEvent ("stopped", exitTimeoutMs)
        report:check ("stopped event received", stoppedEvent ~= nil)
        report:check ("stopped reason == 'exception'", stoppedEvent ~= nil and stoppedEvent.body.reason == "exception")
        local threadId = stoppedEvent.body.threadId

        client:send ("exceptionInfo", { threadId = threadId })
        local exceptionInfoResponse = client:waitForResponse ("exceptionInfo")
        report:check ("exceptionInfo response success", exceptionInfoResponse ~= nil and exceptionInfoResponse.success == true)
        report:check ("exceptionInfo exceptionId non-empty: " .. tostring (exceptionInfoResponse and exceptionInfoResponse.body.exceptionId), exceptionInfoResponse ~= nil and tostring (exceptionInfoResponse.body.exceptionId):find ("EXC_BAD_ACCESS", 1, true) ~= nil)
        report:check ("exceptionInfo breakMode == 'unhandled'", exceptionInfoResponse ~= nil and exceptionInfoResponse.body.breakMode == "unhandled")

        client:send ("terminate", {})
        client:waitForResponse ("terminate")
        client:close ()
    end)
end

local function scenarioDisconnectDetach (binaryPath, fixturePath, fixtureSourcePath, breakpointLine)
    local report = Report.new ("10_disconnect_detach")

    return ScenarioRunner.runScenario (report, function ()
        local client = DapClient.spawn (binaryPath, {})
        local stoppedEvent = BreakpointScenarios.runToStoppedBreakpoint (client, report, { program = fixturePath }, fixtureSourcePath, breakpointLine)
        report:info ("stopped before disconnect, threadId=" .. tostring (stoppedEvent.body.threadId))

        local debuggeePid = TerminateScenarios.findDebuggeePid (fixturePath)
        report:check ("debuggee pid discovered: " .. tostring (debuggeePid), debuggeePid ~= nil)

        client:send ("disconnect", {})
        local disconnectResponse = client:waitForResponse ("disconnect")
        report:check ("disconnect response success=true", disconnectResponse ~= nil and disconnectResponse.success == true)

        local pidStillAlive = vim.wait (500, function ()
            local checkResult = vim.system ({ "ps", "-o", "pid", "-p", tostring (debuggeePid) }, { text = true }):wait (2000)
            return tostring (checkResult.stdout or ""):match (tostring (debuggeePid)) == nil
        end, 100) == false

        report:check ("debuggee pid " .. tostring (debuggeePid) .. " left running after disconnect (detach, not kill)", pidStillAlive)

        pcall (function () vim.system ({ "kill", "-9", tostring (debuggeePid) }):wait (2000) end)
        client:close ()
    end)
end

return {
    runToLaunched = runToLaunched,
    scenarioAttach = scenarioAttach,
    scenarioPause = scenarioPause,
    scenarioOutput = scenarioOutput,
    scenarioCrash = scenarioCrash,
    scenarioDisconnectDetach = scenarioDisconnectDetach,
}
