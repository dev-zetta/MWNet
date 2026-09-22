#include <components/openmw-mp/Mechanics/AttackAnimation.hpp>
#include <components/openmw-mp/NetworkActiveSpell.hpp>
#include <components/openmw-mp/ScopedActorList.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorSpellsActive.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerSpellsActive.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAttack.hpp>

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

    bool checkBatch(bool reverse)
    {
        mwmp::BaseActor hit;
        hit.refNum = 175651;
        hit.attack.target.isPlayer = true;
        hit.attack.target.guid = mwmp::transport::TransportConnectionId{ 7 };
        hit.attack.isHit = true;
        hit.attack.damage = 12;

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
    return forward && reverse && spells && batch ? 0 : 1;
}
