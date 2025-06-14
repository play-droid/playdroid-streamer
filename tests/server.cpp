#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <playsocket.h>

#include "render.h"

#define SOCKET_PATH "/tmp/playdroid_socket"

// These extensions are usually loaded via eglGetProcAddress
PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = NULL;
PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = NULL;
PFNEGLQUERYDMABUFFORMATSEXTPROC eglQueryDmaBufFormatsEXT = NULL;
PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC eglExportDMABUFImageQueryMESA = NULL;
PFNEGLEXPORTDMABUFIMAGEMESAPROC eglExportDMABUFImageMESA = NULL;

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

    // 1. Initialize EGL (platform specific, assumes EGL_DEFAULT_DISPLAY)
    EGLDisplay egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(egl_display, NULL, NULL);

    // 2. Choose EGL config
    EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_NONE};
    EGLConfig config;
    EGLint num_configs;
    eglChooseConfig(egl_display, attribs, &config, 1, &num_configs);

    // 3. Create Pbuffer surface
    EGLint pbuf_attribs[] = {
        EGL_WIDTH,
        message.width,
        EGL_HEIGHT,
        message.height,
        EGL_NONE,
    };
    EGLSurface surface = eglCreatePbufferSurface(egl_display, config, pbuf_attribs);

    // 4. Create OpenGL ES 2 context
    EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, ctx_attribs);
    eglMakeCurrent(egl_display, surface, surface, context);

    // 5. Load required extensions
    eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    eglExportDMABUFImageQueryMESA = (PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC)eglGetProcAddress("eglExportDMABUFImageQueryMESA");
    eglExportDMABUFImageMESA = (PFNEGLEXPORTDMABUFIMAGEMESAPROC)eglGetProcAddress("eglExportDMABUFImageMESA");

    if (!eglExportDMABUFImageMESA) {
        fprintf(stderr, "eglExportDMABUFImageMESA not supported\n");
        return 1;
    }

    const size_t TEXTURE_DATA_WIDTH = message.width;
    const size_t TEXTURE_DATA_HEIGHT = message.height;
    const size_t TEXTURE_DATA_SIZE = message.width * message.height;
    int *texture_data = create_data(TEXTURE_DATA_SIZE);

    // 6. Create texture
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, message.width, message.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, message.width, message.height, GL_RGBA, GL_UNSIGNED_BYTE, texture_data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // 7. Draw something (just clear to red)
    /*glViewport(0, 0, message.width, message.height);
    glClearColor(1.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);*/

    // 8. Create EGLImage from GL texture
    EGLImageKHR image = eglCreateImageKHR(egl_display, context,
                                          EGL_GL_TEXTURE_2D_KHR,
                                          (EGLClientBuffer)(uintptr_t)tex, NULL);

    if (image == EGL_NO_IMAGE_KHR) {
        fprintf(stderr, "Failed to create EGLImageKHR\n");
        return 1;
    }

    // 9. Export to dma-buf
    int dma_buf_fd;
    message.type = MSG_HAVE_BUFFER;

    int num_planes;
    eglExportDMABUFImageQueryMESA(egl_display,
                                                       image,
                                                       &message.format,
                                                       &num_planes,
                                                       &message.modifiers);

    EGLBoolean ok = eglExportDMABUFImageMESA(egl_display, image, &dma_buf_fd, &message.stride, &message.offset);
    if (!ok) {
        fprintf(stderr, "Failed to export DMA-BUF\n");
        return 1;
    }

    printf("DMA-BUF exported: fd=%d stride=%d offset=%d\n", dma_buf_fd, message.stride, message.offset);

    while (1) {
        gl_draw_scene(tex);
        rotate_data(texture_data, TEXTURE_DATA_SIZE);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEXTURE_DATA_WIDTH, TEXTURE_DATA_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, texture_data);

        //fprintf(stderr, "Sending message with fd %d\n", dma_buf_fd);
        send_message(sock, dma_buf_fd, MSG_TYPE_FD, &message);

        usleep(6000000 / message.refresh_rate); // Sleep to match refresh rate
    }

    close(sock);

    return 0;
}
