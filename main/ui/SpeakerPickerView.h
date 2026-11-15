#pragma once

// Where the speaker this display controls is picked: the household's speakers, the chosen one
// marked, and a touch picks one and goes back to the view the picker was opened from. While the
// list is on its way or the backend gave none, it says so instead.

#include <string>

#include "View.h"

class Shell;

class SpeakerPickerView : public View {
    Shell* _shell;
    // What is on screen, so it is built again only when it changed.
    uint32_t _revision{};
    SpeakerList _shown;
    std::string _shown_choice;
    lv_obj_t* _body{};

public:
    SpeakerPickerView(UiHost* host, Shell* shell) : View(host), _shell(shell) {}

protected:
    void do_render(lv_obj_t* parent) override;
    void do_update() override;

private:
    void build();
    void build_loading();
    void build_list();
    void build_notice(const char* icon, const char* message, const std::string& detail, bool retry);
    static void row_clicked(lv_event_t* event);
    static void retry_clicked(lv_event_t* event);
};
