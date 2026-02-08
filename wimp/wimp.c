/*
 * wimp.c – 64-bit Window Manager (Wimp) for RISC OS Phoenix
 * Full desktop with windows, menus, icons, drag-select
 * Integrates with GPU for accelerated redraw
 * Supports context-sensitive menus triggered by middle mouse button
 * Author: R Andrews Grok 4 – 28 Nov 2025
 */

#include "kernel.h"
#include "wimp.h"
#include "gpu.h"
#include <string.h>

#define MAX_WINDOWS     256
#define MAX_EVENTS      1024

// Mouse buttons
#define MOUSE_SELECT    1  // Left
#define MOUSE_MENU      2  // Middle – triggers context menu
#define MOUSE_ADJUST    4  // Right

typedef struct wimp_event_queue {
    wimp_event_t events[MAX_EVENTS];
    int head, tail;
    spinlock_t lock;
} wimp_event_queue_t;

static wimp_event_queue_t event_queue;

static window_t windows[MAX_WINDOWS];
static int num_windows = 0;

static void wimp_init(void) {
    memset(windows, 0, sizeof(windows));
    memset(&event_queue, 0, sizeof(event_queue));
    spinlock_init(&event_queue.lock);
    gpu_init();  // Initialize GPU acceleration
    debug_print("Wimp initialized – desktop ready\n");
}

/* Poll for events – cooperative, but kernel preemptive */
int Wimp_Poll(int mask, wimp_event_t *event) {
    unsigned long flags;
    spin_lock_irqsave(&event_queue.lock, flags);

    if (event_queue.head == event_queue.tail) {
        spin_unlock_irqrestore(&event_queue.lock, flags);
        yield();  // Allow preemption while idle
        return wimp_NULL_REASON_CODE;
    }

    *event = event_queue.events[event_queue.tail % MAX_EVENTS];
    event_queue.tail++;

    spin_unlock_irqrestore(&event_queue.lock, flags);
    return event->type;
}

/* Internal event enqueue */
void wimp_enqueue_event(wimp_event_t *event) {
    unsigned long flags;
    spin_lock_irqsave(&event_queue.lock, flags);

    if ((event_queue.head - event_queue.tail) >= MAX_EVENTS) {
        debug_print("Wimp: Event queue overflow\n");
        spin_unlock_irqrestore(&event_queue.lock, flags);
        return;
    }

    event_queue.events[event_queue.head % MAX_EVENTS] = *event;
    event_queue.head++;

    spin_unlock_irqrestore(&event_queue.lock, flags);
    task_wakeup(wimp_task);  // Wake Wimp if blocked
}

/* Create window */
window_t *wimp_create_window(wimp_window_def *def) {
    if (num_windows >= MAX_WINDOWS) return NULL;

    window_t *win = &windows[num_windows++];
    memcpy(&win->def, def, sizeof(wimp_window_def));

    // Allocate GPU texture for window backing store
    win->texture = gpu_create_texture(def->width, def->height);

    debug_print("Wimp: Window created – handle %p\n", win);
    return win;
}

/* Redraw window request – enqueue event */
void wimp_redraw_request(window_t *win, bbox_t *clip) {
    wimp_event_t event;
    event.type = wimp_REDRAW_WINDOW_REQUEST;
    event.redraw.window = win;
    event.redraw.clip = *clip;

    wimp_enqueue_event(&event);
}

/* Mouse click handler – from input driver */
void input_mouse_click(int button, int x, int y) {
    wimp_event_t event;
    event.type = wimp_MOUSE_CLICK;
    event.mouse.button = button;
    event.mouse.x = x;
    event.mouse.y = y;

    // Find window under mouse
    window_t *win = wimp_find_window_at(x, y);
    if (win) {
        event.mouse.window = win;
        event.mouse.icon = wimp_find_icon_at(win, x - win->def.x0, y - win->def.y0);
    }

    // Context-sensitive menu on middle button
    if (button & MOUSE_MENU) {
        if (win) {
            menu_t *context_menu = win->context_menu;  // Per-window context menu
            if (context_menu) {
                menu_show(context_menu, x, y, win);
            } else {
                // Filer or desktop default menu
                menu_t *default_menu = get_default_menu(win);
                menu_show(default_menu, x, y, win);
            }
        }
    }

    wimp_enqueue_event(&event);
}

/* Key press handler */
void input_key_press(int key, int modifiers) {
    wimp_event_t event;
    event.type = wimp_KEY_PRESSED;
    event.key.code = key;
    event.key.modifiers = modifiers;

    // Send to focused window
    window_t *focus = wimp_get_focus_window();
    if (focus) event.key.window = focus;

    wimp_enqueue_event(&event);
}

/* Main Wimp loop – runs as dedicated task */
void wimp_task(void) {
    wimp_init();

    while (1) {
        wimp_event_t event;
        int code = Wimp_Poll(0, &event);

        switch (code) {
            case wimp_REDRAW_WINDOW_REQUEST:
                gpu_redraw_window(event.redraw.window);  // Accelerated redraw
                break;

            case wimp_MOUSE_CLICK:
                // Dispatch to app
                app_handle_mouse(&event.mouse);
                break;

            case wimp_KEY_PRESSED:
                app_handle_key(&event.key);
                break;

            // ... other events
        }
    }
}

/* Module init – start Wimp task */
_kernel_oserror *module_init(const char *arg, int podule)
{
    task_create("wimp", wimp_task, 0, (1ULL << 0));  // Pin to core 0 for compatibility
    debug_print("Wimp module loaded – desktop active\n");
    return NULL;
}
/* Mouse click handler – from input driver */
void input_mouse_click(int button, int x, int y) {
    wimp_event_t event;
    event.type = wimp_MOUSE_CLICK;
    event.mouse.button = button;
    event.mouse.x = x;
    event.mouse.y = y;

    // Find window/icon under mouse
    window_t *win = wimp_find_window_at(x, y);
    if (win) {
        event.mouse.window = win;
        event.mouse.icon = wimp_find_icon_at(win, x - win->def.x0, y - win->def.y0);
    }

    // Context-sensitive menu on middle button
    if (button & MOUSE_MENU) {
        if (win) {
            menu_t *context_menu = get_context_menu(win, event.mouse.icon);  // Per-item context
            if (context_menu) {
                menu_show(context_menu, x, y, win);
            } else if (win == filer_window) {
                menu_t *filer_menu = get_filer_menu(event.mouse.icon);  // Filer-specific
                menu_show(filer_menu, x, y, win);
            } else {
                menu_t *default_menu = get_default_menu(win);
                menu_show(default_menu, x, y, win);
            }
        }
        wimp_enqueue_event(&event);
        return;
    }

    // Select (left) double-click: Open file/app
    static int last_button = 0;
    static uint64_t last_time = 0;
    uint64_t now = get_time_ms();
    if (button & MOUSE_SELECT && last_button == MOUSE_SELECT && now - last_time < 300) {  // Double-click threshold 300ms
        if (win == filer_window && event.mouse.icon) {
            inode_t *inode = get_icon_inode(event.mouse.icon);
            if (inode->i_mode & S_IFDIR) {
                filer_open_directory(inode);
            } else if (inode->i_mode & S_IFREG) {
                char *app = get_app_for_file_type(inode->file_type);
                if (app) {
                    execve(app, (char*[]){app, inode->path, NULL}, environ);
                }
            } else if (inode->i_mode & S_IFAPP) {  // Custom mode for apps
                execve(inode->path, (char*[]){inode->path, NULL}, environ);
            }
        }
    }

    last_button = button;
    last_time = now;

    wimp_enqueue_event(&event);
}

/* Stub for file type → app registry */
char *get_app_for_file_type(uint16_t type) {
    // Lookup in registry (e.g., hash table or file !MimeMap)
    if (type == 0xFFF) return "/Apps/!Edit";
    if (type == 0xAFF) return "/Apps/!Draw";
    return NULL;  // Default to !Edit or hex viewer
}

/* Stub for Filer menu */
menu_t *get_filer_menu(icon_t *icon) {
    menu_t *menu = menu_create(5);
    menu_add_item(menu, 0, "Open", 0, filer_open_item, NULL);
    menu_add_item(menu, 1, "Copy", 0, filer_copy_item, NULL);
    menu_add_item(menu, 2, "Rename", 0, filer_rename_item, NULL);
    menu_add_item(menu, 3, "Delete", 0, filer_delete_item, NULL);
    menu_add_item(menu, 4, "Info", 0, filer_info_item, NULL);
    return menu;
}