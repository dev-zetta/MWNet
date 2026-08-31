#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorSpellsActive.hpp"

using namespace mwmp;

PacketActorSpellsActive::PacketActorSpellsActive() : ActorPacket()
{
    packetID = ID_ACTOR_SPELLS_ACTIVE;
}

void PacketActorSpellsActive::Actor(BaseActor &actor, bool send)
{
    Field(actor.spellsActiveChanges.action);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(actor.spellsActiveChanges.activeSpells.size());

    if (!CollectionSize(count))
        return;

    if (!send)
    {
        actor.spellsActiveChanges.activeSpells.clear();
        actor.spellsActiveChanges.activeSpells.resize(count);
    }

    for (auto&& activeSpell : actor.spellsActiveChanges.activeSpells)
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
