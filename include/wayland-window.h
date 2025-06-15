#pragma once

// Struct to hold the application's window_state
struct window_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_surface *surface;
    struct xdg_wm_base *xdg_wm_base;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct zwp_linux_dmabuf_v1 *linux_dmabuf;
    struct wl_buffer *buffer;
    int width;
    int height;
    int running;
    uint32_t *formats;
    int formats_count;
};

struct window_state *setup_wayland_window();
void setup_window(struct window_state *app_state);
int draw_window(struct window_state *app_state, struct MessageData *message, int dmabuf_fd);
int destroy_window(struct window_state *app_state);
