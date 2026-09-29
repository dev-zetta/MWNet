#include <components/openmw-mp/Persistence/AtomicFile.hpp>
#include <components/openmw-mp/Persistence/PersistenceService.hpp>

#include <array>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <mutex>
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

    void testPersistenceService()
    {
        std::mutex mutex;
        std::condition_variable condition;
        bool firstStarted = false;
        bool releaseFirst = false;
        std::atomic_int completions = 0;
        std::vector<std::pair<std::string, std::string>> writes;

        PersistenceService service(1,
            [&](const std::filesystem::path& path, std::span<const std::byte> contents,
                const AtomicWriteOptions&, std::string&) {
                const std::string value(reinterpret_cast<const char*>(contents.data()),
                    contents.size());
                if (path == "block")
                {
                    std::unique_lock lock(mutex);
                    firstStarted = true;
                    condition.notify_all();
                    condition.wait(lock, [&] { return releaseFirst; });
                }
                std::lock_guard lock(mutex);
                writes.emplace_back(path.generic_string(), value);
                return true;
            });

        EXPECT(service.save("block", bytes("first")) == QueueDecision::Queued);
        {
            std::unique_lock lock(mutex);
            condition.wait(lock, [&] { return firstStarted; });
        }
        EXPECT(service.save("record.json", bytes("old"), {},
                   [&](const PersistenceResult&) { ++completions; }) == QueueDecision::Queued);
        EXPECT(service.save("record.json", bytes("new"), {},
                   [&](const PersistenceResult&) { ++completions; }) == QueueDecision::Coalesced);
        EXPECT(service.save("other.json", bytes("full")) == QueueDecision::QueueFull);
        EXPECT(service.pending() == 1);
        {
            std::lock_guard lock(mutex);
            releaseFirst = true;
        }
        condition.notify_all();
        service.flush();
        EXPECT(writes.size() == 2);
        EXPECT(writes.at(0).first == "block" && writes.at(0).second == "first");
        EXPECT(writes.at(1).first == "record.json" && writes.at(1).second == "new");
        EXPECT(completions == 2);

        service.stop();
        EXPECT(service.stopping());
        EXPECT(service.save("late", bytes("value")) == QueueDecision::Stopping);
        EXPECT(std::string(describe(QueueDecision::Coalesced))
            == "the persistence write replaced an older pending write");
    }
}

int runPersistenceTests()
{
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory = std::filesystem::temp_directory_path()
        / ("mwnet-persistence-" + unique);
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

    testPersistenceService();

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    return sFailures;
}
