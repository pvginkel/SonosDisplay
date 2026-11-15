#include "scenes.h"

#include <memory>

#include "LoadingView.h"
#include "Messages.h"
#include "PlaceholderView.h"

static void loading(SimHost& host, Shell& shell) { shell.replace(std::make_unique<LoadingView>(&host, MSG_STARTING)); }

static void loading_update(SimHost& host, Shell& shell) {
    shell.replace(std::make_unique<LoadingView>(&host, MSG_CHECKING_FOR_UPDATE));
}

static void no_backend(SimHost& host, Shell& shell) {
    host.model().backend_url.clear();

    auto view = std::make_unique<LoadingView>(&host, MSG_CHECKING_FOR_UPDATE);
    auto loading_view = view.get();
    shell.replace(std::move(view));

    loading_view->set_error(MSG_NO_BACKEND);
}

static void placeholder(SimHost& host, Shell& shell) { shell.replace(std::make_unique<PlaceholderView>(&host)); }

static void placeholder_reconnecting(SimHost& host, Shell& shell) {
    host.model().stream_state = StreamState::Disconnected;
    host.model().stream_down_since = lv_tick_get() - 10 * 1000;

    shell.replace(std::make_unique<PlaceholderView>(&host));
}

const std::vector<Scene>& get_scenes() {
    static const std::vector<Scene> scenes = {
        {"loading", loading},
        {"loading-update", loading_update},
        {"no-backend", no_backend},
        {"placeholder", placeholder},
        {"placeholder-reconnecting", placeholder_reconnecting},
    };

    return scenes;
}
