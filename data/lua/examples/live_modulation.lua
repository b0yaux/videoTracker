-- Live Modulation Example
-- Demonstrates real-time parameter modulation from Lua

local clock = engine:getClock()
local registry = engine:getModuleRegistry()

-- This would typically run in a loop or callback
-- For demonstration, we'll set some parameters

local moduleNames = registry:getAllModuleNames()
if #moduleNames > 0 then
    local module = registry:getModule(moduleNames[1])
    if module then
        -- Example: modulate a parameter
        -- module:setParameter("volume", 0.8)
        print("Live modulation example")
        print("Module: " .. module:getName())
    end
end

-- In a real live-coding scenario, you'd use:
-- - Clock callbacks for timed modulation
-- - Loops for continuous modulation
-- - Math functions for LFOs, envelopes, etc.

print("Live modulation ready")

