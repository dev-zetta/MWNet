#ifndef OPENMW_GUISERVERBROWSER_HPP
#define OPENMW_GUISERVERBROWSER_HPP

#include <MyGUI_Button.h>
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
        ~GUIServerBrowser() override = default;

        void refresh();

    private:
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
        void updateStoredFingerprint();
        void doConnect(bool registerAccount);
    };
}

#endif
