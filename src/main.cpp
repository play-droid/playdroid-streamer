#include <cstdio>
#include <getopt.h>
#include <stdlib.h>
#include <thread>

#include <gst/allocators/gstdmabuf.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/video/gstvideometa.h>

#include <playsocket.h>

#include <wayland-window.h>

#define QUOTE(str) #str
#define EXPAND_AND_QUOTE(str) QUOTE(str)

#define DISPLAY_WIDTH 1920
#define DISPLAY_HEIGHT 1080
#define DISPLAY_REFRESH_RATE 60

#define LIVE_PORT 1111
#define LIVE_HOST "127.0.0.1"

const char *SOCKET_PATH = "/tmp/playdroid_socket";

struct display {
    const char *socket_path;
    int port;

    GstAllocator *allocator;
    char *gst_pipeline;
    GstElement *pipeline;
    GstAppSrc *appsrc;
    GstBus *bus;
    GstClockTime start_time;
    bool is_live;

    int width;
    int height;
    int refresh_rate;

    struct window_state *wayland_state;
    bool open_wayland_window;
};

static int gst_pipeline_init(struct display *display) {
    GstCaps *caps;
    GError *err = NULL;
    GstStateChangeReturn ret;

    if (!gst_init_check(NULL, NULL, &err)) {
        fprintf(stderr, "GStreamer initialization error: %s\n",
                err->message);
        g_error_free(err);
        return -1;
    }

    fprintf(stderr, "GStreamer initialization\n");
    display->allocator = gst_dmabuf_allocator_new();

    if (!display->gst_pipeline) {
        char pipeline_str[1024];
        snprintf(pipeline_str, sizeof(pipeline_str),
                 "appsrc name=src is-live=true format=time "
                 "! waylandsink name=videosink");
        display->gst_pipeline = strdup(pipeline_str);
    }
    fprintf(stderr, "GST pipeline: %s\n", display->gst_pipeline);

    display->pipeline = gst_parse_launch(display->gst_pipeline, &err);
    if (!display->pipeline) {
        fprintf(stderr, "Could not create gstreamer pipeline. Error: %s\n",
                err->message);
        g_error_free(err);
        return -1;
    }

    display->appsrc = (GstAppSrc *)
        gst_bin_get_by_name(GST_BIN(display->pipeline), "src");
    if (!display->appsrc) {
        fprintf(stderr, "Could not get appsrc from gstreamer pipeline\n");
        goto err;
    }

    /* check sink */
    if (display->is_live && !gst_bin_get_by_name(GST_BIN(display->pipeline), "sink")) {
        fprintf(stderr, "Could not get sink from gstreamer pipeline\n");
        goto err;
    }

    caps = gst_caps_new_simple("video/x-raw",
                               "format", G_TYPE_STRING,
                               "BGRx",
                               "width", G_TYPE_INT, display->width,
                               "height", G_TYPE_INT, display->height,
                               "framerate", GST_TYPE_FRACTION,
                               display->refresh_rate, 1,
                               NULL);
    if (!caps) {
        fprintf(stderr, "Could not create gstreamer caps.\n");
        goto err;
    }

    g_object_set(G_OBJECT(display->appsrc),
                 "caps", caps,
                 "stream-type", 0,
                 "format", GST_FORMAT_TIME,
                 "is-live", TRUE,
                 NULL);
    gst_caps_unref(caps);

    display->bus = gst_pipeline_get_bus(GST_PIPELINE(display->pipeline));
    if (!display->bus) {
        fprintf(stderr, "Could not get bus from gstreamer pipeline\n");
        goto err;
    }
    /*gst_bus_set_sync_handler(display->bus, remoting_gst_bus_sync_handler,
                             &display->gstpipe, NULL);*/

    // display->start_time = 0;
    ret = gst_element_set_state(display->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        fprintf(stderr, "Couldn't set GST_STATE_PLAYING to pipeline\n");
        goto err;
    }

    return 0;

err:
    gst_object_unref(GST_OBJECT(display->pipeline));
    display->pipeline = NULL;
    return -1;
}

static void gst_pipeline_deinit(struct display *display) {
    if (!display->pipeline)
        return;

    gst_element_set_state(display->pipeline, GST_STATE_NULL);
    if (display->bus)
        gst_object_unref(GST_OBJECT(display->bus));
    gst_object_unref(GST_OBJECT(display->pipeline));
    display->pipeline = NULL;
    display->start_time = 0;
}

static int gst_output_frame(struct display *display, struct MessageData *message, int dmabuf_fd) {
    GstBuffer *buf;
    GstMemory *mem;

    struct timespec current_frame_ts;
    GstClockTime ts, current_frame_time;

    gsize offset[GST_VIDEO_MAX_PLANES] = {
        (gsize)message->offset,
    };
    gint stride[GST_VIDEO_MAX_PLANES] = {
        (gint)message->stride,
    };

    buf = gst_buffer_new();
    mem = gst_dmabuf_allocator_alloc(display->allocator, dmabuf_fd,
                                     message->stride * message->height);
    gst_buffer_append_memory(buf, mem);
    gst_buffer_add_video_meta_full(buf,
                                   GST_VIDEO_FRAME_FLAG_NONE,
                                   GST_VIDEO_FORMAT_BGRx,
                                   message->width,
                                   message->height,
                                   1,
                                   offset,
                                   stride);

    clock_gettime(CLOCK_REALTIME, &current_frame_ts);

    current_frame_time = GST_TIMESPEC_TO_TIME(current_frame_ts);
    if (display->start_time == 0)
        display->start_time = current_frame_time;
    ts = current_frame_time - display->start_time;

    if (GST_CLOCK_TIME_IS_VALID(ts)) {
        GST_BUFFER_PTS(buf) = ts;

    } else
        GST_BUFFER_PTS(buf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(buf) = GST_CLOCK_TIME_NONE;

    int ret = gst_app_src_push_buffer((GstAppSrc *)display->appsrc, buf);
    if (ret != GST_FLOW_OK) {
        /* something wrong, stop pushing */
        fprintf(stderr, "Error: gst_app_src_push_buffer failed: %d\n", ret);
    }

    return 0;
}

static void print_usage_and_exit(void) {
    printf("usage flags:\n"
           "\t'-s,--socket=<>'"
           "\n\t\tsocket path, default is %s\n"
           "\t'-w,--width=<>'"
           "\n\t\twidth of screen, default is %d\n"
           "\t'-y,--height=<>'"
           "\n\t\theight of screen, default is %d\n"
           "\t'-r,--refresh-rate=<>'"
           "\n\t\trefresh rate of display, default is %d\n"
           "\t'-l,--gst-pipeline=<>'"
           "\n\t\tCustom GST pipeline, default is wayland\n"
           "\t'-p,--port=<>'"
           "\n\t\tport to stream to, default is %d\n"
           "\t'-a,--wayland-window'"
           "\n\t\tOpen Real wayland window\n",
           SOCKET_PATH, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_REFRESH_RATE, LIVE_PORT);
    exit(0);
}

int main(int argc, char **argv) {
    int c, option_index = 0;
    struct display *display = NULL;

    display = (struct display *)calloc(1, sizeof *display);
    if (display == NULL) {
        fprintf(stderr, "out of memory\n");
    }
    display->socket_path = SOCKET_PATH;
    display->width = DISPLAY_WIDTH;
    display->height = DISPLAY_HEIGHT;
    display->refresh_rate = DISPLAY_REFRESH_RATE;
    display->is_live = false;
    display->port = LIVE_PORT;

    static struct option long_options[] = {
        {"socket", required_argument, 0, 's'},
        {"width", required_argument, 0, 'w'},
        {"height", required_argument, 0, 'y'},
        {"refresh-rate", required_argument, 0, 'r'},
        {"gst-pipeline", required_argument, 0, 'l'},
        {"port", required_argument, 0, 'p'},
        {"wayland-window", no_argument, 0, 'a'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};

    while ((c = getopt_long(argc, argv, "hs:w:y:r:p:l:a",
                            long_options, &option_index)) != -1) {
        switch (c) {
        case 's':
            display->socket_path = optarg;
            if (display->socket_path == NULL || strlen(display->socket_path) == 0) {
                fprintf(stderr, "Invalid socket path: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case 'w':
            display->width = strtol(optarg, NULL, 10);
            break;
        case 'y':
            display->height = strtol(optarg, NULL, 10);
            break;
        case 'r':
            display->refresh_rate = strtol(optarg, NULL, 10);
            break;
        case 'l':
            display->gst_pipeline = optarg;
            break;
        case 'p':
            display->port = strtol(optarg, NULL, 10);
            if (display->port <= 0 || display->port > 65535) {
                fprintf(stderr, "Invalid port number: %d\n", display->port);
                exit(EXIT_FAILURE);
            }
            break;
        case 'a':
            display->open_wayland_window = true;
            break;
        default:
            print_usage_and_exit();
        }
    }

    printf("This is project %s, version %s.\n", EXPAND_AND_QUOTE(PROJECT_NAME), EXPAND_AND_QUOTE(PROJECT_VERSION));
    if (display->open_wayland_window) {
        printf("Opening wayland window\n");
        display->wayland_state = setup_wayland_window();
    }

    int sock = create_socket(display->socket_path);
    while (1) {
        MessageType type;
        struct MessageData message;
        int dmabuf_fd;

        int ret = recv_message(sock, &dmabuf_fd, &message, &type);
        if (ret == 0) {
            fprintf(stderr, "recv_message closed\n");
            close(sock);
            sock = create_socket(display->socket_path);
            continue;
        }
        switch (type) {
        case MSG_TYPE_DATA:
            if (message.type == MSG_HELLO) {
                printf("Got hello message\n");
                if (display->open_wayland_window) {
                    setup_window(display->wayland_state);
                } else {
                    gst_pipeline_deinit(display);
                    gst_pipeline_init(display);
                }
            }
            break;
        case MSG_TYPE_DATA_NEEDS_REPLY:
            if (message.type == MSG_ASK_FOR_RESOLUTION) {
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
            printf("Got FD: %d\n", dmabuf_fd);
            printf("Got data: format: %d, stride: %d, offset: %d\n",
                   message.format,
                   message.stride,
                   message.offset);

            if (dmabuf_fd < 0) {
                fprintf(stderr, "Invalid dmabuf_fd: %d\n", dmabuf_fd);
                break;
            }

            if (display->open_wayland_window) {
                std::thread(draw_window, display->wayland_state, &message, dmabuf_fd).detach();
            } else {
                std::thread(gst_output_frame, display, &message, dmabuf_fd).detach();
            }

            break;
        default:
            printf("Unknown message type\n");
            break;
        }
    }
    close(sock);

    return 0;
}
