#ifndef OPENMW_TIMERAPI_HPP
#define OPENMW_TIMERAPI_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <Script/Script.hpp>
#include <Script/ScriptFunction.hpp>

namespace mwmp
{

    class TimerAPI;

    class Timer: public ScriptFunction
    {
        friend class TimerAPI;

    public:

        Timer(ScriptFunc callback, long msec, const std::string& def, std::vector<boost::any> args);
#if defined(ENABLE_LUA)
        Timer(lua_State *lua, ScriptFuncLua callback, long msec, const std::string& def, std::vector<boost::any> args);
#endif
        void Tick();

        bool IsEnded();
        void Stop();
        void Start();
        void Restart(int msec);
    private:
        double startTime, targetMsec;
        std::string publ, arg_types;
        std::vector<boost::any> args;
        Script *scr;
        bool isEnded;
    };

    class TimerAPI
    {
    public:
        static constexpr int MaximumTimers = 16384;

#if defined(ENABLE_LUA)
        static int CreateTimerLua(lua_State *lua, ScriptFuncLua callback, long msec, const std::string& def, std::vector<boost::any> args);
#endif
        static int CreateTimer(ScriptFunc callback, long msec, const std::string& def, std::vector<boost::any> args);
        static void FreeTimer(int timerid);
        static void ResetTimer(int timerid, long msec);
        static void StartTimer(int timerid);
        static void StopTimer(int timerid);
        static bool IsTimerElapsed(int timerid);

        static void Terminate();

        static void Tick();
    private:
        static int allocate(std::unique_ptr<Timer> timer);
        static void applyDeferredFrees();

        static std::unordered_map<int, std::unique_ptr<Timer>> timers;
        static std::vector<int> deferredFrees;
        static bool ticking;
    };
}

#endif //OPENMW_TIMERAPI_HPP
