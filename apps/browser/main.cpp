#include <QApplication>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <components/settings/settings.hpp>
#include <components/files/configurationmanager.hpp>
#include <apps/browser/netutils/QueryClient.hpp>
#include "MainWindow.hpp"
#include <filesystem>
#include <components/misc/strings/lower.hpp>
#include <components/misc/stringops.hpp>

int main(int argc, char *argv[])
{
    boost::program_options::variables_map variables;
    boost::program_options::options_description description;
    Files::ConfigurationManager cfgMgr;
    cfgMgr.addCommonOptions(description);
    cfgMgr.readConfiguration(variables, description, true);

    try { Settings::Manager::load(cfgMgr); } catch (...) {}

    std::string addr = "master.tes3mp.com";
    int port = 25561;

    try { addr = Settings::Manager::getString("address", "Master"); } catch (...) {}
    try { port = Settings::Manager::getInt("port", "Master"); } catch (...) {}

    // Is this an attempt to connect to the official master server at the old port? If so,
    // redirect it to the correct port for the currently used fork of RakNet
    if (Misc::StringUtils::ciEqual(addr, "master.tes3mp.com") && port == 25560)
        port = 25561;

    QueryClient::Get().SetServer(addr, port);
    QApplication app(argc, argv);
    MainWindow d;

    d.show();
    return app.exec();
}
