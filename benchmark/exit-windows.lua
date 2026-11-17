local programPath = arg[1]
local programRoot = arg[2]
local limitMs = 120000
local settleMs = 5000

vim.opt.rtp:append (vim.fn.stdpath ("data") .. "/lazy/nvim-dap")
local dap = require ("dap")
dap.adapters.whatdbg = { type = "executable", command = vim.fn.exepath ("whatdbg") }

local processId = nil
local isTerminated = false

dap.listeners.after.event_process["exit-windows"] = function (_, body) processId = body.systemProcessId end
dap.listeners.after.event_terminated["exit-windows"] = function () isTerminated = true end

dap.run ({ type = "whatdbg", request = "launch", name = "exit-windows", program = programPath, cwd = programRoot, console = "integratedTerminal", stopOnEntry = false })

assert (vim.wait (limitMs, function () return processId ~= nil end, 50), "no process event")
vim.wait (settleMs)

local closeResult = vim.system ({ "taskkill", "/PID", tostring (processId) }):wait ()
assert (closeResult.code == 0, "taskkill failed: " .. tostring (closeResult.stderr))

assert (vim.wait (limitMs, function () return isTerminated end, 50), "no terminated event")
assert (vim.wait (limitMs, function () return dap.session () == nil end, 50), "session did not close")
assert (vim.wait (limitMs, function ()
    return not vim.system ({ "tasklist", "/FI", "PID eq " .. tostring (processId), "/NH" }):wait ().stdout:find (tostring (processId), 1, true)
end), "target did not exit")

vim.cmd ("qa!")
