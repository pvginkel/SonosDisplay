// render: draws the firmware's screens to PNG files, without a device.
//
//   render --all --out <dir>
//   render <scene>... --out <dir>
//   render --list
//
// Exits non-zero when a scene is unknown, an LVGL assertion fires, LVGL logs a warning or an
// error, or a scene's fixture or touch does not do what the scene says.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Shell.h"
#include "SimHost.h"
#include "Theme.h"
#include "lvgl.h"
#include "scenes.h"
#include "sim_assert.h"

// LVGL declares lodepng inside extern "C", where its C++ API does not compile.
#define LODEPNG_NO_COMPILE_CPP
#include "src/libs/lodepng/lodepng.h"

static constexpr int32_t WIDTH = 800;
static constexpr int32_t HEIGHT = 480;

// How long a scene runs before it is drawn, in steps of LVGL's clock.
static constexpr uint32_t SCENE_TIME_MS = 500;
static constexpr uint32_t STEP_MS = 10;

// How long a scene's touch stays down: several reads of the input device, and under LVGL's
// long press.
static constexpr uint32_t TAP_MS = 100;

// From one of a scene's touches to the next: what the first one changed is on screen by then.
static constexpr uint32_t TAP_INTERVAL_MS = 200;

static uint32_t tick_ms;
static const SimHost* scene_host;
static uint32_t scene_elapsed_ms;
static int lvgl_problems;
static uint8_t frame[WIDTH * HEIGHT * 2];

extern "C" void sim_assert_failed(void) {
    fprintf(stderr, "render: an LVGL assertion failed\n");
    exit(3);
}

static void print_log(lv_log_level_t level, const char* message) {
    fputs(message, stderr);

    if (level >= LV_LOG_LEVEL_WARN) {
        lvgl_problems++;
    }
}

static void flush(lv_display_t* display, const lv_area_t*, uint8_t*) { lv_display_flush_ready(display); }

static void read_touch(lv_indev_t*, lv_indev_data_t* data) {
    const auto& taps = scene_host->get_taps();
    const auto index = scene_elapsed_ms / TAP_INTERVAL_MS;

    if (index < taps.size() && scene_elapsed_ms % TAP_INTERVAL_MS < TAP_MS) {
        data->point = taps[index];
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static bool write_png(const std::filesystem::path& path) {
    std::vector<uint8_t> rgb(WIDTH * HEIGHT * 3);

    for (int32_t i = 0; i < WIDTH * HEIGHT; i++) {
        const uint16_t pixel = frame[i * 2] | (frame[i * 2 + 1] << 8);
        const uint8_t r = (pixel >> 11) & 0x1F;
        const uint8_t g = (pixel >> 5) & 0x3F;
        const uint8_t b = pixel & 0x1F;

        rgb[i * 3] = uint8_t((r << 3) | (r >> 2));
        rgb[i * 3 + 1] = uint8_t((g << 2) | (g >> 4));
        rgb[i * 3 + 2] = uint8_t((b << 3) | (b >> 2));
    }

    // Encoded in memory: LVGL's lodepng writes files through lv_fs, which has no driver here.
    unsigned char* png = nullptr;
    size_t size = 0;
    const auto error = lodepng_encode24(&png, &size, rgb.data(), WIDTH, HEIGHT);
    if (error) {
        fprintf(stderr, "render: cannot encode %s: %s\n", path.c_str(), lodepng_error_text(error));
        return false;
    }

    auto file = fopen(path.c_str(), "wb");
    const auto written = file && fwrite(png, 1, size, file) == size;
    if (file) {
        fclose(file);
    }
    lv_free(png);

    if (!written) {
        fprintf(stderr, "render: cannot write %s\n", path.c_str());
    }

    return written;
}

static bool render(const Scene& scene, lv_display_t* display, const std::filesystem::path& out) {
    SimHost host;

    auto shell = std::make_unique<Shell>(&host);
    shell->begin(display);

    scene.build(host, *shell);

    scene_host = &host;
    for (scene_elapsed_ms = 0; scene_elapsed_ms < SCENE_TIME_MS; scene_elapsed_ms += STEP_MS) {
        tick_ms += STEP_MS;
        shell->update();
        lv_timer_handler();
    }
    scene_host = nullptr;

    if (scene.check) {
        if (const auto problem = scene.check(host, *shell)) {
            fprintf(stderr, "render: %s: %s\n", scene.name, problem);
            return false;
        }
    }

    // Everything is drawn, so a scene looks the same whichever scene came before it.
    lv_obj_invalidate(lv_display_get_screen_active(display));
    lv_refr_now(display);

    const auto path = out / (std::string(scene.name) + ".png");
    const auto written = write_png(path);
    if (written) {
        printf("%s\n", path.c_str());
    }

    return written;
}

static int usage() {
    fprintf(stderr, "usage: render --all --out <dir>\n       render <scene>... --out <dir>\n       render --list\n");
    return 2;
}

int main(int argc, char** argv) {
    bool all = false;
    bool list = false;
    std::filesystem::path out;
    std::vector<std::string> names;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--all")) {
            all = true;
        } else if (!strcmp(argv[i], "--list")) {
            list = true;
        } else if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            out = argv[++i];
        } else if (argv[i][0] == '-') {
            return usage();
        } else {
            names.emplace_back(argv[i]);
        }
    }

    const auto& scenes = get_scenes();

    if (list) {
        for (const auto& scene : scenes) {
            printf("%s\n", scene.name);
        }
        return 0;
    }

    if (out.empty() || all == !names.empty()) {
        return usage();
    }

    std::vector<const Scene*> selected;
    for (const auto& scene : scenes) {
        if (all) {
            selected.push_back(&scene);
        }
    }
    for (const auto& name : names) {
        const Scene* found = nullptr;
        for (const auto& scene : scenes) {
            if (name == scene.name) {
                found = &scene;
            }
        }
        if (!found) {
            fprintf(stderr, "render: no scene %s\n", name.c_str());
            return 2;
        }
        selected.push_back(found);
    }

    std::filesystem::create_directories(out);

    lv_init();
    lv_log_register_print_cb(print_log);
    lv_tick_set_cb([]() { return tick_ms; });

    auto display = lv_display_create(WIDTH, HEIGHT);
    lv_display_set_buffers(display, frame, nullptr, sizeof(frame), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, flush);

    auto touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, read_touch);

    Theme::begin(display);

    bool ok = true;
    for (const auto scene : selected) {
        ok = render(*scene, display, out) && ok;
    }

    if (lvgl_problems) {
        fprintf(stderr, "render: LVGL logged %d warnings or errors\n", lvgl_problems);
        return 1;
    }

    return ok ? 0 : 1;
}
