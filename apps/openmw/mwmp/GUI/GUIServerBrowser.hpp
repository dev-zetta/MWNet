#ifndef OPENMW_GUISERVERBROWSER_HPP
#define OPENMW_GUISERVERBROWSER_HPP

#include <thread>
#include <atomic>
#include <map>
#include <mutex>
#include <vector>
#include <string>

#include <RakPeerInterface.h>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_ListBox.h>
#include <MyGUI_TextBox.h>

#include "apps/openmw/mwgui/windowbase.hpp"
#include <components/openmw-mp/Master/MasterData.hpp>

namespace mwmp
{
    struct BrowserServerEntry
    {
        std::string              addr;  // "host:port"
        RakNet::SystemAddress    addrRak;
        QueryData                queryData;
        int                      ping = -1;
    };

    class GUIServerBrowser : public MWGui::WindowModal
    {
    public:
        GUIServerBrowser();
        ~GUIServerBrowser() override;

        void refresh();

    private:
        MyGUI::ListBox*      mServerList;
        MyGUI::TextBox*      mColHeaderPlayers;
        MyGUI::TextBox*      mColHeaderPing;
        MyGUI::TextBox*      mColHeaderGamemode;
        MyGUI::TextBox*      mServerName;
        MyGUI::TextBox*      mServerPlayers;
        MyGUI::TextBox*      mServerGamemode;
        MyGUI::TextBox*      mServerPing;
        MyGUI::ListBox*      mPlayerList;
        MyGUI::EditBox*      mEditAddress;
        MyGUI::Button*       mButtonRefresh;
        MyGUI::Button*       mButtonConnect;
        MyGUI::Button*       mButtonCancel;
        MyGUI::TextBox*      mStatusLabel;

        std::vector<BrowserServerEntry> mServers;       // main-thread only
        std::vector<BrowserServerEntry> mPendingServers; // written by query thread
        std::mutex                       mServersMutex;
        std::thread mQueryThread;
        std::atomic<bool> mQueryDone { false };
        std::atomic<bool> mQueryRunning { false };
        std::atomic<bool> mPingDone { false };
        std::map<std::string, int> mPendingPings;
        float mPingTimer { 0.f };
        int mSelectedIndex { -1 };

        void onServerSelected(MyGUI::ListBox* sender, size_t index);
        void startPingRefresh();
        void onRefreshClicked(MyGUI::Widget* sender);
        void onConnectClicked(MyGUI::Widget* sender);
        void onCancelClicked(MyGUI::Widget* sender);

        void startQuery();
        void populateList();
        void updateDetails(size_t index);
        void doConnect(const std::string& addr);

        void onFrame(float dt) override;
    };
}

#endif // OPENMW_GUISERVERBROWSER_HPP
