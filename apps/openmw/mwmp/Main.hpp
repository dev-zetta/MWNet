#ifndef OPENMW_MWMP_MAIN
#define OPENMW_MWMP_MAIN

#include "../mwworld/ptr.hpp"
#include <boost/program_options.hpp>
#include <components/files/collections.hpp>

namespace mwmp
{
    class GUIController;
    class CellController;
    class LocalSystem;
    class LocalPlayer;
    class Networking;
    struct ClientConnectionOptions;

    class Main
    {
    public:
        Main();
        ~Main();

        static void optionsDesc(boost::program_options::options_description *desc);
        static void configure(const boost::program_options::variables_map &variables);
        static bool init(std::vector<std::string> &content, Files::Collections &collections);
        static void postInit();
        static bool isInitialized();
        static bool isConnected();
        static const std::string &getAddress();
        static void destroy();
        static const Main &get();
        static void frame(float dt);

        // Deferred connect: called by the in-game direct-connect screen after init().
        static bool connectTo(const std::string& host, unsigned short port,
            ClientConnectionOptions options);
        static bool isNewGamePending();
        static void clearNewGamePending();
        static bool isPostInitDone();
        static bool isPendingReturnToBrowser();
        static void clearPendingReturnToBrowser();
        static void requestReturnToBrowser();

        static bool isValidPacketScript(std::string scriptId);
        static bool isValidPacketGlobal(std::string globalId);

        static std::string getResDir();

        Networking *getNetworking() const;
        LocalSystem *getLocalSystem() const;
        LocalPlayer *getLocalPlayer() const;
        GUIController *getGUIController() const;
        CellController *getCellController() const;

        void updateWorld(float dt) const;

    private:
        static std::string resourceDir;
        static std::string address;
        static std::string serverPassword;
        static std::string accountName;
        static std::string accountPasswordFile;
        static std::string trustedFingerprint;
        static bool registerAccount;
        static std::vector<std::string> sContentFiles;
        static Files::Collections sFileCollections;
        static bool sNewGamePending;
        static bool sPendingReturnToBrowser;
        Main (const Main&);
        ///< not implemented
        Main& operator= (const Main&);
        ///< not implemented
        static Main *pMain;
        Networking *mNetworking;
        LocalSystem *mLocalSystem;
        LocalPlayer *mLocalPlayer;

        GUIController *mGUIController;
        CellController *mCellController;

        std::string server;
        unsigned short port;
        bool mPostInitDone;
        bool mWorldInitDone;
    };
}

#endif //OPENMW_MWMP_MAIN
