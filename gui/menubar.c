#include "menubar.h"
#include "log.h"
#include <string.h>

#define MENUBAR_SHIM 10

#define MENUBAR_HEIGHT (display->font->letter_height + 2)

#define MENUBAR_COLOR        (display_maprgb(display, 0x00, 0x00, 0x99))
#define MENUBAR_HIGHLIGHTED  (display_maprgb(display, 0x99, 0x00, 0x00))

static int
menuitem_init(MenuItem_t *item, BitmapFont_t *font)
{
    int width = strlen(item->text) * font->letter_width;

    switch(item->type)
    {
        case mitEntry:
        case mitSeparator:
            break;

        case mitSubMenu:
            if(item->child_menu)
            {
                item->width = menuitem_init(item->child_menu, font);
            }
            printf("%s => %d\n", item->text, item->width);
            break;

    }

    if(item->next)
    {
        int next_width = menuitem_init(item->next, font);

        return (next_width > width) ? next_width : width;
    }

    return width;
}

void
menubar_init(MenuBar_t *menubar, MenuItem_t *menu)
{
    menubar->init = 0;
    menubar->menu = menu;
    menubar->highlighted = NULL;
}

void
menubar_destroy(MenuBar_t *menubar)
{
    ASSERT(0, "destroy not impl");
}

void
menubar_deselect(MenuBar_t *menubar)
{
    // FIXME: clear selections
    menubar->menu->expanded = 0;
}

static void
menuitem_draw(Display_t *display, MenuBar_t *menubar, MenuItem_t *menuitem,
              DisplayPixel_t *origin, int width, int horizontal)
{
    Rect_t clip = {0};
    BitmapFont_t *font = display->font;
    int color = (menuitem == menubar->highlighted) ? MENUBAR_HIGHLIGHTED : MENUBAR_COLOR;
    int stride = display->stride;

    display_fillrect(origin, width, MENUBAR_HEIGHT, color, stride, &clip);
    font_printstr(font, origin, stride, menuitem->text, &clip);

    if(menuitem->next)
    {
        DisplayPixel_t offset;
        if(horizontal)
        {
            offset = width;
        }
        else
        {
            offset = (MENUBAR_HEIGHT * stride);
        }
        menuitem_draw(display, menubar, menuitem->next, origin + offset, width, horizontal);
    }

    if(menuitem->type == mitSubMenu)
    {
        if(menuitem->child_menu && menuitem->expanded)
        {
            menuitem_draw(display, menubar, menuitem->child_menu, origin + MENUBAR_HEIGHT * stride, menuitem->width + MENUBAR_SHIM, 0);
        }
    }
}

void
menubar_draw(Display_t *display, MenuBar_t *menubar, DisplayPixel_t *origin)
{
    if(menubar->visible)
    {
        if(menubar->menu)
        {
            if(! menubar->init)
            {
                menubar->init = 1;
                menuitem_init(menubar->menu, display->font);
            }

            Rect_t clip = {0};
            display_fillrect(origin, display->width, MENUBAR_HEIGHT, MENUBAR_COLOR, display->stride, &clip);

            menuitem_draw(display, menubar, menubar->menu, origin, menubar->menu->width, 1);
        }
    }
}

int
menubar_mousedown(Display_t *display, MenuBar_t *menubar, int x, int y)
{
    MenuItem_t *highlighted = menubar->highlighted;
    if(highlighted)
    {
        NOTIFY("menubar_click(%d,%d)\n", x, y);
        NOTIFY("Clicked menu [%s]\n", highlighted->text);

        // FIXME: need to clear other expansions
        if(highlighted->type == mitSubMenu)
        {
            highlighted->expanded = ! highlighted->expanded;
        }
        else
        {
            if(highlighted->on_select)
            {
                highlighted->on_select(highlighted);
            }

            menubar_deselect(menubar);
        }

        return 1;
    }
    else
    {
        menubar_deselect(menubar);
    }

    return 0;
}

static int
menuitem_check_xy(Display_t *display, MenuBar_t *menubar, MenuItem_t *menuitem, int width,
                  int offset_x, int offset_y,
                  int x, int y, int horizontal)
{
    Rect_t box;

    box.left = offset_x;
    box.right = offset_x + width;
    box.top = offset_y;
    box.bottom = offset_y + MENUBAR_HEIGHT;

    if(x >= box.left && x < box.right &&
       y >= box.top && y < box.bottom)
    {
        menubar->highlighted = menuitem;
        return 1;
    }

    MenuItem_t *next = menuitem->next;
    if(next)
    {
        switch(menuitem->type)
        {
            case mitEntry:
            case mitSeparator:
                offset_y = box.bottom;
                break;

            case mitSubMenu:
            {
                if(menuitem->expanded)
                {
                    int child_x = offset_x;
                    int child_y = offset_y;

                    if(horizontal)
                    {
                        child_y = box.bottom;
                    }
                    else
                    {
                        child_x = box.right;
                    }

                    int result = menuitem_check_xy(display, menubar, menuitem->child_menu, menuitem->width + MENUBAR_SHIM, child_x, child_y, x, y, 0);
                    if(result)
                        return result;
                }

                break;
            }
        }

        if(horizontal)
            offset_x = box.right;
        else
            offset_y = box.bottom;

        return menuitem_check_xy(display, menubar, next, width, offset_x, offset_y, x, y, horizontal);
    }

    return 0;
}

int
menubar_mousemove(Display_t *display, MenuBar_t *menubar, int x, int y)
{
    if(menuitem_check_xy(display, menubar, menubar->menu, menubar->menu->width, 0, 0, x, y, 1))
    {
        return 1;
    }
    else
    {
        menubar->highlighted = NULL;
        return 0;
    }
}

static int
menuitem_check_key(MenuItem_t *menu, int key)
{
    if(menu->key_accelerator == key)
    {
        if(menu->on_select)
        {
            menu->on_select(menu);
        }

        return 1;
    }

    if(menu->child_menu)
    {
        if(menuitem_check_key(menu->child_menu, key))
        {
            return 1;
        }
    }

    if(menu->next)
    {
        if(menuitem_check_key(menu->next, key))
        {
            return 1;
        }
    }

    return 0;
}

int
menubar_key_accelerator(MenuBar_t *menubar, int key)
{
    return menuitem_check_key(menubar->menu, key);
}
