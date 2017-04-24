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

local output = fs.path(arg[1])
local input  = fs.path(arg[2])
local jass   = fs.path(arg[3])

local t = os.clock()
local map = stormlib.open(input, true)
if not map then
    error('Couldn\'t open map.')
    return
end

local of = io.open(output, 'wb')
if not of then
    error(e)
    return
end

local function write(s)
    of:write(s .. '\n')
    print(s)
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
    local buf = io.load(input)
    write('map_size = ' .. tohex(#buf))
    write('map_info = ' .. tohex(crc32(buf)))
end

-- Step.2 hash值计算
do
    local hash = require 'hash'
    local crc, sha1_a, sha1_b, sha1_c, sha1_d, sha1_e = hash(jass, map)
    write('map_crc = ' .. tohex(crc))
    write('map_sha1 = ' .. tohex(sha1_a, true)..' '..tohex(sha1_b, true)..' '..tohex(sha1_c, true)..' '..tohex(sha1_d, true)..' '..tohex(sha1_e, true))
end

-- Step.3 w3i信息
do
    local w3i = require 'w3i'
    local info = w3i(map:load_file 'war3map.w3i')
    local map_options = (info['选项']['对战地图'] << 2)
        | (info['选项']['自定义玩家分组'] << 5)
        | (info['选项']['自定义队伍'] << 6)
    write('map_options = ' .. map_options)
    write('map_width = ' .. tohex(info['地形']['地图宽度']))
    write('map_height = ' .. tohex(info['地形']['地图长度']))
    local n = info['玩家']['玩家数量']
    local closed = 0
    local players = {}
    for i = 1, n do
        local t = info['玩家'..i]
        if t['类型'] ~= 1 and t['类型'] ~= 2 then
            closed = closed + 1
        else
            local ply = {}
            ply.pid = 0
            ply.download_status = 255
            if t['类型'] == 1 then
                ply.slot_status = 0
                ply.computer = 0
            elseif t['类型'] == 2 then
                ply.slot_status = 2
                ply.computer = 1
            else
                ply.slot_status = 1
                ply.computer = 0
            end
            ply.colour = t['玩家']
            if t['种族'] == 1 then
                -- human
                ply.race = 1
            elseif t['种族'] == 2 then
                -- orc
                ply.race = 2
            elseif t['种族'] == 3 then
                -- undead
                ply.race = 8
            elseif t['种族'] == 4 then
                -- nightelf
                ply.race = 4
            else
                -- random
                ply.race = 32
            end
            ply.computer_type = 1
            ply.handicap = 100

            table.insert(players, ply)
        end
    end
    for i = 1, info['队伍']['队伍数量'] do
        for _, c in ipairs(info['队伍'..i]['玩家列表']) do
            for _, ply in ipairs(players) do
                if ply.colour + 1 == c then
                    ply.team = i - 1
                    break
                end
            end
        end
    end
    write('map_numplayers = ' .. (n - closed))
    write('map_numteams = ' .. info['队伍']['队伍数量'])
    for i, ply in ipairs(players) do
        write(('map_slot%d = %d %d %d %d %d %d %d %d %d'):format(i
            , ply.pid
            , ply.download_status
            , ply.slot_status
            , ply.computer
            , ply.team
            , ply.colour
            , ply.race
            , ply.computer_type
            , ply.handicap
            ))
    end
end

print(os.clock() - t)
