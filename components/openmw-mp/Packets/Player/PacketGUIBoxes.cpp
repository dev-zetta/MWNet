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

    Field(player->guiMessageBox.id);
    Field(player->guiMessageBox.type);
    Field(player->guiMessageBox.label);

    Field(player->guiMessageBox.data, true);

    if (player->guiMessageBox.type == BasePlayer::GUIMessageBox::CustomMessageBox)
        Field(player->guiMessageBox.buttons);
    else if (player->guiMessageBox.type == BasePlayer::GUIMessageBox::InputDialog ||
        player->guiMessageBox.type == BasePlayer::GUIMessageBox::PasswordDialog)
        Field(player->guiMessageBox.note);
}

