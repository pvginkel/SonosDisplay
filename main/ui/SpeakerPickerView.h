#pragma once

// The speakers screen: the household's speakers, the chosen one marked. A touch on a row picks its
// speaker as the one this display controls and goes back to the view the screen was opened from.
// While the list has the chosen speaker, every other row carries a control that adds its speaker
// to the chosen speaker's group, or removes it from it: a ring for a speaker outside the group, a
// tick for one in it, and a spinner, which takes no press, while a change of it is pending. While
// the list is on its way or the backend gave none, the screen says so instead.

#include <string>
#include <vector>

#include "View.h"

class Shell;

class SpeakerPickerView : public View {
    Shell* _shell;
    // What is on screen, so it is built again only when it changed.
    uint32_t _revision{};
    SpeakerList _shown;
    std::string _shown_choice;
    // The speakers with a group change pending, as the rows show them.
    std::vector<std::string> _shown_pending;
    lv_obj_t* _hint{};
    lv_obj_t* _body{};
    // The rows' parent, while the body holds the list.
    lv_obj_t* _list{};

public:
    SpeakerPickerView(UiHost* host, Shell* shell) : View(host), _shell(shell) {}

protected:
    void do_render(lv_obj_t* parent) override;
    void do_update() override;

private:
    void build();
    void build_loading();
    void build_list();
    void build_group_control(lv_obj_t* row, const Speaker& speaker, bool member);
    void build_notice(const char* icon, const char* message, const std::string& detail, bool retry);
    static void row_clicked(lv_event_t* event);
    static void group_clicked(lv_event_t* event);
    static void retry_clicked(lv_event_t* event);
};
