#ifndef OPENMW_MP_SERVER_MAGIC_CONTENT_HPP
#define OPENMW_MP_SERVER_MAGIC_CONTENT_HPP

#include <components/openmw-mp/Mechanics/SpellResolver.hpp>
#include <components/openmw-mp/Mechanics/ActorMagicRegistry.hpp>
#include <components/openmw-mp/Mechanics/ActorRecovery.hpp>
#include <components/openmw-mp/Mechanics/PlayerProgressionLedger.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadnpc.hpp>

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace EsmLoader { struct EsmData; }

namespace mwmp
{
    struct CanonicalCharacterContent
    {
        std::shared_ptr<const EsmLoader::EsmData> records;
        float baseMagickaMultiplier = 1;
    };

    struct CanonicalPlayerCharacter
    {
        mechanics::PlayerProgressionState progression;
        std::array<float, 3> dynamicBases{};
        std::array<float, 3> dynamicMaximums{};
    };

    std::optional<CanonicalPlayerCharacter> deriveCanonicalPlayerCharacter(
        const CanonicalCharacterContent& content, const ESM::NPC& appearance,
        const ESM::Class& selectedClass, const std::string& birthsign, std::string& error);

    struct MagicContentOptions
    {
        std::vector<std::filesystem::path> dataDirectories;
        std::vector<std::string> contentFiles;
        std::string encoding = "win1252";
        double maximumTouchRange = 192.0;
        double maximumTargetRange = 8192.0;
    };

    struct CanonicalMagicContent
    {
        CanonicalCharacterContent characterCreation;
        std::vector<mechanics::SpellDefinition> definitions;
        std::vector<mechanics::ActorMagicTemplate> actorTemplates;
        std::vector<mechanics::ActorRecoveryAnchor> recoveryAnchors;
        std::unordered_set<std::string> consumableItems;
        std::unordered_set<std::string> directConsumableItems;
        double unarmedMinimum = 0.1, unarmedMaximum = 0.5, unarmedHealth = 0.1;
        double fatigueRecoveryBase = 2;
        double fatigueRecoveryPerEndurance = 0.02;
        double fatigueBase = 1;
        double fatigueMultiplier = 0;
    };

    CanonicalMagicContent loadCanonicalMagicContent(
        const MagicContentOptions& options);
}

#endif
