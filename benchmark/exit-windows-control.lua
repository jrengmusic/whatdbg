local programPath = arg[1]
local programRoot = arg[2]
local limitMs = 120000
local settleMs = 5000

local target = vim.system ({ programPath }, { cwd = programRoot })
local processId = target.pid
vim.wait (settleMs)

local closeResult = vim.system ({ "taskkill", "/PID", tostring (processId) }):wait ()
assert (closeResult.code == 0, "taskkill failed: " .. tostring (closeResult.stderr))

target:wait (limitMs)

vim.cmd ("qa!")
