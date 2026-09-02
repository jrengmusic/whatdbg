local scriptDirectory = vim.fn.fnamemodify (arg[0], ":h")
package.path = scriptDirectory .. "/?.lua;" .. package.path

local BreakpointScenarios = require ("scenario_breakpoint")
local ProcessScenarios = require ("scenario_process")
local TerminateScenarios = require ("scenario_terminate")

local binaryPath = arg[1]
local fixturePath = arg[2]
local fixtureWaitPath = arg[3]
local fixtureSourcePath = arg[4]
local fixtureCrashPath = arg[5]

assert (binaryPath ~= nil, "usage: run_smoke.lua <whatdbg> <fixture> <fixture_wait> <fixture.cpp> <fixture_crash>")

local exitTimeoutMs = 10000
local breakpointMarker = "SMOKE_BREAKPOINT_LINE"

local function findBreakpointLine (sourcePath, marker)
    local sourceFile = assert (io.open (sourcePath, "r"))
    local lineNumber = 0
    local foundLine = nil

    for line in sourceFile:lines () do
        lineNumber = lineNumber + 1

        if line:find (marker, 1, true) ~= nil then
            foundLine = lineNumber
        end
    end

    sourceFile:close ()
    assert (foundLine ~= nil, "breakpoint marker '" .. marker .. "' not found in " .. sourcePath)
    return foundLine
end

local breakpointLine = findBreakpointLine (fixtureSourcePath, breakpointMarker)

local reports = {
    BreakpointScenarios.scenarioLaunchBpContinue (binaryPath, fixturePath, fixtureSourcePath, breakpointLine, exitTimeoutMs),
    ProcessScenarios.scenarioAttach (binaryPath, fixtureWaitPath),
    BreakpointScenarios.scenarioStep (binaryPath, fixturePath, fixtureSourcePath, breakpointLine, exitTimeoutMs),
    ProcessScenarios.scenarioPause (binaryPath, fixtureWaitPath),
    BreakpointScenarios.scenarioVariables (binaryPath, fixturePath, fixtureSourcePath, breakpointLine, exitTimeoutMs),
    BreakpointScenarios.scenarioEvaluate (binaryPath, fixturePath, fixtureSourcePath, breakpointLine, exitTimeoutMs),
    ProcessScenarios.scenarioOutput (binaryPath, fixturePath, exitTimeoutMs),
    ProcessScenarios.scenarioCrash (binaryPath, fixtureCrashPath, exitTimeoutMs),
    TerminateScenarios.scenarioTerminateNoZombie (binaryPath, fixturePath, fixtureSourcePath, breakpointLine),
    ProcessScenarios.scenarioDisconnectDetach (binaryPath, fixturePath, fixtureSourcePath, breakpointLine),
}

print ("== SMOKE REPORT ==")
print ("Binary: " .. binaryPath)
print ("Fixture: " .. fixturePath)
print ("")

local passCount = 0
local failCount = 0

for reportIndex, report in ipairs (reports) do
    report:print ()

    if report.failed then
        failCount = failCount + 1
    else
        passCount = passCount + 1
    end
end

print ("== SUMMARY == pass=" .. passCount .. " fail=" .. failCount)
