#include "usb_accessory.h"

#include "logging.h"

#include <string.h>

#ifdef HAVE_LIBUSB

#include <libusb.h>
#include <stdlib.h>
#include <unistd.h>

#define AOA_GET_PROTOCOL 51
#define AOA_SEND_IDENT 52
#define AOA_START_ACCESSORY 53
#define AOA_VENDOR_ID 0x18d1

static const int k_aoa_pids[] = {0x2d00, 0x2d01, 0x2d02, 0x2d03, 0x2d04, 0x2d05};

static const char *k_accessory_strings[] = {
    "Loom",
    "Loom Display",
    "Loom tablet display stream",
    "0.1",
    "https://github.com/jainhardik120/loom",
    "LOOM0001"
};

static bool is_aoa_pid(uint16_t vendor_id, uint16_t product_id)
{
    if (vendor_id != AOA_VENDOR_ID) {
        return false;
    }
    for (size_t i = 0; i < sizeof(k_aoa_pids) / sizeof(k_aoa_pids[0]); i++) {
        if (product_id == k_aoa_pids[i]) {
            return true;
        }
    }
    return false;
}

static bool send_accessory_strings(libusb_device_handle *handle)
{
    for (uint16_t i = 0; i < sizeof(k_accessory_strings) / sizeof(k_accessory_strings[0]); i++) {
        const char *value = k_accessory_strings[i];
        int rc = libusb_control_transfer(handle,
                                         LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR,
                                         AOA_SEND_IDENT,
                                         0,
                                         i,
                                         (unsigned char *)value,
                                         (uint16_t)(strlen(value) + 1),
                                         1000);
        if (rc < 0) {
            log_warn("AOA send string %u failed: %s", i, libusb_error_name(rc));
            return false;
        }
    }
    return true;
}

static bool switch_device_to_accessory(libusb_device_handle *handle)
{
    unsigned char protocol[2] = {0};
    int rc = libusb_control_transfer(handle,
                                     LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR,
                                     AOA_GET_PROTOCOL,
                                     0,
                                     0,
                                     protocol,
                                     sizeof(protocol),
                                     1000);
    if (rc < 0) {
        return false;
    }

    int version = protocol[0] | (protocol[1] << 8);
    if (version < 1) {
        return false;
    }
    log_info("AOA protocol version %d", version);

    if (!send_accessory_strings(handle)) {
        return false;
    }

    rc = libusb_control_transfer(handle,
                                 LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR,
                                 AOA_START_ACCESSORY,
                                 0,
                                 0,
                                 NULL,
                                 0,
                                 1000);
    if (rc < 0) {
        log_warn("AOA start accessory failed: %s", libusb_error_name(rc));
        return false;
    }
    return true;
}

static libusb_device_handle *find_aoa_handle(libusb_context *context)
{
    libusb_device **devices = NULL;
    ssize_t count = libusb_get_device_list(context, &devices);
    if (count < 0) {
        return NULL;
    }

    libusb_device_handle *handle = NULL;
    for (ssize_t i = 0; i < count; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devices[i], &desc) != 0) {
            continue;
        }

        if (!is_aoa_pid(desc.idVendor, desc.idProduct)) {
            continue;
        }

        if (libusb_open(devices[i], &handle) == 0) {
            break;
        }
        handle = NULL;
    }

    libusb_free_device_list(devices, 1);
    return handle;
}

static bool find_bulk_out_endpoint(UsbAccessoryTransport *transport)
{
    libusb_device *device = libusb_get_device((libusb_device_handle *)transport->handle);
    struct libusb_config_descriptor *config = NULL;
    int rc = libusb_get_active_config_descriptor(device, &config);
    if (rc < 0) {
        log_error("failed to read USB config descriptor: %s", libusb_error_name(rc));
        return false;
    }

    bool found = false;
    for (int i = 0; i < config->bNumInterfaces && !found; i++) {
        const struct libusb_interface *iface = &config->interface[i];
        for (int j = 0; j < iface->num_altsetting && !found; j++) {
            const struct libusb_interface_descriptor *alt = &iface->altsetting[j];
            for (int k = 0; k < alt->bNumEndpoints; k++) {
                const struct libusb_endpoint_descriptor *ep = &alt->endpoint[k];
                if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK &&
                    (ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_OUT) {
                    transport->interface_number = alt->bInterfaceNumber;
                    transport->out_endpoint = ep->bEndpointAddress;
                    found = true;
                    break;
                }
            }
        }
    }

    libusb_free_config_descriptor(config);
    return found;
}

static libusb_device_handle *open_first_android_device(libusb_context *context)
{
    const char *allow_switch = getenv("LOOM_USB_ACCESSORY_AUTO_SWITCH");
    if (!allow_switch || strcmp(allow_switch, "1") != 0) {
        log_error("refusing to auto-switch Android device into AOA; set LOOM_USB_ACCESSORY_AUTO_SWITCH=1 to override");
        return NULL;
    }

    libusb_device **devices = NULL;
    ssize_t count = libusb_get_device_list(context, &devices);
    if (count < 0) {
        return NULL;
    }

    libusb_device_handle *handle = NULL;
    for (ssize_t i = 0; i < count; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devices[i], &desc) != 0) {
            continue;
        }
        if (is_aoa_pid(desc.idVendor, desc.idProduct)) {
            continue;
        }
        if (libusb_open(devices[i], &handle) == 0 && switch_device_to_accessory(handle)) {
            libusb_close(handle);
            handle = NULL;
            sleep(2);
            break;
        }
        if (handle) {
            libusb_close(handle);
            handle = NULL;
        }
    }

    libusb_free_device_list(devices, 1);
    return find_aoa_handle(context);
}

void usb_accessory_init(UsbAccessoryTransport *transport)
{
    memset(transport, 0, sizeof(*transport));
    transport->interface_number = -1;
}

bool usb_accessory_switch_to_accessory(void)
{
    libusb_context *context = NULL;
    int rc = libusb_init(&context);
    if (rc < 0) {
        log_error("libusb_init failed: %s", libusb_error_name(rc));
        return false;
    }

    libusb_device_handle *handle = find_aoa_handle(context);
    if (handle) {
        log_info("device is already in Android Open Accessory mode");
        libusb_close(handle);
        libusb_exit(context);
        return true;
    }

    handle = open_first_android_device(context);
    if (!handle) {
        libusb_exit(context);
        return false;
    }

    libusb_close(handle);
    libusb_exit(context);
    return true;
}

bool usb_accessory_start(UsbAccessoryTransport *transport)
{
    if (transport->running) {
        return true;
    }

    libusb_context *context = NULL;
    int rc = libusb_init(&context);
    if (rc < 0) {
        log_error("libusb_init failed: %s", libusb_error_name(rc));
        return false;
    }

    libusb_device_handle *handle = find_aoa_handle(context);
    if (!handle) {
        log_info("no Android accessory device found; trying to switch a USB device into AOA mode");
        handle = open_first_android_device(context);
    }
    if (!handle) {
        log_error("no Android Open Accessory device found");
        libusb_exit(context);
        return false;
    }

    transport->context = context;
    transport->handle = handle;
    if (!find_bulk_out_endpoint(transport)) {
        log_error("AOA device has no bulk OUT endpoint");
        usb_accessory_stop(transport);
        return false;
    }

    libusb_set_auto_detach_kernel_driver(handle, 1);
    rc = libusb_claim_interface(handle, transport->interface_number);
    if (rc < 0) {
        log_error("failed to claim AOA interface %d: %s",
                  transport->interface_number,
                  libusb_error_name(rc));
        usb_accessory_stop(transport);
        return false;
    }

    transport->running = true;
    log_info("USB accessory transport ready interface=%d out_ep=0x%02x",
             transport->interface_number,
             transport->out_endpoint);
    return true;
}

void usb_accessory_stop(UsbAccessoryTransport *transport)
{
    if (transport->handle) {
        if (transport->interface_number >= 0) {
            libusb_release_interface((libusb_device_handle *)transport->handle,
                                     transport->interface_number);
        }
        libusb_close((libusb_device_handle *)transport->handle);
    }
    if (transport->context) {
        libusb_exit((libusb_context *)transport->context);
    }
    usb_accessory_init(transport);
}

bool usb_accessory_write(UsbAccessoryTransport *transport, const void *data, size_t size)
{
    if (!transport->running || !transport->handle) {
        return false;
    }

    const unsigned char *cursor = data;
    size_t remaining = size;
    while (remaining > 0) {
        int chunk = remaining > 65536 ? 65536 : (int)remaining;
        int transferred = 0;
        int rc = libusb_bulk_transfer((libusb_device_handle *)transport->handle,
                                      transport->out_endpoint,
                                      (unsigned char *)cursor,
                                      chunk,
                                      &transferred,
                                      1000);
        if (rc < 0) {
            log_warn("USB accessory write failed: %s", libusb_error_name(rc));
            return false;
        }
        cursor += transferred;
        remaining -= (size_t)transferred;
    }
    return true;
}

#else

void usb_accessory_init(UsbAccessoryTransport *transport)
{
    memset(transport, 0, sizeof(*transport));
    transport->interface_number = -1;
}

bool usb_accessory_start(UsbAccessoryTransport *transport)
{
    (void)transport;
    log_error("USB accessory transport requires libusb-1.0-dev at build time");
    return false;
}

bool usb_accessory_switch_to_accessory(void)
{
    log_error("USB accessory transport requires libusb-1.0-dev at build time");
    return false;
}

void usb_accessory_stop(UsbAccessoryTransport *transport)
{
    usb_accessory_init(transport);
}

bool usb_accessory_write(UsbAccessoryTransport *transport, const void *data, size_t size)
{
    (void)transport;
    (void)data;
    (void)size;
    return false;
}

#endif
