local ScenarioRunner = {}

function ScenarioRunner.runScenario (report, scenarioBody)
    local ok, err = pcall (scenarioBody)

    if not ok then
        report:fail ("scenario error: " .. tostring (err))
    end

    return report
end

return ScenarioRunner
