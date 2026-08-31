#include "Stats.hpp"

#include <stdexcept>

#include <iostream>

#include <components/esm/attr.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/misc/stringops.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>

#include <apps/openmw-mp/Networking.hpp>
#include <apps/openmw-mp/Script/ScriptFunctions.hpp>

int StatsFunctions::GetAttributeCount()
{
    return ESM::Attribute::Length;
}

int StatsFunctions::GetSkillCount()
{
    return ESM::Skill::Length;
}

int StatsFunctions::GetAttributeId(const char *name)
{
    for (int x = 0; x < ESM::Attribute::Length; x++)
    {
        if (Misc::StringUtils::ciEqual(name, ESM::Attribute::indexToRefId(x).getRefIdString()))
        {
            return x;
        }
    }

    return -1;
}

int StatsFunctions::GetSkillId(const char *name)
{
    for (int x = 0; x < ESM::Skill::Length; x++)
    {
        if (Misc::StringUtils::ciEqual(name, ESM::Skill::indexToRefId(x).getRefIdString()))
        {
            return x;
        }
    }

    return -1;
}

const char *StatsFunctions::GetAttributeName(unsigned short attributeId)
{
    if (attributeId >= ESM::Attribute::Length)
        return "invalid";

    static std::string attrName;
    attrName = ESM::Attribute::indexToRefId(attributeId).getRefIdString();
    return attrName.c_str();
}

const char *StatsFunctions::GetSkillName(unsigned short skillId)
{
    if (skillId >= ESM::Skill::Length)
        return "invalid";

    static std::string skillName;
    skillName = ESM::Skill::indexToRefId(skillId).getRefIdString();
    return skillName.c_str();
}

const char *StatsFunctions::GetName(unsigned short pid)
{

    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->npc.mName.c_str();
}

const char *StatsFunctions::GetRace(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->npc.mRace.getRefIdString().c_str();
}

const char *StatsFunctions::GetHead(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->npc.mHead.getRefIdString().c_str();
}

const char *StatsFunctions::GetHairstyle(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->npc.mHair.getRefIdString().c_str();
}

int StatsFunctions::GetIsMale(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, false);

    return player->npc.isMale();
}

const char* StatsFunctions::GetModel(unsigned short pid)
{
    Player* player;
    GET_PLAYER(pid, player, 0);

    return player->npc.mModel.getOriginal().c_str();
}

const char *StatsFunctions::GetBirthsign(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->birthsign.c_str();
}

int StatsFunctions::GetLevel(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->creatureStats.mLevel;
}

int StatsFunctions::GetLevelProgress(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->npcStats.mLevelProgress;
}

double StatsFunctions::GetHealthBase(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->creatureStats.mDynamic[0].mBase;
}

double StatsFunctions::GetHealthCurrent(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->creatureStats.mDynamic[0].mCurrent;
}

double StatsFunctions::GetPlayerAttackStrength(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0.0);

    return player->attack.attackStrength;
}

double StatsFunctions::GetMagickaBase(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->creatureStats.mDynamic[1].mBase;
}

double StatsFunctions::GetMagickaCurrent(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->creatureStats.mDynamic[1].mCurrent;
}

double StatsFunctions::GetFatigueBase(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->creatureStats.mDynamic[2].mBase;
}

double StatsFunctions::GetFatigueCurrent(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->creatureStats.mDynamic[2].mCurrent;
}

int StatsFunctions::GetAttributeBase(unsigned short pid, unsigned short attributeId)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    if (attributeId >= ESM::Attribute::Length)
        return 0;

    return player->creatureStats.mAttributes[ESM::Attribute::indexToRefId(attributeId)].mBase;
}

int StatsFunctions::GetAttributeModifier(unsigned short pid, unsigned short attributeId)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    if (attributeId >= ESM::Attribute::Length)
        return 0;

    return player->creatureStats.mAttributes[ESM::Attribute::indexToRefId(attributeId)].mMod;
}

double StatsFunctions::GetAttributeDamage(unsigned short pid, unsigned short attributeId)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    if (attributeId >= ESM::Attribute::Length)
        return 0;

    return player->creatureStats.mAttributes[ESM::Attribute::indexToRefId(attributeId)].mDamage;
}

int StatsFunctions::GetSkillBase(unsigned short pid, unsigned short skillId)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    if (skillId >= ESM::Skill::Length)
        return 0;

    return player->npcStats.mSkills[ESM::Skill::indexToRefId(skillId)].mBase;
}

int StatsFunctions::GetSkillModifier(unsigned short pid, unsigned short skillId)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    if (skillId >= ESM::Skill::Length)
        return 0;

    return player->npcStats.mSkills[ESM::Skill::indexToRefId(skillId)].mMod;
}

double StatsFunctions::GetSkillDamage(unsigned short pid, unsigned short skillId)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    if (skillId >= ESM::Skill::Length)
        return 0;

    return player->npcStats.mSkills[ESM::Skill::indexToRefId(skillId)].mDamage;
}

double StatsFunctions::GetSkillProgress(unsigned short pid, unsigned short skillId)
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    if (skillId >= ESM::Skill::Length)
        return 0;

    return player->npcStats.mSkills[ESM::Skill::indexToRefId(skillId)].mProgress;
}

int StatsFunctions::GetSkillIncrease(unsigned short pid, unsigned int attributeId)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    if (attributeId >= ESM::Attribute::Length)
        return 0;

    return player->npcStats.mSkillIncrease[ESM::Attribute::indexToRefId(attributeId)];
}

int StatsFunctions::GetBounty(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->npcStats.mBounty;
}

void StatsFunctions::SetName(unsigned short pid, const char *name)
{
    Player *player;
    GET_PLAYER(pid, player,);

    if (player->npc.mName == name)
        return;

    player->npc.mName = name;
}

void StatsFunctions::SetRace(unsigned short pid, const char *race)
{
    Player *player;
    GET_PLAYER(pid, player,);

    if (player->npc.mRace == race)
        return;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Setting race for %s: %s -> %s", player->npc.mName.c_str(),
                       player->npc.mRace.getRefIdString().c_str(), race);

    player->npc.mRace = ESM::RefId::stringRefId(race);
}

void StatsFunctions::SetHead(unsigned short pid, const char *head)
{
    Player *player;
    GET_PLAYER(pid, player,);

    if (player->npc.mHead == head)
        return;

    player->npc.mHead = ESM::RefId::stringRefId(head);
}

void StatsFunctions::SetHairstyle(unsigned short pid, const char *hairstyle)
{
    Player *player;
    GET_PLAYER(pid, player,);

    if (player->npc.mHair == hairstyle)
        return;

    player->npc.mHair = ESM::RefId::stringRefId(hairstyle);
}

void StatsFunctions::SetIsMale(unsigned short pid, int state)
{
    Player *player;
    GET_PLAYER(pid, player,);

    player->npc.setIsMale(state > 0 ? true : false);
}

void StatsFunctions::SetModel(unsigned short pid, const char *model)
{
    Player* player;
    GET_PLAYER(pid, player, );

    if (player->npc.mModel.getOriginal() == model)
        return;

    player->npc.mModel = model;
}

void StatsFunctions::SetBirthsign(unsigned short pid, const char *sign)
{
    Player *player;
    GET_PLAYER(pid, player, );

    if (player->birthsign == sign)
        return;

    player->birthsign = sign;
}

void StatsFunctions::SetResetStats(unsigned short pid, bool resetStats)
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->resetStats = resetStats;
}

void StatsFunctions::SetLevel(unsigned short pid, int value)
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->creatureStats.mLevel = value;
}

void StatsFunctions::SetLevelProgress(unsigned short pid, int value)
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->npcStats.mLevelProgress = value;
}

void StatsFunctions::SetHealthBase(unsigned short pid, double value)
{
    Player *player;
    GET_PLAYER(pid, player,);

    player->creatureStats.mDynamic[0].mBase = value;

    if (!Utils::vectorContains(player->statsDynamicIndexChanges, 0))
        player->statsDynamicIndexChanges.push_back(0);
}

void StatsFunctions::SetHealthCurrent(unsigned short pid, double value)
{
    Player *player;
    GET_PLAYER(pid, player,);

    player->creatureStats.mDynamic[0].mCurrent = value;

    if (!Utils::vectorContains(player->statsDynamicIndexChanges, 0))
        player->statsDynamicIndexChanges.push_back(0);
}

void StatsFunctions::SetPlayerAttackStrength(unsigned short pid, double value)
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->attack.attackStrength = static_cast<float>(value);
}

void StatsFunctions::SetMagickaBase(unsigned short pid, double value)
{
    Player *player;
    GET_PLAYER(pid, player,);

    player->creatureStats.mDynamic[1].mBase = value;

    if (!Utils::vectorContains(player->statsDynamicIndexChanges, 1))
        player->statsDynamicIndexChanges.push_back(1);
}

void StatsFunctions::SetMagickaCurrent(unsigned short pid, double value)
{
    Player *player;
    GET_PLAYER(pid, player,);

    player->creatureStats.mDynamic[1].mCurrent = value;

    if (!Utils::vectorContains(player->statsDynamicIndexChanges, 1))
        player->statsDynamicIndexChanges.push_back(1);
}

void StatsFunctions::SetFatigueBase(unsigned short pid, double value)
{
    Player *player;
    GET_PLAYER(pid, player,);

    player->creatureStats.mDynamic[2].mBase = value;

    if (!Utils::vectorContains(player->statsDynamicIndexChanges, 2))
        player->statsDynamicIndexChanges.push_back(2);
}

void StatsFunctions::SetFatigueCurrent(unsigned short pid, double value)
{
    Player *player;
    GET_PLAYER(pid, player,);

    player->creatureStats.mDynamic[2].mCurrent = value;

    if (!Utils::vectorContains(player->statsDynamicIndexChanges, 2))
        player->statsDynamicIndexChanges.push_back(2);
}

void StatsFunctions::SetAttributeBase(unsigned short pid, unsigned short attributeId, int value)
{
    Player *player;
    GET_PLAYER(pid, player,);

    if (attributeId >= ESM::Attribute::Length)
        return;

    player->creatureStats.mAttributes[ESM::Attribute::indexToRefId(attributeId)].mBase = value;

    if (!Utils::vectorContains(player->attributeIndexChanges, attributeId))
        player->attributeIndexChanges.push_back(attributeId);
}

void StatsFunctions::ClearAttributeModifier(unsigned short pid, unsigned short attributeId)
{
    Player *player;
    GET_PLAYER(pid, player,);

    if (attributeId >= ESM::Attribute::Length)
        return;

    player->creatureStats.mAttributes[ESM::Attribute::indexToRefId(attributeId)].mMod = 0;

    if (!Utils::vectorContains(player->attributeIndexChanges, attributeId))
        player->attributeIndexChanges.push_back(attributeId);
}

void StatsFunctions::SetAttributeDamage(unsigned short pid, unsigned short attributeId, double value)
{
    Player *player;
    GET_PLAYER(pid, player, );

    if (attributeId >= ESM::Attribute::Length)
        return;

    player->creatureStats.mAttributes[ESM::Attribute::indexToRefId(attributeId)].mDamage = value;

    if (!Utils::vectorContains(player->attributeIndexChanges, attributeId))
        player->attributeIndexChanges.push_back(attributeId);
}

void StatsFunctions::SetSkillBase(unsigned short pid, unsigned short skillId, int value)
{
    Player *player;
    GET_PLAYER(pid, player,);

    if (skillId >= ESM::Skill::Length)
        return;

    player->npcStats.mSkills[ESM::Skill::indexToRefId(skillId)].mBase = value;

    if (!Utils::vectorContains(player->skillIndexChanges, skillId))
        player->skillIndexChanges.push_back(skillId);
}

void StatsFunctions::ClearSkillModifier(unsigned short pid, unsigned short skillId)
{
    Player *player;
    GET_PLAYER(pid, player,);

    if (skillId >= ESM::Skill::Length)
        return;

    player->npcStats.mSkills[ESM::Skill::indexToRefId(skillId)].mMod = 0;

    if (!Utils::vectorContains(player->skillIndexChanges, skillId))
        player->skillIndexChanges.push_back(skillId);
}

void StatsFunctions::SetSkillDamage(unsigned short pid, unsigned short skillId, double value)
{
    Player *player;
    GET_PLAYER(pid, player, );

    if (skillId >= ESM::Skill::Length)
        return;

    player->npcStats.mSkills[ESM::Skill::indexToRefId(skillId)].mDamage = value;

    if (!Utils::vectorContains(player->skillIndexChanges, skillId))
        player->skillIndexChanges.push_back(skillId);
}

void StatsFunctions::SetSkillProgress(unsigned short pid, unsigned short skillId, double value)
{
    Player *player;
    GET_PLAYER(pid, player, );

    if (skillId >= ESM::Skill::Length)
        return;

    player->npcStats.mSkills[ESM::Skill::indexToRefId(skillId)].mProgress = value;

    if (!Utils::vectorContains(player->skillIndexChanges, skillId))
        player->skillIndexChanges.push_back(skillId);
}

void StatsFunctions::SetSkillIncrease(unsigned short pid, unsigned int attributeId, int value)
{
    Player *player;
    GET_PLAYER(pid, player,);

    if (attributeId >= ESM::Attribute::Length)
        return;

    player->npcStats.mSkillIncrease[ESM::Attribute::indexToRefId(attributeId)] = value;

    if (!Utils::vectorContains(player->attributeIndexChanges, attributeId))
        player->attributeIndexChanges.push_back(attributeId);
}

void StatsFunctions::SetBounty(unsigned short pid, int value)
{
    Player *player;
    GET_PLAYER(pid, player, );

    const int previousValue = player->npcStats.mBounty;
    player->npcStats.mBounty = value;
    if (!mwmp::Networking::getPtr()->applyServerPlayerBounty(*player))
    {
        player->npcStats.mBounty = previousValue;
        throw std::runtime_error("the server-authored bounty was rejected");
    }
}

void StatsFunctions::SetCharGenStage(unsigned short pid, int currentStage, int endStage)
{
    Player *player;
    GET_PLAYER(pid, player,);

    player->charGenState.currentStage = currentStage;
    player->charGenState.endStage = endStage;
    player->charGenState.isFinished = false;

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_CHARGEN);
    packet->setPlayer(player);
    
    packet->Send(false);
}

void StatsFunctions::SendBaseInfo(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player,);

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_BASEINFO);
    packet->setPlayer(player);
    
    packet->Send(false);
    packet->Send(true);
}

void StatsFunctions::SendStatsDynamic(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, );

    if (!mwmp::Networking::getPtr()->applyServerPlayerStats(*player))
        return;

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_STATS_DYNAMIC);
    packet->setPlayer(player);
    
    packet->Send(false);
    packet->Send(true);

    player->statsDynamicIndexChanges.clear();
}

void StatsFunctions::SendAttributes(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player,);

    if (mwmp::Networking::getPtr()->isPlayerAttributeIntentPending(*player))
        throw std::runtime_error(
            "a pending attribute intent cannot be sent before canonical commit");
    if (!mwmp::Networking::getPtr()->applyServerPlayerAttributes(*player))
        throw std::runtime_error("the server-authored attributes were rejected");

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_ATTRIBUTE);
    packet->setPlayer(player);
    
    packet->Send(false);
    packet->Send(true);

    player->attributeIndexChanges.clear();
}

void StatsFunctions::SendSkills(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player,);

    if (mwmp::Networking::getPtr()->isPlayerSkillIntentPending(*player))
        throw std::runtime_error(
            "a pending skill intent cannot be sent before canonical commit");
    if (!mwmp::Networking::getPtr()->applyServerPlayerSkills(*player))
        throw std::runtime_error("the server-authored skills were rejected");

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_SKILL);
    packet->setPlayer(player);
    
    packet->Send(false);
    packet->Send(true);

    player->skillIndexChanges.clear();
}

void StatsFunctions::SendLevel(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, );

    if (mwmp::Networking::getPtr()->isPlayerLevelIntentPending(*player))
        throw std::runtime_error(
            "a pending level intent cannot be sent before canonical commit");
    if (!mwmp::Networking::getPtr()->applyServerPlayerLevel(*player))
        throw std::runtime_error("the server-authored level was rejected");

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_LEVEL);
    packet->setPlayer(player);
    
    packet->Send(false);
    packet->Send(true);
}

void StatsFunctions::SendBounty(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, );

    if (mwmp::Networking::getPtr()->isPlayerBountyIntentPending(*player))
        throw std::runtime_error(
            "a pending bounty intent cannot be sent before canonical commit");
    if (!mwmp::Networking::getPtr()->applyServerPlayerBounty(*player))
        throw std::runtime_error("the server-authored bounty was rejected");

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_BOUNTY);
    packet->setPlayer(player);
    
    packet->Send(false);
    packet->Send(true);
}
