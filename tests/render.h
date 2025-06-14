#pragma once

#include <cassert>
#include <stddef.h>
#include <math.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>

void initialize_egl(Display *x11_display, Window x11_window, EGLDisplay *egl_display, EGLContext *egl_context, EGLSurface *egl_surface)
{
    // Set OpenGL rendering API
    eglBindAPI(EGL_OPENGL_API);

    // get an EGL display connection
    EGLDisplay display = eglGetDisplay(x11_display);

    // initialize the EGL display connection
    eglInitialize(display, NULL, NULL);

    // get an appropriate EGL frame buffer configuration
    EGLConfig config;
    EGLint num_config;
    EGLint const attribute_list_config[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE};
    eglChooseConfig(display, attribute_list_config, &config, 1, &num_config);

    // create an EGL rendering context
    EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attribs);

    // create an EGL window surface
    EGLSurface surface = eglCreateWindowSurface(display, config, x11_window, NULL);

    // connect the context to the surface
    eglMakeCurrent(display, surface, surface, context);

    // Return
    *egl_display = display;
    *egl_context = context;
    *egl_surface = surface;
}

void gl_setup_scene()
{
    // Shader source that draws a textures quad
    const char *vertex_shader_source = "#version 200 core\n"
                                       "layout (location = 0) in vec3 aPos;\n"
                                       "layout (location = 1) in vec2 aTexCoords;\n"

                                       "out vec2 TexCoords;\n"

                                       "void main()\n"
                                       "{\n"
                                       "   TexCoords = aTexCoords;\n"
                                       "   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
                                       "}\0";
    const char *fragment_shader_source = "#version 200 core\n"
                                         "out vec4 FragColor;\n"

                                         "in vec2 TexCoords;\n"

                                         "uniform sampler2D Texture1;\n"

                                         "void main()\n"
                                         "{\n"
                                         "   FragColor = texture(Texture1, TexCoords);\n"
                                         "}\0";

    // vertex shader
    int vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL);
    glCompileShader(vertex_shader);
    // fragment shader
    int fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_shader_source, NULL);
    glCompileShader(fragment_shader);
    // link shaders
    int shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);
    // delete shaders
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    // quad
    float vertices[] = {
        0.5f, 0.5f, 0.0f, 1.0f, 0.0f,   // top right
        0.5f, -0.5f, 0.0f, 1.0f, 1.0f,  // bottom right
        -0.5f, -0.5f, 0.0f, 0.0f, 1.0f, // bottom left
        -0.5f, 0.5f, 0.0f, 0.0f, 0.0f   // top left
    };
    unsigned int indices[] = {
        0, 1, 3, // first Triangle
        1, 2, 3  // second Triangle
    };

    unsigned int VBO, VAO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindVertexArray(0);

    // Prebind needed stuff for drawing
    glUseProgram(shader_program);
    glBindVertexArray(VAO);
}

void gl_draw_scene(GLuint texture)
{
    // clear
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // draw quad
    // VAO and shader program are already bound from the call to gl_setup_scene
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

int *create_data(size_t size) {
    size_t edge = sqrt(size);
    assert(edge * edge == size);
    size_t half_edge = edge / 2;

    int *data = (int *)malloc(size * sizeof(int));

    // Paint the texture like so:
    // RG
    // BW
    // where R - red, G - green, B - blue, W - white
    int red = 0x000000FF;
    int green = 0x0000FF00;
    int blue = 0X00FF0000;
    int white = 0x00FFFFFF;
    for (size_t i = 0; i < size; i++) {
        size_t x = i % edge;
        size_t y = i / edge;

        if (x < half_edge) {
            if (y < half_edge) {
                data[i] = red;
            } else {
                data[i] = blue;
            }
        } else {
            if (y < half_edge) {
                data[i] = green;
            } else {
                data[i] = white;
            }
        }
    }

    return data;
}

void rotate_data(int *data, size_t size) {
    size_t edge = sqrt(size);
    assert(edge * edge == size);
    size_t half_edge = edge / 2;

    for (size_t i = 0; i < half_edge * half_edge; i++) {
        size_t x = i % half_edge;
        size_t y = i / half_edge;

        int temp = data[x + y * edge];
        data[x + y * edge] = data[(x + half_edge) + y * edge];
        data[(x + half_edge) + y * edge] = data[(x + half_edge) + (y + half_edge) * edge];
        data[(x + half_edge) + (y + half_edge) * edge] = data[x + (y + half_edge) * edge];
        data[x + (y + half_edge) * edge] = temp;
    }
}