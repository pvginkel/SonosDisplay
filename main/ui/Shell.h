#pragma once

// The one LVGL screen: a content area holding a stack of views, the now-playing bar under it, and
// the "Reconnecting" marker. The bar is the Shell's, which is how it stays at the bottom of every
// screen: it shows under every view that has it (View::has_bar()) while the display is not held
// on the speaker picker, and the content area ends where it begins.
//
// It also decides which view the app opens on: now playing, or the speaker picker while the
// display is held on it (Model::needs_speaker()).

#include <memory>
#include <optional>
#include <vector>

#include "NowPlayingBar.h"
#include "UiHost.h"
#include "View.h"
#include "lvgl.h"

class Shell {
    UiHost* _host;
    lv_obj_t* _screen{};
    int32_t _height{};
    lv_obj_t* _content{};
    NowPlayingBar _bar;
    lv_obj_t* _marker{};
    std::vector<std::unique_ptr<View>> _views;
    // Whether the app's first view is the speaker picker; empty until open().
    std::optional<bool> _held;
    bool _back_requested{};

public:
    // The marker shows once the event stream has been down for this long.
    static constexpr uint32_t RECONNECTING_MARKER_DELAY_MS = 5000;

    explicit Shell(UiHost* host);
    Shell(const Shell&) = delete;
    Shell& operator=(const Shell&) = delete;
    ~Shell();

    // Builds the screen on the display's active screen. Theme::begin() comes first.
    void begin(lv_display_t* display);

    // Shows a view on top of the stack; the one under it stays, hidden.
    void push(std::unique_ptr<View> view);

    // Destroys the top view and shows the one under it.
    void pop();

    // Destroys every view and shows this one.
    void replace(std::unique_ptr<View> view);

    // Shows the app's first view, and from here on update() keeps it the right one: when the
    // display becomes held on the speaker picker, or stops being, every view gives way to it.
    void open();

    // Shows the speaker picker on top of the stack.
    void open_speaker_picker();

    // Goes back to the view under the top one at the next update(), so a view may ask from inside
    // one of its own events. Nothing happens when the top view is the only one.
    void back() { _back_requested = true; }

    View* get_current() const { return _views.empty() ? nullptr : _views.back().get(); }

    bool shows_bar() const { return _bar.is_shown(); }

    // Called from the UI loop.
    void update();

private:
    void show_first_view();
    void layout();
    void create_marker();
    void update_marker();
};
