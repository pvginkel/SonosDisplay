#pragma once

// What the display shows while it starts: a message with a spinner, or an error that keeps it
// from going on.

#include <string>

#include "View.h"

class LoadingView : public View {
    std::string _message;
    bool _error{};
    lv_obj_t* _icon{};
    lv_obj_t* _label{};
    lv_obj_t* _spinner{};

public:
    LoadingView(UiHost* host, const char* message);

    void set_message(const char* message);
    void set_error(const char* message);

    // It shows before the backend's tasks start: the bar has nothing to control yet.
    bool has_bar() const override { return false; }

protected:
    void do_render(lv_obj_t* parent) override;

private:
    void apply();
};
