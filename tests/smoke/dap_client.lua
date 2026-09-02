local DapClient = {}
DapClient.__index = DapClient

local defaultTimeoutMs = 8000
local closeGracefulTimeoutMs = 3000

local function encodeFrame (body)
    local content = vim.json.encode (body)
    return "Content-Length: " .. tostring (#content) .. "\r\n\r\n" .. content
end

local function extractFrame (buffer)
    local headerEnd = buffer:find ("\r\n\r\n", 1, true)

    if headerEnd == nil then
        return nil, buffer
    end

    local header = buffer:sub (1, headerEnd - 1)
    local length = tonumber (header:match ("Content%-Length:%s*(%d+)"))
    assert (length ~= nil, "malformed DAP header: " .. header)

    local bodyStart = headerEnd + 4
    local bodyEnd = bodyStart + length - 1

    if #buffer < bodyEnd then
        return nil, buffer
    end

    local body = buffer:sub (bodyStart, bodyEnd)
    local remaining = buffer:sub (bodyEnd + 1)
    return vim.json.decode (body), remaining
end

function DapClient.spawn (binaryPath, extraArguments)
    local self = setmetatable ({}, DapClient)
    self.buffer = ""
    self.messages = {}
    self.log = {}
    self.nextSeq = 1

    local command = { binaryPath }

    for argumentIndex, argument in ipairs (extraArguments or {}) do
        table.insert (command, argument)
    end

    self.process = vim.system (command, {
        stdin = true,
        stdout = function (readError, data)
            if data ~= nil then
                self.buffer = self.buffer .. data
                self:drainMessages ()
            end
        end,
        stderr = function () end,
    })

    return self
end

function DapClient:drainMessages ()
    while true do
        local message, remaining = extractFrame (self.buffer)

        if message == nil then
            return
        end

        self.buffer = remaining
        table.insert (self.messages, message)
        table.insert (self.log, message)
    end
end

function DapClient:send (command, arguments)
    local seq = self.nextSeq
    self.nextSeq = self.nextSeq + 1

    local requestBody = {
        seq = seq,
        type = "request",
        command = command,
        arguments = arguments or vim.empty_dict (),
    }

    self.process:write (encodeFrame (requestBody))
    return seq
end

function DapClient:waitFor (predicate, timeoutMs)
    local deadline = timeoutMs or defaultTimeoutMs
    local found = nil

    vim.wait (deadline, function ()
        for index, message in ipairs (self.messages) do
            if predicate (message) then
                found = message
                table.remove (self.messages, index)
                return true
            end
        end

        return false
    end, 20)

    return found
end

function DapClient:waitForResponse (command, timeoutMs)
    return self:waitFor (function (message)
        return message.type == "response" and message.command == command
    end, timeoutMs)
end

function DapClient:waitForEvent (eventName, timeoutMs)
    return self:waitFor (function (message)
        return message.type == "event" and message.event == eventName
    end, timeoutMs)
end

function DapClient:close ()
    if self.process ~= nil then
        pcall (function () self.process:write (nil) end)
        pcall (function () self.process:wait (closeGracefulTimeoutMs) end)
    end
end

return DapClient
