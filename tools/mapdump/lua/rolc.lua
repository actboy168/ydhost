local strunpack = string.unpack
local setmetatable = setmetatable

local function rol(n, r)
	return ((n << r) | (n >> (32 - r))) & 0xFFFFFFFF
end

local function update(str)
    local h = 0
	for n = 1, #str - 3, 4 do
		h = rol(h ~ strunpack('<I4', str, n), 3)
    end
	for n = #str // 4 * 4 + 1, #str do
		h = rol(h ~ strunpack('<I1', str, n), 3)
    end
    return h
end

local mt = {}
mt.__index = mt

function mt:init()
end

function mt:update(str)
    if not self.h then
        self.h = update(str)
        return
    end
	self.h = rol(self.h ~ update(str), 3)
end

function mt:final()
	return self.h or 0
end

return function()
    local o = setmetatable({}, mt)
    o:init()
    return o
end
