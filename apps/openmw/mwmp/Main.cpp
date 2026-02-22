#include <cstdlib>

#include <components/openmw-mp/Utils.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Version.hpp>

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
std::string Main::serverPassword = TES3MP_DEFAULT_PASSW;
std::string Main::resourceDir = "";

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
            ("password", bpo::value<std::string>()->default_value(TES3MP_DEFAULT_PASSW),
                        "сonnect to a secured server. (e.g. --password=AnyPassword");
}

void Main::configure(const boost::program_options::variables_map &variables)
{
    Main::address = variables["connect"].as<std::string>();
    Main::serverPassword = variables["password"].as<std::string>();
    resourceDir = variables["resources"].as<Files::MaybeQuotedPath>().string();
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
    auto safeGetStr = [](std::string_view key, std::string_view cat, std::string def) {
        try { return Settings::Manager::getString(key, cat); } catch (...) { return def; }
    };

    int logLevel = safeGetInt("logLevel", "General", 5);
    TimedLog::SetLevel(logLevel);
    if (address.empty())
    {
        pMain->server = safeGetStr("destinationAddress", "General", "mp.tes3mp.com");
        pMain->port = (unsigned short) safeGetInt("port", "General", 25565);

        serverPassword = safeGetStr("password", "General", "");
        if (serverPassword.empty())
            serverPassword = TES3MP_DEFAULT_PASSW;
    }
    else
    {
        size_t delimPos = address.find(':');
        pMain->server = address.substr(0, delimPos);
        pMain->port = atoi(address.substr(delimPos + 1).c_str());
    }
    get().mLocalSystem->serverPassword = serverPassword;

    pMain->mNetworking->connect(pMain->server, pMain->port, content, collections);

    return pMain->mNetworking->isConnected();
}

void Main::postInit()
{
    pMain->mGUIController->setupChat();
}

bool Main::isInitialized()
{
    return pMain != nullptr;
}

void Main::destroy()
{
    assert(pMain);

    delete pMain;
    pMain = 0;
}

void Main::frame(float dt)
{
    /*
        Start of tes3mp addition

        On the first frame after the game is running, perform deferred post-init
        steps that require the world and render loop to be fully started.
    */
    static bool postInitDone = false;
    if (!postInitDone && MWBase::Environment::get().getStateManager()->getState() == MWBase::StateManager::State_Running)
    {
        postInitDone = true;
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

    static bool init = true;
    if (init)
    {
        init = false;
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_BASEINFO to server");

        mNetworking->getPlayerPacket(ID_PLAYER_BASEINFO)->setPlayer(getLocalPlayer());
        mNetworking->getPlayerPacket(ID_LOADED)->setPlayer(getLocalPlayer());
        mNetworking->getPlayerPacket(ID_PLAYER_BASEINFO)->Send();
        mNetworking->getPlayerPacket(ID_LOADED)->Send();
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
