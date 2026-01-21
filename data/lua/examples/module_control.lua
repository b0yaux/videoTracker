-- Module Control Example
-- Demonstrates how to access and control modules from Lua

local registry = engine:getModuleRegistry()
local moduleNames = registry:getAllModuleNames()

print("Available modules:")
for i, name in ipairs(moduleNames) do
    print("  " .. i .. ": " .. name)
end

-- Get a specific module (example: first module)
if #moduleNames > 0 then
    local module = registry:getModule(moduleNames[1])
    if module then
        print("\nModule: " .. module:getName())
        print("Instance: " .. module:getInstanceName())
        
        -- Get parameters
        local params = module:getParameters()
        print("Parameters:")
        for i, param in ipairs(params) do
            print("  " .. param.name .. " = " .. module:getParameter(param.name))
        end
    end
end

