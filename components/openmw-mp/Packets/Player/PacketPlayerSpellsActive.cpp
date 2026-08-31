#include "PacketPlayerSpellsActive.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerSpellsActive::PacketPlayerSpellsActive() : PlayerPacket()
{
    packetID = ID_PLAYER_SPELLS_ACTIVE;
}

void PacketPlayerSpellsActive::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->spellsActiveChanges.action);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->spellsActiveChanges.activeSpells.size());

    if (!CollectionSize(count))
        return;

    if (!send)
    {
        player->spellsActiveChanges.activeSpells.clear();
        player->spellsActiveChanges.activeSpells.resize(count);
    }

    for (auto&& activeSpell : player->spellsActiveChanges.activeSpells)
    {
        Field(activeSpell.id, true);
        Field(activeSpell.isStackingSpell);
        Field(activeSpell.timestampDay);
        Field(activeSpell.timestampHour);
        Field(activeSpell.params.mDisplayName, true);

        Field(activeSpell.caster.isPlayer);

        if (activeSpell.caster.isPlayer)
        {
            Field(activeSpell.caster.guid);
        }
        else
        {
            Field(activeSpell.caster.refId, true);
            Field(activeSpell.caster.refNum);
            Field(activeSpell.caster.mpNum);
        }

        uint32_t effectCount = 0;

        if (send)
            effectCount = static_cast<uint32_t>(activeSpell.params.mEffects.size());

        if (!CollectionSize(effectCount, protocol::limits::spellEffects))
            return;

        if (!send)
        {
            activeSpell.params.mEffects.clear();
            activeSpell.params.mEffects.resize(effectCount);
        }

        for (auto&& effect : activeSpell.params.mEffects)
        {
            Field(effect.mEffectId);
            Field(effect.mArg);
            Field(effect.mMagnitude);
            Field(effect.mDuration);
            Field(effect.mTimeLeft);
        }
    }
}
