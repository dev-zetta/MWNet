#include <apps/openmw-mp/MagicContent.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/cellref.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadbsgn.hpp>
#include <components/esmloader/esmdata.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm/attr.hpp>
#include <components/esm3/loadspel.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string_view>

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
        for (const auto& id : { "fEffectCostMult", "fNPCbaseMagickaMult", "fPCbaseMagickaMult", "fFatigueBase", "fFatigueMult", "fMinHandToHandMult", "fMaxHandToHandMult", "fHandtoHandHealthPer", "fFatigueReturnBase", "fFatigueReturnMult" })
        {
            ESM::GameSetting setting{};
            setting.mId = ESM::RefId::stringRefId(id);
            setting.mValue = ESM::Variant(1.0f);
            writeRecord(writer, setting);
        }
        ESM::Race race;
        race.blank();
        race.mId = ESM::RefId::stringRefId("test race");
        race.mData.mFlags = ESM::Race::Playable;
        for (int index = 0; index < 8; ++index)
        {
            race.mData.setAttribute(ESM::Attribute::indexToRefId(index), true, 40);
            race.mData.setAttribute(ESM::Attribute::indexToRefId(index), false, 30);
        }
        race.mData.mBonus[0] = { ESM::Skill::Block, 10 };
        race.mPowers.mList = { ESM::RefId::stringRefId("test racial magicka") };
        writeRecord(writer, race);
        ESM::Class characterClass;
        characterClass.blank();
        characterClass.mId = ESM::RefId::stringRefId("test class");
        characterClass.mData.mIsPlayable = true;
        characterClass.mData.mSpecialization = ESM::Class::Combat;
        characterClass.mData.mAttribute = { ESM::Attribute::Strength, ESM::Attribute::Endurance };
        for (int index = 0; index < 5; ++index)
        {
            characterClass.mData.mSkills[index][1] = ESM::Skill::indexToRefId(index);
            characterClass.mData.mSkills[index][0] = ESM::Skill::indexToRefId(index + 5);
        }
        writeRecord(writer, characterClass);
        for (int index = 0; index < 27; ++index)
        {
            ESM::Skill skill;
            skill.blank();
            skill.mId = ESM::StringRefId(ESM::Skill::indexToRefId(index).getRefIdString());
            skill.mData.mAttribute = ESM::Attribute::Strength;
            skill.mData.mSpecialization = index % 3;
            writeRecord(writer, skill);
        }
        ESM::Spell ability;
        ability.blank();
        ability.mId = ESM::RefId::stringRefId("test racial magicka");
        ability.mData.mType = ESM::Spell::ST_Ability;
        ESM::ENAMstruct passive{};
        passive.mEffectID = ESM::MagicEffect::FortifyMaximumMagicka;
        passive.mMagnMin = passive.mMagnMax = 10;
        ability.mEffects.populate({ passive });
        writeRecord(writer, ability);
        ability.mId = ESM::RefId::stringRefId("test sign endurance");
        passive.mEffectID = ESM::MagicEffect::FortifyAttribute;
        passive.mAttribute = ESM::Attribute::Endurance;
        passive.mMagnMin = passive.mMagnMax = 25;
        ability.mEffects.populate({ passive });
        writeRecord(writer, ability);
        ESM::BirthSign sign;
        sign.blank();
        sign.mId = ESM::RefId::stringRefId("test sign");
        sign.mPowers.mList = { ability.mId };
        writeRecord(writer, sign);
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
        npc.mNpdt.mSkills[ESM::Skill::HandToHand] = 40;
        npc.mNpdt.mAttributes[ESM::Attribute::Endurance] = 50;
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

    bool checkCharacterCreation(const mwmp::CanonicalCharacterContent& content)
    {
        ESM::NPC appearance;
        appearance.blank();
        appearance.mRace = ESM::RefId::stringRefId("test race");
        ESM::Class selected;
        selected.blank();
        selected.mId = ESM::RefId::stringRefId("test class");
        std::string error;
        const auto male = mwmp::deriveCanonicalPlayerCharacter(content, appearance, selected, "test sign", error);
        if (!male || male->progression.attributes[0].base != 50
            || male->progression.attributes[5].base != 75 || male->progression.attributes[5].modifier != 0
            || male->progression.attributes[5].current != 0 || male->progression.skills[0].base != 45
            || male->progression.skills[1].base != 30 || male->progression.skills[5].base != 15
            || male->dynamicBases != std::array<float, 3>{ 50, 80, 205 }
            || male->dynamicMaximums != male->dynamicBases || male->progression.level != 1)
        {
            std::cerr << "Canonical player creation formula mismatch: " << error << '\n';
            return false;
        }
        appearance.setIsMale(false);
        const auto female = mwmp::deriveCanonicalPlayerCharacter(content, appearance, selected, "test sign", error);
        if (!female || female->dynamicBases != std::array<float, 3>{ 40, 60, 165 }) return false;
        selected.mId = ESM::RefId::stringRefId("unknown class");
        if (mwmp::deriveCanonicalPlayerCharacter(content, appearance, selected, "test sign", error)) return false;
        selected = content.records->mClasses.front();
        selected.mId = {};
        if (!mwmp::deriveCanonicalPlayerCharacter(content, appearance, selected, "test sign", error)) return false;
        selected.mData.mSkills[1][0] = selected.mData.mSkills[0][1];
        if (mwmp::deriveCanonicalPlayerCharacter(content, appearance, selected, "test sign", error)) return false;
        selected = content.records->mClasses.front();
        selected.mId = {};
        selected.mData.mAttribute[1] = selected.mData.mAttribute[0];
        if (mwmp::deriveCanonicalPlayerCharacter(content, appearance, selected, "test sign", error)) return false;
        selected = content.records->mClasses.front();
        selected.mId = {};
        selected.mData.mSpecialization = 3;
        if (mwmp::deriveCanonicalPlayerCharacter(content, appearance, selected, "test sign", error)) return false;
        selected = content.records->mClasses.front();
        selected.mId = {};
        selected.mData.mIsPlayable = false;
        if (mwmp::deriveCanonicalPlayerCharacter(content, appearance, selected, "test sign", error)) return false;
        selected.mId = ESM::RefId::stringRefId("test class");
        // Default classes must use canonical definitions, not forged wire fields.
        if (!mwmp::deriveCanonicalPlayerCharacter(content, appearance, selected, "test sign", error)) return false;
        appearance.mRace = ESM::RefId::stringRefId("unknown race");
        if (mwmp::deriveCanonicalPlayerCharacter(content, appearance, selected, "test sign", error)) return false;
        appearance.mRace = ESM::RefId::stringRefId("test race");
        if (mwmp::deriveCanonicalPlayerCharacter(content, appearance, selected, "unknown sign", error)) return false;
        auto changed = std::make_shared<EsmLoader::EsmData>();
        changed->mRaces = content.records->mRaces;
        changed->mClasses = content.records->mClasses;
        changed->mSkills = content.records->mSkills;
        changed->mBirthSigns = content.records->mBirthSigns;
        changed->mSpells = content.records->mSpells;
        mwmp::CanonicalCharacterContent modified{ changed, content.baseMagickaMultiplier };
        auto signAbility = std::find_if(changed->mSpells.begin(), changed->mSpells.end(),
            [](const auto& spell) { return spell.mId == "test sign endurance"; });
        auto& passive = signAbility->mEffects.mList[0].mData;
        passive.mEffectID = ESM::MagicEffect::DrainAttribute;
        passive.mMagnMin = passive.mMagnMax = 100;
        const auto drained = mwmp::deriveCanonicalPlayerCharacter(modified, appearance, selected, "test sign", error);
        if (!drained || drained->progression.attributes[5].damage != 100) return false;
        for (const auto& unsupported : { ESM::MagicEffect::DrainHealth,
                ESM::MagicEffect::DamageHealth, ESM::MagicEffect::FortifyMagicka,
                ESM::MagicEffect::RestoreAttribute, ESM::MagicEffect::AbsorbSkill })
        {
            passive.mEffectID = unsupported;
            if (mwmp::deriveCanonicalPlayerCharacter(modified, appearance, selected, "test sign", error)
                || error.find("unsupported") == std::string::npos) return false;
        }
        passive.mEffectID = ESM::MagicEffect::FortifyAttribute;
        passive.mMagnMax = 101;
        if (mwmp::deriveCanonicalPlayerCharacter(modified, appearance, selected, "test sign", error)
            || error.find("nonconstant") == std::string::npos) return false;
        return true;
    }

    bool checkCanonicalChoices(const std::filesystem::path& directory)
    {
        mwmp::MagicContentOptions options;
        options.dataDirectories = { directory };
        options.contentFiles = { "Morrowind.esm" };
        for (const auto* name : { "Tribunal.esm", "Bloodmoon.esm" })
            if (std::filesystem::is_regular_file(directory / name)) options.contentFiles.emplace_back(name);
        const auto content = mwmp::loadCanonicalMagicContent(options);
        std::size_t choices = 0;
        for (const auto& race : content.characterCreation.records->mRaces)
        {
            if ((race.mData.mFlags & ESM::Race::Playable) == 0) continue;
            for (const auto& characterClass : content.characterCreation.records->mClasses)
            {
                if (!characterClass.mData.mIsPlayable) continue;
                for (const auto& sign : content.characterCreation.records->mBirthSigns)
                    for (bool male : { false, true })
                    {
                        ESM::NPC appearance;
                        appearance.blank();
                        appearance.mRace = race.mId;
                        appearance.setIsMale(male);
                        std::string error;
                        if (!mwmp::deriveCanonicalPlayerCharacter(content.characterCreation, appearance,
                                characterClass, sign.mId.getRefIdString(), error))
                        {
                            std::cerr << "Canonical creation rejected " << race.mId << '/' << characterClass.mId
                                      << '/' << sign.mId << ": " << error << '\n';
                            return false;
                        }
                        ++choices;
                    }
            }
        }
        std::cout << "Canonical race/class/birthsign/sex choices validated: " << choices << '\n';
        return choices > 0;
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
        if (!content.actorTemplates[0].isNpc || content.actorTemplates[0].handToHand != 40
            || content.actorTemplates[0].endurance != 50 || content.unarmedMinimum != 1
            || content.unarmedMaximum != 1 || content.unarmedHealth != 1
            || content.fatigueRecoveryBase != 1 || content.fatigueRecoveryPerEndurance != 1)
        {
            std::cerr << "Canonical fist and recovery profile did not match loaded content\n";
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
        return checkCharacterCreation(content.characterCreation);
    }
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--canonical-data")
    {
        try { return checkCanonicalChoices(argv[2]) ? 0 : 1; }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    }
    const auto directory = std::filesystem::temp_directory_path()
        / ("mwnet-magic-content-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
