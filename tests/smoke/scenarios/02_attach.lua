-- Scenario 02: Feature 2 — attach mode
--
-- Flow:
--   1. Spawn smoke_fixture_wait externally — long-running target
--   2. DAP attach whatdbg to that pid
--   3. Target suspends on attach (ptrace/AttachToProcessWithID semantics)
--   4. configurationDone → adapter resumes, emits thread(started)
--   5. Disconnect with terminateDebuggee=true → kills fixture_wait

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

return {
    name = "02_attach",

    run = function (context, result)
        local uv = context.uv

        -- 1. Spawn long-running target externally.
        local targetStdout = uv.new_pipe (false)
        local targetStderr = uv.new_pipe (false)
        local targetExited = false

        local targetHandle, targetPid = uv.spawn (context.waitFixtureBinary, {
            stdio = { nil, targetStdout, targetStderr },
        }, function (_code, _signal)
            targetExited = true
        end)

        assertStep (result, targetHandle ~= nil and targetPid ~= nil,
                    "spawned smoke_fixture_wait pid=" .. tostring (targetPid))

        -- Give the target a moment to enter its sleep loop.
        vim.wait (200, function () return false end, 20)

        -- 2. Start whatdbg client.
        local client = context.Client.new (context.whatdbgBinary)

        local ok, err = pcall (function ()
            client:send ("initialize", {
                clientID = "whatdbg-smoke", adapterID = "whatdbg",
                linesStartAt1 = true, columnsStartAt1 = true, pathFormat = "path",
            })
            assertStep (result, client:waitForResponse (1, 5000) ~= nil, "initialize response")
            assertStep (result, client:waitForEvent ("initialized", 5000) ~= nil, "initialized event")

            -- 3. Attach by pid.
            local attachSeq = client:send ("attach", { pid = targetPid })
            local attachResp = client:waitForResponse (attachSeq, 15000)
            assertStep (result, attachResp and attachResp.success == true,
                        "attach response success (pid=" .. tostring (targetPid) .. ")")

            -- 4. configurationDone → adapter resumes, emits thread(started).
            local cfgSeq = client:send ("configurationDone", {})
            assertStep (result,
                        (client:waitForResponse (cfgSeq, 10000) or {}).success == true,
                        "configurationDone response")

            local threadEvt = client:waitForEvent ("thread", 10000)
            assertStep (result, threadEvt ~= nil, "thread event received")
            assertStep (result,
                        threadEvt and threadEvt.body.reason == "started",
                        "thread reason == 'started'")

            -- Thread-list enumeration (Feature 9) is validated by scenario 01
            -- against a stopped target. After resolveAndResumeAfterInitialBreak,
            -- fixture_wait is running — lldb cannot enumerate threads mid-run.
            -- Scenario 02 verifies the attach path only; threads request omitted.

            -- 5. Disconnect with terminate — kills fixture_wait.
            local discSeq = client:send ("disconnect", { terminateDebuggee = true })
            client:waitForResponse (discSeq, 5000)
        end)

        client:shutdown ()

        -- Ensure the target is gone even on failure.
        if not targetExited and targetHandle ~= nil and not targetHandle:is_closing () then
            targetHandle:kill ("sigkill")
        end

        if not ok then error (err, 0) end
    end,
}
