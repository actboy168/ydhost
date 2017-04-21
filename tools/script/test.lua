require 'filesystem'
local uni = require 'ffi.unicode'
local stormlib = require 'ffi.stormlib'

local io_open = io.open
function io.open(path, ...)
    return io_open(uni.u2a(path:string()), ...)
end
function io.load(filepath)
	local f, e = io.open(filepath, 'rb')
	if f then
		local content = f:read('*a')
		f:close()
		return content
	else
		return nil, e
	end
end

local map_path = fs.path(arg[1])

local map = stormlib.open(map_path, true)
if not map then
    return
end

local function tohex(n, reverse)
    if reverse then
	    local s = tostring(n & 255)
	    n = n // 256
	    s = tostring(n & 255) .. ' ' .. s
	    n = n // 256
	    s = tostring(n & 255) .. ' ' .. s
	    n = n // 256
	    return tostring(n & 255) .. ' ' .. s
    end
	local s = tostring(n & 255)
	n = n // 256
	s = s .. ' ' .. tostring(n & 255)
	n = n // 256
	s = s .. ' ' .. tostring(n & 255)
	n = n // 256
	return s .. ' ' .. tostring(n & 255)
end

-- Step.1 基本信息
do 
    local crc32 = require 'crc32'
    local buf = io.load(map_path)
    print('map_size = ' .. tohex(#buf))
    print('map_info = ' .. tohex(crc32(buf)))
end

-- Step.2 hash值计算
do
    local path = fs.current_path():remove_filename()
    local hash = require 'hash'
    local crc, sha1_a, sha1_b, sha1_c, sha1_d, sha1_e = hash(path, map)
    print('map_crc = ' .. tohex(crc))
    print('map_sha1 = ' .. tohex(sha1_a, true)..' '..tohex(sha1_b, true)..' '..tohex(sha1_c, true)..' '..tohex(sha1_d, true)..' '..tohex(sha1_e, true))
end

-- Step.3 w3i信息
do
    local w3i = require 'w3i'
    local info = w3i(map:load_file 'war3map.w3i')
    local map_options = (info['选项']['对战地图'] << 2)
        | (info['选项']['自定义玩家分组'] << 5)
        | (info['选项']['自定义队伍'] << 6)
    print('map_options = ' .. map_options)
    print('map_width = ' .. tohex(info['地形']['地图宽度']))
    print('map_height = ' .. tohex(info['地形']['地图长度']))
    local n = info['玩家']['玩家数量']
    local closed = 0
    for i = 1, n do
        if info['玩家'..i]['类型'] ~= 1 and info['玩家'..i]['类型'] ~= 2 then
            closed = closed + 1
        end
    end
    print('map_numplayers = ' .. (n - closed))
    print('map_numteams = ' .. info['队伍']['队伍数量'])
end
