// Touch control panel. Self-contained: one high-priority swallowing touch layer + manual hit-testing,
// so it cannot leak taps into gameplay and does not depend on Popup/ScrollLayer API versions.
#include "Panel.hpp"
#include "Engine.hpp"
#include <functional>
using namespace geode::prelude;

namespace {
constexpr float W = 400, H = 250, VIEW_H = 164, VIEW_W = 376;
struct Theme { char const* n; char const* p; char const* s; char const* f; char const* t; };
// Theme NAMES recovered from the original binary; colour values are an independent approximation.
Theme const kThemes[15] = {
 {"violet","A855F7","D5B4FF","17131F","EFE7F7"},{"ocean","3B82F6","9CC8FF","0F1722","E6F0FF"},
 {"mint","10B981","A7F3D0","0E1A17","E6FFF5"},{"amber","F59E0B","FCD34D","1C160B","FFF4DB"},
 {"rose","F43F5E","FDA4AF","1E1015","FFE8EC"},{"mono","E5E5E5","A3A3A3","121212","F5F5F5"},
 {"ice","67E8F9","CFFAFE","0E1A1E","ECFEFF"},{"sunset","FB7185","FDBA74","1F1315","FFEDE5"},
 {"grape","8B5CF6","C4B5FD","17122B","EEE9FF"},{"lime","84CC16","D9F99D","121A0A","F3FFE0"},
 {"crimson","DC2626","FCA5A5","1A0D0D","FFE9E9"},{"slate","64748B","CBD5E1","10151C","E2E8F0"},
 {"dark","A855F7","D5B4FF","0B0B0F","EFE7F7"},{"white","6D28D9","8B5CF6","FFFFFF","1F1B2E"},
 {"graphene","22D3EE","94A3B8","0D1117","E6EDF3"}};
char const* kTour[][2] = {
 {"hey! quick tour","spam fast enough and the assist\ntakes over, clicking at a steady\nspeed for you. here's how."},
 {"on / off","tap the pill at the top (or the\nVoidius Assist toggle in the pause\nmenu) to turn the assist on or off."},
 {"four pages","live, speed, modes and look.\ntap one to switch pages."},
 {"live readout","your cps while you play. it turns\ngreen and shows the assist's\nspeed when it's on."},
 {"trigger cps","spam at least this fast and it\ntakes over. stop spamming and\nyou get control back."},
 {"go straight","ship, wave, ufo and swing hold\nthe height you started at."},
 {"modes","tap a gamemode to let the assist\nhelp there. ship, wave, ufo and\nswing are on by default."},
 {"you're set","open this from the pause menu,\nthen spam a ship part to try it."}};
constexpr int kTourN = 8;

ccColor3B hex(char const* h) { unsigned v = std::strtoul(h, nullptr, 16); return {(GLubyte)(v >> 16), (GLubyte)(v >> 8), (GLubyte)v}; }
ccColor4F c4(ccColor3B c, float a) { return {c.r / 255.f, c.g / 255.f, c.b / 255.f, a}; }
ccColor3B colSet(char const* id) { return Mod::get()->getSettingValue<ccColor3B>(id); }
bool getB(char const* id) { return Mod::get()->getSettingValue<bool>(id); }
int64_t getI(char const* id) { return Mod::get()->getSettingValue<int64_t>(id); }
void setB(char const* id, bool v) { Mod::get()->setSettingValue<bool>(id, v); vd::E().refresh(); }
void setI(char const* id, int64_t v) { Mod::get()->setSettingValue<int64_t>(id, v); vd::E().refresh(); }

void rrect(CCDrawNode* d, float x, float y, float w, float h, float r, ccColor4F f) {
    std::vector<CCPoint> p; r = std::min(r, std::min(w, h) / 2);
    auto arc = [&](float cx, float cy, float a0) { for (int i = 0; i <= 5; i++) { float a = (a0 + i * 18) * 3.14159265f / 180; p.push_back({cx + r * cosf(a), cy + r * sinf(a)}); } };
    arc(x + w - r, y + r, -90); arc(x + w - r, y + h - r, 0); arc(x + r, y + h - r, 90); arc(x + r, y + r, 180);
    d->drawPolygon(p.data(), p.size(), f, 0, {0, 0, 0, 0});
}

struct Scissor : CCNode {
    CCSize sz;
    static Scissor* create(CCSize s) { auto n = new Scissor(); n->sz = s; n->setContentSize(s); n->autorelease(); return n; }
    void visit() override {
        auto bl = convertToWorldSpace({0, 0}), tr = convertToWorldSpace({sz.width, sz.height});
        glEnable(GL_SCISSOR_TEST);
        CCEGLView::get()->setScissorInPoints(bl.x, bl.y, tr.x - bl.x, tr.y - bl.y);
        CCNode::visit();
        glDisable(GL_SCISSOR_TEST);
    }
};

struct Hit { CCRect r; std::function<void()> fn; };
enum Page { LIVE, SPEED, MODES, LOOK };
}

class VdPanel : public CCLayer {
    CCNode* m_root = nullptr; CCLayerColor* m_dim = nullptr; Scissor* m_view = nullptr; CCNode* m_content = nullptr;
    CCNode* m_chrome = nullptr; CCNode* m_tut = nullptr;
    std::vector<Hit> m_hits, m_chits, m_thits;
    int m_page = LIVE, m_tutStep = -1; float m_scroll = 0, m_contentH = 0, m_ts = 1, m_fit = 1;
    CCPoint m_start, m_last; bool m_drag = false, m_closing = false;
    CCLabelBMFont *m_lCps = nullptr, *m_lState = nullptr, *m_lMode = nullptr, *m_lTarget = nullptr, *m_lLog = nullptr;
    ccColor3B m_pri, m_sec, m_sur, m_txt;

public:
    static VdPanel* create() { auto p = new VdPanel(); if (p && p->init()) { p->autorelease(); return p; } delete p; return nullptr; }

    bool init() override {
        if (!CCLayer::init()) return false;
        this->setTouchEnabled(true); this->setKeypadEnabled(true);
        auto win = CCDirector::get()->getWinSize();
        m_dim = CCLayerColor::create({0, 0, 0, 0}, win.width, win.height); this->addChild(m_dim);
        m_dim->runAction(CCFadeTo::create(.18f, 150));
        m_root = CCNode::create(); m_root->setContentSize({W, H}); m_root->setAnchorPoint({.5f, .5f});
        m_root->setPosition(win / 2); this->addChild(m_root);
        build();
        m_root->setScale(m_fit * .85f);
        m_root->runAction(CCEaseBackOut::create(CCScaleTo::create(.22f, m_fit)));
        this->schedule(schedule_selector(VdPanel::tickLive), .1f);
        if (!Mod::get()->getSavedValue<bool>("tutorial-done") || getB("replay-tutorial")) startTour();
        return true;
    }

    void registerWithTouchDispatcher() override { CCTouchDispatcher::get()->addTargetedDelegate(this, -500, true); }
    void keyBackClicked() override { close(); }

    void close() {
        if (m_closing) return; m_closing = true;
        m_dim->runAction(CCFadeTo::create(.15f, 0));
        m_root->runAction(CCSequence::create(CCEaseSineIn::create(CCScaleTo::create(.15f, m_fit * .88f)),
            CCCallFunc::create(this, callfunc_selector(VdPanel::kill)), nullptr));
    }
    void kill() { this->setTouchEnabled(false); this->setKeypadEnabled(false); this->removeFromParentAndCleanup(true); }

    // ---- layout ---------------------------------------------------------------
    void build() {
        m_pri = colSet("ui-primary-color"); m_sec = colSet("ui-secondary-color"); m_sur = colSet("ui-surface-color"); m_txt = colSet("ui-text-color");
        m_ts = getI("ui-text-size") / 100.f;
        auto win = CCDirector::get()->getWinSize();
        m_fit = std::min((win.width * .94f) / W, (win.height * .90f) / H) * (getI("ui-panel-size") / 100.f);
        m_fit = std::min(m_fit, std::min(win.width * .98f / W, win.height * .98f / H));   // never clip off-screen
        if (m_fit < .35f) m_fit = .35f;
        m_root->setScale(m_fit);
        if (m_chrome) m_chrome->removeFromParent();
        m_hits.clear();
        m_chrome = CCNode::create(); m_root->addChild(m_chrome);
        auto bg = CCDrawNode::create(); m_chrome->addChild(bg);
        if (getB("ui-glow")) for (int i = 3; i >= 1; i--) rrect(bg, -i * 3, -i * 3, W + i * 6, H + i * 6, 16 + i * 3, c4(m_pri, .07f));
        rrect(bg, 0, 0, W, H, 16, c4(m_sur, .97f));
        rrect(bg, 0, H - 40, W, 40, 16, c4(m_sur, 1));
        txt(m_chrome, "voidius", 16, H - 20, .5f, m_pri, true, true);
        // pill
        bool on = getB("enabled"); float px = 120;
        rrect(bg, px, H - 32, 92, 24, 12, c4(on ? m_pri : ccColor3B{80, 80, 90}, on ? 1.f : .9f));
        txt(m_chrome, on ? "assist on" : "assist off", px + 46, H - 20, .3f, on ? m_sur : m_txt, false);
        hit(m_hits, px, H - 38, 92, 36, [this] { setB("enabled", !getB("enabled")); if (!getB("enabled")) vd::E().releaseAll(PlayLayer::get()); build(); fillPage(); });
        // ? and X
        rrect(bg, W - 84, H - 32, 30, 24, 12, c4(m_pri, .25f)); txt(m_chrome, "?", W - 69, H - 20, .34f, m_sec, false);
        hit(m_hits, W - 88, H - 38, 38, 36, [this] { startTour(); });
        rrect(bg, W - 46, H - 32, 30, 24, 12, c4(m_pri, .25f)); txt(m_chrome, "x", W - 31, H - 20, .34f, m_sec, false);
        hit(m_hits, W - 50, H - 38, 40, 36, [this] { close(); });
        // tabs
        char const* tabs[4] = {"live", "speed", "modes", "look"};
        for (int i = 0; i < 4; i++) {
            float x = 12 + i * 94, y = H - 70; bool sel = m_page == i;
            rrect(bg, x, y, 88, 26, 13, c4(sel ? m_pri : m_sur, sel ? 1.f : .0f));
            if (!sel) rrect(bg, x, y, 88, 26, 13, c4(m_pri, .15f));
            txt(m_chrome, tabs[i], x + 44, y + 13, .3f, sel ? m_sur : m_txt, false);
            hit(m_hits, x, y - 4, 88, 34, [this, i] { m_page = i; m_scroll = 0; fillPage(); });
        }
        if (!m_view) {
            m_view = Scissor::create({VIEW_W, VIEW_H}); m_view->setPosition(12, 8); m_root->addChild(m_view, 1);
            m_content = CCNode::create(); m_view->addChild(m_content);
        }
        fillPage();
    }

    // ---- widgets ----------------------------------------------------------------
    CCLabelBMFont* txt(CCNode* p, char const* s, float x, float y, float sc, ccColor3B c, bool left = true, bool display = false) {
        auto l = CCLabelBMFont::create(s, display ? "afterflow-display.fnt"_spr : "afterflow-body.fnt"_spr);
        l->setScale(sc * m_ts); l->setColor(c); l->setPosition(x, y); l->setAnchorPoint({left ? 0.f : .5f, .5f}); p->addChild(l); return l;
    }
    void hit(std::vector<Hit>& v, float x, float y, float w, float h, std::function<void()> f) { v.push_back({CCRect(x, y, w, h), std::move(f)}); }

    float m_cy = 0;     // cursor (distance from top of content)
    CCDrawNode* m_cd = nullptr;
    float rowY(float h) { float y = m_contentH - m_cy - h; return y; }

    void toggleRow(char const* label, char const* id, float x, float w, bool useFn = false) {
        float h = 30, y = rowY(h); bool on = getB(id);
        rrect(m_cd, x, y, w, h, 9, c4(m_pri, .12f));
        txt(m_content, label, x + 10, y + h / 2, .3f, m_txt);
        rrect(m_cd, x + w - 52, y + 5, 44, 20, 10, c4(on ? m_pri : ccColor3B{80, 80, 90}, 1));
        rrect(m_cd, x + w - (on ? 28 : 50), y + 7, 16, 16, 8, c4(on ? m_sur : m_txt, 1));
        hit(m_chits, x, y - 2, w, h + 4, [this, id] { setB(id, !getB(id)); fillPage(); });
    }
    void stepRow(char const* label, char const* id, int lo, int hi, int step, float x, float w, char const* suffix = "") {
        float h = 30, y = rowY(h); int v = (int)getI(id);
        rrect(m_cd, x, y, w, h, 9, c4(m_pri, .12f));
        txt(m_content, label, x + 10, y + h / 2, .3f, m_txt);
        float bx = x + w - 124;
        rrect(m_cd, bx, y + 2, 34, 26, 8, c4(m_pri, .35f)); txt(m_content, "-", bx + 17, y + 15, .4f, m_txt, false);
        txt(m_content, (std::to_string(v) + suffix).c_str(), bx + 62, y + 15, .32f, m_sec, false);
        rrect(m_cd, bx + 90, y + 2, 34, 26, 8, c4(m_pri, .35f)); txt(m_content, "+", bx + 107, y + 15, .4f, m_txt, false);
        hit(m_chits, bx - 4, y - 3, 42, h + 6, [this, id, lo, step] { setI(id, std::max<int64_t>(lo, getI(id) - step)); fillPage(); });
        hit(m_chits, bx + 86, y - 3, 42, h + 6, [this, id, hi, step] { setI(id, std::min<int64_t>(hi, getI(id) + step)); fillPage(); });
    }
    void buttonRow(char const* label, float x, float w, std::function<void()> f, bool accent = true) {
        float h = 30, y = rowY(h);
        rrect(m_cd, x, y, w, h, 9, c4(m_pri, accent ? .9f : .3f));
        txt(m_content, label, x + w / 2, y + h / 2, .3f, accent ? m_sur : m_txt, false);
        hit(m_chits, x, y - 2, w, h + 4, std::move(f));
    }
    void textLine(std::string s, float sc, ccColor3B c, float gap = 22) {
        auto l = txt(m_content, s.c_str(), 4, rowY(14) + 7, sc, c); (void)l; m_cy += gap;
    }
    void gap(float g) { m_cy += g; }

    void fillPage() {
        m_content->removeAllChildren(); m_chits.clear(); m_lCps = m_lState = m_lMode = m_lTarget = m_lLog = nullptr;
        m_contentH = 520; m_cy = 0;               // generous upper bound while building; trimmed below
        m_cd = CCDrawNode::create(); m_content->addChild(m_cd);
        float W2 = VIEW_W, half = (W2 - 8) / 2;
        if (m_page == LIVE) {
            m_lCps = txt(m_content, "0", 8, rowY(30) + 15, .9f, m_pri, true, true);
            m_lState = txt(m_content, "", 120, rowY(30) + 22, .32f, m_txt);
            m_lMode = txt(m_content, "", 120, rowY(30) + 6, .28f, m_sec);
            m_cy += 36;
            m_lTarget = txt(m_content, "", 8, rowY(14) + 7, .3f, m_sec); m_cy += 20;
            txt(m_content, "log", 8, rowY(14) + 7, .3f, m_pri); m_cy += 16;
            m_lLog = txt(m_content, "", 8, rowY(70) + 70, .27f, m_txt); m_lLog->setAnchorPoint({0, 1}); m_cy += 78;
            buttonRow("clear log", 0, 120, [this] { vd::E().clearLog(); }, false);
            tickLive(0);
        } else if (m_page == SPEED) {
            stepRow("trigger cps", "trigger-cps", 3, 30, 1, 0, W2); gap(34);
            toggleRow("quick start", "quick-start", 0, half); toggleRow("go straight", "go-straight", half + 8, half); gap(34);
            { float h = 30, y = rowY(h); bool fixed = Mod::get()->getSettingValue<std::string>("rhythm") == "fixed";
              rrect(m_cd, 0, y, W2, h, 9, c4(m_pri, .12f)); txt(m_content, "speed", 10, y + 15, .3f, m_txt);
              rrect(m_cd, W2 - 190, y + 3, 90, 24, 12, c4(m_pri, fixed ? 1.f : .25f)); txt(m_content, "fixed", W2 - 145, y + 15, .28f, fixed ? m_sur : m_txt, false);
              rrect(m_cd, W2 - 96, y + 3, 90, 24, 12, c4(m_pri, fixed ? .25f : 1.f)); txt(m_content, "match you", W2 - 51, y + 15, .26f, fixed ? m_txt : m_sur, false);
              hit(m_chits, W2 - 190, y, 90, h, [this] { Mod::get()->setSettingValue<std::string>("rhythm", "fixed"); vd::E().refresh(); fillPage(); });
              hit(m_chits, W2 - 96, y, 90, h, [this] { Mod::get()->setSettingValue<std::string>("rhythm", "match your speed"); vd::E().refresh(); fillPage(); }); }
            gap(34);
            stepRow("hold %", "hold-percent", 10, 90, 5, 0, W2, "%"); gap(34);
            stepRow("ship cps", "cps-ship", 2, 60, 1, 0, W2); gap(34);
            stepRow("wave cps", "cps-wave", 2, 60, 1, 0, W2); gap(34);
            stepRow("ufo cps", "cps-ufo", 2, 60, 1, 0, W2); gap(34);
            stepRow("swing cps", "cps-swing", 2, 60, 1, 0, W2); gap(34);
            stepRow("others cps", "target-cps", 2, 60, 1, 0, W2); gap(38);
            buttonRow("recommended", 0, W2, [this] { recommended(); });
        } else if (m_page == MODES) {
            for (int i = 0; i < 8; i += 2) {
                toggleRow(vd::kModeName[i], vd::kModeKey[i], 0, half); toggleRow(vd::kModeName[i + 1], vd::kModeKey[i + 1], half + 8, half); gap(36);
            }
            textLine("ship, wave, ufo and swing are on by default", .26f, m_sec);
        } else {
            for (int i = 0; i < 15; i += 3) { // theme chips, 3 per row
                float cw = (W2 - 16) / 3;
                for (int k = 0; k < 3; k++) {
                    float x = k * (cw + 8), y = rowY(30); auto const& t = kThemes[i + k];
                    rrect(m_cd, x, y, cw, 30, 9, c4(hex(t.f), 1)); rrect(m_cd, x, y, 14, 30, 7, c4(hex(t.p), 1));
                    txt(m_content, t.n, x + 24, y + 15, .28f, hex(t.t));
                    hit(m_chits, x, y, cw, 30, [this, i, k] { applyTheme(kThemes[i + k]); });
                }
                gap(36);
            }
            toggleRow("glow", "ui-glow", 0, half); toggleRow("show cps in game", "show-cps", half + 8, half); gap(34);
            stepRow("panel size", "ui-panel-size", 60, 140, 10, 0, W2, "%"); gap(34);
            stepRow("text size", "ui-text-size", 80, 140, 10, 0, W2, "%"); gap(36);
            textLine("exact colours: Geode settings > Voidius", .26f, m_sec);
        }
        m_contentH = std::max(VIEW_H, m_cy + 6);
        // content was laid out against 520; shift so layout anchors to top of the final height
        float shift = m_contentH - 520; for (auto c : CCArrayExt<CCNode*>(m_content->getChildren())) c->setPositionY(c->getPositionY() + shift);
        for (auto& h : m_chits) h.r.origin.y += shift;
        m_scroll = std::clamp(m_scroll, 0.f, std::max(0.f, m_contentH - VIEW_H));
        applyScroll();
    }
    void applyScroll() { m_content->setPosition(0, VIEW_H - m_contentH + m_scroll); }

    void applyTheme(Theme const& t) {
        auto* m = Mod::get();
        m->setSettingValue<ccColor3B>("ui-primary-color", hex(t.p)); m->setSettingValue<ccColor3B>("ui-secondary-color", hex(t.s));
        m->setSettingValue<ccColor3B>("ui-surface-color", hex(t.f)); m->setSettingValue<ccColor3B>("ui-text-color", hex(t.t));
        build();
    }
    void recommended() {
        int cps[] = {20, 15, 12, 12, 10}; char const* k[] = {"cps-ship", "cps-wave", "cps-ufo", "cps-swing", "target-cps"};
        for (int i = 0; i < 5; i++) setI(k[i], cps[i]);
        setI("trigger-cps", 8); setI("hold-percent", 50); setB("quick-start", false); setB("go-straight", true);
        Mod::get()->setSettingValue<std::string>("rhythm", "fixed");
        for (int i = 0; i < vd::ModeCount; i++) setB(vd::kModeKey[i], i == vd::Ship || i == vd::Wave || i == vd::Ufo || i == vd::Swing);
        vd::E().say("recommended settings applied");
        fillPage();
    }

    void tickLive(float) {
        if (m_page != LIVE || !m_lCps) return;
        auto& e = vd::E(); bool act = e.anyActive(); auto* pl = PlayLayer::get();
        m_lCps->setString(std::to_string(act ? (int)std::round(e.assistCps()) : e.realCps()).c_str());
        m_lCps->setColor(act ? ccColor3B{90, 255, 140} : m_pri);
        m_lState->setString(!e.cfg.enabled ? "assist is off" : act ? "spamming" : "watching");
        std::string md = "not in a level"; int tgt = 0;
        if (pl && pl->m_player1) { auto m = vd::Engine::modeOf(pl->m_player1); md = vd::kModeName[m]; if (!e.cfg.on[m]) md += " (off in modes)"; tgt = e.cfg.cps[m]; }
        m_lMode->setString(md.c_str());
        m_lTarget->setString(fmt::format("trigger {}  ·  target {}  ·  {}", e.cfg.trigger, e.cfg.match ? std::string("match you") : std::to_string(tgt),
            act ? "assist input" : "manual input").c_str());
        std::string lg; int n = 0; for (auto it = e.log.rbegin(); it != e.log.rend() && n < 6; ++it, ++n) lg += *it + "\n";
        m_lLog->setString(lg.empty() ? "nothing yet. spam a ship part" : lg.c_str());
    }

    // ---- tutorial ----------------------------------------------------------------
    void startTour() { m_tutStep = 0; Mod::get()->setSettingValue<bool>("replay-tutorial", false); drawTour(); }
    void drawTour() {
        if (m_tut) { m_tut->removeFromParent(); m_tut = nullptr; } m_thits.clear();
        if (m_tutStep < 0) return;
        m_tut = CCNode::create(); m_root->addChild(m_tut, 10);
        auto d = CCDrawNode::create(); m_tut->addChild(d);
        rrect(d, 0, 0, W, H, 16, c4({0, 0, 0}, .55f)); rrect(d, 50, 36, W - 100, H - 72, 14, c4(m_sur, 1)); rrect(d, 50, 36, W - 100, 4, 2, c4(m_pri, 1));
        txt(m_tut, kTour[m_tutStep][0], W / 2, H - 62, .45f, m_pri, false, true);
        txt(m_tut, kTour[m_tutStep][1], W / 2, H / 2 + 2, .3f, m_txt, false);
        bool last = m_tutStep == kTourN - 1;
        rrect(d, W / 2 - 50, 52, 100, 28, 14, c4(m_pri, 1)); txt(m_tut, last ? "done" : "next", W / 2, 66, .32f, m_sur, false);
        hit(m_thits, W / 2 - 54, 48, 108, 36, [this] { if (++m_tutStep >= kTourN) { m_tutStep = -1; Mod::get()->setSavedValue<bool>("tutorial-done", true); } drawTour(); });
        hit(m_thits, W - 100, H - 80, 40, 36, [this] { m_tutStep = -1; Mod::get()->setSavedValue<bool>("tutorial-done", true); drawTour(); });
        txt(m_tut, "x", W - 80, H - 62, .34f, m_sec, false);
    }

    // ---- touch --------------------------------------------------------------------
    bool ccTouchBegan(CCTouch* t, CCEvent*) override { m_start = m_last = m_root->convertToNodeSpace(t->getLocation()); m_drag = false; return true; }  // always swallow
    void ccTouchMoved(CCTouch* t, CCEvent*) override {
        if (m_tutStep >= 0) return;
        auto p = m_root->convertToNodeSpace(t->getLocation());
        if (!m_drag && fabsf(p.y - m_start.y) > 8 && CCRect(12, 8, VIEW_W, VIEW_H).containsPoint(m_start)) m_drag = true;
        if (m_drag) { m_scroll = std::clamp(m_scroll + (p.y - m_last.y), 0.f, std::max(0.f, m_contentH - VIEW_H)); applyScroll(); }
        m_last = p;
    }
    void ccTouchCancelled(CCTouch*, CCEvent*) override { m_drag = false; }
    void ccTouchEnded(CCTouch* t, CCEvent*) override {
        if (m_closing || m_drag) { m_drag = false; return; }
        auto p = m_root->convertToNodeSpace(t->getLocation());
        if (m_tutStep >= 0) { for (auto& h : m_thits) if (h.r.containsPoint(p)) { auto f = h.fn; f(); return; } return; }
        for (auto& h : m_hits) if (h.r.containsPoint(p)) { auto f = h.fn; f(); return; }
        if (CCRect(12, 8, VIEW_W, VIEW_H).containsPoint(p)) {
            auto cp = m_content->convertToNodeSpace(t->getLocation());
            for (auto& h : m_chits) if (h.r.containsPoint(cp)) { auto f = h.fn; f(); return; }
            return;
        }
        if (!CCRect(0, 0, W, H).containsPoint(p)) close();            // tap outside the panel closes it
    }
};

void vd::openPanel() {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene || scene->getChildByID("voidius-panel")) return;        // never stack panels
    auto p = VdPanel::create(); if (!p) return;
    p->setID("voidius-panel"); scene->addChild(p, 100000);
}
