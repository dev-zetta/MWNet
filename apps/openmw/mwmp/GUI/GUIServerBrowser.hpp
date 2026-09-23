#ifndef OPENMW_GUISERVERBROWSER_HPP
#define OPENMW_GUISERVERBROWSER_HPP

#include <MyGUI_Button.h>
#include <MyGUI_ListBox.h>
#include <components/openmw-mp/Discovery/Client.hpp>
#include <future>
#include "../Networking.hpp"
#include <MyGUI_ComboBox.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_TextBox.h>

#include "apps/openmw/mwgui/windowbase.hpp"

namespace mwmp
{
    class GUIServerBrowser : public MWGui::WindowModal
    {
    public:
        GUIServerBrowser();
        ~GUIServerBrowser() override;
        void onFrame(float duration) override;

        void refresh();

    private:
        MyGUI::Widget* mPublicPanel = nullptr;
        MyGUI::Widget* mDirectPanel = nullptr;
        MyGUI::ListBox* mPublicServers = nullptr;
        MyGUI::EditBox* mSearch = nullptr;
        MyGUI::Button* mButtonPublic = nullptr;
        MyGUI::Button* mButtonRefresh = nullptr;
        MyGUI::Button* mButtonMore = nullptr;
        std::vector<discovery::Server> mServers;
        std::vector<std::size_t> mFiltered;
        std::string mNextPage, mListedEndpoint, mListedFingerprint;
        std::atomic_bool mCancelDiscovery{false};
        std::atomic_bool mCancelProbe{false};
        std::future<discovery::Page> mDirectoryRequest;
        std::future<ServerProbeResult> mProbeRequest;
        std::string mProbeAddress;
        void onPublicClicked(MyGUI::Widget*);
        void onRefreshClicked(MyGUI::Widget*);
        void onMoreClicked(MyGUI::Widget*);
        void onSearchChanged(MyGUI::EditBox*);
        void onPublicSelected(MyGUI::ListBox*, std::size_t);
        void startDirectoryRequest(bool reset);
        void filterServers();
        MyGUI::ComboBox* mEditAddress = nullptr;
        MyGUI::EditBox* mEditAccount = nullptr;
        MyGUI::EditBox* mEditPassword = nullptr;
        MyGUI::EditBox* mEditServerPassword = nullptr;
        MyGUI::EditBox* mEditFingerprint = nullptr;
        MyGUI::Button* mButtonPing = nullptr;
        MyGUI::Button* mButtonSave = nullptr;
        MyGUI::Button* mButtonForget = nullptr;
        MyGUI::Button* mButtonLogin = nullptr;
        MyGUI::Button* mButtonRegister = nullptr;
        MyGUI::Button* mButtonCancel = nullptr;
        MyGUI::TextBox* mFingerprintLabel = nullptr;
        MyGUI::TextBox* mStatusLabel = nullptr;

        void onAddressSelected(MyGUI::ComboBox* sender, std::size_t position);
        void onPingClicked(MyGUI::Widget* sender);
        void onSaveClicked(MyGUI::Widget* sender);
        void onForgetClicked(MyGUI::Widget* sender);
        void onLoginClicked(MyGUI::Widget* sender);
        void onRegisterClicked(MyGUI::Widget* sender);
        void onCancelClicked(MyGUI::Widget* sender);
        void reloadServerChoices();
        void updateStoredFingerprint(bool preserveInput = false);
        void doConnect(bool registerAccount);
    };
}

#endif
