#include <stdio.h>
#include <playsocket.h>

#define QUOTE(str) #str
#define EXPAND_AND_QUOTE(str) QUOTE(str)

int main(int argc, char **argv) {
    if(argc != 1) {
        printf("%s takes no arguments.\n", argv[0]);
        return 1;
    }
    printf("This is project %s, version %s.\n", EXPAND_AND_QUOTE(PROJECT_NAME), EXPAND_AND_QUOTE(PROJECT_VERSION));
    return 0;
}
