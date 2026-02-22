#include <QApplication>
#include <components/settings/settings.hpp>
#include <components/files/configurationmanager.hpp>
#include <apps/browser/netutils/QueryClient.hpp>
#include "MainWindow.hpp"
#include <filesystem>
#include <components/misc/strings/lower.hpp>
#include <components/misc/stringops.hpp>

std::string loadSettings()
{
    Files::ConfigurationManager mCfgMgr;
    return Settings::Manager::load(mCfgMgr).string();
}

int main(int argc, char *argv[])
{
    loadSettings();

    std::string addr = Settings::Manager::getString("address", "Master");
    int port = Settings::Manager::getInt("port", "Master");

    // Is this an attempt to connect to the official master server at the old port? If so,
    // redirect it to the correct port for the currently used fork of RakNet
    if (Misc::StringUtils::ciEqual(addr, "master.tes3mp.com") && port == 25560)
        port = 25561;

    // initialize resources, if needed
    // Q_INIT_RESOURCE(resfile);

    QueryClient::Get().SetServer(addr, port);
    QApplication app(argc, argv);
    MainWindow d;

    d.show();
    return app.exec();
}
