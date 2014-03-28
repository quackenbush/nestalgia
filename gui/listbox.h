#ifndef __listbox_h__
#define __listbox_h__

#include "window.h"

#define MAX_ITEMS 128

typedef struct ListBox
{
    char *items[MAX_ITEMS];
    int num_items;

    int selection;
    int mouseover;
    int scroll_top;
    int num_rows_visible;

    int lost_focus;

    void (*on_highlight)(struct ListBox *, const char *item);
    void (*on_select)(struct ListBox *, const char *item);
    void (*on_keydown)(struct ListBox *, int key);
} ListBox_t;

struct Display; // Forward declaration

void listbox_init(ListBox_t *box);
void listbox_destroy(ListBox_t *box);

void listbox_draw(struct Display *display, ListBox_t *box, Window_t *window, DisplayPixel_t *origin, int stride, Rect_t *clip);
void listbox_keydown(ListBox_t *box, int key);

void listbox_mousemove(ListBox_t *box, Window_t *window, int x, int y);
void listbox_mousedown(ListBox_t *box, Window_t *window, int x, int y);

void listbox_add(ListBox_t *box, char *entry);
void listbox_clear(ListBox_t *box);
void listbox_sort(ListBox_t *box);

void listbox_highlight(ListBox_t *box, int selection);

#endif
