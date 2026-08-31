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
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>

static std::string tempFilename;
static std::chrono::high_resolution_clock::time_point startupTime = std::chrono::high_resolution_clock::now();

namespace
{
    void warnRemovedDiscoveryCall(std::string_view function)
    {
        static std::mutex mutex;
        static std::unordered_set<std::string> warned;
        std::scoped_lock lock(mutex);
        if (!warned.emplace(function).second)
            return;
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
            "[Script]: %.*s is deprecated and has no effect because public server discovery was removed",
            static_cast<int>(function.size()), function.data());
    }

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

void ServerFunctions::LogMessage(unsigned short level, const char *message)
{
    LOG_MESSAGE_SIMPLE(level, "[Script]: %s", message);
}

void ServerFunctions::LogAppend(unsigned short level, const char *message)
{
    LOG_APPEND(level, "[Script]: %s", message);
}

void ServerFunctions::StopServer(int code)
{
    mwmp::Networking::getPtr()->stopServer(code);
}

void ServerFunctions::Kick(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player,);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Kicking player %s (%i)", player->npc.mName.c_str(), player->getId());
    mwmp::Networking::getPtr()->kickPlayer(player->guid);
    player->setLoadState(Player::KICKED);
}

void ServerFunctions::BanAddress(const char *ipAddress)
{
    mwmp::Networking::getPtr()->banAddress(ipAddress);
}

void ServerFunctions::UnbanAddress(const char *ipAddress)
{
    mwmp::Networking::getPtr()->unbanAddress(ipAddress);
}

bool ServerFunctions::DoesFilePathExist(const char *filePath)
{
    return boost::filesystem::exists(filePath);
}

const char *ServerFunctions::GetCaseInsensitiveFilename(const char *folderPath, const char *filename)
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

const char* ServerFunctions::GetDataPath()
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

unsigned int ServerFunctions::GetMillisecondsSinceServerStart()
{
    std::chrono::high_resolution_clock::time_point currentTime = std::chrono::high_resolution_clock::now();
    std::chrono::milliseconds milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startupTime);
    return milliseconds.count();
}

const char *ServerFunctions::GetOperatingSystemType()
{
    static const std::string operatingSystemType = Utils::getOperatingSystemType();
    return operatingSystemType.c_str();
}

const char *ServerFunctions::GetArchitectureType()
{
    static const std::string architectureType = Utils::getArchitectureType();
    return architectureType.c_str();
}

const char *ServerFunctions::GetServerVersion()
{
    return TES3MP_VERSION;
}

const char *ServerFunctions::GetProtocolVersion()
{
    static std::string version = std::to_string(TES3MP_PROTO_VERSION);
    return version.c_str();
}

int ServerFunctions::GetAvgPing(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, -1);
    return mwmp::Networking::get().getAvgPing(player->guid);
}

const char *ServerFunctions::GetIP(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, "");
    static thread_local std::string address;
    address = mwmp::Networking::getPtr()->getPeerAddress(player->guid);
    return address.c_str();
}

unsigned short ServerFunctions::GetPort()
{
    return mwmp::Networking::get().getPort();
}

unsigned int ServerFunctions::GetMaxPlayers()
{
    return mwmp::Networking::get().maxConnections();
}

bool ServerFunctions::HasPassword()
{
    return mwmp::Networking::get().isPassworded();
}

bool ServerFunctions::GetDataFileEnforcementState()
{
    return mwmp::Networking::getPtr()->getDataFileEnforcementState();
}

bool ServerFunctions::GetScriptErrorIgnoringState()
{
    return mwmp::Networking::getPtr()->getScriptErrorIgnoringState();
}

void ServerFunctions::SetGameMode(const char *gameMode)
{
    (void)gameMode;
    warnRemovedDiscoveryCall("SetGameMode");
}

void ServerFunctions::SetHostname(const char *name)
{
    (void)name;
    warnRemovedDiscoveryCall("SetHostname");
}

void ServerFunctions::SetServerPassword(const char *password)
{
    std::string error;
    if (!mwmp::Networking::getPtr()->setServerPassword(password == nullptr ? "" : password, error))
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "[Script]: Failed to update the server access password: %s", error.c_str());
}

void ServerFunctions::SetDataFileEnforcementState(bool state)
{
    mwmp::Networking::getPtr()->setDataFileEnforcementState(state);
}

void ServerFunctions::SetScriptErrorIgnoringState(bool state)
{
    mwmp::Networking::getPtr()->setScriptErrorIgnoringState(state);
}

void ServerFunctions::SetRuleString(const char *key, const char *value)
{
    (void)key;
    (void)value;
    warnRemovedDiscoveryCall("SetRuleString");
}

void ServerFunctions::SetRuleValue(const char *key, double value)
{
    (void)key;
    (void)value;
    warnRemovedDiscoveryCall("SetRuleValue");
}

void ServerFunctions::AddDataFileRequirement(const char *dataFilename, const char *checksumString)
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

bool ServerFunctions::DoesFileExist(const char *filePath)
{
    return DoesFilePathExist(filePath);
}

const char* ServerFunctions::GetModDir()
{
    return GetDataPath();
}

bool ServerFunctions::GetPluginEnforcementState()
{
    return mwmp::Networking::getPtr()->getDataFileEnforcementState();
}

void ServerFunctions::SetPluginEnforcementState(bool state)
{
    SetDataFileEnforcementState(state);
}

void ServerFunctions::AddPluginHash(const char *pluginName, const char *checksumString)
{
    AddDataFileRequirement(pluginName, checksumString);
}
