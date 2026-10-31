#pragma once
#include <chrono>
#include <string>

namespace opennow::autologin {
enum class Game { Nte, Genshin, Honkai, Wuwa, Zzz, Epic };
struct Point { float x, y; };
struct Profile {
    Game game;
    const char* title;
    const char* folder;
    bool email_only;
    Point login, password, submit;
};
const Profile* FindProfile(const std::string& title);
const Profile& GetProfile(Game game);
struct Credentials {
    std::string login, password;
    void Wipe();
};
bool Valid(const Profile&, const Credentials&);
Credentials Parse(const Profile&, const std::string& text);
std::string Serialize(const Profile&, const Credentials&);
inline constexpr const char* kHome = "sdmc:/switch/SwitchNOW";
std::string Directory(const Profile&, const std::string& home = kHome);
std::string CredentialsPath(const Profile&, const std::string& home = kHome);
Credentials Load(const Profile&, const std::string& home = kHome);
bool Save(const Profile&, const Credentials&, const std::string& home = kHome);
bool Clear(const Profile&, const std::string& home = kHome);

enum class ActionKind { None, Click, ClearField, Character, Submitted, Aborted };
struct Action {
    ActionKind kind = ActionKind::None;
    Point point{};
    char character = 0;
    const char* stage = "idle"; // Fixed labels only, never credential content.
};
// One action per UI pass. No sleep, frame inspection, network or file I/O here.
class Flow {
public:
    using Clock = std::chrono::steady_clock;
    Flow() = default;
    Flow(const Flow&) = delete;
    Flow& operator=(const Flow&) = delete;
    ~Flow();
    bool Start(const Profile&, Credentials, Clock::time_point);
    Action Tick(Clock::time_point, bool input_ready);
    void Cancel();
    bool Active() const { return profile_ != nullptr; }
private:
    const Profile* profile_ = nullptr;
    Credentials credentials_;
    unsigned step_ = 0;
    size_t index_ = 0;
    Clock::time_point due_{}, deadline_{};
};
} // namespace opennow::autologin
