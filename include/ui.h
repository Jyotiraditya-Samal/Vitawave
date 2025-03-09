#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include "file_browser.h"

typedef enum {
    UI_SCREEN_BROWSER = 0,
    UI_SCREEN_NOW_PLAYING,
} UIScreen;

typedef struct {
    UIScreen current_screen;
    int      selected;
    int      list_offset;
} UIState;

int  ui_init(UIState *ui);
void ui_destroy(UIState *ui);
void ui_handle_input(UIState *ui, FileList *browser);
void ui_render(UIState *ui, FileList *browser);

#endif
