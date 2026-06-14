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

void stream_encoder_init(StreamEncoder *encoder)
{
    memset(encoder, 0, sizeof(*encoder));
    encoder->child_pid = -1;
    encoder->input_fd = -1;
    pthread_mutex_init(&encoder->mutex, NULL);
    pthread_cond_init(&encoder->cond, NULL);
    snprintf(encoder->config.host, sizeof(encoder->config.host), "127.0.0.1");
    encoder->config.port = 27183;
    encoder->config.bitrate_kbps = 8000;
    encoder->config.fps = 30;
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
    if (encoder->pump_running) {
        pthread_join(encoder->pump_thread, NULL);
        encoder->pump_running = false;
    }
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

    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        log_error("failed to create stream pipe: %s", strerror(errno));
        return false;
    }

    char pipeline[2048];
    snprintf(pipeline,
             sizeof(pipeline),
             "gst-launch-1.0 -q "
             "fdsrc fd=0 blocksize=%d do-timestamp=true ! "
             "rawvideoparse format=bgrx width=%d height=%d framerate=%d/1 ! "
             "videoconvert ! video/x-raw,format=NV12 ! "
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

    pid_t child = fork();
    if (child < 0) {
        log_error("failed to fork stream encoder: %s", strerror(errno));
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return false;
    }

    if (child == 0) {
        dup2(pipe_fds[0], STDIN_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        execl("/bin/sh", "sh", "-c", pipeline, (char *)NULL);
        _exit(127);
    }

    close(pipe_fds[0]);
    encoder->child_pid = child;
    encoder->input_fd = pipe_fds[1];
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
    if (pthread_create(&encoder->pump_thread, NULL, stream_pump_main, encoder) != 0) {
        log_error("failed to start stream frame pump");
        stream_encoder_stop(encoder);
        return false;
    }
    encoder->pump_running = true;

    log_info("stream encoder started pid=%d host=%s port=%d bitrate=%dkbps fps=%d",
             (int)child,
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
    if (encoder->pump_running) {
        pthread_join(encoder->pump_thread, NULL);
        encoder->pump_running = false;
    }
    if (encoder->child_pid > 0) {
        kill(encoder->child_pid, SIGTERM);
        waitpid(encoder->child_pid, NULL, 0);
    }
    encoder->child_pid = -1;
    encoder->running = false;
    encoder->has_frame = false;
    free(encoder->latest_frame);
    encoder->latest_frame = NULL;
    encoder->frame_size = 0;
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

static void add_ns(struct timespec *time, long ns)
{
    time->tv_nsec += ns;
    while (time->tv_nsec >= 1000000000L) {
        time->tv_nsec -= 1000000000L;
        time->tv_sec++;
    }
}

static bool write_all(StreamEncoder *encoder, const unsigned char *data, size_t size)
{
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
    clock_gettime(CLOCK_MONOTONIC, &next_frame_time);

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

        if (!write_all(encoder, frame, encoder->frame_size)) {
            pthread_mutex_lock(&encoder->mutex);
            encoder->pump_stop = true;
            pthread_mutex_unlock(&encoder->mutex);
            break;
        }

        add_ns(&next_frame_time, frame_interval_ns);
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_frame_time, NULL) == EINTR) {
        }
    }

    free(frame);
    log_info("stream frame pump stopped");
    return NULL;
}
