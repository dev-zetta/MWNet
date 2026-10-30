#include <components/openmw-mp/Mechanics/AttackAnimation.hpp>
#include <components/openmw-mp/NetworkActiveSpell.hpp>
#include <components/openmw-mp/ScopedActorList.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorSpellsActive.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerSpellsActive.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAttack.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorList.hpp>
#include <components/openmw-mp/Packets/Object/PacketContainer.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerAttack.hpp>

#include <algorithm>
#include <iostream>
#include <type_traits>
#include <vector>

namespace
{
    class AttackPacket : public mwmp::PacketActorAttack
    {
    public:
        std::vector<std::byte> encode(mwmp::BaseActorList& actors)
        {
            setActorList(&actors);
            Packet(true);
            const auto bytes = writePayload();
            return { bytes.begin(), bytes.end() };
        }
    };

    template <class Packet, class State>
    class EffectPacket : public Packet
    {
    public:
        void bind(State& state)
        {
            if constexpr (std::is_same_v<State, mwmp::BasePlayer>)
                this->setPlayer(&state);
            else if constexpr (std::is_same_v<State, mwmp::BaseObjectList>)
                this->setObjectList(&state);
            else
                this->setActorList(&state);
        }
        std::vector<std::byte> encode(State& state)
        {
            bind(state);
            this->Packet(true);
            const auto bytes = this->writePayload();
            return { bytes.begin(), bytes.end() };
        }
        bool prepare(State& state)
        {
            bind(state);
            this->Packet(true);
            return this->prepareWrite();
        }
    };

    bool checkReceivedSpell(const mwmp::ActiveSpell& spell)
    {
        const ESM::RefNum caster{ 243210, 0 };
        const auto params = mwmp::networkActiveSpell(ESM::RefId::stringRefId(spell.id),
            spell.isStackingSpell, spell.params.mEffects, spell.params.mDisplayName, caster);
        if (!(params.mFlags & ESM::ActiveSpells::Flag_Temporary)
            || !(params.mFlags & ESM::ActiveSpells::Flag_Stackable)
            || (params.mFlags & (ESM::ActiveSpells::Flag_SpellStore | ESM::ActiveSpells::Flag_Lua))
            || params.mCaster != caster || params.mEffects.size() != 2)
            return false;
        for (std::size_t i = 0; i < params.mEffects.size(); ++i)
        {
            const auto& effect = params.mEffects[i];
            if (effect.mMinMagnitude != 7 || effect.mMaxMagnitude != 7
                || effect.mMagnitude != 7 || effect.mDuration != 10 || effect.mTimeLeft != 8
                || effect.mEffectIndex != static_cast<int>(i)
                || (effect.mFlags & ESM::ActiveEffect::Flag_Applied)
                || !(effect.mFlags & ESM::ActiveEffect::Flag_Ignore_Resistances))
                return false;
        }
        const auto unstacked = mwmp::networkActiveSpell(params.mSourceSpellId, false,
            params.mEffects, params.mDisplayName, {});
        return !(unstacked.mFlags & ESM::ActiveSpells::Flag_Stackable)
            && mwmp::serverTicksMagicEffect(params.mEffects[0].mEffectId)
            && !mwmp::serverTicksMagicEffect(params.mEffects[1].mEffectId);
    }

    bool checkActiveSpellPackets()
    {
        mwmp::ActiveSpell spell;
        spell.id = "npc hostile spell";
        spell.isStackingSpell = true;
        spell.caster.refNum = 243210;
        spell.params.mDisplayName = "NPC hostile spell";
        ESM::ActiveEffect effect{};
        effect.mEffectId = ESM::MagicEffect::FireDamage;
        effect.mMagnitude = 7;
        effect.mMinMagnitude = 7;
        effect.mMaxMagnitude = 7;
        effect.mDuration = 10;
        effect.mTimeLeft = 8;
        spell.params.mEffects.push_back(effect);
        effect.mEffectId = ESM::MagicEffect::Burden;
        spell.params.mEffects.push_back(effect);
        mwmp::BasePlayer player;
        player.spellsActiveChanges.action = mwmp::SpellsActiveChanges::ADD;
        player.spellsActiveChanges.activeSpells = { spell };
        EffectPacket<mwmp::PacketPlayerSpellsActive, mwmp::BasePlayer> playerEncoder, playerDecoder;
        const auto playerBytes = playerEncoder.encode(player);
        mwmp::BasePlayer receivedPlayer;
        playerDecoder.bind(receivedPlayer);
        playerDecoder.Read(playerBytes);
        if (!playerEncoder.isPacketValid() || !playerDecoder.isPacketValid()
            || receivedPlayer.spellsActiveChanges.activeSpells.size() != 1
            || !checkReceivedSpell(receivedPlayer.spellsActiveChanges.activeSpells.front()))
            return false;
        // Death/respawn sends SET with no spells after a previous ADD. Reusing
        // the decoder must replace the old payload, including queued instances.
        mwmp::BasePlayer cleared;
        cleared.spellsActiveChanges.action = mwmp::SpellsActiveChanges::SET;
        playerDecoder.Read(playerEncoder.encode(cleared));
        if (!playerDecoder.isPacketValid()
            || receivedPlayer.spellsActiveChanges.action != mwmp::SpellsActiveChanges::SET
            || !receivedPlayer.spellsActiveChanges.activeSpells.empty())
            return false;
        mwmp::BaseActorList actors;
        actors.cell.blank();
        actors.authorityLeaseId = 1;
        actors.baseActors.emplace_back();
        actors.baseActors.front().refNum = 243210;
        actors.baseActors.front().spellsActiveChanges = player.spellsActiveChanges;
        EffectPacket<mwmp::PacketActorSpellsActive, mwmp::BaseActorList> actorEncoder, actorDecoder;
        const auto actorBytes = actorEncoder.encode(actors);
        mwmp::BaseActorList receivedActors;
        actorDecoder.bind(receivedActors);
        actorDecoder.Read(actorBytes);
        return actorEncoder.isPacketValid() && actorDecoder.isPacketValid()
            && receivedActors.baseActors.size() == 1
            && receivedActors.baseActors.front().spellsActiveChanges.activeSpells.size() == 1
            && checkReceivedSpell(receivedActors.baseActors.front().spellsActiveChanges.activeSpells.front());
    }

    bool checkCastBatchSurvivesScriptEvent()
    {
        mwmp::BaseActorList batch, event;
        batch.count = 2;
        batch.baseActors.resize(2);
        batch.baseActors[0].refNum = 243210;
        batch.baseActors[0].cast.spellId = "self buff";
        batch.baseActors[1].refNum = 259837;
        batch.baseActors[1].cast.spellId = "hostile spell";
        event.count = 1;
        event.baseActors.resize(1);
        event.baseActors[0].refNum = 99;
        const auto* original = &batch.baseActors[0];
        for (bool throwFromCallback : { false, true })
        {
            try
            {
                const mwmp::ScopedActorList publish(batch, event);
                if (batch.count != 1 || batch.baseActors[0].refNum != 99
                    || original->cast.spellId != "self buff")
                    return false;
                if (throwFromCallback)
                    throw 1;
            }
            catch (int) {}
            if (batch.count != 2 || batch.baseActors.size() != 2
                || &batch.baseActors[0] != original
                || batch.baseActors[1].cast.spellId != "hostile spell")
                return false;
        }
        return true;
    }

    bool checkPlayerFistPackets()
    {
        for (const bool hit : { false, true })
            for (const bool health : { false, true })
            {
                mwmp::BasePlayer sent, received;
                sent.attack.unarmed = true;
                sent.attack.isHit = sent.attack.success = hit;
                sent.attack.healthDamage = health;
                sent.attack.attackStrength = 0.35f;
                sent.attack.target.refNum = 123;
                sent.attack.damage = hit ? 7 : 0;
                sent.attack.knockdown = hit;
                EffectPacket<mwmp::PacketPlayerAttack, mwmp::BasePlayer> encoder, decoder;
                const auto bytes = encoder.encode(sent);
                decoder.bind(received);
                decoder.Read(bytes);
                if (!encoder.isPacketValid() || !decoder.isPacketValid()
                    || !received.attack.unarmed || received.attack.isHit != hit
                    || received.attack.attackStrength != 0.35f
                    || received.attack.damage != sent.attack.damage
                    || (hit && received.attack.healthDamage != health)
                    || received.attack.knockdown != hit)
                    return false;
            }
        return true;
    }

    bool checkBatch(bool reverse)
    {
        mwmp::BaseActor hit;
        hit.refNum = 175651;
        hit.attack.target.isPlayer = true;
        hit.attack.target.guid = mwmp::transport::TransportConnectionId{ 7 };
        hit.attack.isHit = true;
        hit.attack.damage = 12;
        hit.attack.unarmed = true;
        hit.attack.healthDamage = false;
        hit.attack.attackStrength = 0.35f;

        mwmp::BaseActor release;
        release.refNum = 175653;
        release.attack.attackAnimation = "slash";

        mwmp::BaseActor actorHit;
        actorHit.refNum = 175652;
        actorHit.attack.target.refNum = 175653;
        actorHit.attack.isHit = true;

        mwmp::BaseActor sceneryImpact;
        sceneryImpact.refNum = 175654;
        sceneryImpact.attack.type = mwmp::Attack::RANGED;
        sceneryImpact.attack.attackStrength = 0.75f;

        mwmp::BaseActorList sent;
        sent.cell.blank();
        sent.cell.mData.mX = -1;
        sent.cell.mData.mY = -3;
        sent.authorityLeaseId = 1;
        sent.baseActors = { hit, release, actorHit, release, sceneryImpact };
        if (reverse)
            std::reverse(sent.baseActors.begin(), sent.baseActors.end());

        AttackPacket encoder;
        const auto payload = encoder.encode(sent);
        mwmp::BaseActorList received;
        AttackPacket decoder;
        decoder.setActorList(&received);
        decoder.Read(payload);
        if (!encoder.isPacketValid() || !decoder.isPacketValid()
            || received.baseActors.size() != sent.baseActors.size())
            return false;

        for (std::size_t i = 0; i < sent.baseActors.size(); ++i)
        {
            const auto& expected = sent.baseActors[i].attack;
            const auto& actual = received.baseActors[i].attack;
            if (actual.target.guid != expected.target.guid
                || actual.target.refNum != expected.target.refNum
                || actual.target.isPlayer != expected.target.isPlayer
                || actual.damage != expected.damage
                || actual.unarmed != expected.unarmed
                || actual.healthDamage != expected.healthDamage
                || actual.attackStrength != expected.attackStrength
                || mwmp::mechanics::isAttackAnimationOnly(actual)
                    != mwmp::mechanics::isAttackAnimationOnly(expected))
            {
                std::cerr << "Attack batch entry " << i
                          << " inherited fields from another actor\n";
                return false;
            }
        }
        return true;
    }

    bool checkActorListSyncPackets()
    {
        EffectPacket<mwmp::PacketActorList, mwmp::BaseActorList> encoder, decoder;
        mwmp::BaseActorList sent, received;
        sent.cell.blank();
        sent.cell.mData.mX = -2;
        sent.cell.mData.mY = -9;
        sent.authorityLeaseId = 10;
        sent.action = mwmp::BaseActorList::SET;
        mwmp::BaseActor actor;
        actor.refId = "imperial guard";
        actor.refNum = 1234;
        auto generatedActor = actor;
        generatedActor.refId = "rat";
        generatedActor.refNum = 0;
        generatedActor.mpNum = 8;
        sent.baseActors = {actor, generatedActor};
        received.isValid = true;
        decoder.bind(received);
        const auto populated = encoder.encode(sent);
        decoder.Read(populated);
        if (!encoder.isPacketValid() || !decoder.isPacketValid() || !received.isValid
            || received.baseActors.size() != 2 || received.baseActors[0].refId != actor.refId
            || received.baseActors[1].refId != "rat" || received.baseActors[1].mpNum != 8)
            return false;

        // The same reusable packet object handles a fresh authority's empty
        // whole-cell request after it has decoded a populated cached roster.
        sent.authorityLeaseId = 11;
        sent.action = mwmp::BaseActorList::REQUEST;
        sent.baseActors.clear();
        const auto request = encoder.encode(sent);
        decoder.Read(request);
        if (!encoder.isPacketValid() || !decoder.isPacketValid() || !received.isValid
            || received.action != mwmp::BaseActorList::REQUEST
            || received.count != 0 || !received.baseActors.empty()
            || received.authorityLeaseId != 11)
            return false;

        // A malformed follow-up may not partially replace that accepted state.
        auto truncated = populated;
        truncated.pop_back();
        decoder.Read(truncated);
        if (decoder.isPacketValid() || received.action != mwmp::BaseActorList::REQUEST
            || !received.baseActors.empty() || received.authorityLeaseId != 11)
            return false;
        decoder.Read(populated);
        return decoder.isPacketValid() && received.isValid
            && received.baseActors.size() == 2 && received.authorityLeaseId == 10;
    }

    bool checkContainerSyncPackets()
    {
        EffectPacket<mwmp::PacketContainer, mwmp::BaseObjectList> encoder, decoder;
        mwmp::BaseObjectList sent, received;
        sent.cell.blank();
        sent.cell.mData.mX = -2;
        sent.cell.mData.mY = -9;
        sent.packetOrigin = mwmp::SERVER_SCRIPT;
        sent.action = mwmp::BaseObjectList::SET;
        mwmp::BaseObject chest;
        chest.refId = "com_chest_01";
        chest.refNum = 125;
        mwmp::ContainerItem item;
        item.refId = "gold_001";
        item.count = 20;
        item.charge = -1;
        item.enchantmentCharge = -1;
        chest.containerItems = {item};
        auto emptyGenerated = chest;
        emptyGenerated.refId = "com_barrel_01";
        emptyGenerated.refNum = 0;
        emptyGenerated.mpNum = 9;
        emptyGenerated.containerItems.clear();
        sent.baseObjects = {chest, emptyGenerated};
        received.isValid = true;
        decoder.bind(received);
        const auto populated = encoder.encode(sent);
        decoder.Read(populated);
        if (!encoder.isPacketValid() || !decoder.isPacketValid() || !received.isValid
            || received.baseObjects.size() != 2
            || received.baseObjects[0].containerItems.size() != 1
            || received.baseObjects[0].containerItems[0].count != 20
            || !received.baseObjects[1].containerItems.empty()
            || received.baseObjects[1].mpNum != 9)
            return false;

        sent.action = mwmp::BaseObjectList::REQUEST;
        sent.containerSubAction = mwmp::BaseObjectList::NONE;
        sent.baseObjects.clear();
        const auto request = encoder.encode(sent);
        decoder.Read(request);
        if (!encoder.isPacketValid() || !decoder.isPacketValid() || !received.isValid
            || received.action != mwmp::BaseObjectList::REQUEST
            || received.baseObjectCount != 0 || !received.baseObjects.empty())
            return false;

        auto truncated = populated;
        truncated.pop_back();
        decoder.Read(truncated);
        if (decoder.isPacketValid() || received.action != mwmp::BaseObjectList::REQUEST
            || !received.baseObjects.empty())
            return false;
        decoder.Read(populated);
        return decoder.isPacketValid() && received.isValid
            && received.baseObjects.size() == 2
            && received.baseObjects[0].containerItems.size() == 1;
    }

    bool checkInvalidSyncPacketsCannotBeSent()
    {
        EffectPacket<mwmp::PacketActorList, mwmp::BaseActorList> actorEncoder;
        EffectPacket<mwmp::PacketContainer, mwmp::BaseObjectList> objectEncoder;
        EffectPacket<mwmp::PacketActorList, mwmp::BaseActorList> actorDecoder;
        EffectPacket<mwmp::PacketContainer, mwmp::BaseObjectList> objectDecoder;
        mwmp::BaseActorList acceptedActors;
        acceptedActors.isValid = true;
        acceptedActors.authorityLeaseId = 17;
        actorDecoder.bind(acceptedActors);
        mwmp::BaseObjectList acceptedObjects;
        acceptedObjects.isValid = true;
        acceptedObjects.action = mwmp::BaseObjectList::REQUEST;
        objectDecoder.bind(acceptedObjects);
        for (bool missingRefId : {false, true})
        {
            mwmp::BaseActorList actors;
            actors.cell.blank();
            actors.isValid = true;
            mwmp::BaseActor actor;
            actor.refId = missingRefId ? "" : "guard";
            actor.refNum = 123;
            actor.mpNum = missingRefId ? 0 : 456;
            actors.baseActors = {actor};
            if (actorEncoder.prepare(actors) || actorEncoder.isPacketValid()
                || actorEncoder.getCodecError() != mwmp::protocol::CodecError::InvalidValue)
            {
                std::cerr << "Invalid actor roster can escape as a partial outbound packet\n";
                return false;
            }
            // A peer may still send these malformed bytes. The decode must
            // fail atomically rather than commit a model with isValid=false.
            actorDecoder.Read(actorEncoder.encode(actors));
            if (actorDecoder.isPacketValid() || !acceptedActors.isValid
                || acceptedActors.authorityLeaseId != 17 || !acceptedActors.baseActors.empty())
                return false;

            mwmp::BaseObjectList objects;
            objects.cell.blank();
            objects.isValid = true;
            mwmp::BaseObject object;
            object.refId = actor.refId;
            object.refNum = actor.refNum;
            object.mpNum = actor.mpNum;
            objects.baseObjects = {object};
            if (objectEncoder.prepare(objects) || objectEncoder.isPacketValid()
                || objectEncoder.getCodecError() != mwmp::protocol::CodecError::InvalidValue)
            {
                std::cerr << "Invalid container can escape as a partial outbound packet\n";
                return false;
            }
            objectDecoder.Read(objectEncoder.encode(objects));
            if (objectDecoder.isPacketValid() || !acceptedObjects.isValid
                || acceptedObjects.action != mwmp::BaseObjectList::REQUEST
                || !acceptedObjects.baseObjects.empty())
                return false;
        }
        return true;
    }
}

int main()
{
    const bool forward = checkBatch(false);
    const bool reverse = checkBatch(true);
    const bool spells = checkActiveSpellPackets();
    if (!spells)
        std::cerr << "Received spells lost magnitude, lifetime, caster, or presentation flags\n";
    const bool batch = checkCastBatchSurvivesScriptEvent();
    if (!batch)
        std::cerr << "Publishing NPC effects destroyed the remaining cast batch\n";
    const bool actors = checkActorListSyncPackets();
    const bool containers = checkContainerSyncPackets();
    if (!actors || !containers)
        std::cerr << "Actor/container synchronization request or cached-state round trip failed\n";
    return forward && reverse && spells && batch && actors && containers
        && checkInvalidSyncPacketsCannotBeSent() && checkPlayerFistPackets() ? 0 : 1;
}
