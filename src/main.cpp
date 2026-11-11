#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include "Engine.hpp"
#include "Panel.hpp"
using namespace geode::prelude;

class $modify(VdGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (!vd::E().intercept(this, down, button, isPlayer1))
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
    void processCommands(float dt) {
        vd::E().step(this);
        GJBaseGameLayer::processCommands(dt);
    }
};

class $modify(VdPlayLayer, PlayLayer) {
    struct Fields { CCLabelBMFont* label = nullptr; int shown = -2; bool shownActive = false; };

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        vd::E().refresh(); vd::E().releaseAll(this);
        auto l = CCLabelBMFont::create("0 cps", "afterflow-body.fnt"_spr);
        l->setID("voidius-cps"); l->setAnchorPoint({0, 0}); l->setPosition(12, 8); l->setScale(.45f); l->setOpacity(190);
        m_uiLayer->addChild(l, 100);
        m_fields->label = l;
    }
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto& f = *m_fields; auto& e = vd::E();
        if (!f.label) return;
        bool show = e.cfg.showCps;
        f.label->setVisible(show);
        if (!show) return;
        bool act = e.anyActive(); int v = act ? (int)std::round(e.assistCps()) : e.realCps();
        if (v != f.shown || act != f.shownActive) {
            f.shown = v; f.shownActive = act;
            f.label->setString(fmt::format("{} cps", v).c_str());
            f.label->setColor(act ? ccColor3B{90, 255, 140} : ccColor3B{255, 255, 255});
        }
    }
    void pauseGame(bool b) { vd::E().releaseAll(this); PlayLayer::pauseGame(b); }
    void resetLevel() { vd::E().releaseAll(this); PlayLayer::resetLevel(); }
    void destroyPlayer(PlayerObject* p, GameObject* o) { vd::E().releaseAll(this); PlayLayer::destroyPlayer(p, o); }
    void levelComplete() { vd::E().releaseAll(this); PlayLayer::levelComplete(); }
    void onQuit() { vd::E().releaseAll(this); PlayLayer::onQuit(); }
};

class $modify(VdPauseLayer, PauseLayer) {
    struct Fields { CCLabelBMFont* status = nullptr; };

    void customSetup() {
        PauseLayer::customSetup();
        if (this->getChildByID("voidius-menu")) return;           // never duplicate
        auto win = CCDirector::get()->getWinSize();
        auto menu = CCMenu::create(); menu->setID("voidius-menu"); menu->setPosition(0, 0);

        auto bg = CCScale9Sprite::create("GJ_button_01.png");
        bg->setContentSize({118, 32}); bg->setColor({168, 85, 247});
        auto lbl = CCLabelBMFont::create("Voidius", "bigFont.fnt"); lbl->setScale(.5f);
        lbl->setPosition(bg->getContentSize() / 2); bg->addChild(lbl);
        auto open = CCMenuItemSpriteExtra::create(bg, this, menu_selector(VdPauseLayer::onVdOpen));
        open->setID("voidius-open");

        auto tog = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(VdPauseLayer::onVdToggle), .65f);
        tog->setID("voidius-assist-toggle"); tog->toggle(vd::E().cfg.enabled);
        auto name = CCLabelBMFont::create("Voidius Assist", "bigFont.fnt"); name->setScale(.3f);
        m_fields->status = CCLabelBMFont::create("", "bigFont.fnt"); m_fields->status->setScale(.3f);

        float x = win.width - 80, y = win.height - 30;       // top-right: clear of Resume/Restart column
        open->setPosition(x, y);
        tog->setPosition(x - 42, y - 34);
        name->setPosition(x + 14, y - 28); m_fields->status->setPosition(x + 14, y - 42);
        menu->addChild(open); menu->addChild(tog);
        this->addChild(menu, 50); this->addChild(name, 50); this->addChild(m_fields->status, 50);
        updateStatus();
    }
    void updateStatus() {
        bool on = vd::E().cfg.enabled;
        m_fields->status->setString(on ? "ON" : "OFF");
        m_fields->status->setColor(on ? ccColor3B{190, 120, 255} : ccColor3B{150, 150, 150});
    }
    void onVdOpen(CCObject*) { vd::openPanel(); }
    void onVdToggle(CCObject* s) {
        bool now = !static_cast<CCMenuItemToggler*>(s)->isToggled();    // callback fires before the toggler flips
        Mod::get()->setSettingValue<bool>("enabled", now);
        vd::E().refresh();
        if (!now) vd::E().releaseAll(PlayLayer::get());
        updateStatus();
    }
};

$on_mod(Loaded) { vd::E().refresh(); }

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
// Desktop-only shortcuts, unchanged in behaviour from the original (F7 toggle, F5 panel).
// Not compiled for Android. Signature taken from the original binary's symbols; unverified here.
$execute {
    KeybindSettingPressedEventV3(Mod::get(), "panel-key").listen([](Keybind const&, bool down, bool repeat, double) {
        if (down && !repeat) vd::openPanel();
        return ListenerResult::Propagate;
    }).leak();
    KeybindSettingPressedEventV3(Mod::get(), "toggle-key").listen([](Keybind const&, bool down, bool repeat, double) {
        if (down && !repeat) {
            bool v = !Mod::get()->getSettingValue<bool>("enabled");
            Mod::get()->setSettingValue<bool>("enabled", v); vd::E().refresh();
            if (!v) vd::E().releaseAll(PlayLayer::get());
        }
        return ListenerResult::Propagate;
    }).leak();
}
#endif
