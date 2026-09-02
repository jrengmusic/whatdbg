local DapClient = require ("dap_client")
local Report = require ("report")
local ScenarioRunner = require ("scenario_runner")
local BreakpointScenarios = require ("scenario_breakpoint")

local function findDebuggeePid (fixturePath)
    local pgrepResult = vim.system ({ "pgrep", "-f", fixturePath .. "$" }, { text = true }):wait (2000)
    local firstLine = tostring (pgrepResult.stdout or ""):match ("(%d+)")
    return firstLine and tonumber (firstLine) or nil
end

local function countZombies (parentPid)
    local psResult = vim.system ({ "ps", "-ax", "-o", "pid,ppid,stat" }, { text = true }):wait (2000)
    local zombieCount = 0

    for line in tostring (psResult.stdout or ""):gmatch ("[^\n]+") do
        local processId, parentProcessId, processStat = line:match ("^%s*(%d+)%s+(%d+)%s+(%S+)")

        if processId ~= nil and tonumber (parentProcessId) == parentPid and processStat:match ("^Z") ~= nil then
            zombieCount = zombieCount + 1
        end
    end

    return zombieCount
end

local function scenarioTerminateNoZombie (binaryPath, fixturePath, fixtureSourcePath, breakpointLine)
    local report = Report.new ("09_terminate_no_zombie")

    return ScenarioRunner.runScenario (report, function ()
        local client = DapClient.spawn (binaryPath, {})
        BreakpointScenarios.runToStoppedBreakpoint (client, report, { program = fixturePath }, fixtureSourcePath, breakpointLine)

        local debuggeePid = findDebuggeePid (fixturePath)
        report:check ("debuggee pid discovered: " .. tostring (debuggeePid), debuggeePid ~= nil)

        local zombieCountBefore = countZombies (client.process.pid)
        report:info ("zombie count before terminate: " .. tostring (zombieCountBefore))

        client:send ("terminate", {})
        local terminateResponse = client:waitForResponse ("terminate")
        report:check ("terminate response success=true", terminateResponse ~= nil and terminateResponse.success == true)

        local pidGone = vim.wait (5000, function ()
            local checkResult = vim.system ({ "ps", "-o", "pid,ppid,stat", "-p", tostring (debuggeePid) }, { text = true }):wait (2000)
            local outputLines = tostring (checkResult.stdout or "")
            return outputLines:match (tostring (debuggeePid)) == nil
        end, 100)

        report:check ("debuggee pid " .. tostring (debuggeePid) .. " is gone from process table", pidGone == true)

        local finalCheck = vim.system ({ "ps", "-o", "pid,ppid,stat", "-p", tostring (debuggeePid) }, { text = true }):wait (2000)
        local finalOutput = tostring (finalCheck.stdout or "")
        report:check ("no zombie stat for debuggee pid (STAT does not contain Z)", finalOutput:match ("Z") == nil)

        local zombieCountAfter = countZombies (client.process.pid)
        report:info ("zombie count after terminate: " .. tostring (zombieCountAfter))
        report:check ("zombie count did not rise (" .. tostring (zombieCountBefore) .. " -> " .. tostring (zombieCountAfter) .. ")", zombieCountAfter <= zombieCountBefore)

        client:close ()
    end)
end

return {
    findDebuggeePid = findDebuggeePid,
    countZombies = countZombies,
    scenarioTerminateNoZombie = scenarioTerminateNoZombie,
}
