#pragma once

// LV_ASSERT_HANDLER for sim/: an LVGL assertion fails the render.

#ifdef __cplusplus
extern "C" {
#endif

void sim_assert_failed(void);

#ifdef __cplusplus
}
#endif
