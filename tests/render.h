#pragma once

#include <cassert>
#include <fcntl.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <gbm.h>

#define BUFFER_FORMAT DRM_FORMAT_XRGB8888
#define MAX_BUFFER_PLANES 1

#ifndef ARRAY_LENGTH
#define ARRAY_LENGTH(a) (sizeof(a) / sizeof(a)[0])
#endif

struct display {
    uint64_t *modifiers;
    int modifiers_count;

    struct {
        EGLDisplay display;
        EGLContext context;
        EGLConfig conf;
        bool has_dma_buf_import_modifiers;
        bool has_no_config_context;
        PFNEGLQUERYDMABUFMODIFIERSEXTPROC query_dma_buf_modifiers;
        PFNEGLCREATEIMAGEKHRPROC create_image;
        PFNEGLDESTROYIMAGEKHRPROC destroy_image;
        PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d;
        PFNEGLCREATESYNCKHRPROC create_sync;
        PFNEGLDESTROYSYNCKHRPROC destroy_sync;
        PFNEGLCLIENTWAITSYNCKHRPROC client_wait_sync;
        PFNEGLDUPNATIVEFENCEFDANDROIDPROC dup_native_fence_fd;
        PFNEGLWAITSYNCKHRPROC wait_sync;
        PFNEGLEXPORTDMABUFIMAGEMESAPROC export_dmabuf_image_mesa;
        PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC eglExportDMABUFImageQueryMESA;
    } egl;

    struct {
        int drm_fd;
        struct gbm_device *device;
    } gbm;

    struct {
		GLuint program;
		GLuint pos;
		GLuint color;
		GLuint offset_uniform;
	} gl;
};

struct buffer {
    struct display *display;
    int busy;

    struct gbm_bo *bo;

    int width;
    int height;
    int format;
    uint64_t modifier;
    int plane_count;

    int dmabuf_fds[MAX_BUFFER_PLANES];
    uint32_t strides[MAX_BUFFER_PLANES];
    uint32_t offsets[MAX_BUFFER_PLANES];

    EGLImageKHR egl_image;
    GLuint gl_texture;
    GLuint gl_fbo;
};

static void destroy_display(struct display *display) {
    if (display->gbm.device)
        gbm_device_destroy(display->gbm.device);

    if (display->gbm.drm_fd >= 0)
        close(display->gbm.drm_fd);

    if (display->egl.context != EGL_NO_CONTEXT)
        eglDestroyContext(display->egl.display, display->egl.context);

    if (display->egl.display != EGL_NO_DISPLAY)
        eglTerminate(display->egl.display);

    free(display->modifiers);

    free(display);
}

static inline EGLDisplay
weston_platform_get_egl_display(EGLenum platform, void *native_display,
                                const EGLint *attrib_list) {
    static PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display = NULL;

    if (!get_platform_display) {
        get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
            eglGetProcAddress(
                "eglGetPlatformDisplayEXT");
    }

    if (get_platform_display)
        return get_platform_display(platform,
                                    native_display, attrib_list);

    return eglGetDisplay((EGLNativeDisplayType)native_display);
}

static bool
weston_check_egl_extension(const char *extensions, const char *extension) {
    size_t extlen = strlen(extension);
    const char *end = extensions + strlen(extensions);

    while (extensions < end) {
        size_t n = 0;

        /* Skip whitespaces, if any */
        if (*extensions == ' ') {
            extensions++;
            continue;
        }

        n = strcspn(extensions, " ");

        /* Compare strings */
        if (n == extlen && strncmp(extension, extensions, n) == 0)
            return true; /* Found */

        extensions += n;
    }

    /* Not found */
    return false;
}

static bool
display_set_up_egl(struct display *display) {
    static const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE};
    EGLint major, minor, ret, count;
    const char *egl_extensions = NULL;
    const char *gl_extensions = NULL;

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 1,
        EGL_GREEN_SIZE, 1,
        EGL_BLUE_SIZE, 1,
        EGL_ALPHA_SIZE, 1,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE};

    display->egl.display =
        weston_platform_get_egl_display(EGL_PLATFORM_GBM_KHR,
                                        display->gbm.device, NULL);
    if (display->egl.display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Failed to create EGLDisplay\n");
        goto error;
    }

    if (eglInitialize(display->egl.display, &major, &minor) == EGL_FALSE) {
        fprintf(stderr, "Failed to initialize EGLDisplay\n");
        goto error;
    }

    if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
        fprintf(stderr, "Failed to bind OpenGL ES API\n");
        goto error;
    }

    egl_extensions = eglQueryString(display->egl.display, EGL_EXTENSIONS);
    assert(egl_extensions != NULL);

    if (!weston_check_egl_extension(egl_extensions,
                                    "EGL_EXT_image_dma_buf_import")) {
        fprintf(stderr, "EGL_EXT_image_dma_buf_import not supported\n");
        goto error;
    }

    if (!weston_check_egl_extension(egl_extensions,
                                    "EGL_KHR_surfaceless_context")) {
        fprintf(stderr, "EGL_KHR_surfaceless_context not supported\n");
        goto error;
    }

    if (weston_check_egl_extension(egl_extensions,
                                   "EGL_KHR_no_config_context")) {
        display->egl.has_no_config_context = true;
    }

    if (display->egl.has_no_config_context) {
        display->egl.conf = EGL_NO_CONFIG_KHR;
    } else {
        fprintf(stderr,
                "Warning: EGL_KHR_no_config_context not supported\n");
        ret = eglChooseConfig(display->egl.display, config_attribs,
                              &display->egl.conf, 1, &count);
        assert(ret && count >= 1);
    }

    display->egl.context = eglCreateContext(display->egl.display,
                                            display->egl.conf,
                                            EGL_NO_CONTEXT,
                                            context_attribs);
    if (display->egl.context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Failed to create EGLContext\n");
        goto error;
    }

    eglMakeCurrent(display->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   display->egl.context);

    gl_extensions = (const char *)glGetString(GL_EXTENSIONS);
    assert(gl_extensions != NULL);

    if (!weston_check_egl_extension(gl_extensions,
                                    "GL_OES_EGL_image")) {
        fprintf(stderr, "GL_OES_EGL_image not supported\n");
        goto error;
    }

    if (weston_check_egl_extension(egl_extensions,
                                   "EGL_EXT_image_dma_buf_import_modifiers")) {
        display->egl.has_dma_buf_import_modifiers = true;
        display->egl.query_dma_buf_modifiers =
            (PFNEGLQUERYDMABUFMODIFIERSEXTPROC)eglGetProcAddress("eglQueryDmaBufModifiersEXT");
        assert(display->egl.query_dma_buf_modifiers);
    }

    display->egl.create_image =
        (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    assert(display->egl.create_image);

    display->egl.destroy_image =
        (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    assert(display->egl.destroy_image);

    display->egl.image_target_texture_2d =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    assert(display->egl.image_target_texture_2d);

    if (weston_check_egl_extension(egl_extensions, "EGL_KHR_fence_sync") &&
        weston_check_egl_extension(egl_extensions,
                                   "EGL_ANDROID_native_fence_sync")) {
        display->egl.create_sync =
            (PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR");
        assert(display->egl.create_sync);

        display->egl.destroy_sync =
            (PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress("eglDestroySyncKHR");
        assert(display->egl.destroy_sync);

        display->egl.client_wait_sync =
            (PFNEGLCLIENTWAITSYNCKHRPROC)eglGetProcAddress("eglClientWaitSyncKHR");
        assert(display->egl.client_wait_sync);

        display->egl.dup_native_fence_fd =
            (PFNEGLDUPNATIVEFENCEFDANDROIDPROC)eglGetProcAddress("eglDupNativeFenceFDANDROID");
        assert(display->egl.dup_native_fence_fd);
    }

    if (weston_check_egl_extension(egl_extensions,
                                   "EGL_KHR_wait_sync")) {
        display->egl.wait_sync =
            (PFNEGLWAITSYNCKHRPROC)eglGetProcAddress("eglWaitSyncKHR");
        assert(display->egl.wait_sync);
    }

    display->egl.export_dmabuf_image_mesa =
        (PFNEGLEXPORTDMABUFIMAGEMESAPROC)eglGetProcAddress("eglExportDMABUFImageMESA");
    assert(display->egl.export_dmabuf_image_mesa);

    display->egl.eglExportDMABUFImageQueryMESA =
        (PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC)eglGetProcAddress("eglExportDMABUFImageQueryMESA");
    assert(display->egl.eglExportDMABUFImageQueryMESA);

    return true;

error:
    return false;
}

static bool
display_update_supported_modifiers_for_egl(struct display *d) {
    EGLBoolean ret;
    bool try_modifiers = d->egl.has_dma_buf_import_modifiers;

    if (try_modifiers) {
        ret = d->egl.query_dma_buf_modifiers(d->egl.display,
                                             BUFFER_FORMAT,
                                             0,    /* max_modifiers */
                                             NULL, /* modifiers */
                                             NULL, /* external_only */
                                             &d->modifiers_count);
        if (ret == EGL_FALSE) {
            fprintf(stderr, "Failed to query num EGL modifiers for format\n");
            goto error;
        }
    }

    if (!d->modifiers_count)
        try_modifiers = false;

    /* If EGL doesn't support modifiers, don't use them at all. */
    if (!try_modifiers) {
        d->modifiers_count = 0;
        free(d->modifiers);
        d->modifiers = NULL;
        return true;
    }

    d->modifiers = (uint64_t *)calloc(1, d->modifiers_count * sizeof(*d->modifiers));
    ret = d->egl.query_dma_buf_modifiers(d->egl.display,
                                         BUFFER_FORMAT,
                                         d->modifiers_count,
                                         d->modifiers,
                                         NULL, /* external_only */
                                         &d->modifiers_count);
    if (ret == EGL_FALSE) {
        fprintf(stderr, "Failed to query EGL modifiers for format\n");
        goto error;
    }

    return true;

error:

    return false;
}

static bool
display_set_up_gbm(struct display *display, char const *drm_render_node) {
    display->gbm.drm_fd = open(drm_render_node, O_RDWR);
    if (display->gbm.drm_fd < 0) {
        fprintf(stderr, "Failed to open drm render node %s\n",
                drm_render_node);
        return false;
    }

    display->gbm.device = gbm_create_device(display->gbm.drm_fd);
    if (display->gbm.device == NULL) {
        fprintf(stderr, "Failed to create gbm device\n");
        return false;
    }

    return true;
}

static struct display * create_display(char const *drm_render_node) {
    struct display *display;
    display = (struct display *)calloc(1, sizeof *display);

    display->gbm.drm_fd = -1;

    /* GBM needs to be initialized before EGL, so that we have a valid
     * render node gbm_device to create the EGL display from. */
    if (!display_set_up_gbm(display, drm_render_node))
        goto error;

    if (!display_set_up_egl(display))
        goto error;

    if (!display_update_supported_modifiers_for_egl(display))
        goto error;

    /* We use explicit synchronization only if the user hasn't disabled it,
     * the compositor supports it, we can handle fence fds. */
    if (!display->egl.dup_native_fence_fd) {
        fprintf(stderr,
                "Warning: EGL_ANDROID_native_fence_sync not supported,\n"
                "         will not use explicit synchronization\n");
    } else if (!display->egl.wait_sync) {
        fprintf(stderr,
                "Warning: EGL_KHR_wait_sync not supported,\n"
                "         will not use server-side wait\n");
    }

    return display;

error:
    if (display != NULL)
        destroy_display(display);
    return NULL;
}

static bool
create_fbo_for_buffer(struct display *display, struct buffer *buffer) {
    static const int general_attribs = 3;
    static const int plane_attribs = 5;
    static const int entries_per_attrib = 2;
    EGLint attribs[(general_attribs + plane_attribs * MAX_BUFFER_PLANES) *
                       entries_per_attrib +
                   1];
    unsigned int atti = 0;

    attribs[atti++] = EGL_WIDTH;
    attribs[atti++] = buffer->width;
    attribs[atti++] = EGL_HEIGHT;
    attribs[atti++] = buffer->height;
    attribs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
    attribs[atti++] = buffer->format;

#define ADD_PLANE_ATTRIBS(plane_idx)                                          \
    {                                                                         \
        attribs[atti++] = EGL_DMA_BUF_PLANE##plane_idx##_FD_EXT;              \
        attribs[atti++] = buffer->dmabuf_fds[plane_idx];                      \
        attribs[atti++] = EGL_DMA_BUF_PLANE##plane_idx##_OFFSET_EXT;          \
        attribs[atti++] = (int)buffer->offsets[plane_idx];                    \
        attribs[atti++] = EGL_DMA_BUF_PLANE##plane_idx##_PITCH_EXT;           \
        attribs[atti++] = (int)buffer->strides[plane_idx];                    \
        if (display->egl.has_dma_buf_import_modifiers) {                      \
            attribs[atti++] = EGL_DMA_BUF_PLANE##plane_idx##_MODIFIER_LO_EXT; \
            attribs[atti++] = buffer->modifier & 0xFFFFFFFF;                  \
            attribs[atti++] = EGL_DMA_BUF_PLANE##plane_idx##_MODIFIER_HI_EXT; \
            attribs[atti++] = buffer->modifier >> 32;                         \
        }                                                                     \
    }

    if (buffer->plane_count > 0)
        ADD_PLANE_ATTRIBS(0);

    if (buffer->plane_count > 1)
        ADD_PLANE_ATTRIBS(1);

    if (buffer->plane_count > 2)
        ADD_PLANE_ATTRIBS(2);

    if (buffer->plane_count > 3)
        ADD_PLANE_ATTRIBS(3);

#undef ADD_PLANE_ATTRIBS

    attribs[atti] = EGL_NONE;

    assert(atti < ARRAY_LENGTH(attribs));

    buffer->egl_image = display->egl.create_image(display->egl.display,
                                                  EGL_NO_CONTEXT,
                                                  EGL_LINUX_DMA_BUF_EXT,
                                                  NULL, attribs);
    if (buffer->egl_image == EGL_NO_IMAGE_KHR) {
        fprintf(stderr, "EGLImageKHR creation failed\n");
        return false;
    }

    eglMakeCurrent(display->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   display->egl.context);

    glGenTextures(1, &buffer->gl_texture);
    glBindTexture(GL_TEXTURE_2D, buffer->gl_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    display->egl.image_target_texture_2d(GL_TEXTURE_2D, buffer->egl_image);

    glGenFramebuffers(1, &buffer->gl_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, buffer->gl_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, buffer->gl_texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "FBO creation failed\n");
        return false;
    }

    return true;
}

static int create_dmabuf_buffer(struct display *display, struct buffer *buffer) {
    /* Y-Invert the buffer image, since we are going to renderer to the
     * buffer through a FBO. */
    int i;

    if (!buffer->bo) {
        buffer->bo = gbm_bo_create(display->gbm.device,
                                   buffer->width,
                                   buffer->height,
                                   buffer->format,
                                   GBM_BO_USE_RENDERING);
        buffer->modifier = DRM_FORMAT_MOD_INVALID;
    }

    if (!buffer->bo) {
        fprintf(stderr, "create_bo failed\n");
        goto error;
    }

    buffer->plane_count = gbm_bo_get_plane_count(buffer->bo);
    for (i = 0; i < buffer->plane_count; ++i) {
        int ret;
        union gbm_bo_handle handle;

        handle = gbm_bo_get_handle_for_plane(buffer->bo, i);
        if (handle.s32 == -1) {
            fprintf(stderr, "error: failed to get gbm_bo_handle\n");
            goto error;
        }

        ret = drmPrimeHandleToFD(display->gbm.drm_fd, handle.u32, 0,
                                 &buffer->dmabuf_fds[i]);
        if (ret < 0 || buffer->dmabuf_fds[i] < 0) {
            fprintf(stderr, "error: failed to get dmabuf_fd\n");
            goto error;
        }
        buffer->strides[i] = gbm_bo_get_stride_for_plane(buffer->bo, i);
        buffer->offsets[i] = gbm_bo_get_offset(buffer->bo, i);
    }

    printf("DMA-BUF created: fd=%d stride=%d offset=%d\n", buffer->dmabuf_fds[0], buffer->strides[0], buffer->offsets[0]);

    if (!create_fbo_for_buffer(display, buffer))
        goto error;

    return 0;

error:
    return -1;
}

static const char *vert_shader_text =
    "uniform float offset;\n"
    "attribute vec4 pos;\n"
    "attribute vec4 color;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "  gl_Position = pos + vec4(offset, offset, 0.0, 0.0);\n"
    "  v_color = color;\n"
    "}\n";

static const char *frag_shader_text =
    "precision mediump float;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "  gl_FragColor = v_color;\n"
    "}\n";

static GLuint
create_shader(const char *source, GLenum shader_type) {
    GLuint shader;
    GLint status;

    shader = glCreateShader(shader_type);
    assert(shader != 0);

    glShaderSource(shader, 1, (const char **)&source, NULL);
    glCompileShader(shader);

    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[1000];
        GLsizei len;
        glGetShaderInfoLog(shader, 1000, &len, log);
        fprintf(stderr, "Error: compiling %s: %.*s\n",
                shader_type == GL_VERTEX_SHADER ? "vertex" : "fragment",
                len, log);
        return 0;
    }

    return shader;
}

static GLuint
create_and_link_program(GLuint vert, GLuint frag) {
    GLint status;
    GLuint program = glCreateProgram();

    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        char log[1000];
        GLsizei len;
        glGetProgramInfoLog(program, 1000, &len, log);
        fprintf(stderr, "Error: linking:\n%.*s\n", len, log);
        return 0;
    }

    return program;
}

static bool
window_set_up_gl(struct display *display) {
    GLuint vert = create_shader(
        vert_shader_text,
        GL_VERTEX_SHADER);
    GLuint frag = create_shader(
        frag_shader_text,
        GL_FRAGMENT_SHADER);

    display->gl.program = create_and_link_program(vert, frag);

    glDeleteShader(vert);
    glDeleteShader(frag);

    display->gl.pos = glGetAttribLocation(display->gl.program, "pos");
    display->gl.color = glGetAttribLocation(display->gl.program, "color");

    glUseProgram(display->gl.program);

    display->gl.offset_uniform =
        glGetUniformLocation(display->gl.program, "offset");

    return display->gl.program != 0;
}

/* Renders a square moving from the lower left corner to the
 * upper right corner of the window. The square's vertices have
 * the following colors:
 *
 *  green +-----+ yellow
 *        |     |
 *        |     |
 *    red +-----+ blue
 */
static void
render(struct display *display, struct buffer *buffer) {
    /* Complete a movement iteration in 5000 ms. */
    static const uint64_t iteration_ms = 5000;
    static const GLfloat verts[4][2] = {
        {-0.5, -0.5},
        {-0.5, 0.5},
        {0.5, -0.5},
        {0.5, 0.5}};
    static const GLfloat colors[4][3] = {
        {1, 0, 0},
        {0, 1, 0},
        {0, 0, 1},
        {1, 1, 0}};
    GLfloat offset;
    struct timeval tv;
    uint64_t time_ms;

    gettimeofday(&tv, NULL);
    time_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;

    /* Split time_ms in repeating windows of [0, iteration_ms) and map them
     * to offsets in the [-0.5, 0.5) range. */
    offset = (time_ms % iteration_ms) / (float)iteration_ms - 0.5;

    /* Direct all GL draws to the buffer through the FBO */
    glBindFramebuffer(GL_FRAMEBUFFER, buffer->gl_fbo);

    glViewport(0, 0, buffer->width, buffer->height);

    glUniform1f(display->gl.offset_uniform, offset);

    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    glVertexAttribPointer(display->gl.pos, 2, GL_FLOAT, GL_FALSE, 0, verts);
    glVertexAttribPointer(display->gl.color, 3, GL_FLOAT, GL_FALSE, 0, colors);
    glEnableVertexAttribArray(display->gl.pos);
    glEnableVertexAttribArray(display->gl.color);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(display->gl.pos);
    glDisableVertexAttribArray(display->gl.color);
}
