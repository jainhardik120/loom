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
    char usb_serial[128];
    int port;
    int bitrate_kbps;
    int fps;
} StreamConfig;

typedef struct StreamMetrics {
    bool running;
    pid_t encoder_pid;
    int width;
    int height;
    int stride;
    int target_fps;
    double raw_fps;
    double avg_raw_write_ms;
    double max_raw_write_ms;
    int slow_raw_writes;
    double encoded_kbps;
    double encoded_au_fps;
    double usb_mbps;
    double avg_usb_write_ms;
    double max_usb_write_ms;
    int usb_writes;
    unsigned long long raw_frames_total;
    unsigned long long encoded_bytes_total;
    unsigned long long usb_bytes_total;
} StreamMetrics;

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
    StreamMetrics metrics;
} StreamEncoder;

void stream_encoder_init(StreamEncoder *encoder);
void stream_encoder_configure(StreamEncoder *encoder, const StreamConfig *config);
bool stream_encoder_start(StreamEncoder *encoder, int width, int height, int stride);
void stream_encoder_stop(StreamEncoder *encoder);
void stream_encoder_write_frame(StreamEncoder *encoder, const void *data, size_t size);
void stream_encoder_get_metrics(StreamEncoder *encoder, StreamMetrics *metrics);

#endif
