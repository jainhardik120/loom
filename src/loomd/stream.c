#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "stream.h"

#include "logging.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

static void *stream_pump_main(void *user_data);
static void *encoded_output_main(void *user_data);
static long elapsed_us_since(const struct timespec *started, const struct timespec *ended);
static int count_h264_aud_nals(const unsigned char *tail,
                               size_t tail_size,
                               const unsigned char *data,
                               size_t data_size);

static bool use_usb_accessory(const StreamEncoder *encoder)
{
    return strcmp(encoder->config.transport, "usb_accessory") == 0;
}

void stream_encoder_init(StreamEncoder *encoder)
{
    memset(encoder, 0, sizeof(*encoder));
    encoder->child_pid = -1;
    encoder->input_fd = -1;
    encoder->output_fd = -1;
    pthread_mutex_init(&encoder->mutex, NULL);
    pthread_cond_init(&encoder->cond, NULL);
    usb_accessory_init(&encoder->usb_accessory);
    snprintf(encoder->config.transport, sizeof(encoder->config.transport), "tcp");
    snprintf(encoder->config.host, sizeof(encoder->config.host), "127.0.0.1");
    encoder->config.usb_serial[0] = '\0';
    encoder->config.port = 27183;
    encoder->config.bitrate_kbps = 8000;
    encoder->config.fps = 30;
    encoder->metrics.target_fps = encoder->config.fps;
}

void stream_encoder_configure(StreamEncoder *encoder, const StreamConfig *config)
{
    encoder->config = *config;
}

static bool child_is_running(StreamEncoder *encoder)
{
    if (!encoder->running || encoder->child_pid <= 0) {
        return false;
    }

    int status = 0;
    pid_t rc = waitpid(encoder->child_pid, &status, WNOHANG);
    if (rc == 0) {
        return true;
    }
    if (rc == encoder->child_pid) {
        log_warn("stream encoder exited status=%d", status);
    }
    encoder->running = false;
    encoder->child_pid = -1;
    pthread_mutex_lock(&encoder->mutex);
    encoder->pump_stop = true;
    pthread_cond_broadcast(&encoder->cond);
    pthread_mutex_unlock(&encoder->mutex);
    if (encoder->input_fd >= 0) {
        close(encoder->input_fd);
        encoder->input_fd = -1;
    }
    if (encoder->output_fd >= 0) {
        close(encoder->output_fd);
        encoder->output_fd = -1;
    }
    if (encoder->pump_running) {
        pthread_join(encoder->pump_thread, NULL);
        encoder->pump_running = false;
    }
    if (encoder->output_thread_running) {
        pthread_join(encoder->encoded_output_thread, NULL);
        encoder->output_thread_running = false;
    }
    usb_accessory_stop(&encoder->usb_accessory);
    encoder->has_frame = false;
    free(encoder->latest_frame);
    encoder->latest_frame = NULL;
    encoder->frame_size = 0;
    return false;
}

bool stream_encoder_start(StreamEncoder *encoder, int width, int height, int stride)
{
    if (!encoder->config.enabled) {
        return false;
    }
    if (child_is_running(encoder)) {
        return true;
    }

    int input_pipe_fds[2];
    int output_pipe_fds[2] = {-1, -1};
    if (pipe(input_pipe_fds) != 0) {
        log_error("failed to create stream pipe: %s", strerror(errno));
        return false;
    }
    if (use_usb_accessory(encoder) && pipe(output_pipe_fds) != 0) {
        log_error("failed to create stream output pipe: %s", strerror(errno));
        close(input_pipe_fds[0]);
        close(input_pipe_fds[1]);
        return false;
    }

    if (use_usb_accessory(encoder) &&
        !usb_accessory_start_for_serial(&encoder->usb_accessory, encoder->config.usb_serial)) {
        close(input_pipe_fds[0]);
        close(input_pipe_fds[1]);
        if (output_pipe_fds[0] >= 0) {
            close(output_pipe_fds[0]);
            close(output_pipe_fds[1]);
        }
        return false;
    }

    char pipeline[2048];
    if (use_usb_accessory(encoder)) {
        snprintf(pipeline,
                 sizeof(pipeline),
                 "gst-launch-1.0 -q "
                 "fdsrc fd=0 blocksize=%d do-timestamp=true ! "
                 "rawvideoparse format=bgrx width=%d height=%d framerate=%d/1 ! "
                 "vapostproc disable-passthrough=true ! "
                 "'video/x-raw(memory:VAMemory),format=NV12' ! "
                 "vah264enc rate-control=cbr bitrate=%d key-int-max=%d aud=true target-usage=7 ! "
                 "h264parse config-interval=-1 ! video/x-h264,stream-format=byte-stream,alignment=au ! "
                 "fdsink fd=1 sync=false",
                 stride * height,
                 width,
                 height,
                 encoder->config.fps,
                 encoder->config.bitrate_kbps,
                 encoder->config.fps > 0 ? encoder->config.fps : 30);
    } else {
        snprintf(pipeline,
                 sizeof(pipeline),
                 "gst-launch-1.0 -q "
                 "fdsrc fd=0 blocksize=%d do-timestamp=true ! "
                 "rawvideoparse format=bgrx width=%d height=%d framerate=%d/1 ! "
                 "vapostproc disable-passthrough=true ! "
                 "'video/x-raw(memory:VAMemory),format=NV12' ! "
                 "vah264enc rate-control=cbr bitrate=%d key-int-max=%d aud=true target-usage=7 ! "
                 "h264parse config-interval=-1 ! video/x-h264,stream-format=byte-stream,alignment=au ! "
                 "tcpclientsink host=%s port=%d sync=false",
                 stride * height,
                 width,
                 height,
                 encoder->config.fps,
                 encoder->config.bitrate_kbps,
                 encoder->config.fps > 0 ? encoder->config.fps : 30,
                 encoder->config.host,
                 encoder->config.port);
    }

    pid_t child = fork();
    if (child < 0) {
        log_error("failed to fork stream encoder: %s", strerror(errno));
        close(input_pipe_fds[0]);
        close(input_pipe_fds[1]);
        if (output_pipe_fds[0] >= 0) {
            close(output_pipe_fds[0]);
            close(output_pipe_fds[1]);
        }
        usb_accessory_stop(&encoder->usb_accessory);
        return false;
    }

    if (child == 0) {
        dup2(input_pipe_fds[0], STDIN_FILENO);
        if (output_pipe_fds[1] >= 0) {
            dup2(output_pipe_fds[1], STDOUT_FILENO);
        }
        close(input_pipe_fds[0]);
        close(input_pipe_fds[1]);
        if (output_pipe_fds[0] >= 0) {
            close(output_pipe_fds[0]);
            close(output_pipe_fds[1]);
        }
        execl("/bin/sh", "sh", "-c", pipeline, (char *)NULL);
        _exit(127);
    }

    close(input_pipe_fds[0]);
    encoder->child_pid = child;
    encoder->input_fd = input_pipe_fds[1];
    if (output_pipe_fds[0] >= 0) {
        close(output_pipe_fds[1]);
        encoder->output_fd = output_pipe_fds[0];
        if (pthread_create(&encoder->encoded_output_thread, NULL, encoded_output_main, encoder) != 0) {
            log_error("failed to start encoded USB output thread");
            stream_encoder_stop(encoder);
            return false;
        }
        encoder->output_thread_running = true;
    }
    encoder->width = width;
    encoder->height = height;
    encoder->stride = stride;
    encoder->frame_size = (size_t)stride * (size_t)height;
    encoder->latest_frame = malloc(encoder->frame_size);
    if (!encoder->latest_frame) {
        log_error("failed to allocate stream frame cache");
        close(encoder->input_fd);
        encoder->input_fd = -1;
        kill(child, SIGTERM);
        waitpid(child, NULL, 0);
        encoder->child_pid = -1;
        return false;
    }

    pthread_mutex_lock(&encoder->mutex);
    encoder->has_frame = false;
    encoder->pump_stop = false;
    pthread_mutex_unlock(&encoder->mutex);

    encoder->running = true;
    pthread_mutex_lock(&encoder->mutex);
    memset(&encoder->metrics, 0, sizeof(encoder->metrics));
    encoder->metrics.running = true;
    encoder->metrics.encoder_pid = child;
    encoder->metrics.width = width;
    encoder->metrics.height = height;
    encoder->metrics.stride = stride;
    encoder->metrics.target_fps = encoder->config.fps;
    pthread_mutex_unlock(&encoder->mutex);
    if (pthread_create(&encoder->pump_thread, NULL, stream_pump_main, encoder) != 0) {
        log_error("failed to start stream frame pump");
        stream_encoder_stop(encoder);
        return false;
    }
    encoder->pump_running = true;

    log_info("stream encoder started pid=%d transport=%s target=%s:%d bitrate=%dkbps fps=%d",
             (int)child,
             encoder->config.transport,
             encoder->config.host,
             encoder->config.port,
             encoder->config.bitrate_kbps,
             encoder->config.fps);
    return true;
}

void stream_encoder_stop(StreamEncoder *encoder)
{
    pthread_mutex_lock(&encoder->mutex);
    encoder->pump_stop = true;
    pthread_cond_broadcast(&encoder->cond);
    pthread_mutex_unlock(&encoder->mutex);

    if (encoder->input_fd >= 0) {
        close(encoder->input_fd);
        encoder->input_fd = -1;
    }
    if (encoder->output_fd >= 0) {
        close(encoder->output_fd);
        encoder->output_fd = -1;
    }
    if (encoder->pump_running) {
        pthread_join(encoder->pump_thread, NULL);
        encoder->pump_running = false;
    }
    if (encoder->output_thread_running) {
        pthread_join(encoder->encoded_output_thread, NULL);
        encoder->output_thread_running = false;
    }
    if (encoder->child_pid > 0) {
        kill(encoder->child_pid, SIGTERM);
        waitpid(encoder->child_pid, NULL, 0);
    }
    encoder->child_pid = -1;
    encoder->running = false;
    encoder->has_frame = false;
    pthread_mutex_lock(&encoder->mutex);
    encoder->metrics.running = false;
    encoder->metrics.encoder_pid = -1;
    pthread_mutex_unlock(&encoder->mutex);
    free(encoder->latest_frame);
    encoder->latest_frame = NULL;
    encoder->frame_size = 0;
    usb_accessory_stop(&encoder->usb_accessory);
}

void stream_encoder_write_frame(StreamEncoder *encoder, const void *data, size_t size)
{
    if (!child_is_running(encoder) || !encoder->latest_frame || size != encoder->frame_size) {
        return;
    }

    pthread_mutex_lock(&encoder->mutex);
    memcpy(encoder->latest_frame, data, size);
    encoder->has_frame = true;
    pthread_cond_signal(&encoder->cond);
    pthread_mutex_unlock(&encoder->mutex);
}

void stream_encoder_get_metrics(StreamEncoder *encoder, StreamMetrics *metrics)
{
    pthread_mutex_lock(&encoder->mutex);
    *metrics = encoder->metrics;
    metrics->running = encoder->running;
    metrics->encoder_pid = encoder->child_pid;
    pthread_mutex_unlock(&encoder->mutex);
}

static void add_ns(struct timespec *time, long ns)
{
    time->tv_nsec += ns;
    while (time->tv_nsec >= 1000000000L) {
        time->tv_nsec -= 1000000000L;
        time->tv_sec++;
    }
}

static bool write_all(StreamEncoder *encoder, const unsigned char *data, size_t size, long *elapsed_us)
{
    struct timespec started;
    struct timespec ended;
    clock_gettime(CLOCK_MONOTONIC, &started);

    size_t remaining = size;
    while (remaining > 0) {
        int fd = encoder->input_fd;
        if (fd < 0) {
            return false;
        }

        ssize_t written = write(fd, data, remaining);
        if (written < 0) {
            if (errno == EPIPE) {
                log_warn("stream encoder pipe closed");
                return false;
            }
            log_warn("stream encoder write failed: %s", strerror(errno));
            return false;
        }
        data += written;
        remaining -= (size_t)written;
    }
    clock_gettime(CLOCK_MONOTONIC, &ended);
    if (elapsed_us) {
        *elapsed_us = elapsed_us_since(&started, &ended);
    }
    return true;
}

static void *stream_pump_main(void *user_data)
{
    StreamEncoder *encoder = user_data;
    unsigned char *frame = malloc(encoder->frame_size);
    if (!frame) {
        log_error("failed to allocate stream pump frame");
        return NULL;
    }

    const int fps = encoder->config.fps > 0 ? encoder->config.fps : 30;
    const long frame_interval_ns = 1000000000L / fps;
    struct timespec next_frame_time;
    struct timespec stats_window_started;
    clock_gettime(CLOCK_MONOTONIC, &next_frame_time);
    clock_gettime(CLOCK_MONOTONIC, &stats_window_started);
    int frames_in_window = 0;
    long write_us_in_window = 0;
    long max_write_us = 0;
    int slow_writes_in_window = 0;

    log_info("stream frame pump running at %d fps", fps);
    while (true) {
        pthread_mutex_lock(&encoder->mutex);
        while (!encoder->pump_stop && !encoder->has_frame) {
            pthread_cond_wait(&encoder->cond, &encoder->mutex);
            clock_gettime(CLOCK_MONOTONIC, &next_frame_time);
        }
        if (encoder->pump_stop) {
            pthread_mutex_unlock(&encoder->mutex);
            break;
        }
        memcpy(frame, encoder->latest_frame, encoder->frame_size);
        pthread_mutex_unlock(&encoder->mutex);

        long write_us = 0;
        if (!write_all(encoder, frame, encoder->frame_size, &write_us)) {
            pthread_mutex_lock(&encoder->mutex);
            encoder->pump_stop = true;
            pthread_mutex_unlock(&encoder->mutex);
            break;
        }
        frames_in_window++;
        write_us_in_window += write_us;
        if (write_us > max_write_us) {
            max_write_us = write_us;
        }
        if (write_us > frame_interval_ns / 1000L) {
            slow_writes_in_window++;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long window_ms = elapsed_us_since(&stats_window_started, &now) / 1000L;
        if (window_ms >= 1000) {
            double frames_per_sec = ((double)frames_in_window * 1000.0) / (double)window_ms;
            double avg_write_ms = frames_in_window > 0
                                      ? ((double)write_us_in_window / 1000.0) / (double)frames_in_window
                                      : 0.0;
            log_info("stream raw pump %.1f frames/s avg_write=%.2fms max_write=%.2fms slow_writes=%d",
                     frames_per_sec,
                     avg_write_ms,
                     (double)max_write_us / 1000.0,
                     slow_writes_in_window);
            pthread_mutex_lock(&encoder->mutex);
            encoder->metrics.raw_fps = frames_per_sec;
            encoder->metrics.avg_raw_write_ms = avg_write_ms;
            encoder->metrics.max_raw_write_ms = (double)max_write_us / 1000.0;
            encoder->metrics.slow_raw_writes = slow_writes_in_window;
            encoder->metrics.raw_frames_total += (unsigned long long)frames_in_window;
            pthread_mutex_unlock(&encoder->mutex);
            frames_in_window = 0;
            write_us_in_window = 0;
            max_write_us = 0;
            slow_writes_in_window = 0;
            stats_window_started = now;
        }

        add_ns(&next_frame_time, frame_interval_ns);
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_frame_time, NULL) == EINTR) {
        }
    }

    free(frame);
    log_info("stream frame pump stopped");
    return NULL;
}

static long elapsed_us_since(const struct timespec *started, const struct timespec *ended)
{
    return (ended->tv_sec - started->tv_sec) * 1000000L +
           (ended->tv_nsec - started->tv_nsec) / 1000L;
}

static void *encoded_output_main(void *user_data)
{
    StreamEncoder *encoder = user_data;
    unsigned char buffer[64 * 1024];
    unsigned char tail[8] = {0};
    size_t tail_size = 0;
    size_t bytes_in_window = 0;
    size_t usb_bytes_in_window = 0;
    int access_units_in_window = 0;
    long usb_write_us_in_window = 0;
    long max_usb_write_us = 0;
    int usb_writes_in_window = 0;
    struct timespec window_started;
    clock_gettime(CLOCK_MONOTONIC, &window_started);

    log_info("USB accessory encoded output thread running");
    while (encoder->output_fd >= 0) {
        ssize_t nread = read(encoder->output_fd, buffer, sizeof(buffer));
        if (nread == 0) {
            break;
        }
        if (nread < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_warn("encoded output read failed: %s", strerror(errno));
            break;
        }
        access_units_in_window += count_h264_aud_nals(tail, tail_size, buffer, (size_t)nread);
        bytes_in_window += (size_t)nread;
        if ((size_t)nread >= sizeof(tail)) {
            memcpy(tail, buffer + (size_t)nread - sizeof(tail), sizeof(tail));
            tail_size = sizeof(tail);
        } else {
            size_t keep_from_tail = tail_size;
            if (keep_from_tail + (size_t)nread > sizeof(tail)) {
                keep_from_tail = sizeof(tail) - (size_t)nread;
                memmove(tail, tail + tail_size - keep_from_tail, keep_from_tail);
            }
            memcpy(tail + keep_from_tail, buffer, (size_t)nread);
            tail_size = keep_from_tail + (size_t)nread;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - window_started.tv_sec) * 1000L +
                          (now.tv_nsec - window_started.tv_nsec) / 1000000L;
        if (elapsed_ms >= 1000) {
            double kbps = ((double)bytes_in_window * 8.0) / (double)elapsed_ms;
            double usb_mbps = ((double)usb_bytes_in_window * 8.0) / ((double)elapsed_ms * 1000.0);
            double au_per_sec = ((double)access_units_in_window * 1000.0) / (double)elapsed_ms;
            double avg_usb_write_ms = usb_writes_in_window > 0
                                          ? ((double)usb_write_us_in_window / 1000.0) /
                                                (double)usb_writes_in_window
                                          : 0.0;
            log_info("USB encoded output %.0f kbps %.1f access-units/s usb=%.1fMbps avg_usb_write=%.2fms max_usb_write=%.2fms writes=%d",
                     kbps,
                     au_per_sec,
                     usb_mbps,
                     avg_usb_write_ms,
                     (double)max_usb_write_us / 1000.0,
                     usb_writes_in_window);
            pthread_mutex_lock(&encoder->mutex);
            encoder->metrics.encoded_kbps = kbps;
            encoder->metrics.encoded_au_fps = au_per_sec;
            encoder->metrics.usb_mbps = usb_mbps;
            encoder->metrics.avg_usb_write_ms = avg_usb_write_ms;
            encoder->metrics.max_usb_write_ms = (double)max_usb_write_us / 1000.0;
            encoder->metrics.usb_writes = usb_writes_in_window;
            encoder->metrics.encoded_bytes_total += (unsigned long long)bytes_in_window;
            encoder->metrics.usb_bytes_total += (unsigned long long)usb_bytes_in_window;
            pthread_mutex_unlock(&encoder->mutex);
            bytes_in_window = 0;
            usb_bytes_in_window = 0;
            access_units_in_window = 0;
            usb_write_us_in_window = 0;
            max_usb_write_us = 0;
            usb_writes_in_window = 0;
            window_started = now;
        }

        struct timespec usb_write_started;
        struct timespec usb_write_ended;
        clock_gettime(CLOCK_MONOTONIC, &usb_write_started);
        if (!usb_accessory_write(&encoder->usb_accessory, buffer, (size_t)nread)) {
            break;
        }
        clock_gettime(CLOCK_MONOTONIC, &usb_write_ended);
        long usb_write_us = elapsed_us_since(&usb_write_started, &usb_write_ended);
        usb_bytes_in_window += (size_t)nread;
        usb_write_us_in_window += usb_write_us;
        if (usb_write_us > max_usb_write_us) {
            max_usb_write_us = usb_write_us;
        }
        usb_writes_in_window++;
    }

    log_info("USB accessory encoded output thread stopped");
    return NULL;
}

static int count_h264_aud_nals(const unsigned char *tail,
                               size_t tail_size,
                               const unsigned char *data,
                               size_t data_size)
{
    unsigned char scan[sizeof(((unsigned char[8]){0})) + 64 * 1024];
    if (tail_size > 8) {
        tail_size = 8;
    }
    if (data_size > 64 * 1024) {
        data_size = 64 * 1024;
    }
    memcpy(scan, tail, tail_size);
    memcpy(scan + tail_size, data, data_size);
    size_t size = tail_size + data_size;
    int count = 0;
    for (size_t i = 0; i + 4 < size; i++) {
        if (scan[i] == 0 && scan[i + 1] == 0) {
            size_t nal_offset = 0;
            if (scan[i + 2] == 1) {
                nal_offset = i + 3;
            } else if (i + 3 < size && scan[i + 2] == 0 && scan[i + 3] == 1) {
                nal_offset = i + 4;
            }
            if (nal_offset >= tail_size && nal_offset < size && (scan[nal_offset] & 0x1f) == 9) {
                count++;
            }
        }
    }
    return count;
}
