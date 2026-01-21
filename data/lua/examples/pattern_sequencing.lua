-- Pattern Sequencing Example
-- Demonstrates how to control patterns via Lua

local patternRuntime = engine:getPatternRuntime()
local clock = engine:getClock()

-- Start transport
clock:setBPM(120)
clock:play()

-- Pattern control would go here
-- (PatternRuntime API to be exposed via SWIG bindings)

print("Pattern sequencing example")
print("BPM set to 120, transport started")

