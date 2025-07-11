#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include <gsthelper.h>
#include <input.h>
#include <xkbcommon/xkbcommon.h>

static gboolean gst_video_src_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    gboolean ret = FALSE;
    const gchar *key;
    gint button;
    guint id;
    gdouble x, y, delta_x, delta_y, pressure;
    GstNavigationEventType type = GST_NAVIGATION_EVENT_INVALID;
    struct input *input = (struct input *)gst_pad_get_element_private(pad);

    if (!input) {
        fprintf(stderr, "Input is NULL in gst_video_src_event\n");
        goto out;
    }

    if (GST_EVENT_TYPE(event) != GST_EVENT_NAVIGATION) {
        goto out;
    }

    type = gst_navigation_event_get_type(event);
    switch (type) {
        case GST_NAVIGATION_EVENT_KEY_PRESS:
        case GST_NAVIGATION_EVENT_KEY_RELEASE:
            if (gst_navigation_event_parse_key_event(event, &key)) {
                uint32_t keysym = (uint32_t) xkb_keysym_from_name(key, XKB_KEYSYM_NO_FLAGS);
                keyboard_handle_key(input, keysym, type == GST_NAVIGATION_EVENT_KEY_PRESS ? 1 : 0);
                ret = TRUE;
            }
            break;
        case GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS:
        case GST_NAVIGATION_EVENT_MOUSE_BUTTON_RELEASE:
            if (gst_navigation_event_parse_mouse_button_event(event, &button, &x, &y)) {
                pointer_handle_button(input, button, type == GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS ? 1 : 0);
                ret = TRUE;
            }
            break;
        case GST_NAVIGATION_EVENT_MOUSE_MOVE:
            if (gst_navigation_event_parse_mouse_move_event(event, &x, &y)) {
                pointer_handle_motion(input, x, y);
                ret = TRUE;
            }
            break;
        case GST_NAVIGATION_EVENT_MOUSE_SCROLL:
            if (gst_navigation_event_parse_mouse_scroll_event(event, &x, &y, &delta_x, &delta_y)) {
                if (delta_x) {
                    pointer_handle_axis(input, 1, delta_x);
                } else {
                    pointer_handle_axis(input, 0, delta_y);
                }
                ret = TRUE;
            }
            break;
        case GST_NAVIGATION_EVENT_TOUCH_DOWN:
            if (gst_navigation_event_parse_touch_event(event, &id, &x, &y, &pressure)) {
                touch_handle_down(input, id, x, y, pressure);
                ret = TRUE;
            }
            break;
        case GST_NAVIGATION_EVENT_TOUCH_MOTION:
            if (gst_navigation_event_parse_touch_event(event, &id, &x, &y, &pressure)) {
                touch_handle_motion(input, id, x, y, pressure);
                ret = TRUE;
            }
            break;
        case GST_NAVIGATION_EVENT_TOUCH_UP:
            if (gst_navigation_event_parse_touch_event(event, &id, &x, &y, &pressure)) {
                touch_handle_up(input, id);
                ret = TRUE;
            }
            break;
        case GST_NAVIGATION_EVENT_TOUCH_CANCEL:
            touch_handle_cancel(input);
            ret = TRUE;
            break;
        case GST_NAVIGATION_EVENT_TOUCH_FRAME:
        case GST_NAVIGATION_EVENT_COMMAND:
        case GST_NAVIGATION_EVENT_INVALID:
        default:
            break;
    }

out:
    if (!ret) {
        ret = gst_pad_event_default(pad, parent, event);
    } else {
        gst_event_unref(event);
    }
    return ret;
}

static void cb_need_data (GstElement *, guint , gpointer user_data) {
    struct gsthelper *gsthelper = (struct gsthelper *)user_data;

    gsthelper->want_data = true;
}

static void cb_enough_data (GstElement *, gpointer user_data) {
    struct gsthelper *gsthelper = (struct gsthelper *)user_data;

    gsthelper->want_data = false;
}

int gst_pipeline_init(struct gsthelper *gsthelper, int width, int height, int refresh_rate, struct input *input) {
    GstCaps *caps;
    GError *err = NULL;
    GstStateChangeReturn ret;
    GstPad *pad;

    if (!gst_init_check(NULL, NULL, &err)) {
        fprintf(stderr, "GStreamer initialization error: %s\n",
                err->message);
        g_error_free(err);
        return -1;
    }

    fprintf(stderr, "GStreamer initialization\n");
    gsthelper->allocator = gst_dmabuf_allocator_new();

    if (!gsthelper->gst_pipeline) {
        char pipeline_str[1024];
        snprintf(pipeline_str, sizeof(pipeline_str),
                 "appsrc name=src "
                 " ! vaapipostproc ! vaapivp9enc ! webrtcsink signaller::uri=\"ws://localhost:8443\" enable-control-data-channel=true name=sink");
        gsthelper->gst_pipeline = strdup(pipeline_str);
    }
    fprintf(stderr, "GST pipeline: %s\n", gsthelper->gst_pipeline);

    gsthelper->pipeline = gst_parse_launch(gsthelper->gst_pipeline, &err);
    if (!gsthelper->pipeline) {
        fprintf(stderr, "Could not create gstreamer pipeline. Error: %s\n",
                err->message);
        g_error_free(err);
        return -1;
    }

    gsthelper->appsrc = (GstAppSrc *)
        gst_bin_get_by_name(GST_BIN(gsthelper->pipeline), "src");
    if (!gsthelper->appsrc) {
        fprintf(stderr, "Could not get appsrc from gstreamer pipeline\n");
        goto err;
    }

    caps = gst_caps_new_simple("video/x-raw",
                               "format", G_TYPE_STRING,
                               "BGRx",
                               "width", G_TYPE_INT, width,
                               "height", G_TYPE_INT, height,
                               "framerate", GST_TYPE_FRACTION,
                               refresh_rate, 1,
                               NULL);
    if (!caps) {
        fprintf(stderr, "Could not create gstreamer caps.\n");
        goto err;
    }

    g_object_set(G_OBJECT(gsthelper->appsrc),
                 "caps", caps,
                 "stream-type", 0,
                 "format", GST_FORMAT_TIME,
                 "is-live", TRUE,
                 NULL);
    gst_caps_unref(caps);

    gsthelper->bus = gst_pipeline_get_bus(GST_PIPELINE(gsthelper->pipeline));
    if (!gsthelper->bus) {
        fprintf(stderr, "Could not get bus from gstreamer pipeline\n");
        goto err;
    }
    /*gst_bus_set_sync_handler(gsthelper->bus, remoting_gst_bus_sync_handler,
                             &gsthelper->gstpipe, NULL);*/

    g_signal_connect (gsthelper->appsrc, "need-data", G_CALLBACK (cb_need_data), gsthelper);
    g_signal_connect (gsthelper->appsrc, "enough-data", G_CALLBACK (cb_enough_data), gsthelper);

    pad = gst_element_get_static_pad (GST_ELEMENT_CAST(gst_bin_get_by_name(GST_BIN(gsthelper->pipeline), "src")), "src");
    gst_pad_set_element_private(pad, input);
    gst_pad_set_event_function_full(pad, gst_video_src_event, input, NULL);

    ret = gst_element_set_state(gsthelper->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        fprintf(stderr, "Couldn't set GST_STATE_PLAYING to pipeline\n");
        goto err;
    }

    return 0;

err:
    gst_object_unref(GST_OBJECT(gsthelper->pipeline));
    gsthelper->pipeline = NULL;
    return -1;
}

void gst_pipeline_deinit(struct gsthelper *gsthelper) {
    if (!gsthelper->pipeline)
        return;

    gst_element_set_state(gsthelper->pipeline, GST_STATE_NULL);
    if (gsthelper->bus)
        gst_object_unref(GST_OBJECT(gsthelper->bus));
    gst_object_unref(GST_OBJECT(gsthelper->pipeline));
    gsthelper->pipeline = NULL;
}

void gst_output_frame(struct gsthelper *gsthelper, int dmabuf_fd, int width, int height, int refresh_rate, gsize offset, gint stride) {
    GstBuffer *buf;
    GstMemory *mem;

    if(!gsthelper->want_data) {
        close(dmabuf_fd);
        return;
    }

    gsize offsets[GST_VIDEO_MAX_PLANES] = {
        offset,
    };
    gint strides[GST_VIDEO_MAX_PLANES] = {
        stride,
    };

    buf = gst_buffer_new();
    mem = gst_dmabuf_allocator_alloc(gsthelper->allocator, dmabuf_fd,
                                     stride * height);
    gst_buffer_append_memory(buf, mem);
    gst_buffer_add_video_meta_full(buf,
                                   GST_VIDEO_FRAME_FLAG_NONE,
                                   GST_VIDEO_FORMAT_BGRx,
                                   width,
                                   height,
                                   1,
                                   offsets,
                                   strides);

    GstClock *clock = gst_element_get_clock(GST_ELEMENT(gsthelper->pipeline));
    GstClockTime base_time = gst_element_get_base_time(GST_ELEMENT(gsthelper->pipeline));
    GstClockTime now = gst_clock_get_time(clock);
    GstClockTime running_time = now - base_time;
    gst_object_unref(clock);

    GST_BUFFER_PTS(buf) = running_time;
    GST_BUFFER_DURATION(buf) = gst_util_uint64_scale_int(1, GST_SECOND, refresh_rate);

    int ret = gst_app_src_push_buffer((GstAppSrc *)gsthelper->appsrc, buf);
    if (ret != GST_FLOW_OK) {
        /* something wrong, stop pushing */
        fprintf(stderr, "Error: gst_app_src_push_buffer failed: %d\n", ret);
    }
}
