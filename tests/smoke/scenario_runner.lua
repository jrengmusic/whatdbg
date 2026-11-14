local DapClient = require ("dap_client")

local ScenarioRunner = {}

function ScenarioRunner.runScenario (report, binaryPath, scenarioBody)
    local client = DapClient.spawn (binaryPath, {})
    local ok, err = pcall (scenarioBody, client)

    if not ok then
        report:fail ("scenario error: " .. tostring (err))
    end

    client:close ()

    local transcript = client:getTranscript ()
    local baselinePath = ScenarioRunner.baselineDirectory .. "/" .. report.name .. ".txt"

    if ScenarioRunner.isRecording then
        local baselineFile = assert (io.open (baselinePath, "w"))
        baselineFile:write (transcript)
        baselineFile:close ()
        report:info ("baseline recorded")
    else
        local baselineFile = io.open (baselinePath, "r")

        if baselineFile ~= nil then
            local baselineText = baselineFile:read ("*a")
            baselineFile:close ()
            report:check ("message sequence equals baseline", transcript == baselineText)

            if transcript ~= baselineText then
                report:info ("expected:\n" .. baselineText)
                report:info ("actual:\n" .. transcript)
            end
        else
            report:fail ("baseline not found: " .. baselinePath)
        end
    end

    return report
end

return ScenarioRunner
