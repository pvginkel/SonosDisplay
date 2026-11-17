#include "ItemPresses.h"

#include <span>
#include <utility>

#include "PressButton.h"
#include "Theme.h"
#include "lv_support.h"

static constexpr int32_t BUTTON_GAP = 8;

// The queue's buttons of an item, in order: a track is never shuffled.
static std::span<const QueueAction> get_queue_actions(ItemKind kind) {
    static constexpr QueueAction ALL[] = {QueueAction::Play, QueueAction::Shuffle, QueueAction::Add};
    static constexpr QueueAction UNSHUFFLED[] = {QueueAction::Play, QueueAction::Add};

    if (kind == ItemKind::Track) {
        return UNSHUFFLED;
    }

    return ALL;
}

static const char* get_queue_icon(QueueAction action) {
    switch (action) {
        case QueueAction::Play:
            return ICON_PLAY;
        case QueueAction::Shuffle:
            return ICON_SHUFFLE_VARIANT;
        case QueueAction::Add:
            return ICON_PLAYLIST_PLUS;
    }

    return ICON_PLAY;
}

static const char* get_favourite_icon(std::optional<int64_t> favourite_id) {
    return favourite_id ? ICON_HEART : ICON_HEART_OUTLINE;
}

// The favourite toggle comes after the queue's buttons, so a queue's button's place among the
// buttons is its action's among get_queue_actions().
ItemPresses::ItemPresses(UiHost* host, lv_obj_t* parent, ItemKind kind, std::string item_id,
                         std::optional<int64_t> favourite_id)
    : _host(host), _kind(kind), _item_id(std::move(item_id)), _favourite_id(favourite_id) {
    _buttons = lv_container_create(parent);
    lv_obj_set_style_pad_column(_buttons, BUTTON_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(_buttons, LV_FLEX_FLOW_ROW);

    for (const auto action : get_queue_actions(kind)) {
        PressButton::create(_buttons, get_queue_icon(action), queue_clicked, this);
    }

    if (kind != ItemKind::Track) {
        PressButton::create(_buttons, get_favourite_icon(favourite_id), favourite_clicked, this);
    }
}

void ItemPresses::apply(const FavouriteChange& change) {
    const auto favourite_id = change.apply(_kind, _item_id, _favourite_id);
    if (favourite_id == _favourite_id) {
        return;
    }

    _favourite_id = favourite_id;
    PressButton::set_icon(lv_obj_get_child(_buttons, -1), get_favourite_icon(favourite_id));
}

void ItemPresses::show_on_its_way() {
    const auto& model = _host->get_model();
    const auto actions = get_queue_actions(_kind);

    for (size_t i = 0; i < actions.size(); i++) {
        const auto on_its_way =
            model.queueing.is_on_its_way(QueueRequest{.kind = _kind, .item_id = _item_id, .action = actions[i]});

        PressButton::show_on_its_way(lv_obj_get_child(_buttons, int32_t(i)), on_its_way);
    }

    if (_kind != ItemKind::Track) {
        PressButton::show_on_its_way(lv_obj_get_child(_buttons, -1), model.favouring.is_on_its_way(_kind, _item_id));
    }
}

// A press whose request is on its way sends nothing; the item's other buttons still send theirs.
// The host changes the model, and the view's next update shows the press on its way.
void ItemPresses::queue_clicked(lv_event_t* event) {
    auto self = (ItemPresses*)lv_event_get_user_data(event);
    const auto place = size_t(lv_obj_get_index(lv_event_get_current_target_obj(event)));
    const QueueRequest request{
        .kind = self->_kind, .item_id = self->_item_id, .action = get_queue_actions(self->_kind)[place]};

    if (!self->_host->get_model().queueing.is_on_its_way(request)) {
        self->_host->queue_item(request);
    }
}

// A press while a change of the item is on its way sends nothing.
void ItemPresses::favourite_clicked(lv_event_t* event) {
    auto self = (ItemPresses*)lv_event_get_user_data(event);

    if (!self->_host->get_model().favouring.is_on_its_way(self->_kind, self->_item_id)) {
        self->_host->change_favourite(
            FavouriteRequest{.kind = self->_kind, .item_id = self->_item_id, .favourite_id = self->_favourite_id});
    }
}
