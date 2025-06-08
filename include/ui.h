#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include "file_browser.h"
#include "audio_engine.h"
#include "metadata.h"

typedef enum {
    UI_SCREEN_BROWSER = 0,
    UI_SCREEN_NOW_PLAYING,
} UIScreen;

typedef struct {
    UIScreen      current_screen;
    UIScreen      prev_screen;
    int           selected;
    int           list_offset;
    int           anim_frame;
    TrackMetadata current_meta;
} UIState;

int  ui_init(UIState *ui);
void ui_destroy(UIState *ui);
void ui_handle_input(UIState *ui, AudioEngine *engine, FileList *browser);
void ui_render(UIState *ui, AudioEngine *engine, FileList *browser);

#endif
