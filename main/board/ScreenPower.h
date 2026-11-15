#pragma once

#include <cstdint>

class Ch422g;
class RgbPanel;

// The only code that switches the backlight: wake on motion or a touch, sleep after a while,
// and never two switches within a second (docs/firmware-design.md, section 3). Runs on the UI
// task.
class ScreenPower {
public:
    enum class State {
        Off,
        On,
        // Lit by a touch that is still down. The touch is swallowed until the finger lifts, so it
        // cannot press what was under it.
        WaitRelease,
    };

private:
    Ch422g* _expander;
    RgbPanel* _panel;
    State _state{State::Off};
    bool _lit{};
    bool _switched{};
    int64_t _last_switch_ms{};
    int64_t _last_activity_ms{};
    int64_t _timeout_ms;

public:
    static constexpr uint32_t DEFAULT_TIMEOUT_SECONDS = 60;

    ScreenPower(Ch422g* expander, RgbPanel* panel);
    ScreenPower(const ScreenPower&) = delete;
    ScreenPower& operator=(const ScreenPower&) = delete;

    // `lit` says whether the expander's first write switched the backlight on.
    void begin(bool lit);
    void set_timeout_seconds(uint32_t seconds);

    // Called for every touch read. Returns whether the touch reaches the UI.
    bool handle_touch(bool pressed);
    void handle_motion();
    void process();

    State get_state() const { return _state; }
    bool is_lit() const { return _lit; }

private:
    void switch_backlight(bool on, int64_t now);
};
