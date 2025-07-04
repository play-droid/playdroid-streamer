#include <cstdio>
#include <cstdlib>
#include <thread>
#include <getopt.h>

#include <display.h>
#include <gsthelper.h>
#include <input.h>

#define QUOTE(str) #str
#define EXPAND_AND_QUOTE(str) QUOTE(str)

struct playdroid {
    struct display *display;
    struct gsthelper *gsthelper;
    struct input *input;

};

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
           "\t'-l,--gst-pipeline=<>'"
           "\n\t\tCustom GST pipeline, default is wayland\n"
           "\t'-a,--wayland-window'"
           "\n\t\tOpen Real wayland window\n",
           DISPLAY_SOCKET_PATH, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_REFRESH_RATE);
    exit(0);
}

void parse_args(int argc, char **argv, struct playdroid *playdroid) {
    int c, option_index = 0;

    static struct option long_options[] = {
        {"socket", required_argument, 0, 's'},
        {"width", required_argument, 0, 'w'},
        {"height", required_argument, 0, 'y'},
        {"refresh-rate", required_argument, 0, 'r'},
        {"gst-pipeline", required_argument, 0, 'l'},
        {"wayland-window", no_argument, 0, 'a'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};

    while ((c = getopt_long(argc, argv, "hs:w:y:r:l:a",
                            long_options, &option_index)) != -1) {
        switch (c) {
        case 's':
            playdroid->display->socket_path = optarg;
            if (playdroid->display->socket_path == NULL || strlen(playdroid->display->socket_path) == 0) {
                fprintf(stderr, "Invalid socket path: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case 'w':
            playdroid->display->width = strtol(optarg, NULL, 10);
            break;
        case 'y':
            playdroid->display->height = strtol(optarg, NULL, 10);
            break;
        case 'r':
            playdroid->display->refresh_rate = strtol(optarg, NULL, 10);
            break;
        case 'l':
            playdroid->gsthelper->gst_pipeline = optarg;
            break;
        case 'a':
            playdroid->display->open_wayland_window = true;
            break;
        default:
            print_usage_and_exit();
        }
    }

}

int main(int argc, char **argv) {
    struct playdroid *playdroid = NULL;

    playdroid = (struct playdroid *)calloc(1, sizeof *playdroid);
    if (playdroid == NULL) {
        fprintf(stderr, "out of memory\n");
    }
    playdroid->display = (struct display *)calloc(1, sizeof *playdroid->display);
    if (playdroid->display == NULL) {
        fprintf(stderr, "out of memory\n");
    }
    playdroid->gsthelper = (struct gsthelper *)calloc(1, sizeof *playdroid->gsthelper);
    if (playdroid->gsthelper == NULL) {
        fprintf(stderr, "out of memory\n");
    }
    playdroid->input = (struct input *)calloc(1, sizeof *playdroid->input);
    if (playdroid->input == NULL) {
        fprintf(stderr, "out of memory\n");
    }
    printf("This is project %s, version %s.\n", EXPAND_AND_QUOTE(PROJECT_NAME), EXPAND_AND_QUOTE(PROJECT_VERSION));
    init_display(playdroid->display);
    parse_args(argc, argv, playdroid);

    // Set up the Input Event Handlers
    init_input(playdroid->input);

    if (!playdroid->display->open_wayland_window) {
        gst_pipeline_deinit(playdroid->gsthelper);
        gst_pipeline_init(playdroid->gsthelper, playdroid->display->width, playdroid->display->height, 
            playdroid->display->refresh_rate, playdroid->input);
    }

    // Set up the display socket
    std::thread display_thread([&playdroid]() {
        run_display(playdroid->display, playdroid->gsthelper);
    });

    display_thread.join();

    return 0;
}
