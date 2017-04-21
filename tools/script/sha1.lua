local strchar = string.char
local strunpack = string.unpack
local setmetatable = setmetatable

local function rol(n, r)
	return (n << r) | ((n & 0xFFFFFFFF) >> (32 - r))
end

local function fill(str, len)
    local s = ''
	len = len * 8
	for i = 1, 8 do
		s = strchar(len & 255) .. s
		len = len // 256
	end
	local last = 56 - ((#str + 1) & 63)
	if last < 0 then
		last = last + 64
	end
	for i = 1, last do
		s = strchar(0) .. s
	end
	s = strchar(128) .. s
	return str .. s
end

local function update(self, str, pos)
    local h0, h1, h2, h3, h4 = self.h0, self.h1, self.h2, self.h3, self.h4
	local a, b, c, d, e, f, k, t
	local w = {}
	for n = pos, #str - 63, 64 do
		w[0x01], w[0x02], w[0x03], w[0x04], w[0x05], w[0x06], w[0x07], w[0x08],
		w[0x09], w[0x0A], w[0x0B], w[0x0C], w[0x0D], w[0x0E], w[0x0F], w[0x10]
		= strunpack('>I4I4I4I4I4I4I4I4I4I4I4I4I4I4I4I4', str, n)
		for i = 17, 80 do
			w[i] = rol(((w[i - 3] ~ w[i - 8]) ~ (w[i - 14] ~ w[i - 16])), 1)
		end
		a = h0
		b = h1
		c = h2
		d = h3
		e = h4
		for i = 1, 80 do
			if i <= 20 then
				f = (b & c) | ((~b) & d)
				k = 1518500249
			elseif i <= 40 then
				f = ((b ~ c) ~ d)
				k = 1859775393
			elseif i <= 60 then
				f = (b & c) | (b & d) | (c & d)
				k = 2400959708
			else
				f = (b ~ c) ~ d
				k = 3395469782
			end
			t = rol(a, 5) + f + e + k + w[i]	
			e = d
			d = c
			c = rol(b, 30)
			b = a
			a = t
		end
		h0 = (h0 + a) & 0xFFFFFFFF
		h1 = (h1 + b) & 0xFFFFFFFF
		h2 = (h2 + c) & 0xFFFFFFFF
		h3 = (h3 + d) & 0xFFFFFFFF
		h4 = (h4 + e) & 0xFFFFFFFF
	end
    self.h0, self.h1, self.h2, self.h3, self.h4 = h0, h1, h2, h3, h4
end

local mt = {}
mt.__index = mt

function mt:init()
	self.h0 = 1732584193
	self.h1 = 4023233417
	self.h2 = 2562383102
	self.h3 = 0271733878
	self.h4 = 3285377520
    self.len = 0
    self.str = ''
end

function mt:update(str)
    self.len = self.len + #str
    if #self.str + #str < 64 then
        self.str = self.str .. str
        return
    end
    if #self.str > 0 then
        update(self, self.str .. str:sub(1, 64 - #self.str), 1)
        update(self, str, 65 - #self.str)
        self.str = str:sub(#str - (#self.str + #str) % 64 + 1)
    else
        update(self, str, 1)
        self.str = str:sub(#str // 64 * 64 + 1)
    end
end

function mt:final()
    update(self, fill(self.str, self.len), 1)
    return self.h0, self.h1, self.h2, self.h3, self.h4
end

return function()
    local o = setmetatable({}, mt)
    o:init()
    return o
end
