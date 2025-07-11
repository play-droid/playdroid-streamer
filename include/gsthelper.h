#pragma once

#include <gst/allocators/gstdmabuf.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/video/gstvideometa.h>

struct gsthelper {
    GstAllocator *allocator;
    char *gst_pipeline;
    GstElement *pipeline;
    GstAppSrc *appsrc;
    GstBus *bus;

    bool want_data;
};

int gst_pipeline_init(struct gsthelper *gsthelper, int width, int height, int refresh_rate, struct input *input);
void gst_pipeline_deinit(struct gsthelper *gsthelper);
void gst_output_frame(struct gsthelper *gsthelper, int dmabuf_fd, int width, int height, int refresh_rate, gsize offset, gint stride);
