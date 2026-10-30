#ifndef OPENMW_MWMP_GAMEPLAYTEST_HPP
#define OPENMW_MWMP_GAMEPLAYTEST_HPP

#include <string>

namespace mwmp
{
    // Explicit, local-only deterministic control for testing the real rendered
    // multiplayer client. It does not install a socket or accept remote input.
    class GameplayTest
    {
    public:
        static void configure(const std::string& path);
        static bool active();
        static bool configured();
        static bool requested();
        static std::string diagnostic(std::string message);
        static std::string address();
        static std::string account();
        static std::string accountPasswordFile();
        static std::string accountPassword();
        static std::string serverPassword();
        static std::string fingerprint();
        static bool registerAccount();
        static void frame(float dt);
        static void connectionFailed();
    };
}

#endif
