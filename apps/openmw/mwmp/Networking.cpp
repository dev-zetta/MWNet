#include <stdexcept>
#include <iostream>
#include <string>

#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Utils.hpp>
#include <components/openmw-mp/Version.hpp>
#include <components/openmw-mp/Packets/PacketPreInit.hpp>
#include <components/openmw-mp/Protocol/MessageType.hpp>
#include <components/openmw-mp/Security/AuthenticationMessages.hpp>
#include <components/openmw-mp/Security/PasswordHash.hpp>
#include <components/openmw-mp/Session/SessionState.hpp>
#include <components/openmw-mp/Transport/LegacyPacketFrame.hpp>

#include <components/esm3/cellid.hpp>
#include <components/files/configurationmanager.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwclass/npc.hpp"

#include "../mwmechanics/combat.hpp"
#include "../mwmechanics/npcstats.hpp"

#include "../mwstate/statemanagerimp.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"

#include <SDL_messagebox.h>
#include <iomanip>
#include <components/version/version.hpp>
#include <sodium.h>

#include "Networking.hpp"
#include "Main.hpp"
#include "GUIController.hpp"
#include "processors/ProcessorInitializer.hpp"
#include "processors/SystemProcessor.hpp"
#include "processors/PlayerProcessor.hpp"
#include "processors/ObjectProcessor.hpp"
#include "processors/ActorProcessor.hpp"
#include "processors/WorldstateProcessor.hpp"
#include "CellController.hpp"

using namespace mwmp;

ClientConnectionOptions::~ClientConnectionOptions()
{
    if (!accountPassword.empty())
        sodium_memzero(accountPassword.data(), accountPassword.size());
    if (!serverAccessPassword.empty())
        sodium_memzero(serverAccessPassword.data(), serverAccessPassword.size());
}

std::string listDiscrepancies(PacketPreInit::PluginContainer checksums, PacketPreInit::PluginContainer checksumsResponse)
{
    std::ostringstream sstr;
    sstr << "Your plugins or their load order don't match the server's. A full comparison is included in your debug window and latest log file. In short, the following discrepancies have been found:\n\n";

    int discrepancyCount = 0;

    for (size_t fileIndex = 0; fileIndex < checksums.size() || fileIndex < checksumsResponse.size(); fileIndex++)
    {
        if (fileIndex >= checksumsResponse.size())
        {
            discrepancyCount++;

            if (discrepancyCount > 1)
                sstr << "\n";

            std::string clientFilename = checksums.at(fileIndex).first;

            sstr << fileIndex << ": ";
            sstr << clientFilename << " is past the number of plugins used by the server";
        }
        else if (fileIndex >= checksums.size())
        {
            discrepancyCount++;

            if (discrepancyCount > 1)
                sstr << "\n";

            std::string serverFilename = checksumsResponse.at(fileIndex).first;

            sstr << fileIndex << ": ";
            sstr << serverFilename << " is completely missing from the client but required by the server";
        }
        else
        {
            std::string clientFilename = checksums.at(fileIndex).first;
            std::string serverFilename = checksumsResponse.at(fileIndex).first;

            std::string clientChecksum = Utils::intToHexStr(checksums.at(fileIndex).second.at(0));

            bool filenameMatches = false;
            bool checksumMatches = false;
            std::string eligibleChecksums = "";

            if (Misc::StringUtils::ciEqual(clientFilename, serverFilename))
                filenameMatches = true;

            if (checksumsResponse.at(fileIndex).second.size() > 0)
            {
                for (size_t checksumIndex = 0; checksumIndex < checksumsResponse.at(fileIndex).second.size(); checksumIndex++)
                {
                    std::string serverChecksum = Utils::intToHexStr(checksumsResponse.at(fileIndex).second.at(checksumIndex));

                    if (checksumIndex != 0)
                        eligibleChecksums = eligibleChecksums + " or ";

                    eligibleChecksums = eligibleChecksums + serverChecksum;

                    if (Misc::StringUtils::ciEqual(clientChecksum, serverChecksum))
                    {
                        checksumMatches = true;
                        break;
                    }
                }
            }
            else
                checksumMatches = true;

            if (!filenameMatches || !checksumMatches)
            {
                discrepancyCount++;

                if (discrepancyCount > 1)
                    sstr << "\n";

                sstr << fileIndex << ": ";

                if (!filenameMatches)
                    sstr << clientFilename << " doesn't match " << serverFilename;

                if (!filenameMatches && !checksumMatches)
                    sstr << ", ";

                if (!checksumMatches)
                    sstr << "checksum " << clientChecksum << " doesn't match " << eligibleChecksums;
            }
        }
    }

    return sstr.str();
}

std::string listComparison(PacketPreInit::PluginContainer checksums, PacketPreInit::PluginContainer checksumsResponse,
                      bool full = false)
{
    std::ostringstream sstr;
    size_t pluginNameLen1 = 0;
    size_t pluginNameLen2 = 0;
    for (const auto &checksum : checksums)
        if (pluginNameLen1 < checksum.first.size())
            pluginNameLen1 = checksum.first.size();

    for (const auto &checksum : checksums)
        if (pluginNameLen2 < checksum.first.size())
            pluginNameLen2 = checksum.first.size();

    Utils::printWithWidth(sstr, "Your current plugins are:", pluginNameLen1 + 16);
    sstr << "To join this server, use:\n";

    Utils::printWithWidth(sstr, "name", pluginNameLen1 + 2);
    Utils::printWithWidth(sstr, "hash", 14);
    Utils::printWithWidth(sstr, "name", pluginNameLen2 + 2);
    sstr << "hash\n";

    for (size_t i = 0; i < checksums.size() || i < checksumsResponse.size(); i++)
    {
        std::string plugin;
        unsigned val;

        if (i < checksums.size())
        {
            plugin = checksums.at(i).first;
            val = checksums.at(i).second[0];

            Utils::printWithWidth(sstr, plugin, pluginNameLen1 + 2);
            Utils::printWithWidth(sstr, Utils::intToHexStr(val), 14);
        }
        else
            Utils::printWithWidth(sstr, "", pluginNameLen1 + 16);

        if (i < checksumsResponse.size())
        {
            Utils::printWithWidth(sstr, checksumsResponse[i].first, pluginNameLen2 + 2);
            if (checksumsResponse[i].second.size() > 0)
            {
                if (full)
                    for (size_t j = 0; j < checksumsResponse[i].second.size(); j++)
                        Utils::printWithWidth(sstr, Utils::intToHexStr(checksumsResponse[i].second[j]), 14);
                else
                    sstr << Utils::intToHexStr(checksumsResponse[i].second[0]);
            }
            else
                sstr << "any";
        }

        sstr << "\n";
    }

    return sstr.str();
}

Networking::Networking()
    : receiver(transport::ApplicationPacketFlow::ServerToClient)
    , systemPacketController(nullptr)
    , playerPacketController(nullptr)
    , actorPacketController(nullptr)
    , objectPacketController(nullptr)
    , worldstatePacketController(nullptr)
{
    Files::ConfigurationManager configuration;
    std::string error;
    endpoint = transport::Protocol11Endpoint::createClient(
        configuration.getUserConfigPath() / "trusted-servers.json", error);
    if (!endpoint)
        throw std::runtime_error("Failed to initialize protocol-11 client transport: " + error);
    dispatcher = std::make_unique<transport::ApplicationPacketDispatcher>(
        endpoint->transport(), transport::ApplicationPacketFlow::ClientToServer, 1);

    systemPacketController.SetStream(0, &bsOut);
    playerPacketController.SetStream(0, &bsOut);
    actorPacketController.SetStream(0, &bsOut);
    objectPacketController.SetStream(0, &bsOut);
    worldstatePacketController.SetStream(0, &bsOut);
    systemPacketController.SetApplicationPacketDispatcher(dispatcher.get());
    playerPacketController.SetApplicationPacketDispatcher(dispatcher.get());
    actorPacketController.SetApplicationPacketDispatcher(dispatcher.get());
    objectPacketController.SetApplicationPacketDispatcher(dispatcher.get());
    worldstatePacketController.SetApplicationPacketDispatcher(dispatcher.get());

    connected = false;
    ProcessorInitializer();
}

Networking::~Networking()
{
    disconnect();
    endpoint->shutdown(std::chrono::seconds(5));
}

void Networking::update()
{
    if (!pendingPackets.empty() && mwmp::Main::isPostInitDone())
    {
        std::vector<unsigned char> data = std::move(pendingPackets.front());
        pendingPackets.pop_front();
        pendingPacketBytes -= data.size();
        RakNet::Packet fake{};
        fake.data = data.data();
        fake.length = (unsigned int)data.size();
        fake.systemAddress = serverAddr;
        fake.guid = RakNet::RakNetGUID(serverConnection.value);
        receiveMessage(&fake);
    }

    for (std::size_t count = 0; connected && count < 256; ++count)
    {
        auto event = endpoint->poll(std::chrono::milliseconds(0));
        if (!event)
            break;
        processTransportEvent(std::move(*event));
    }
}

void Networking::connect(const std::string& ip, unsigned short port,
    std::vector<std::string>& content, Files::Collections& collections,
    ClientConnectionOptions options)
{
    disconnect();
    lastError.clear();
    pendingPackets.clear();
    pendingPacketBytes = 0;
    receiver.clear();
    serverAddr.SetBinaryAddress(ip.c_str());
    serverAddr.SetPortHostOrder(port);
    BaseClientPacketProcessor::SetServerAddr(serverAddr);

    transport::ConnectOptions connectOptions;
    connectOptions.host = ip;
    connectOptions.port = port;
    connectOptions.trustedFingerprint = options.trustedFingerprint;
    transport::TransportError error;
    if (!endpoint->connect(connectOptions, serverConnection, error))
    {
        failConnection(error.detail.empty() ? "Connection attempt failed." : error.detail);
        return;
    }

    const auto deadline = std::chrono::steady_clock::now()
        + connectOptions.timeouts.connect + connectOptions.timeouts.handshake;
    while (!connected && std::chrono::steady_clock::now() < deadline)
    {
        auto event = endpoint->poll(std::chrono::milliseconds(100));
        if (!event)
            continue;
        if (event->connection != serverConnection)
            continue;
        if (event->type == transport::TransportEventType::TrustRequired)
        {
            if (!confirmServerFingerprint(ip, port, event->detail))
            {
                failConnection("The server fingerprint was not trusted.");
                return;
            }
            if (!endpoint->confirmFingerprint(serverConnection, event->detail, error))
            {
                failConnection(error.detail.empty()
                        ? "Failed to store the server fingerprint." : error.detail);
                return;
            }
        }
        else if (event->type == transport::TransportEventType::Connected)
        {
            if (!dispatcher->addConnection(serverConnection))
            {
                failConnection("Failed to bind the server transport connection.");
                return;
            }
            connected = true;
        }
        else if (event->type == transport::TransportEventType::Disconnected)
        {
            failConnection(event->detail.empty()
                    ? "Connection closed during the secure handshake." : event->detail);
            return;
        }
    }
    if (!connected)
    {
        failConnection("Timed out while establishing the encrypted server session.");
        return;
    }

    if (!preInit(content, collections) || !authenticate(options) || !requestSpawn())
        return;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
        "Protocol 11 session established with %s:%u",
        ip.c_str(), static_cast<unsigned int>(port));
}

bool Networking::preInit(std::vector<std::string>& content, Files::Collections& collections)
{
    PacketPreInit::PluginContainer checksums;
    std::vector<std::string>::const_iterator it(content.begin());
    for (int idx = 0; it != content.end(); ++it, ++idx)
    {
        boost::filesystem::path filename(*it);
        std::string ext = filename.extension().string();
        // MultiDirCollection expects extension WITHOUT leading dot
        if (!ext.empty() && ext[0] == '.')
            ext = ext.substr(1);
        const Files::MultiDirCollection& col = collections.getCollection(ext);
        if (col.doesExist(*it))
        {
            PacketPreInit::HashList hashList;
            unsigned crc32 = Utils::crc32Checksum(col.getPath(*it).string());
            hashList.push_back(crc32);
            checksums.push_back(make_pair(*it, hashList));

            LOG_APPEND(TimedLog::LOG_WARN, "idx: %d\tchecksum: %X\tfile: %s\n", idx, crc32, col.getPath(*it).string().c_str());
        }
        else
        {
            std::string errmsg = "Plugin not found: \"" + *it + "\" (extension: \"" + filename.extension().string() + "\")";
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "%s", errmsg.c_str());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "tes3mp - Plugin not found", errmsg.c_str(), 0);
            return failConnection(errmsg);
        }
    }

    PacketPreInit packetPreInit(nullptr);
    RakNet::BitStream bs;
    packetPreInit.setChecksums(&checksums);
    packetPreInit.setGUID(RakNet::RakNetGUID(serverConnection.value));
    packetPreInit.SetSendStream(&bs);
    packetPreInit.SetApplicationPacketDispatcher(dispatcher.get());
    if (packetPreInit.Send(serverAddr) == 0)
        return failConnection("Failed to send the content manifest.");

    PacketPreInit::PluginContainer checksumsResponse;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    bool receivedResponse = false;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto event = endpoint->poll(std::chrono::milliseconds(100));
        if (!event)
            continue;
        if (event->type == transport::TransportEventType::Disconnected)
            return failConnection(event->detail.empty()
                    ? "Server closed the connection during content verification." : event->detail);
        if (event->type != transport::TransportEventType::Message)
            continue;

        transport::ReceivedApplicationPacket application;
        if (!receiveApplicationMessage(event->message, application)
            || application.id != protocol::ApplicationPacketId::GamePreInit)
            return failConnection("Received an invalid content-verification response.");
        std::vector<unsigned char> frame;
        protocol::CodecError codecError = protocol::CodecError::None;
        if (!transport::buildLegacyPacketFrame(application, frame, codecError))
            return failConnection("Failed to decode the content-verification response.");

        RakNet::BitStream bsIn(&frame[1], frame.size() - 1, false);
        bsIn.IgnoreBytes(static_cast<unsigned int>(RakNet::RakNetGUID::size()));
        packetPreInit.setChecksums(&checksumsResponse);
        packetPreInit.SetReadStream(&bsIn);
        packetPreInit.Read();
        if (!packetPreInit.isPacketValid())
            return failConnection("The server sent an invalid content-verification response.");
        receivedResponse = true;
        break;
    }
    if (!receivedResponse)
        return failConnection("Timed out during content verification.");

    if (!checksumsResponse.empty())
    {
        std::string errmsg = listDiscrepancies(checksums, checksumsResponse);
        std::string comparison = listComparison(checksums, checksumsResponse, true);

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "%s", errmsg.c_str());
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "%s", comparison.c_str());
        return failConnection(std::move(errmsg));
    }

    transport::TransportError error;
    if (endpoint->advance(serverConnection, session::State::ContentVerified, error)
        != session::TransitionResult::Advanced)
        return failConnection(error.detail.empty()
                ? "Failed to advance the verified content session." : error.detail);
    return true;
}

bool Networking::authenticate(ClientConnectionOptions& options)
{
    std::string errorMessage;
    auto password = security::PasswordBuffer::copyFrom(options.accountPassword, errorMessage);
    if (!options.accountPassword.empty())
        sodium_memzero(options.accountPassword.data(), options.accountPassword.size());
    options.accountPassword.clear();
    if (!password)
        return failConnection(errorMessage);

    std::optional<security::PasswordBuffer> accessPassword;
    if (!options.serverAccessPassword.empty())
    {
        accessPassword = security::PasswordBuffer::copyFrom(
            options.serverAccessPassword, errorMessage);
        sodium_memzero(options.serverAccessPassword.data(), options.serverAccessPassword.size());
        options.serverAccessPassword.clear();
        if (!accessPassword)
            return failConnection(errorMessage);
    }

    std::vector<std::byte> payload;
    protocol::CodecError codecError = protocol::CodecError::None;
    const auto operation = options.registerAccount
        ? security::AuthenticationOperation::Register
        : security::AuthenticationOperation::Login;
    if (!security::encodeAuthenticationRequest(operation, options.accountName, *password,
            accessPassword ? &*accessPassword : nullptr, payload, codecError))
        return failConnection("Failed to encode the authentication request.");

    transport::TransportMessage request;
    request.connection = serverConnection;
    request.delivery = transport::DeliveryMode::ReliableOrdered;
    request.lane = transport::MessageLane::System;
    request.messageType = static_cast<std::uint16_t>(options.registerAccount
        ? protocol::MessageType::AccountRegister : protocol::MessageType::AccountLogin);
    request.sequence = 1;
    request.payload = std::move(payload);
    transport::TransportError transportError;
    if (!endpoint->send(std::move(request), transportError))
        return failConnection(transportError.detail.empty()
                ? "Failed to send the authentication request." : transportError.detail);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto event = endpoint->poll(std::chrono::milliseconds(100));
        if (!event)
            continue;
        if (event->type == transport::TransportEventType::Disconnected)
            return failConnection(event->detail.empty()
                    ? "Server closed the connection during authentication." : event->detail);
        if (event->type != transport::TransportEventType::Message)
            continue;
        if (event->message.messageType
            != static_cast<std::uint16_t>(protocol::MessageType::AuthenticationResult))
            return failConnection("Received an unexpected authentication message.");

        security::AuthenticationResponse response;
        if (!security::decodeAuthenticationResponse(event->message.payload, response))
            return failConnection("Received an invalid authentication response.");
        if (!response.authenticated())
            return failConnection(response.message.empty()
                    ? "Account authentication failed." : response.message);

        getLocalPlayer()->guid = getLocalSystem()->guid
            = RakNet::RakNetGUID(event->message.subject);
        if (endpoint->advance(serverConnection, session::State::AccountAuthenticated,
                transportError) != session::TransitionResult::Advanced)
            return failConnection(transportError.detail.empty()
                    ? "Failed to advance the authenticated account session."
                    : transportError.detail);
        return true;
    }
    return failConnection("Timed out during account authentication.");
}

bool Networking::requestSpawn()
{
    PlayerPacket* packet = getPlayerPacket(ID_LOADED);
    packet->setPlayer(getLocalPlayer());
    if (packet->Send() == 0)
        return failConnection("Failed to send the spawn-ready message.");

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto event = endpoint->poll(std::chrono::milliseconds(100));
        if (!event)
            continue;
        if (event->type == transport::TransportEventType::Disconnected)
            return failConnection(event->detail.empty()
                    ? "Server closed the connection while spawning." : event->detail);
        if (event->type != transport::TransportEventType::Message)
            continue;

        transport::ReceivedApplicationPacket application;
        if (!receiveApplicationMessage(event->message, application)
            || application.id != protocol::ApplicationPacketId::Loaded)
            return failConnection("Received an invalid spawn response.");
        transport::TransportError error;
        if (endpoint->advance(serverConnection, session::State::Spawned, error)
            != session::TransitionResult::Advanced)
            return failConnection(error.detail.empty()
                    ? "Failed to advance the spawned session." : error.detail);
        return true;
    }
    return failConnection("Timed out while waiting for the spawn response.");
}

bool Networking::confirmServerFingerprint(std::string_view host, unsigned short port,
    std::string_view fingerprint)
{
    const std::string message = "This is the first connection to " + std::string(host)
        + ":" + std::to_string(port) + ".\n\nServer identity:\n"
        + std::string(fingerprint)
        + "\n\nOnly continue if this fingerprint is expected.";
    const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel" },
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Trust and connect" },
    };
    SDL_MessageBoxData box{};
    box.flags = SDL_MESSAGEBOX_WARNING;
    box.title = "TES3MP server identity";
    box.message = message.c_str();
    box.numbuttons = 2;
    box.buttons = buttons;
    int selected = 0;
    return SDL_ShowMessageBox(&box, &selected) == 0 && selected == 1;
}

bool Networking::failConnection(std::string message)
{
    if (serverConnection)
    {
        dispatcher->removeConnection(serverConnection);
        receiver.removeConnection(serverConnection);
        endpoint->disconnect(serverConnection);
    }
    serverConnection = {};
    connected = false;
    lastError = std::move(message);
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "%s", lastError.c_str());
    return false;
}

bool Networking::receiveApplicationMessage(const transport::TransportMessage& message,
    transport::ReceivedApplicationPacket& packet)
{
    const auto result = receiver.receive(message, packet);
    return result.status == transport::ApplicationReceiveStatus::Accepted;
}

void Networking::processTransportEvent(transport::TransportEvent event)
{
    switch (event.type)
    {
        case transport::TransportEventType::Connected:
            break;
        case transport::TransportEventType::TrustRequired:
            failConnection("The server requested an unexpected trust confirmation.");
            Main::get().getGUIController()->requestShowBrowser();
            break;
        case transport::TransportEventType::Disconnected:
        {
            dispatcher->removeConnection(event.connection);
            receiver.removeConnection(event.connection);
            serverConnection = {};
            connected = false;
            lastError = event.detail.empty() ? "Connection to server lost." : event.detail;
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "%s", lastError.c_str());
            Main::get().getGUIController()->requestShowBrowser();
            break;
        }
        case transport::TransportEventType::Message:
        {
            transport::ReceivedApplicationPacket application;
            if (!receiveApplicationMessage(event.message, application))
            {
                failConnection("Received an invalid protocol-11 application packet.");
                Main::get().getGUIController()->requestShowBrowser();
                return;
            }
            std::vector<unsigned char> frame;
            protocol::CodecError codecError = protocol::CodecError::None;
            if (!transport::buildLegacyPacketFrame(application, frame, codecError))
            {
                failConnection("Failed to adapt a protocol-11 application packet.");
                Main::get().getGUIController()->requestShowBrowser();
                return;
            }
            if (!Main::isPostInitDone())
            {
                if (pendingPackets.size() >= protocol::limits::defaultCollectionElements
                    || frame.size() > protocol::limits::bulkTransferBytes - pendingPacketBytes)
                {
                    failConnection("Initial synchronization exceeded the bounded client queue.");
                    Main::get().getGUIController()->requestShowBrowser();
                    return;
                }
                pendingPacketBytes += frame.size();
                pendingPackets.push_back(std::move(frame));
            }
            else
            {
                RakNet::Packet packet{};
                packet.data = frame.data();
                packet.length = static_cast<unsigned int>(frame.size());
                packet.systemAddress = serverAddr;
                packet.guid = RakNet::RakNetGUID(serverConnection.value);
                receiveMessage(&packet);
            }
            break;
        }
    }
}

void Networking::receiveMessage(RakNet::Packet *packet)
{
    if (packet->length < BasePacket::headerSize()
        || packet->length > protocol::limits::normalMessageBytes + BasePacket::headerSize())
        return;

    if (systemPacketController.ContainsPacket(packet->data[0]))
    {
        if (!SystemProcessor::Process(*packet))
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled SystemPacket with identifier %i has arrived", packet->data[0]);
    }
    else if (playerPacketController.ContainsPacket(packet->data[0]))
    {
        if (!PlayerProcessor::Process(*packet))
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled PlayerPacket with identifier %i has arrived", packet->data[0]);
    }
    else if (actorPacketController.ContainsPacket(packet->data[0]))
    {
        if (!ActorProcessor::Process(*packet, actorList))
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled ActorPacket with identifier %i has arrived", packet->data[0]);
    }
    else if (objectPacketController.ContainsPacket(packet->data[0]))
    {
        if (!ObjectProcessor::Process(*packet, objectList))
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled ObjectPacket with identifier %i has arrived", packet->data[0]);
    }
    else if (worldstatePacketController.ContainsPacket(packet->data[0]))
    {
        if (!WorldstateProcessor::Process(*packet, worldstate))
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled WorldstatePacket with identifier %i has arrived", packet->data[0]);
    }
}

SystemPacket *Networking::getSystemPacket(RakNet::MessageID id)
{
    return systemPacketController.GetPacket(id);
}

PlayerPacket *Networking::getPlayerPacket(RakNet::MessageID id)
{
    return playerPacketController.GetPacket(id);
}

ActorPacket *Networking::getActorPacket(RakNet::MessageID id)
{
    return actorPacketController.GetPacket(id);
}

ObjectPacket *Networking::getObjectPacket(RakNet::MessageID id)
{
    return objectPacketController.GetPacket(id);
}

WorldstatePacket *Networking::getWorldstatePacket(RakNet::MessageID id)
{
    return worldstatePacketController.GetPacket(id);
}

LocalSystem *Networking::getLocalSystem()
{
    return mwmp::Main::get().getLocalSystem();
}

LocalPlayer *Networking::getLocalPlayer()
{
    return mwmp::Main::get().getLocalPlayer();
}

ActorList *Networking::getActorList()
{
    return &actorList;
}

ObjectList *Networking::getObjectList()
{
    return &objectList;
}

Worldstate *Networking::getWorldstate()
{
    return &worldstate;
}

bool Networking::isConnected()
{
    return connected;
}

void Networking::disconnect()
{
    if (serverConnection)
    {
        dispatcher->removeConnection(serverConnection);
        receiver.removeConnection(serverConnection);
        endpoint->disconnect(serverConnection);
        serverConnection = {};
    }
    pendingPackets.clear();
    pendingPacketBytes = 0;
    connected = false;
}
