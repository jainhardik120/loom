#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "stream.h"

#include "logging.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void stream_encoder_init(StreamEncoder *encoder)
{
    memset(encoder, 0, sizeof(*encoder));
    encoder->child_pid = -1;
    encoder->input_fd = -1;
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
    if (encoder->input_fd >= 0) {
        close(encoder->input_fd);
        encoder->input_fd = -1;
    }
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
    encoder->running = true;

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
    if (encoder->input_fd >= 0) {
        close(encoder->input_fd);
        encoder->input_fd = -1;
    }
    if (encoder->child_pid > 0) {
        kill(encoder->child_pid, SIGTERM);
        waitpid(encoder->child_pid, NULL, 0);
    }
    encoder->child_pid = -1;
    encoder->running = false;
}

void stream_encoder_write_frame(StreamEncoder *encoder, const void *data, size_t size)
{
    if (!child_is_running(encoder) || encoder->input_fd < 0) {
        return;
    }

    const unsigned char *cursor = data;
    size_t remaining = size;
    while (remaining > 0) {
        ssize_t written = write(encoder->input_fd, cursor, remaining);
        if (written < 0) {
            if (errno == EPIPE) {
                log_warn("stream encoder pipe closed");
                stream_encoder_stop(encoder);
                return;
            }
            log_warn("stream encoder write failed: %s", strerror(errno));
            return;
        }
        cursor += written;
        remaining -= (size_t)written;
    }
}
