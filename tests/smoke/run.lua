-- whatdbg smoke-test driver
--
-- Run with:
--   nvim --headless -u NONE -c "luafile tests/smoke/run.lua" +qall
--
-- Spawns the whatdbg binary, speaks DAP JSON-RPC over stdio, dispatches each
-- scenario in tests/smoke/scenarios/, writes a report to carol/SMOKE-<ts>.md.

local uv   = vim.uv or vim.loop
local json = vim.json

local repoRoot           = vim.fn.getcwd ()
local whatdbgBinary      = repoRoot .. "/Builds/Ninja/whatdbg_App_artefacts/Debug/whatdbg"
local fixtureBinary      = repoRoot .. "/tests/smoke/build/smoke_fixture"
local waitFixtureBinary  = repoRoot .. "/tests/smoke/build/smoke_fixture_wait"
local crashFixtureBinary = repoRoot .. "/tests/smoke/build/smoke_fixture_crash"
local fixtureSource      = repoRoot .. "/tests/smoke/fixture.cpp"
local scenariosDir       = repoRoot .. "/tests/smoke/scenarios"

-- ---------------------------------------------------------------------------
-- DAP client (framed stdio JSON-RPC)
-- ---------------------------------------------------------------------------

local Client = {}
Client.__index = Client

function Client.new (binaryPath)
    local self = setmetatable ({}, Client)

    self.stdin   = uv.new_pipe (false)
    self.stdout  = uv.new_pipe (false)
    self.stderr  = uv.new_pipe (false)
    self.buffer  = ""
    self.nextSeq = 1
    self.responses = {}
    self.events    = {}
    self.isExited  = false
    self.exitCode  = nil

    self.handle, self.pid = uv.spawn (binaryPath, {
        stdio = { self.stdin, self.stdout, self.stderr },
    }, function (code, _signal)
        self.isExited = true
        self.exitCode = code
    end)

    assert (self.handle, "failed to spawn " .. binaryPath)

    self.stdout:read_start (function (_err, chunk)
        if chunk then
            self.buffer = self.buffer .. chunk
            self:_drain ()
        end
    end)

    self.stderr:read_start (function (_err, chunk)
        if chunk then
            io.stderr:write ("[whatdbg stderr] " .. chunk)
        end
    end)

    return self
end

function Client:_drain ()
    while true do
        local headerEnd = self.buffer:find ("\r\n\r\n", 1, true)
        if not headerEnd then return end

        local header = self.buffer:sub (1, headerEnd - 1)
        local contentLength = tonumber (header:match ("Content%-Length:%s*(%d+)"))
        if not contentLength then return end

        local bodyStart = headerEnd + 4
        if #self.buffer < bodyStart + contentLength - 1 then return end

        local body = self.buffer:sub (bodyStart, bodyStart + contentLength - 1)
        self.buffer = self.buffer:sub (bodyStart + contentLength)

        local ok, message = pcall (json.decode, body)
        if ok then self:_dispatch (message) end
    end
end

function Client:_dispatch (message)
    io.stdout:write (string.format ("[smoke recv] type=%s command=%s event=%s request_seq=%s\n",
                                    tostring (message.type),
                                    tostring (message.command),
                                    tostring (message.event),
                                    tostring (message.request_seq)))
    if message.type == "response" then
        self.responses[message.request_seq] = message
    elseif message.type == "event" then
        table.insert (self.events, message)
    end
end

function Client:send (command, arguments)
    local seq = self.nextSeq
    self.nextSeq = seq + 1

    local message = {
        seq       = seq,
        type      = "request",
        command   = command,
        arguments = arguments or vim.empty_dict (),
    }

    local body   = json.encode (message)
    local header = string.format ("Content-Length: %d\r\n\r\n", #body)
    self.stdin:write (header .. body)

    return seq
end

function Client:waitForResponse (seq, timeoutMs)
    local ok = vim.wait (timeoutMs or 5000, function ()
        return self.responses[seq] ~= nil or self.isExited
    end, 20)
    return ok and self.responses[seq] or nil
end

function Client:waitForEvent (eventName, timeoutMs)
    local found
    local foundIndex

    local ok = vim.wait (timeoutMs or 5000, function ()
        for i = 1, #self.events do
            if self.events[i].event == eventName then
                found = self.events[i]
                foundIndex = i
                return true
            end
        end
        return self.isExited
    end, 20)

    if ok and found then
        table.remove (self.events, foundIndex)
        return found
    end
    return nil
end

function Client:consumeEvent (eventName)
    for i = 1, #self.events do
        if self.events[i].event == eventName then
            local event = self.events[i]
            table.remove (self.events, i)
            return event
        end
    end
    return nil
end

function Client:shutdown ()
    if not self.isExited then
        self.stdin:close ()
        vim.wait (2000, function () return self.isExited end, 20)
    end

    if self.handle and not self.handle:is_closing () then
        self.handle:close ()
    end
end

-- ---------------------------------------------------------------------------
-- Scenario runner + report writer
-- ---------------------------------------------------------------------------

local function utcTimestamp ()
    return os.date ("!%Y%m%dT%H%M%SZ")
end

local function preflight ()
    local errors = {}

    if vim.fn.filereadable (whatdbgBinary) == 0 then
        table.insert (errors, "whatdbg binary missing: " .. whatdbgBinary)
    end

    if vim.fn.filereadable (fixtureBinary) == 0 then
        table.insert (errors, "smoke_fixture binary missing: " .. fixtureBinary)
    end

    if vim.fn.filereadable (waitFixtureBinary) == 0 then
        table.insert (errors, "smoke_fixture_wait binary missing: " .. waitFixtureBinary)
    end

    if vim.fn.filereadable (crashFixtureBinary) == 0 then
        table.insert (errors, "smoke_fixture_crash binary missing: " .. crashFixtureBinary)
    end

    return errors
end

local function discoverScenarios ()
    local entries = vim.fn.readdir (scenariosDir)
    table.sort (entries)

    local scenarios = {}
    for _, name in ipairs (entries) do
        if name:match ("%.lua$") then
            table.insert (scenarios, scenariosDir .. "/" .. name)
        end
    end
    return scenarios
end

local function runScenario (path, context)
    local chunk, loadErr = loadfile (path)
    if not chunk then
        return { name = path, status = "load_error", message = loadErr, steps = {} }
    end

    local scenario = chunk ()
    local result = {
        name    = scenario.name or path,
        status  = "pass",
        message = "",
        steps   = {},
    }

    local ok, err = pcall (scenario.run, context, result)
    if not ok then
        result.status  = "fail"
        result.message = tostring (err)
    end

    return result
end

local function writeReport (results, reportPath)
    local lines = {
        "# whatdbg smoke report",
        "",
        "**Timestamp:** " .. utcTimestamp (),
        "**Binary:**    " .. whatdbgBinary,
        "**Fixture:**   " .. fixtureBinary,
        "",
        "## Results",
        "",
    }

    for _, r in ipairs (results) do
        table.insert (lines, string.format ("### %s — **%s**", r.name, r.status:upper ()))
        if r.message ~= "" then
            table.insert (lines, "")
            table.insert (lines, "```")
            table.insert (lines, r.message)
            table.insert (lines, "```")
        end
        if #r.steps > 0 then
            table.insert (lines, "")
            for _, step in ipairs (r.steps) do
                table.insert (lines, string.format ("- [%s] %s", step.status, step.detail))
            end
        end
        table.insert (lines, "")
    end

    vim.fn.mkdir (vim.fn.fnamemodify (reportPath, ":h"), "p")
    local file = io.open (reportPath, "w")
    assert (file, "cannot write " .. reportPath)
    file:write (table.concat (lines, "\n"))
    file:close ()
end

-- ---------------------------------------------------------------------------
-- Main
-- ---------------------------------------------------------------------------

local function main ()
    local errors = preflight ()
    if #errors > 0 then
        io.stderr:write ("PREFLIGHT FAILURE:\n")
        for _, e in ipairs (errors) do io.stderr:write ("  " .. e .. "\n") end
        vim.cmd ("cq")
        return
    end

    local context = {
        whatdbgBinary      = whatdbgBinary,
        fixtureBinary      = fixtureBinary,
        waitFixtureBinary  = waitFixtureBinary,
        crashFixtureBinary = crashFixtureBinary,
        fixtureSource      = fixtureSource,
        Client             = Client,
        uv                 = uv,
    }

    local results = {}
    for _, scenarioPath in ipairs (discoverScenarios ()) do
        local name = vim.fn.fnamemodify (scenarioPath, ":t:r")
        io.stdout:write (string.format ("[smoke] running %s\n", name))
        local r = runScenario (scenarioPath, context)
        table.insert (results, r)
        io.stdout:write (string.format ("[smoke]   -> %s\n", r.status:upper ()))
    end

    local reportPath = repoRoot .. "/carol/SMOKE-" .. utcTimestamp () .. ".md"
    writeReport (results, reportPath)
    io.stdout:write ("[smoke] report: " .. reportPath .. "\n")

    local hasFail = false
    for _, r in ipairs (results) do
        if r.status ~= "pass" then hasFail = true end
    end

    if hasFail then vim.cmd ("cq") end
end

main ()
