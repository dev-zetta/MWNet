#include <iostream>
#include "LangLua.hpp"
#include <Script/API/TimerAPI.hpp>
#include <Script/API/PublicFnAPI.hpp>

inline std::vector<boost::any> DefToVec(lua_State *lua, std::string types, int args_begin, int args_n)
{
    std::vector<boost::any> args;

    for (int i = args_begin; i < args_n + args_begin; i++)
    {
        switch (types[i - args_begin])
        {
            case 'i':
            {
                args.emplace_back(sol::stack::get<unsigned int>(lua, i));
                break;
            }

            case 'q':
            {
                args.emplace_back(sol::stack::get<signed int>(lua, i));
                break;
            }

                /*case 'l':
                {
                    args.emplace_back(sol::stack::get<unsigned long long>(lua, i));
                    break;
                }

                case 'w':
                {
                    args.emplace_back(sol::stack::get<signed long long>(lua, i));
                    break;
                }*/

            case 'f':
            {
                args.emplace_back(sol::stack::get<double>(lua, i));
                break;
            }

            case 's':
            {
                args.emplace_back(sol::stack::get<const char*>(lua, i));
                break;
            }

            default:
            {
                std::stringstream ssErr;
                ssErr << "Lua: Unknown argument identifier" << "\"" << types[i] << "\"" << std::endl;
                throw std::runtime_error(ssErr.str());
            }
        }
    }
    return args;
}

int LangLua::MakePublic(lua_State *lua)
{
    const char * callback = sol::stack::get<const char*>(lua, 1);
    const char * name = sol::stack::get<const char*>(lua, 2);
    char ret_type = sol::stack::get<char>(lua, 3);
    const char * def = sol::stack::get<const char*>(lua, 4);

    Public::MakePublic(callback, lua, name, ret_type, def);
    return 0;

}

int LangLua::CallPublic(lua_State *lua)
{
    const char * name = sol::stack::get<const char*>(lua, 1);

    int args_n = lua_gettop(lua) - 1;

    std::string types = Public::GetDefinition(name);

    if (args_n  != (long)types.size())
        throw std::invalid_argument("Script call: Number of arguments does not match definition");

    std::vector<boost::any> args = DefToVec(lua, types, 2, args_n);

    boost::any result = Public::Call(&name[0], args);
    if (result.empty())
        return 0;

    if (result.type().hash_code() == typeid(signed int).hash_code())
        sol::stack::push(lua, boost::any_cast<signed int>(result));
    else if (result.type().hash_code() == typeid(unsigned int).hash_code())
        sol::stack::push(lua, boost::any_cast<unsigned int>(result));
    else if (result.type().hash_code() == typeid(double).hash_code())
        sol::stack::push(lua, boost::any_cast<double>(result));
    else if (result.type().hash_code() == typeid(const char*).hash_code())
        sol::stack::push(lua, boost::any_cast<const char*>(result));
    return 1;
}

int LangLua::CreateTimer(lua_State *lua)
{

    const char * callback= sol::stack::get<const char*>(lua, 1);
    int msec = sol::stack::get<int>(lua, 2);

    int id = mwmp::TimerAPI::CreateTimerLua(lua, callback, msec, "", std::vector<boost::any>());
    sol::stack::push(lua, id);
    return 1;
}

int LangLua::CreateTimerEx(lua_State *lua)
{
    const char * callback = sol::stack::get<const char*>(lua, 1);
    int msec = sol::stack::get<int>(lua, 2);

    const char * types = sol::stack::get<const char*>(lua, 3);

    int args_n = (int)lua_strlen(lua, 3);

    std::vector<boost::any> args;

    for (int i = 4; i < args_n + 4; i++)
    {
        switch (types[i - 4])
        {
            case 'i':
            {
                args.emplace_back(sol::stack::get<unsigned int>(lua, i));
                break;
            }

            case 'q':
            {
                args.emplace_back(sol::stack::get<signed int>(lua, i));
                break;
            }

                /*case 'l':
                {
                    args.emplace_back(sol::stack::get<unsigned long long>(lua, i));
                    break;
                }

                case 'w':
                {
                    args.emplace_back(sol::stack::get<signed long long>(lua, i));
                    break;
                }*/

            case 'f':
            {
                args.emplace_back(sol::stack::get<double>(lua, i));
                break;
            }

            case 's':
            {
                args.emplace_back(sol::stack::get<const char*>(lua, i));
                break;
            }

            default:
            {
                std::stringstream ssErr;
                ssErr << "Lua: Unknown argument identifier" << "\"" << types[i] << "\"" << std::endl;
                throw std::runtime_error(ssErr.str());
            }
        }
    }


    int id = mwmp::TimerAPI::CreateTimerLua(lua, callback, msec, types, args);
    sol::stack::push(lua, id);
    return 1;
}
