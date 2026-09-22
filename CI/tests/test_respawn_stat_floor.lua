-- Run from the repository root with Lua 5.2+ or LuaJIT.
local root = "files/tes3mp/core-scripts/scripts/"
config = { maxAttributeValue = 100, respawnAttributeFloor = 10 }
package.loaded.config = config
package.loaded.patterns = {}
package.loaded.stateHelper = {}
package.loaded.tableHelper = {}
class = function() return {} end

local attributes = {
    { base = 1, damage = 0 },
    { base = 50, damage = 50 },
    { base = 70, damage = 5 },
}
local sends = 0
tes3mp = {
    GetAttributeCount = function() return #attributes end,
    GetAttributeName = function(i) return tostring(i) end,
    GetAttributeBase = function(_, i) return attributes[i+1].base end,
    GetAttributeDamage = function(_, i) return attributes[i+1].damage end,
    SetAttributeBase = function(_, i, value) attributes[i+1].base = value end,
    SetAttributeDamage = function(_, i, value) attributes[i+1].damage = value end,
    SendAttributes = function() sends = sends + 1 end,
}
local BasePlayer = dofile(root .. "player/base.lua")
local player = { pid = 0, data = { attributes = { ["0"] = { skillIncrease = 7 } } } }
BasePlayer.RestoreRespawnAttributes(player)
assert(attributes[1].base == 10 and attributes[1].damage == 0)
assert(attributes[2].base == 50 and attributes[2].damage == 40)
assert(attributes[3].base == 70 and attributes[3].damage == 5)
assert(player.data.attributes["0"].skillIncrease == 7)
assert(player.data.attributes["1"].damage == 40 and sends == 1)
for _ = 1, 100 do
    attributes[1].damage = 100
    BasePlayer.RestoreRespawnAttributes(player)
    assert(attributes[1].base - attributes[1].damage == 10)
end
config.respawnAttributeFloor = 20
BasePlayer.RestoreRespawnAttributes(player)
assert(attributes[1].base == 20 and attributes[1].damage == 0)

-- Exercise the actual asynchronous jail callback and its registered skill handler.
local skills = {
    athletics = { base = 50 }, security = { base = 50 }, sneak = { base = 50 },
    acrobatics = { base = 1 }, restoration = { base = 0 },
}
local stats = {}
for id, value in pairs(skills) do stats[id] = function() return value end end
local progression, handler, module = {}, nil, nil
progression.SKILL_INCREASE_SOURCES = { Jail = 'jail' }
progression.addSkillUsedHandler = function() end
progression.addSkillLevelUpHandler = function(fn) handler = fn end
progression.skillLevelUp = function(id, source)
    handler(id, source, module.interface.getSkillLevelUpOptions(id, source))
end
package.loaded['openmw.ambient'] = {}
package.loaded['openmw.core'] = {
    stats = { Skill = { record = function(id) return { name = id } end } },
    l10n = function() return function(message) return message end end,
    getSimulationTime = function() return 1 end,
    getGMST = function() return 1 end,
}
package.loaded['openmw.interfaces'] = {
    SkillProgression = progression, UI = { showInteractiveMessage = function() end },
}
package.loaded['openmw.self'] = {}
package.loaded['openmw.types'] = {
    NPC = {
        stats = { skills = stats }, record = function() return { class = 'test' } end,
        classes = { record = function() return { minorSkills = {}, majorSkills = {} } end },
    },
    Actor = { stats = { level = function() return {} end } },
}
package.loaded['openmw.ui'] = {}
package.loaded['openmw_aux.util'] = { shallowCopy = function(value)
    local copy = {}; for k,v in pairs(value) do copy[k] = v end; return copy
end }
module = dofile('files/data-mw/scripts/omw/playerskillhandlers.lua')
module.engineHandlers._onJailTimeServed(1000, true)
for id, value in pairs(skills) do
    assert(value.base == (id == 'restoration' and 0 or 1), id)
end
module.engineHandlers._onJailTimeServed(1000, true)
assert(skills.athletics.base == 1 and skills.security.base == 1)
-- The death-recovery floor must not change ordinary prison penalties.
handler('athletics', 'jail', { skillIncreaseValue = -1 })
assert(skills.athletics.base == 0)
print('Respawn attribute and repeated recovery skill-floor tests passed')
