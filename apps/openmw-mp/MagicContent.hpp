#ifndef OPENMW_MP_SERVER_MAGIC_CONTENT_HPP
#define OPENMW_MP_SERVER_MAGIC_CONTENT_HPP

#include <components/openmw-mp/Mechanics/SpellResolver.hpp>
#include <components/openmw-mp/Mechanics/ActorMagicRegistry.hpp>

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace mwmp
{
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
        std::vector<mechanics::SpellDefinition> definitions;
        std::vector<mechanics::ActorMagicTemplate> actorTemplates;
        std::unordered_set<std::string> consumableItems;
        std::unordered_set<std::string> directConsumableItems;
        double fatigueBase = 1;
        double fatigueMultiplier = 0;
    };

    CanonicalMagicContent loadCanonicalMagicContent(
        const MagicContentOptions& options);
}

#endif
