#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include <gsthelper.h>

int gst_pipeline_init(struct gsthelper *gsthelper, int width, int height, int refresh_rate) {
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
    gsthelper->allocator = gst_dmabuf_allocator_new();

    if (!gsthelper->gst_pipeline) {
        char pipeline_str[1024];
        snprintf(pipeline_str, sizeof(pipeline_str),
                 "appsrc name=src is-live=true format=time "
                 "! waylandsink name=videosink");
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
