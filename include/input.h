#pragma once

#include <stdint.h>
#include <array>

#define MAX_TOUCHPOINTS 10

enum {
    INPUT_TOUCH,
    INPUT_KEYBOARD,
    INPUT_POINTER,
    INPUT_TOTAL
};

struct input {
    int input_fd[INPUT_TOTAL];
    int ptrPrvX;
    int ptrPrvY;
    double wheelAccumulatorX;
    double wheelAccumulatorY;
    bool reverseScroll;
    int touch_id[MAX_TOUCHPOINTS];
    std::array<uint8_t, 239> keysDown;
    bool wheelEvtIsDiscrete;
};

void init_input(struct input *input);
void keyboard_handle_key(struct input* input, uint32_t key, uint32_t state);
void touch_handle_down(struct input* input, int32_t id, double x_w, double y_w, double pressure);
void touch_handle_up(struct input* input, int32_t id);
void touch_handle_motion(struct input* input, int32_t id, double x_w, double y_w, double pressure);
void touch_handle_cancel(struct input* input);
void pointer_handle_motion(struct input* input, double sx, double sy);
void pointer_handle_button(struct input* input, uint32_t button, uint32_t state);
void pointer_handle_axis(struct input* input, uint32_t axis, double value);

