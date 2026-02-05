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