#include "GUIServerBrowser.hpp"
#include <MyGUI_TextIterator.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <components/openmw-mp/Protocol/ProtocolLimits.hpp>
#include <components/openmw-mp/Version.hpp>
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

    getWidget(mPublicPanel, "PublicPanel");
    getWidget(mDirectPanel, "DirectPanel");
    getWidget(mPublicServers, "PublicServers");
    getWidget(mSearch, "SearchServers");
    getWidget(mButtonPublic, "ButtonPublic");
    getWidget(mButtonRefresh, "RefreshServers");
    getWidget(mButtonMore, "MoreServers");
    mPublicPanel->setVisible(false);
    mButtonMore->setEnabled(false);
    mButtonPublic->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIServerBrowser::onPublicClicked);
    mButtonRefresh->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIServerBrowser::onRefreshClicked);
    mButtonMore->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIServerBrowser::onMoreClicked);
    mSearch->eventEditTextChange += MyGUI::newDelegate(this, &GUIServerBrowser::onSearchChanged);
    mPublicServers->eventListSelectAccept += MyGUI::newDelegate(this, &GUIServerBrowser::onPublicSelected);

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
    updateStoredFingerprint(true);
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

    if (mProbeRequest.valid()) return;
    mProbeAddress = endpoint->canonical;
    mStatusLabel->setCaption("Probing the encrypted handshake...");
    mButtonPing->setEnabled(false);
    mCancelProbe = false;
    mProbeRequest = std::async(std::launch::async, [this, host=endpoint->host, port=endpoint->port] {
        return Networking::probeServer(host, port, &mCancelProbe);
    });
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
    mCancelDiscovery = true;
    mCancelProbe = true;
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

void GUIServerBrowser::updateStoredFingerprint(bool preserveInput)
{
    if (!preserveInput) mEditFingerprint->setCaption("");
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
    if (endpoint->canonical == mListedEndpoint && !mListedFingerprint.empty())
    {
        options.expectedFingerprint = mListedFingerprint;
        if (!fingerprint.empty() && fingerprint != mListedFingerprint)
        {
            mStatusLabel->setCaption("The expected fingerprint differs from the selected public server.");
            return;
        }
    }
    else if (!fingerprint.empty())
        options.expectedFingerprint = fingerprint;

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

GUIServerBrowser::~GUIServerBrowser()
{
    mCancelDiscovery = true;
    mCancelProbe = true;
}

void GUIServerBrowser::onPublicClicked(MyGUI::Widget*)
{
    const bool visible = !mPublicPanel->getVisible();
    mPublicPanel->setVisible(visible);
    mDirectPanel->setVisible(!visible);
    if (!visible) mCancelDiscovery = true;
    mButtonLogin->setEnabled(!visible);
    mButtonRegister->setEnabled(!visible);
    mButtonPublic->setCaption(visible ? "Direct connect" : "Public servers");
    if (visible && mServers.empty()) startDirectoryRequest(true);
}

void GUIServerBrowser::onRefreshClicked(MyGUI::Widget*) { startDirectoryRequest(true); }
void GUIServerBrowser::onMoreClicked(MyGUI::Widget*) { startDirectoryRequest(false); }
void GUIServerBrowser::onSearchChanged(MyGUI::EditBox*) { filterServers(); }

void GUIServerBrowser::startDirectoryRequest(bool reset)
{
    if (mDirectoryRequest.valid()) return;
    discovery::HttpOptions options{
        Settings::Manager::getOrDefault<std::string>("directoryUrl", "Discovery", ""),
        Settings::Manager::getOrDefault<std::string>("caFile", "Discovery", "")};
    if (options.origin.empty())
    {
        mStatusLabel->setCaption("Public discovery is not configured. Enter a server address to connect directly.");
        return;
    }
    if (reset) { mServers.clear(); mNextPage.clear(); filterServers(); }
    else if (mNextPage.empty() || mServers.size() >= discovery::maximumListings) return;
    mCancelDiscovery = false;
    mButtonRefresh->setEnabled(false);
    mButtonMore->setEnabled(false);
    mStatusLabel->setCaption("Loading public servers...");
    mDirectoryRequest = std::async(std::launch::async, [this, options, cursor=mNextPage] {
        return discovery::fetch(options, cursor, mCancelDiscovery);
    });
}

void GUIServerBrowser::filterServers()
{
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const auto search = lower(mSearch->getCaption().asUTF8());
    mPublicServers->removeAllItems();
    mFiltered.clear();
    for (std::size_t i = 0; i < mServers.size(); ++i)
    {
        const auto& l = mServers[i].listing;
        if (lower(l.name).find(search) == std::string::npos) continue;
        mFiltered.push_back(i);
        const auto label = l.name + "  " + std::to_string(l.players) + "/" + std::to_string(l.capacity)
            + (l.password ? "  [password]" : "")
            + (l.protocol == TES3MP_PROTO_VERSION ? "  [protocol matches]" : "  [incompatible protocol]")
            + "  " + std::to_string(l.content.size()) + " content rules";
        // Server names are untrusted text, never MyGUI colour markup.
        mPublicServers->addItem(MyGUI::TextIterator::toTagsString(label));
    }
}

void GUIServerBrowser::onPublicSelected(MyGUI::ListBox*, std::size_t index)
{
    if (index >= mFiltered.size()) return;
    const auto& server = mServers[mFiltered[index]];
    if (server.listing.protocol != TES3MP_PROTO_VERSION)
    {
        mStatusLabel->setCaption("This server advertises an incompatible gameplay protocol.");
        return;
    }
    mListedEndpoint = *security::TrustStore::canonicalEndpoint(server.listing.host, server.listing.port);
    mListedFingerprint = server.fingerprint;
    mEditAddress->setCaption(mListedEndpoint);
    updateStoredFingerprint();
    mEditFingerprint->setCaption(mListedFingerprint);
    onPublicClicked(nullptr);
    mStatusLabel->setCaption("Server selected. Content is checked on join; new identities require confirmation.");
}

void GUIServerBrowser::onFrame(float)
{
    if (mDirectoryRequest.valid() && mDirectoryRequest.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try
        {
            auto page = mDirectoryRequest.get();
            if (page.next == mNextPage && !page.next.empty()) throw std::runtime_error("Directory repeated its page cursor.");
            mNextPage = page.next;
            for (auto& server : page.servers)
            {
                if (mServers.size() == discovery::maximumListings) break;
                if (std::none_of(mServers.begin(), mServers.end(), [&](const auto& s) { return s.fingerprint == server.fingerprint; }))
                    mServers.push_back(std::move(server));
            }
            filterServers();
            mStatusLabel->setCaption("Double-click a server to select it. Content compatibility is checked when joining.");
        }
        catch (const std::exception& e) { mStatusLabel->setCaption(MyGUI::TextIterator::toTagsString(e.what())); }
        mButtonRefresh->setEnabled(true);
        mButtonMore->setEnabled(!mNextPage.empty() && mServers.size() < discovery::maximumListings);
    }
    if (mProbeRequest.valid() && mProbeRequest.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        mButtonPing->setEnabled(true);
        try
        {
            auto result = mProbeRequest.get();
            auto current = parseEndpoint(mEditAddress->getCaption().asUTF8());
            if (!current || current->canonical != mProbeAddress) return;
            const auto expected = mEditFingerprint->getCaption().asUTF8();
            mFingerprintLabel->setCaption("Presented fingerprint: " + result.fingerprint);
            if (result.reachable && !expected.empty() && expected != result.fingerprint)
                mStatusLabel->setCaption("Reachable, but its identity differs from the expected fingerprint.");
            else mStatusLabel->setCaption(MyGUI::TextIterator::toTagsString(result.detail));
        }
        catch (const std::exception& e) { mStatusLabel->setCaption(MyGUI::TextIterator::toTagsString(e.what())); }
    }
}
