#pragma once

#include <cstdint>
#include <stdlib.h>
#include <unistd.h>

enum MessageType {
    MSG_FAILED,
    MSG_TYPE_DATA,
    MSG_TYPE_DATA_NEEDS_REPLY,
    MSG_TYPE_DATA_REPLY,
    MSG_TYPE_FD
};

struct MessageHeader {
    uint32_t type;   // from MessageType
    uint32_t length; // length of the payload (excluding header)
};

enum DataType {
    MSG_HELLO,
    MSG_ASK_FOR_RESOLUTION,
    MSG_HAVE_RESOLUTION,
    MSG_HAVE_BUFFER
};

struct MessageData {
    enum DataType type;
    int width;
    int height;
    int refresh_rate;

    int format;
    uint64_t modifiers;
    int32_t stride;
    int32_t offset;
};
