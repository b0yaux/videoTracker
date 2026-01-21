-- Basic Clock Control Example
-- Demonstrates how to control the transport and BPM from Lua

local clock = engine:getClock()

-- Set BPM
clock:setBPM(140)

-- Start transport
clock:play()

-- Wait a bit (in a real scenario, you'd use callbacks or timers)
-- For now, this is just a demonstration

print("Clock set to 140 BPM and started")

