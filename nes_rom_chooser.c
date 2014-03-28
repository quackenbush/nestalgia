#include "nes_rom_chooser.h"
#include "log.h"
#include <string.h>

static void
nes_rom_chooser_highlight(FileListBox_t *box, const char *path)
{
    NESRomChooser_t *chooser = (NESRomChooser_t *) box->p;
    chooser->ines_valid = 0;
    FILE *fp = fopen(path, "rb");
    if(! fp)
        return;

    if(ines_load(&chooser->ines, fp))
    {
        chooser->ines_valid = 1;
    }

    fclose(fp);
}

static void
nes_rom_chooser_select(FileListBox_t *box, const char *path)
{
    NESRomChooser_t *chooser = (NESRomChooser_t *) box->p;
    if(chooser->on_select)
    {
        chooser->on_select(chooser, path);
    }
}

static void
nes_rom_chooser_draw(Display_t *display, Window_t *window, DisplayPixel_t *origin, int stride, Rect_t *clip)
{
    NESRomChooser_t *chooser = (NESRomChooser_t *) window->p;
    char msg[40];
    listbox_draw(display, &chooser->filebox.listbox, window, origin, stride, clip);

    origin += stride * (window->height - 12);

    display_fillrect(origin, window->width, 12, 0x6333, stride, clip);
    if(chooser->ines_valid)
    {
        int mapper_num = ines_mapper_num(&chooser->ines);
        snprintf(msg, sizeof(msg), "Mapper %d | %dKB PRG | %dKB VROM",
                 mapper_num,
                 chooser->ines.num_16k_prg_rom_banks * 16,
                 chooser->ines.num_8k_vrom_banks * 8);
        msg[sizeof(msg)-1] = 0;
        font_printstr(window->font, (origin + 1), stride, msg, clip);
    }
}

static void
nes_rom_chooser_mousemove(Window_t *window, int x, int y)
{
    NESRomChooser_t *chooser = (NESRomChooser_t *) window->p;
    listbox_mousemove(&chooser->filebox.listbox, window, x, y);
}

static void
nes_rom_chooser_mousedown(Window_t *window, int x, int y)
{
    NESRomChooser_t *chooser = (NESRomChooser_t *) window->p;
    listbox_mousedown(&chooser->filebox.listbox, window, x, y);
}

static void
nes_rom_chooser_keydown(Window_t *window, int key)
{
    NESRomChooser_t *chooser = (NESRomChooser_t *) window->p;
    listbox_keydown(&chooser->filebox.listbox, key);
}

void
nes_rom_chooser_init(NESRomChooser_t *chooser, Display_t *display)
{
    Window_t *window = &chooser->window;
    FileListBox_t *filebox = &chooser->filebox;

    ASSERT(window, "No window!\n");
    filebox->on_select = nes_rom_chooser_select;
    filebox->on_highlight = nes_rom_chooser_highlight;
    filebox->p = chooser;

    window->on_mousemove = nes_rom_chooser_mousemove;
    window->on_mousedown = nes_rom_chooser_mousedown;
    window->on_keydown = nes_rom_chooser_keydown;

    file_listbox_init(filebox, ".nes", ".");

    window->title = "Load ROM";
    window->width = 260;
    window->height = 240;
    window->p = chooser;
    window->draw = nes_rom_chooser_draw;
    window->x = 0;
    window->y = 0;
    window_init(window);

    display_add_window(display, window);
}

void
nes_rom_chooser_destroy(NESRomChooser_t *chooser)
{
    file_listbox_destroy(&chooser->filebox);
}

