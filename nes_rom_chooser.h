#ifndef __nes_rom_chooser_h__
#define __nes_rom_chooser_h__

#include "display.h"
#include "file_listbox.h"
#include "ines.h"

typedef struct NESRomChooser
{
    Window_t window;
    FileListBox_t filebox;

    iNES_t ines;
    int ines_valid;

    void *p;
    void (*on_select)(struct NESRomChooser *chooser, const char *rom);
} NESRomChooser_t;

void nes_rom_chooser_init(NESRomChooser_t *chooser, Display_t *display);
void nes_rom_chooser_destroy(NESRomChooser_t *chooser);

#endif
