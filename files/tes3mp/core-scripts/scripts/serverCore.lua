require("utils")
require("enumerations")
tableHelper = require("tableHelper")
class = require("classy")
jsonInterface = require("jsonInterface")

-- Lua's default io library for input/output can't open Unicode filenames on Windows,
-- which is why on Windows it's replaced by TES3MP's io2 (https://github.com/TES3MP/Lua-io2)
if tes3mp.GetOperatingSystemType() == "Windows" then
    jsonInterface.setLibrary(require("io2"))
else
    jsonInterface.setLibrary(io)
end

require("color")
require("config")
require("time")

customEventHooks = require("customEventHooks")
customCommandHooks = require("customCommandHooks")
logicHandler = require("logicHandler")
eventHandler = require("eventHandler")
guiHelper = require("guiHelper")
animHelper = require("animHelper")
speechHelper = require("speechHelper")
menuHelper = require("menuHelper")
require("defaultCommands")
require("customScripts")

Database = nil
Player = nil
Cell = nil
RecordStore = nil
World = nil

pidsByIpAddress = {}
clientDataFiles = {}
clientVariableScopes = {}
speechCollections = {}
authenticatedPlayers = {}

hourCounter = nil
updateTimerId = nil

banList = {}

function LoadModerationRules()
    local moderation = jsonInterface.load(config.moderationFile)

    if type(moderation) ~= "table" or moderation.schemaVersion ~= 1 or
        type(moderation.disallowedNameStrings) ~= "table" then
        config.disallowedNameStrings = {}
        tes3mp.LogMessage(enumerations.log.WARN,
            "Moderation rules are missing or invalid; no name terms were loaded")
        return
    end

    local validatedTerms = {}
    for _, term in ipairs(moderation.disallowedNameStrings) do
        if type(term) == "string" and string.len(term) > 0 and string.len(term) <= 64 then
            table.insert(validatedTerms, term)
        end

        if #validatedTerms >= 4096 then
            break
        end
    end

    config.disallowedNameStrings = validatedTerms
    tes3mp.LogMessage(enumerations.log.INFO,
        "Loaded " .. #validatedTerms .. " private moderation name rules")
end

if (config.databaseType ~= nil and config.databaseType ~= "json") and doesModuleExist("luasql." .. config.databaseType) then

    Database = require("database")
    Database:LoadDriver(config.databaseType)

    tes3mp.LogMessage(enumerations.log.INFO, "Using " .. Database.driver._VERSION .. " with " .. config.databaseType ..
        " driver")

    Database:Connect(config.databasePath)

    -- Make sure we enable foreign keys
    Database:Execute("PRAGMA foreign_keys = ON;")

    Database:CreatePlayerTables()
    Database:CreateWorldTables()

    Player = require("player.sql")
    Cell = require("cell.sql")
    RecordStore = require("recordstore.sql")
    World = require("world.sql")
else
    Player = require("player.json")
    Cell = require("cell.json")
    RecordStore = require("recordstore.json")
    World = require("world.json")
end

function LoadBanList()
    tes3mp.LogMessage(enumerations.log.INFO, "Reading banlist.json")
    banList = jsonInterface.load("banlist.json")

    if banList.playerNames == nil then
        banList.playerNames = {}
    elseif banList.ipAddresses == nil then
        banList.ipAddresses = {}
    end

    if #banList.ipAddresses > 0 then
        local message = "- Banning manually-added IP addresses:\n"

        for index, ipAddress in pairs(banList.ipAddresses) do
            message = message .. ipAddress

            if index < #banList.ipAddresses then
                message = message .. ", "
            end

            tes3mp.BanAddress(ipAddress)
        end

        tes3mp.LogAppend(enumerations.log.WARN, message)
    end

    if #banList.playerNames > 0 then
        local message = "- Banning all IP addresses stored for players:\n"

        for index, targetName in pairs(banList.playerNames) do
            message = message .. targetName

            if index < #banList.playerNames then
                message = message .. ", "
            end

            local targetPlayer = logicHandler.GetPlayerByName(targetName)

            if targetPlayer ~= nil then

                for index, ipAddress in pairs(targetPlayer.data.ipAddresses) do
                    tes3mp.BanAddress(ipAddress)
                end
            end
        end

        tes3mp.LogAppend(enumerations.log.WARN, message)
    end
end

function SaveBanList()
    jsonInterface.save("banlist.json", banList)
end

do
    local previousHourFloor = nil

    function UpdateTime()

        if config.passTimeWhenEmpty or tableHelper.getCount(Players) > 0 then

            hourCounter = hourCounter + (0.0083 * WorldInstance.frametimeMultiplier)

            local hourFloor = math.floor(hourCounter)

            if previousHourFloor == nil then
                previousHourFloor = hourFloor

            elseif hourFloor > previousHourFloor then

                if hourFloor >= 24 then

                    hourCounter = hourCounter - hourFloor
                    hourFloor = 0

                    tes3mp.LogMessage(enumerations.log.INFO, "The world time day has been incremented")
                    WorldInstance:IncrementDay()
                end

                tes3mp.LogMessage(enumerations.log.INFO, "The world time hour is now " .. hourFloor)
                WorldInstance.data.time.hour = hourCounter

                WorldInstance:UpdateFrametimeMultiplier()

                if tableHelper.getCount(Players) > 0 then
                    WorldInstance:LoadTime(tableHelper.getAnyValue(Players).pid, true)
                end

                previousHourFloor = hourFloor
            end
        end

        tes3mp.RestartTimer(updateTimerId, time.seconds(1))
    end
end

function OnServerInit()

    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnServerInit\"")

    local expectedVersionPrefix = "1.0.0"
    local serverVersion = tes3mp.GetServerVersion()

    if string.sub(serverVersion, 1, string.len(expectedVersionPrefix)) ~= expectedVersionPrefix then
        tes3mp.LogAppend(enumerations.log.ERROR, "- Version mismatch between server and Core scripts!")
        tes3mp.LogAppend(enumerations.log.ERROR, "- The Core scripts require a server version that starts with " ..
            expectedVersionPrefix)
        tes3mp.StopServer(1)
    end
    
    local eventStatus = customEventHooks.triggerValidators("OnServerInit", {})

    if eventStatus.validDefaultHandler then
        logicHandler.InitializeWorld()

        for priorityLevel, recordStoreTypes in ipairs(config.recordStoreLoadOrder) do
            for _, storeType in ipairs(recordStoreTypes) do
                logicHandler.LoadRecordStore(storeType)
            end
        end

        hourCounter = WorldInstance.data.time.hour
        WorldInstance:UpdateFrametimeMultiplier()

        updateTimerId = tes3mp.CreateTimer("UpdateTime", time.seconds(1))
        tes3mp.StartTimer(updateTimerId)

        logicHandler.PushPlayerList(Players)

        LoadModerationRules()
        LoadBanList()

        tes3mp.SetDataFileEnforcementState(config.enforceDataFiles)
        tes3mp.SetScriptErrorIgnoringState(config.ignoreScriptErrors)
    end

    customEventHooks.triggerHandlers("OnServerInit", eventStatus, {})
end

function OnServerPostInit()
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnServerPostInit\"")
    local eventStatus = customEventHooks.triggerValidators("OnServerPostInit", {})
    if eventStatus.validDefaultHandler then

        clientVariableScopes = require("clientVariableScopes")
        speechCollections = require("speechCollections")

        eventHandler.InitializeDefaultValidators()
        eventHandler.InitializeDefaultHandlers()

        tes3mp.SetGameMode(config.gameMode)

        local consoleRuleString = "allowed"
        if not config.allowConsole then
            consoleRuleString = "not " .. consoleRuleString
        end

        local bedRestRuleString = "allowed"
        if not config.allowBedRest then
            bedRestRuleString = "not " .. bedRestRuleString
        end

        local wildRestRuleString = "allowed"
        if not config.allowWildernessRest then
            wildRestRuleString = "not " .. wildRestRuleString
        end

        local waitRuleString = "allowed"
        if not config.allowWait then
            waitRuleString = "not " .. waitRuleString
        end

        tes3mp.SetRuleString("enforceDataFiles", tostring(config.enforceDataFiles))
        tes3mp.SetRuleString("ignoreScriptErrors", tostring(config.ignoreScriptErrors))
        tes3mp.SetRuleValue("difficulty", config.difficulty)
        tes3mp.SetRuleValue("deathPenaltyJailDays", config.deathPenaltyJailDays)
        tes3mp.SetRuleString("console", consoleRuleString)
        tes3mp.SetRuleString("bedResting", bedRestRuleString)
        tes3mp.SetRuleString("wildernessResting", wildRestRuleString)
        tes3mp.SetRuleString("waiting", waitRuleString)
        tes3mp.SetRuleValue("enforcedLogLevel", config.enforcedLogLevel)
        tes3mp.SetRuleValue("physicsFramerate", config.physicsFramerate)
        tes3mp.SetRuleString("shareJournal", tostring(config.shareJournal))
        tes3mp.SetRuleString("shareFactionRanks", tostring(config.shareFactionRanks))
        tes3mp.SetRuleString("shareFactionExpulsion", tostring(config.shareFactionExpulsion))
        tes3mp.SetRuleString("shareFactionReputation", tostring(config.shareFactionReputation))
        tes3mp.SetRuleString("shareTopics", tostring(config.shareTopics))
        tes3mp.SetRuleString("shareBounty", tostring(config.shareBounty))
        tes3mp.SetRuleString("shareReputation", tostring(config.shareReputation))
        tes3mp.SetRuleString("shareMapExploration", tostring(config.shareMapExploration))
        tes3mp.SetRuleString("enablePlacedObjectCollision", tostring(config.enablePlacedObjectCollision))

        local respawnCell

        if config.respawnAtImperialShrine == true then
            respawnCell = "nearest Imperial shrine"

            if config.respawnAtTribunalTemple == true then
                respawnCell = respawnCell .. " or Tribunal temple"
            end
        elseif config.respawnAtTribunalTemple == true then
            respawnCell = "nearest Tribunal temple"
        else
            respawnCell = config.defaultRespawn.cellDescription
        end

        tes3mp.SetRuleString("respawnCell", respawnCell)
    end
    customEventHooks.triggerHandlers("OnServerPostInit", eventStatus, {})
end

function OnServerExit(errorState)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnServerExit\"")
    tes3mp.LogMessage(enumerations.log.ERROR, "Error state: " .. tostring(errorState))
    customEventHooks.triggerHandlers("OnServerExit", customEventHooks.makeEventStatus(true, true) , {errorState})
end

function OnServerScriptCrash(errorMessage)
    tes3mp.LogMessage(enumerations.log.ERROR, "Server crash from script error!")
    customEventHooks.triggerHandlers("OnServerExit", customEventHooks.makeEventStatus(true, true), {errorMessage})
end

function LoadDataFileList(filename)
    local dataFileList = {}
    tes3mp.LogMessage(enumerations.log.INFO, "Reading " .. filename)

    local jsonDataFileList = jsonInterface.load(filename)

    if jsonDataFileList == nil then
        tes3mp.LogMessage(enumerations.log.ERROR, "Data file list " .. filename .. " cannot be read!")
        tes3mp.StopServer(2)
    else
        -- Fix numerical keys to print plugins in the correct order
        tableHelper.fixNumericalKeys(jsonDataFileList, true)

        for listIndex, pluginEntry in ipairs(jsonDataFileList) do
            for entryIndex, checksumStringArray in pairs(pluginEntry) do

                dataFileList[listIndex] = {}
                dataFileList[listIndex].name = entryIndex

                local checksums = {}
                local debugMessage = ("- %d: \"%s\": ["):format(listIndex, entryIndex)

                for _, checksumString in ipairs(checksumStringArray) do

                    debugMessage = debugMessage .. ("%X, "):format(tonumber(checksumString, 16))
                    table.insert(checksums, tonumber(checksumString, 16))
                end
                dataFileList[listIndex].checksums = checksums
                table.insert(dataFileList[listIndex], "")

                debugMessage = debugMessage .. "\b\b]"
                tes3mp.LogAppend(enumerations.log.WARN, debugMessage)
            end
        end

        return dataFileList
    end
end

function OnRequestDataFileList()

    local dataFileList = LoadDataFileList("requiredDataFiles.json")

    for _, entry in ipairs(dataFileList) do
        local name = entry.name
        table.insert(clientDataFiles, name)

        if tableHelper.isEmpty(entry.checksums) then
            tes3mp.AddDataFileRequirement(name, "")
        else
            for _, checksum in ipairs(entry.checksums) do
                tes3mp.AddDataFileRequirement(name, checksum)
            end
        end
    end
end

-- Older server builds will call an "OnRequestPluginList" event instead of
-- "OnRequestDataFileList", so keep this around for backwards compatibility
function OnRequestPluginList()
    OnRequestDataFileList()
end

function OnTransportConnect(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Transport connected for pid " .. pid)
end

function OnPlayerAuthenticated(pid, accountName, isNewAccount)
    authenticatedPlayers[pid] = {
        accountName = accountName,
        isNewAccount = isNewAccount
    }
    tes3mp.LogMessage(enumerations.log.INFO, "Account authenticated for pid " .. pid)
end

function OnPlayerConnect(pid)

    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerConnect\" for pid " .. pid)

    local nativeAuthentication = authenticatedPlayers[pid]
    authenticatedPlayers[pid] = nil
    local playerName = nativeAuthentication ~= nil and nativeAuthentication.accountName or tes3mp.GetName(pid)

    if string.len(playerName) > 35 then
        playerName = string.sub(playerName, 0, 35)
    end

    if not logicHandler.IsNameAllowed(playerName) then
        local message = playerName .. " (" .. pid .. ") " .. "joined and tried to use a disallowed name.\n"
        tes3mp.SendMessage(pid, message, true)
        tes3mp.Kick(pid)
    elseif logicHandler.IsPlayerNameLoggedIn(playerName) then
        local message = playerName .. " (" .. pid .. ") " .. "joined and tried to use an existing player's name.\n"
        tes3mp.SendMessage(pid, message, true)
        tes3mp.Kick(pid)
    else
        tes3mp.LogAppend(enumerations.log.INFO, "- New player is named " .. playerName)
        eventHandler.OnPlayerConnect(pid, playerName, nativeAuthentication)
    end
end

function OnPlayerDisconnect(pid)

    authenticatedPlayers[pid] = nil

    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerDisconnect\" for " .. logicHandler.GetChatName(pid))

    eventHandler.OnPlayerDisconnect(pid)
end

function OnPlayerResurrect(pid)
    customEventHooks.triggerHandlers("OnPlayerResurrect", customEventHooks.makeEventStatus(true, true), {pid})
end

function OnPlayerSendMessage(pid, message)
    eventHandler.OnPlayerSendMessage(pid, message)
end

function OnPlayerDeath(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerDeath\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerDeath(pid)
end

function OnPlayerAttribute(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerAttribute\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerAttribute(pid)
end

function OnPlayerSkill(pid)
    eventHandler.OnPlayerSkill(pid)
end

function OnPlayerLevel(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerLevel\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerLevel(pid)
end

function OnPlayerShapeshift(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerShapeshift\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerShapeshift(pid)
end

function OnPlayerCellChange(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerCellChange\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerCellChange(pid)
end

function OnPlayerEquipment(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerEquipment\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerEquipment(pid)
end

function OnPlayerEquipmentIntent(pid)
    return eventHandler.OnPlayerEquipmentIntent(pid)
end

function OnPlayerEquipmentIntentRejected(pid)
    eventHandler.OnPlayerEquipmentIntentRejected(pid)
end

function OnPlayerInventory(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerInventory\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerInventory(pid)
end

function OnPlayerInventoryIntent(pid)
    return eventHandler.OnPlayerInventoryIntent(pid)
end

function OnPlayerInventoryIntentRejected(pid)
    eventHandler.OnPlayerInventoryIntentRejected(pid)
end

function OnPlayerAttackIntent(pid, isRanged, targetPid, refNum, mpNum, strength)
    return eventHandler.OnPlayerAttackIntent(pid, isRanged, targetPid, refNum, mpNum, strength)
end

function OnPlayerAttackIntentRejected(pid, reason)
    eventHandler.OnPlayerAttackIntentRejected(pid, reason)
end

function OnPlayerCastIntent(pid, isItem, pressed, targetPid, refNum, mpNum)
    return eventHandler.OnPlayerCastIntent(pid, isItem, pressed, targetPid, refNum, mpNum)
end

function OnPlayerCastIntentRejected(pid, reason)
    eventHandler.OnPlayerCastIntentRejected(pid, reason)
end

function OnPlayerSpellbook(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerSpellbook\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerSpellbook(pid)
end

function OnPlayerSpellsActive(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerSpellsActive\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerSpellsActive(pid)
end

function OnPlayerSpellsActiveIntent(pid)
    return eventHandler.OnPlayerSpellsActiveIntent(pid)
end

function OnPlayerSpellsActiveIntentRejected(pid, reason)
    eventHandler.OnPlayerSpellsActiveIntentRejected(pid, reason)
end

function OnPlayerCooldowns(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerCooldowns\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerCooldowns(pid)
end

function OnPlayerQuickKeys(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerQuickKeys\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerQuickKeys(pid)
end

function OnPlayerJournal(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerJournal\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerJournal(pid)
end

function OnPlayerFaction(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerFaction\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerFaction(pid)
end

function OnPlayerTopic(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerTopic\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerTopic(pid)
end

function OnPlayerBounty(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerBounty\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerBounty(pid)
end

function OnPlayerBountyIntent(pid)
    return eventHandler.OnPlayerBountyIntent(pid)
end

function OnPlayerBountyIntentRejected(pid, reason)
    eventHandler.OnPlayerBountyIntentRejected(pid, reason)
end

function OnPlayerJailComplete(pid, sentenceId)
    eventHandler.OnPlayerJailComplete(pid, sentenceId)
end

function OnPlayerReputation(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerReputation\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerReputation(pid)
end

function OnPlayerBook(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerBook\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerBook(pid)
end

function OnPlayerItemUse(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerItemUse\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerItemUse(pid)
end

function OnPlayerMiscellaneous(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerMiscellaneous\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerMiscellaneous(pid)
end

function OnPlayerEndCharGen(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerEndCharGen\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerEndCharGen(pid)
end

function OnCellLoad(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnCellLoad\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnCellLoad(pid, cellDescription)
end

function OnCellUnload(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnCellUnload\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnCellUnload(pid, cellDescription)
end

function OnCellDeletion(cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnCellDeletion\" for cell " .. cellDescription)
    eventHandler.OnCellDeletion(cellDescription)
end

function OnActorList(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnActorList\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnActorList(pid, cellDescription)
end

function OnActorEquipment(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnActorEquipment\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnActorEquipment(pid, cellDescription)
end

function OnActorAI(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnActorAI\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnActorAI(pid, cellDescription)
end

function OnActorAttackIntent(pid, cellDescription, actorIndex, isRanged, targetPid, refNum, mpNum, strength)
    return eventHandler.OnActorAttackIntent(pid, cellDescription, actorIndex, isRanged,
        targetPid, refNum, mpNum, strength)
end

function OnActorAttackIntentRejected(pid, cellDescription, actorIndex, reason)
    eventHandler.OnActorAttackIntentRejected(pid, cellDescription, actorIndex, reason)
end

function OnActorCastIntent(pid, cellDescription, actorIndex, isItem, pressed,
    targetPid, refNum, mpNum)
    return eventHandler.OnActorCastIntent(pid, cellDescription, actorIndex, isItem,
        pressed, targetPid, refNum, mpNum)
end

function OnActorCastIntentRejected(pid, cellDescription, actorIndex, reason)
    eventHandler.OnActorCastIntentRejected(pid, cellDescription, actorIndex, reason)
end

function OnActorDeath(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnActorDeath\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnActorDeath(pid, cellDescription)
end

function OnActorSpellsActive(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnActorSpellsActive\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnActorSpellsActive(pid, cellDescription)
end

function OnActorSpellsActiveIntent(pid, cellDescription)
    return eventHandler.OnActorSpellsActiveIntent(pid, cellDescription)
end

function OnActorSpellsActiveIntentRejected(pid, cellDescription, reason)
    eventHandler.OnActorSpellsActiveIntentRejected(pid, cellDescription, reason)
end

function OnActorCellChange(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnActorCellChange\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnActorCellChange(pid, cellDescription)
end

function OnObjectActivate(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectActivate\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectActivate(pid, cellDescription)
end

function OnObjectHit(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectHit\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectHit(pid, cellDescription)
end

function OnObjectPlace(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectPlace\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectPlace(pid, cellDescription)
end

function OnObjectPlaceIntent(pid, cellDescription)
    return eventHandler.OnObjectPlaceIntent(pid, cellDescription)
end

function OnObjectPlaceIntentRejected(pid, cellDescription, reason)
    eventHandler.OnObjectPlaceIntentRejected(pid, cellDescription, reason)
end

function OnObjectMutationIntent(pid, cellDescription, packetType)
    return eventHandler.OnObjectMutationIntent(pid, cellDescription, packetType)
end

function OnObjectMutationIntentRejected(pid, cellDescription, packetType, reason)
    eventHandler.OnObjectMutationIntentRejected(pid, cellDescription, packetType, reason)
end

function OnObjectMutationCommitted(pid, cellDescription, packetType)
    eventHandler.OnObjectMutationCommitted(pid, cellDescription, packetType)
end

function OnObjectSpawn(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectSpawn\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectSpawn(pid, cellDescription)
end

function OnObjectDelete(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectDelete\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectDelete(pid, cellDescription)
end

function OnObjectLock(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectLock\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectLock(pid, cellDescription)
end

function OnObjectDialogueChoice(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectDialogueChoice\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectDialogueChoice(pid, cellDescription)
end

function OnObjectMiscellaneous(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectMiscellaneous\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectMiscellaneous(pid, cellDescription)
end

function OnObjectRestock(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectRestock\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectRestock(pid, cellDescription)
end

function OnObjectTrap(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectTrap\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectTrap(pid, cellDescription)
end

function OnObjectScale(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectScale\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectScale(pid, cellDescription)
end

function OnObjectSound(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectSound\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectSound(pid, cellDescription)
end

function OnObjectState(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectState\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectState(pid, cellDescription)
end

function OnDoorState(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnDoorState\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnDoorState(pid, cellDescription)
end

function OnConsoleCommand(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnConsoleCommand\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnConsoleCommand(pid, cellDescription)
end

function OnContainer(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnContainer\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnContainer(pid, cellDescription)
end

function OnContainerIntent(pid, cellDescription)
    return eventHandler.OnContainerIntent(pid, cellDescription)
end

function OnContainerIntentRejected(pid, cellDescription, reason)
    eventHandler.OnContainerIntentRejected(pid, cellDescription, reason)
end

function OnVideoPlay(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnVideoPlay\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnVideoPlay(pid)
end

function OnRecordDynamic(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnRecordDynamic\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnRecordDynamic(pid)
end

function OnWorldKillCount(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnWorldKillCount\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnWorldKillCount(pid)
end

function OnWorldMap(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnWorldMap\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnWorldMap(pid)
end

function OnWorldWeather(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnWorldWeather\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnWorldWeather(pid)
end

function OnClientScriptLocal(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnClientScriptLocal\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnClientScriptLocal(pid, cellDescription)
end

function OnClientScriptGlobal(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnClientScriptGlobal\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnClientScriptGlobal(pid)
end

function OnGUIAction(pid, idGui, data)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnGUIAction\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnGUIAction(pid, idGui, data)
end

function OnMpNumIncrement(currentMpNum)
    eventHandler.OnMpNumIncrement(currentMpNum)
end

-- Timer-based events
function OnLoginTimeExpiration(pid, accountName)
    eventHandler.OnLoginTimeExpiration(pid, accountName)
end

function OnDeathTimeExpiration(pid, accountName)
    eventHandler.OnDeathTimeExpiration(pid, accountName)
end

function OnObjectLoopTimeExpiration(loopIndex)
    eventHandler.OnObjectLoopTimeExpiration(loopIndex)
end
