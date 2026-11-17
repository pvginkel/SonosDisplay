#include "ScreenPower.h"

#include "BoardConfig.h"
#include "Ch422g.h"
#include "RgbPanel.h"
#include "support.h"

LOG_TAG(ScreenPower);

ScreenPower::ScreenPower(Ch422g* expander, RgbPanel* panel)
    : _expander(expander), _panel(panel), _timeout_ms(DEFAULT_TIMEOUT_SECONDS * 1000ll) {}

void ScreenPower::begin(bool lit) {
    _lit = lit;
    _state = lit ? State::On : State::Off;
    _last_activity_ms = esp_get_millis();
}

void ScreenPower::set_timeout_seconds(uint32_t seconds) {
    ESP_LOGI(TAG, "Screen goes off after %" PRIu32 " seconds", seconds);

    _timeout_ms = seconds * 1000ll;
}

bool ScreenPower::handle_touch(bool pressed) {
    const auto now = esp_get_millis();

    switch (_state) {
        case State::Off:
            if (pressed) {
                ESP_LOGI(TAG, "Woken by a touch");

                _state = State::WaitRelease;
                _last_activity_ms = now;
            }
            return false;

        case State::WaitRelease:
            _last_activity_ms = now;
            if (!pressed) {
                _state = State::On;
            }
            return false;

        case State::On:
            if (pressed) {
                _last_activity_ms = now;
            }
            return true;
    }

    return false;
}

void ScreenPower::set_motion_wakes(bool wakes) {
    ESP_LOGI(TAG, "%s", wakes ? "Motion wakes the screen" : "Only a touch wakes the screen");

    _motion_wakes = wakes;
}

void ScreenPower::handle_motion() {
    if (!_motion_wakes) {
        return;
    }

    _last_activity_ms = esp_get_millis();

    if (_state == State::Off) {
        ESP_LOGI(TAG, "Woken by motion");

        _state = State::On;
    }
}

void ScreenPower::process() {
    const auto now = esp_get_millis();

    if (_state == State::On && now - _last_activity_ms >= _timeout_ms) {
        ESP_LOGI(TAG, "No touch or motion; going to sleep");

        _state = State::Off;
    }

    const auto want_lit = _state != State::Off;
    if (want_lit == _lit) {
        return;
    }

    // A request inside the interval stays pending until it ends. The first switch after boot is
    // not delayed.
    if (_switched && now - _last_switch_ms < BACKLIGHT_MIN_INTERVAL_MS) {
        return;
    }

    switch_backlight(want_lit, now);
}

void ScreenPower::switch_backlight(bool on, int64_t now) {
    ESP_LOGI(TAG, "Switching the backlight %s", on ? "on" : "off");

    if (on) {
        // A picture that drifted while the screen was dark is put right before it is seen.
        _panel->restart();
    }

    const auto err = _expander->set(EXPANDER_BACKLIGHT, on);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot switch the backlight: %s", esp_err_to_name(err));
    }

    // A failed write also counts as a switch, so it is retried no faster than any other.
    _lit = err == ESP_OK ? on : _lit;
    _switched = true;
    _last_switch_ms = now;
}
