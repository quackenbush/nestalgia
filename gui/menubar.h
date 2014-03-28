#ifndef __menubar_h__
#define __menubar_h__

#include <stdint.h>

typedef enum
{
    mitEntry,
    mitSeparator,
    mitSubMenu, // FIXME: segment into MainMenu/SubMenu so that orientation is automagic?
} MenuItemType_t;

typedef struct MenuItem
{
    const char *text;
    int width;

    int checked;  // Only for mitEntry
    int expanded; // Only for mitSubMenu
    struct MenuItem *child_menu; // Only for mitSubMenu

    int key_accelerator;

    void (*on_select)(struct MenuItem *menu);

    MenuItemType_t type;
    struct MenuItem *next; // Next MenuItem after this (forms a linked list)
} MenuItem_t;

typedef struct
{
    int visible;
    int init;

    MenuItem_t *menu;
    MenuItem_t *highlighted;
} MenuBar_t;

#include "display.h"

struct Display; // Forward declaration

void menubar_init(MenuBar_t *menubar, MenuItem_t *menu);
void menubar_destroy(MenuBar_t *menubar);

void menubar_deselect(MenuBar_t *menubar);

void menubar_draw(struct Display *display, MenuBar_t *menubar, DisplayPixel_t *origin);

int menubar_mousedown(struct Display *display, MenuBar_t *menubar, int x, int y);
int menubar_mousemove(struct Display *display, MenuBar_t *menubar, int x, int y);

int menubar_key_accelerator(MenuBar_t *menubar, int key);

#endif
