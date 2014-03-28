#ifndef __file_listbox_h__
#define __file_listbox_h__

#include "listbox.h"
#include "cond_lock.h"

typedef struct FileListBox
{
    ListBox_t listbox;
    const char *path;
    const char *suffix;

    SDL_Thread *thread; // FIXME: platform-specific
    CondLock_t cond;

    void *p;
    void (*on_highlight)(struct FileListBox *box, const char *file);
    void (*on_select)(struct FileListBox *box, const char *file);
} FileListBox_t;

void file_listbox_init(FileListBox_t *box, const char *suffix, const char *path);
void file_listbox_destroy(FileListBox_t *box);
void file_listbox_update(FileListBox_t *box);

#endif
