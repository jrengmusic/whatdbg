local Report = {}
Report.__index = Report

function Report.new (name)
    return setmetatable ({ name = name, bullets = {}, failed = false }, Report)
end

function Report:pass (description)
    table.insert (self.bullets, "- [pass] " .. description)
end

function Report:info (description)
    table.insert (self.bullets, "- [info] " .. description)
end

function Report:fail (description)
    table.insert (self.bullets, "- [fail] " .. description)
    self.failed = true
end

function Report:check (description, condition)
    if condition then
        self:pass (description)
    else
        self:fail (description)
    end
end

function Report:print ()
    print ("### " .. self.name .. " -- " .. (self.failed and "FAIL" or "PASS"))
    print ("")

    for bulletIndex, bullet in ipairs (self.bullets) do
        print (bullet)
    end

    print ("")
end

return Report
