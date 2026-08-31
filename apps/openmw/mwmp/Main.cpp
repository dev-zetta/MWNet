#include <cstdlib>
#include <fstream>

#include <components/openmw-mp/Utils.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Version.hpp>
#include <components/openmw-mp/Protocol/ProtocolLimits.hpp>

#include <components/esm3/esmwriter.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/settings/parser.hpp>

#include "../mwbase/environment.hpp"

#include "../mwclass/creature.hpp"
#include "../mwclass/npc.hpp"

#include "../mwdialogue/dialoguemanagerimp.hpp"

#include "../mwgui/windowmanagerimp.hpp"

#include "../mwinput/inputmanagerimp.hpp"

#include "../mwmechanics/aitravel.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/mechanicsmanagerimp.hpp"
#include "../mwmechanics/spellcasting.hpp"

#include "../mwscript/scriptmanagerimp.hpp"

#include "../mwstate/statemanagerimp.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/customdata.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/manualref.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/worldimp.hpp"

#include "Main.hpp"
#include "Networking.hpp"
#include "LocalSystem.hpp"
#include "LocalPlayer.hpp"
#include "DedicatedPlayer.hpp"
#include "PlayerList.hpp"
#include "GUIController.hpp"
#include "CellController.hpp"
#include "MechanicsHelper.hpp"
#include "RecordHelper.hpp"

using namespace mwmp;

Main *Main::pMain = 0;
std::string Main::address = "";
std::string Main::serverPassword;
std::string Main::accountName;
std::string Main::accountPasswordFile;
std::string Main::trustedFingerprint;
bool Main::registerAccount = false;
std::string Main::resourceDir = "";
std::vector<std::string> Main::sContentFiles;
Files::Collections Main::sFileCollections;
bool Main::sNewGamePending = false;
bool Main::sPendingReturnToBrowser = false;

std::string Main::getResDir()
{
    return resourceDir;
}

std::string loadSettings(Settings::Manager& settings)
{
    Files::ConfigurationManager mCfgMgr;

    // Load defaults first so all keys exist
    const std::filesystem::path defaultPath = std::filesystem::path(Main::getResDir()) / ".." / "tes3mp-client-default.cfg";
    if (std::filesystem::exists(defaultPath))
    {
        Settings::SettingsFileParser parser;
        parser.loadSettingsFile(defaultPath, Settings::Manager::mDefaultSettings, false, true);
    }

    // Overlay user config
    const std::filesystem::path settingspath = mCfgMgr.getUserConfigPath() / "tes3mp-client.cfg";
    if (std::filesystem::exists(settingspath))
    {
        Settings::SettingsFileParser parser;
        parser.loadSettingsFile(settingspath, Settings::Manager::mUserSettings, false, true);
    }
    return settingspath.string();
}

Main::Main()
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "tes3mp started");
    mNetworking = new Networking();
    mLocalSystem = new LocalSystem();
    mLocalPlayer = new LocalPlayer();
    mGUIController = new GUIController();
    mCellController = new CellController();

    server = "mp.tes3mp.com";
    port = 25565;
    mPostInitDone = false;
    mWorldInitDone = false;
}

Main::~Main()
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "tes3mp stopped");
    delete mNetworking;
    delete mLocalSystem;
    delete mLocalPlayer;
    delete mCellController;
    delete mGUIController;
    PlayerList::cleanUp();
}

void Main::optionsDesc(boost::program_options::options_description *desc)
{
    namespace bpo = boost::program_options;
    desc->add_options()
            ("connect", bpo::value<std::string>()->default_value(""),
                        "connect to server (e.g. --connect=127.0.0.1:25565)")
            ("password", bpo::value<std::string>()->default_value(""),
                        "server access password")
            ("account", bpo::value<std::string>()->default_value(""),
                        "protocol-11 account name")
            ("account-password-file", bpo::value<std::string>()->default_value(""),
                        "read the account password from a file")
            ("register-account", bpo::bool_switch()->default_value(false),
                        "register the protocol-11 account on first connection")
            ("trust-fingerprint", bpo::value<std::string>()->default_value(""),
                        "require this server identity fingerprint");
}

void Main::configure(const boost::program_options::variables_map &variables)
{
    Main::address = variables["connect"].as<std::string>();
    Main::serverPassword = variables["password"].as<std::string>();
    Main::accountName = variables["account"].as<std::string>();
    Main::accountPasswordFile = variables["account-password-file"].as<std::string>();
    Main::registerAccount = variables["register-account"].as<bool>();
    Main::trustedFingerprint = variables["trust-fingerprint"].as<std::string>();
    resourceDir = variables["resources"].as<Files::MaybeQuotedPath>().string();
}

namespace
{
    bool readAccountPassword(const std::string& path, std::string& password, std::string& error)
    {
        error.clear();
        if (path.empty())
        {
            error = "An account password is required. Use the direct-connect screen or --account-password-file.";
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            error = "Could not open the account password file.";
            return false;
        }
        std::getline(input, password);
        if (!password.empty() && password.back() == '\r')
            password.pop_back();
        if (password.empty() || password.size() > protocol::limits::passwordBytes)
        {
            error = "The account password file must contain between 1 and 128 bytes.";
            return false;
        }
        return true;
    }

    ClientConnectionOptions commandLineConnectionOptions(const std::string& accountName,
        const std::string& accountPasswordFile, const std::string& serverPassword,
        bool registerAccount, const std::string& trustedFingerprint, std::string& error)
    {
        ClientConnectionOptions options;
        options.accountName = accountName;
        options.serverAccessPassword = serverPassword;
        options.registerAccount = registerAccount;
        if (!trustedFingerprint.empty())
            options.trustedFingerprint = trustedFingerprint;
        readAccountPassword(accountPasswordFile, options.accountPassword, error);
        return options;
    }
}

bool Main::init(std::vector<std::string> &content, Files::Collections &collections)
{
    assert(!pMain);
    pMain = new Main();

    Settings::Manager manager;
    loadSettings(manager);

    auto safeGetInt = [](std::string_view key, std::string_view cat, int def) {
        try { return Settings::Manager::getInt(key, cat); } catch (...) { return def; }
    };
    int logLevel = safeGetInt("logLevel", "General", 5);
    TimedLog::SetLevel(logLevel);
    sContentFiles    = content;
    sFileCollections = collections;

    if (address.empty())
    {
        /*
            Start of tes3mp change (major)

            No --connect CLI arg provided: skip connecting here and let
            the in-game direct-connect screen handle it via connectTo().
        */
        get().mLocalSystem->serverPassword.clear();
        return true;
        /* End of tes3mp change (major) */
    }

    size_t delimPos = address.find(':');
    pMain->server = address.substr(0, delimPos);
    pMain->port = atoi(address.substr(delimPos + 1).c_str());
    get().mLocalSystem->serverPassword.clear();
    std::string credentialError;
    auto connectionOptions = commandLineConnectionOptions(accountName,
        accountPasswordFile, serverPassword, registerAccount, trustedFingerprint,
        credentialError);
    if (!credentialError.empty())
    {
        pMain->mNetworking->setLastError(credentialError);
        return false;
    }
    pMain->mNetworking->connect(pMain->server, pMain->port, content, collections,
        std::move(connectionOptions));

    return pMain->mNetworking->isConnected();
}

bool Main::connectTo(const std::string& host, unsigned short port,
    ClientConnectionOptions options)
{
    assert(pMain);
    pMain->server = host;
    pMain->port   = port;
    get().mLocalSystem->serverPassword.clear();
    // Reset per-connection flags so post-init and world-init run again
    pMain->mPostInitDone = false;
    pMain->mWorldInitDone = false;
    // Reset chargen state so password/chargen flow runs fresh on reconnect
    pMain->mLocalPlayer->receivedCharacter = false;
    pMain->mLocalPlayer->charGenState.currentStage = 0;
    pMain->mLocalPlayer->charGenState.endStage = 1;
    pMain->mLocalPlayer->charGenState.isFinished = false;
    pMain->mNetworking->connect(host, port, sContentFiles, sFileCollections,
        std::move(options));
    bool connected = pMain->mNetworking->isConnected();
    if (connected)
        sNewGamePending = true;
    return connected;
}

bool Main::isNewGamePending()
{
    return sNewGamePending;
}

bool Main::isPostInitDone()
{
    return pMain && pMain->mPostInitDone;
}

void Main::clearNewGamePending()
{
    sNewGamePending = false;
}

bool Main::isPendingReturnToBrowser()
{
    return sPendingReturnToBrowser;
}

void Main::clearPendingReturnToBrowser()
{
    sPendingReturnToBrowser = false;
}

void Main::requestReturnToBrowser()
{
    sPendingReturnToBrowser = true;
}

void Main::postInit()
{
    pMain->mGUIController->setupChat();
}

bool Main::isInitialized()
{
    return pMain != nullptr;
}

bool Main::isConnected()
{
    return pMain != nullptr && pMain->mNetworking->isConnected();
}

const std::string &Main::getAddress()
{
    return address;
}

void Main::destroy()
{
    assert(pMain);

    delete pMain;
    pMain = 0;
}

void Main::frame(float dt)
{
    // Skip all world-dependent calls when not in-game (e.g. after cleanup() on disconnect)
    if (MWBase::Environment::get().getStateManager()->getState() == MWBase::StateManager::State_NoGame)
        return;

    /*
        Start of tes3mp addition

        On the first frame after the game is running, perform deferred post-init
        steps that require the world and render loop to be fully started.
    */
    if (!pMain->mPostInitDone && MWBase::Environment::get().getStateManager()->getState() == MWBase::StateManager::State_Running)
    {
        pMain->mPostInitDone = true;
        MWBase::Environment::get().getMechanicsManager()->toggleAI();
        RecordHelper::createPlaceholderInteriorCell();
        // Stop vanilla chargen scripts before they run - TES3MP handles chargen itself
        MWBase::Environment::get().getScriptManager()->getGlobalScripts().removeScript(
            ESM::RefId::stringRefId("CharGen"));
        // Process the first network update immediately after setup so that
        // ID_PLAYER_CELL_CHANGE packets find the placeholder cell already created.
        get().getNetworking()->update();
        PlayerList::update(dt);
        get().getCellController()->updateDedicated(dt);
        get().updateWorld(dt);
        get().getGUIController()->update(dt);
        return;
    }
    /* End of tes3mp addition */

    get().getNetworking()->update();

    PlayerList::update(dt);
    get().getCellController()->updateDedicated(dt);
    get().updateWorld(dt);

    get().getGUIController()->update(dt);
}

void Main::updateWorld(float dt) const
{

    if (!mLocalPlayer->processCharGen())
        return;

    if (!pMain->mWorldInitDone)
    {
        pMain->mWorldInitDone = true;
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_BASEINFO to server");

        mNetworking->getPlayerPacket(ID_PLAYER_BASEINFO)->setPlayer(getLocalPlayer());
        mNetworking->getPlayerPacket(ID_PLAYER_BASEINFO)->Send();
        mLocalPlayer->updateStatsDynamic(true);
        get().getGUIController()->setChatVisible(true);
    }
    else
    {
        mLocalPlayer->update();
        mCellController->updateLocal(false);
    }
}

const Main &Main::get()
{
    return *pMain;
}

Networking *Main::getNetworking() const
{
    return mNetworking;
}

LocalSystem *Main::getLocalSystem() const
{
    return mLocalSystem;
}

LocalPlayer *Main::getLocalPlayer() const
{
    return mLocalPlayer;
}

GUIController *Main::getGUIController() const
{
    return mGUIController;
}

CellController *Main::getCellController() const
{
    return mCellController;
}

bool Main::isValidPacketScript(std::string scriptId)
{
    mwmp::BaseWorldstate *worldstate = get().getNetworking()->getWorldstate();

    if (Utils::vectorContains(worldstate->synchronizedClientScriptIds, scriptId))
        return true;

    return false;
}

bool Main::isValidPacketGlobal(std::string globalId)
{
    mwmp::BaseWorldstate *worldstate = get().getNetworking()->getWorldstate();

    if (Utils::vectorContains(worldstate->synchronizedClientGlobalIds, globalId))
        return true;

    return false;
}
