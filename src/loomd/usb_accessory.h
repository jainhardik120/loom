#ifndef LOOMD_USB_ACCESSORY_H
#define LOOMD_USB_ACCESSORY_H

#include <stdbool.h>
#include <stddef.h>

typedef struct UsbAccessoryTransport {
    bool running;
    void *context;
    void *handle;
    unsigned char out_endpoint;
    int interface_number;
} UsbAccessoryTransport;

void usb_accessory_init(UsbAccessoryTransport *transport);
bool usb_accessory_switch_to_accessory(void);
bool usb_accessory_start(UsbAccessoryTransport *transport);
void usb_accessory_stop(UsbAccessoryTransport *transport);
bool usb_accessory_write(UsbAccessoryTransport *transport, const void *data, size_t size);

#endif
