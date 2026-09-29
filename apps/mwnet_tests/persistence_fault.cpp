#include <components/openmw-mp/Persistence/AtomicFile.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
    using mwmp::persistence::AtomicWriteOptions;
    using mwmp::persistence::AtomicWriteStage;
    using mwmp::persistence::writeFileAtomically;

    constexpr int sInjectedExitCode = 73;
    constexpr std::array sStages{
        AtomicWriteStage::BeforeTemporaryOpen,
        AtomicWriteStage::AfterTemporaryWrite,
        AtomicWriteStage::AfterTemporarySync,
        AtomicWriteStage::AfterBackup,
        AtomicWriteStage::BeforeReplace,
        AtomicWriteStage::AfterReplace,
    };

    std::span<const std::byte> bytes(std::string_view value)
    {
        return std::as_bytes(std::span(value));
    }

    std::string read(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>() };
    }

    [[noreturn]] void fail(std::string message)
    {
        throw std::runtime_error(std::move(message));
    }

    void require(bool condition, std::string_view message)
    {
        if (!condition)
            fail(std::string(message));
    }

    [[noreturn]] void crashWrite(
        const std::filesystem::path& target, AtomicWriteStage stage)
    {
        AtomicWriteOptions options;
        options.injectFailure = [stage](AtomicWriteStage current) {
            if (current == stage)
                std::_Exit(sInjectedExitCode);
            return false;
        };
        std::string error;
        const bool written = writeFileAtomically(
            target, bytes("new-complete-record"), options, error);
        std::_Exit(written ? 0 : 74);
    }

#ifdef _WIN32
    std::string quote(std::string_view value)
    {
        std::string result = "\"";
        for (const char character : value)
        {
            if (character == '"')
                result += '\\';
            result += character;
        }
        result += '"';
        return result;
    }
#endif

    void runCrash(const std::filesystem::path& executable,
        const std::filesystem::path& target, std::size_t stage)
    {
#ifdef _WIN32
        const std::string command = quote(executable.string()) + " --child "
            + quote(target.string()) + " " + std::to_string(stage);
        require(std::system(command.c_str()) != 0,
            "fault child unexpectedly completed its write");
#else
        (void)executable;
        const pid_t child = ::fork();
        require(child >= 0, "failed to fork persistence fault child");
        if (child == 0)
            crashWrite(target, sStages.at(stage));
        int status = 0;
        require(::waitpid(child, &status, 0) == child,
            "failed to wait for persistence fault child");
        require(WIFEXITED(status) && WEXITSTATUS(status) == sInjectedExitCode,
            "persistence fault child exited at the wrong point");
#endif
    }

    void validateCompleteFiles(const std::filesystem::path& directory)
    {
        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            if (!entry.is_regular_file())
                continue;
            const std::string contents = read(entry.path());
            require(contents == "old-complete-record"
                    || contents == "new-complete-record"
                    || contents == "recovered-complete-record",
                "a persistence artifact contains a partial record");
        }
    }

    void runDriver(const std::filesystem::path& executable)
    {
        const auto root = std::filesystem::temp_directory_path()
            / ("mwnet-persistence-fault-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        for (std::size_t index = 0; index < sStages.size(); ++index)
        {
            const auto directory = root / std::to_string(index);
            const auto target = directory / "record.json";
            std::string error;
            AtomicWriteOptions options;
            require(writeFileAtomically(
                    target, bytes("old-complete-record"), options, error),
                "failed to create old persistence record");

            runCrash(executable, target, index);
            const std::string retained = read(target);
            require(retained == "old-complete-record"
                    || retained == "new-complete-record",
                "process termination left no complete target record");
            validateCompleteFiles(directory);

            require(writeFileAtomically(
                    target, bytes("recovered-complete-record"), options, error),
                "a post-crash persistence write was blocked by stale state");
            require(read(target) == "recovered-complete-record",
                "post-crash persistence recovery did not replace the target");
            validateCompleteFiles(directory);
        }
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 4 && std::string_view(argv[1]) == "--child")
        {
            const std::size_t stage = std::stoul(argv[3]);
            require(stage < sStages.size(), "invalid fault stage");
            crashWrite(argv[2], sStages.at(stage));
        }
        require(argc == 1, "unexpected persistence fault test arguments");
        runDriver(std::filesystem::absolute(argv[0]));
        std::cout << "Atomic persistence survived termination at all "
                  << sStages.size() << " stages.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "mwnet-persistence-fault: " << error.what() << '\n';
        return 1;
    }
}
