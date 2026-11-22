local whatdbgBinary = arg[1]
local fixturePath = arg[2]
local fixtureWaitPath = arg[3]
local fixtureSourcePath = arg[4]

assert (whatdbgBinary ~= nil and fixturePath ~= nil and fixtureWaitPath ~= nil and fixtureSourcePath ~= nil,
        "usage: run_mcp.lua <whatdbg> <fixture> <fixture_wait> <fixture.cpp>")

local scriptDirectory = vim.fn.fnamemodify (arg[0], ":h")
local baselinePath = scriptDirectory .. "/baseline/" .. (vim.fn.has ("win32") == 1 and "windows.txt" or "macos.txt")

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

local breakpointLine = findBreakpointLine (fixtureSourcePath, "SMOKE_BREAKPOINT_LINE")

local buffer = ""
local lines = {}

local handle = vim.system ({ whatdbgBinary, "--mcp" }, {
    stdin = true,
    text = true,
    stdout = function (_, data)
        if data ~= nil then
            buffer = buffer .. data

            while true do
                local newlineIndex = buffer:find ("\n", 1, true)

                if newlineIndex == nil then
                    break
                end

                local line = buffer:sub (1, newlineIndex - 1):gsub ("\r$", "")
                buffer = buffer:sub (newlineIndex + 1)

                if #line > 0 then
                    lines[#lines + 1] = line
                end
            end
        end
    end,
})

local serverPid = handle.pid

local function readResponse (timeoutMs)
    local ready = vim.wait (timeoutMs, function ()
        return #lines > 0
    end, 10)

    assert (ready and #lines > 0, "timed out waiting for response")
    return vim.json.decode (table.remove (lines, 1))
end

local nextId = 0

local function sendRequest (method, params)
    nextId = nextId + 1

    local payload = vim.json.encode ({
        jsonrpc = "2.0",
        id = nextId,
        method = method,
        params = params,
    })

    handle:write (payload .. "\n")
    return readResponse (60000)
end

local meta = {
    ["io.modelcontextprotocol/protocolVersion"] = "2026-07-28",
    ["io.modelcontextprotocol/clientCapabilities"] = vim.empty_dict (),
}

local function callTool (name, arguments)
    local response = sendRequest ("tools/call", {
        _meta = meta,
        name = name,
        arguments = arguments,
    })

    assert (response.result ~= nil, "missing result for tool: " .. name)
    return response.result
end

local function waitForEvents (session, expectedEvents, timeoutMs)
    local collected = {}
    local seen = {}
    local deadline = vim.uv.now () + timeoutMs

    local function allSeen ()
        for _, name in ipairs (expectedEvents) do
            if not seen[name] then
                return false
            end
        end

        return true
    end

    while not allSeen () do
        local remaining = deadline - vim.uv.now ()

        if remaining <= 0 then
            break
        end

        local result = callTool ("events", {
            session = session,
            timeoutMs = math.min (remaining, timeoutMs),
        })

        if result.isError == true then
            break
        end

        local batch = result.structuredContent

        if type (batch) == "table" then
            for _, item in ipairs (batch) do
                collected[#collected + 1] = item

                if type (item) == "table" and type (item.event) == "string" then
                    seen[item.event] = true
                end
            end
        end
    end

    return collected
end

local function isUnknownSessionError (result)
    return result.isError == true
        and type (result.content) == "table"
        and type (result.content[1]) == "table"
        and type (result.content[1].text) == "string"
        and result.content[1].text:find ("unknown session", 1, true) ~= nil
end

local function waitForUnknownSession (session, timeoutMs)
    local deadline = vim.uv.now () + timeoutMs
    local result = callTool ("threads", { session = session })

    while not isUnknownSessionError (result) and vim.uv.now () < deadline do
        vim.wait (50, function ()
            return false
        end)

        result = callTool ("threads", { session = session })
    end

    return result
end

local transcript = {}

local launchResult = callTool ("launch", { program = fixturePath })
assert (launchResult.isError ~= true, "launch returned error")
local session1 = launchResult.structuredContent ~= nil and launchResult.structuredContent.session or nil
assert (type (session1) == "string" and #session1 > 0, "launch did not return session handle")
transcript[#transcript + 1] = "launch ok"

local breakpointsResult = callTool ("setBreakpoints", {
    session = session1,
    source = { path = fixtureSourcePath },
    breakpoints = { { line = breakpointLine } },
})
assert (breakpointsResult.isError ~= true, "setBreakpoints returned error")
assert (type (breakpointsResult.structuredContent) == "table"
        and type (breakpointsResult.structuredContent.breakpoints) == "table"
        and type (breakpointsResult.structuredContent.breakpoints[1]) == "table"
        and breakpointsResult.structuredContent.breakpoints[1].verified == true,
        "breakpoint was not verified")
transcript[#transcript + 1] = "setBreakpoints ok"

local configDoneResult = callTool ("configurationDone", { session = session1 })
assert (configDoneResult.isError ~= true, "configurationDone returned error")
transcript[#transcript + 1] = "configurationDone ok"

local stopEvents = waitForEvents (session1, { "process", "thread", "stopped" }, 10000)
local processIndex = nil
local threadIndex = nil
local stoppedIndex = nil

for index, item in ipairs (stopEvents) do
    if processIndex == nil and item.event == "process" then
        processIndex = index
    elseif processIndex ~= nil and threadIndex == nil and item.event == "thread" then
        threadIndex = index
    elseif threadIndex ~= nil and stoppedIndex == nil and item.event == "stopped"
        and type (item.body) == "table" and item.body.reason == "breakpoint" then
        stoppedIndex = index
    end
end

assert (processIndex ~= nil and threadIndex ~= nil and stoppedIndex ~= nil,
        "expected process, thread, stopped(breakpoint) events in order")
transcript[#transcript + 1] = "events ok process,thread,stopped(breakpoint)"

local stackTraceResult = callTool ("stackTrace", { session = session1 })
assert (stackTraceResult.isError ~= true, "stackTrace returned error")
assert (type (stackTraceResult.structuredContent) == "table"
        and type (stackTraceResult.structuredContent.stackFrames) == "table"
        and #stackTraceResult.structuredContent.stackFrames >= 1,
        "stackTrace did not return stackFrames")
transcript[#transcript + 1] = "stackTrace ok"

local continueResult = callTool ("continue", { session = session1 })
assert (continueResult.isError ~= true, "continue returned error")
transcript[#transcript + 1] = "continue ok"

local exitEvents = waitForEvents (session1, { "exited", "terminated" }, 10000)
local exitedIndex = nil
local terminatedIndex = nil

for index, item in ipairs (exitEvents) do
    if exitedIndex == nil and item.event == "exited" then
        exitedIndex = index
    elseif exitedIndex ~= nil and terminatedIndex == nil and item.event == "terminated" then
        terminatedIndex = index
    end
end

assert (exitedIndex ~= nil and terminatedIndex ~= nil,
        "expected exited then terminated events in order")
transcript[#transcript + 1] = "events ok exited,terminated"

local threadsAfterExit = waitForUnknownSession (session1, 2000)
assert (isUnknownSessionError (threadsAfterExit), "expected unknown session error after exit")
transcript[#transcript + 1] = "threads error unknown session"

local launchWaitResult = callTool ("launch", { program = fixtureWaitPath })
assert (launchWaitResult.isError ~= true, "launch fixture_wait returned error")
local session2 = launchWaitResult.structuredContent ~= nil and launchWaitResult.structuredContent.session or nil
assert (type (session2) == "string" and #session2 > 0, "launch fixture_wait did not return session handle")

local terminateResult = callTool ("terminate", { session = session2 })
assert (terminateResult.isError ~= true, "terminate returned error")
waitForEvents (session2, { "terminated" }, 5000)

local threadsAfterTerminate = waitForUnknownSession (session2, 2000)
assert (isUnknownSessionError (threadsAfterTerminate), "expected unknown session error after terminate")
transcript[#transcript + 1] = "terminate ok"

local launchShutdownResult = callTool ("launch", { program = fixtureWaitPath })
assert (launchShutdownResult.isError ~= true, "launch for shutdown returned error")
local session3 = launchShutdownResult.structuredContent ~= nil and launchShutdownResult.structuredContent.session or nil
assert (type (session3) == "string" and #session3 > 0, "launch for shutdown did not return session handle")

handle:write (nil)
local exited = handle:wait (10000)
assert (exited.code == 0, "server exited with non-zero code: " .. tostring (exited.code))

if vim.fn.has ("win32") == 0 then
    local noChildrenRemain = vim.wait (2000, function ()
        local pgrepChild = vim.system ({ "pgrep", "-P", tostring (serverPid) }, { text = true }):wait (2000)
        local pgrepWhatdbg = vim.system ({ "pgrep", "-x", "whatdbg" }, { text = true }):wait (2000)
        return pgrepChild.code ~= 0 and pgrepWhatdbg.code ~= 0
    end, 50)

    assert (noChildrenRemain, "whatdbg child process remained after server exit")
end

transcript[#transcript + 1] = "exit " .. tostring (exited.code)

local baselineFile = assert (io.open (baselinePath, "r"))
local expected = {}

for line in baselineFile:lines () do
    expected[#expected + 1] = line:gsub ("\r$", "")
end

baselineFile:close ()

local matched = (#expected == #transcript)

if matched then
    for index = 1, #expected do
        if expected[index] ~= transcript[index] then
            matched = false
            break
        end
    end
end

if matched then
    print ("PASS")
    os.exit (0)
else
    print ("FAIL: transcript mismatch against " .. baselinePath)

    local maxLines = math.max (#expected, #transcript)

    for index = 1, maxLines do
        local expectedLine = expected[index] or "<missing>"
        local actualLine = transcript[index] or "<missing>"

        if expectedLine ~= actualLine then
            print ("line " .. index .. ":")
            print ("  expected: " .. expectedLine)
            print ("  actual:   " .. actualLine)
        end
    end

    os.exit (1)
end
