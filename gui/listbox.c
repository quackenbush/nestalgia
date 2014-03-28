#include "listbox.h"
#include "log.h"
#include "display.h"
#include <string.h>
#include <ctype.h>

#define COLOR_SELECTION_FOCUS  0x5555
#define COLOR_SELECTION_AWAY   display_maprgb(display, 0x67,0x67,0x67)
#define COLOR_MOUSEOVER        display_maprgb(display, 0x99,0x99,0x99)

#include <SDL/SDL_keysym.h>  // FIXME: lame-o include

static const char *
listbox_get(ListBox_t *box, int item)
{
    return box->items[item];
}

static void
listbox_scrollup(ListBox_t *box)
{
    if(box->selection < box->scroll_top)
    {
        box->scroll_top = box->selection;
    }
}

static void
listbox_scrolldown(ListBox_t *box)
{
    int box_rows = box->num_rows_visible;
    if(box_rows > 0 && box->selection >= box->scroll_top + box_rows - 1)
    {
        box->scroll_top = box->selection - box_rows + 1;
        if(box->scroll_top < 0)
            box->scroll_top = 0;
    }
}

static void
listbox_select(ListBox_t *box)
{
    if(box->on_select)
    {
        box->on_select(box, listbox_get(box, box->selection));
    }
}

void
listbox_highlight(ListBox_t *box, int selection)
{
    box->selection = selection;

    if(box->selection < 0)
        box->selection = 0;

    if(box->selection >= box->num_items)
        box->selection = box->num_items - 1;

    listbox_scrollup(box);
    listbox_scrolldown(box);

    if(box->on_highlight)
    {
        box->on_highlight(box, listbox_get(box, box->selection));
    }
}

static int
listbox_xy_to_item(ListBox_t *box, Window_t *window, int x, int y)
{
    int row = y / window->font->letter_height;
    if(row >= 0 && row < box->num_rows_visible)
    {
        return row + box->scroll_top;
    }

    return -1;
}

void
listbox_mousemove(ListBox_t *box, Window_t *window, int x, int y)
{
    box->mouseover = listbox_xy_to_item(box, window, x, y);
}

void
listbox_mousedown(ListBox_t *box, Window_t *window, int x, int y)
{
    int selection = listbox_xy_to_item(box, window, x, y);
    int prev_selection = box->selection;

    if(box->lost_focus)
    {
        // Listbox lost focus since last click; ensure we don't double-click from this click
        prev_selection = -1;
    }

    if(selection >= 0)
    {
        if(selection == prev_selection &&
           selection >= 0 &&
           selection < box->num_items)
        {
            // FIXME: this is a crappy double-click implementation (needs to take time into account)
            listbox_select(box);
        }
        else
        {
            listbox_highlight(box, selection);
        }
    }

    box->lost_focus = 0;
}

static int
listbox_highlight_by_key(ListBox_t *box, int key)
{
    // Check if the user is already on an item starting with the same letter
    // If so, select the next item starting with that letter
    if(box->selection >= 0 && box->selection < box->num_items)
    {
        const char *item = listbox_get(box, box->selection);
        if(tolower(item[0]) == key)
        {
            const char *next = listbox_get(box, box->selection + 1);
            if(tolower(next[0]) == key)
            {
                listbox_highlight(box, box->selection + 1);
                return box->selection;
            }
        }
    }

    // Binary search to select the nearest entry corresponding to the first letter
    int lo = 0;
    int hi = box->num_items - 1;
    while(lo <= hi)
    {
        char entry;
        int i = (hi + lo) / 2;
        entry = tolower(*listbox_get(box, i));
        if(entry < key)
            lo = i + 1;
        else if(entry > key)
        {
            hi = i;
            if(lo == hi)
                break;
        }
        else
        {
            char prev = tolower(*listbox_get(box, i - 1));
            if(prev == key)
            {
                hi = i;
            }
            else
            {
                listbox_highlight(box, i);

                return i;
            }
        }
    }

    // Not found
    return -1;
}

void
listbox_keydown(ListBox_t *box, int key)
{
    int is_alphanumeric = ((key >= SDLK_a && key <= SDLK_z) ||
                           (key >= SDLK_0 && key <= SDLK_9) ||
                           (key == '.'));

    if(is_alphanumeric)
    {
        listbox_highlight_by_key(box, key);
        return;
    }

    switch(key)
    {
        case SDLK_UP:
            listbox_highlight(box, box->selection - 1);
            break;

        case SDLK_DOWN:
            listbox_highlight(box, box->selection + 1);
            break;

        case SDLK_PAGEDOWN:
            listbox_highlight(box, box->selection + box->num_rows_visible);
            break;

        case SDLK_PAGEUP:
            listbox_highlight(box, box->selection - box->num_rows_visible);
            break;

        case SDLK_HOME:
            listbox_highlight(box, 0);
            break;

        case SDLK_END:
            listbox_highlight(box, box->num_items - 1);
            break;

        case SDLK_RETURN:
            if(box->selection >= 0)
            {
                listbox_select(box);
            }
            break;

        default:
            if(box->on_keydown)
            {
                box->on_keydown(box, key);
            }
            break;
    }
}

void
listbox_init(ListBox_t *box)
{
    memset(box, 0, sizeof(ListBox_t));

    box->num_rows_visible = 0;
    box->num_items = 0;
    listbox_clear(box);
}

void
listbox_destroy(ListBox_t *box)
{
    // FIXME: this needs to be invoked at shutdown
    listbox_clear(box);
}

void
listbox_clear(ListBox_t *box)
{
    int i;
    for(i = 0; i < box->num_items; i++)
    {
        free(box->items[i]);
    }

    box->scroll_top = 0;
    box->num_items = 0;

    // Reset selection
    box->mouseover = -1;
    box->selection = -1;
}

void
listbox_add(ListBox_t *box, char *entry)
{
    if(box->num_items >= MAX_ITEMS)
    {
        //NOTIFY("Item overflow: %d\n", box->num_items);
        return;
    }

    box->items[box->num_items] = entry;
    box->num_items++;
}

static int
comp(const void *a, const void *b)
{
    const char **stra = (const char **) a;
    const char **strb = (const char **) b;
    int res = strcasecmp(*stra, *strb);

    return res;
}

void
listbox_sort(ListBox_t *box)
{
    qsort(box->items, box->num_items, sizeof(char *), comp);
}

void
listbox_draw(Display_t *display, ListBox_t *box, Window_t *window, DisplayPixel_t *origin, int stride, Rect_t *clip)
{
    int font_height = window->font->letter_height;
    int font_width = window->font->letter_width;
    int top_row = box->scroll_top;
    if(window->height > 0)
    {
        box->num_rows_visible = (window->height / font_height) - 1; // FIXME: I think the -1 is due to TITLEBAR_HEIGHT
    }
    int bottom_row = min(box->num_items, top_row + box->num_rows_visible);
    int n;

    for(n = top_row; n < bottom_row; n++)
    {
        DisplayPixel_t *top = origin + 1 + ((n - top_row) * font_height * stride);
        int color = -1;

        if(n == box->mouseover)
        {
            color = COLOR_MOUSEOVER;
        }
        else if(n == box->selection)
        {
            color = (window->selected) ? COLOR_SELECTION_FOCUS : COLOR_SELECTION_AWAY;
        }

        if(color >= 0)
        {
            display_fillrect(top, window->width, font_height, color, stride, clip);
        }

        const char *item = listbox_get(box, n);
        unsigned max_width = window->width / font_width;
        char item_display[max_width + 1];
        unsigned item_width = strlen(item);
        if(item_width >= max_width)
        {
            // Item won't fit, add ellipsis (...) to the end
            int i;
            strcpy(item_display, item);
            for(i = 0; i < 3; i++)
            {
                item_display[max_width - i - 1] = '.';
            }
            item_display[max_width] = 0;
            item = item_display;
        }

        font_printstr(window->font, top, stride, item, clip);
    }

    if(! window->selected)
        box->lost_focus = 1;
}
