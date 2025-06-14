#include <getopt.h>
#include <cstdio>
#include <stdlib.h>

#include <gst/allocators/gstdmabuf.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/video/gstvideometa.h>

#include <playsocket.h>

#define QUOTE(str) #str
#define EXPAND_AND_QUOTE(str) QUOTE(str)

#define DISPLAY_WIDTH 1920
#define DISPLAY_HEIGHT 1080
#define DISPLAY_REFRESH_RATE 60

#define LIVE_PORT 1111
#define LIVE_HOST "127.0.0.1"

char *SOCKET_PATH = "/tmp/playdroid_socket";

struct display {
    char *socket_path;
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

    int dmabuf_fd;
    int format;
    uint64_t modifier;
    uint32_t stride;
    uint32_t offset;
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
        /* TODO: use encodebin instead of jpegenc */
        if (display->is_live) {
            snprintf(pipeline_str, sizeof(pipeline_str),
                    "rtpbin name=rtpbin "
                    "appsrc name=src ! videoconvert ! "
                    "video/x-raw,format=I420 ! jpegenc ! rtpjpegpay ! "
                    "rtpbin.send_rtp_sink_0 "
                    "rtpbin.send_rtp_src_0 ! "
                    "udpsink name=sink host=%s port=%d "
                    "rtpbin.send_rtcp_src_0 ! "
                    "udpsink host=%s port=%d sync=false async=false "
                    "udpsrc port=%d ! rtpbin.recv_rtcp_sink_0",
                    LIVE_HOST, display->port, LIVE_HOST,
                    display->port + 1, display->port + 2);
        } else {
            snprintf(pipeline_str, sizeof(pipeline_str),
                    "appsrc name=src is-live=true format=time "
                    "! videoconvert ! xvimagesink name=videosink");
        }
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
}

static int gst_output_frame(struct display *display) {
    GstBuffer *buf;
    GstMemory *mem;

    struct timespec current_frame_ts;
    GstClockTime ts, current_frame_time;
    static GstClockTime timestamp = 0;

    // gsize offset = display->offset;
    gsize offset[GST_VIDEO_MAX_PLANES] = {
        0,
    };
    gint stride[GST_VIDEO_MAX_PLANES] = {
        0,
    };

    buf = gst_buffer_new();
    mem = gst_dmabuf_allocator_alloc(display->allocator, display->dmabuf_fd,
                                     display->stride * display->height);
    gst_buffer_append_memory(buf, mem);
    gst_buffer_add_video_meta_full(buf,
                                   GST_VIDEO_FRAME_FLAG_NONE,
                                   GST_VIDEO_FORMAT_BGRx,
                                   display->width,
                                   display->height,
                                   1,
                                   offset,
                                   stride);

    GST_BUFFER_PTS(buf) = timestamp;
    GST_BUFFER_DURATION(buf) = gst_util_uint64_scale_int(1, GST_SECOND, 4);

    timestamp += GST_BUFFER_DURATION(buf);

    // fprintf(stderr, "remoting_output_frame: buffer %p, fd %d, stride %d, offset %zu\n",
    //			   buf, display->dmabuf_fds[0], display->strides[0], display->offsets[0]);
    int ret = gst_app_src_push_buffer((GstAppSrc *)display->appsrc, buf);
    // fprintf(stderr, "remoting_output_frame: buffer fd %d vs img %d \n", display->dmabuf_fds[0], texture_dmabuf_fd);

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
           "\t'-l,--live'"
           "\n\t\tShould live stream to rstp\n"
           "\t'-p,--port=<>'"
           "\n\t\tport to stream to, default is %d\n",
           SOCKET_PATH, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_REFRESH_RATE, LIVE_PORT);
    exit(0);
}

static int is_true(const char *c) {
    if (!strcmp(c, "1"))
        return 1;
    else if (!strcmp(c, "0"))
        return 0;
    else
        print_usage_and_exit();

    return 0;
}

int main(int argc, char **argv) {
    int c, option_index = 0;
    struct display *display = NULL;

    display = (struct display *) malloc(sizeof *display);
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
        {"live", no_argument, 0, 'l'},
        {"port", required_argument, 0, 'p'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};

    while ((c = getopt_long(argc, argv, "hs:w:y:r:p:l",
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
            display->is_live = is_true(optarg);
            break;
        case 'p':
            display->port = strtol(optarg, NULL, 10);
            if (display->port <= 0 || display->port > 65535) {
                fprintf(stderr, "Invalid port number: %d\n", display->port);
                exit(EXIT_FAILURE);
            }
            break;
        default:
            print_usage_and_exit();
        }
    }

    printf("This is project %s, version %s.\n", EXPAND_AND_QUOTE(PROJECT_NAME), EXPAND_AND_QUOTE(PROJECT_VERSION));

    int sock = create_socket(display->socket_path);
    while (1) {
        MessageType type;
        struct MessageData message;
        int dmabuf_fd;

        fprintf(stderr, "Waiting for message...\n");

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
                gst_pipeline_deinit(display);
                gst_pipeline_init(display);
                // Reset Everything
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
                reply.refresh_rate = display->refresh_rate;
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
            display->dmabuf_fd = dmabuf_fd;
            display->format = message.format;
            display->modifier = message.modifiers;
            display->stride = message.stride;
            display->offset = message.offset;

            gst_output_frame(display);
            break;
        default:
            printf("Unknown message type\n");
            break;
        }
    }
    close(sock);

    return 0;
}
