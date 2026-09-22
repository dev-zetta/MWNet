-- Run against the actual combat script without starting a graphical world.
local sounds, staggers, blood, hits = {}, 0, 0, 0
local hitHandler
local npc, attacker = {}, {}
local function getDamage(data, stat) return data.damage[stat] or 0 end
local common = {
    getDamage = getDamage,
    hasDamage = function(data)
        return getDamage(data, 'health') > 0 or getDamage(data, 'fatigue') > 0
    end,
    settings = { get = function() return false end },
}
local combat = {
    addOnHitHandler = function(fn) hitHandler = fn end,
    applyArmor = function() error('unexpected armor calculation') end,
    adjustDamageForDifficulty = function() error('unexpected difficulty calculation') end,
    spawnBloodEffect = function() blood = blood + 1 end,
}
package.loaded['openmw.core'] = {
    sound = { playSound3d = function(id) sounds[#sounds+1] = id end },
    getGMST = function() return 1 end,
}
package.loaded['openmw.interfaces'] = { Combat = combat }
package.loaded['openmw.self'] = npc
package.loaded['openmw.storage'] = {}
package.loaded['openmw.types'] = {
    Player = { objectIsInstance = function(obj) return obj == attacker end },
    Creature = {}, Armor = {},
    Actor = {
        stats = { attributes = { agility = function() return { modified = 100 } end } },
        setKnockedDown = function() staggers = staggers + 1 end,
        setHitRecovery = function() staggers = staggers + 1 end,
        _onHit = function(_, data)
            hits = hits + 1
            if data.waitForServerHit then
                assert(next(data.damage) == nil, 'predicted fist damage must not change resources')
            elseif data.successful then
                assert(data.damage.health == 5, 'ordinary damage must be preserved')
            end
        end,
    },
}
package.loaded['openmw_aux.util'] = { shallowCopy = function(t)
    local copy = {}; for k,v in pairs(t) do copy[k] = v end; return copy
end }
package.loaded['scripts.omw.combat.common'] = common
local module = dofile('files/data-mw/scripts/omw/combat/local.lua')
combat.applyStagger = module.interface.applyStagger
local function hit(waiting, successful, health, fatigue)
    hitHandler({
        waitForServerHit = waiting, successful = successful, attacker = attacker,
        damage = { health = health, fatigue = fatigue }, hitPos = {},
        ignoreArmor = true, ignoreDifficulty = true,
    })
end
-- Neither prediction outcome may produce an impact before the server reply.
hit(true, true, 0, 5)
hit(true, true, 5, 0)
hit(true, false, 0, 0)
assert(#sounds == 0 and staggers == 0 and blood == 0 and hits == 3)
-- Other combat retains its normal presentation and mechanics callback.
hit(false, true, 5, 0)
assert(#sounds == 1 and sounds[1] == 'Health Damage' and staggers == 1 and blood == 1)
hit(false, false, 0, 0)
assert(#sounds == 2 and sounds[2] == 'miss' and staggers == 1 and hits == 5)
print('Fist prediction suppresses duplicate/wrong impact sounds and reactions; other attacks retain presentation')
