require 'filesystem'
local uni = require 'ffi.unicode'

local output = fs.path(arg[1])
local input  = fs.path(arg[2])
local jass   = fs.path(arg[3])

local t = os.clock()

local of = io.open(uni.u2a(output:string()), 'wb')
if not of then
    return error(e)
end

local function write(s)
    of:write(s .. '\n')
    print(s)
end

local mapdump = require 'mapdump'
mapdump(input, jass, write)
of:close()

print(os.clock() - t)
