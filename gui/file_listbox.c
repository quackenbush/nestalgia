#include "file_listbox.h"
#include "log.h"
#include <string.h>

#include <sys/types.h> // non-portable vvv
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include <SDL/SDL_keysym.h>  // FIXME: lame-o include

static void
file_listbox_chdir(FileListBox_t *box, const char *path)
{
    // FIXME: need real directory appender
    //box->path = path;
    if(chdir(path) == 0)
        file_listbox_update(box);
}

static void
file_listbox_on_highlight(ListBox_t *b, const char *path)
{
    FileListBox_t *box = (FileListBox_t *) b;

    if(box->on_highlight)
    {
        box->on_highlight(box, path);
    }
}

static void
file_listbox_on_select(ListBox_t *b, const char *path)
{
    FileListBox_t *box = (FileListBox_t *) b;
    int is_dir = path[strlen(path) - 1] == '/';

    if(is_dir)
    {
        file_listbox_chdir(box, path);
    }
    else
    {
        if(box->on_select)
        {
            box->on_select(box, path);
        }
    }
}

static void
file_listbox_on_keydown(ListBox_t *b, int key)
{
    FileListBox_t *box = (FileListBox_t *) b;
    switch(key)
    {
        case SDLK_LEFT:
        case SDLK_BACKSPACE:
            file_listbox_chdir(box, "..");
            break;

        default:
            break;
    }
}

static int
file_listbox_thread(void *p)
{
    // FIXME: implement file thread
#if 0
    FileListBox_t *box = (FileListBox_t *) p;

    while(1)
    {
        cond_wait(&box->cond);

        //wait();
    }
#endif

    return 0;
}

void
file_listbox_init(FileListBox_t *box, const char *suffix, const char *path)
{
    ListBox_t *listbox = (ListBox_t *) box;
    listbox_init(listbox);
    listbox->on_highlight = file_listbox_on_highlight;
    listbox->on_select = file_listbox_on_select;
    listbox->on_keydown = file_listbox_on_keydown;

    box->path = path;
    box->suffix = suffix;

    cond_init(&box->cond);
    if(0)
    box->thread = SDL_CreateThread(file_listbox_thread, box);

    file_listbox_update(box);
}

void
file_listbox_destroy(FileListBox_t *box)
{
    cond_destroy(&box->cond);
}

static void
update_dir(const char *path, const char *suffix,
           void *p, void (*callback)(void *p, char *path))
{
    DIR *dp;
    struct dirent *file;
    dp = opendir(path);

    if(dp != NULL)
    {
        int suffix_len = strlen(suffix);

        while ((file = readdir(dp)) != NULL)
        {
            const char *filename = file->d_name;
            int len = strlen(filename);
            struct stat file_info;
            char *new_filename;

            if(stat(filename, &file_info) == -1)
            {
                //ASSERT(0, "stat failed: %s", filename);
                continue;
            }

            if(S_ISDIR(file_info.st_mode))
            {
                int skip = 0;
                if(filename[0] == '.')
                {
                    // Skip hidden directories
                    skip = 1;
                    if(len == 2 && filename[1] == '.')
                    {
                        // Except the .. directory
                        skip = 0;
                    }
                }

                if(! skip)
                {
                    // Append a trailing slash to the dirname
                    len++;

                    new_filename = malloc(len + 1);
                    strcpy(new_filename, filename);

                    new_filename[len-1] = '/';
                    new_filename[len] = 0;
                    callback(p, new_filename);
                }
            }
            else
            {
                // Check if the filename suffix matches
                if(len > suffix_len)
                {
                    if(strcasecmp(filename + (len - suffix_len), suffix) == 0)
                    {
                        new_filename = malloc(len + 1);
                        strcpy(new_filename, filename);

                        callback(p, new_filename);
                    }
                }
            }
        }

        (void) closedir(dp);
    }
}

static void
file_listbox_add_file(void *p, char *path)
{
    ListBox_t *box = (ListBox_t *) p;
    listbox_add(box, path);
}

void file_listbox_update(FileListBox_t *box)
{
    ListBox_t *listbox = (ListBox_t *) box;

    listbox_clear(listbox);
    update_dir(box->path, box->suffix, box, file_listbox_add_file);
    listbox_sort(listbox);

    listbox_highlight(listbox, 0);
}
