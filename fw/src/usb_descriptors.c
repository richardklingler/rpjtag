#include "pico/unique_id.h"
#include "tusb.h"

#define USB_CONFIGURATION_LENGTH (TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN + TUD_CDC_DESC_LEN)
#define USB_STRING_DESCRIPTOR_CAPACITY 32u
#define USB_MS_OS_VENDOR_CODE 0x20u
#define USB_MS_OS_FUNCTION_LENGTH 160u
#define USB_MS_OS_DESCRIPTOR_LENGTH (10u + 8u + USB_MS_OS_FUNCTION_LENGTH)
#define USB_BOS_DESCRIPTOR_LENGTH (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

enum {
    USB_INTERFACE_CMSIS_DAP = 0,
    USB_INTERFACE_CDC = 1,
    USB_STRING_MANUFACTURER = 1,
    USB_STRING_PRODUCT = 2,
    USB_STRING_SERIAL = 3,
    USB_STRING_CMSIS_DAP = 4,
    USB_STRING_CDC = 5,
};

enum {
    USB_ENDPOINT_CMSIS_DAP_OUT = 0x01,
    USB_ENDPOINT_CMSIS_DAP_IN = 0x81,
    USB_ENDPOINT_CDC_NOTIFICATION = 0x82,
    USB_ENDPOINT_CDC_OUT = 0x03,
    USB_ENDPOINT_CDC_IN = 0x83,
};

static const tusb_desc_device_t usb_device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0210,
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USBD_VID,
    .idProduct = USBD_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = USB_STRING_MANUFACTURER,
    .iProduct = USB_STRING_PRODUCT,
    .iSerialNumber = USB_STRING_SERIAL,
    .bNumConfigurations = 1,
};

static const uint8_t usb_configuration_descriptor[USB_CONFIGURATION_LENGTH] = {
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, USB_CONFIGURATION_LENGTH, 0, 250),
    TUD_VENDOR_DESCRIPTOR(USB_INTERFACE_CMSIS_DAP, USB_STRING_CMSIS_DAP,
                          USB_ENDPOINT_CMSIS_DAP_OUT, USB_ENDPOINT_CMSIS_DAP_IN, 64),
    TUD_CDC_DESCRIPTOR(USB_INTERFACE_CDC, USB_STRING_CDC,
                       USB_ENDPOINT_CDC_NOTIFICATION, 8,
                       USB_ENDPOINT_CDC_OUT, USB_ENDPOINT_CDC_IN, 64),
};

static const uint8_t usb_bos_descriptor[USB_BOS_DESCRIPTOR_LENGTH] = {
    TUD_BOS_DESCRIPTOR(USB_BOS_DESCRIPTOR_LENGTH, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(USB_MS_OS_DESCRIPTOR_LENGTH, USB_MS_OS_VENDOR_CODE),
};

static uint8_t usb_ms_os_20_descriptor[USB_MS_OS_DESCRIPTOR_LENGTH];
static char usb_serial_string[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

static const char *const usb_string_descriptors[] = {
    [USB_STRING_MANUFACTURER] = USBD_MANUFACTURER,
    [USB_STRING_PRODUCT] = USBD_PRODUCT,
    [USB_STRING_SERIAL] = usb_serial_string,
    [USB_STRING_CMSIS_DAP] = "CMSIS-DAP v2",
    [USB_STRING_CDC] = "rpJTAG CDC",
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&usb_device_descriptor;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return usb_configuration_descriptor;
}

const uint8_t *tud_descriptor_bos_cb(void) {
    return usb_bos_descriptor;
}

static void put_u16(uint8_t *buffer, uint16_t value) {
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8);
}

static void append_u16(size_t *offset, uint16_t value) {
    put_u16(&usb_ms_os_20_descriptor[*offset], value);
    *offset += 2;
}

static void append_u32(size_t *offset, uint32_t value) {
    usb_ms_os_20_descriptor[(*offset)++] = (uint8_t)value;
    usb_ms_os_20_descriptor[(*offset)++] = (uint8_t)(value >> 8);
    usb_ms_os_20_descriptor[(*offset)++] = (uint8_t)(value >> 16);
    usb_ms_os_20_descriptor[(*offset)++] = (uint8_t)(value >> 24);
}

static void append_utf16_ascii(size_t *offset, const char *string, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        append_u16(offset, (uint8_t)string[index]);
    }
}

static void build_ms_os_20_descriptor(void) {
    static bool built;
    if (built) {
        return;
    }

    static const char property_name[] = "DeviceInterfaceGUIDs";
    static const char interface_guid[] = "{F20E99EC-73B5-4F5B-9F2D-7D7B5D67A501}";
    size_t offset = 0;

    append_u16(&offset, 10);
    append_u16(&offset, 0);
    append_u32(&offset, 0x06030000);
    append_u16(&offset, USB_MS_OS_DESCRIPTOR_LENGTH);

    append_u16(&offset, 8);
    append_u16(&offset, 1);
    usb_ms_os_20_descriptor[offset++] = 0;
    usb_ms_os_20_descriptor[offset++] = 0;
    append_u16(&offset, USB_MS_OS_FUNCTION_LENGTH);

    append_u16(&offset, 20);
    append_u16(&offset, 3);
    memcpy(&usb_ms_os_20_descriptor[offset], "WINUSB", 6);
    offset += 8;
    offset += 8;

    append_u16(&offset, 132);
    append_u16(&offset, 4);
    append_u16(&offset, 7);
    append_u16(&offset, 42);
    append_utf16_ascii(&offset, property_name, sizeof(property_name));
    append_u16(&offset, 80);
    append_utf16_ascii(&offset, interface_guid, sizeof(interface_guid) - 1);
    append_u16(&offset, 0);
    append_u16(&offset, 0);

    built = offset == sizeof(usb_ms_os_20_descriptor);
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                tusb_control_request_t const *request) {
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }

    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR &&
        request->bRequest == USB_MS_OS_VENDOR_CODE && request->wIndex == 7) {
        build_ms_os_20_descriptor();
        return tud_control_xfer(rhport, request, usb_ms_os_20_descriptor,
                                sizeof(usb_ms_os_20_descriptor));
    }

    return false;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t language_id) {
    (void)language_id;
    static uint16_t descriptor[USB_STRING_DESCRIPTOR_CAPACITY];
    uint8_t character_count = 0;

    if (index == 0) {
        descriptor[1] = 0x0409;
        character_count = 1;
    } else {
        if (index >= sizeof(usb_string_descriptors) / sizeof(usb_string_descriptors[0])) {
            return NULL;
        }

        if (index == USB_STRING_SERIAL && usb_serial_string[0] == '\0') {
            pico_get_unique_board_id_string(usb_serial_string, sizeof(usb_serial_string));
        }

        const char *string = usb_string_descriptors[index];
        while (character_count < USB_STRING_DESCRIPTOR_CAPACITY - 1 && string[character_count]) {
            descriptor[1 + character_count] = (uint8_t)string[character_count];
            ++character_count;
        }
    }

    descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * character_count + 2));
    return descriptor;
}