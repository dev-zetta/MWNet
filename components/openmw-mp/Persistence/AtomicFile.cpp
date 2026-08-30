#include "AtomicFile.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#else
#define NOMINMAX
#include <windows.h>
#endif

namespace mwmp::persistence
{
    namespace
    {
        std::atomic<std::uint64_t> sTemporarySequence = 0;

        bool failAt(const AtomicWriteOptions& options, AtomicWriteStage stage,
            std::string& error)
        {
            if (!options.injectFailure || !options.injectFailure(stage))
                return false;
            error = "injected atomic persistence failure";
            return true;
        }

        std::filesystem::path temporaryPath(const std::filesystem::path& target)
        {
            auto path = target;
            path += ".tmp." + std::to_string(++sTemporarySequence);
            return path;
        }

#ifndef _WIN32
        bool writeAndSync(const std::filesystem::path& path,
            std::span<const std::byte> contents, bool ownerOnly,
            const AtomicWriteOptions* stages, std::string& error)
        {
            const mode_t mode = ownerOnly ? S_IRUSR | S_IWUSR
                                          : S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
            const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                mode);
            if (descriptor < 0)
            {
                error = "failed to create temporary file: " + std::string(std::strerror(errno));
                return false;
            }

            std::size_t written = 0;
            while (written < contents.size())
            {
                const ssize_t result = ::write(descriptor, contents.data() + written,
                    contents.size() - written);
                if (result < 0 && errno == EINTR)
                    continue;
                if (result <= 0)
                {
                    error = "failed to write temporary file: "
                        + std::string(std::strerror(errno));
                    ::close(descriptor);
                    return false;
                }
                written += static_cast<std::size_t>(result);
            }
            if (stages != nullptr
                && failAt(*stages, AtomicWriteStage::AfterTemporaryWrite, error))
            {
                ::close(descriptor);
                return false;
            }
            if (::fsync(descriptor) != 0)
            {
                error = "failed to sync temporary file: " + std::string(std::strerror(errno));
                ::close(descriptor);
                return false;
            }
            if (stages != nullptr
                && failAt(*stages, AtomicWriteStage::AfterTemporarySync, error))
            {
                ::close(descriptor);
                return false;
            }
            if (::close(descriptor) != 0)
            {
                error = "failed to close temporary file: " + std::string(std::strerror(errno));
                return false;
            }
            return true;
        }

        void syncDirectory(const std::filesystem::path& directory) noexcept
        {
            const int descriptor = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (descriptor >= 0)
            {
                (void)::fsync(descriptor);
                (void)::close(descriptor);
            }
        }
#else
        bool writeAndSync(const std::filesystem::path& path,
            std::span<const std::byte> contents, bool ownerOnly,
            const AtomicWriteOptions* stages, std::string& error)
        {
            (void)ownerOnly;
            const HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                error = "failed to create temporary file";
                return false;
            }
            std::size_t written = 0;
            while (written < contents.size())
            {
                const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
                    contents.size() - written, std::numeric_limits<DWORD>::max()));
                DWORD completed = 0;
                if (!WriteFile(handle, contents.data() + written, chunk, &completed, nullptr)
                    || completed == 0)
                {
                    error = "failed to write temporary file";
                    CloseHandle(handle);
                    return false;
                }
                written += completed;
            }
            if (stages != nullptr
                && failAt(*stages, AtomicWriteStage::AfterTemporaryWrite, error))
            {
                CloseHandle(handle);
                return false;
            }
            if (!FlushFileBuffers(handle))
            {
                error = "failed to sync temporary file";
                CloseHandle(handle);
                return false;
            }
            if (stages != nullptr
                && failAt(*stages, AtomicWriteStage::AfterTemporarySync, error))
            {
                CloseHandle(handle);
                return false;
            }
            if (!CloseHandle(handle))
            {
                error = "failed to close temporary file";
                return false;
            }
            return true;
        }

        void syncDirectory(const std::filesystem::path&) noexcept {}
#endif

        bool installTemporary(const std::filesystem::path& temporary,
            const std::filesystem::path& target, std::string& error)
        {
            std::error_code filesystemError;
#ifdef _WIN32
            if (MoveFileExW(temporary.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return true;
            error = "failed to atomically replace target file";
            return false;
#else
            std::filesystem::rename(temporary, target, filesystemError);
            if (!filesystemError)
                return true;
            error = "failed to atomically replace target file: " + filesystemError.message();
            return false;
#endif
        }

        bool createBackup(const std::filesystem::path& target,
            const AtomicWriteOptions& options, std::string& error)
        {
            std::error_code filesystemError;
            if (!std::filesystem::exists(target, filesystemError))
                return !filesystemError;
            if (filesystemError)
            {
                error = "failed to inspect target before backup: " + filesystemError.message();
                return false;
            }

            std::ifstream input(target, std::ios::binary | std::ios::ate);
            if (!input)
            {
                error = "failed to open target for backup";
                return false;
            }
            const auto size = input.tellg();
            if (size < 0 || static_cast<std::uintmax_t>(size) > options.maximumBytes)
            {
                error = "target exceeds persistence size limit";
                return false;
            }
            std::vector<std::byte> contents(static_cast<std::size_t>(size));
            input.seekg(0);
            input.read(reinterpret_cast<char*>(contents.data()), size);
            if (!input)
            {
                error = "failed to read target for backup";
                return false;
            }

            auto backup = target;
            backup += ".bak";
            const auto temporary = temporaryPath(backup);
            if (!writeAndSync(temporary, contents, options.ownerOnly, nullptr, error))
            {
                std::filesystem::remove(temporary, filesystemError);
                return false;
            }
            if (!installTemporary(temporary, backup, error))
            {
                std::filesystem::remove(temporary, filesystemError);
                return false;
            }
            return true;
        }
    }

    bool writeFileAtomically(const std::filesystem::path& path,
        std::span<const std::byte> contents, const AtomicWriteOptions& options,
        std::string& error)
    {
        error.clear();
        if (path.empty() || contents.size() > options.maximumBytes)
        {
            error = path.empty() ? "atomic persistence target is empty"
                                 : "atomic persistence payload exceeds its size limit";
            return false;
        }

        std::error_code filesystemError;
        const auto directory = path.has_parent_path() ? path.parent_path()
                                                      : std::filesystem::path(".");
        std::filesystem::create_directories(directory, filesystemError);
        if (filesystemError)
        {
            error = "failed to create persistence directory: " + filesystemError.message();
            return false;
        }

        const auto temporary = temporaryPath(path);
        if (failAt(options, AtomicWriteStage::BeforeTemporaryOpen, error))
            return false;
        if (!writeAndSync(temporary, contents, options.ownerOnly, &options, error))
        {
            std::filesystem::remove(temporary, filesystemError);
            return false;
        }
        if (options.backup == BackupPolicy::MaintainOne
            && !createBackup(path, options, error))
        {
            std::filesystem::remove(temporary, filesystemError);
            return false;
        }
        if (failAt(options, AtomicWriteStage::AfterBackup, error)
            || failAt(options, AtomicWriteStage::BeforeReplace, error))
        {
            std::filesystem::remove(temporary, filesystemError);
            return false;
        }
        if (!installTemporary(temporary, path, error))
        {
            std::filesystem::remove(temporary, filesystemError);
            return false;
        }
        syncDirectory(directory);
        if (failAt(options, AtomicWriteStage::AfterReplace, error))
            return false;
        return true;
    }
}
