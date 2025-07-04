#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include <gsthelper.h>

static gboolean gst_video_src_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    gboolean ret = FALSE;

    if (GST_EVENT_TYPE(event) == GST_EVENT_NAVIGATION) {
        fprintf(stderr, "Got GST_EVENT_NAVIGATION\n");
    }
        /*
        TODO: Kanged from WPE for reference, needs to be adapted
        const gchar *key;
        gint button;
        gdouble x, y, delta_x, delta_y;
    
        switch (gst_navigation_event_get_type (event)) {
          case GST_NAVIGATION_EVENT_KEY_PRESS:
          case GST_NAVIGATION_EVENT_KEY_RELEASE:
            if (gst_navigation_event_parse_key_event (event, &key)) {
              uint32_t keysym =
                  (uint32_t) xkb_keysym_from_name (key, XKB_KEYSYM_NO_FLAGS);
              struct wpe_input_keyboard_event wpe_event;
              wpe_event.key_code = keysym;
              wpe_event.pressed =
                  gst_navigation_event_get_type (event) ==
                  GST_NAVIGATION_EVENT_KEY_PRESS;
              src->view->dispatchKeyboardEvent (wpe_event);
              ret = TRUE;
            }
            break;
          case GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS:
          case GST_NAVIGATION_EVENT_MOUSE_BUTTON_RELEASE:
            if (gst_navigation_event_parse_mouse_button_event (event, &button, &x,
                    &y)) {
              struct wpe_input_pointer_event wpe_event;
              wpe_event.time = GST_TIME_AS_MSECONDS (GST_EVENT_TIMESTAMP (event));
              wpe_event.type = wpe_input_pointer_event_type_button;
              wpe_event.x = (int) x;
              wpe_event.y = (int) y;
              if (button == 1) {
                wpe_event.modifiers = wpe_input_pointer_modifier_button1;
              } else if (button == 2) {
                wpe_event.modifiers = wpe_input_pointer_modifier_button2;
              } else if (button == 3) {
                wpe_event.modifiers = wpe_input_pointer_modifier_button3;
              } else if (button == 4) {
                wpe_event.modifiers = wpe_input_pointer_modifier_button4;
              } else if (button == 5) {
                wpe_event.modifiers = wpe_input_pointer_modifier_button5;
              }
              wpe_event.button = button;
              wpe_event.state =
                  gst_navigation_event_get_type (event) ==
                  GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS;
              src->view->dispatchPointerEvent (wpe_event);
              ret = TRUE;
            }
            break;
          case GST_NAVIGATION_EVENT_MOUSE_MOVE:
            if (gst_navigation_event_parse_mouse_move_event (event, &x, &y)) {
              struct wpe_input_pointer_event wpe_event;
              wpe_event.time = GST_TIME_AS_MSECONDS (GST_EVENT_TIMESTAMP (event));
              wpe_event.type = wpe_input_pointer_event_type_motion;
              wpe_event.x = (int) x;
              wpe_event.y = (int) y;
              src->view->dispatchPointerEvent (wpe_event);
              ret = TRUE;
            }
            break;
          case GST_NAVIGATION_EVENT_MOUSE_SCROLL:
            if (gst_navigation_event_parse_mouse_scroll_event (event, &x, &y,
                    &delta_x, &delta_y)) {
              struct wpe_input_axis_event wpe_event;
              if (delta_x) {
                wpe_event.axis = 1;
                wpe_event.value = delta_x;
              } else {
                wpe_event.axis = 0;
                wpe_event.value = delta_y;
              }
              wpe_event.time = GST_TIME_AS_MSECONDS (GST_EVENT_TIMESTAMP (event));
              wpe_event.type = wpe_input_axis_event_type_motion;
              wpe_event.x = (int) x;
              wpe_event.y = (int) y;
              src->view->dispatchAxisEvent (wpe_event);
              ret = TRUE;
            }
            break;
          default:
            break;
        }
      }*/
    
    if (!ret) {
        ret = gst_pad_event_default(pad, parent, event);
    } else {
        gst_event_unref(event);
    }
    return ret;
}

int gst_pipeline_init(struct gsthelper *gsthelper, int width, int height, int refresh_rate) {
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
                 " ! webrtcsink signaller::uri=\"ws://192.168.64.1:8443\" enable-control-data-channel=true name=sink");
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
     
                             
    pad = gst_element_get_static_pad (GST_ELEMENT_CAST(gst_bin_get_by_name(GST_BIN(gsthelper->pipeline), "src")), "src");
    gst_pad_set_event_function_full(pad, gst_video_src_event, gsthelper, NULL);

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
