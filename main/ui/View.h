#pragma once

// A screen's worth of UI inside the Shell's content area. A view builds its objects in
// do_render() and keeps them current in do_update(), called from the UI loop.

#include <memory>

#include "UiHost.h"
#include "lvgl.h"

class View {
    // Owned by the view and handed out as a weak reference with every request it makes, so a
    // closed view neither receives late data nor leaves downloads behind it.
    std::shared_ptr<void> _scope;
    lv_obj_t* _root{};

protected:
    UiHost* _host;

public:
    explicit View(UiHost* host);
    View(const View&) = delete;
    View& operator=(const View&) = delete;
    virtual ~View();

    void render(lv_obj_t* parent);
    void update();
    void set_visible(bool visible);

    std::weak_ptr<void> get_scope() const { return _scope; }

protected:
    virtual void do_render(lv_obj_t* parent) = 0;
    virtual void do_update() {}
};
