#include <apps/openmw-mp/MagicContent.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/cellref.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm3/loadspel.hpp>

#include <chrono>
#include <fstream>
#include <iostream>

namespace
{
    template <class Record>
    void writeRecord(ESM::ESMWriter& writer, const Record& record)
    {
        writer.startRecord(Record::sRecordId);
        record.save(writer);
        writer.endRecord(Record::sRecordId);
    }

    void writeFixture(const std::filesystem::path& path)
    {
        std::ofstream stream(path, std::ios::binary);
        ESM::ESMWriter writer;
        // Legacy content reloads the engine's real fixed flags for WeaknessToFire.
        writer.setFormatVersion(ESM::DefaultFormatVersion);
        writer.save(stream);
        for (const auto& id : { "fEffectCostMult", "fNPCbaseMagickaMult", "fFatigueBase", "fFatigueMult" })
        {
            ESM::GameSetting setting{};
            setting.mId = ESM::RefId::stringRefId(id);
            setting.mValue = ESM::Variant(1.0f);
            writeRecord(writer, setting);
        }
        ESM::MagicEffect effect;
        effect.blank();
        effect.mId = ESM::MagicEffect::WeaknessToFire;
        effect.mData.mSchool = ESM::Skill::Destruction;
        effect.mData.mBaseCost = 1;
        writeRecord(writer, effect);
        ESM::Spell spell;
        spell.blank();
        spell.mId = ESM::RefId::stringRefId("test weakness");
        spell.mName = "Timed weakness";
        spell.mData.mType = ESM::Spell::ST_Spell;
        spell.mData.mFlags = ESM::Spell::F_Always;
        ESM::ENAMstruct application{};
        application.mEffectID = effect.mId;
        application.mRange = ESM::RT_Target;
        application.mDuration = 10;
        application.mMagnMin = application.mMagnMax = 25;
        spell.mEffects.populate({ application });
        writeRecord(writer, spell);
        ESM::NPC npc;
        npc.blank();
        npc.mId = ESM::RefId::stringRefId("test merchant");
        npc.mNpdt.mHealth = npc.mNpdt.mMana = npc.mNpdt.mFatigue = 100;
        npc.mInventory.mList = {
            { -5, ESM::RefId::stringRefId("restocking scroll") },
            { 2, ESM::RefId::stringRefId("ordinary item") },
            { 0, ESM::RefId::stringRefId("empty item") },
        };
        writeRecord(writer, npc);
        ESM::Cell cell;
        cell.blank();
        cell.mName = "test cell";
        cell.mData.mFlags = ESM::Cell::Interior | ESM::Cell::HasWater;
        cell.mWater = 0;
        writer.startRecord(ESM::Cell::sRecordId);
        cell.save(writer);
        ESM::CellRef ref;
        ref.blank();
        ref.mRefID = npc.mId;
        ref.mRefNum = {1, 0};
        ref.mPos.pos[0] = 100;
        ref.mPos.pos[2] = 50;
        ref.save(writer);
        ref.mRefNum.mIndex = 2;
        ref.mPos.pos[2] = -50; // Original underwater placements are ineligible.
        ref.save(writer);
        ref.mRefNum.mIndex = 3;
        ref.mPos.pos[2] = 50;
        ref.save(writer, false, false, true); // Deleted placement is ineligible.
        ref.mRefNum.mIndex = 4;
        ref.mRefID = ESM::RefId::stringRefId("ordinary item");
        ref.save(writer);
        writer.endRecord(ESM::Cell::sRecordId);
        writer.close();
    }

    bool checkContent(const std::filesystem::path& directory)
    {
        using namespace mwmp::mechanics;
        const auto content = mwmp::loadCanonicalMagicContent({ { directory }, { "test.esm" } });
        if (content.recoveryAnchors.size() != 1
            || content.recoveryAnchors[0].identity.cell != "test cell"
            || content.recoveryAnchors[0].identity.refNum != 1
            || content.recoveryAnchors[0].refId != "test merchant"
            || content.recoveryAnchors[0].transform.position.x != 100
            || content.recoveryAnchors[0].transform.position.z != 50)
        {
            std::cerr << "NPC recovery did not retain only the live dry content placement\n";
            return false;
        }
        if (content.definitions.size() != 1)
            return false;
        if (content.actorTemplates.size() != 1 || content.actorTemplates[0].inventory.size() != 2
            || content.actorTemplates[0].inventory[0].refId != "restocking scroll"
            || content.actorTemplates[0].inventory[0].count != 5
            || content.actorTemplates[0].inventory[1].count != 2)
        {
            std::cerr << "Restocking NPC inventory was dropped or retained a negative count\n";
            return false;
        }
        const auto& definition = content.definitions.front();
        if (definition.effects.size() != 1 || definition.effects[0].kind != SpellEffectKind::Timed
            || definition.effects[0].duration != 10)
        {
            std::cerr << "AppliedOnce weakness lost its ten-second duration during content loading\n";
            return false;
        }
        SpellResolver resolver;
        const CombatantId caster{ CombatantKind::Actor, 1, "test cell" };
        const CombatantId target{ CombatantKind::Player, 2, {} };
        SpellCombatantState state;
        state.health = state.maximumHealth = 100;
        state.magicka = state.maximumMagicka = 100;
        state.fatigue = state.maximumFatigue = 100;
        if (!resolver.upsertDefinition(definition) || !resolver.upsertCombatant(caster, state)
            || !resolver.upsertCombatant(target, state))
            return false;
        const auto cast = resolver.resolve({ caster, target, definition.id, 1 }, 0, 0);
        if (!cast.applied() || cast.applications.size() != 1 || !cast.applications[0].activeSpell)
            return false;
        const auto& active = *cast.applications[0].activeSpell;
        if (cast.applications[0].target != target || active.effects.size() != 1
            || active.effects[0].magnitude != 25 || active.effects[0].timeLeft != 10)
            return false;
        ActiveEffectLedger ledger;
        if (!ledger.apply(target, ActiveEffectAction::Add, { active }).applied()
            || !ledger.advance(9).applied() || !ledger.snapshot(target)
            || !ledger.advance(1).applied() || ledger.snapshot(target))
            return false;
        return true;
    }
}

int main()
{
    const auto directory = std::filesystem::temp_directory_path()
        / ("tes3mp-magic-content-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    bool passed = false;
    try
    {
        writeFixture(directory / "test.esm");
        passed = checkContent(directory);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
    }
    std::filesystem::remove_all(directory);
    return passed ? 0 : 1;
}
