#ifndef LOOMD_USB_ACCESSORY_H
#define LOOMD_USB_ACCESSORY_H

#include <stdbool.h>
#include <stddef.h>

#define LOOM_USB_IDENTITY_STRING_SIZE 128

typedef struct UsbAccessoryIdentity {
    char serial[LOOM_USB_IDENTITY_STRING_SIZE];
    char manufacturer[LOOM_USB_IDENTITY_STRING_SIZE];
    char product[LOOM_USB_IDENTITY_STRING_SIZE];
    unsigned int bus_number;
    unsigned int device_address;
    bool accessory_mode;
} UsbAccessoryIdentity;

typedef struct UsbAccessoryTransport {
    bool running;
    void *context;
    void *handle;
    unsigned char out_endpoint;
    int interface_number;
} UsbAccessoryTransport;

void usb_accessory_init(UsbAccessoryTransport *transport);
bool usb_accessory_switch_to_accessory(void);
bool usb_accessory_switch_to_accessory_for_serial(const char *serial);
bool usb_accessory_device_present(void);
bool usb_accessory_device_present_for_serial(const char *serial);
int usb_accessory_list_identities(UsbAccessoryIdentity *identities, size_t capacity);
bool usb_accessory_list_identities_text(char *buffer, size_t buffer_size);
bool usb_accessory_start(UsbAccessoryTransport *transport);
bool usb_accessory_start_for_serial(UsbAccessoryTransport *transport, const char *serial);
void usb_accessory_stop(UsbAccessoryTransport *transport);
bool usb_accessory_write(UsbAccessoryTransport *transport, const void *data, size_t size);

#endif
