#ifndef LOOMD_STREAM_H
#define LOOMD_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>
#include <sys/types.h>

#include "usb_accessory.h"

typedef struct StreamConfig {
    bool enabled;
    char transport[32];
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
    bool pump_running;
    bool pump_stop;
    bool has_frame;
    unsigned char *latest_frame;
    size_t frame_size;
    pthread_t pump_thread;
    pthread_t encoded_output_thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int output_fd;
    bool output_thread_running;
    UsbAccessoryTransport usb_accessory;
} StreamEncoder;

void stream_encoder_init(StreamEncoder *encoder);
void stream_encoder_configure(StreamEncoder *encoder, const StreamConfig *config);
bool stream_encoder_start(StreamEncoder *encoder, int width, int height, int stride);
void stream_encoder_stop(StreamEncoder *encoder);
void stream_encoder_write_frame(StreamEncoder *encoder, const void *data, size_t size);

#endif
