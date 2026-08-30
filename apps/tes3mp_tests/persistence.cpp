#include <components/openmw-mp/Persistence/AtomicFile.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace mwmp::persistence;

    int sFailures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "persistence.cpp:" << line << ": expectation failed: "
                  << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    std::span<const std::byte> bytes(std::string_view value)
    {
        return std::as_bytes(std::span(value));
    }

    std::string read(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    }
}

int runPersistenceTests()
{
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory = std::filesystem::temp_directory_path()
        / ("tes3mp-persistence-" + unique);
    const auto target = directory / "record.json";
    std::string error;
    AtomicWriteOptions options;
    EXPECT(writeFileAtomically(target, bytes("old-record"), options, error));
    EXPECT(read(target) == "old-record");

    constexpr std::array stages{
        AtomicWriteStage::BeforeTemporaryOpen,
        AtomicWriteStage::AfterTemporaryWrite,
        AtomicWriteStage::AfterTemporarySync,
        AtomicWriteStage::AfterBackup,
        AtomicWriteStage::BeforeReplace,
        AtomicWriteStage::AfterReplace,
    };
    for (const AtomicWriteStage stage : stages)
    {
        EXPECT(writeFileAtomically(target, bytes("old-record"), options, error));
        AtomicWriteOptions injected;
        injected.injectFailure = [stage](AtomicWriteStage current) {
            return current == stage;
        };
        EXPECT(!writeFileAtomically(target, bytes("new-record"), injected, error));
        const std::string retained = read(target);
        EXPECT(retained == "old-record" || retained == "new-record");
    }

    options.backup = BackupPolicy::MaintainOne;
    EXPECT(writeFileAtomically(target, bytes("final-record"), options, error));
    auto backup = target;
    backup += ".bak";
    EXPECT(read(backup) == "new-record" || read(backup) == "old-record");

    options.backup = BackupPolicy::None;
    options.ownerOnly = true;
    EXPECT(writeFileAtomically(target, bytes("credential-record"), options, error));
    EXPECT(read(target) == "credential-record");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    return sFailures;
}
