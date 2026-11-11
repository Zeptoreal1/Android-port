// Voidius (Android port, independent reimplementation).
// Engine: detects real taps, takes over with an even rhythm, hands control back.
// All timing is in game "ticks" counted in GJBaseGameLayer::processCommands
// (original mod hooked the same function: 240 ticks/s). NOT verified on device.
#pragma once
#include <Geode/Geode.hpp>
#include <array>
#include <cmath>
#include <deque>
#include <string>

namespace vd {
using namespace geode::prelude;

enum Mode { Cube, Ship, Ball, Ufo, Wave, Robot, Spider, Swing, ModeCount };
inline constexpr char const* kModeName[ModeCount] = {"cube","ship","ball","ufo","wave","robot","spider","swing"};
inline constexpr char const* kModeKey[ModeCount]  = {"mode-cube","mode-ship","mode-ball","mode-ufo","mode-wave","mode-robot","mode-spider","mode-swing"};
inline constexpr char const* kCpsKey[ModeCount]   = {"target-cps","cps-ship","target-cps","cps-ufo","cps-wave","target-cps","target-cps","cps-swing"};
inline constexpr double kTps = 240.0;

struct Config {
    bool enabled = true, quick = false, straight = true, match = false, showCps = true;
    int trigger = 8, hold = 50;
    std::array<int, ModeCount> cps{};
    std::array<bool, ModeCount> on{};
};

struct Lane {
    bool realHeld = false, synth = false, active = false;
    std::deque<uint64_t> presses;      // real presses only (never synthetic)
    double period = 0, nextPress = 0;
    uint64_t releaseAt = 0, startTick = 0, lastPress = 0;
    int clicks = 0;
    float targetY = 0, lastCycleY = 0;
    Mode mode = Cube;
};

class Engine {
public:
    Config cfg;
    std::array<Lane, 2> lanes;
    std::deque<std::string> log;
    uint64_t tick = 0;
    bool synthFlag = false;   // true only while WE call handleButton

    void refresh() {
        auto* m = Mod::get();
        cfg.enabled  = m->getSettingValue<bool>("enabled");
        cfg.quick    = m->getSettingValue<bool>("quick-start");
        cfg.straight = m->getSettingValue<bool>("go-straight");
        cfg.match    = m->getSettingValue<std::string>("rhythm") != "fixed";
        cfg.showCps  = m->getSettingValue<bool>("show-cps");
        cfg.trigger  = (int)m->getSettingValue<int64_t>("trigger-cps");
        cfg.hold     = (int)m->getSettingValue<int64_t>("hold-percent");
        for (int i = 0; i < ModeCount; i++) {
            cfg.cps[i] = (int)m->getSettingValue<int64_t>(kCpsKey[i]);
            cfg.on[i]  = m->getSettingValue<bool>(kModeKey[i]);
        }
    }

    static Mode modeOf(PlayerObject* p) {
        if (p->m_isShip) return Ship;
        if (p->m_isBird) return Ufo;
        if (p->m_isDart) return Wave;
        if (p->m_isSwing) return Swing;
        if (p->m_isBall) return Ball;
        if (p->m_isRobot) return Robot;
        if (p->m_isSpider) return Spider;
        return Cube;
    }
    static PlayerObject* playerOf(GJBaseGameLayer* gl, bool p1) { return p1 ? gl->m_player1 : gl->m_player2; }

    void say(std::string s) { log.push_back(std::move(s)); if (log.size() > 40) log.pop_front(); }
    void clearLog() { log.clear(); }

    // ---- hook entry points -------------------------------------------------
    // returns true if the original handleButton must NOT run
    bool intercept(GJBaseGameLayer* gl, bool down, int button, bool p1) {
        if (synthFlag || button != 1 || !cfg.enabled) return false;   // synthetic input is never counted
        if (gl != PlayLayer::get()) return false;
        Lane& L = lanes[p1 ? 0 : 1];
        if (down) {
            L.realHeld = true;
            L.presses.push_back(tick); if (L.presses.size() > 64) L.presses.pop_front();
            L.lastPress = tick;
            if (L.active) {
                if (cfg.match) L.period = std::clamp(manualInterval(L), kTps / 60.0, kTps / 2.0);
                return true;                                  // swallow: assist owns the button
            }
            auto* pl = playerOf(gl, p1);
            if (pl && cfg.on[modeOf(pl)] && triggered(L)) { activate(gl, L, p1, pl); return true; }
            return false;
        }
        L.realHeld = false;
        return L.active;                                      // swallow releases while assisting
    }

    void step(GJBaseGameLayer* gl) {
        if (gl != PlayLayer::get()) return;
        ++tick;
        if ((tick & 63) == 0) refresh();
        for (int i = 0; i < 2; i++) {
            Lane& L = lanes[i];
            if (!L.active) continue;
            bool p1 = i == 0;
            auto* pl = playerOf(gl, p1);
            if (!cfg.enabled) { releaseLane(gl, L, p1); continue; }
            if (!pl) { handBack(gl, L, p1, "player gone"); continue; }
            Mode m = modeOf(pl);
            if (!cfg.on[m]) { handBack(gl, L, p1, "mode off"); continue; }
            if (m != L.mode) { L.mode = m; L.targetY = pl->getPositionY(); L.lastCycleY = L.targetY; if (!cfg.match) L.period = kTps / std::max(2, cfg.cps[m]); }
            if ((double)(tick - L.lastPress) > 1.5 * manualInterval(L)) { handBack(gl, L, p1, "stopped"); continue; }

            if ((double)tick >= std::ceil(L.nextPress)) {
                if (L.synth) send(gl, L, p1, false);
                bool skip = false; double duty = cfg.hold / 100.0;
                if (cfg.straight && (m == Ship || m == Wave || m == Ufo || m == Swing)) {
                    float y = pl->getPositionY(), s = pl->m_isUpsideDown ? -1.f : 1.f;
                    float err = s * (L.targetY - y) / 30.f, dy = s * (y - L.lastCycleY) / 30.f;
                    L.lastCycleY = y;
                    if (m == Ship) duty = std::clamp(0.5 + 0.15 * err - 0.30 * dy, 0.1, 0.9);   // UNTUNED gains
                    else if (m == Wave) duty = std::clamp(0.5 + 0.15 * err - 0.25 * dy, 0.1, 0.9); // UNTUNED gains
                    else { duty = 0.5; if (m == Ufo && err < -1.f) skip = true; }  // ufo: skip a click when >1 block above target; swing: even alternation, no correction
                }
                if (!skip) {
                    send(gl, L, p1, true); ++L.clicks;
                    double hold = std::clamp(std::round(duty * L.period), 1.0, std::max(1.0, L.period - 1.0));
                    L.releaseAt = tick + (uint64_t)hold;
                }
                L.nextPress += L.period;
                if (L.nextPress < (double)tick) L.nextPress = (double)tick + L.period;
            } else if (L.synth && tick >= L.releaseAt) send(gl, L, p1, false);
        }
    }

    // pause / death / reset / quit / disable: never leave a synthetic hold behind
    void releaseAll(GJBaseGameLayer* gl) {
        for (int i = 0; i < 2; i++) { releaseLane(gl, lanes[i], i == 0); lanes[i].realHeld = false; lanes[i].presses.clear(); }
    }

    // ---- HUD / panel readouts ---------------------------------------------
    int realCps() const {
        int n = 0; for (auto const& L : lanes) for (auto p : L.presses) if (tick - p <= 240) ++n; return n;
    }
    bool anyActive() const { return lanes[0].active || lanes[1].active; }
    double assistCps() const { auto const& L = lanes[0].active ? lanes[0] : lanes[1]; return L.period > 0 ? kTps / L.period : 0; }

private:
    double manualInterval(Lane const& L) const {
        size_t n = L.presses.size();
        if (n < 2) return kTps / std::max(3, cfg.trigger);
        size_t k = std::min<size_t>(n, 5);
        return (double)(L.presses.back() - L.presses[n - k]) / (double)(k - 1);
    }
    bool triggered(Lane const& L) const {
        size_t need = cfg.quick ? 2 : 3, n = L.presses.size();
        if (n < need) return false;
        double maxIvl = kTps / cfg.trigger;
        for (size_t i = n - need + 1; i < n; i++)
            if ((double)(L.presses[i] - L.presses[i - 1]) > maxIvl) return false;
        return true;
    }
    void send(GJBaseGameLayer* gl, Lane& L, bool p1, bool down) {
        if (gl) { synthFlag = true; gl->handleButton(down, 1, p1); synthFlag = false; }
        L.synth = down;
    }
    void releaseLane(GJBaseGameLayer* gl, Lane& L, bool p1) {
        if (L.synth) send(gl, L, p1, false);
        L.active = false;
    }
    void activate(GJBaseGameLayer* gl, Lane& L, bool p1, PlayerObject* pl) {
        L.mode = modeOf(pl);
        L.period = cfg.match ? std::clamp(manualInterval(L), kTps / 60.0, kTps / 2.0) : kTps / std::max(2, cfg.cps[L.mode]);
        L.active = true; L.startTick = tick; L.clicks = 1;
        L.targetY = L.lastCycleY = pl->getPositionY();
        L.nextPress = (double)tick + L.period;
        send(gl, L, p1, true);                       // the activating click is the first assisted click
        L.releaseAt = tick + (uint64_t)std::clamp(std::round(cfg.hold / 100.0 * L.period), 1.0, std::max(1.0, L.period - 1.0));
        say(std::string("takeover · ") + kModeName[L.mode] + " · " + std::to_string((int)std::round(kTps / L.period)) + " cps");
    }
    void handBack(GJBaseGameLayer* gl, Lane& L, bool p1, char const* why) {
        L.active = false;
        // only hold-type modes re-sync a held button; elsewhere a press would cause an unwanted jump/flip
        bool wantDown = L.realHeld && (L.mode == Ship || L.mode == Wave);
        if (L.synth && !wantDown) send(gl, L, p1, false);
        else if (!L.synth && wantDown) send(gl, L, p1, true);
        say(std::string("hand-back · ") + why + " · " + std::to_string(L.clicks) + " clicks · " +
            std::to_string((tick - L.startTick) / 240.0).substr(0, 4) + "s");
    }
};

inline Engine& E() { static Engine e; return e; }
}
