#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <drm_fourcc.h>
#include <stdio.h>
#include <thread>

#include <playsocket.h>

#include "render.h"
#include <wayland-window.h>

#define DEF_SOCKET_PATH "/tmp/playdroid_socket"

int main(int argc, char **argv) {
    if(argc > 2) {
        printf("%s takes no more than 1 argument.\n", argv[0]);
        return 1;
    }

    const char *socket_path = DEF_SOCKET_PATH;
    if (argc == 2) {
        socket_path = argv[1];
    }

    int sock = connect_socket(socket_path);

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

    const int num_buffers = 3;
    struct buffer *buffers[num_buffers];
    for (int i = 0; i < num_buffers; ++i) {
        buffers[i] = (struct buffer *)calloc(1, sizeof *buffers[i]);
        buffers[i]->display = display;
        buffers[i]->width = message.width;
        buffers[i]->height = message.height;
        buffers[i]->format = BUFFER_FORMAT;

        if (create_dmabuf_buffer(display, buffers[i]) < 0) {
            fprintf(stderr, "Failed to create dmabuf buffer %d\n", i);
            return 1;
        }
    }

    window_set_up_gl(display);

    //struct window_state *wayland_state = setup_wayland_window();
    //setup_window(wayland_state);

    message.type = MSG_HAVE_BUFFER;
    message.format = buffers[0]->format;
    message.modifiers = buffers[0]->modifier;
    message.stride = buffers[0]->strides[0];
    message.offset = buffers[0]->offsets[0];

    int current = 0;

    while (1) {
        buffer *buffer = buffers[current];
        render(display, buffer);
        glFinish();

        //draw_window(wayland_state, &message, buffer->dmabuf_fds[0]);

        //fprintf(stderr, "Sending message with fd %d\n", buffer->dmabuf_fds[0]);
        send_message(sock, buffer->dmabuf_fds[0], MSG_TYPE_FD, &message);

        current = (current + 1) % num_buffers;

        std::this_thread::sleep_for(std::chrono::microseconds(1000000 / message.refresh_rate / 1000)); // Convert Hz to microseconds
    }

    for (int i = 0; i < num_buffers; ++i) {
        close(buffers[i]->dmabuf_fds[0]);
        free(buffers[i]);
    }
    close(sock);

    return 0;
}
