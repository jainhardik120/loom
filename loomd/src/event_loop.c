#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "event_loop.h"

#include "logging.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>

static volatile sig_atomic_t g_stop_requested = 0;

static void handle_signal(int signo)
{
    (void)signo;
    g_stop_requested = 1;
}

static bool install_signal_handlers(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGINT, &action, NULL) != 0) {
        log_error("failed to install SIGINT handler: %s", strerror(errno));
        return false;
    }
    if (sigaction(SIGTERM, &action, NULL) != 0) {
        log_error("failed to install SIGTERM handler: %s", strerror(errno));
        return false;
    }
    return true;
}

int event_loop_run(EvdiDevice *device)
{
    if (!install_signal_handlers()) {
        return 1;
    }

    log_info("entering event loop; press Ctrl+C to stop");
    while (!g_stop_requested) {
        evdi_device_request_update(device);

        struct pollfd pfd;
        pfd.fd = evdi_device_event_fd(device);
        pfd.events = POLLIN;
        pfd.revents = 0;

        int rc = poll(&pfd, 1, 1000);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_error("poll failed: %s", strerror(errno));
            return 1;
        }

        if (rc == 0) {
            continue;
        }

        if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            log_warn("EVDI event fd returned revents=0x%x", pfd.revents);
        }

        if ((pfd.revents & POLLIN) != 0) {
            evdi_device_handle_events(device);
        }
    }

    log_info("stop requested");
    return 0;
}
