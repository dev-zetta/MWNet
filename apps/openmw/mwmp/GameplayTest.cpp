#include "GameplayTest.hpp"

#include "LocalPlayer.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "ObjectList.hpp"
#include "PlayerList.hpp"

#include "../mwbase/dialoguemanager.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwgui/dialogue.hpp"
#include "../mwgui/charactercreation.hpp"
#include "../mwgui/mode.hpp"
#include "../mwmechanics/activespells.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/drawstate.hpp"
#include "../mwmechanics/magiceffects.hpp"
#include "../mwmechanics/movement.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwworld/cell.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/player.hpp"

#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm/attr.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Version.hpp>
#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>
#include <components/version/version.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace mwmp
{
    namespace
    {
        using Tree = boost::property_tree::ptree;
        using Clock = std::chrono::steady_clock;
        bool sRequested = false;
        std::vector<std::string> sSecrets;

        std::string quote(const std::string& value)
        {
            std::ostringstream out;
            out << '"';
            for (unsigned char c : value)
            {
                if (c == '"' || c == '\\') out << '\\' << c;
                else if (c < 32) out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << unsigned(c) << std::dec;
                else out << c;
            }
            out << '"';
            return out.str();
        }

        std::string readPrivateFile(const std::filesystem::path& path, std::uintmax_t limit)
        {
#ifndef _WIN32
            const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
            if (fd < 0) throw std::runtime_error("Cannot open private gameplay test input.");
            struct stat info{};
            if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != ::getuid()
                || (info.st_mode & 0777) != 0600 || info.st_size < 0 || static_cast<std::uintmax_t>(info.st_size) > limit)
            {
                ::close(fd);
                throw std::runtime_error("Gameplay test input must be owned by the current user with mode 0600.");
            }
            std::string value;
            char buffer[4096];
            while (true)
            {
                const auto count = ::read(fd, buffer, std::min<std::uintmax_t>(sizeof(buffer), limit + 1 - value.size()));
                if (count == 0) break;
                if (count < 0 || value.size() + static_cast<std::size_t>(count) > limit)
                {
                    ::close(fd);
                    throw std::runtime_error("Private gameplay test input changed or could not be read.");
                }
                value.append(buffer, static_cast<std::size_t>(count));
            }
            ::close(fd);
            return value;
#else
            const auto status = std::filesystem::symlink_status(path);
            if (!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)
                || std::filesystem::file_size(path) > limit)
                throw std::runtime_error("Gameplay test input must be a bounded regular file.");
            std::ifstream input(path, std::ios::binary);
            std::string value(limit + 1, '\0');
            input.read(value.data(), static_cast<std::streamsize>(value.size()));
            value.resize(static_cast<std::size_t>(input.gcount()));
            if ((!input && !input.eof()) || value.size() > limit) throw std::runtime_error("Cannot read bounded gameplay test input.");
            return value;
#endif
        }

        std::string readPassword(const std::string& path, bool optional)
        {
            if (optional && path.empty()) return {};
            std::string value = readPrivateFile(path, 130);
            if (!value.empty() && value.back() == '\n') value.pop_back();
            if (!value.empty() && value.back() == '\r') value.pop_back();
            if (value.empty() || value.size() > 128 || value.find_first_of("\r\n\0", 0, 3) != std::string::npos)
                throw std::runtime_error("Invalid gameplay test credential file.");
            sSecrets.push_back(value);
            return value;
        }

        class PrivateOutputBuffer final : public std::streambuf
        {
            std::FILE* mFile = nullptr;

        public:
            ~PrivateOutputBuffer() override
            {
                if (mFile) std::fclose(mFile);
            }

            void open(const std::filesystem::path& path)
            {
#ifndef _WIN32
                const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
                if (fd < 0) throw std::runtime_error("Cannot exclusively create private gameplay test result.");
                mFile = ::fdopen(fd, "wb");
                if (!mFile) ::close(fd);
#else
                mFile = ::_wfopen(path.c_str(), L"wbx");
#endif
                if (!mFile) throw std::runtime_error("Cannot open private gameplay test result.");
            }

        protected:
            std::streamsize xsputn(const char* text, std::streamsize count) override
            {
                return mFile ? static_cast<std::streamsize>(std::fwrite(text, 1, static_cast<std::size_t>(count), mFile)) : 0;
            }

            int_type overflow(int_type ch) override
            {
                if (traits_type::eq_int_type(ch, traits_type::eof())) return traits_type::not_eof(ch);
                return mFile && std::fputc(traits_type::to_char_type(ch), mFile) != EOF ? ch : traits_type::eof();
            }

            int sync() override { return mFile ? std::fflush(mFile) : -1; }
        };

        struct Driver
        {
            Tree config;
            std::vector<Tree> commands;
            PrivateOutputBuffer sink;
            std::ostream output{ &sink };
            Clock::time_point started = Clock::now();
            Clock::time_point stepStarted = started;
            std::size_t index = 0;
            bool stepEntered = false;
            bool dialogueChoiceSelected = false;
            bool complete = false;
            bool connectedOnce = false;
            osg::Vec3f origin;
            std::string originalCell;
            std::string secret;
            std::string accountSecret;

            bool blockingGui() const
            {
                const auto windows = MWBase::Environment::get().getWindowManager();
                return windows->isGuiMode() || windows->isCharacterCreationVisible()
                    || windows->isConsoleMode() || windows->isSettingsWindowVisible()
                    || windows->isInteractiveMessageBoxActive();
            }

            bool gameplayInputAvailable(const MWWorld::Ptr& player) const
            {
                const auto& env = MWBase::Environment::get();
                return !blockingGui() && !env.getInputManager()->controlsDisabled()
                    && env.getInputManager()->getControlSwitch("playercontrols")
                    && Main::get().getLocalPlayer()->charGenState.isFinished
                    && !player.getClass().getCreatureStats(player).isDead();
            }

            const char* uiModeName() const
            {
                const auto windows = MWBase::Environment::get().getWindowManager();
                if (windows->isConsoleMode()) return "console";
                if (windows->isSettingsWindowVisible()) return "settings";
                if (windows->isInteractiveMessageBoxActive()) return "message_box";
                switch (windows->getMode())
                {
                    case MWGui::GM_None: return "none";
                    case MWGui::GM_Inventory: return "inventory";
                    case MWGui::GM_Container: return "container";
                    case MWGui::GM_Dialogue: return "dialogue";
                    case MWGui::GM_Barter: return "barter";
                    case MWGui::GM_MainMenu: return "main_menu";
                    case MWGui::GM_Loading: return "loading";
                    default: return "other";
                }
            }

            void emit(const std::string& event, const std::string& status, const std::string& detail = {})
            {
                output << "{\"event\":" << quote(event) << ",\"status\":" << quote(status)
                       << ",\"index\":" << index << ",\"elapsedSeconds\":"
                       << std::chrono::duration<double>(Clock::now() - started).count();
                if (index < commands.size()) output << ",\"action\":" << quote(commands[index].get<std::string>("action"));
                if (event == "configured" || event == "finish")
                    output << ",\"version\":" << quote(MWNET_VERSION)
                           << ",\"protocol\":" << MWNET_PROTO_VERSION
                           << ",\"buildCommit\":" << quote(std::string(Version::getCommitHash()));
                if (!detail.empty()) output << ",\"detail\":" << quote(detail);
                output << "}\n";
                output.flush();
                if (!output) throw std::runtime_error("Gameplay test receipt write failed.");
            }

            void controlsOff()
            {
                auto& env = MWBase::Environment::get();
                if (env.getStateManager()->getState() == MWBase::StateManager::State_NoGame) return;
                const MWWorld::Ptr ptr = env.getWorld()->getPlayerPtr();
                if (ptr.isEmpty()) return;
                auto& movement = ptr.getClass().getMovementSettings(ptr);
                std::fill(std::begin(movement.mPosition), std::end(movement.mPosition), 0.f);
                std::fill(std::begin(movement.mRotation), std::end(movement.mRotation), 0.f);
                ptr.getClass().getCreatureStats(ptr).setAttackingOrSpell(false);
                if (auto* controls = env.getLuaManager()->getActorControls(ptr))
                {
                    controls->mChanged = true;
                    controls->mMovement = controls->mSideMovement = controls->mPitchChange = controls->mYawChange = 0;
                    controls->mJump = controls->mRun = controls->mSneak = false;
                    controls->mUse = MWMechanics::AttackType::NoAttack;
                }
            }

            void finish(bool success, const std::string& reason) noexcept
            {
                if (complete) return;
                complete = true;
                try { controlsOff(); } catch (...) { success = false; }
                if (!success && Main::isInitialized() && Main::isPostInitDone())
                {
                    try
                    {
                        if (MWBase::Environment::get().getStateManager()->getState() != MWBase::StateManager::State_NoGame)
                        {
                            Tree command = index < commands.size() ? commands[index] : Tree{};
                            command.put("label", "failure-current-command");
                            try { snapshot(command, true); }
                            catch (...)
                            {
                                Tree fallback;
                                fallback.put("label", "failure-player-state-target-unavailable");
                                try { snapshot(fallback, true); } catch (...) {}
                            }
                        }
                    }
                    catch (...) {}
                }
                try { emit("finish", success ? "pass" : "fail", reason); } catch (...) { success = false; }
                try
                {
                    LOG_MESSAGE_SIMPLE(success ? TimedLog::LOG_INFO : TimedLog::LOG_ERROR,
                        "Gameplay automation %s: %s", success ? "passed" : "failed", reason.c_str());
                }
                catch (...) {}
                // A full disk or broken receipt must never leave a test client
                // playing unattended. The runner also requires a terminal pass.
                try { MWBase::Environment::get().getStateManager()->requestQuit(); } catch (...) {}
            }

            MWWorld::Ptr target(const Tree& command, const MWWorld::Ptr& player)
            {
                if (const auto refNum = command.get_optional<int>("refNum"))
                {
                    const auto object = MWBase::Environment::get().getWorld()->searchPtrViaUniqueIndex(
                        *refNum, command.get<int>("mpNum", 0));
                    if (object.isEmpty()) throw std::runtime_error("Gameplay test target is not loaded.");
                    return object;
                }
                const std::string id = command.get<std::string>("targetRefId", command.get<std::string>("refId", ""));
                if (id.empty()) return player;
                return MWBase::Environment::get().getWorld()->getPtr(ESM::RefId::stringRefId(id), true);
            }

            void snapshot(const Tree& command, bool failure = false)
            {
                auto world = MWBase::Environment::get().getWorld();
                const auto player = world->getPlayerPtr();
                const auto ptr = target(command, player);
                const auto* stats = ptr.getClass().isActor() ? &ptr.getClass().getCreatureStats(ptr) : nullptr;
                const auto& pos = ptr.getRefData().getPosition();
                output << "{\"event\":" << quote(failure ? "failure_snapshot" : "snapshot")
                       << ",\"status\":" << quote(failure ? "diagnostic" : "pass") << ",\"index\":" << index
                       << ",\"label\":" << quote(command.get<std::string>("label", ""))
                       << ",\"targetRefId\":" << quote(ptr.getCellRef().getRefId().serializeText())
                       << ",\"connected\":" << (Main::isConnected() ? "true" : "false")
                       << ",\"uiMode\":" << MWBase::Environment::get().getWindowManager()->getMode()
                       << ",\"uiModeName\":" << quote(uiModeName())
                       << ",\"charGenVisible\":" << (MWBase::Environment::get().getWindowManager()->isCharacterCreationVisible() ? "true" : "false")
                       << ",\"blockingGui\":" << (blockingGui() ? "true" : "false")
                       << ",\"gameplayInputAvailable\":" << (gameplayInputAvailable(player) ? "true" : "false")
                       << ",\"cell\":" << quote(ptr.getCell()->getCell()->getEsm3().getShortDescription())
                       << ",\"position\":[" << pos.pos[0] << ',' << pos.pos[1] << ',' << pos.pos[2] << ']'
                       << ",\"rotation\":[" << pos.rot[0] << ',' << pos.rot[1] << ',' << pos.rot[2] << ']'
                       << ",\"health\":" << (stats ? std::to_string(stats->getHealth().getCurrent()) : "null")
                       << ",\"healthBase\":" << (stats ? std::to_string(stats->getHealth().getBase()) : "null")
                       << ",\"fatigue\":" << (stats ? std::to_string(stats->getFatigue().getCurrent()) : "null")
                       << ",\"fatigueBase\":" << (stats ? std::to_string(stats->getFatigue().getBase()) : "null")
                       << ",\"magicka\":" << (stats ? std::to_string(stats->getMagicka().getCurrent()) : "null")
                       << ",\"magickaBase\":" << (stats ? std::to_string(stats->getMagicka().getBase()) : "null")
                       << ",\"dead\":" << (stats && stats->isDead() ? "true" : "false")
                       << ",\"knockedDown\":" << (stats && stats->getKnockedDown() ? "true" : "false")
                       << ",\"hitRecovery\":" << (stats && stats->getHitRecovery() ? "true" : "false")
                       << ",\"charGenFinished\":" << (Main::get().getLocalPlayer()->charGenState.isFinished ? "true" : "false")
                       << ",\"remotePlayerCount\":" << PlayerList::getPlayersInCell(player.getCell()->getCell()->getEsm3()).size()
                       << ",\"remotePlayerCountScope\":\"currentCell\""
                       << ",\"magicEffects\":[";
                bool first = true;
                if (stats) for (const auto& [effect, value] : stats->getMagicEffects())
                {
                    if (!first) output << ',';
                    first = false;
                    output << "{\"id\":" << quote(effect.mId.serializeText()) << ",\"magnitude\":" << value.getMagnitude() << '}';
                }
                output << "],\"activeSpells\":[";
                first = true;
                if (stats) for (const auto& spell : stats->getActiveSpells())
                {
                    if (!first) output << ',';
                    first = false;
                    output << quote(spell.getSourceSpellId().serializeText());
                }
                output << "],\"attributes\":[";
                if (stats) for (int index = 0; index < 8; ++index)
                {
                    const auto id = ESM::Attribute::indexToRefId(index);
                    const auto& attribute = stats->getAttribute(id);
                    if (index) output << ',';
                    output << "{\"id\":" << quote(id.serializeText()) << ",\"base\":" << attribute.getBase()
                           << ",\"modifier\":" << attribute.getModifier() << ",\"damage\":" << attribute.getDamage() << '}';
                }
                output << "],\"skills\":[";
                if (ptr.getClass().isNpc()) for (int index = 0; index < 27; ++index)
                {
                    const auto id = ESM::Skill::indexToRefId(index);
                    const auto& skill = ptr.getClass().getNpcStats(ptr).getSkill(id);
                    if (index) output << ',';
                    output << "{\"id\":" << quote(id.serializeText()) << ",\"base\":" << skill.getBase()
                           << ",\"modifier\":" << skill.getModifier() << ",\"damage\":" << skill.getDamage() << '}';
                }
                output << "],\"dialogueChoices\":[";
                first = true;
                std::size_t choiceCount = 0;
                for (const auto& [text, answer] : MWBase::Environment::get().getDialogueManager()->getChoices())
                {
                    if (++choiceCount > 32) break;
                    if (!first) output << ',';
                    first = false;
                    output << "{\"text\":" << quote(text.substr(0, 256)) << ",\"answer\":" << answer << '}';
                }
                output << "],\"inventory\":[";
                first = true;
                if (ptr.getClass().hasContainerStore(ptr))
                {
                    std::size_t count = 0;
                    for (const auto& item : ptr.getClass().getContainerStore(ptr))
                    {
                        if (++count > 256) break;
                        if (!first) output << ',';
                        first = false;
                        output << "{\"refId\":" << quote(item.getCellRef().getRefId().serializeText())
                               << ",\"count\":" << item.getCellRef().getCount() << '}';
                    }
                }
                output << "]}\n";
                output.flush();
            }

            void characterGeneration()
            {
                if (!config.get<bool>("autoCharGen", true) || !Main::isPostInitDone()) return;
                auto* local = Main::get().getLocalPlayer();
                if (local->charGenState.isFinished) return;
                auto& env = MWBase::Environment::get();
                auto windows = env.getWindowManager();
                if (!windows->isCharacterCreationVisible()) return;
                const int stage = local->charGenState.currentStage;
                MWGui::CharacterCreationSelection selection;
                selection.name = config.get<std::string>("character.name", config.get<std::string>("account"));
                selection.raceId = ESM::RefId::stringRefId(config.get<std::string>("character.race", "imperial"));
                selection.headId = ESM::RefId::stringRefId(config.get<std::string>("character.head", "b_n_imperial_m_head_01"));
                selection.hairId = ESM::RefId::stringRefId(config.get<std::string>("character.hair", "b_n_imperial_m_hair_01"));
                selection.classId = ESM::RefId::stringRefId(config.get<std::string>("character.class", "warrior"));
                selection.birthSignId = ESM::RefId::stringRefId(config.get<std::string>("character.birthsign", "Lady's Favor"));
                emit("character_stage_start", "running", std::to_string(stage));
                const auto mode = windows->getMode();
                if (windows->completeCharacterCreationForAutomation(selection))
                {
                    emit("character_stage", "pass", std::to_string(stage));
                    if (mode == MWGui::GM_Birth)
                    {
                        Tree oracle;
                        oracle.put("label", "character_created_locally");
                        snapshot(oracle);
                    }
                }
            }

            void tick()
            {
                if (complete) return;
                const auto now = Clock::now();
                if (std::chrono::duration<double>(now - started).count() > config.get<double>("timeoutSeconds", 180))
                { finish(false, "scenario deadline exceeded"); return; }
                const bool connected = Main::isConnected();
                if (connectedOnce && !connected) { finish(false, "multiplayer connection lost"); return; }
                connectedOnce |= connected;
                auto& env = MWBase::Environment::get();
                if (env.getStateManager()->getState() == MWBase::StateManager::State_NoGame || !connected) return;
                characterGeneration();
                auto* local = Main::get().getLocalPlayer();
                if (!local->isLoggedIn()) return;
                if (index >= commands.size()) { finish(false, "scenario has no finish command"); return; }
                const auto player = env.getWorld()->getPlayerPtr();
                const auto& cmd = commands[index];
                const auto action = cmd.get<std::string>("action");
                if (!stepEntered)
                {
                    stepEntered = true;
                    dialogueChoiceSelected = false;
                    stepStarted = now;
                    origin = player.getRefData().getPosition().asVec3();
                    originalCell = player.getCell()->getCell()->getEsm3().getShortDescription();
                    emit("step_start", "running");
                }
                const double elapsed = std::chrono::duration<double>(now - stepStarted).count();
                if (elapsed > cmd.get<double>("timeoutSeconds", 30)) { finish(false, "command deadline exceeded"); return; }
                bool done = false;
                if (action == "wait_ready") done = Main::isPostInitDone() && local->isLoggedIn()
                    && local->charGenState.isFinished && !env.getWindowManager()->isCharacterCreationVisible();
                else if (action == "wait") done = elapsed >= cmd.get<double>("seconds", 1);
                else if (action == "snapshot") { snapshot(cmd); done = true; }
                else if (action == "open_inventory") { env.getWindowManager()->pushGuiMode(MWGui::GM_Inventory); done = true; }
                else if (action == "close_gui")
                {
                    const auto windows = env.getWindowManager();
                    const auto mode = windows->getMode();
                    if (mode == MWGui::GM_Inventory || mode == MWGui::GM_Container || mode == MWGui::GM_Dialogue || mode == MWGui::GM_Barter)
                        windows->exitCurrentGuiMode();
                    done = windows->getMode() != mode || !blockingGui();
                }
                else if (action == "chat")
                {
                    local->chatMessage = cmd.get<std::string>("message");
                    if (local->chatMessage.size() > 512) { finish(false, "chat command exceeds limit"); return; }
                    auto* packet = Main::get().getNetworking()->getPlayerPacket(ID_CHAT_MESSAGE);
                    packet->setPlayer(local); packet->Send(); done = true;
                }
                else if (action == "wait_chat") done = local->chatMessage.find(cmd.get<std::string>("text")) != std::string::npos;
                else if (action == "expect_gameplay_input") done = gameplayInputAvailable(player);
                else if (action == "wait_dialogue")
                    done = env.getWindowManager()->containsMode(MWGui::GM_Dialogue) == cmd.get<bool>("present", true);
                else if (action == "resist_arrest")
                {
                    const auto windows = env.getWindowManager();
                    const auto dialogue = env.getDialogueManager();
                    if (!dialogueChoiceSelected)
                    {
                        if (!windows->containsMode(MWGui::GM_Dialogue) || !dialogue->isInChoice()) return;
                        const auto& choices = dialogue->getChoices();
                        const auto choice = std::find_if(choices.begin(), choices.end(), [](const auto& entry)
                        {
                            auto text = entry.first;
                            while (!text.empty() && (text.back() == '.' || text.back() == ' ')) text.pop_back();
                            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
                            return text == "resist arrest";
                        });
                        if (choice == choices.end()) { finish(false, "active dialogue has no Resist Arrest choice"); return; }
                        const int answer = choice->second;
                        const std::string choiceText = choice->first;
                        MWGui::ResponseCallback callback(windows->getDialogueWindow());
                        dialogue->questionAnswered(answer, &callback);
                        callback.updateTopics();
                        dialogueChoiceSelected = true;
                        emit("dialogue_choice", "pass", choiceText);
                    }
                    if (windows->getMode() == MWGui::GM_Dialogue && !dialogue->isInChoice())
                        windows->exitCurrentGuiMode();
                    done = !windows->containsMode(MWGui::GM_Dialogue) && !blockingGui();
                }
                else if (action == "move")
                {
                    if (!gameplayInputAvailable(player)) { controlsOff(); return; }
                    auto& movement = player.getClass().getMovementSettings(player);
                    movement.mPosition[0] = std::clamp(cmd.get<float>("strafe", 0), -1.f, 1.f);
                    movement.mPosition[1] = std::clamp(cmd.get<float>("forward", 1), -1.f, 1.f);
                    movement.mPosition[2] = cmd.get<bool>("jump", false) ? 1.f : 0.f;
                    if (auto* controls = env.getLuaManager()->getActorControls(player))
                    {
                        controls->mChanged = true;
                        controls->mMovement = movement.mPosition[1];
                        controls->mSideMovement = movement.mPosition[0];
                        controls->mJump = cmd.get<bool>("jump", false);
                        controls->mRun = cmd.get<bool>("run", false);
                        controls->mUse = MWMechanics::AttackType::NoAttack;
                    }
                    if (elapsed >= cmd.get<double>("seconds", 1))
                    {
                        const float distance = (player.getRefData().getPosition().asVec3() - origin).length();
                        if (distance < cmd.get<float>("minimumDistance", 1)) { finish(false, "movement distance assertion failed"); return; }
                        controlsOff(); done = true;
                    }
                }
                else if (action == "activate")
                {
                    if (!gameplayInputAvailable(player)) { controlsOff(); return; }
                    const auto object = target(cmd, player);
                    if (object == player || object.getCell() != player.getCell()
                        || (object.getRefData().getPosition().asVec3() - player.getRefData().getPosition().asVec3()).length()
                            > env.getWorld()->getMaxActivationDistance() + 1)
                    { finish(false, "activation target is not in local reach"); return; }
                    auto* objects = Main::get().getNetworking()->getObjectList();
                    objects->reset(); objects->packetOrigin = CLIENT_GAMEPLAY;
                    objects->addObjectActivate(object, player); objects->sendObjectActivate(); done = true;
                }
                else if (action == "attack")
                {
                    if (!gameplayInputAvailable(player) || !env.getInputManager()->getControlSwitch("playerfighting"))
                    {
                        controlsOff();
                        if (cmd.get<bool>("stopOnDialogue", false) && env.getWindowManager()->containsMode(MWGui::GM_Dialogue))
                        {
                            emit("gameplay_input_interrupted", "pass", "dialogue blocked the attack");
                            emit("step", "pass");
                            ++index; stepEntered = false;
                        }
                        return;
                    }
                    env.getWorld()->getPlayer().setDrawState(MWMechanics::DrawState::Weapon);
                    float yaw = 0;
                    if (cmd.get_optional<std::string>("targetRefId") || cmd.get_optional<std::string>("refId") || cmd.get_optional<int>("refNum"))
                    {
                        const auto object = target(cmd, player);
                        const auto delta = object.getRefData().getPosition().asVec3() - player.getRefData().getPosition().asVec3();
                        yaw = std::remainder(std::atan2(delta.x(), delta.y()) - player.getRefData().getPosition().rot[2], 6.283185307179586f);
                        player.getClass().getMovementSettings(player).mRotation[2] = yaw;
                    }
                    const bool held = elapsed < cmd.get<double>("seconds", 1);
                    player.getClass().getCreatureStats(player).setAttackingOrSpell(held);
                    if (auto* controls = env.getLuaManager()->getActorControls(player))
                    {
                        controls->mChanged = true;
                        controls->mMovement = controls->mSideMovement = 0;
                        controls->mPitchChange = 0; controls->mYawChange = yaw;
                        controls->mUse = held ? MWMechanics::AttackType::Any : MWMechanics::AttackType::NoAttack;
                    }
                    if (elapsed >= cmd.get<double>("seconds", 1)) { controlsOff(); done = true; }
                }
                else if (action == "expect_cell") done = player.getCell()->getCell()->getEsm3().getShortDescription() == cmd.get<std::string>("cell");
                else if (action == "wait_dead") done = player.getClass().getCreatureStats(player).isDead();
                else if (action == "wait_alive") done = !player.getClass().getCreatureStats(player).isDead() && player.getClass().getCreatureStats(player).getHealth().getCurrent() > 0;
                else if (action == "expect_remote_players")
                {
                    std::size_t count = 0;
                    const std::string name = cmd.get<std::string>("name", "");
                    for (const auto connection : PlayerList::getPlayersInCell(player.getCell()->getCell()->getEsm3()))
                    {
                        const auto* remote = PlayerList::getPlayer(connection);
                        if (remote && (name.empty() || remote->npc.mName == name)) ++count;
                    }
                    done = count >= cmd.get<std::size_t>("minimumCount", 1)
                        && count <= cmd.get<std::size_t>("maximumCount", 64);
                }
                else if (action == "expect_inventory")
                {
                    const auto object = target(cmd, player);
                    const int count = object.getClass().getContainerStore(object).count(ESM::RefId::stringRefId(cmd.get<std::string>("itemRefId")));
                    done = count >= cmd.get<int>("minimumCount", cmd.get<int>("count", 1)) && count <= cmd.get<int>("maximumCount", cmd.get<int>("count", 2147483647));
                }
                else if (action == "expect_actor")
                {
                    const auto object = target(cmd, player);
                    const auto& stats = object.getClass().getCreatureStats(object);
                    const float health = stats.getHealth().getCurrent();
                    done = health >= cmd.get<float>("minimumHealth", -1e9f) && health <= cmd.get<float>("maximumHealth", 1e9f)
                        && (!cmd.get_optional<bool>("dead") || stats.isDead() == cmd.get<bool>("dead"));
                }
                else if (action == "wait_magic_effect" || action == "expect_magic_effect")
                {
                    const auto& stats = player.getClass().getCreatureStats(player);
                    bool found = false;
                    if (const auto id = cmd.get_optional<std::string>("spellId"))
                    {
                        for (const auto& spell : stats.getActiveSpells()) found |= spell.getSourceSpellId().serializeText() == *id;
                    }
                    else
                        found = stats.getMagicEffects().getOrDefault(ESM::RefId::stringRefId(cmd.get<std::string>("effectId"))).getMagnitude() != 0;
                    done = found == cmd.get<bool>("present", true);
                }
                else if (action == "finish") { snapshot(cmd); finish(true, "all scenario assertions passed"); return; }
                else { finish(false, "unknown command action"); return; }
                if (done)
                {
                    emit("step", "pass");
                    ++index; stepEntered = false;
                }
            }
        };

        std::unique_ptr<Driver> sDriver;
    }

    void GameplayTest::configure(const std::string& path)
    {
        if (path.empty()) return;
        sRequested = true;
        const std::string input = readPrivateFile(path, 131072);
        auto driver = std::make_unique<Driver>();
        std::istringstream json(input);
        try { boost::property_tree::read_json(json, driver->config); }
        catch (...) { throw std::runtime_error("Invalid gameplay test JSON configuration."); }
        const auto& config = driver->config;
        const std::string fingerprint = config.get<std::string>("trustFingerprint", "");
        const std::string account = config.get<std::string>("account", "");
        if (config.get<int>("schemaVersion", 0) != 1 || config.get<std::string>("host", "") != "127.0.0.1"
            || config.get<unsigned int>("port", 0) == 0 || config.get<unsigned int>("port", 0) > 65535
            || account.empty() || account.size() > 64
            || std::any_of(account.begin(), account.end(), [](unsigned char c) { return c < 32 || c == 127; })
            || config.get<std::string>("character.name", account).size() > 31
            || fingerprint.size() != 51 || fingerprint.rfind("ed25519/", 0) != 0
            || !std::all_of(fingerprint.begin() + std::min<std::size_t>(8, fingerprint.size()), fingerprint.end(),
                [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; })
            || !std::isfinite(config.get<double>("timeoutSeconds", 180))
            || config.get<double>("timeoutSeconds", 180) <= 0 || config.get<double>("timeoutSeconds", 180) > 300)
            throw std::runtime_error("Gameplay tests require a bounded loopback scenario and pinned identity.");
        driver->accountSecret = readPassword(config.get<std::string>("accountPasswordFile"), false);
        driver->secret = readPassword(config.get<std::string>("serverPasswordFile", ""), true);
        for (const auto& command : config.get_child("commands"))
        {
            static const std::vector<std::string> actions{
                "wait_ready", "wait", "snapshot", "chat", "wait_chat", "move", "activate", "attack",
                "expect_cell", "wait_dead", "wait_alive", "expect_inventory", "expect_actor",
                "wait_magic_effect", "expect_magic_effect", "open_inventory", "close_gui", "expect_remote_players",
                "expect_gameplay_input", "wait_dialogue", "resist_arrest", "finish"
            };
            if (!command.first.empty() || command.second.get<std::string>("action", "").empty())
                throw std::runtime_error("Invalid gameplay test command.");
            if (std::find(actions.begin(), actions.end(), command.second.get<std::string>("action")) == actions.end())
                throw std::runtime_error("Unknown gameplay test action.");
            const double timeout = command.second.get<double>("timeoutSeconds", 30);
            const double duration = command.second.get<double>("seconds", 1);
            if (!std::isfinite(timeout) || timeout <= 0 || timeout > 60
                || !std::isfinite(duration) || duration < 0 || duration > 60)
                throw std::runtime_error("Gameplay test commands must have bounded finite deadlines.");
            for (const char* key : { "forward", "strafe", "minimumDistance", "minimumHealth", "maximumHealth" })
                if (!std::isfinite(command.second.get<double>(key, 0)))
                    throw std::runtime_error("Gameplay test command has a non-finite value.");
            if (command.second.get<std::string>("action") == "expect_remote_players"
                && (command.second.get<unsigned int>("minimumCount", 1) > 64
                    || command.second.get<unsigned int>("maximumCount", 64) > 64
                    || command.second.get<unsigned int>("minimumCount", 1) > command.second.get<unsigned int>("maximumCount", 64)))
                throw std::runtime_error("Remote-player assertions must be bounded to at most 64 players.");
            driver->commands.push_back(command.second);
        }
        if (driver->commands.empty() || driver->commands.size() > 256
            || driver->commands.back().get<std::string>("action") != "finish")
            throw std::runtime_error("Gameplay test requires bounded commands ending with finish.");
        const std::filesystem::path result(config.get<std::string>("resultFile"));
        if (!result.is_absolute() || std::filesystem::symlink_status(result).type() != std::filesystem::file_type::not_found
            || !std::filesystem::is_directory(result.parent_path()))
            throw std::runtime_error("Gameplay test result must be a new absolute file in an existing private directory.");
#ifndef _WIN32
        struct stat info{};
        if (::lstat(result.parent_path().c_str(), &info) != 0 || !S_ISDIR(info.st_mode)
            || info.st_uid != ::getuid() || (info.st_mode & 077) != 0)
            throw std::runtime_error("Gameplay test result directory must be owned and private.");
#endif
        driver->sink.open(result);
        driver->emit("configured", "pass");
        sDriver = std::move(driver);
    }

    bool GameplayTest::active() { return sDriver && !sDriver->complete; }
    bool GameplayTest::configured() { return static_cast<bool>(sDriver); }
    bool GameplayTest::requested() { return sRequested; }
    std::string GameplayTest::diagnostic(std::string message)
    {
        for (const auto& secret : sSecrets)
        {
            std::size_t pos = 0;
            while ((pos = message.find(secret, pos)) != std::string::npos)
            {
                message.replace(pos, secret.size(), "[redacted]");
                pos += 10;
            }
        }
        if (message.size() > 512) message.resize(512);
        return message;
    }
    std::string GameplayTest::address() { return "127.0.0.1:" + sDriver->config.get<std::string>("port"); }
    std::string GameplayTest::account() { return sDriver->config.get<std::string>("account"); }
    std::string GameplayTest::accountPasswordFile() { return sDriver->config.get<std::string>("accountPasswordFile"); }
    std::string GameplayTest::accountPassword() { return sDriver->accountSecret; }
    std::string GameplayTest::serverPassword() { return sDriver->secret; }
    std::string GameplayTest::fingerprint() { return sDriver->config.get<std::string>("trustFingerprint"); }
    bool GameplayTest::registerAccount() { return sDriver->config.get<bool>("registerAccount", false); }
    void GameplayTest::connectionFailed()
    {
        // Initial networking precedes prepareEngine(), so no world/state
        // managers exist here yet. Engine::go() returns directly on failure.
        if (active())
        {
            sDriver->complete = true;
            try { sDriver->emit("finish", "fail", "initial multiplayer connection failed"); } catch (...) {}
        }
    }
    void GameplayTest::frame(float)
    {
        if (!active()) return;
        try { sDriver->tick(); }
        catch (const std::exception& error)
        {
            try { sDriver->finish(false, diagnostic(std::string("engine exception: ") + error.what())); }
            catch (...) { sDriver->finish(false, "gameplay command raised an engine exception"); }
        }
        catch (...) { sDriver->finish(false, "gameplay command raised an engine exception"); }
    }
}
