-- Staged only into an isolated test profile by CI/run_mwnet_gameplay.py.
-- Nothing in the shipped CoreScripts requires this module.
-- These commands arrange fixtures; only subsequent client actions/readbacks
-- can establish gameplay acceptance. "die" is a controlled death, not an NPC kill.
if os.getenv("MWNET_GAMEPLAY_TEST") ~= "1" then
    return {}
end

local json = require("dkjson")
local metadataPath = mwnet.GetDataPath() .. "/../scripts/gameplay-fixture.json"
local file = assert(io.open(metadataPath, "rb"))
local input = file:read(131073)
file:close()
assert(#input <= 131072, "Oversized gameplay fixture metadata")
local fixture, _, decodeError = json.decode(input)
assert(not decodeError and type(fixture) == "table" and fixture.schemaVersion == 1,
    "Invalid gameplay fixture metadata")
assert(type(fixture.spawn) == "table" and type(fixture.door) == "table" and
    type(fixture.container) == "table" and type(fixture.combatTarget) == "table" and
    type(fixture.dialogueTarget) == "table",
    "Incomplete gameplay fixture metadata")

local function allowed(pid)
    local player = Players[pid]
    if player == nil or not player:IsLoggedIn() or
        type(player.accountName) ~= "string" or player.accountName:lower():sub(1, 9) ~= "mwnetauto" then
        return false
    end
    local ip = mwnet.GetIP(pid)
    return ip == "127.0.0.1" or ip == "::1" or ip == "::ffff:127.0.0.1"
end

local function state(pid, verb, label)
    return {event = verb, label = label, pid = pid, cell = mwnet.GetCell(pid),
        position = {mwnet.GetPosX(pid), mwnet.GetPosY(pid), mwnet.GetPosZ(pid)},
        health = mwnet.GetHealthCurrent(pid), fatigue = mwnet.GetFatigueCurrent(pid)}
end

local function emit(pid, verb, label)
    mwnet.LogMessage(enumerations.log.INFO, "MWNET_GAMEPLAY_FIXTURE " ..
        json.encode(state(pid, verb, label)))
    mwnet.SendMessage(pid, "__mwnet_test_ack " .. verb .. "\n", false)
end

local function teleport(pid, point)
    assert(type(point.cell) == "string" and type(point.position) == "table")
    mwnet.SetCell(pid, point.cell)
    mwnet.SetPos(pid, point.position[1], point.position[2], point.position[3])
    mwnet.SetRot(pid, point.rotation[1], point.rotation[2])
    mwnet.SendCell(pid)
    mwnet.SendPos(pid)
end

local function baseline(pid)
    for attribute = 0, 7 do
        mwnet.SetAttributeBase(pid, attribute, 100)
        mwnet.SetAttributeDamage(pid, attribute, 0)
    end
    -- Hand-to-hand: reduce random misses so real client attacks are repeatable.
    mwnet.SetSkillBase(pid, 26, 100)
    mwnet.SetSkillDamage(pid, 26, 0)
    mwnet.SendAttributes(pid)
    mwnet.SendSkills(pid)
    mwnet.SetHealthBase(pid, 200)
    mwnet.SetHealthCurrent(pid, 200)
    mwnet.SetMagickaBase(pid, 100)
    mwnet.SetMagickaCurrent(pid, 100)
    mwnet.SetFatigueBase(pid, 400)
    mwnet.SetFatigueCurrent(pid, 400)
    mwnet.SendStatsDynamic(pid)
    -- Match CoreScripts persistence to this explicit native arrangement. A
    -- respawn/reconnect must not replay the character's earlier saved values.
    local player = assert(Players[pid], "Fixture player was not found")
    player:SaveAttributes(packetReader.GetPlayerPacketTables(pid, "PlayerAttribute"))
    player:SaveSkills(packetReader.GetPlayerPacketTables(pid, "PlayerSkill"))
    player:SaveStatsDynamic(packetReader.GetPlayerPacketTables(pid, "PlayerStatsDynamic"))
    player:SaveToDrive()
end

local function seedContainer(pid, count, broadcast)
    local target = fixture.container
    mwnet.ClearObjectList()
    mwnet.SetObjectListPid(pid)
    mwnet.SetObjectListCell(target.cell)
    mwnet.SetObjectListAction(enumerations.container.SET)
    mwnet.SetObjectListContainerSubAction(enumerations.containerSub.NONE)
    mwnet.SetObjectRefId(target.refId)
    mwnet.SetObjectRefNum(target.refNum)
    mwnet.SetObjectMpNum(target.mpNum)
    mwnet.SetContainerItemRefId("gold_001")
    mwnet.SetContainerItemCount(count)
    mwnet.SetContainerItemCharge(-1)
    mwnet.SetContainerItemEnchantmentCharge(-1)
    mwnet.SetContainerItemSoul("")
    mwnet.AddContainerItem()
    mwnet.AddObject()
    assert(mwnet.SeedContainerInventory(), "Canonical fixture container seed rejected")

    -- CoreScripts restores its persisted container state on cell load. Keep
    -- that state aligned with the native seed before a teleport can load it;
    -- otherwise a fresh client's cell replay would restore the vanilla items.
    logicHandler.LoadCell(target.cell)
    local cell = assert(LoadedCells[target.cell], "Fixture container cell was not loaded")
    local uniqueIndex = string.format("%d-%d", target.refNum, target.mpNum)
    cell:InitializeObjectData(uniqueIndex, target.refId)
    cell.data.objectData[uniqueIndex].inventory = {{refId = "gold_001", count = count,
        charge = -1, enchantmentCharge = -1, soul = ""}}
    tableHelper.insertValueIfMissing(cell.data.packets.container, uniqueIndex)
    cell:SaveToDrive()
    mwnet.SendContainer(broadcast, false)
end

local queuedResurrections = {}
local originalResurrect = mwnet.Resurrect
mwnet.Resurrect = function(pid, ...)
    local armed = queuedResurrections[pid]
    queuedResurrections[pid] = nil
    if armed and allowed(pid) then
        -- Both spell changes and resurrection use the reliable ordered Player
        -- lane. Send consecutively, without a timer/chat/log between them. The
        -- native removal count proves whether the client observed a queued
        -- effect; ordering alone does not prove both arrived in one frame.
        local ok, errorText = pcall(function()
            local spell = fixture.weakness
            mwnet.ClearSpellsActiveChanges(pid)
            mwnet.SetSpellsActiveChangesAction(pid, 1)
            mwnet.AddSpellActiveEffect(pid, spell.effectId, spell.magnitude,
                spell.duration, spell.duration, -1)
            mwnet.AddSpellActive(pid, "mwnet_gameplay_queued_weakness",
                "Queued resurrection fixture weakness", false)
            mwnet.SendSpellsActiveChanges(pid, false, false)
        end)
        if not ok then
            mwnet.LogMessage(enumerations.log.ERROR,
                "MWNET_GAMEPLAY_FIXTURE_ERROR queued resurrection: " .. tostring(errorText))
        end
    end
    return originalResurrect(pid, ...)
end

local pendingCombat = {}
local readyPlayers = {}

local function ready(pid)
    for waitingPid in pairs(readyPlayers) do
        if not allowed(waitingPid) then readyPlayers[waitingPid] = nil end
    end
    readyPlayers[pid] = true
    local participants = {}
    for waitingPid in pairs(readyPlayers) do
        participants[#participants + 1] = waitingPid
    end
    if #participants < 2 then return end
    table.sort(participants)
    readyPlayers = {}
    for _, waitingPid in ipairs(participants) do
        emit(waitingPid, "ready")
    end
end

function MWNetGameplayFixtureCombatReady(pid)
    if not pendingCombat[pid] or not allowed(pid) then return end
    pendingCombat[pid] = nil
    local target = fixture.combatTarget

    -- The guard may have patrolled since its cell first loaded. Approach its
    -- latest replicated position while leaving the NPC's simulation untouched.
    mwnet.ReadCellActorList(target.cell)
    local approached = false
    for index = 0, mwnet.GetActorListSize() - 1 do
        if mwnet.GetActorRefNum(index) == target.refNum and
            mwnet.GetActorMpNum(index) == target.mpNum and mwnet.DoesActorHavePosition(index) then
            local yaw = mwnet.GetActorRotZ(index)
            teleport(pid, {cell = target.cell, position = {
                mwnet.GetActorPosX(index) - math.sin(yaw) * 80,
                mwnet.GetActorPosY(index) - math.cos(yaw) * 80,
                mwnet.GetActorPosZ(index) + 4}, rotation = {0, yaw}})
            approached = true
            break
        end
    end
    if not approached then
        mwnet.LogMessage(enumerations.log.WARN,
            "MWNET_GAMEPLAY_FIXTURE guard position unavailable; retaining static approach")
    end
    mwnet.ClearActorList()
    mwnet.SetActorListPid(pid)
    mwnet.SetActorListCell(target.cell)
    mwnet.SetActorRefId(target.refId)
    mwnet.SetActorRefNum(target.refNum)
    mwnet.SetActorMpNum(target.mpNum)
    mwnet.SetActorHealthBase(200)
    mwnet.SetActorHealthModified(200)
    mwnet.SetActorHealthCurrent(200)
    mwnet.SetActorFatigueBase(40)
    mwnet.SetActorFatigueModified(40)
    mwnet.SetActorFatigueCurrent(40)
    mwnet.AddActor()
    mwnet.SendActorStatsDynamic(false, false)
    emit(pid, "combat")
end

config.useInstancedSpawn = false
config.noninstancedSpawn = {cellDescription = fixture.spawn.cell,
    position = fixture.spawn.position, rotation = fixture.spawn.rotation,
    text = "Autonomous isolated gameplay fixture", items = {}}
config.defaultRespawn = {cellDescription = fixture.spawn.cell,
    position = fixture.spawn.position, rotation = fixture.spawn.rotation}
config.respawnAtImperialShrine = false
config.respawnAtTribunalTemple = false
config.playersRespawn = true
config.deathTime = 5
config.deathPenaltyJailDays = 0
config.bountyDeathPenalty = false
config.bountyResetOnDeath = true

customEventHooks.registerValidator("OnPlayerSendMessage", function(_, pid, message)
    if type(message) ~= "string" or message:sub(1, 13) ~= "__mwnet_test " then return end
    if not allowed(pid) then
        mwnet.LogMessage(enumerations.log.WARN, "Rejected non-fixture gameplay command")
        return customEventHooks.makeEventStatus(false, false)
    end
    local verb, label = message:match("^__mwnet_test ([%a_]+)%s*([%w_-]*)$")
    local ok, errorText = pcall(function()
        assert(verb ~= nil, "Malformed fixture command")
        if verb == "checkpoint" then
            emit(pid, verb, label)
        elseif verb == "ready" then
            ready(pid)
        elseif verb == "spawn" then
            teleport(pid, fixture.spawn)
            baseline(pid)
            emit(pid, verb)
        elseif verb == "door" then
            teleport(pid, fixture.door.approach)
            emit(pid, verb)
        elseif verb == "dialogue" then
            mwnet.SetBounty(pid, 0)
            mwnet.SendBounty(pid)
            Players[pid]:SaveBounty()
            teleport(pid, fixture.dialogueTarget.approach)
            emit(pid, verb)
        elseif verb == "container" then
            teleport(pid, fixture.container.approach)
            seedContainer(pid, 7, false)
            emit(pid, verb)
        elseif verb == "container_update" then
            seedContainer(pid, 11, true)
            emit(pid, verb)
        elseif verb == "combat" then
            baseline(pid)
            teleport(pid, fixture.combatTarget.approach)
            pendingCombat[pid] = true
            local timer = mwnet.CreateTimerEx("MWNetGameplayFixtureCombatReady", 2000, "i", pid)
            mwnet.StartTimer(timer)
        elseif verb == "weakness" then
            local spell = fixture.weakness
            mwnet.ClearSpellsActiveChanges(pid)
            mwnet.SetSpellsActiveChangesAction(pid, 1)
            mwnet.AddSpellActiveEffect(pid, spell.effectId, spell.magnitude,
                spell.duration, spell.duration, -1)
            mwnet.AddSpellActive(pid, spell.spellId, "Gameplay fixture weakness", false)
            mwnet.SendSpellsActiveChanges(pid, false, false)
            emit(pid, verb)
        elseif verb == "die" or verb == "die_queued" then
            if verb == "die_queued" then queuedResurrections[pid] = true end
            emit(pid, verb)
            mwnet.SetHealthCurrent(pid, 0)
            mwnet.SendStatsDynamic(pid)
        else
            error("Unknown fixture command")
        end
    end)
    if not ok then
        mwnet.LogMessage(enumerations.log.ERROR, "MWNET_GAMEPLAY_FIXTURE_ERROR " .. tostring(errorText))
        mwnet.SendMessage(pid, "__mwnet_test_error " .. tostring(verb) .. "\n", false)
    end
    return customEventHooks.makeEventStatus(false, false)
end)

customEventHooks.registerHandler("OnPlayerDisconnect", function(_, pid)
    readyPlayers[pid] = nil
    pendingCombat[pid] = nil
    queuedResurrections[pid] = nil
end)

return fixture
