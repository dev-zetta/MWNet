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
local names = {}
local sends, dynamicSends = 0, 0
local fatigueBase, fatigueCurrent = 200, 200
tes3mp = {
    GetAttributeCount = function() return #attributes end,
    GetAttributeName = function(i) return names[i+1] or tostring(i) end,
    GetAttributeModifier = function(_, i) return attributes[i+1].modifier or 0 end,
    GetAttributeBase = function(_, i) return attributes[i+1].base end,
    GetAttributeDamage = function(_, i) return attributes[i+1].damage end,
    SetAttributeBase = function(_, i, value) attributes[i+1].base = value end,
    SetAttributeDamage = function(_, i, value) attributes[i+1].damage = value end,
    SendAttributes = function() sends = sends + 1 end,
    SetFatigueBase = function(_, value) fatigueBase = value end,
    SetFatigueCurrent = function(_, value) fatigueCurrent = value end,
    SendStatsDynamic = function() dynamicSends = dynamicSends + 1 end,
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

-- A saved pre-penalty capacity must not survive restoration of all four attributes.
config.respawnAttributeFloor = 10
names = { "Strength", "Willpower", "Agility", "Endurance" }
attributes = {
    { base = 1, damage = 0 }, { base = 50, damage = 50 },
    { base = 10, damage = 0 }, { base = 10, damage = 0 },
}
local fatigueSaves = 0
player.QuicksaveToDrive = function() fatigueSaves = fatigueSaves + 1 end
player.data.stats = { fatigueBase = 200, fatigueCurrent = 200, healthCurrent = 0 }
BasePlayer.RestoreRespawnAttributes(player)
assert(fatigueBase == 40 and fatigueCurrent == 40 and dynamicSends == 1, "stale saved fatigue maximum survived respawn")
assert(player.data.stats.fatigueBase == 40 and player.data.stats.fatigueCurrent == 40 and fatigueSaves == 1)
assert(player.data.stats.healthCurrent == 0, 'fatigue preparation must not revive health early')
-- Match the engine's effective-attribute formula, including modifiers.
attributes[1].modifier = 5
attributes[4].modifier = -3
BasePlayer.RestoreRespawnAttributes(player)
assert(fatigueBase == 42 and fatigueCurrent == 42 and dynamicSends == 2)
print('Respawn derives and saves fatigue capacity from current effective attributes')

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

-- The native death/respawn callback persists an empty active-spell SET.
-- Learned spells and permanent abilities remain in their separate spellbook.
enumerations = { spellbook = { SET = 0, ADD = 1, REMOVE = 2 } }
player.data.spellbook = { "permanent ability", "learned spell" }
player.data.spellsActive = { weakness = { { effects = { { duration = 60 } } } } }
BasePlayer.SaveSpellsActive(player, { action = enumerations.spellbook.SET, spellsActive = {} })
assert(next(player.data.spellsActive) == nil)
assert(#player.data.spellbook == 2)
print('Empty respawn active-spell SET clears saved effects without erasing the spellbook')

-- Exercise the real notification callbacks and their persistence boundaries.
package.loaded.commandHandler = {}
local events = dofile(root .. "eventHandler.lua")
local saves = 0
player.IsLoggedIn = function() return true end
player.SaveSpellsActive = BasePlayer.SaveSpellsActive
player.QuicksaveToDrive = function() saves = saves + 1 end
Players = { [0] = player }
customEventHooks = {
    makeEventStatus = function() return {} end,
    triggerHandlers = function() end,
}
packetReader = { GetPlayerPacketTables = function()
    return { action = enumerations.spellbook.SET, spellsActive = {} }
end }
player.data.spellsActive = { poison = { {} } }
events.OnPlayerSpellsActive(0)
assert(next(player.data.spellsActive) == nil and saves == 1)
local recoveredPositions = false
LoadedCells = { ["test cell"] = {
    SaveActorPositions = function() recoveredPositions = true end,
    QuicksaveToDrive = function() assert(recoveredPositions); saves = saves + 1 end,
} }
events.OnActorRecovered(0, "test cell")
assert(saves == 2)
events.OnActorRecovered(0, "unloaded cell")
assert(saves == 2)
print('Respawn effect-clear and NPC recovery callbacks persist their committed state')
