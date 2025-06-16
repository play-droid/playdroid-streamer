#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

// Include the generated protocol headers
#include "linux-dmabuf-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <socket-protocol.h>
#include <wayland-window.h>

static void dmabuf_format(void *, struct zwp_linux_dmabuf_v1 *, uint32_t);
static void dmabuf_modifiers(void *data, struct zwp_linux_dmabuf_v1 *dmabuf,
                 uint32_t format, uint32_t modifier_hi, uint32_t modifier_lo) {
    
}

static void dmabuf_format(void *data, struct zwp_linux_dmabuf_v1 *, uint32_t format) {
    struct window_state *app_state = (struct window_state *)data;

    ++app_state->formats_count;
    app_state->formats = (uint32_t *)realloc(app_state->formats,
                                             app_state->formats_count * sizeof(*app_state->formats));
    app_state->formats[app_state->formats_count - 1] = format;
}

static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
    dmabuf_format,
    dmabuf_modifiers
};

// --- XDG Toplevel Listener ---
static void xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *toplevel,
                                          int32_t width, int32_t height, struct wl_array *states) {
    // No-op, we use the initial size.
}

static void xdg_toplevel_handle_close(void *data, struct xdg_toplevel *toplevel) {
    struct window_state *app_state = (struct window_state *) data;
    app_state->running = 0; // Signal the main loop to exit
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_handle_configure,
    .close = xdg_toplevel_handle_close,
};

// --- XDG Surface Listener ---
static void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct window_state *app_state = (struct window_state*) data;
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_handle_configure,
};

// --- XDG WM Base Listener ---
static void xdg_wm_base_handle_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_handle_ping,
};

// --- Registry Listener ---
static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface, uint32_t version) {
    struct window_state *app_state = (struct window_state * ) data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app_state->compositor = (wl_compositor *)wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app_state->xdg_wm_base = (xdg_wm_base *)wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(app_state->xdg_wm_base, &xdg_wm_base_listener, app_state);
    } else if (strcmp(interface, zwp_linux_dmabuf_v1_interface.name) == 0) {
        // We bind to version 3, which is common.
        app_state->linux_dmabuf = (zwp_linux_dmabuf_v1 *)wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, 3);
        zwp_linux_dmabuf_v1_add_listener(app_state->linux_dmabuf, &dmabuf_listener, app_state);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    // This is handled in more complex applications. For this example, we ignore it.
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

struct window_state *setup_wayland_window() {
    // Initialize the application window_state

    struct window_state* app_state;
    app_state = (struct window_state *)calloc(1, sizeof(struct window_state));

    app_state->running = 1;

    // 1. Connect to Wayland display
    app_state->display = wl_display_connect(NULL);
    if (!app_state->display) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        return app_state;
    }

    // 2. Get registry and discover global objects
    app_state->registry = wl_display_get_registry(app_state->display);
    wl_registry_add_listener(app_state->registry, &registry_listener, app_state);
    wl_display_roundtrip(app_state->display); // Wait for globals to be announced

    // Check if we got the necessary globals
    if (!app_state->compositor || !app_state->xdg_wm_base || !app_state->linux_dmabuf) {
        fprintf(stderr, "Required Wayland interfaces not available.\n");
        if (!app_state->compositor) {
            fprintf(stderr, "Compositor interface not found.\n");
        }
        if (!app_state->xdg_wm_base) {
            fprintf(stderr, "XDG WM Base interface not found.\n");
        }
        if (!app_state->linux_dmabuf) {
            fprintf(stderr, "Linux DMABUF interface not found.\n");
        }
        wl_display_disconnect(app_state->display);
        return app_state;
    }

    return app_state;
}

void setup_window(struct window_state *app_state) {
    // 4. Create window surface
    app_state->surface = wl_compositor_create_surface(app_state->compositor);
    app_state->xdg_surface = xdg_wm_base_get_xdg_surface(app_state->xdg_wm_base, app_state->surface);
    xdg_surface_add_listener(app_state->xdg_surface, &xdg_surface_listener, app_state);

    app_state->xdg_toplevel = xdg_surface_get_toplevel(app_state->xdg_surface);
    xdg_toplevel_add_listener(app_state->xdg_toplevel, &xdg_toplevel_listener, app_state);

    xdg_toplevel_set_title(app_state->xdg_toplevel, "DMABUF Viewer");

    // The surface is ready to be configured and displayed
    wl_surface_commit(app_state->surface);
}

bool isFormatSupported(struct window_state *app_state, uint32_t format) {
    for (int i = 0; i < app_state->formats_count; i++) {
        if (format == app_state->formats[i])
            return true;
    }
    return false;
}

int findFormat(uint32_t hal_format) {
    switch (hal_format) {
    case DRM_FORMAT_BGR888:
        return DRM_FORMAT_RGB888;
    case DRM_FORMAT_ARGB8888:
        return DRM_FORMAT_ABGR8888;
    case DRM_FORMAT_XBGR8888:
        return DRM_FORMAT_XRGB8888;
    case DRM_FORMAT_ABGR8888:
        return DRM_FORMAT_ARGB8888;
    case DRM_FORMAT_BGR565:
        return DRM_FORMAT_RGB565;
    case DRM_FORMAT_YVU420:
        return DRM_FORMAT_GR88;
    }
    return DRM_FORMAT_XRGB8888;
}

int draw_window(struct window_state *app_state, struct MessageData *message, int dmabuf_fd) {
    app_state->width = message->width;
    app_state->height = message->height;
    uint32_t format = DRM_FORMAT_XRGB8888;
    uint32_t stride = message->stride;

    if (isFormatSupported(app_state, message->format)) {
        format = message->format;
    } else {
        format = findFormat(message->format);
    }

    // 3. Create the DMABUF-based wl_buffer
    struct zwp_linux_buffer_params_v1 *params;
    params = zwp_linux_dmabuf_v1_create_params(app_state->linux_dmabuf);
    if (!params) {
        fprintf(stderr, "Failed to create dmabuf params.\n");
        return EXIT_FAILURE;
    }

    // Add the file descriptor for the first (and only) plane
    // For multi-planar formats like YUV, you would call this multiple times.
    zwp_linux_buffer_params_v1_add(params,
                                   dmabuf_fd,
                                   0,               // plane_idx
                                   message->offset, // offset
                                   stride,
                                   message->modifiers >> 32,
                                   message->modifiers & 0xffffffff);

    // Create the wl_buffer. The 'created' event is immediate for this version.
    app_state->buffer = zwp_linux_buffer_params_v1_create_immed(params,
                                                                app_state->width,
                                                                app_state->height,
                                                                format,
                                                                0 // flags
    );
    zwp_linux_buffer_params_v1_destroy(params);
    if (!app_state->buffer) {
        fprintf(stderr, "Failed to create wl_buffer from dmabuf.\n");
        return EXIT_FAILURE;
    }

    // The dmabuf fd can be closed after import by the compositor
    close(dmabuf_fd);
    fprintf(stderr, "Created wl_buffer with format %u, stride %u\n", format, stride);

    // Initial draw
    wl_surface_attach(app_state->surface, app_state->buffer, 0, 0);
    wl_surface_damage(app_state->surface, 0, 0, app_state->width, app_state->height);
    wl_surface_commit(app_state->surface);

    return EXIT_SUCCESS;
}

int destroy_window(struct window_state *app_state) {
    printf("Displaying buffer. Close the window to exit.\n");

    if (!app_state)
        return EXIT_FAILURE;

    // 6. Cleanup
    printf("Cleaning up and exiting.\n");
    if (app_state->buffer)
        wl_buffer_destroy(app_state->buffer);
    if (app_state->xdg_toplevel)
        xdg_toplevel_destroy(app_state->xdg_toplevel);
    if (app_state->xdg_surface)
        xdg_surface_destroy(app_state->xdg_surface);
    if (app_state->surface)
        wl_surface_destroy(app_state->surface);
    if (app_state->linux_dmabuf)
        zwp_linux_dmabuf_v1_destroy(app_state->linux_dmabuf);
    if (app_state->xdg_wm_base)
        xdg_wm_base_destroy(app_state->xdg_wm_base);
    if (app_state->compositor)
        wl_compositor_destroy(app_state->compositor);
    if (app_state->registry)
        wl_registry_destroy(app_state->registry);
    if (app_state->display)
        wl_display_disconnect(app_state->display);

    return EXIT_SUCCESS;
}
