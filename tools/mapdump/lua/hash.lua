local sha1 = require 'sha1'
local rolc = require 'rolc'

local hash = {}
function hash:init(map)
    self.map = map
    self.rolc = rolc()
    self.sha1 = sha1()
end
function hash:update_magic()
    local function rol(n, r)
	    return ((n << r) | (n >> (32 - r))) & 0xFFFFFFFF
    end
    self.rolc.h = rol(self.rolc.h ~ 0x03F1379E, 3)
    self.sha1:update('\x9E\x37\xF1\x03')
end
function hash:update(str)
    self.rolc:update(str)
    self.sha1:update(str)
end
function hash:update_file(filepath)
    local str = io.load(filepath)
    if not str then
        return false
    end
    self:update(str)
    return true
end
function hash:update_mpq(filename)
    local str = self.map:load_file(filename)
    if not str then
        return false
    end
    self:update(str)
    return true
end
function hash:final()
    return self.rolc:final(), self.sha1:final()
end

return function(path, map)
    hash:init(map)
    if not (hash:update_mpq 'common.j' or hash:update_mpq 'scripts\\common.j') then
        assert(hash:update_file(path / 'common.j'))
    end
    if not (hash:update_mpq 'blizzard.j' or hash:update_mpq 'scripts\\blizzard.j') then
        assert(hash:update_file(path / 'blizzard.j'))
    end
    hash:update_magic()
    if not hash:update_mpq 'war3map.j' then
        hash:update_mpq 'scripts\\war3map.j'
    end
    hash:update_mpq 'war3map.w3e'
    hash:update_mpq 'war3map.wpm'
    hash:update_mpq 'war3map.doo'
    hash:update_mpq 'war3map.w3u'
    hash:update_mpq 'war3map.w3b'
    hash:update_mpq 'war3map.w3d'
    hash:update_mpq 'war3map.w3a'
    hash:update_mpq 'war3map.w3q'
    return hash:final()
end
