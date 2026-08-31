#include "Server.hpp"

#include <components/misc/stringops.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Version.hpp>
#include <components/openmw-mp/Persistence/AtomicFile.hpp>

#include <apps/openmw-mp/Script/ScriptFunctions.hpp>
#include <apps/openmw-mp/Networking.hpp>
#include <Script/Script.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

static std::string tempFilename;
static std::chrono::high_resolution_clock::time_point startupTime = std::chrono::high_resolution_clock::now();

namespace
{
    std::optional<std::filesystem::path> persistencePath(const char* relativePath)
    {
        if (relativePath == nullptr || relativePath[0] == '\0')
            return std::nullopt;
        const std::filesystem::path relative(relativePath);
        if (relative.is_absolute() || relative.has_root_path())
            return std::nullopt;
        const std::filesystem::path normalized = relative.lexically_normal();
        for (const auto& part : normalized)
        {
            if (part == "..")
                return std::nullopt;
        }
        return std::filesystem::path(Script::GetModDir()) / normalized;
    }
}

void ServerFunctions::LogMessage(unsigned short level, const char *message) noexcept
{
    LOG_MESSAGE_SIMPLE(level, "[Script]: %s", message);
}

void ServerFunctions::LogAppend(unsigned short level, const char *message) noexcept
{
    LOG_APPEND(level, "[Script]: %s", message);
}

void ServerFunctions::StopServer(int code) noexcept
{
    mwmp::Networking::getPtr()->stopServer(code);
}

void ServerFunctions::Kick(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player,);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Kicking player %s (%i)", player->npc.mName.c_str(), player->getId());
    mwmp::Networking::getPtr()->kickPlayer(player->guid);
    player->setLoadState(Player::KICKED);
}

void ServerFunctions::BanAddress(const char *ipAddress) noexcept
{
    mwmp::Networking::getPtr()->banAddress(ipAddress);
}

void ServerFunctions::UnbanAddress(const char *ipAddress) noexcept
{
    mwmp::Networking::getPtr()->unbanAddress(ipAddress);
}

bool ServerFunctions::DoesFilePathExist(const char *filePath) noexcept
{
    return boost::filesystem::exists(filePath);
}

const char *ServerFunctions::GetCaseInsensitiveFilename(const char *folderPath, const char *filename) noexcept
{
    if (!boost::filesystem::exists(folderPath)) return "invalid";

    boost::filesystem::directory_iterator end_itr; // default construction yields past-the-end

    for (boost::filesystem::directory_iterator itr(folderPath); itr != end_itr; ++itr)
    {
        if (Misc::StringUtils::ciEqual(itr->path().filename().string(), filename))
        {
            tempFilename = itr->path().filename().string();
            return tempFilename.c_str();
        }
    }
    return "invalid";
}

const char* ServerFunctions::GetDataPath() noexcept
{
    return Script::GetModDir();
}

bool ServerFunctions::WriteFileAtomically(const char* relativePath, const char* contents)
{
    const auto path = persistencePath(relativePath);
    if (!path || contents == nullptr)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "%s",
            "Rejected invalid atomic persistence path or content");
        return false;
    }
    const std::string_view value(contents);
    mwmp::persistence::AtomicWriteOptions options;
    options.backup = mwmp::persistence::BackupPolicy::MaintainOne;
    options.maximumBytes = 64U * 1024U * 1024U;
    std::string error;
    mwmp::Networking::getPtr()->flushPersistence();
    const bool written = mwmp::persistence::writeFileAtomically(
        *path, std::as_bytes(std::span(value)), options, error);
    if (!written)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Atomic persistence failed for %s: %s", relativePath, error.c_str());
    }
    return written;
}

bool ServerFunctions::QueueFileWrite(const char* relativePath, const char* contents)
{
    const auto path = persistencePath(relativePath);
    if (!path || contents == nullptr)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "%s",
            "Rejected invalid queued persistence path or content");
        return false;
    }
    const mwmp::persistence::QueueDecision decision
        = mwmp::Networking::getPtr()->queuePersistenceWrite(*path, contents);
    if (decision != mwmp::persistence::QueueDecision::Queued
        && decision != mwmp::persistence::QueueDecision::Coalesced)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Queued persistence rejected for %s: %s", relativePath,
            mwmp::persistence::describe(decision));
        return false;
    }
    return true;
}

void ServerFunctions::FlushPersistence()
{
    mwmp::Networking::getPtr()->flushPersistence();
}

unsigned int ServerFunctions::GetMillisecondsSinceServerStart() noexcept
{
    std::chrono::high_resolution_clock::time_point currentTime = std::chrono::high_resolution_clock::now();
    std::chrono::milliseconds milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startupTime);
    return milliseconds.count();
}

const char *ServerFunctions::GetOperatingSystemType() noexcept
{
    static const std::string operatingSystemType = Utils::getOperatingSystemType();
    return operatingSystemType.c_str();
}

const char *ServerFunctions::GetArchitectureType() noexcept
{
    static const std::string architectureType = Utils::getArchitectureType();
    return architectureType.c_str();
}

const char *ServerFunctions::GetServerVersion() noexcept
{
    return TES3MP_VERSION;
}

const char *ServerFunctions::GetProtocolVersion() noexcept
{
    static std::string version = std::to_string(TES3MP_PROTO_VERSION);
    return version.c_str();
}

int ServerFunctions::GetAvgPing(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, -1);
    return mwmp::Networking::get().getAvgPing(player->guid);
}

const char *ServerFunctions::GetIP(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, "");
    static thread_local std::string address;
    address = mwmp::Networking::getPtr()->getPeerAddress(player->guid);
    return address.c_str();
}

unsigned short ServerFunctions::GetPort() noexcept
{
    return mwmp::Networking::get().getPort();
}

unsigned int ServerFunctions::GetMaxPlayers() noexcept
{
    return mwmp::Networking::get().maxConnections();
}

bool ServerFunctions::HasPassword() noexcept
{
    return mwmp::Networking::get().isPassworded();
}

bool ServerFunctions::GetDataFileEnforcementState() noexcept
{
    return mwmp::Networking::getPtr()->getDataFileEnforcementState();
}

bool ServerFunctions::GetScriptErrorIgnoringState() noexcept
{
    return mwmp::Networking::getPtr()->getScriptErrorIgnoringState();
}

void ServerFunctions::SetGameMode(const char *gameMode) noexcept
{
    (void)gameMode;
}

void ServerFunctions::SetHostname(const char *name) noexcept
{
    (void)name;
}

void ServerFunctions::SetServerPassword(const char *password) noexcept
{
    std::string error;
    if (!mwmp::Networking::getPtr()->setServerPassword(password == nullptr ? "" : password, error))
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "[Script]: Failed to update the server access password: %s", error.c_str());
}

void ServerFunctions::SetDataFileEnforcementState(bool state) noexcept
{
    mwmp::Networking::getPtr()->setDataFileEnforcementState(state);
}

void ServerFunctions::SetScriptErrorIgnoringState(bool state) noexcept
{
    mwmp::Networking::getPtr()->setScriptErrorIgnoringState(state);
}

void ServerFunctions::SetRuleString(const char *key, const char *value) noexcept
{
    (void)key;
    (void)value;
}

void ServerFunctions::SetRuleValue(const char *key, double value) noexcept
{
    (void)key;
    (void)value;
}

void ServerFunctions::AddDataFileRequirement(const char *dataFilename, const char *checksumString) noexcept
{
    auto &samples = mwmp::Networking::getPtr()->getSamples();
    
    auto it = std::find_if(samples.begin(), samples.end(), [&dataFilename](mwmp::PacketPreInit::PluginPair &item) {
        return item.first == dataFilename;
    });

    if (it != samples.end())
    {
        // If this is a filename we've added before, ensure our new checksumString for it isn't empty
        if (strlen(checksumString) != 0)
            it->second.push_back((unsigned)std::stoul(checksumString));
    }
    else
    {
        mwmp::PacketPreInit::HashList checksumList;

        unsigned checksum = 0;

        if (strlen(checksumString) != 0)
        {
            checksum = (unsigned) std::stoul(checksumString);
            checksumList.push_back(checksum);
        }
        samples.emplace_back(dataFilename, checksumList);

        (void)checksum;
    }
}

// All methods below are deprecated versions of methods from above

bool ServerFunctions::DoesFileExist(const char *filePath) noexcept
{
    return DoesFilePathExist(filePath);
}

const char* ServerFunctions::GetModDir() noexcept
{
    return GetDataPath();
}

bool ServerFunctions::GetPluginEnforcementState() noexcept
{
    return mwmp::Networking::getPtr()->getDataFileEnforcementState();
}

void ServerFunctions::SetPluginEnforcementState(bool state) noexcept
{
    SetDataFileEnforcementState(state);
}

void ServerFunctions::AddPluginHash(const char *pluginName, const char *checksumString) noexcept
{
    AddDataFileRequirement(pluginName, checksumString);
}
