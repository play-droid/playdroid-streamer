#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <linux/input.h>
#include <sys/stat.h>
#include <xkbcommon/xkbcommon.h>
#include <linux/input-event-codes.h>

#include <input.h>


struct keysym_keycode_map {
    uint32_t keysym;
    uint32_t keycode;
};

struct keysym_keycode_map qwerty_map[] = {
    { XKB_KEY_a, KEY_A },
    { XKB_KEY_b, KEY_B },
    { XKB_KEY_c, KEY_C },
    { XKB_KEY_d, KEY_D },
    { XKB_KEY_e, KEY_E },
    { XKB_KEY_f, KEY_F },
    { XKB_KEY_g, KEY_G },
    { XKB_KEY_h, KEY_H },
    { XKB_KEY_i, KEY_I },
    { XKB_KEY_j, KEY_J },
    { XKB_KEY_k, KEY_K },
    { XKB_KEY_l, KEY_L },
    { XKB_KEY_m, KEY_M },
    { XKB_KEY_n, KEY_N },
    { XKB_KEY_o, KEY_O },
    { XKB_KEY_p, KEY_P },
    { XKB_KEY_q, KEY_Q },
    { XKB_KEY_r, KEY_R },
    { XKB_KEY_s, KEY_S },
    { XKB_KEY_t, KEY_T },
    { XKB_KEY_u, KEY_U },
    { XKB_KEY_v, KEY_V },
    { XKB_KEY_w, KEY_W },
    { XKB_KEY_x, KEY_X },
    { XKB_KEY_y, KEY_Y },
    { XKB_KEY_z, KEY_Z },

    { XKB_KEY_A, KEY_A },
    { XKB_KEY_B, KEY_B },
    { XKB_KEY_C, KEY_C },
    { XKB_KEY_D, KEY_D },
    { XKB_KEY_E, KEY_E },
    { XKB_KEY_F, KEY_F },
    { XKB_KEY_G, KEY_G },
    { XKB_KEY_H, KEY_H },
    { XKB_KEY_I, KEY_I },
    { XKB_KEY_J, KEY_J },
    { XKB_KEY_K, KEY_K },
    { XKB_KEY_L, KEY_L },
    { XKB_KEY_M, KEY_M },
    { XKB_KEY_N, KEY_N },
    { XKB_KEY_O, KEY_O },
    { XKB_KEY_P, KEY_P },
    { XKB_KEY_Q, KEY_Q },
    { XKB_KEY_R, KEY_R },
    { XKB_KEY_S, KEY_S },
    { XKB_KEY_T, KEY_T },
    { XKB_KEY_U, KEY_U },
    { XKB_KEY_V, KEY_V },
    { XKB_KEY_W, KEY_W },
    { XKB_KEY_X, KEY_X },
    { XKB_KEY_Y, KEY_Y },
    { XKB_KEY_Z, KEY_Z },

    { XKB_KEY_1, KEY_1 },
    { XKB_KEY_2, KEY_2 },
    { XKB_KEY_3, KEY_3 },
    { XKB_KEY_4, KEY_4 },
    { XKB_KEY_5, KEY_5 },
    { XKB_KEY_6, KEY_6 },
    { XKB_KEY_7, KEY_7 },
    { XKB_KEY_8, KEY_8 },
    { XKB_KEY_9, KEY_9 },
    { XKB_KEY_0, KEY_0 },

    { XKB_KEY_Return, KEY_ENTER },
    { XKB_KEY_Escape, KEY_ESC },
    { XKB_KEY_BackSpace, KEY_BACKSPACE },
    { XKB_KEY_Tab, KEY_TAB },
    { XKB_KEY_space, KEY_SPACE },

    { XKB_KEY_minus, KEY_MINUS },
    { XKB_KEY_equal, KEY_EQUAL },
    { XKB_KEY_bracketleft, KEY_LEFTBRACE },
    { XKB_KEY_bracketright, KEY_RIGHTBRACE },
    { XKB_KEY_backslash, KEY_BACKSLASH },
    { XKB_KEY_semicolon, KEY_SEMICOLON },
    { XKB_KEY_apostrophe, KEY_APOSTROPHE },
    { XKB_KEY_grave, KEY_GRAVE },
    { XKB_KEY_comma, KEY_COMMA },
    { XKB_KEY_period, KEY_DOT },
    { XKB_KEY_slash, KEY_SLASH },
    
    { XKB_KEY_Shift_L, KEY_LEFTSHIFT },
    { XKB_KEY_Shift_R, KEY_RIGHTSHIFT },
    { XKB_KEY_Control_L, KEY_LEFTCTRL },
    { XKB_KEY_Control_R, KEY_RIGHTCTRL },
    { XKB_KEY_Alt_L, KEY_LEFTALT },
    { XKB_KEY_Alt_R, KEY_RIGHTALT },
    { XKB_KEY_Super_L, KEY_LEFTMETA },
    { XKB_KEY_Super_R, KEY_RIGHTMETA },
    { XKB_KEY_Meta_L, KEY_LEFTMETA },
    { XKB_KEY_Meta_R, KEY_RIGHTMETA }
};

#define QWERTY_MAP_SIZE (sizeof(qwerty_map) / sizeof(qwerty_map[0]))

uint32_t qwerty_lookup_keycode(uint32_t keysym) {
    for (size_t i = 0; i < QWERTY_MAP_SIZE; i++) {
        if (qwerty_map[i].keysym == keysym)
            return qwerty_map[i].keycode;
    }
    return 0; // 0 indicates not found
}

struct keysym_keycode_map mouse_map[] = {
    { 1,  BTN_LEFT },
    { 2,  BTN_MIDDLE },
    { 3,  BTN_RIGHT },
    { 8,  BTN_BACK },
    { 9,  BTN_FORWARD }
};

#define MOUSE_MAP_SIZE (sizeof(mouse_map) / sizeof(mouse_map[0]))

uint32_t mouse_lookup_keycode(uint32_t keysym) {
    for (size_t i = 0; i < MOUSE_MAP_SIZE; i++) {
        if (mouse_map[i].keysym == keysym)
            return mouse_map[i].keycode;
    }
    return 0; // 0 indicates not found
}

static const char *INPUT_PIPE_NAME[INPUT_TOTAL] = {
    "/tmp/pd_touch_events",
    "/tmp/pd_keyboard_events",
    "/tmp/pd_pointer_events"
};

#define ADD_EVENT(type_, code_, value_)            \
    event[n].time.tv_sec = rt.tv_sec;              \
    event[n].time.tv_usec = rt.tv_nsec / 1000;     \
    event[n].type = type_;                         \
    event[n].code = code_;                         \
    event[n].value = value_;                       \
    n++;

void init_input(struct input *input) {
    // Pointer
    input->input_fd[INPUT_POINTER] = -1;
    input->ptrPrvX = 0;
    input->ptrPrvY = 0;
    input->reverseScroll = true;
    mkfifo(INPUT_PIPE_NAME[INPUT_POINTER], S_IRWXO | S_IRWXG | S_IRWXU);
    chown(INPUT_PIPE_NAME[INPUT_POINTER], 1000, 1000);

    // Keyboard
    input->input_fd[INPUT_KEYBOARD] = -1;
    mkfifo(INPUT_PIPE_NAME[INPUT_KEYBOARD], S_IRWXO | S_IRWXG | S_IRWXU);
    chown(INPUT_PIPE_NAME[INPUT_KEYBOARD], 1000, 1000);

    // Touch
    input->input_fd[INPUT_TOUCH] = -1;
    mkfifo(INPUT_PIPE_NAME[INPUT_TOUCH], S_IRWXO | S_IRWXG | S_IRWXU);
    chown(INPUT_PIPE_NAME[INPUT_TOUCH], 1000, 1000);
    for (int i = 0; i < MAX_TOUCHPOINTS; i++) {
        input->touch_id[i] = -1;
    }
}

static int ensure_pipe(struct input* input, int input_type) {
    if (input->input_fd[input_type] == -1) {
        input->input_fd[input_type] = open(INPUT_PIPE_NAME[input_type], O_WRONLY | O_NONBLOCK);
        if (input->input_fd[input_type] == -1) {
            fprintf(stderr, "Failed to open pipe to InputFlinger: %s\n", strerror(errno));
            return -1;
        }
    }
    return 0;
}

static void send_key_event(struct input* input, uint32_t key, uint32_t state) {
    struct input_event event[1];
    struct timespec rt;
    unsigned int res, n = 0;

    if (key >= input->keysDown.size()) {
        fprintf(stderr, "Invalid key: %u\n", key);
        return;
    }

    if (ensure_pipe(input, INPUT_KEYBOARD))
        return;

    if (clock_gettime(CLOCK_MONOTONIC, &rt) == -1) {
        fprintf(stderr, "%s:%d error in touch clock_gettime: %s",
              __FILE__, __LINE__, strerror(errno));
    }
    ADD_EVENT(EV_KEY, key, state);

    res = write(input->input_fd[INPUT_KEYBOARD], &event, sizeof(event));
    if (res < sizeof(event))
        fprintf(stderr, "Failed to write event for InputFlinger: %s", strerror(errno));
    input->keysDown[(uint8_t)key] = state;
}

void keyboard_handle_key(struct input* input, uint32_t key, uint32_t state) {
    uint32_t keycode = qwerty_lookup_keycode(key);
    if (keycode == 0) {
        fprintf(stderr, "Key not found in qwerty map: %u\n", key);
        return;
    }
    send_key_event(input, keycode, state);
}

static int get_touch_id(struct input *input, int id) {
    int i = 0;
    for (i = 0; i < MAX_TOUCHPOINTS; i++) {
        if (input->touch_id[i] == id)
            return i;
    }
    for (i = 0; i < MAX_TOUCHPOINTS; i++) {
        if (input->touch_id[i] == -1) {
            input->touch_id[i] = id;
            return i;
        }
    }
    return -1;
}

static int flush_touch_id(struct input *input, int id) {
    for (int i = 0; i < MAX_TOUCHPOINTS; i++) {
        if (input->touch_id[i] == id) {
            input->touch_id[i] = -1;
            return i;
        }
    }
    return -1;
}

void touch_handle_down(struct input* input,
          int32_t id, double x_w, double y_w, double pressure) {
    struct input_event event[6];
    struct timespec rt;
    int x, y;
    unsigned int res, n = 0;

    if (ensure_pipe(input, INPUT_TOUCH))
        return;

    if (clock_gettime(CLOCK_MONOTONIC, &rt) == -1) {
       fprintf(stderr, "%s:%d error in touch clock_gettime: %s",
            __FILE__, __LINE__, strerror(errno));
    }
    x = (int)x_w;
    y = (int)y_w;

    ADD_EVENT(EV_ABS, ABS_MT_SLOT, get_touch_id(input, id));
    ADD_EVENT(EV_ABS, ABS_MT_TRACKING_ID, get_touch_id(input, id));
    ADD_EVENT(EV_ABS, ABS_MT_POSITION_X, x);
    ADD_EVENT(EV_ABS, ABS_MT_POSITION_Y, y);
    ADD_EVENT(EV_ABS, ABS_MT_PRESSURE, (int)pressure);
    ADD_EVENT(EV_SYN, SYN_REPORT, 0);

    res = write(input->input_fd[INPUT_TOUCH], &event, sizeof(event));
    if (res < sizeof(event))
        fprintf(stderr, "Failed to write event for InputFlinger: %s", strerror(errno));
}

void touch_handle_up(struct input* input, int32_t id) {
    struct input_event event[3];
    struct timespec rt;
    unsigned int res, n = 0;

    if (ensure_pipe(input, INPUT_TOUCH))
        return;

    if (clock_gettime(CLOCK_MONOTONIC, &rt) == -1) {
       fprintf(stderr, "%s:%d error in touch clock_gettime: %s",
            __FILE__, __LINE__, strerror(errno));
    }

    ADD_EVENT(EV_ABS, ABS_MT_SLOT, flush_touch_id(input, id));
    ADD_EVENT(EV_ABS, ABS_MT_TRACKING_ID, -1);
    ADD_EVENT(EV_SYN, SYN_REPORT, 0);

    res = write(input->input_fd[INPUT_TOUCH], &event, sizeof(event));
    if (res < sizeof(event))
        fprintf(stderr, "Failed to write event for InputFlinger: %s", strerror(errno));
}

void touch_handle_motion(struct input* input, int32_t id, double x_w, double y_w, double pressure) {
    struct input_event event[6];
    struct timespec rt;
    int x, y;
    unsigned int res, n = 0;

    if (ensure_pipe(input, INPUT_TOUCH))
        return;

    if (clock_gettime(CLOCK_MONOTONIC, &rt) == -1) {
       fprintf(stderr, "%s:%d error in touch clock_gettime: %s",
            __FILE__, __LINE__, strerror(errno));
    }
    x = (int)x_w;
    y = (int)y_w;

    ADD_EVENT(EV_ABS, ABS_MT_SLOT, get_touch_id(input, id));
    ADD_EVENT(EV_ABS, ABS_MT_TRACKING_ID, get_touch_id(input, id));
    ADD_EVENT(EV_ABS, ABS_MT_POSITION_X, x);
    ADD_EVENT(EV_ABS, ABS_MT_POSITION_Y, y);
    ADD_EVENT(EV_ABS, ABS_MT_PRESSURE, (int)pressure);
    ADD_EVENT(EV_SYN, SYN_REPORT, 0);

    res = write(input->input_fd[INPUT_TOUCH], &event, sizeof(event));
    if (res < sizeof(event))
        fprintf(stderr, "Failed to write event for InputFlinger: %s", strerror(errno));
}

void touch_handle_cancel(struct input* input) {
    struct input_event event[6];
    struct timespec rt;
    unsigned int res, n;
    int i;

    if (ensure_pipe(input, INPUT_TOUCH))
        return;

    if (clock_gettime(CLOCK_MONOTONIC, &rt) == -1) {
       fprintf(stderr, "%s:%d error in touch clock_gettime: %s",
            __FILE__, __LINE__, strerror(errno));
    }

    // Cancel all touch points.
    for (i = 0; i < MAX_TOUCHPOINTS; i++) {
        if (input->touch_id[i] != -1) {
            input->touch_id[i] = -1;

            n = 0;
            // Turn finger into palm.
            ADD_EVENT(EV_ABS, ABS_MT_SLOT, i);
            ADD_EVENT(EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_PALM);
            ADD_EVENT(EV_SYN, SYN_REPORT, 0);
            // Lift off.
            ADD_EVENT(EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_FINGER);
            ADD_EVENT(EV_ABS, ABS_MT_TRACKING_ID, -1);
            ADD_EVENT(EV_SYN, SYN_REPORT, 0);

            res = write(input->input_fd[INPUT_TOUCH], &event, sizeof(event));
            if (res < sizeof(event))
                fprintf(stderr, "Failed to write event for InputFlinger: %s", strerror(errno));
        }
    }
}

void pointer_handle_motion(struct input* input, double sx, double sy) {
    struct input_event event[5];
    struct timespec rt;
    int x, y;
    unsigned int res, n = 0;

    if (ensure_pipe(input, INPUT_POINTER))
        return;

    if (clock_gettime(CLOCK_MONOTONIC, &rt) == -1) {
        fprintf(stderr, "%s:%d error in touch clock_gettime: %s",
              __FILE__, __LINE__, strerror(errno));
    }
    x = (int)sx;
    y = (int)sy;

    ADD_EVENT(EV_ABS, ABS_X, x);
    ADD_EVENT(EV_ABS, ABS_Y, y);
    ADD_EVENT(EV_REL, REL_X, x - input->ptrPrvX);
    ADD_EVENT(EV_REL, REL_Y, y - input->ptrPrvY);
    ADD_EVENT(EV_SYN, SYN_REPORT, 0);
    input->ptrPrvX = x;
    input->ptrPrvY = y;

    res = write(input->input_fd[INPUT_POINTER], &event, sizeof(event));
    if (res < sizeof(event))
        fprintf(stderr, "Failed to write event for InputFlinger: %s", strerror(errno));
}

void pointer_handle_button(struct input* input, uint32_t button, uint32_t state) {
    struct input_event event[2];
    struct timespec rt;
    unsigned int res, n = 0;

    if (ensure_pipe(input, INPUT_POINTER))
        return;

    if (clock_gettime(CLOCK_MONOTONIC, &rt) == -1) {
        fprintf(stderr, "%s:%d error in touch clock_gettime: %s",
              __FILE__, __LINE__, strerror(errno));
    }
    ADD_EVENT(EV_KEY, mouse_lookup_keycode(button), state);
    ADD_EVENT(EV_SYN, SYN_REPORT, 0);

    res = write(input->input_fd[INPUT_POINTER], &event, sizeof(event));
    if (res < sizeof(event))
        fprintf(stderr, "Failed to write event for InputFlinger: %s", strerror(errno));
}

void pointer_handle_axis(struct input* input, uint32_t axis, double value) {
    struct input_event event[2];
    struct timespec rt;
    unsigned int res, move, n = 0;
    double fVal = value / 100.0f;
    double step = 1.0f;

    if (ensure_pipe(input, INPUT_POINTER))
        return;

    if (!input->reverseScroll) {
        fVal = -fVal;
    }

    if (axis == 0) {
        input->wheelAccumulatorY += fVal;
        if (std::abs(input->wheelAccumulatorY) < step)
            return;
        move = (int)(input->wheelAccumulatorY / step);
        input->wheelAccumulatorY = std::fmod(input->wheelAccumulatorY, step);
    } else {
        input->wheelAccumulatorX += fVal;
        if (std::abs(input->wheelAccumulatorX) < step)
            return;
        move = (int)(input->wheelAccumulatorX / step);
        input->wheelAccumulatorX = std::fmod(input->wheelAccumulatorX, step);
    }

    if (clock_gettime(CLOCK_MONOTONIC, &rt) == -1) {
        fprintf(stderr, "%s:%d error in touch clock_gettime: %s",
              __FILE__, __LINE__, strerror(errno));
    }

    ADD_EVENT(EV_REL, (axis == 0)
              ? REL_WHEEL : REL_HWHEEL, move);
    ADD_EVENT(EV_SYN, SYN_REPORT, 0);

    res = write(input->input_fd[INPUT_POINTER], &event, sizeof(event));
    if (res < sizeof(event))
        fprintf(stderr, "Failed to write event for InputFlinger: %s", strerror(errno));
}
