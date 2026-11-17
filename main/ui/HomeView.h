#pragma once

// The view the display opens on once a speaker is chosen, with the now-playing bar under it: a
// heading that ends in the entry into the library. While the Shell's "Reconnecting" marker shows at
// the top right, the heading ends before it.

#include "View.h"

class Shell;

class HomeView : public View {
    Shell* _shell;
    lv_obj_t* _heading{};

public:
    HomeView(UiHost* host, Shell* shell) : View(host), _shell(shell) {}

protected:
    void do_render(lv_obj_t* parent) override;
    void do_update() override;

private:
    void fit_heading();
    static void library_clicked(lv_event_t* event);
};
