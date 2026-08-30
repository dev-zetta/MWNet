#ifndef OPENMW_MP_PERSISTENCE_ATOMIC_FILE_HPP
#define OPENMW_MP_PERSISTENCE_ATOMIC_FILE_HPP

#include <cstddef>
#include <filesystem>
#include <functional>
#include <span>
#include <string>

namespace mwmp::persistence
{
    enum class BackupPolicy
    {
        None,
        MaintainOne,
    };

    enum class AtomicWriteStage
    {
        BeforeTemporaryOpen,
        AfterTemporaryWrite,
        AfterTemporarySync,
        AfterBackup,
        BeforeReplace,
        AfterReplace,
    };

    struct AtomicWriteOptions
    {
        BackupPolicy backup = BackupPolicy::MaintainOne;
        bool ownerOnly = false;
        std::size_t maximumBytes = 64U * 1024U * 1024U;
        std::function<bool(AtomicWriteStage)> injectFailure;
    };

    bool writeFileAtomically(const std::filesystem::path& path,
        std::span<const std::byte> contents, const AtomicWriteOptions& options,
        std::string& error);
}

#endif
