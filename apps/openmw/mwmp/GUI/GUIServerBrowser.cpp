#include "GUIServerBrowser.hpp"

#include <charconv>

#include <components/openmw-mp/Protocol/ProtocolLimits.hpp>
#include <components/settings/settings.hpp>

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwbase/statemanager.hpp"

#include "../Main.hpp"
#include "../Networking.hpp"

using namespace mwmp;

GUIServerBrowser::GUIServerBrowser()
    : WindowModal("tes3mp_server_browser.layout")
{
    center();
    setVisible(false);

    getWidget(mEditAddress, "EditAddress");
    getWidget(mEditAccount, "EditAccount");
    getWidget(mEditPassword, "EditPassword");
    getWidget(mEditServerPassword, "EditServerPassword");
    getWidget(mEditFingerprint, "EditFingerprint");
    getWidget(mButtonLogin, "ButtonLogin");
    getWidget(mButtonRegister, "ButtonRegister");
    getWidget(mButtonCancel, "ButtonCancel");
    getWidget(mStatusLabel, "StatusLabel");

    mEditPassword->setEditPassword(true);
    mEditServerPassword->setEditPassword(true);
    mButtonLogin->eventMouseButtonClick
        += MyGUI::newDelegate(this, &GUIServerBrowser::onLoginClicked);
    mButtonRegister->eventMouseButtonClick
        += MyGUI::newDelegate(this, &GUIServerBrowser::onRegisterClicked);
    mButtonCancel->eventMouseButtonClick
        += MyGUI::newDelegate(this, &GUIServerBrowser::onCancelClicked);

    const std::string defaultAddress
        = Settings::Manager::getString("destinationAddress", "General");
    const int defaultPort = Settings::Manager::getInt("port", "General");
    mEditAddress->setCaption(defaultAddress + ":" + std::to_string(defaultPort));
    mEditAccount->setCaption(Settings::Manager::getString("accountName", "General"));
}

void GUIServerBrowser::refresh()
{
    const std::string& error = Main::get().getNetworking()->getLastError();
    if (error.empty())
        mStatusLabel->setCaption(
            "Enter a server and account. First use will ask you to confirm its fingerprint.");
    else
    {
        mStatusLabel->setCaption(error);
        Main::get().getNetworking()->setLastError("");
    }
}

void GUIServerBrowser::onLoginClicked(MyGUI::Widget*)
{
    doConnect(false);
}

void GUIServerBrowser::onRegisterClicked(MyGUI::Widget*)
{
    doConnect(true);
}

void GUIServerBrowser::onCancelClicked(MyGUI::Widget*)
{
    setVisible(false);
    MWBase::Environment::get().getStateManager()->requestQuit();
}

void GUIServerBrowser::doConnect(bool registerAccount)
{
    const std::string address = mEditAddress->getCaption().asUTF8();
    const std::size_t separator = address.rfind(':');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= address.size())
    {
        mStatusLabel->setCaption("Use an address in the form host:port.");
        return;
    }

    unsigned int parsedPort = 0;
    const char* portBegin = address.data() + separator + 1;
    const char* portEnd = address.data() + address.size();
    const auto parsed = std::from_chars(portBegin, portEnd, parsedPort);
    if (parsed.ec != std::errc{} || parsed.ptr != portEnd || parsedPort == 0
        || parsedPort > 65535)
    {
        mStatusLabel->setCaption("The server port must be between 1 and 65535.");
        return;
    }

    ClientConnectionOptions options;
    options.accountName = mEditAccount->getCaption().asUTF8();
    options.accountPassword = mEditPassword->getCaption().asUTF8();
    options.serverAccessPassword = mEditServerPassword->getCaption().asUTF8();
    options.registerAccount = registerAccount;
    const std::string fingerprint = mEditFingerprint->getCaption().asUTF8();
    if (!fingerprint.empty())
        options.trustedFingerprint = fingerprint;

    if (options.accountName.empty()
        || options.accountName.size() > protocol::limits::accountNameBytes)
    {
        mStatusLabel->setCaption("The account name must contain between 1 and 64 bytes.");
        return;
    }
    if (options.accountPassword.empty()
        || options.accountPassword.size() > protocol::limits::passwordBytes)
    {
        mStatusLabel->setCaption("The account password must contain between 1 and 128 bytes.");
        return;
    }

    mStatusLabel->setCaption("Establishing encrypted session...");
    const bool connected = Main::connectTo(address.substr(0, separator),
        static_cast<unsigned short>(parsedPort), std::move(options));
    mEditPassword->setCaption("");
    mEditServerPassword->setCaption("");
    if (connected)
        setVisible(false);
    else
        refresh();
}
