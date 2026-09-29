packetBuilder = {}

packetBuilder.AddPlayerInventoryItemChange = function(pid, item)

    -- Use default values when necessary
    if item.charge == nil or item.charge < -1 then item.charge = -1 end
    if item.enchantmentCharge == nil or item.enchantmentCharge < -1 then item.enchantmentCharge = -1 end
    if item.soul == nil then item.soul = "" end

    mwnet.AddItemChange(pid, item.refId, item.count, item.charge, item.enchantmentCharge, item.soul)
end

packetBuilder.AddPlayerSpellsActive = function(pid, spellsActive, action)

    mwnet.ClearSpellsActiveChanges(pid)
    mwnet.SetSpellsActiveChangesAction(pid, action)

    for spellId, spellInstances in pairs(spellsActive) do
        for spellInstanceIndex, spellInstanceValues in pairs(spellInstances) do

            if action == enumerations.spellbook.SET or action == enumerations.spellbook.ADD then
                for effectIndex, effectTable in pairs(spellInstanceValues.effects) do

                    if effectTable.timeLeft > 0 then
                        mwnet.AddSpellActiveEffect(pid, effectTable.id, effectTable.magnitude,
                            effectTable.duration, effectTable.timeLeft, effectTable.arg)
                    end
                end
            end

            mwnet.AddSpellActive(pid, spellId, spellInstanceValues.displayName,
                spellInstanceValues.stackingState)
        end
    end
end

packetBuilder.AddObjectDelete = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    mwnet.SetObjectRefNum(splitIndex[1])
    mwnet.SetObjectMpNum(splitIndex[2])
    if objectData.refId ~= nil then mwnet.SetObjectRefId(objectData.refId) end
    mwnet.AddObject()
end

packetBuilder.AddObjectPlace = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    mwnet.SetObjectRefNum(splitIndex[1])
    mwnet.SetObjectMpNum(splitIndex[2])
    mwnet.SetObjectRefId(objectData.refId)

    local count = objectData.count
    local charge = objectData.charge
    local enchantmentCharge = objectData.enchantmentCharge
    local soul = objectData.soul
    local goldValue = objectData.goldValue
    local droppedByPlayer = objectData.droppedByPlayer

    -- Use default values when necessary
    if count == nil then count = 1 end
    if charge == nil then charge = -1 end
    if enchantmentCharge == nil then enchantmentCharge = -1 end
    if soul == nil then soul = "" end
    if goldValue == nil then goldValue = 1 end
    if droppedByPlayer == nil then droppedByPlayer = false end

    mwnet.SetObjectCount(count)
    mwnet.SetObjectCharge(charge)
    mwnet.SetObjectEnchantmentCharge(enchantmentCharge)
    mwnet.SetObjectSoul(soul)
    mwnet.SetObjectGoldValue(goldValue)
    mwnet.SetObjectDroppedByPlayerState(droppedByPlayer)

    local location = objectData.location
    mwnet.SetObjectPosition(location.posX, location.posY, location.posZ)
    mwnet.SetObjectRotation(location.rotX, location.rotY, location.rotZ)

    mwnet.AddObject()
end

packetBuilder.AddObjectSpawn = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    mwnet.SetObjectRefNum(splitIndex[1])
    mwnet.SetObjectMpNum(splitIndex[2])
    mwnet.SetObjectRefId(objectData.refId)

    if objectData.summon ~= nil then
        mwnet.SetObjectSummonState(true)
        mwnet.SetObjectSummonEffectId(objectData.summon.effectId)
        mwnet.SetObjectSummonSpellId(objectData.summon.spellId)

        local currentTime = os.time()
        local finishTime = objectData.summon.startTime + objectData.summon.duration
        mwnet.SetObjectSummonDuration(finishTime - currentTime)

        if objectData.summon.summoner.playerName then
            local player = logicHandler.GetPlayerByName(objectData.summon.summoner.playerName)
            mwnet.SetObjectSummonerPid(player.pid)
        else
            local summonerSplitIndex = objectData.summon.summoner.uniqueIndex:split("-")
            mwnet.SetObjectSummonerRefNum(summonerSplitIndex[1])
            mwnet.SetObjectSummonerMpNum(summonerSplitIndex[2])
        end
    end

    local location = objectData.location
    mwnet.SetObjectPosition(location.posX, location.posY, location.posZ)
    mwnet.SetObjectRotation(location.rotX, location.rotY, location.rotZ)

    mwnet.AddObject()
end

packetBuilder.AddObjectLock = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    mwnet.SetObjectRefNum(splitIndex[1])
    mwnet.SetObjectMpNum(splitIndex[2])
    if objectData.refId ~= nil then mwnet.SetObjectRefId(objectData.refId) end
    mwnet.SetObjectLockLevel(objectData.lockLevel)
    mwnet.AddObject()
end

packetBuilder.AddObjectMiscellaneous = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    mwnet.SetObjectRefNum(splitIndex[1])
    mwnet.SetObjectMpNum(splitIndex[2])
    if objectData.refId ~= nil then mwnet.SetObjectRefId(objectData.refId) end
    mwnet.SetObjectGoldPool(objectData.goldPool)
    mwnet.SetObjectLastGoldRestockHour(objectData.lastGoldRestockHour)
    mwnet.SetObjectLastGoldRestockDay(objectData.lastGoldRestockDay)
    mwnet.AddObject()
end

packetBuilder.AddObjectTrap = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    mwnet.SetObjectRefNum(splitIndex[1])
    mwnet.SetObjectMpNum(splitIndex[2])
    if objectData.refId ~= nil then mwnet.SetObjectRefId(objectData.refId) end
    mwnet.SetObjectDisarmState(true)
    mwnet.AddObject()
end

packetBuilder.AddObjectScale = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    mwnet.SetObjectRefNum(splitIndex[1])
    mwnet.SetObjectMpNum(splitIndex[2])
    if objectData.refId ~= nil then mwnet.SetObjectRefId(objectData.refId) end
    mwnet.SetObjectScale(objectData.scale)
    mwnet.AddObject()
end

packetBuilder.AddObjectState = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    mwnet.SetObjectRefNum(splitIndex[1])
    mwnet.SetObjectMpNum(splitIndex[2])
    if objectData.refId ~= nil then mwnet.SetObjectRefId(objectData.refId) end
    mwnet.SetObjectState(objectData.state)
    mwnet.AddObject()
end

packetBuilder.AddDoorState = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    mwnet.SetObjectRefNum(splitIndex[1])
    mwnet.SetObjectMpNum(splitIndex[2])
    if objectData.refId ~= nil then mwnet.SetObjectRefId(objectData.refId) end
    mwnet.SetObjectDoorState(objectData.doorState)
    mwnet.AddObject()
end

packetBuilder.AddClientScriptLocal = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    mwnet.SetObjectRefNum(splitIndex[1])
    mwnet.SetObjectMpNum(splitIndex[2])
    if objectData.refId ~= nil then mwnet.SetObjectRefId(objectData.refId) end

    local variableCount = 0

    for variableType, variableTable in pairs(objectData.variables) do

        if type(variableTable) == "table" then

            for internalIndex, value in pairs(variableTable) do

                if variableType == enumerations.variableType.SHORT then
                    mwnet.AddClientLocalInteger(tonumber(internalIndex), value, enumerations.variableType.SHORT)
                elseif variableType == enumerations.variableType.LONG then
                    mwnet.AddClientLocalInteger(tonumber(internalIndex), value, enumerations.variableType.LONG)
                elseif variableType == enumerations.variableType.FLOAT then
                    mwnet.AddClientLocalFloat(tonumber(internalIndex), value)
                end

                variableCount = variableCount + 1
            end
        end
    end

    if variableCount > 0 then
        mwnet.AddObject()
    end
end

packetBuilder.AddAIActor = function(actorUniqueIndex, targetPid, aiData)

    local splitIndex = actorUniqueIndex:split("-")
    mwnet.SetActorRefNum(splitIndex[1])
    mwnet.SetActorMpNum(splitIndex[2])

    mwnet.SetActorAIAction(aiData.action)

    if targetPid ~= nil then
        mwnet.SetActorAITargetToPlayer(targetPid)
    elseif aiData.targetUniqueIndex ~= nil then
        local targetSplitIndex = aiData.targetUniqueIndex:split("-")

        if targetSplitIndex[2] ~= nil then
            mwnet.SetActorAITargetToObject(targetSplitIndex[1], targetSplitIndex[2])
        end
    elseif aiData.posX ~= nil and aiData.posY ~= nil and aiData.posZ ~= nil then
        mwnet.SetActorAICoordinates(aiData.posX, aiData.posY, aiData.posZ)
    elseif aiData.distance ~= nil then
        mwnet.SetActorAIDistance(aiData.distance)
    elseif aiData.duration ~= nil then
        mwnet.SetActorAIDuration(aiData.duration)
    end

    mwnet.SetActorAIRepetition(aiData.shouldRepeat)

    mwnet.AddActor()
end

packetBuilder.AddActorSpellsActive = function(actorUniqueIndex, spellsActive, action)

    local splitIndex = actorUniqueIndex:split("-")
    mwnet.SetActorRefNum(splitIndex[1])
    mwnet.SetActorMpNum(splitIndex[2])
    mwnet.SetActorSpellsActiveAction(action)

    for spellId, spellInstances in pairs(spellsActive) do
        for spellInstanceIndex, spellInstanceValues in pairs(spellInstances) do

            if action == enumerations.spellbook.SET or action == enumerations.spellbook.ADD then
                for effectIndex, effectTable in pairs(spellInstanceValues.effects) do

                    if effectTable.timeLeft > 0 then
                        mwnet.AddActorSpellActiveEffect(effectTable.id, effectTable.magnitude,
                            effectTable.duration, effectTable.timeLeft, effectTable.arg)
                    end
                end
            end

            mwnet.AddActorSpellActive(spellId, spellInstanceValues.displayName,
                spellInstanceValues.stackingState)
        end
    end

    mwnet.AddActor()
end

packetBuilder.AddEffectToRecord = function(effect)

    mwnet.SetRecordEffectId(effect.id)
    if effect.attribute ~= nil then mwnet.SetRecordEffectAttribute(effect.attribute) end
    if effect.skill ~= nil then mwnet.SetRecordEffectSkill(effect.skill) end
    if effect.rangeType ~= nil then mwnet.SetRecordEffectRangeType(effect.rangeType) end
    if effect.area ~= nil then mwnet.SetRecordEffectArea(effect.area) end
    if effect.duration ~= nil then mwnet.SetRecordEffectDuration(effect.duration) end
    if effect.magnitudeMin ~= nil then mwnet.SetRecordEffectMagnitudeMin(effect.magnitudeMin) end
    if effect.magnitudeMax ~= nil then mwnet.SetRecordEffectMagnitudeMax(effect.magnitudeMax) end

    mwnet.AddRecordEffect()
end

packetBuilder.AddBodyPartToRecord = function(part)

    mwnet.SetRecordBodyPartType(part.partType)
    if part.malePart ~= nil then mwnet.SetRecordBodyPartIdForMale(part.malePart) end
    if part.femalePart ~= nil then mwnet.SetRecordBodyPartIdForFemale(part.femalePart) end

    mwnet.AddRecordBodyPart()
end

packetBuilder.AddInventoryItemToRecord = function(item)

    mwnet.SetRecordInventoryItemId(item.id)
    if item.count ~= nil then mwnet.SetRecordInventoryItemCount(item.count) end

    mwnet.AddRecordInventoryItem()
end

packetBuilder.AddRecordByType = function(id, record, storeType)
    local stype = {
        ["activator"] = packetBuilder.AddActivatorRecord,
        ["apparatus"] = packetBuilder.AddApparatusRecord,
        ["armor"] = packetBuilder.AddArmorRecord,
        ["bodypart"] = packetBuilder.AddBodyPartRecord,
        ["book"] = packetBuilder.AddBookRecord,
        ["cell"] = packetBuilder.AddCellRecord,
        ["clothing"] = packetBuilder.AddClothingRecord,
        ["container"] = packetBuilder.AddContainerRecord,
        ["creature"] = packetBuilder.AddCreatureRecord,
        ["door"] = packetBuilder.AddDoorRecord,
        ["enchantment"] = packetBuilder.AddEnchantmentRecord,
        ["gamesetting"] = packetBuilder.AddGameSettingRecord,
        ["ingredient"] = packetBuilder.AddIngredientRecord,
        ["light"] = packetBuilder.AddLightRecord,
        ["lockpick"] = packetBuilder.AddLockpickRecord,
        ["miscellaneous"] = packetBuilder.AddMiscellaneousRecord,
        ["npc"] = packetBuilder.AddNpcRecord,
        ["potion"] = packetBuilder.AddPotionRecord,
        ["probe"] = packetBuilder.AddProbeRecord,
        ["repair"] = packetBuilder.AddRepairRecord,
        ["script"] = packetBuilder.AddScriptRecord,
        ["sound"] = packetBuilder.AddSoundRecord,
        ["spell"] = packetBuilder.AddSpellRecord,
        ["static"] = packetBuilder.AddStaticRecord,
        ["weapon"] = packetBuilder.AddWeaponRecord,
    }

    if stype[storeType] then
        stype[storeType](id, record)
    end
end

packetBuilder.AddActivatorRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then mwnet.SetRecordName(record.name) end
    if record.model ~= nil then mwnet.SetRecordModel(record.model) end
    if record.script ~= nil then mwnet.SetRecordScript(record.script) end

    mwnet.AddRecord()
end

packetBuilder.AddApparatusRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then mwnet.SetRecordName(record.name) end
    if record.model ~= nil then mwnet.SetRecordModel(record.model) end
    if record.icon ~= nil then mwnet.SetRecordIcon(record.icon) end
    if record.subtype ~= nil then mwnet.SetRecordSubtype(record.subtype) end
    if record.weight ~= nil then mwnet.SetRecordWeight(record.weight) end
    if record.value ~= nil then mwnet.SetRecordValue(record.value) end
    if record.quality ~= nil then mwnet.SetRecordQuality(record.quality) end
    if record.script ~= nil then mwnet.SetRecordScript(record.script) end

    mwnet.AddRecord()
end

packetBuilder.AddArmorRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then mwnet.SetRecordName(record.name) end
    if record.model ~= nil then mwnet.SetRecordModel(record.model) end
    if record.icon ~= nil then mwnet.SetRecordIcon(record.icon) end
    if record.subtype ~= nil then mwnet.SetRecordSubtype(record.subtype) end
    if record.weight ~= nil then mwnet.SetRecordWeight(record.weight) end
    if record.value ~= nil then mwnet.SetRecordValue(record.value) end
    if record.health ~= nil then mwnet.SetRecordHealth(record.health) end
    if record.armorRating ~= nil then mwnet.SetRecordArmorRating(record.armorRating) end
    if record.enchantmentId ~= nil then mwnet.SetRecordEnchantmentId(record.enchantmentId) end
    if record.enchantmentCharge ~= nil then mwnet.SetRecordEnchantmentCharge(record.enchantmentCharge) end
    if record.script ~= nil then mwnet.SetRecordScript(record.script) end

    if type(record.parts) == "table" then
        for _, part in pairs(record.parts) do
            packetBuilder.AddBodyPartToRecord(part)
        end
    end

    mwnet.AddRecord()
end

packetBuilder.AddBodyPartRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.subtype ~= nil then mwnet.SetRecordSubtype(record.subtype) end
    if record.part ~= nil then mwnet.SetRecordBodyPartType(record.part) end
    if record.model ~= nil then mwnet.SetRecordModel(record.model) end
    if record.race ~= nil then mwnet.SetRecordRace(record.race) end
    if record.vampireState ~= nil then mwnet.SetRecordVampireState(record.vampireState) end
    if record.flags ~= nil then mwnet.SetRecordFlags(record.flags) end

    mwnet.AddRecord()
end

packetBuilder.AddBookRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then mwnet.SetRecordName(record.name) end
    if record.model ~= nil then mwnet.SetRecordModel(record.model) end
    if record.icon ~= nil then mwnet.SetRecordIcon(record.icon) end
    if record.text ~= nil then mwnet.SetRecordText(record.text) end
    if record.weight ~= nil then mwnet.SetRecordWeight(record.weight) end
    if record.value ~= nil then mwnet.SetRecordValue(record.value) end
    if record.scrollState ~= nil then mwnet.SetRecordScrollState(record.scrollState) end
    if record.skillId ~= nil then mwnet.SetRecordSkillId(record.skillId) end
    if record.enchantmentId ~= nil then mwnet.SetRecordEnchantmentId(record.enchantmentId) end
    if record.enchantmentCharge ~= nil then mwnet.SetRecordEnchantmentCharge(record.enchantmentCharge) end
    if record.script ~= nil then mwnet.SetRecordScript(record.script) end

    mwnet.AddRecord()
end

packetBuilder.AddCellRecord = function(id, record)

    mwnet.SetRecordName(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end

    mwnet.AddRecord()
end

packetBuilder.AddClothingRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then mwnet.SetRecordName(record.name) end
    if record.model ~= nil then mwnet.SetRecordModel(record.model) end
    if record.icon ~= nil then mwnet.SetRecordIcon(record.icon) end
    if record.subtype ~= nil then mwnet.SetRecordSubtype(record.subtype) end
    if record.weight ~= nil then mwnet.SetRecordWeight(record.weight) end
    if record.value ~= nil then mwnet.SetRecordValue(record.value) end
    if record.enchantmentId ~= nil then mwnet.SetRecordEnchantmentId(record.enchantmentId) end
    if record.enchantmentCharge ~= nil then mwnet.SetRecordEnchantmentCharge(record.enchantmentCharge) end
    if record.script ~= nil then mwnet.SetRecordScript(record.script) end

    if type(record.parts) == "table" then
        for _, part in pairs(record.parts) do
            packetBuilder.AddBodyPartToRecord(part)
        end
    end

    mwnet.AddRecord()
end

packetBuilder.AddContainerRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then mwnet.SetRecordName(record.name) end
    if record.model ~= nil then mwnet.SetRecordModel(record.model) end
    if record.weight ~= nil then mwnet.SetRecordWeight(record.weight) end
    if record.flags ~= nil then mwnet.SetRecordFlags(record.flags) end
    if record.script ~= nil then mwnet.SetRecordScript(record.script) end

    if type(record.items) == "table" then
        for _, item in pairs(record.items) do
            packetBuilder.AddInventoryItemToRecord(item)
        end
    end

    mwnet.AddRecord()
end

packetBuilder.AddCreatureRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then mwnet.SetRecordName(record.name) end
    if record.model ~= nil then mwnet.SetRecordModel(record.model) end
    if record.subtype ~= nil then mwnet.SetRecordSubtype(record.subtype) end
    if record.scale ~= nil then mwnet.SetRecordScale(record.scale) end
    if record.bloodType ~= nil then mwnet.SetRecordBloodType(record.bloodType) end
    if record.level ~= nil then mwnet.SetRecordLevel(record.level) end
    if record.health ~= nil then mwnet.SetRecordHealth(record.health) end
    if record.magicka ~= nil then mwnet.SetRecordMagicka(record.magicka) end
    if record.fatigue ~= nil then mwnet.SetRecordFatigue(record.fatigue) end
    if record.soulValue ~= nil then mwnet.SetRecordSoulValue(record.soulValue) end
    if record.damageChop ~= nil then mwnet.SetRecordDamageChop(record.damageChop.min, record.damageChop.max) end
    if record.damageSlash ~= nil then mwnet.SetRecordDamageSlash(record.damageSlash.min, record.damageSlash.max) end
    if record.damageThrust ~= nil then mwnet.SetRecordDamageThrust(record.damageThrust.min, record.damageThrust.max) end
    if record.aiFight ~= nil then mwnet.SetRecordAIFight(record.aiFight) end
    if record.aiServices ~= nil then mwnet.SetRecordAIServices(record.aiServices) end
    if record.aiFlee ~= nil then mwnet.SetRecordAIFlee(record.aiFlee) end
    if record.aiAlarm ~= nil then mwnet.SetRecordAIAlarm(record.aiAlarm) end
    if record.flags ~= nil then mwnet.SetRecordFlags(record.flags) end
    if record.script ~= nil then mwnet.SetRecordScript(record.script) end

    if type(record.items) == "table" then
        for _, item in pairs(record.items) do
            packetBuilder.AddInventoryItemToRecord(item)
        end
    end

    mwnet.AddRecord()
end

packetBuilder.AddDoorRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then mwnet.SetRecordName(record.name) end
    if record.model ~= nil then mwnet.SetRecordModel(record.model) end
    if record.openSound ~= nil then mwnet.SetRecordOpenSound(record.openSound) end
    if record.closeSound ~= nil then mwnet.SetRecordCloseSound(record.closeSound) end
    if record.script ~= nil then mwnet.SetRecordScript(record.script) end

    mwnet.AddRecord()
end

packetBuilder.AddEnchantmentRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.subtype ~= nil then mwnet.SetRecordSubtype(record.subtype) end
    if record.cost ~= nil then mwnet.SetRecordCost(record.cost) end
    if record.charge ~= nil then mwnet.SetRecordCharge(record.charge) end

    if record.flags ~= nil then mwnet.SetRecordFlags(record.flags)
    -- Keep this for compatibility with older data which used autoCalc
    elseif record.autoCalc ~= nil then mwnet.SetRecordFlags(record.autoCalc) end

    if type(record.effects) == "table" then
        for _, effect in pairs(record.effects) do
            packetBuilder.AddEffectToRecord(effect)
        end
    end

    mwnet.AddRecord()
end

packetBuilder.AddGameSettingRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end

    if record.intVar ~= nil then mwnet.SetRecordIntegerVariable(record.intVar)
    elseif record.floatVar ~= nil then mwnet.SetRecordFloatVariable(record.floatVar)
    elseif record.stringVar ~= nil then mwnet.SetRecordStringVariable(tostring(record.stringVar)) end

    mwnet.AddRecord()
end

packetBuilder.AddIngredientRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then mwnet.SetRecordName(record.name) end
    if record.model ~= nil then mwnet.SetRecordModel(record.model) end
    if record.icon ~= nil then mwnet.SetRecordIcon(record.icon) end
    if record.weight ~= nil then mwnet.SetRecordWeight(record.weight) end
    if record.value ~= nil then mwnet.SetRecordValue(record.value) end
    if record.script ~= nil then mwnet.SetRecordScript(record.script) end

    if type(record.effects) == "table" then
        for effectIndex = 1, 4 do
            local effect = record.effects[effectIndex]

            if effect == nil then
                effect = { id = -1 }
            end

            packetBuilder.AddEffectToRecord(effect)
        end
    end

    mwnet.AddRecord()
end

packetBuilder.AddLightRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then mwnet.SetRecordName(record.name) end
    if record.model ~= nil then mwnet.SetRecordModel(record.model) end
    if record.icon ~= nil then mwnet.SetRecordIcon(record.icon) end
    if record.sound ~= nil then mwnet.SetRecordSound(record.sound) end
    if record.weight ~= nil then mwnet.SetRecordWeight(record.weight) end
    if record.value ~= nil then mwnet.SetRecordValue(record.value) end
    if record.time ~= nil then mwnet.SetRecordTime(record.time) end
    if record.radius ~= nil then mwnet.SetRecordRadius(record.radius) end
    if record.color ~= nil then mwnet.SetRecordColor(record.color.red, record.color.green, record.color.blue) end
    if record.flags ~= nil then mwnet.SetRecordFlags(record.flags) end
    if record.script ~= nil then mwnet.SetRecordScript(record.script) end

    mwnet.AddRecord()
end

packetBuilder.AddLockpickRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then mwnet.SetRecordName(record.name) end
    if record.model ~= nil then mwnet.SetRecordModel(record.model) end
    if record.icon ~= nil then mwnet.SetRecordIcon(record.icon) end
    if record.weight ~= nil then mwnet.SetRecordWeight(record.weight) end
    if record.value ~= nil then mwnet.SetRecordValue(record.value) end
    if record.quality ~= nil then mwnet.SetRecordQuality(record.quality) end
    if record.uses ~= nil then mwnet.SetRecordUses(record.uses) end
    if record.script ~= nil then mwnet.SetRecordScript(record.script) end

    mwnet.AddRecord()
end

packetBuilder.AddMiscellaneousRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then mwnet.SetRecordName(record.name) end
    if record.model ~= nil then mwnet.SetRecordModel(record.model) end
    if record.icon ~= nil then mwnet.SetRecordIcon(record.icon) end
    if record.weight ~= nil then mwnet.SetRecordWeight(record.weight) end
    if record.value ~= nil then mwnet.SetRecordValue(record.value) end
    if record.keyState ~= nil then mwnet.SetRecordKeyState(record.keyState) end
    if record.script ~= nil then mwnet.SetRecordScript(record.script) end

    mwnet.AddRecord()
end

packetBuilder.AddNpcRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.inventoryBaseId ~= nil then mwnet.SetRecordInventoryBaseId(record.inventoryBaseId) end
    if record.name ~= nil then mwnet.SetRecordName(record.name) end
    if record.gender ~= nil then mwnet.SetRecordGender(record.gender) end
    if record.race ~= nil then mwnet.SetRecordRace(record.race) end
    if record.hair ~= nil then mwnet.SetRecordHair(record.hair) end
    if record.head ~= nil then mwnet.SetRecordHead(record.head) end
    if record.class ~= nil then mwnet.SetRecordClass(record.class) end
    if record.level ~= nil then mwnet.SetRecordLevel(record.level) end
    if record.health ~= nil then mwnet.SetRecordHealth(record.health) end
    if record.magicka ~= nil then mwnet.SetRecordMagicka(record.magicka) end
    if record.fatigue ~= nil then mwnet.SetRecordFatigue(record.fatigue) end
    if record.aiFight ~= nil then mwnet.SetRecordAIFight(record.aiFight) end
    if record.aiFlee ~= nil then mwnet.SetRecordAIFlee(record.aiFlee) end
    if record.aiAlarm ~= nil then mwnet.SetRecordAIAlarm(record.aiAlarm) end
    if record.aiServices ~= nil then mwnet.SetRecordAIServices(record.aiServices) end
    if record.autoCalc ~= nil then mwnet.SetRecordAutoCalc(record.autoCalc) end
    if record.faction ~= nil then mwnet.SetRecordFaction(record.faction) end
    if record.script ~= nil then mwnet.SetRecordScript(record.script) end

    if type(record.items) == "table" then
        for _, item in pairs(record.items) do
            packetBuilder.AddInventoryItemToRecord(item)
        end
    end

    mwnet.AddRecord()
end

packetBuilder.AddPotionRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then mwnet.SetRecordName(record.name) end
    if record.weight ~= nil then mwnet.SetRecordWeight(record.weight) end
    if record.value ~= nil then mwnet.SetRecordValue(record.value) end
    if record.autoCalc ~= nil then mwnet.SetRecordAutoCalc(record.autoCalc) end
    if record.icon ~= nil then mwnet.SetRecordIcon(record.icon) end
    if record.model ~= nil then mwnet.SetRecordModel(record.model) end
    if record.script ~= nil then mwnet.SetRecordScript(record.script) end

    if type(record.effects) == "table" then
        for _, effect in pairs(record.effects) do
            packetBuilder.AddEffectToRecord(effect)
        end
    end

    mwnet.AddRecord()
end

packetBuilder.AddProbeRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then mwnet.SetRecordName(record.name) end
    if record.model ~= nil then mwnet.SetRecordModel(record.model) end
    if record.icon ~= nil then mwnet.SetRecordIcon(record.icon) end
    if record.weight ~= nil then mwnet.SetRecordWeight(record.weight) end
    if record.value ~= nil then mwnet.SetRecordValue(record.value) end
    if record.quality ~= nil then mwnet.SetRecordQuality(record.quality) end
    if record.uses ~= nil then mwnet.SetRecordUses(record.uses) end
    if record.script ~= nil then mwnet.SetRecordScript(record.script) end

    mwnet.AddRecord()
end

packetBuilder.AddRepairRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then mwnet.SetRecordName(record.name) end
    if record.model ~= nil then mwnet.SetRecordModel(record.model) end
    if record.icon ~= nil then mwnet.SetRecordIcon(record.icon) end
    if record.weight ~= nil then mwnet.SetRecordWeight(record.weight) end
    if record.value ~= nil then mwnet.SetRecordValue(record.value) end
    if record.quality ~= nil then mwnet.SetRecordQuality(record.quality) end
    if record.uses ~= nil then mwnet.SetRecordUses(record.uses) end
    if record.script ~= nil then mwnet.SetRecordScript(record.script) end

    mwnet.AddRecord()
end

packetBuilder.AddScriptRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.scriptText ~= nil then mwnet.SetRecordScriptText(record.scriptText) end

    mwnet.AddRecord()
end

packetBuilder.AddSoundRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.sound ~= nil then mwnet.SetRecordSound(record.sound) end
    if record.volume ~= nil then mwnet.SetRecordVolume(record.volume) end
    if record.minRange ~= nil then mwnet.SetRecordMinRange(record.minRange) end
    if record.maxRange ~= nil then mwnet.SetRecordMaxRange(record.maxRange) end

    mwnet.AddRecord()
end

packetBuilder.AddSpellRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then mwnet.SetRecordName(record.name) end
    if record.subtype ~= nil then mwnet.SetRecordSubtype(record.subtype) end
    if record.cost ~= nil then mwnet.SetRecordCost(record.cost) end
    if record.flags ~= nil then mwnet.SetRecordFlags(record.flags) end

    if type(record.effects) == "table" then
        for _, effect in pairs(record.effects) do
            packetBuilder.AddEffectToRecord(effect)
        end
    end

    mwnet.AddRecord()
end

packetBuilder.AddStaticRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.model ~= nil then mwnet.SetRecordModel(record.model) end

    mwnet.AddRecord()
end

packetBuilder.AddWeaponRecord = function(id, record)

    mwnet.SetRecordId(id)
    if record.baseId ~= nil then mwnet.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then mwnet.SetRecordName(record.name) end
    if record.model ~= nil then mwnet.SetRecordModel(record.model) end
    if record.icon ~= nil then mwnet.SetRecordIcon(record.icon) end
    if record.subtype ~= nil then mwnet.SetRecordSubtype(record.subtype) end
    if record.weight ~= nil then mwnet.SetRecordWeight(record.weight) end
    if record.value ~= nil then mwnet.SetRecordValue(record.value) end
    if record.health ~= nil then mwnet.SetRecordHealth(record.health) end
    if record.speed ~= nil then mwnet.SetRecordSpeed(record.speed) end
    if record.reach ~= nil then mwnet.SetRecordReach(record.reach) end
    if record.damageChop ~= nil then mwnet.SetRecordDamageChop(record.damageChop.min, record.damageChop.max) end
    if record.damageSlash ~= nil then mwnet.SetRecordDamageSlash(record.damageSlash.min, record.damageSlash.max) end
    if record.damageThrust ~= nil then mwnet.SetRecordDamageThrust(record.damageThrust.min, record.damageThrust.max) end
    if record.flags ~= nil then mwnet.SetRecordFlags(record.flags) end
    if record.enchantmentId ~= nil then mwnet.SetRecordEnchantmentId(record.enchantmentId) end
    if record.enchantmentCharge ~= nil then mwnet.SetRecordEnchantmentCharge(record.enchantmentCharge) end
    if record.script ~= nil then mwnet.SetRecordScript(record.script) end

    mwnet.AddRecord()
end

return packetBuilder
