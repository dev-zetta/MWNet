#include "../Packets/Player/PacketDisconnect.hpp"
#include "../Packets/Player/PacketChatMessage.hpp"
#include "../Packets/Player/PacketPlayerCharGen.hpp"
#include "../Packets/Player/PacketGUIBoxes.hpp"
#include "../Packets/Player/PacketLoaded.hpp"
#include "../Packets/Player/PacketGameSettings.hpp"
#include "../Packets/Player/PacketPlayerSpellsActive.hpp"
#include "../Packets/Player/PacketPlayerAlly.hpp"
#include "../Packets/Player/PacketPlayerAnimFlags.hpp"
#include "../Packets/Player/PacketPlayerAnimPlay.hpp"
#include "../Packets/Player/PacketPlayerAttack.hpp"
#include "../Packets/Player/PacketPlayerAttribute.hpp"
#include "../Packets/Player/PacketPlayerBaseInfo.hpp"
#include "../Packets/Player/PacketPlayerBehavior.hpp"
#include "../Packets/Player/PacketPlayerBook.hpp"
#include "../Packets/Player/PacketPlayerBounty.hpp"
#include "../Packets/Player/PacketPlayerCast.hpp"
#include "../Packets/Player/PacketPlayerCellChange.hpp"
#include "../Packets/Player/PacketPlayerCellState.hpp"
#include "../Packets/Player/PacketPlayerClass.hpp"
#include "../Packets/Player/PacketPlayerCooldowns.hpp"
#include "../Packets/Player/PacketPlayerDeath.hpp"
#include "../Packets/Player/PacketPlayerEquipment.hpp"
#include "../Packets/Player/PacketPlayerFaction.hpp"
#include "../Packets/Player/PacketPlayerInput.hpp"
#include "../Packets/Player/PacketPlayerInventory.hpp"
#include "../Packets/Player/PacketPlayerItemUse.hpp"
#include "../Packets/Player/PacketPlayerJail.hpp"
#include "../Packets/Player/PacketPlayerJournal.hpp"
#include "../Packets/Player/PacketPlayerLevel.hpp"
#include "../Packets/Player/PacketPlayerMiscellaneous.hpp"
#include "../Packets/Player/PacketPlayerMomentum.hpp"
#include "../Packets/Player/PacketPlayerPosition.hpp"
#include "../Packets/Player/PacketPlayerQuickKeys.hpp"
#include "../Packets/Player/PacketPlayerReputation.hpp"
#include "../Packets/Player/PacketPlayerRest.hpp"
#include "../Packets/Player/PacketPlayerResurrect.hpp"
#include "../Packets/Player/PacketPlayerShapeshift.hpp"
#include "../Packets/Player/PacketPlayerSkill.hpp"
#include "../Packets/Player/PacketPlayerSpeech.hpp"
#include "../Packets/Player/PacketPlayerSpellbook.hpp"
#include "../Packets/Player/PacketPlayerStatsDynamic.hpp"
#include "../Packets/Player/PacketPlayerTopic.hpp"

#include "PlayerPacketController.hpp"

template <typename T>
inline void AddPacket(mwmp::PlayerPacketController::packets_t *packets)
{
    auto packet = std::make_unique<T>();
    const auto id = packet->GetPacketID();
    packets->emplace(id, std::move(packet));
}

mwmp::PlayerPacketController::PlayerPacketController()
{
    AddPacket<PacketDisconnect>(&packets);
    AddPacket<PacketChatMessage>(&packets);
    AddPacket<PacketGUIBoxes>(&packets);
    AddPacket<PacketLoaded>(&packets);
    AddPacket<PacketGameSettings>(&packets);
    AddPacket<PacketPlayerSpellsActive>(&packets);

    AddPacket<PacketPlayerAlly>(&packets);
    AddPacket<PacketPlayerAnimFlags>(&packets);
    AddPacket<PacketPlayerAnimPlay>(&packets);
    AddPacket<PacketPlayerAttack>(&packets);
    AddPacket<PacketPlayerAttribute>(&packets);
    AddPacket<PacketPlayerBaseInfo>(&packets);
    AddPacket<PacketPlayerBehavior>(&packets);
    AddPacket<PacketPlayerBook>(&packets);
    AddPacket<PacketPlayerBounty>(&packets);
    AddPacket<PacketPlayerCast>(&packets);
    AddPacket<PacketPlayerCellChange>(&packets);
    AddPacket<PacketPlayerCellState>(&packets);
    AddPacket<PacketPlayerCharGen>(&packets);
    AddPacket<PacketPlayerClass>(&packets);
    AddPacket<PacketPlayerCooldowns>(&packets);
    AddPacket<PacketPlayerDeath>(&packets);
    AddPacket<PacketPlayerEquipment>(&packets);
    AddPacket<PacketPlayerFaction>(&packets);
    AddPacket<PacketPlayerInput>(&packets);
    AddPacket<PacketPlayerInventory>(&packets);
    AddPacket<PacketPlayerItemUse>(&packets);
    AddPacket<PacketPlayerJail>(&packets);
    AddPacket<PacketPlayerJournal>(&packets);
    AddPacket<PacketPlayerLevel>(&packets);
    AddPacket<PacketPlayerMiscellaneous>(&packets);
    AddPacket<PacketPlayerMomentum>(&packets);
    AddPacket<PacketPlayerPosition>(&packets);
    AddPacket<PacketPlayerQuickKeys>(&packets);
    AddPacket<PacketPlayerReputation>(&packets);
    AddPacket<PacketPlayerRest>(&packets);
    AddPacket<PacketPlayerResurrect>(&packets);
    AddPacket<PacketPlayerShapeshift>(&packets);
    AddPacket<PacketPlayerSkill>(&packets);
    AddPacket<PacketPlayerSpeech>(&packets);
    AddPacket<PacketPlayerSpellbook>(&packets);
    AddPacket<PacketPlayerStatsDynamic>(&packets);
    AddPacket<PacketPlayerTopic>(&packets);
}


mwmp::PlayerPacket *mwmp::PlayerPacketController::GetPacket(std::uint16_t id)
{
    const auto packet = packets.find(static_cast<unsigned char>(id));
    return packet == packets.end() ? nullptr : packet->second.get();
}

void mwmp::PlayerPacketController::SetStream(RakNet::BitStream *inStream, RakNet::BitStream *outStream)
{
    for(const auto &packet : packets)
        packet.second->SetStreams(inStream, outStream);
}

void mwmp::PlayerPacketController::SetApplicationPacketDispatcher(
    transport::ApplicationPacketDispatcher* dispatcher)
{
    for (const auto& packet : packets)
        packet.second->SetApplicationPacketDispatcher(dispatcher);
}

bool mwmp::PlayerPacketController::ContainsPacket(std::uint16_t id)
{
    return packets.find(static_cast<unsigned char>(id)) != packets.end();
}
