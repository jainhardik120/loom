#ifndef LOOMD_STREAM_H
#define LOOMD_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct StreamConfig {
    bool enabled;
    char host[128];
    int port;
    int bitrate_kbps;
    int fps;
} StreamConfig;

typedef struct StreamEncoder {
    StreamConfig config;
    pid_t child_pid;
    int input_fd;
    int width;
    int height;
    int stride;
    bool running;
} StreamEncoder;

void stream_encoder_init(StreamEncoder *encoder);
void stream_encoder_configure(StreamEncoder *encoder, const StreamConfig *config);
bool stream_encoder_start(StreamEncoder *encoder, int width, int height, int stride);
void stream_encoder_stop(StreamEncoder *encoder);
void stream_encoder_write_frame(StreamEncoder *encoder, const void *data, size_t size);

#endif

