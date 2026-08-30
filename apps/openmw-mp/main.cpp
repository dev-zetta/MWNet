#include <iostream>
#include <limits>

#include <boost/filesystem/fstream.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream_buffer.hpp>

#include <components/files/configurationmanager.hpp>
#include <components/files/escape.hpp>
#include <components/settings/parser.hpp>
#include <components/settings/settings.hpp>
#include <components/version/version.hpp>

#include <components/openmw-mp/ErrorMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Protocol/EndpointSecurity.hpp>
#include <components/openmw-mp/Security/PasswordHash.hpp>
#include <components/openmw-mp/Transport/Protocol11Endpoint.hpp>
#include <components/openmw-mp/Utils.hpp>
#include <components/openmw-mp/Version.hpp>

#include <BitStream.h>
#include <MessageIdentifiers.h>
#include <RakPeer.h>
#include <RakPeerInterface.h>
#include <sodium.h>

#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#include "Player.hpp"
#include "Networking.hpp"
#include "Utils.hpp"

#include <apps/openmw-mp/Script/Script.hpp>

#ifdef ENABLE_BREAKPAD
#include <handler/exception_handler.h>
#endif

using namespace mwmp;

#ifdef ENABLE_BREAKPAD
google_breakpad::ExceptionHandler *pHandler = 0;
#if defined(_WIN32)
bool DumpCallback(const wchar_t* _dump_dir,const wchar_t* _minidump_id,void* context,EXCEPTION_POINTERS* exinfo,MDRawAssertionInfo* assertion,bool success)
#elif defined(__linux)
bool DumpCallback(const google_breakpad::MinidumpDescriptor &md, void *context, bool success)
#endif
{
    // NO STACK USE, NO HEAP USE THERE !!!
    return success;
}

void breakpad(std::string pathToDump)
{
#ifdef _WIN32
    pHandler = new google_breakpad::ExceptionHandler(
            L"crashdumps\\",
            /*FilterCallback*/ 0,
            DumpCallback,
            0,
            google_breakpad::ExceptionHandler::HANDLER_ALL);
#else
    google_breakpad::MinidumpDescriptor md(pathToDump);
    pHandler = new google_breakpad::ExceptionHandler(
            md,
            /*FilterCallback*/ 0,
            DumpCallback,
            /*context*/ 0,
            true,
            -1
    );
#endif
}

void breakpad_close()
{
    delete pHandler;
}
#else
void breakpad(std::string pathToDump){}
void breakpad_close(){}
#endif

std::filesystem::path loadSettings(const Files::ConfigurationManager& cfgMgr)
{
    const std::filesystem::path localDefault = cfgMgr.getLocalPath() / "tes3mp-server-default.cfg";
    const std::filesystem::path globalDefault = cfgMgr.getGlobalPath() / "tes3mp-server-default.cfg";

    const std::filesystem::path* defaultSettings = nullptr;
    if (std::filesystem::exists(localDefault))
        defaultSettings = &localDefault;
    else if (std::filesystem::exists(globalDefault))
        defaultSettings = &globalDefault;
    else
        throw std::runtime_error(
            "No default settings file found! Make sure the file \"tes3mp-server-default.cfg\" was properly installed.");

    Settings::SettingsFileParser parser;
    parser.loadSettingsFile(*defaultSettings, Settings::Manager::mDefaultSettings);

    const std::filesystem::path userSettings = cfgMgr.getUserConfigPath() / "tes3mp-server.cfg";
    if (std::filesystem::exists(userSettings))
        parser.loadSettingsFile(userSettings, Settings::Manager::mUserSettings);

    return userSettings;
}

class Tee : public boost::iostreams::sink
{
public:
    Tee(std::ostream &stream, std::ostream &stream2)
            : out(stream), out2(stream2)
    {
    }

    std::streamsize write(const char *str, std::streamsize size)
    {
        out.write (str, size);
        out.flush();
        out2.write (str, size);
        out2.flush();
        return size;
    }

private:
    std::ostream &out;
    std::ostream &out2;
};

namespace
{
    bool readSecretLine(std::string& value)
    {
        bool echoDisabled = false;
#ifndef _WIN32
        termios original{};
        if (isatty(STDIN_FILENO) != 0 && tcgetattr(STDIN_FILENO, &original) == 0)
        {
            termios hidden = original;
            hidden.c_lflag &= static_cast<tcflag_t>(~ECHO);
            echoDisabled = tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden) == 0;
        }
#else
        HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        DWORD original = 0;
        if (input != INVALID_HANDLE_VALUE && GetConsoleMode(input, &original) != 0)
        {
            echoDisabled = SetConsoleMode(input, original & ~ENABLE_ECHO_INPUT) != 0;
        }
#endif
        const bool read = static_cast<bool>(std::getline(std::cin, value));
#ifndef _WIN32
        if (echoDisabled)
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
#else
        if (echoDisabled)
            SetConsoleMode(input, original);
#endif
        if (echoDisabled)
            std::cerr << '\n';
        return read;
    }

    int generateAccessPasswordHash()
    {
        std::cerr << "Server access password: " << std::flush;
        std::string plaintext;
        if (!readSecretLine(plaintext))
        {
            std::cerr << "Unable to read the password.\n";
            return 1;
        }
        if (plaintext.empty())
        {
            std::cerr << "The access password must not be empty.\n";
            return 1;
        }
        std::string error;
        auto password = mwmp::security::PasswordBuffer::copyFrom(plaintext, error);
        sodium_memzero(plaintext.data(), plaintext.size());
        plaintext.clear();
        if (!password)
        {
            std::cerr << "Unable to accept the password: " << error << '\n';
            return 1;
        }
        std::string encoded;
        if (!mwmp::security::PasswordHash::createArgon2id(*password, encoded, error))
        {
            std::cerr << "Unable to hash the password: " << error << '\n';
            return 1;
        }
        std::cout << encoded << '\n';
        return 0;
    }
}

boost::program_options::variables_map launchOptions(int argc, char *argv[], Files::ConfigurationManager& cfgMgr)
{
    namespace bpo = boost::program_options;
    bpo::variables_map variables;
    bpo::options_description desc;

    Files::ConfigurationManager::addCommonOptions(desc);
    desc.add_options()
            ("no-logs", bpo::value<bool>()->implicit_value(true)->default_value(false),
             "Do not write logs. Useful for daemonizing.")
            ("hash-access-password",
             bpo::value<bool>()->implicit_value(true)->default_value(false),
             "Read a server access password from stdin and print its Argon2id hash.");

    bpo::parsed_options valid_opts = bpo::command_line_parser(argc, argv).options(desc).allow_unregistered().run();

    bpo::store(valid_opts, variables);
    bpo::notify(variables);
    cfgMgr.processPaths(variables, std::filesystem::current_path());
    cfgMgr.readConfiguration(variables, desc, true);

    return variables;
}

int main(int argc, char *argv[])
{
    Settings::Manager mgr;
    Files::ConfigurationManager cfgMgr;

    breakpad(boost::filesystem::path(cfgMgr.getLogPath()).string());

    auto variables = launchOptions(argc, argv, cfgMgr);
    if (variables["hash-access-password"].as<bool>())
        return generateAccessPasswordHash();
    loadSettings(cfgMgr);

    std::string versionStr(Version::getVersion());
    std::string commitHash(Version::getCommitHash());

    int logLevel = mgr.getInt("logLevel", "General");
    if (logLevel < TimedLog::LOG_VERBOSE || logLevel > TimedLog::LOG_FATAL)
        logLevel = TimedLog::LOG_VERBOSE;

    // Some objects used to redirect cout and cerr
    // Scope must be here, so this still works inside the catch block for logging exceptions
    std::streambuf* cout_rdbuf = std::cout.rdbuf ();
    std::streambuf* cerr_rdbuf = std::cerr.rdbuf ();

    boost::iostreams::stream_buffer<Tee> coutsb;
    boost::iostreams::stream_buffer<Tee> cerrsb;

    std::ostream oldcout(cout_rdbuf);
    std::ostream oldcerr(cerr_rdbuf);

    boost::filesystem::ofstream logfile;

    if (!variables["no-logs"].as<bool>())
    {
        // Redirect cout and cerr to tes3mp server log

        logfile.open(boost::filesystem::path(
                cfgMgr.getLogPath() / "/tes3mp-server-" += TimedLog::getFilenameTimestamp() += ".log"));

        coutsb.open(Tee(logfile, oldcout));
        cerrsb.open(Tee(logfile, oldcerr));

        std::cout.rdbuf(&coutsb);
        std::cerr.rdbuf(&cerrsb);
    }

    LOG_INIT(logLevel);

    int players = mgr.getInt("maximumPlayers", "General");
    std::string address = mgr.getString("localAddress", "General");
    bool publicListen = mgr.getBool("publicListen", "General");
    int port = mgr.getInt("port", "General");

    std::string passwordHash = mgr.getString("passwordHash", "General");

    std::string pluginHome = mgr.getString("home", "Plugins");
    std::string dataDirectory = Utils::convertPath(pluginHome + "/data");

    std::vector<std::string> plugins(Utils::split(mgr.getString("plugins", "Plugins"), ','));

    std::string versionInfo = Utils::getVersionInfo("TES3MP dedicated server", TES3MP_VERSION, commitHash, TES3MP_PROTO_VERSION);
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "%s", versionInfo.c_str());
    
    Script::SetModDir(dataDirectory);

#ifdef ENABLE_LUA
    LangLua::AddPackagePath(Utils::convertPath(pluginHome + "/scripts/?.lua" + ";"
        + pluginHome + "/lib/lua/?.lua" + ";"));
#ifdef _WIN32
    LangLua::AddPackageCPath(Utils::convertPath(pluginHome + "/lib/?.dll"));
#else
    LangLua::AddPackageCPath(Utils::convertPath(pluginHome + "/lib/?.so"));
#endif

#endif

    int code;

    RakNet::RakPeerInterface *peer = RakNet::RakPeerInterface::GetInstance();

    if (!protocol::isListenAddressAllowed(address, publicListen))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "Refusing non-loopback listen address %s because General/publicListen is false.", address.c_str());
        return 1;
    }

    try
    {
        for (auto plugin : plugins)
            Script::LoadScript(plugin.c_str(), pluginHome.c_str());

        if (players <= 0 || players > std::numeric_limits<unsigned short>::max())
            throw std::runtime_error("maximumPlayers must be between 1 and 65535");
        if (port <= 0 || port > std::numeric_limits<unsigned short>::max())
            throw std::runtime_error("port must be between 1 and 65535");

        std::string transportError;
        auto endpoint = transport::Protocol11Endpoint::createServer(
            cfgMgr.getUserConfigPath() / "server-identity.key", transportError);
        if (!endpoint)
            throw std::runtime_error("Failed to load the server identity: " + transportError);

        transport::ListenOptions listenOptions;
        listenOptions.address = address;
        listenOptions.port = static_cast<unsigned short>(port);
        listenOptions.maximumConnections = static_cast<std::size_t>(players);
        listenOptions.publicListen = publicListen;
        transport::TransportError listenError;
        if (!endpoint->listen(listenOptions, listenError))
            throw std::runtime_error("Failed to listen: " + listenError.detail);

        if (const auto fingerprint = endpoint->serverFingerprint())
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "Server identity fingerprint: %s", fingerprint->c_str());

        const std::filesystem::path serverData(dataDirectory);
        Networking networking(peer, *endpoint, serverData / "account",
            serverData / "player", static_cast<unsigned int>(players),
            static_cast<unsigned short>(port));
        std::string passwordError;
        if (!networking.setServerPasswordHash(std::move(passwordHash), passwordError))
            throw std::runtime_error("Invalid General/passwordHash: " + passwordError);

        networking.postInit();

        code = networking.mainLoop();
        endpoint->shutdown(listenOptions.timeouts.shutdown);
    }
    catch (std::exception &e)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "%s", e.what());
        Script::Call<Script::CallbackIdentity("OnServerScriptCrash")>(e.what());
        throw; //fall through
    }

    RakNet::RakPeerInterface::DestroyInstance(peer);

    if (code == 0)
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Quitting peacefully.");

    LOG_QUIT();

    if (!variables["no-logs"].as<bool>())
    {
        // Restore cout and cerr
        std::cout.rdbuf(cout_rdbuf);
        std::cerr.rdbuf(cerr_rdbuf);
    }


    breakpad_close();
    return code;
}
