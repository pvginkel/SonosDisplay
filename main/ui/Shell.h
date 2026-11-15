#pragma once

// The one LVGL screen: a content area holding a stack of views, and the "Reconnecting" marker.
// From milestone 1 the now-playing bar sits under the content area, which is how it stays at the
// bottom of every screen.

#include <memory>
#include <vector>

#include "UiHost.h"
#include "View.h"
#include "lvgl.h"

class Shell {
    UiHost* _host;
    lv_obj_t* _screen{};
    lv_obj_t* _content{};
    lv_obj_t* _marker{};
    std::vector<std::unique_ptr<View>> _views;

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

    View* get_current() const { return _views.empty() ? nullptr : _views.back().get(); }

    // Called from the UI loop.
    void update();

private:
    void create_marker();
    void update_marker();
};
