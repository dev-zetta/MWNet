packetReader = {}

packetReader.GetPlayerPacketTables = function(pid, packetType)

    local packetTable = {}

    if packetType == "PlayerClass" then
        packetTable.character = {}
        packetTable.character.defaultClassState = mwnet.IsClassDefault(pid)

        if packetTable.character.defaultClassState == 1 then
            packetTable.character.class = mwnet.GetDefaultClass(pid)
        else
            packetTable.character.class = "custom"
            packetTable.customClass = {
                name = mwnet.GetClassName(pid),
                description = mwnet.GetClassDesc(pid):gsub("\n", "\\n"),
                specialization = mwnet.GetClassSpecialization(pid)
            }

            local majorAttributes = {}
            local majorSkills = {}
            local minorSkills = {}

            for index = 0, 1, 1 do
                majorAttributes[index + 1] = mwnet.GetAttributeName(tonumber(mwnet.GetClassMajorAttribute(pid, index)))
            end

            for index = 0, 4, 1 do
                majorSkills[index + 1] = mwnet.GetSkillName(tonumber(mwnet.GetClassMajorSkill(pid, index)))
                minorSkills[index + 1] = mwnet.GetSkillName(tonumber(mwnet.GetClassMinorSkill(pid, index)))
            end

            packetTable.customClass.majorAttributes = table.concat(majorAttributes, ", ")
            packetTable.customClass.majorSkills = table.concat(majorSkills, ", ")
            packetTable.customClass.minorSkills = table.concat(minorSkills, ", ")
        end
    elseif packetType == "PlayerStatsDynamic" then
        packetTable.stats = {
            healthBase = mwnet.GetHealthBase(pid),
            magickaBase = mwnet.GetMagickaBase(pid),
            fatigueBase = mwnet.GetFatigueBase(pid),
            healthCurrent = mwnet.GetHealthCurrent(pid),
            magickaCurrent = mwnet.GetMagickaCurrent(pid),
            fatigueCurrent = mwnet.GetFatigueCurrent(pid)
        }
    elseif packetType == "PlayerAttribute" then
        packetTable.attributes = {}

        for attributeIndex = 0, mwnet.GetAttributeCount() - 1 do
            local attributeName = mwnet.GetAttributeName(attributeIndex)

            packetTable.attributes[attributeName] = {
                base = mwnet.GetAttributeBase(pid, attributeIndex),
                damage = mwnet.GetAttributeDamage(pid, attributeIndex),
                skillIncrease = mwnet.GetSkillIncrease(pid, attributeIndex),
                modifier = mwnet.GetAttributeModifier(pid, attributeIndex)
            }
        end
    elseif packetType == "PlayerSkill" then
        packetTable.skills = {}

        for skillIndex = 0, mwnet.GetSkillCount() - 1 do
            local skillName = mwnet.GetSkillName(skillIndex)

            packetTable.skills[skillName] = {
                base = mwnet.GetSkillBase(pid, skillIndex),
                damage = mwnet.GetSkillDamage(pid, skillIndex),
                progress = mwnet.GetSkillProgress(pid, skillIndex),
                modifier = mwnet.GetSkillModifier(pid, skillIndex)
            }
        end
    elseif packetType == "PlayerLevel" then
        packetTable.stats = {
            level = mwnet.GetLevel(pid),
            levelProgress = mwnet.GetLevelProgress(pid)
        }
    elseif packetType == "PlayerShapeshift" then
        packetTable.shapeshift = {
            scale = mwnet.GetScale(pid),
            isWerewolf = mwnet.IsWerewolf(pid)
        }
    elseif packetType == "PlayerCellChange" then
        packetTable.location = {
            cell = mwnet.GetCell(pid),
            posX = mwnet.GetPosX(pid),
            posY = mwnet.GetPosY(pid),
            posZ = mwnet.GetPosZ(pid),
            rotX = mwnet.GetRotX(pid),
            rotZ = mwnet.GetRotZ(pid)
        }
    elseif packetType == "PlayerEquipment" then
        packetTable.equipment = {}

        for changesIndex = 0, mwnet.GetEquipmentChangesSize(pid) - 1 do
            local slot = mwnet.GetEquipmentChangesSlot(pid, changesIndex)

            packetTable.equipment[slot] = {
                refId = mwnet.GetEquipmentItemRefId(pid, slot),
                count = mwnet.GetEquipmentItemCount(pid, slot),
                charge = mwnet.GetEquipmentItemCharge(pid, slot),
                enchantmentCharge = mwnet.GetEquipmentItemEnchantmentCharge(pid, slot)
            }
        end
    elseif packetType == "PlayerInventory" then
        packetTable.inventory = {}
        packetTable.action = mwnet.GetInventoryChangesAction(pid)

        for changesIndex = 0, mwnet.GetInventoryChangesSize(pid) - 1 do
            local item = {
                refId = mwnet.GetInventoryItemRefId(pid, changesIndex),
                count = mwnet.GetInventoryItemCount(pid, changesIndex),
                charge = mwnet.GetInventoryItemCharge(pid, changesIndex),
                enchantmentCharge = mwnet.GetInventoryItemEnchantmentCharge(pid, changesIndex),
                soul = mwnet.GetInventoryItemSoul(pid, changesIndex)
            }

            table.insert(packetTable.inventory, item)
        end
    elseif packetType == "PlayerSpellbook" then
        packetTable.spellbook = {}
        packetTable.action = mwnet.GetSpellbookChangesAction(pid)

        for changesIndex = 0, mwnet.GetSpellbookChangesSize(pid) - 1 do
            local spellId = mwnet.GetSpellId(pid, changesIndex)
            table.insert(packetTable.spellbook, spellId)
        end
    elseif packetType == "PlayerSpellsActive" then
        packetTable.spellsActive = {}
        packetTable.action = mwnet.GetSpellsActiveChangesAction(pid)

        for changesIndex = 0, mwnet.GetSpellsActiveChangesSize(pid) - 1 do
            local spellId = mwnet.GetSpellsActiveId(pid, changesIndex)

            if packetTable.spellsActive[spellId] == nil then
                packetTable.spellsActive[spellId] = {}
            end

            local spellInstance = {
                effects = {},
                displayName = mwnet.GetSpellsActiveDisplayName(pid, changesIndex),
                stackingState = mwnet.GetSpellsActiveStackingState(pid, changesIndex),
                startTime = os.time(),
                caster = {}
            }

            spellInstance.hasPlayerCaster = mwnet.DoesSpellsActiveHavePlayerCaster(pid, changesIndex)

            if spellInstance.hasPlayerCaster == true then
                local casterPid = mwnet.GetSpellsActiveCasterPid(pid, changesIndex)
                spellInstance.caster.pid = casterPid

                if Players[casterPid] ~= nil then
                    spellInstance.caster.playerName = Players[casterPid].accountName
                end
            else
                spellInstance.caster.uniqueIndex = mwnet.GetSpellsActiveCasterRefNum(pid, changesIndex) ..
                    "-" .. mwnet.GetSpellsActiveCasterMpNum(pid, changesIndex)
                spellInstance.caster.refId = mwnet.GetSpellsActiveCasterRefId(pid, changesIndex)
            end

            for effectIndex = 0, mwnet.GetSpellsActiveEffectCount(pid, changesIndex) - 1 do
                local effect = {
                    id = mwnet.GetSpellsActiveEffectId(pid, changesIndex, effectIndex),
                    magnitude = mwnet.GetSpellsActiveEffectMagnitude(pid, changesIndex, effectIndex),
                    duration = mwnet.GetSpellsActiveEffectDuration(pid, changesIndex, effectIndex),
                    timeLeft = mwnet.GetSpellsActiveEffectTimeLeft(pid, changesIndex, effectIndex),
                    arg = mwnet.GetSpellsActiveEffectArg(pid, changesIndex, effectIndex)
                }

                if effect.timeLeft > 0 then
                    table.insert(spellInstance.effects, effect)
                end
            end

            if tableHelper.getCount(spellInstance.effects) > 0 then
                table.insert(packetTable.spellsActive[spellId], spellInstance)
            end
        end
    elseif packetType == "PlayerCooldowns" then
        packetTable.cooldowns = {}

        for changesIndex = 0, mwnet.GetCooldownChangesSize(pid) - 1 do

            local cooldown = {
                spellId = mwnet.GetCooldownSpellId(pid, changesIndex),
                startDay = mwnet.GetCooldownStartDay(pid, changesIndex),
                startHour = mwnet.GetCooldownStartHour(pid, changesIndex)
            }

            table.insert(packetTable.cooldowns, cooldown)
        end
    elseif packetType == "PlayerQuickKeys" then
        packetTable.quickKeys = {}

        for changesIndex = 0, mwnet.GetQuickKeyChangesSize(pid) - 1 do

            local slot = mwnet.GetQuickKeySlot(pid, changesIndex)

            packetTable.quickKeys[slot] = {
                keyType = mwnet.GetQuickKeyType(pid, changesIndex),
                itemId = mwnet.GetQuickKeyItemId(pid, changesIndex)
            }
        end
    elseif packetType == "PlayerJournal" then
        packetTable.journal = {}

        for changesIndex = 0, mwnet.GetJournalChangesSize(pid) - 1 do
            local journalItem = {
                type = mwnet.GetJournalItemType(pid, changesIndex),
                index = mwnet.GetJournalItemIndex(pid, changesIndex),
                quest = mwnet.GetJournalItemQuest(pid, changesIndex),
                timestamp = {
                    daysPassed = WorldInstance.data.time.daysPassed,
                    month = WorldInstance.data.time.month,
                    day = WorldInstance.data.time.day
                }
            }

            if journalItem.type == enumerations.journal.ENTRY then
                journalItem.actorRefId = mwnet.GetJournalItemActorRefId(pid, changesIndex)
            end

            table.insert(packetTable.journal, journalItem)
        end
    end

    return packetTable
end

packetReader.GetActorPacketTables = function(packetType)

    local packetTables = { actors = {} }
    local actorListSize = mwnet.GetActorListSize()

    if actorListSize == 0 then return packetTables end

    for packetIndex = 0, actorListSize - 1 do
        local actor = {}
        local uniqueIndex = mwnet.GetActorRefNum(packetIndex) .. "-" .. mwnet.GetActorMpNum(packetIndex)
        actor.uniqueIndex = uniqueIndex

        -- Only non-repetitive actor packets contain refId information
        if tableHelper.containsValue({"ActorList", "ActorDeath"}, packetType) then
            actor.refId = mwnet.GetActorRefId(packetIndex)
        end

        if packetType == "ActorEquipment" then

            actor.equipment = {}
            local equipmentSize = mwnet.GetEquipmentSize()

            for itemIndex = 0, equipmentSize - 1 do
                local itemRefId = mwnet.GetActorEquipmentItemRefId(packetIndex, itemIndex)

                if itemRefId ~= "" then
                    actor.equipment[itemIndex] = {
                        refId = itemRefId,
                        count = mwnet.GetActorEquipmentItemCount(packetIndex, itemIndex),
                        charge = mwnet.GetActorEquipmentItemCharge(packetIndex, itemIndex),
                        enchantmentCharge = mwnet.GetActorEquipmentItemEnchantmentCharge(packetIndex, itemIndex)
                    }
                end
            end
        elseif packetType == "ActorSpellsActive" then

            actor.spellsActive = {}
            local spellsActiveChangesSize = mwnet.GetActorSpellsActiveChangesSize(packetIndex)

            for spellIndex = 0, spellsActiveChangesSize - 1 do

                local spellId = mwnet.GetActorSpellsActiveId(packetIndex, spellIndex)

                if actor.spellsActive[spellId] == nil then
                    actor.spellsActive[spellId] = {}
                end

                actor.spellActiveChangesAction = mwnet.GetActorSpellsActiveChangesAction(packetIndex)

                local spellInstance = {
                    effects = {},
                    displayName = mwnet.GetActorSpellsActiveDisplayName(packetIndex, spellIndex),
                    stackingState = mwnet.GetActorSpellsActiveStackingState(packetIndex, spellIndex),
                    startTime = os.time(),
                    caster = {}
                }

                spellInstance.hasPlayerCaster = mwnet.DoesActorSpellsActiveHavePlayerCaster(packetIndex, spellIndex)

                if spellInstance.hasPlayerCaster == true then
                    spellInstance.caster.pid = mwnet.GetActorSpellsActiveCasterPid(packetIndex, spellIndex)

                    if Players[spellInstance.caster.pid] ~= nil then
                        spellInstance.caster.playerName = Players[spellInstance.caster.pid].accountName
                    end
                else
                    spellInstance.caster.uniqueIndex = mwnet.GetActorSpellsActiveCasterRefNum(packetIndex, spellIndex) ..
                        "-" .. mwnet.GetSpellsActiveCasterMpNum(packetIndex, spellIndex)
                    spellInstance.caster.refId = mwnet.GetActorSpellsActiveCasterRefId(packetIndex, spellIndex)
                end

                for effectIndex = 0, mwnet.GetActorSpellsActiveEffectCount(packetIndex, spellIndex) - 1 do
                    local effect = {
                        id = mwnet.GetActorSpellsActiveEffectId(packetIndex, spellIndex, effectIndex),
                        magnitude = mwnet.GetActorSpellsActiveEffectMagnitude(packetIndex, spellIndex, effectIndex),
                        duration = mwnet.GetActorSpellsActiveEffectDuration(packetIndex, spellIndex, effectIndex),
                        timeLeft = mwnet.GetActorSpellsActiveEffectTimeLeft(packetIndex, spellIndex, effectIndex),
                        arg = mwnet.GetActorSpellsActiveEffectArg(packetIndex, spellIndex, effectIndex)
                    }

                    if effect.timeLeft > 0 then
                        table.insert(spellInstance.effects, effect)
                    end
                end

                if tableHelper.getCount(spellInstance.effects) > 0 then
                    table.insert(actor.spellsActive[spellId], spellInstance)
                end
            end
        elseif packetType == "ActorDeath" then

            actor.deathState = mwnet.GetActorDeathState(packetIndex)
            actor.killer = {}

            local doesActorHavePlayerKiller = mwnet.DoesActorHavePlayerKiller(packetIndex)

            if doesActorHavePlayerKiller then
                actor.killer.pid = mwnet.GetActorKillerPid(packetIndex)

                if Players[actor.killer.pid] ~= nil then
                    actor.killer.playerName = Players[actor.killer.pid].accountName
                end
            else
                actor.killer.refId = mwnet.GetActorKillerRefId(packetIndex)
                actor.killer.name = mwnet.GetActorKillerName(packetIndex)
                actor.killer.uniqueIndex = mwnet.GetActorKillerRefNum(packetIndex) ..
                    "-" .. mwnet.GetActorKillerMpNum(packetIndex)
            end
        end

        packetTables.actors[uniqueIndex] = actor
    end

    return packetTables
end

packetReader.GetObjectPacketTables = function(packetType)

    local packetTables = { objects = {}, players = {} }
    local objectListSize = mwnet.GetObjectListSize()

    if objectListSize == 0 then return packetTables end

    for packetIndex = 0, objectListSize - 1 do
        local object, uniqueIndex, player, pid = nil, nil, nil, nil

        if tableHelper.containsValue({"ObjectActivate", "ObjectHit", "ObjectSound", "ConsoleCommand"}, packetType) then

            local isObjectPlayer = mwnet.IsObjectPlayer(packetIndex)

            if isObjectPlayer then
                pid = mwnet.GetObjectPid(packetIndex)
                player = Players[pid]
            else
                object = {}
                uniqueIndex = mwnet.GetObjectRefNum(packetIndex) .. "-" .. mwnet.GetObjectMpNum(packetIndex)
                object.refId = mwnet.GetObjectRefId(packetIndex)
                object.uniqueIndex = uniqueIndex
            end

            if packetType == "ObjectSound" then

                local soundId = mwnet.GetObjectSoundId(packetIndex)

                if isObjectPlayer then
                    player.soundId = soundId
                else
                    object.soundId = soundId
                end

            elseif packetType == "ObjectActivate" then

                local doesObjectHaveActivatingPlayer = mwnet.DoesObjectHavePlayerActivating(packetIndex)

                if doesObjectHaveActivatingPlayer then
                    local activatingPid = mwnet.GetObjectActivatingPid(packetIndex)

                    if isObjectPlayer then
                        player.activatingPid = activatingPid
                        player.drawState = mwnet.GetDrawState(activatingPid) -- for backwards compatibility
                    else
                        object.activatingPid = activatingPid
                    end
                else
                    local activatingRefId = mwnet.GetObjectActivatingRefId(packetIndex)
                    local activatingUniqueIndex = mwnet.GetObjectActivatingRefNum(packetIndex) ..
                        "-" .. mwnet.GetObjectActivatingMpNum(packetIndex)

                    if isObjectPlayer then
                        player.activatingRefId = activatingRefId
                        player.activatingUniqueIndex = activatingUniqueIndex
                    else
                        object.activatingRefId = activatingRefId
                        object.activatingUniqueIndex = activatingUniqueIndex
                    end
                end

            elseif packetType == "ObjectHit" then

                local hit = {
                    success = mwnet.GetObjectHitSuccess(packetIndex),
                    damage = mwnet.GetObjectHitDamage(packetIndex),
                    block = mwnet.GetObjectHitBlock(packetIndex),
                    knockdown = mwnet.GetObjectHitKnockdown(packetIndex)
                }

                if isObjectPlayer then
                    player.hit = hit
                else
                    object.hit = hit
                end

                local doesObjectHaveHittingPlayer = mwnet.DoesObjectHavePlayerHitting(packetIndex)

                if doesObjectHaveHittingPlayer then
                    local hittingPid = mwnet.GetObjectHittingPid(packetIndex)

                    if isObjectPlayer then
                        player.hittingPid = hittingPid
                    else
                        object.hittingPid = hittingPid
                    end
                else
                    local hittingRefId = mwnet.GetObjectHittingRefId(packetIndex)
                    local hittingUniqueIndex = mwnet.GetObjectHittingRefNum(packetIndex) ..
                        "-" .. mwnet.GetObjectHittingMpNum(packetIndex)

                    if isObjectPlayer then
                        player.hittingRefId = hittingRefId
                        player.hittingUniqueIndex = hittingUniqueIndex
                    else
                        object.hittingRefId = hittingRefId
                        object.hittingUniqueIndex = hittingUniqueIndex
                    end
                end
            end
        else
            object = {}
            uniqueIndex = mwnet.GetObjectRefNum(packetIndex) .. "-" .. mwnet.GetObjectMpNum(packetIndex)
            object.refId = mwnet.GetObjectRefId(packetIndex)
            object.uniqueIndex = uniqueIndex

            if tableHelper.containsValue({"ObjectPlace", "ObjectSpawn"}, packetType) then

                object.location = {
                    posX = mwnet.GetObjectPosX(packetIndex), posY = mwnet.GetObjectPosY(packetIndex),
                    posZ = mwnet.GetObjectPosZ(packetIndex), rotX = mwnet.GetObjectRotX(packetIndex),
                    rotY = mwnet.GetObjectRotY(packetIndex), rotZ = mwnet.GetObjectRotZ(packetIndex)
                }

                if packetType == "ObjectPlace" then
                    object.count = mwnet.GetObjectCount(packetIndex)
                    object.charge = mwnet.GetObjectCharge(packetIndex)
                    object.enchantmentCharge = mwnet.GetObjectEnchantmentCharge(packetIndex)
                    object.soul = mwnet.GetObjectSoul(packetIndex)
                    object.goldValue = mwnet.GetObjectGoldValue(packetIndex)
                    object.hasContainer = mwnet.DoesObjectHaveContainer(packetIndex)
                    object.droppedByPlayer = mwnet.IsObjectDroppedByPlayer(packetIndex)
                elseif packetType == "ObjectSpawn" then
                    local summonState = mwnet.GetObjectSummonState(packetIndex)

                    if summonState == true then
                        object.summon = {}
                        object.summon.effectId = mwnet.GetObjectSummonEffectId(packetIndex)
                        object.summon.spellId = mwnet.GetObjectSummonSpellId(packetIndex)
                        object.summon.duration = mwnet.GetObjectSummonDuration(packetIndex)
                        object.summon.startTime = os.time()
                        object.summon.summoner = {}
                        object.summon.hasPlayerSummoner = mwnet.DoesObjectHavePlayerSummoner(packetIndex)

                        if object.summon.hasPlayerSummoner == true then
                            object.summon.summoner.pid = mwnet.GetObjectSummonerPid(packetIndex)

                            if Players[object.summon.summoner.pid] ~= nil then
                                object.summon.summoner.playerName = Players[object.summon.summoner.pid].accountName
                            end
                        else
                            object.summon.summoner.refId = mwnet.GetObjectSummonerRefId(packetIndex)
                            object.summon.summoner.uniqueIndex = mwnet.GetObjectSummonerRefNum(packetIndex) ..
                                "-" .. mwnet.GetObjectSummonerMpNum(packetIndex)
                        end
                    end
                end

            elseif packetType == "ObjectLock" then
                object.lockLevel = mwnet.GetObjectLockLevel(packetIndex)
            elseif packetType == "ObjectDialogueChoice" then
                object.dialogueChoiceType = mwnet.GetObjectDialogueChoiceType(packetIndex)

                if object.dialogueChoiceType == enumerations.dialogueChoice.TOPIC then
                    object.dialogueTopic = mwnet.GetObjectDialogueChoiceTopic(packetIndex)
                end
            elseif packetType == "ObjectMiscellaneous" then
                object.goldPool = mwnet.GetObjectGoldPool(packetIndex)
                object.lastGoldRestockHour = mwnet.GetObjectLastGoldRestockHour(packetIndex)
                object.lastGoldRestockDay = mwnet.GetObjectLastGoldRestockDay(packetIndex)
            elseif packetType == "ObjectScale" then
                object.scale = mwnet.GetObjectScale(packetIndex)
            elseif packetType == "ObjectState" then
                object.state = mwnet.GetObjectState(packetIndex)
            elseif packetType == "ObjectMove" then
                object.location = {
                    posX = mwnet.GetObjectPosX(packetIndex),
                    posY = mwnet.GetObjectPosY(packetIndex),
                    posZ = mwnet.GetObjectPosZ(packetIndex)
                }
            elseif packetType == "ObjectRotate" then
                object.location = {
                    rotX = mwnet.GetObjectRotX(packetIndex),
                    rotY = mwnet.GetObjectRotY(packetIndex),
                    rotZ = mwnet.GetObjectRotZ(packetIndex)
                }
            elseif packetType == "DoorState" then
                object.doorState = mwnet.GetObjectDoorState(packetIndex)
            elseif packetType =="ClientScriptLocal" then

                local variables = {}
                local variableCount = mwnet.GetClientLocalsSize(packetIndex)

                for variableIndex = 0, variableCount - 1 do
                    local internalIndex = mwnet.GetClientLocalInternalIndex(packetIndex, variableIndex)
                    local variableType = mwnet.GetClientLocalVariableType(packetIndex, variableIndex)
                    local value

                    if tableHelper.containsValue({enumerations.variableType.SHORT, enumerations.variableType.LONG},
                        variableType) then
                        value = mwnet.GetClientLocalIntValue(packetIndex, variableIndex)
                    elseif variableType == enumerations.variableType.FLOAT then
                        value = mwnet.GetClientLocalFloatValue(packetIndex, variableIndex)
                    end

                    if variables[variableType] == nil then
                        variables[variableType] = {}
                    end

                    variables[variableType][internalIndex] = value
                end

                object.variables = variables
            end
        end

        if object ~= nil then
            packetTables.objects[uniqueIndex] = object
        elseif player ~= nil then
            packetTables.players[pid] = player
        end
    end

    return packetTables
end

packetReader.GetWorldMapTileArray = function()

    local mapTileArray = {}
    local mapTileCount = mwnet.GetMapChangesSize()

    for index = 0, mapTileCount - 1 do
        mapTile = {
            cellX = mwnet.GetMapTileCellX(index),
            cellY = mwnet.GetMapTileCellY(index),
        }

        mapTile.filename = mapTile.cellX .. ", " .. mapTile.cellY .. ".png"

        table.insert(mapTileArray, mapTile)
    end

    return mapTileArray
end

packetReader.GetClientScriptGlobalPacketTable = function()

    local variables = {}
    local variableCount = mwnet.GetClientGlobalsSize()

    for index = 0, variableCount - 1 do
        local id = mwnet.GetClientGlobalId(index)
        local variable = { variableType = mwnet.GetClientGlobalVariableType(index) }

        if tableHelper.containsValue({enumerations.variableType.SHORT, enumerations.variableType.LONG},
            variable.variableType) then
            variable.intValue = mwnet.GetClientGlobalIntValue(index)
        elseif variable.variableType == enumerations.variableType.FLOAT then
            variable.floatValue = mwnet.GetClientGlobalFloatValue(index)
        end

        variables[id] = variable
    end

    return variables
end

packetReader.GetRecordDynamicArray = function(pid)

    local recordArray = {}
    local recordCount = mwnet.GetRecordCount(pid)
    local recordNumericalType = mwnet.GetRecordType(pid)

    for recordIndex = 0, recordCount - 1 do
        local record = {}

        if recordNumericalType ~= enumerations.recordType.ENCHANTMENT then
            record.name = mwnet.GetRecordName(recordIndex)
        end

        if recordNumericalType == enumerations.recordType.SPELL then
            record.subtype = mwnet.GetRecordSubtype(recordIndex)
            record.cost = mwnet.GetRecordCost(recordIndex)
            record.flags = mwnet.GetRecordFlags(recordIndex)
            record.effects = packetReader.GetRecordPacketEffectArray(recordIndex)

        elseif recordNumericalType == enumerations.recordType.POTION then
            record.weight = math.floor(mwnet.GetRecordWeight(recordIndex) * 100) / 100
            record.value = mwnet.GetRecordValue(recordIndex)
            record.autoCalc = mwnet.GetRecordAutoCalc(recordIndex)
            record.icon = mwnet.GetRecordIcon(recordIndex)
            record.model = mwnet.GetRecordModel(recordIndex)
            record.script = mwnet.GetRecordScript(recordIndex)
            record.effects = packetReader.GetRecordPacketEffectArray(recordIndex)

            -- Temporary data that should be discarded afterwards
            record.quantity = mwnet.GetRecordQuantity(recordIndex)

        elseif recordNumericalType == enumerations.recordType.ENCHANTMENT then
            record.subtype = mwnet.GetRecordSubtype(recordIndex)
            record.cost = mwnet.GetRecordCost(recordIndex)
            record.charge = mwnet.GetRecordCharge(recordIndex)
            record.flags = mwnet.GetRecordFlags(recordIndex)
            record.effects = packetReader.GetRecordPacketEffectArray(recordIndex)

            -- Temporary data that should be discarded afterwards
            record.clientsideEnchantmentId = mwnet.GetRecordId(recordIndex)

        else
            record.baseId = mwnet.GetRecordBaseId(recordIndex)
            record.enchantmentCharge = mwnet.GetRecordEnchantmentCharge(recordIndex)

            -- Temporary data that should be discarded afterwards
            if recordNumericalType == enumerations.recordType.WEAPON then
                record.quantity = mwnet.GetRecordQuantity(recordIndex)
            else
                record.quantity = 1
            end

            -- Enchanted item records always have client-set ids for their enchantments
            -- when received by us, so we need to check for the server-set ids matching
            -- them in the player's unresolved enchantments
            local clientEnchantmentId = mwnet.GetRecordEnchantmentId(recordIndex)
            record.enchantmentId = Players[pid].unresolvedEnchantments[clientEnchantmentId]

            -- Stop tracking this as an unresolved enchantment, assuming the enchantment
            -- itself wasn't previously denied
            if record.enchantmentId ~= nil and Players[pid] ~= nil then
                Players[pid].unresolvedEnchantments[clientEnchantmentId] = nil
            end
        end

        table.insert(recordArray, record)
    end

    return recordArray
end

packetReader.GetRecordPacketEffectArray = function(recordIndex)

    local effectArray = {}
    local effectCount = mwnet.GetRecordEffectCount(recordIndex)

    for effectIndex = 0, effectCount - 1 do

        local effect = {
            id = mwnet.GetRecordEffectId(recordIndex, effectIndex),
            attribute = mwnet.GetRecordEffectAttribute(recordIndex, effectIndex),
            skill = mwnet.GetRecordEffectSkill(recordIndex, effectIndex),
            rangeType = mwnet.GetRecordEffectRangeType(recordIndex, effectIndex),
            area = mwnet.GetRecordEffectArea(recordIndex, effectIndex),
            duration = mwnet.GetRecordEffectDuration(recordIndex, effectIndex),
            magnitudeMin = mwnet.GetRecordEffectMagnitudeMin(recordIndex, effectIndex),
            magnitudeMax = mwnet.GetRecordEffectMagnitudeMax(recordIndex, effectIndex)
        }

        table.insert(effectArray, effect)
    end

    return effectArray
end

return packetReader
