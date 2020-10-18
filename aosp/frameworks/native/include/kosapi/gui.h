#ifndef _LIBKOSAPI_GUI_H
#define _LIBKOSAPI_GUI_H

#ifndef _WIN32
#include <sys/cdefs.h>
#include <stdint.h>
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct ANativeWindow;

ANativeWindow* kosGetSurface();

typedef void (*fdid_gui2_screen_captured)(uint8_t* pixel_buf, int length, int width, int height, uint32_t flags, void* user);
int kosRecordScreenLoop(uint32_t bitrate_kbps, uint32_t max_fps_to_encoder, uint8_t* pixel_buf, fdid_gui2_screen_captured did, void* user);
void kosStopRecordScreen();
void kosPauseRecordScreen(bool pause);
bool kosRecordScreenPaused();

#ifdef __cplusplus
}
#endif

#endif /* _LIBKOSAPI_GUI_H */
