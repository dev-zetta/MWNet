#ifndef OPENMW_MP_SERVER_MAGIC_CONTENT_HPP
#define OPENMW_MP_SERVER_MAGIC_CONTENT_HPP

#include <components/openmw-mp/Mechanics/SpellResolver.hpp>

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
        std::unordered_set<std::string> consumableItems;
    };

    CanonicalMagicContent loadCanonicalMagicContent(
        const MagicContentOptions& options);
}

#endif
