#include "PacketGUIBoxes.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketGUIBoxes::PacketGUIBoxes() : PlayerPacket()
{
    packetID = ID_GUI_MESSAGEBOX;
}

void PacketGUIBoxes::Packet(bool send)
{
    PlayerPacket::Packet(send);

    RW(player->guiMessageBox.id, send);
    RW(player->guiMessageBox.type, send);
    RW(player->guiMessageBox.label, send);

    RW(player->guiMessageBox.data, send, true);

    if (player->guiMessageBox.type == BasePlayer::GUIMessageBox::CustomMessageBox)
        RW(player->guiMessageBox.buttons, send);
    else if (player->guiMessageBox.type == BasePlayer::GUIMessageBox::InputDialog ||
        player->guiMessageBox.type == BasePlayer::GUIMessageBox::PasswordDialog)
        RW(player->guiMessageBox.note, send);
}

