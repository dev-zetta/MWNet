#include "TimerAPI.hpp"

#include <chrono>

#include <algorithm>
#include <iostream>
using namespace mwmp;

Timer::Timer(ScriptFunc callback, long msec, const std::string& def, std::vector<boost::any> args) : ScriptFunction(callback, 'v', def)
{
    targetMsec = msec;
    this->args = args;
    isEnded = true;
}

#if defined(ENABLE_LUA)
Timer::Timer(lua_State *lua, ScriptFuncLua callback, long msec, const std::string& def, std::vector<boost::any> args): ScriptFunction(callback, lua, 'v', def)
{
    targetMsec = msec;
    this->args = args;
    isEnded = true;
}
#endif

void Timer::Tick()
{
    if (isEnded)
        return;

    const auto duration = std::chrono::system_clock::now().time_since_epoch();
    const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    if (time - startTime >= targetMsec)
    {
        isEnded = true;
        Call(args);
    }
}

bool Timer::IsEnded()
{
    return isEnded;
}

void Timer::Stop()
{
    isEnded = true;
}

void Timer::Restart(int msec)
{
    targetMsec = msec;
    Start();
}

void Timer::Start()
{
    isEnded = false;

    const auto duration = std::chrono::system_clock::now().time_since_epoch();
    const auto msec = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    startTime = msec;
}

std::unordered_map<int, std::unique_ptr<Timer>> TimerAPI::timers;
std::vector<int> TimerAPI::deferredFrees;
bool TimerAPI::ticking = false;

int TimerAPI::allocate(std::unique_ptr<Timer> timer)
{
    if (!timer || timers.size() >= static_cast<std::size_t>(MaximumTimers))
        return -1;

    for (int id = 0; id < MaximumTimers; ++id)
    {
        if (!timers.contains(id))
        {
            timers.emplace(id, std::move(timer));
            return id;
        }
    }
    return -1;
}

#if defined(ENABLE_LUA)
int TimerAPI::CreateTimerLua(lua_State *lua, ScriptFuncLua callback, long msec, const std::string& def, std::vector<boost::any> args)
{
    return allocate(std::make_unique<Timer>(lua, callback, msec, def, std::move(args)));
}
#endif


int TimerAPI::CreateTimer(ScriptFunc callback, long msec, const std::string &def, std::vector<boost::any> args)
{
    return allocate(std::make_unique<Timer>(callback, msec, def, std::move(args)));
}

void TimerAPI::FreeTimer(int timerid)
{
    const auto found = timers.find(timerid);
    if (found == timers.end())
    {
        std::cerr << "Timer " << timerid << " not found!" << std::endl;
        return;
    }

    if (ticking)
    {
        found->second->Stop();
        if (std::find(deferredFrees.begin(), deferredFrees.end(), timerid) == deferredFrees.end())
            deferredFrees.push_back(timerid);
        return;
    }
    timers.erase(found);
}

void TimerAPI::ResetTimer(int timerid, long msec)
{
    const auto found = timers.find(timerid);
    if (found == timers.end())
    {
        std::cerr << "Timer " << timerid << " not found!" << std::endl;
        return;
    }
    found->second->Restart(msec);
}

void TimerAPI::StartTimer(int timerid)
{
    const auto found = timers.find(timerid);
    if (found == timers.end())
    {
        std::cerr << "Timer " << timerid << " not found!" << std::endl;
        return;
    }
    found->second->Start();
}

void TimerAPI::StopTimer(int timerid)
{
    const auto found = timers.find(timerid);
    if (found == timers.end())
    {
        std::cerr << "Timer " << timerid << " not found!" << std::endl;
        return;
    }
    found->second->Stop();
}

bool TimerAPI::IsTimerElapsed(int timerid)
{
    const auto found = timers.find(timerid);
    if (found == timers.end())
    {
        std::cerr << "Timer " << timerid << " not found!" << std::endl;
        return false;
    }
    return found->second->IsEnded();
}

void TimerAPI::Terminate()
{
    if (ticking)
    {
        deferredFrees.reserve(timers.size());
        for (auto& [id, timer] : timers)
        {
            timer->Stop();
            deferredFrees.push_back(id);
        }
        return;
    }
    timers.clear();
    deferredFrees.clear();
}

void TimerAPI::Tick()
{
    std::vector<int> activeTimers;
    activeTimers.reserve(timers.size());
    for (const auto& [id, timer] : timers)
        activeTimers.push_back(id);

    ticking = true;
    try
    {
        for (const int id : activeTimers)
        {
            const auto found = timers.find(id);
            if (found != timers.end())
                found->second->Tick();
        }
    }
    catch (...)
    {
        ticking = false;
        applyDeferredFrees();
        throw;
    }
    ticking = false;
    applyDeferredFrees();
}

void TimerAPI::applyDeferredFrees()
{
    for (const int id : deferredFrees)
        timers.erase(id);
    deferredFrees.clear();
}
