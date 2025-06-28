#pragma once

#define DISPLAY_WIDTH 1920
#define DISPLAY_HEIGHT 1080
#define DISPLAY_REFRESH_RATE 60

#define DISPLAY_SOCKET_PATH "/tmp/playdroid_socket"

struct display {
    const char *socket_path;

    int width;
    int height;
    int refresh_rate;

    struct window_state *wayland_state;
    bool open_wayland_window;
};

void init_display(struct display *display);
void run_display(struct display *display, struct gsthelper *gsthelper);
