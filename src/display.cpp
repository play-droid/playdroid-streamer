#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include <display.h>
#include <playsocket.h>
#include <wayland-window.h>
#include <gsthelper.h>


void handle_message(struct display *display, int sock, MessageType type, MessageData *message, int dmabuf_fd, struct gsthelper *gsthelper) {
    switch (type) {
        case MSG_TYPE_DATA:
            if (message->type == MSG_HELLO) {
                printf("Got hello message\n");
                if (display->open_wayland_window) {
                    setup_window(display->wayland_state);
                } else {
                    gst_pipeline_deinit(gsthelper);
                    gst_pipeline_init(gsthelper, display->width, display->height, display->refresh_rate);
                }
            }
            break;
        case MSG_TYPE_DATA_NEEDS_REPLY:
            if (message->type == MSG_ASK_FOR_RESOLUTION) {
                printf("Got ask for resolution message\n");
                // Respond with resolution
                struct MessageData reply;
                reply.type = MSG_HAVE_RESOLUTION;
                reply.width = display->width;
                reply.height = display->height;
                reply.refresh_rate = display->refresh_rate * 1000; // Convert to ms
                send_message(sock, -1, MSG_TYPE_DATA_REPLY, &reply);
            }
            break;
        case MSG_TYPE_FD:
            /*printf("Got FD: %d\n", dmabuf_fd);
            printf("Got data: format: %d, stride: %d, offset: %d\n",
                   message->format,
                   message->stride,
                   message->offset);*/

            if (dmabuf_fd < 0) {
                fprintf(stderr, "Invalid dmabuf_fd: %d\n", dmabuf_fd);
                break;
            }

            if (display->open_wayland_window) {
                draw_window(display->wayland_state, message, dmabuf_fd);
                close(dmabuf_fd); 
            } else {
                gst_output_frame(gsthelper, dmabuf_fd, display->width, display->height, display->refresh_rate, message->offset, message->stride);
            }

            break;
        default:
            printf("Unknown message type\n");
            break;
    }
}

void init_display(struct display *display) {
    display->socket_path = DISPLAY_SOCKET_PATH;
    display->width = DISPLAY_WIDTH;
    display->height = DISPLAY_HEIGHT;
    display->refresh_rate = DISPLAY_REFRESH_RATE;
    display->open_wayland_window = false;
    display->wayland_state = nullptr;
}

void run_display(struct display *display, struct gsthelper *gsthelper) {
    if (display->open_wayland_window)
        display->wayland_state = setup_wayland_window();

    int sock = create_socket(display->socket_path);

    while (true) {
        MessageType type;
        MessageData message;
        int dmabuf_fd;

        if (recv_message(sock, &dmabuf_fd, &message, &type) == 0) {
            fprintf(stderr, "recv_message closed\n");
            close(sock);
            sock = create_socket(display->socket_path);
            continue;
        }

        handle_message(display, sock, type, &message, dmabuf_fd, gsthelper);
    }

    close(sock);
}
