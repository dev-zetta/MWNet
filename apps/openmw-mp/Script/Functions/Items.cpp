#include "Items.hpp"

#include <components/misc/stringops.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>

#include <apps/openmw-mp/Script/ScriptFunctions.hpp>
#include <apps/openmw-mp/Networking.hpp>
#include <apps/openmw/mwworld/inventorystore.hpp>

#include <stdexcept>

using namespace mwmp;

void ItemFunctions::ClearInventoryChanges(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->inventoryChanges.items.clear();
}

int ItemFunctions::GetEquipmentSize()
{
    return MWWorld::InventoryStore::Slots;
}

unsigned int ItemFunctions::GetEquipmentChangesSize(unsigned short pid)
{
    Player* player;
    GET_PLAYER(pid, player, 0);

    return player->equipmentIndexChanges.size();
}

unsigned int ItemFunctions::GetInventoryChangesSize(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->inventoryChanges.items.size();
}

unsigned int ItemFunctions::GetInventoryChangesAction(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->inventoryChanges.action;
}

void ItemFunctions::SetInventoryChangesAction(unsigned short pid, unsigned char action)
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->inventoryChanges.action = action;
}

void ItemFunctions::EquipItem(unsigned short pid, unsigned short slot, const char *refId, unsigned int count,
    int charge, double enchantmentCharge)
{
    Player *player;
    GET_PLAYER(pid, player,);

    if (slot >= mwmp::mechanics::EquipmentLedger::SlotCount)
        throw std::out_of_range("equipment slot is outside the valid range");

    player->equipmentItems[slot].refId = refId;
    player->equipmentItems[slot].count = count;
    player->equipmentItems[slot].charge = charge;
    player->equipmentItems[slot].enchantmentCharge = enchantmentCharge;

    if (!Utils::vectorContains(player->equipmentIndexChanges, slot))
        player->equipmentIndexChanges.push_back(slot);
}

void ItemFunctions::UnequipItem(unsigned short pid, unsigned short slot)
{
    Player *player;
    GET_PLAYER(pid, player, );

    ItemFunctions::EquipItem(pid, slot, "", 0, -1, -1);
}

void ItemFunctions::AddItemChange(unsigned short pid, const char* refId, unsigned int count, int charge,
    double enchantmentCharge, const char* soul)
{
    Player *player;
    GET_PLAYER(pid, player, );

    Item item;
    item.refId = refId;
    item.count = count;
    item.charge = charge;
    item.enchantmentCharge = enchantmentCharge;
    item.soul = soul;

    player->inventoryChanges.items.push_back(item);
}

bool ItemFunctions::HasItemEquipped(unsigned short pid, const char* refId)
{
    Player *player;
    GET_PLAYER(pid, player, false);

    for (int slot = 0; slot < MWWorld::InventoryStore::Slots; slot++)
        if (Misc::StringUtils::ciEqual(player->equipmentItems[slot].refId, refId))
            return true;
    return false;
}

int ItemFunctions::GetEquipmentChangesSlot(unsigned short pid, unsigned int changeIndex)
{
    Player* player;
    GET_PLAYER(pid, player, 0);


    return player->equipmentIndexChanges[changeIndex];
}

const char *ItemFunctions::GetEquipmentItemRefId(unsigned short pid, unsigned short slot)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->equipmentItems[slot].refId.c_str();
}

int ItemFunctions::GetEquipmentItemCount(unsigned short pid, unsigned short slot)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->equipmentItems[slot].count;
}

int ItemFunctions::GetEquipmentItemCharge(unsigned short pid, unsigned short slot)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->equipmentItems[slot].charge;
}

double ItemFunctions::GetEquipmentItemEnchantmentCharge(unsigned short pid, unsigned short slot)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->equipmentItems[slot].enchantmentCharge;
}

const char *ItemFunctions::GetInventoryItemRefId(unsigned short pid, unsigned int index)
{
    Player *player;
    GET_PLAYER(pid, player, "");

    if (index >= player->inventoryChanges.items.size())
        return "invalid";

    return player->inventoryChanges.items.at(index).refId.c_str();
}

int ItemFunctions::GetInventoryItemCount(unsigned short pid, unsigned int index)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->inventoryChanges.items.at(index).count;
}

int ItemFunctions::GetInventoryItemCharge(unsigned short pid, unsigned int index)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->inventoryChanges.items.at(index).charge;
}

double ItemFunctions::GetInventoryItemEnchantmentCharge(unsigned short pid, unsigned int index)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->inventoryChanges.items.at(index).enchantmentCharge;
}

const char *ItemFunctions::GetInventoryItemSoul(unsigned short pid, unsigned int index)
{
    Player *player;
    GET_PLAYER(pid, player, "");

    if (index >= player->inventoryChanges.items.size())
        return "invalid";

    return player->inventoryChanges.items.at(index).soul.c_str();
}

const char *ItemFunctions::GetUsedItemRefId(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, "");

    return player->usedItem.refId.c_str();
}

int ItemFunctions::GetUsedItemCount(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->usedItem.count;
}

int ItemFunctions::GetUsedItemCharge(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->usedItem.charge;
}

double ItemFunctions::GetUsedItemEnchantmentCharge(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->usedItem.enchantmentCharge;
}

const char *ItemFunctions::GetUsedItemSoul(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, "");

    return player->usedItem.soul.c_str();
}

bool ItemFunctions::IsUsedItemServerResolved(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, false);

    return player->itemUseServerResolved;
}

void ItemFunctions::SendEquipment(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, );

    if (!mwmp::Networking::getPtr()->applyServerPlayerEquipment(*player))
        throw std::runtime_error("the server-authored equipment action was rejected");

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_EQUIPMENT);
    packet->setPlayer(player);

    packet->Send(false);
    packet->Send(true);

    player->equipmentIndexChanges.clear();
}

void ItemFunctions::SendInventoryChanges(unsigned short pid, bool sendToOtherPlayers, bool skipAttachedPlayer)
{
    Player *player;
    GET_PLAYER(pid, player, );

    if (!mwmp::Networking::getPtr()->applyServerInventoryChanges(*player))
        throw std::runtime_error("the server-authored inventory action was rejected");

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_INVENTORY);
    packet->setPlayer(player);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ItemFunctions::SendItemUse(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, );

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_ITEM_USE);
    packet->setPlayer(player);

    packet->Send(false);
}

// All methods below are deprecated versions of methods from above

void ItemFunctions::InitializeInventoryChanges(unsigned short pid)
{
    ClearInventoryChanges(pid);
}

void ItemFunctions::AddItem(unsigned short pid, const char* refId, unsigned int count, int charge,
    double enchantmentCharge, const char* soul)
{
    AddItemChange(pid, refId, count, charge, enchantmentCharge, soul);
}
