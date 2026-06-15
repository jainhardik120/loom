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

int event_loop_run(LoomDisplayManager *display_manager, LoomControlService *control_service)
{
    if (!install_signal_handlers()) {
        return 1;
    }

    log_info("entering event loop; press Ctrl+C to stop");
    while (!g_stop_requested) {
        control_service_dispatch(control_service);
        display_manager_tick(display_manager);
        for (size_t i = 0; i < display_manager->session_count; i++) {
            LoomDisplaySession *session = &display_manager->sessions[i];
            if (session->evdi_open) {
                evdi_device_request_update(&session->evdi);
            }
        }

        struct pollfd pfds[LOOM_MAX_DISPLAY_SESSIONS + 1];
        LoomDisplaySession *poll_sessions[LOOM_MAX_DISPLAY_SESSIONS];
        int nfds = 0;
        int evdi_fds = 0;

        for (size_t i = 0; i < display_manager->session_count; i++) {
            LoomDisplaySession *session = &display_manager->sessions[i];
            if (!session->evdi_open) {
                continue;
            }
            pfds[nfds].fd = evdi_device_event_fd(&session->evdi);
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            poll_sessions[evdi_fds++] = session;
            nfds++;
        }

        const int dbus_fd = control_service_fd(control_service);
        if (dbus_fd >= 0) {
            pfds[nfds].fd = dbus_fd;
            pfds[nfds].events = control_service_events(control_service);
            if (pfds[nfds].events == 0) {
                pfds[nfds].events = POLLIN;
            }
            pfds[nfds].events |= POLLERR | POLLHUP;
            pfds[nfds].revents = 0;
            nfds++;
        }

        int rc = poll(pfds, nfds, 1000);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_error("poll failed: %s", strerror(errno));
            return 1;
        }

        if (rc == 0) {
            control_service_dispatch(control_service);
            continue;
        }

        for (int i = 0; i < evdi_fds; i++) {
            if ((pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                log_warn("display %s EVDI event fd returned revents=0x%x",
                         poll_sessions[i]->profile.id,
                         pfds[i].revents);
            }
            if ((pfds[i].revents & POLLIN) != 0) {
                evdi_device_handle_events(&poll_sessions[i]->evdi);
            }
        }

        if (dbus_fd >= 0 && nfds > evdi_fds && pfds[evdi_fds].revents != 0) {
            control_service_dispatch(control_service);
        }
    }

    log_info("stop requested");
    return 0;
}
