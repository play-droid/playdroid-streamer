#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <drm_fourcc.h>
#include <stdio.h>

#include <playsocket.h>

#include "render.h"
#include <wayland-window.h>

#define SOCKET_PATH "/run/user/1000/playdroid_socket"

int main(int argc, char **argv) {
    if(argc != 1) {
        printf("%s takes no arguments.\n", argv[0]);
        return 1;
    }

    int sock = connect_socket(SOCKET_PATH);

    struct MessageData message;
    message.type = MSG_HELLO;
    send_message(sock, -1, MSG_TYPE_DATA, &message);

    message.type = MSG_ASK_FOR_RESOLUTION;
    send_message(sock, -1, MSG_TYPE_DATA_NEEDS_REPLY, &message);

    MessageType type;
    recv_message(sock, NULL, &message, &type);
    if (type != MSG_TYPE_DATA_REPLY || message.type != MSG_HAVE_RESOLUTION) {
        fprintf(stderr, "Expected resolution reply, got type %d, message type %d\n", type, message.type);
        return 1;
    }
    printf("Got resolution: %dx%d@%dHz\n", message.width, message.height, message.refresh_rate);

    struct display *display = create_display("/dev/dri/renderD128");

    struct buffer *buffer;
    buffer = (struct buffer *)calloc(1, sizeof *buffer);
    buffer->display = display;
    buffer->width = message.width;
    buffer->height = message.height;
    buffer->format = BUFFER_FORMAT;

    create_dmabuf_buffer(display, buffer);
    window_set_up_gl(display);

    struct window_state *wayland_state = setup_wayland_window();
    setup_window(wayland_state);

    message.type = MSG_HAVE_BUFFER;
    message.format = buffer->format;
    message.modifiers = buffer->modifier;
    message.stride = buffer->strides[0];
    message.offset = buffer->offsets[0];

    while (1) {
        render(display, buffer);
        glFinish();

        //draw_window(wayland_state, &message, buffer->dmabuf_fds[0]);

        //fprintf(stderr, "Sending message with fd %d\n", dma_buf_fd);
        send_message(sock, buffer->dmabuf_fds[0], MSG_TYPE_FD, &message);

        usleep(6000000 / (message.refresh_rate / 1000)); // Sleep to match refresh rate
    }

    close(sock);

    return 0;
}
