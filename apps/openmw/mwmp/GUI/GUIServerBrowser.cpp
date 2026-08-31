#include "GUIServerBrowser.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <components/openmw-mp/Protocol/ProtocolLimits.hpp>
#include <components/openmw-mp/Security/TrustStore.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/settings/settings.hpp>

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwbase/statemanager.hpp"

#include "../Main.hpp"
#include "../Networking.hpp"

using namespace mwmp;

namespace
{
    constexpr std::size_t sMaximumSavedServers = 32;
    constexpr std::size_t sMaximumRecentServers = 10;

    struct ParsedEndpoint
    {
        std::string host;
        std::string canonical;
        std::uint16_t port = 0;
    };

    std::string_view trim(std::string_view value)
    {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
            value.remove_prefix(1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
            value.remove_suffix(1);
        return value;
    }

    std::optional<ParsedEndpoint> parseEndpoint(std::string_view value)
    {
        value = trim(value);
        if (value.empty())
            return std::nullopt;

        std::string_view host;
        std::string_view portText;
        if (value.front() == '[')
        {
            const std::size_t bracket = value.find(']');
            if (bracket == std::string_view::npos || bracket <= 1 || bracket + 2 >= value.size()
                || value[bracket + 1] != ':')
                return std::nullopt;
            host = value.substr(1, bracket - 1);
            portText = value.substr(bracket + 2);
        }
        else
        {
            const std::size_t separator = value.rfind(':');
            if (separator == std::string_view::npos || separator == 0 || separator + 1 >= value.size()
                || value.find(':') != separator)
                return std::nullopt;
            host = value.substr(0, separator);
            portText = value.substr(separator + 1);
        }

        unsigned int port = 0;
        const auto parsed = std::from_chars(portText.data(), portText.data() + portText.size(), port);
        if (parsed.ec != std::errc{} || parsed.ptr != portText.data() + portText.size()
            || port == 0 || port > 65535)
            return std::nullopt;

        const auto canonical = security::TrustStore::canonicalEndpoint(
            host, static_cast<std::uint16_t>(port));
        if (!canonical)
            return std::nullopt;

        ParsedEndpoint result;
        result.canonical = *canonical;
        result.port = static_cast<std::uint16_t>(port);
        if (result.canonical.front() == '[')
            result.host = result.canonical.substr(1, result.canonical.rfind(']') - 1);
        else
            result.host = result.canonical.substr(0, result.canonical.rfind(':'));
        return result;
    }

    std::vector<std::string> loadEndpoints(std::string_view setting, std::size_t maximum)
    {
        std::vector<std::string> result;
        for (const std::string& entry : Settings::Manager::getOrDefault<std::vector<std::string>>(
                 setting, "General", {}))
        {
            const auto endpoint = parseEndpoint(entry);
            if (!endpoint || std::ranges::find(result, endpoint->canonical) != result.end())
                continue;
            result.push_back(endpoint->canonical);
            if (result.size() == maximum)
                break;
        }
        return result;
    }

    void promoteEndpoint(std::vector<std::string>& endpoints, const std::string& endpoint,
        std::size_t maximum)
    {
        std::erase(endpoints, endpoint);
        endpoints.insert(endpoints.begin(), endpoint);
        if (endpoints.size() > maximum)
            endpoints.resize(maximum);
    }
}

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
    getWidget(mButtonPing, "ButtonPing");
    getWidget(mButtonSave, "ButtonSave");
    getWidget(mButtonForget, "ButtonForget");
    getWidget(mButtonLogin, "ButtonLogin");
    getWidget(mButtonRegister, "ButtonRegister");
    getWidget(mButtonCancel, "ButtonCancel");
    getWidget(mFingerprintLabel, "FingerprintStatus");
    getWidget(mStatusLabel, "StatusLabel");

    mEditPassword->setEditPassword(true);
    mEditServerPassword->setEditPassword(true);
    mEditAddress->eventComboChangePosition
        += MyGUI::newDelegate(this, &GUIServerBrowser::onAddressSelected);
    mButtonPing->eventMouseButtonClick
        += MyGUI::newDelegate(this, &GUIServerBrowser::onPingClicked);
    mButtonSave->eventMouseButtonClick
        += MyGUI::newDelegate(this, &GUIServerBrowser::onSaveClicked);
    mButtonForget->eventMouseButtonClick
        += MyGUI::newDelegate(this, &GUIServerBrowser::onForgetClicked);
    mButtonLogin->eventMouseButtonClick
        += MyGUI::newDelegate(this, &GUIServerBrowser::onLoginClicked);
    mButtonRegister->eventMouseButtonClick
        += MyGUI::newDelegate(this, &GUIServerBrowser::onRegisterClicked);
    mButtonCancel->eventMouseButtonClick
        += MyGUI::newDelegate(this, &GUIServerBrowser::onCancelClicked);

    const std::string defaultAddress
        = Settings::Manager::getString("destinationAddress", "General");
    const int defaultPort = Settings::Manager::getInt("port", "General");
    const auto safeDefaultPort = static_cast<std::uint16_t>(
        defaultPort > 0 && defaultPort <= 65535 ? defaultPort : 25565);
    const auto defaultEndpoint = security::TrustStore::canonicalEndpoint(
        defaultAddress, safeDefaultPort);
    mEditAddress->setCaption(
        defaultEndpoint.value_or("localhost:" + std::to_string(safeDefaultPort)));
    mEditAccount->setCaption(Settings::Manager::getString("accountName", "General"));
    reloadServerChoices();
    updateStoredFingerprint();
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

void GUIServerBrowser::onAddressSelected(MyGUI::ComboBox*, std::size_t)
{
    updateStoredFingerprint();
}

void GUIServerBrowser::onPingClicked(MyGUI::Widget*)
{
    const auto endpoint = parseEndpoint(mEditAddress->getCaption().asUTF8());
    if (!endpoint)
    {
        mStatusLabel->setCaption("Use host:port or [IPv6]:port before probing.");
        return;
    }

    mStatusLabel->setCaption("Probing the protocol-11 encrypted handshake...");
    const ServerProbeResult result = Networking::probeServer(endpoint->host, endpoint->port);
    if (!result.reachable)
    {
        mStatusLabel->setCaption(result.detail);
        return;
    }

    const std::string expected = mEditFingerprint->getCaption().asUTF8();
    if (!expected.empty() && expected != result.fingerprint)
    {
        mFingerprintLabel->setCaption("Presented fingerprint: " + result.fingerprint);
        mStatusLabel->setCaption("Reachable, but the presented fingerprint does not match the expected one.");
        return;
    }
    mFingerprintLabel->setCaption("Presented fingerprint: " + result.fingerprint);
    mStatusLabel->setCaption(result.detail + " Handshake: "
        + std::to_string(result.elapsed.count()) + " ms.");
}

void GUIServerBrowser::onSaveClicked(MyGUI::Widget*)
{
    const auto endpoint = parseEndpoint(mEditAddress->getCaption().asUTF8());
    if (!endpoint)
    {
        mStatusLabel->setCaption("Use host:port or [IPv6]:port before saving.");
        return;
    }

    auto saved = loadEndpoints("savedServers", sMaximumSavedServers);
    promoteEndpoint(saved, endpoint->canonical, sMaximumSavedServers);
    Settings::Manager::setStringArray("savedServers", "General", saved);
    mEditAddress->setCaption(endpoint->canonical);
    reloadServerChoices();
    updateStoredFingerprint();
    mStatusLabel->setCaption("Saved this direct-connect address. Passwords are never saved.");
}

void GUIServerBrowser::onForgetClicked(MyGUI::Widget*)
{
    const auto endpoint = parseEndpoint(mEditAddress->getCaption().asUTF8());
    if (!endpoint)
    {
        mStatusLabel->setCaption("Select a saved or recent address to remove.");
        return;
    }

    auto saved = loadEndpoints("savedServers", sMaximumSavedServers);
    auto recent = loadEndpoints("recentServers", sMaximumRecentServers);
    const std::size_t removed = std::erase(saved, endpoint->canonical)
        + std::erase(recent, endpoint->canonical);
    Settings::Manager::setStringArray("savedServers", "General", saved);
    Settings::Manager::setStringArray("recentServers", "General", recent);
    reloadServerChoices();
    mStatusLabel->setCaption(removed == 0
            ? "This address was not in saved or recent servers."
            : "Removed address history. Its security fingerprint remains trusted.");
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

void GUIServerBrowser::reloadServerChoices()
{
    const std::string current = mEditAddress->getCaption().asUTF8();
    const auto saved = loadEndpoints("savedServers", sMaximumSavedServers);
    const auto recent = loadEndpoints("recentServers", sMaximumRecentServers);

    mEditAddress->removeAllItems();
    for (const std::string& endpoint : saved)
        mEditAddress->addItem(endpoint);
    for (const std::string& endpoint : recent)
    {
        if (std::ranges::find(saved, endpoint) == saved.end())
            mEditAddress->addItem(endpoint);
    }
    mEditAddress->setCaption(current);
}

void GUIServerBrowser::updateStoredFingerprint()
{
    mEditFingerprint->setCaption("");
    mFingerprintLabel->setCaption("Stored fingerprint: none");
    const auto endpoint = parseEndpoint(mEditAddress->getCaption().asUTF8());
    if (!endpoint)
        return;

    Files::ConfigurationManager configuration;
    std::string error;
    auto store = security::TrustStore::load(
        configuration.getUserConfigPath() / "trusted-servers.json", error);
    if (!store)
        return;
    const auto fingerprint = store->trustedFingerprint(endpoint->host, endpoint->port);
    if (fingerprint)
        mFingerprintLabel->setCaption("Stored fingerprint: " + *fingerprint);
}

void GUIServerBrowser::doConnect(bool registerAccount)
{
    const auto endpoint = parseEndpoint(mEditAddress->getCaption().asUTF8());
    if (!endpoint)
    {
        mStatusLabel->setCaption("Use host:port or [IPv6]:port with a port from 1 to 65535.");
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
    const std::string accountName = options.accountName;
    const bool connected = Main::connectTo(endpoint->host, endpoint->port, std::move(options));
    mEditPassword->setCaption("");
    mEditServerPassword->setCaption("");
    if (connected)
    {
        auto recent = loadEndpoints("recentServers", sMaximumRecentServers);
        promoteEndpoint(recent, endpoint->canonical, sMaximumRecentServers);
        Settings::Manager::setStringArray("recentServers", "General", recent);
        Settings::Manager::setString("destinationAddress", "General", endpoint->host);
        Settings::Manager::setInt("port", "General", endpoint->port);
        Settings::Manager::setString("accountName", "General", accountName);
        setVisible(false);
    }
    else
        refresh();
}
