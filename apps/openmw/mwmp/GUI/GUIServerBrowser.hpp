#ifndef OPENMW_GUISERVERBROWSER_HPP
#define OPENMW_GUISERVERBROWSER_HPP

#include <MyGUI_Button.h>
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
        MyGUI::EditBox* mEditAddress = nullptr;
        MyGUI::EditBox* mEditAccount = nullptr;
        MyGUI::EditBox* mEditPassword = nullptr;
        MyGUI::EditBox* mEditServerPassword = nullptr;
        MyGUI::EditBox* mEditFingerprint = nullptr;
        MyGUI::Button* mButtonLogin = nullptr;
        MyGUI::Button* mButtonRegister = nullptr;
        MyGUI::Button* mButtonCancel = nullptr;
        MyGUI::TextBox* mStatusLabel = nullptr;

        void onLoginClicked(MyGUI::Widget* sender);
        void onRegisterClicked(MyGUI::Widget* sender);
        void onCancelClicked(MyGUI::Widget* sender);
        void doConnect(bool registerAccount);
    };
}

#endif
