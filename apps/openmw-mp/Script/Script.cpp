#include "Script.hpp"
#include "API/PublicFnAPI.hpp"
#include "LangNative/LangNative.hpp"
#include <filesystem>
#include <fstream>

#if defined (ENABLE_LUA)
#include "LangLua/LangLua.hpp"
#endif

Script::ScriptList Script::scripts;
std::string Script::moddir;
thread_local unsigned int Script::sPreAuthenticationDepth = 0;
thread_local unsigned int Script::sIntentValidationDepth = 0;
thread_local std::optional<unsigned short> Script::sIntentPlayer;

Script::CallbackContext::CallbackContext(
    bool preAuthentication, std::optional<unsigned short> intentPlayer) noexcept
    : mPreAuthentication(preAuthentication)
    , mIntentValidation(intentPlayer.has_value())
    , mPreviousIntentPlayer(sIntentPlayer)
{
    if (mPreAuthentication)
        ++sPreAuthenticationDepth;
    if (mIntentValidation)
    {
        ++sIntentValidationDepth;
        sIntentPlayer = intentPlayer;
    }
}

Script::CallbackContext::~CallbackContext()
{
    if (mPreAuthentication)
        --sPreAuthenticationDepth;
    if (mIntentValidation)
    {
        --sIntentValidationDepth;
        sIntentPlayer = mPreviousIntentPlayer;
    }
}

bool Script::IsPreAuthenticationCallback() noexcept
{
    return sPreAuthenticationDepth != 0;
}

bool Script::IsIntentValidationCallback() noexcept
{
    return sIntentValidationDepth != 0;
}

std::optional<unsigned short> Script::GetIntentPlayer() noexcept
{
    return sIntentPlayer;
}

Script::Script(const char *path)
{
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);

    if (!file)
        throw std::runtime_error("Script not found: " + std::string(path));

    file.close();

#ifdef _WIN32
    if (strstr(path, ".dll"))
#else
    if (strstr(path, ".so"))
#endif
    {
        script_type = SCRIPT_CPP;
        lang = std::make_unique<LangNative>();
    }
#if defined (ENABLE_LUA)
    else if (strstr(path, ".lua") || strstr(path, ".t"))
    {
        lang = std::make_unique<LangLua>();
        script_type = SCRIPT_LUA;
    }
#endif
    else
        throw std::runtime_error("Script type not recognized: " + std::string(path));

    lang->LoadProgram(path);
}

void Script::LoadScripts(char *scripts, const char *base)
{
    char *token = strtok(scripts, ",");

    try
    {
        while (token)
        {
            char path[4096];
            snprintf(path, sizeof(path), Utils::convertPath("%s/%s/%s").c_str(), base, "scripts", token);
            Script::scripts.emplace_back(new Script(path));
            token = strtok(nullptr, ",");
        }
    }
    catch (...)
    {
        UnloadScripts();
        throw;
    }
}

void Script::UnloadScripts()
{
    Public::DeleteAll();
    scripts.clear();
}

void Script::LoadScript(const char *script, const char *base)
{
    char path[4096];
    snprintf(path, sizeof(path), Utils::convertPath("%s/%s/%s").c_str(), base, "scripts", script);
    Script::scripts.emplace_back(new Script(path));
}

void Script::SetModDir(const std::string &moddir)
{
    if (Script::moddir.empty()) // do not allow to change in runtime
        Script::moddir = moddir;
}

const char* Script::GetModDir()
{
    return moddir.c_str();
}
