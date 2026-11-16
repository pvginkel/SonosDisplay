#pragma once

// The queue of the chosen speaker's group, in the order the speaker plays it: each entry's title
// and artist. It opens on the page that holds the track that is loaded, that track's row at the
// top and marked. While no track of the queue is loaded the group plays from another source, which
// a line under the heading says, or the queue is empty, which the screen says. While it shows it
// is fetched again whenever the event stream says that the queue or the loaded track changed, and
// once the stream is back after it was away.
// Clear, at the heading's end, asks to be confirmed, over the list, before it empties the queue.

#include <cstddef>
#include <cstdint>
#include <optional>

#include "ListView.h"

class QueueView : public ListView {
    PagedList<QueueEntry> _entries;
    // The place in the queue, from 1, of the track that is loaded, by the last page answered.
    std::optional<uint32_t> _current;
    // The model's count of the queue's changes when the queue was last asked for.
    uint32_t _queue_changes;
    // The model's stream_down_since when the queue was last asked for with the stream connected;
    // none while it has not been.
    std::optional<uint32_t> _stream_down_since;
    // Whether the page that holds the loaded track came, and the list is not built with it yet.
    bool _located{};
    lv_obj_t* _clear{};
    lv_obj_t* _clear_label{};
    lv_obj_t* _confirm{};

public:
    QueueView(UiHost* host, Shell* shell);

protected:
    void do_update() override;
    PageWindow& get_pages() override { return _entries; }
    void request_page(uint32_t page) override;
    void build_row(lv_obj_t* row, size_t index) override;
    const char* get_loading_message() const override;
    const char* get_empty_message() const override;
    void do_render_heading(lv_obj_t* heading) override;
    void do_render_beside(lv_obj_t* body, lv_obj_t* area) override;
    void do_list_built() override;

private:
    void ask_queue(uint32_t page, bool locate, bool replaces);
    void show_confirm(bool shown);
    static void clear_clicked(lv_event_t* event);
    static void confirm_clicked(lv_event_t* event);
    static void cancel_clicked(lv_event_t* event);
};
