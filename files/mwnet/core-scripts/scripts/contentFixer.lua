tableHelper = require("tableHelper")
require("utils")

local contentFixer = {}

local deadlyItems = { "keening", "sunder" }
local fixesByCell = {}

-- Delete the chargen boat and associated guards and objects
fixesByCell["-1, -9"] = { disable =  { 268178, 297457, 297459, 297460, 299125 }}
fixesByCell["-2, -9"] = { disable = { 172848, 172850, 172852, 289104, 297461, 397559 }}
fixesByCell["-2, -10"] = { disable = { 297463, 297464, 297465, 297466 }}

-- Delete the census papers and unlock the doors
fixesByCell["Seyda Neen, Census and Excise Office"] = { disable = { 172859 }, unlock = { 119513, 172860 }}

function contentFixer.FixCell(pid, cellDescription)

    if fixesByCell[cellDescription] ~= nil then

        for action, refNumArray in pairs(fixesByCell[cellDescription]) do

            mwnet.ClearObjectList()
            mwnet.SetObjectListPid(pid)
            mwnet.SetObjectListCell(cellDescription)

            for arrayIndex, refNum in ipairs(refNumArray) do
                mwnet.SetObjectRefNum(refNum)
                mwnet.SetObjectMpNum(0)
                mwnet.SetObjectRefId("")
                if action == "disable" then mwnet.SetObjectState(false) end
                if action == "unlock" then mwnet.SetObjectLockLevel(0) end
                mwnet.AddObject()
            end

            if action == "delete" then
                mwnet.SendObjectDelete()
            elseif action == "disable" then
                mwnet.SendObjectState()
            elseif action == "unlock" then
                mwnet.SendObjectLock()
            end
        end
    end
end

-- Unequip items that damage the player when worn
--
-- Note: Items with constant damage effects like Whitewalker and the Mantle of Woe
--       are already unequipped by default in the MWNet client, so this only needs
--       to account for scripted items that are missed there
--
function contentFixer.UnequipDeadlyItems(pid)

    local itemsFound = 0

    for arrayIndex, itemRefId in pairs(deadlyItems) do
        if tableHelper.containsKeyValue(Players[pid].data.equipment, "refId", itemRefId, true) then
            local itemSlot = tableHelper.getIndexByNestedKeyValue(Players[pid].data.equipment, "refId", itemRefId, true)
            Players[pid].data.equipment[itemSlot] = nil
            itemsFound = itemsFound + 1
        end
    end

    if itemsFound > 0 then
        Players[pid]:QuicksaveToDrive()
        Players[pid]:LoadEquipment()
    end
end

function contentFixer.AdjustSharedCorprusState(pid)

    local corprusId = "corprus"

    if WorldInstance.data.customVariables.corprusCured == true then
        if tableHelper.containsValue(Players[pid].data.spellbook, corprusId) == true then
        
            tableHelper.removeValue(Players[pid].data.spellbook, corprusId)
            tableHelper.cleanNils(Players[pid].data.spellbook)

            mwnet.ClearSpellbookChanges(pid)
            mwnet.SetSpellbookChangesAction(pid, enumerations.spellbook.REMOVE)
            mwnet.AddSpell(pid, corprusId)
            mwnet.SendSpellbookChanges(pid)

            mwnet.ClearSpellbookChanges(pid)
            mwnet.SetSpellbookChangesAction(pid, enumerations.spellbook.ADD)
            for _, spellId in ipairs({"common disease immunity", "blight disease immunity","corprus immunity"}) do
                table.insert(Players[pid].data.spellbook, spellId)
                mwnet.AddSpell(pid, spellId)
            end
            mwnet.SendSpellbookChanges(pid)
            mwnet.MessageBox(pid, -1, "You have been cured of corprus.")
        end
    elseif WorldInstance.data.customVariables.corprusGained == true then
        if tableHelper.containsValue(Players[pid].data.spellbook, corprusId) == false then

            table.insert(Players[pid].data.spellbook, corprusId)

            mwnet.ClearSpellbookChanges(pid)
            mwnet.SetSpellbookChangesAction(pid, enumerations.spellbook.ADD)
            mwnet.AddSpell(pid, corprusId)
            mwnet.SendSpellbookChanges(pid)
            mwnet.MessageBox(pid, -1, "You have been afflicted with corprus.")
        end
    end
end

function contentFixer.AdjustWorldCorprusVariables(journal)

    local madeAdjustment = false

    for _, journalItem in ipairs(journal) do

        if journalItem.quest == "a2_3_corpruscure" and journalItem.index >= 50 then
            WorldInstance.data.customVariables.corprusCured = true
            madeAdjustment = true
        elseif journalItem.quest == "a2_2_6thhouse" and journalItem.index >= 50 then
            WorldInstance.data.customVariables.corprusGained = true
            madeAdjustment = true
        end
    end

    return madeAdjustment
end

customEventHooks.registerHandler("OnPlayerJournal", function(eventStatus, pid, playerPacket)
    if config.shareJournal == true then
        local madeAdjustment = contentFixer.AdjustWorldCorprusVariables(playerPacket.journal)

        if madeAdjustment == true then
            for otherPid, otherPlayer in pairs(Players) do
                if otherPid ~= pid then
                    contentFixer.AdjustSharedCorprusState(otherPid)
                end
            end
        end
    end
end)

customEventHooks.registerHandler("OnPlayerFinishLogin", function(eventStatus, pid)
    if config.shareJournal == true then
        contentFixer.AdjustSharedCorprusState(pid)
    end
end)

customEventHooks.registerHandler("OnWorldReload", function(eventStatus)
    if config.shareJournal == true then
        contentFixer.AdjustWorldCorprusVariables(WorldInstance.data.journal)
    end
end)

return contentFixer
