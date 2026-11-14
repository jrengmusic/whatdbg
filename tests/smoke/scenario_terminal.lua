local DapClient = require ("dap_client")
local Report = require ("report")
local ScenarioRunner = require ("scenario_runner")
local BreakpointScenarios = require ("scenario_breakpoint")

local function scenarioRunInTerminal (binaryPath, fixturePath, fixtureSourcePath, breakpointLine, exitTimeoutMs)
    local report = Report.new ("22_run_in_terminal")

    return ScenarioRunner.runScenario (report, binaryPath, function (client)
        local stoppedEvent = BreakpointScenarios.runToStoppedBreakpoint (client, report, { program = fixturePath, console = DapClient.terminalConsole }, fixtureSourcePath, breakpointLine)
        local threadId = stoppedEvent.body.threadId

        client:send ("continue", { threadId = threadId })
        local continueResponse = client:waitForResponse ("continue")
        report:check ("continue response", continueResponse ~= nil and continueResponse.success == true)

        local exitedEvent = client:waitForEvent ("exited", exitTimeoutMs)
        report:check ("received 'exited' event", exitedEvent ~= nil)

        local terminatedEvent = client:waitForEvent ("terminated", exitTimeoutMs)
        report:check ("received 'terminated' event", terminatedEvent ~= nil)
    end)
end

return {
    scenarioRunInTerminal = scenarioRunInTerminal,
}
