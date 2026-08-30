#include "GUIServerBrowser.hpp"

#include <algorithm>
#include <mutex>
#include <numeric>
#include <sstream>

#include <MyGUI_Gui.h>
#include <MyGUI_FontManager.h>
#include <MyGUI_IFont.h>
#include <MyGUI_TextBox.h>

#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Version.hpp>
#include <components/settings/settings.hpp>

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwbase/windowmanager.hpp"
#include "apps/openmw/mwbase/statemanager.hpp"

#include "../Main.hpp"
#include "../GUIController.hpp"
#include "../Networking.hpp"
#include "MasterQuery.hpp"

using namespace mwmp;

static const char* MASTER_SERVER_ADDR = "master.tes3mp.com";
static const unsigned short MASTER_SERVER_PORT = 25561;

static std::string padRight(const std::string& s, size_t width)
{
    if (s.size() >= width) return s.substr(0, width);
    return s + std::string(width - s.size(), ' ');
}

// Measure pixel width of a UTF-8 string using a named MyGUI font.
static int measureTextPx(const std::string& text, const std::string& fontName)
{
    MyGUI::IFont* font = MyGUI::FontManager::getInstance().getByName(fontName);
    if (!font) return static_cast<int>(text.size() * 8); // fallback
    float w = 0.f;
    for (unsigned char c : text)
    {
        const MyGUI::GlyphInfo* g = font->getGlyphInfo(static_cast<MyGUI::Char>(c));
        if (g) w += g->advance;
    }
    return static_cast<int>(w);
}

GUIServerBrowser::GUIServerBrowser()
    : WindowModal("tes3mp_server_browser.layout")
{
    center();
    setVisible(false);

    getWidget(mServerList,       "ServerList");
    getWidget(mColHeaderPlayers,  "ColHeaderPlayers");
    getWidget(mColHeaderPing,     "ColHeaderPing");
    getWidget(mColHeaderGamemode, "ColHeaderGamemode");
    getWidget(mServerName,        "ServerName");
    getWidget(mServerPlayers,     "ServerPlayers");
    getWidget(mServerGamemode,    "ServerGamemode");
    getWidget(mServerPing,        "ServerPing");
    getWidget(mPlayerList,        "PlayerList");
    getWidget(mEditAddress,       "EditAddress");
    getWidget(mButtonRefresh,     "ButtonRefresh");
    getWidget(mButtonConnect,     "ButtonConnect");
    getWidget(mButtonCancel,      "ButtonCancel");
    getWidget(mStatusLabel,       "StatusLabel");

    mServerList->eventListSelectAccept   += MyGUI::newDelegate(this, &GUIServerBrowser::onServerSelected);
    mServerList->eventListChangePosition += MyGUI::newDelegate(this, &GUIServerBrowser::onServerSelected);

    mButtonRefresh->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIServerBrowser::onRefreshClicked);
    mButtonConnect->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIServerBrowser::onConnectClicked);
    mButtonCancel->eventMouseButtonClick  += MyGUI::newDelegate(this, &GUIServerBrowser::onCancelClicked);

    mButtonConnect->setEnabled(true);

    const std::string defaultAddress = Settings::Manager::getString("destinationAddress", "General");
    const int defaultPort = Settings::Manager::getInt("port", "General");
    mEditAddress->setCaption(defaultAddress + ":" + std::to_string(defaultPort));

    startQuery();
}

GUIServerBrowser::~GUIServerBrowser()
{
    if (mQueryThread.joinable())
        mQueryThread.join();
}

void GUIServerBrowser::refresh()
{
    if (mQueryRunning)
        return;
    mServers.clear();
    mServerList->removeAllItems();
    mPlayerList->removeAllItems();
    // Show connection error if one occurred, then clear it
    const std::string& lastErr = Main::get().getNetworking()->getLastError();
    if (!lastErr.empty())
    {
        mStatusLabel->setCaption(lastErr);
        Main::get().getNetworking()->setLastError("");
    }
    else
    {
        mStatusLabel->setCaption("Querying master server...");
    }
    mButtonConnect->setEnabled(false);
    startQuery();
}

void GUIServerBrowser::startQuery()
{
    mQueryDone    = false;
    mQueryRunning = true;

    if (mQueryThread.joinable())
        mQueryThread.join();

    mQueryThread = std::thread([this]()
    {
        MasterQuery mq;
        mq.setServer(MASTER_SERVER_ADDR, MASTER_SERVER_PORT);
        auto result = mq.query();

        // Build entries first (without ping)
        std::vector<BrowserServerEntry> tmp;
        std::vector<std::pair<std::string, unsigned short>> pingTargets;
        for (auto& kv : result)
        {
            BrowserServerEntry entry;
            entry.addrRak   = kv.first;
            entry.queryData = kv.second;
            std::string host = kv.first.ToString(false);
            unsigned short port = kv.first.GetPort();
            entry.addr = host + ":" + std::to_string(port);
            entry.ping = -1;
            pingTargets.push_back({host, port});
            tmp.push_back(std::move(entry));
        }

        // Ping all servers in parallel (single 2s wait for all)
        auto pings = MasterQuery::pingServers(pingTargets);
        for (auto& entry : tmp)
        {
            auto it = pings.find(entry.addr);
            if (it != pings.end())
                entry.ping = it->second;
        }

        {
            std::lock_guard<std::mutex> lock(mServersMutex);
            mPendingServers = std::move(tmp);
        }
        mQueryDone    = true;
        mQueryRunning = false;
    });
}

void GUIServerBrowser::populateList()
{
    // Preserve selection across refreshes
    size_t prevSelected = mServerList->getIndexSelected();
    size_t* prevData = (prevSelected != MyGUI::ITEM_NONE)
        ? mServerList->getItemDataAt<size_t>(prevSelected, false) : nullptr;
    size_t prevRealIndex = prevData ? *prevData : MyGUI::ITEM_NONE;

    // Sort: most players first; equal players sorted by ping asc; unqueried (-1) go last
    std::vector<size_t> order(mServers.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [this](size_t a, size_t b) {
        int pa = mServers[a].ping, pb = mServers[b].ping;
        bool aQueried = pa >= 0, bQueried = pb >= 0;
        if (!aQueried && !bQueried)
            return std::string(mServers[a].queryData.GetName()) < std::string(mServers[b].queryData.GetName());
        if (!aQueried) return false;
        if (!bQueried) return true;
        int playersA = mServers[a].queryData.GetPlayers();
        int playersB = mServers[b].queryData.GetPlayers();
        if (playersA != playersB) return playersA > playersB; // most players first
        return pa < pb; // equal players: lower ping first
    });

    mServerList->removeAllItems();

    for (size_t i : order)
    {
        const BrowserServerEntry& sd = mServers[i];

        std::string name = sd.queryData.GetName();
        if (name.empty()) name = "(unnamed)";
        std::string players = std::to_string(sd.queryData.GetPlayers())
                            + "/" + std::to_string(sd.queryData.GetMaxPlayers());
        std::string ping  = (sd.ping >= 0) ? std::to_string(sd.ping) + "ms" : "---";
        std::string gmode = sd.queryData.GetGameMode();

        // Truncate name to name column width; pad to align Players/Ping/Gamemode
        if (name.size() > 24) name = name.substr(0, 21) + "...";
        std::string label = padRight(name, 24) + padRight(players, 8) + padRight(ping, 8) + gmode;

        mServerList->addItem(label, i);
    }

    // Restore selection if same server still in list
    if (prevRealIndex != MyGUI::ITEM_NONE)
    {
        for (size_t r = 0; r < mServerList->getItemCount(); ++r)
        {
            size_t* d = mServerList->getItemDataAt<size_t>(r, false);
            if (d && *d == prevRealIndex)
            {
                mServerList->setIndexSelected(r);
                break;
            }
        }
    }

    mStatusLabel->setCaption(std::to_string(mServers.size()) + " server(s) found.");

    // Dynamically align column headers/details to actual column pixel positions.
    // The list content starts at listLeft + 3px internal padding.
    // We measure a representative padded name string to find the players column x.
    const std::string fontName = "MonoFont";
    const int listContentX = mServerList->getAbsoluteLeft() + 5; // Client x=3 + BasisSkin offset x=2

    std::string sampleName = padRight("", 24); // 24 spaces = name column width
    std::string samplePlayers = padRight("", 8); // 8 spaces = players column width
    std::string samplePing    = padRight("", 8); // 8 spaces = ping column width

    int nameColPx    = measureTextPx(sampleName,    fontName);
    int playersColPx = measureTextPx(samplePlayers, fontName);
    int pingColPx    = measureTextPx(samplePing,    fontName);

    // Window-relative x for each column (subtract window left)
    int winLeft = mMainWidget->getAbsoluteLeft();
    int xPlayers  = listContentX - winLeft + nameColPx;
    int xPing     = xPlayers + playersColPx;
    int xGamemode = xPing    + pingColPx;

    const int listRight = mServerList->getAbsoluteLeft() + mServerList->getWidth() - winLeft;

    auto repos = [](MyGUI::TextBox* w, int x, int width) {
        MyGUI::IntCoord c = w->getCoord();
        w->setCoord(x, c.top, width, c.height);
    };

    repos(mColHeaderPlayers,  xPlayers,  xPing     - xPlayers  - 2);
    repos(mColHeaderPing,     xPing,     xGamemode - xPing     - 2);
    repos(mColHeaderGamemode, xGamemode, listRight - xGamemode);
    repos(mServerPlayers,     xPlayers,  xPing     - xPlayers  - 2);
    repos(mServerPing,        xPing,     xGamemode - xPing     - 2);
    repos(mServerGamemode,    xGamemode, listRight - xGamemode);
    // Widen ServerName to reach xPlayers
    {
        MyGUI::IntCoord c = mServerName->getCoord();
        mServerName->setCoord(c.left, c.top, xPlayers - c.left - 2, c.height);
    }
}

void GUIServerBrowser::updateDetails(size_t index)
{
    if (index >= mServers.size())
        return;

    const BrowserServerEntry& sd = mServers[index];

    mServerName->setCaption(sd.queryData.GetName());

    std::string players = std::to_string(sd.queryData.GetPlayers())
                        + " / " + std::to_string(sd.queryData.GetMaxPlayers());
    mServerPlayers->setCaption(players);
    mServerGamemode->setCaption(sd.queryData.GetGameMode());

    std::string pingStr = (sd.ping >= 0) ? std::to_string(sd.ping) + " ms" : "---";
    mServerPing->setCaption(pingStr);

    mPlayerList->removeAllItems();
    for (const auto& player : sd.queryData.players)
        mPlayerList->addItem(player);

    mEditAddress->setCaption(sd.addr);
    mButtonConnect->setEnabled(true);
}

void GUIServerBrowser::onServerSelected(MyGUI::ListBox* /*sender*/, size_t index)
{
    if (index == MyGUI::ITEM_NONE)
    {
        mButtonConnect->setEnabled(false);
        return;
    }
    // Item data (col 0) holds the original mServers index
    size_t* dataPtr = mServerList->getItemDataAt<size_t>(index, false);
    size_t realIndex = dataPtr ? *dataPtr : index;
    updateDetails(realIndex);
}

void GUIServerBrowser::onRefreshClicked(MyGUI::Widget* /*sender*/)
{
    refresh();
}

void GUIServerBrowser::onConnectClicked(MyGUI::Widget* /*sender*/)
{
    std::string addr = mEditAddress->getCaption().asUTF8();
    if (addr.empty())
        return;
    doConnect(addr);
}

void GUIServerBrowser::onCancelClicked(MyGUI::Widget* /*sender*/)
{
    setVisible(false);
    MWBase::Environment::get().getStateManager()->requestQuit();
}

void GUIServerBrowser::doConnect(const std::string& addr)
{
    size_t colon = addr.rfind(':');
    if (colon == std::string::npos)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "GUIServerBrowser::doConnect: invalid address '%s'", addr.c_str());
        return;
    }

    std::string host = addr.substr(0, colon);
    unsigned short port = static_cast<unsigned short>(
        std::stoi(addr.substr(colon + 1)));

    if (Main::connectTo(host, port))
    {
        // sNewGamePending is now set; engine.cpp render loop will call newGame()
        setVisible(false);
    }
    else
    {
        mStatusLabel->setCaption("Connection failed. Check address and try again.");
    }
}

void GUIServerBrowser::startPingRefresh()
{
    if (mQueryRunning)
        return;

    std::vector<std::pair<std::string, unsigned short>> targets;
    for (auto& s : mServers)
    {
        size_t colon = s.addr.rfind(':');
        if (colon == std::string::npos) continue;
        std::string host = s.addr.substr(0, colon);
        unsigned short port = (unsigned short)std::stoi(s.addr.substr(colon + 1));
        targets.push_back({host, port});
    }
    if (targets.empty()) return;

    mQueryRunning = true;
    std::thread([this, targets]() {
        auto pings = MasterQuery::pingServers(targets);
        {
            std::lock_guard<std::mutex> lock(mServersMutex);
            mPendingPings = std::move(pings);
        }
        mPingDone = true;
        mQueryRunning = false;
    }).detach();
}

void GUIServerBrowser::onFrame(float dt)
{
    if (mQueryDone)
    {
        mQueryDone = false;
        mPingTimer = 0.f;
        {
            std::lock_guard<std::mutex> lock(mServersMutex);
            mServers = std::move(mPendingServers);
            mPendingServers.clear();
        }
        populateList();
    }

    if (mPingDone)
    {
        mPingDone = false;
        {
            std::lock_guard<std::mutex> lock(mServersMutex);
            for (auto& s : mServers)
            {
                auto it = mPendingPings.find(s.addr);
                if (it != mPendingPings.end())
                    s.ping = it->second;
            }
            mPendingPings.clear();
        }
        populateList();
    }

    if (!mServers.empty() && !mQueryRunning)
    {
        mPingTimer += dt;
        if (mPingTimer >= 5.f)
        {
            mPingTimer = 0.f;
            startPingRefresh();
        }
    }
}
