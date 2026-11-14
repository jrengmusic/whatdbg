local scriptDirectory = vim.fn.fnamemodify (arg[0], ":h")
package.path = scriptDirectory .. "/?.lua;" .. package.path

local BreakpointScenarios = require ("scenario_breakpoint")
local ProcessScenarios = require ("scenario_process")
local TerminateScenarios = require ("scenario_terminate")
local InspectScenarios = require ("scenario_inspect")
local ScenarioRunner = require ("scenario_runner")

local binaryPath = arg[1]
local fixturePath = arg[2]
local fixtureWaitPath = arg[3]
local fixtureSourcePath = arg[4]
local fixtureCrashPath = arg[5]

assert (binaryPath ~= nil, "usage: run_smoke.lua <whatdbg> <fixture> <fixture_wait> <fixture.cpp> <fixture_crash>")

local platformName = vim.fn.has ("win32") == 1 and "windows" or "macos"
ScenarioRunner.isRecording = (arg[6] == "--record")
ScenarioRunner.baselineDirectory = scriptDirectory .. "/baseline/" .. platformName
vim.fn.mkdir (ScenarioRunner.baselineDirectory, "p")

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
local innerLine = findBreakpointLine (fixtureSourcePath, "SMOKE_INNER_LINE")
local emptyLine = findBreakpointLine (fixtureSourcePath, "SMOKE_EMPTY_LINE")
local secondLine = findBreakpointLine (fixtureSourcePath, "SMOKE_SECOND_LINE")

local fixtureDirectory = vim.fn.fnamemodify (fixtureSourcePath, ":h")
local buildDirectory = vim.fn.fnamemodify (fixturePath, ":h")

local waitSourcePath = fixtureDirectory .. "/fixture_wait.cpp"
local waitLine = findBreakpointLine (waitSourcePath, breakpointMarker)

local moduleSourcePath = fixtureDirectory .. "/fixture_module.cpp"
local moduleLine = findBreakpointLine (moduleSourcePath, "SMOKE_MODULE_LINE")

local fixtureLoadPath = buildDirectory .. "/fixture_load" .. (platformName == "windows" and ".exe" or "")

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
    BreakpointScenarios.scenarioStepInOut (binaryPath, fixturePath, fixtureSourcePath, breakpointLine, exitTimeoutMs),
    InspectScenarios.scenarioStructChildren (binaryPath, fixturePath, fixtureSourcePath, breakpointLine, exitTimeoutMs),
    InspectScenarios.scenarioThreadFrames (binaryPath, fixturePath, fixtureSourcePath, breakpointLine, exitTimeoutMs),
    BreakpointScenarios.scenarioBreakpointWhileRunning (binaryPath, fixtureWaitPath, waitSourcePath, waitLine, exitTimeoutMs),
    BreakpointScenarios.scenarioBreakpointRemoveAndWindow (binaryPath, fixturePath, fixtureSourcePath, breakpointLine, emptyLine, exitTimeoutMs),
    BreakpointScenarios.scenarioPauseContinueBreakpoint (binaryPath, fixtureWaitPath, waitSourcePath, waitLine, exitTimeoutMs),
    InspectScenarios.scenarioEvaluateFail (binaryPath, fixturePath, fixtureSourcePath, breakpointLine, exitTimeoutMs),
    InspectScenarios.scenarioInnerFrame (binaryPath, fixturePath, fixtureSourcePath, innerLine, exitTimeoutMs),
    ProcessScenarios.scenarioConfigurationDoneOrder (binaryPath, fixturePath, exitTimeoutMs),
    ProcessScenarios.scenarioUnknownCommand (binaryPath),
    BreakpointScenarios.scenarioPendingBreakpoint (binaryPath, fixtureLoadPath, moduleSourcePath, moduleLine, exitTimeoutMs),
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
