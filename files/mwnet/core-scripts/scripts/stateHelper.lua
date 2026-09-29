StateHelper = class("StateHelper")

function StateHelper:LoadJournal(pid, stateObject)

    if stateObject.data.journal == nil then
        stateObject.data.journal = {}
    end

    mwnet.ClearJournalChanges(pid)

    for index, journalItem in pairs(stateObject.data.journal) do

        if journalItem.type == enumerations.journal.ENTRY then

            if journalItem.actorRefId == nil then
                journalItem.actorRefId = "player"
            end

            if journalItem.timestamp ~= nil then
                mwnet.AddJournalEntryWithTimestamp(pid, journalItem.quest, journalItem.index, journalItem.actorRefId,
                    journalItem.timestamp.daysPassed, journalItem.timestamp.month, journalItem.timestamp.day)
            else
                mwnet.AddJournalEntry(pid, journalItem.quest, journalItem.index, journalItem.actorRefId)
            end
        else
            mwnet.AddJournalIndex(pid, journalItem.quest, journalItem.index)
        end
    end

    mwnet.SendJournalChanges(pid)
end

function StateHelper:LoadFactionRanks(pid, stateObject)

    if stateObject.data.factionRanks == nil then
        stateObject.data.factionRanks = {}
    end

    mwnet.ClearFactionChanges(pid)
    mwnet.SetFactionChangesAction(pid, enumerations.faction.RANK)

    for factionId, rank in pairs(stateObject.data.factionRanks) do

        mwnet.SetFactionId(factionId)
        mwnet.SetFactionRank(rank)
        mwnet.AddFaction(pid)
    end

    mwnet.SendFactionChanges(pid)
end

function StateHelper:LoadFactionExpulsion(pid, stateObject)

    if stateObject.data.factionExpulsion == nil then
        stateObject.data.factionExpulsion = {}
    end

    mwnet.ClearFactionChanges(pid)
    mwnet.SetFactionChangesAction(pid, enumerations.faction.EXPULSION)

    for factionId, state in pairs(stateObject.data.factionExpulsion) do

        mwnet.SetFactionId(factionId)
        mwnet.SetFactionExpulsionState(state)
        mwnet.AddFaction(pid)
    end

    mwnet.SendFactionChanges(pid)
end

function StateHelper:LoadFactionReputation(pid, stateObject)

    if stateObject.data.factionReputation == nil then
        stateObject.data.factionReputation = {}
    end

    mwnet.ClearFactionChanges(pid)
    mwnet.SetFactionChangesAction(pid, enumerations.faction.REPUTATION)

    for factionId, reputation in pairs(stateObject.data.factionReputation) do

        mwnet.SetFactionId(factionId)
        mwnet.SetFactionReputation(reputation)
        mwnet.AddFaction(pid)
    end

    mwnet.SendFactionChanges(pid)
end

function StateHelper:LoadTopics(pid, stateObject)

    if stateObject.data.topics == nil then
        stateObject.data.topics = {}
    end

    mwnet.ClearTopicChanges(pid)

    for index, topicId in pairs(stateObject.data.topics) do

        mwnet.AddTopic(pid, topicId)
    end

    mwnet.SendTopicChanges(pid)
end

function StateHelper:LoadBounty(pid, stateObject)

    if stateObject.data.fame == nil then
        stateObject.data.fame = { bounty = 0, reputation = 0 }
    elseif stateObject.data.fame.bounty == nil then
        stateObject.data.fame.bounty = 0
    end

    -- Update old player files to the new format
    if stateObject.data.stats ~= nil and stateObject.data.stats.bounty ~= nil then
        stateObject.data.fame.bounty = stateObject.data.stats.bounty
        stateObject.data.stats.bounty = nil
    end

    mwnet.SetBounty(pid, stateObject.data.fame.bounty)
    mwnet.SendBounty(pid)
end

function StateHelper:LoadReputation(pid, stateObject)

    if stateObject.data.fame == nil then
        stateObject.data.fame = { bounty = 0, reputation = 0 }
    elseif stateObject.data.fame.reputation == nil then
        stateObject.data.fame.reputation = 0
    end

    mwnet.SetReputation(pid, stateObject.data.fame.reputation)
    mwnet.SendReputation(pid)
end

function StateHelper:LoadClientScriptVariables(pid, stateObject)

    if stateObject.data.clientVariables == nil then
        stateObject.data.clientVariables = {}
    end

    if stateObject.data.clientVariables.globals == nil then
        stateObject.data.clientVariables.globals = {}
    end

    local variableCount = 0

    mwnet.ClearClientGlobals()

    for variableId, variableTable in pairs(stateObject.data.clientVariables.globals) do

        if type(variableTable) == "table" then

            if variableTable.variableType == enumerations.variableType.SHORT then
                mwnet.AddClientGlobalInteger(variableId, variableTable.intValue, enumerations.variableType.SHORT)
            elseif variableTable.variableType == enumerations.variableType.LONG then
                mwnet.AddClientGlobalInteger(variableId, variableTable.intValue, enumerations.variableType.LONG)
            elseif variableTable.variableType == enumerations.variableType.FLOAT then
                mwnet.AddClientGlobalFloat(variableId, variableTable.floatValue)
            end

            variableCount = variableCount + 1
        end
    end

    if variableCount > 0 then
        mwnet.SendClientScriptGlobal(pid)
    end
end

function StateHelper:LoadDestinationOverrides(pid, stateObject)

    if stateObject.data.destinationOverrides == nil then
        stateObject.data.destinationOverrides = {}
    end

    local destinationCount = 0

    mwnet.ClearDestinationOverrides()

    for oldCellDescription, newCellDescription in pairs(stateObject.data.destinationOverrides) do

        mwnet.AddDestinationOverride(oldCellDescription, newCellDescription)
        destinationCount = destinationCount + 1
    end

    if destinationCount > 0 then
        mwnet.SendWorldDestinationOverride(pid)
    end
end

function StateHelper:LoadMap(pid, stateObject)

    if stateObject.data.mapExplored == nil then
        stateObject.data.mapExplored = {}
    end

    local tileCount = 0
    mwnet.ClearMapChanges()

    for index, cellDescription in pairs(stateObject.data.mapExplored) do

        local filePath = config.dataPath .. "/map/" .. cellDescription .. ".png"

        if mwnet.DoesFilePathExist(filePath) then

            local cellX, cellY
            _, _, cellX, cellY = string.find(cellDescription, patterns.exteriorCell)
            cellX = tonumber(cellX)
            cellY = tonumber(cellY)

            if type(cellX) == "number" and type(cellY) == "number" then
                mwnet.LoadMapTileImageFile(cellX, cellY, filePath)
                tileCount = tileCount + 1
            end
        end
    end

    if tileCount > 0 then
        mwnet.SendWorldMap(pid)
    end
end

function StateHelper:SaveJournal(stateObject, playerPacket)

    if stateObject.data.journal == nil then
        stateObject.data.journal = {}
    end

    if stateObject.data.customVariables == nil then
        stateObject.data.customVariables = {}
    end

    for _, journalItem in ipairs(playerPacket.journal) do

        table.insert(stateObject.data.journal, journalItem)

        if journalItem.quest == "a1_1_findspymaster" and journalItem.index >= 14 then
            stateObject.data.customVariables.deliveredCaiusPackage = true
        end
    end

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveFactionRanks(pid, stateObject)

    if stateObject.data.factionRanks == nil then
        stateObject.data.factionRanks = {}
    end

    for i = 0, mwnet.GetFactionChangesSize(pid) - 1 do

        local factionId = mwnet.GetFactionId(pid, i)
        stateObject.data.factionRanks[factionId] = mwnet.GetFactionRank(pid, i)
    end

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveFactionExpulsion(pid, stateObject)

    if stateObject.data.factionExpulsion == nil then
        stateObject.data.factionExpulsion = {}
    end

    for i = 0, mwnet.GetFactionChangesSize(pid) - 1 do

        local factionId = mwnet.GetFactionId(pid, i)
        stateObject.data.factionExpulsion[factionId] = mwnet.GetFactionExpulsionState(pid, i)
    end

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveFactionReputation(pid, stateObject)

    if stateObject.data.factionReputation == nil then
        stateObject.data.factionReputation = {}
    end

    for i = 0, mwnet.GetFactionChangesSize(pid) - 1 do

        local factionId = mwnet.GetFactionId(pid, i)
        stateObject.data.factionReputation[factionId] = mwnet.GetFactionReputation(pid, i)
    end

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveTopics(pid, stateObject)

    if stateObject.data.topics == nil then
        stateObject.data.topics = {}
    end

    for i = 0, mwnet.GetTopicChangesSize(pid) - 1 do

        local topicId = mwnet.GetTopicId(pid, i)

        if not tableHelper.containsValue(stateObject.data.topics, topicId) then
            table.insert(stateObject.data.topics, topicId)
        end
    end

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveBounty(pid, stateObject)

    if stateObject.data.fame == nil then
        stateObject.data.fame = {}
    end

    stateObject.data.fame.bounty = mwnet.GetBounty(pid)

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveReputation(pid, stateObject)

    if stateObject.data.fame == nil then
        stateObject.data.fame = {}
    end

    stateObject.data.fame.reputation = mwnet.GetReputation(pid)

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveClientScriptGlobal(stateObject, variables)

    if stateObject.data.clientVariables == nil then
        stateObject.data.clientVariables = {}
    end

    if stateObject.data.clientVariables.globals == nil then
        stateObject.data.clientVariables.globals = {}
    end

    for id, variable in pairs (variables) do
        stateObject.data.clientVariables.globals[id] = variable
    end

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveMapExploration(pid, stateObject)

    local cell = mwnet.GetCell(pid)

    if mwnet.IsInExterior(pid) == true then
        if not tableHelper.containsValue(stateObject.data.mapExplored, cell) then
            table.insert(stateObject.data.mapExplored, cell)
        end
    end
end

return StateHelper
