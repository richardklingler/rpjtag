#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "DAP_config.h"
#include "DAP.h"
#include "dap_jtag_io.h"
#include "hardware/adc.h"
#include "native_protocol.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "tusb.h"

#define NATIVE_INTERFACE 1u
#define NATIVE_HEADER_SIZE 8u
#define NATIVE_MAX_SCAN_BITS 4096u
#define NATIVE_MAX_SCAN_BYTES ((NATIVE_MAX_SCAN_BITS + 7u) / 8u)
#define NATIVE_MAX_PAYLOAD (3u * NATIVE_MAX_SCAN_BYTES + 16u)
#define NATIVE_RESPONSE_CAPACITY (NATIVE_MAX_PAYLOAD + 1u)
#define NATIVE_MAX_CHAIN_DEVICES DAP_JTAG_DEV_CNT
#define NATIVE_MAX_CHAIN_IR_BITS (NATIVE_MAX_CHAIN_DEVICES * 32u)
#define NATIVE_CHAIN_DETECT_BITS (NATIVE_MAX_CHAIN_DEVICES * 32u + 32u)
#define NATIVE_IR_MARKER_BITS 64u
#define NATIVE_IR_DETECT_BITS (NATIVE_MAX_CHAIN_IR_BITS + NATIVE_IR_MARKER_BITS)
#define NATIVE_BATCH_MAX_COMMANDS 32u

enum {
    NATIVE_FLAG_MORE = 1u,
    NATIVE_FLAG_NO_RESPONSE = 2u,
    NATIVE_STATUS_OK = 0,
    NATIVE_STATUS_BAD_CMD = 1,
    NATIVE_STATUS_BAD_LEN = 2,
    NATIVE_STATUS_NOT_CONFIGURED = 3,
    NATIVE_STATUS_VTREF_LOW = 4,
    NATIVE_STATUS_IO_DISABLED = 5,
    NATIVE_STATUS_TDO_STUCK = 6,
    NATIVE_STATUS_CHECK_MISMATCH = 7,
    NATIVE_STATUS_TIMEOUT = 8,
    NATIVE_STATUS_FLASH_ERROR = 9,
    NATIVE_STATUS_BUSY = 10,
    NATIVE_STATUS_ABORTED = 11,
    NATIVE_STATUS_BUFFER_OVERFLOW = 12,
};

enum {
    NATIVE_CMD_GET_INFO = 0x01,
    NATIVE_CMD_SET_TCK = 0x02,
    NATIVE_CMD_GET_VTREF = 0x03,
    NATIVE_CMD_TAP_RESET = 0x10,
    NATIVE_CMD_TAP_GOTO = 0x11,
    NATIVE_CMD_SCAN_IR = 0x12,
    NATIVE_CMD_SCAN_DR = 0x13,
    NATIVE_CMD_SCAN_DR_CHECK = 0x14,
    NATIVE_CMD_RUNTEST = 0x15,
    NATIVE_CMD_BATCH = 0x16,
    NATIVE_CMD_CHAIN_DETECT = 0x20,
    NATIVE_CMD_CHAIN_CONFIG = 0x21,
};

enum {
    TAP_TEST_LOGIC_RESET = 0,
    TAP_RUN_TEST_IDLE = 1,
    TAP_SELECT_DR_SCAN = 2,
    TAP_CAPTURE_DR = 3,
    TAP_SHIFT_DR = 4,
    TAP_EXIT1_DR = 5,
    TAP_PAUSE_DR = 6,
    TAP_EXIT2_DR = 7,
    TAP_UPDATE_DR = 8,
    TAP_SELECT_IR_SCAN = 9,
    TAP_CAPTURE_IR = 10,
    TAP_SHIFT_IR = 11,
    TAP_EXIT1_IR = 12,
    TAP_PAUSE_IR = 13,
    TAP_EXIT2_IR = 14,
    TAP_UPDATE_IR = 15,
};

typedef struct {
    uint8_t ir_length;
    uint32_t bypass_opcode;
} native_chain_device_t;

static const uint8_t tap_next_state[16][2] = {
    {TAP_TEST_LOGIC_RESET, TAP_RUN_TEST_IDLE},
    {TAP_RUN_TEST_IDLE, TAP_SELECT_DR_SCAN},
    {TAP_CAPTURE_DR, TAP_SELECT_IR_SCAN},
    {TAP_SHIFT_DR, TAP_EXIT1_DR},
    {TAP_SHIFT_DR, TAP_EXIT1_DR},
    {TAP_PAUSE_DR, TAP_UPDATE_DR},
    {TAP_PAUSE_DR, TAP_EXIT2_DR},
    {TAP_SHIFT_DR, TAP_UPDATE_DR},
    {TAP_RUN_TEST_IDLE, TAP_SELECT_DR_SCAN},
    {TAP_CAPTURE_IR, TAP_TEST_LOGIC_RESET},
    {TAP_SHIFT_IR, TAP_EXIT1_IR},
    {TAP_SHIFT_IR, TAP_EXIT1_IR},
    {TAP_PAUSE_IR, TAP_UPDATE_IR},
    {TAP_PAUSE_IR, TAP_EXIT2_IR},
    {TAP_SHIFT_IR, TAP_UPDATE_IR},
    {TAP_RUN_TEST_IDLE, TAP_SELECT_DR_SCAN},
};

static uint8_t native_tap_state = TAP_TEST_LOGIC_RESET;
static native_chain_device_t native_chain[NATIVE_MAX_CHAIN_DEVICES];
static uint8_t native_chain_count = 1;
static uint8_t native_active_device;
static uint8_t native_detected_count;
static bool native_chain_configured;

static uint8_t frame_header[NATIVE_HEADER_SIZE];
static uint8_t frame_payload[NATIVE_MAX_PAYLOAD];
static uint32_t frame_header_used;
static uint32_t frame_payload_expected;
static uint32_t frame_payload_used;
static uint32_t frame_discard_remaining;
static uint8_t frame_cmd;
static uint8_t frame_seq;
static uint16_t frame_flags;

static uint8_t fragment_payload[NATIVE_MAX_PAYLOAD];
static uint32_t fragment_payload_used;
static uint8_t fragment_cmd;
static uint8_t fragment_seq;
static bool fragment_active;

static uint8_t response_payload[NATIVE_RESPONSE_CAPACITY];

static uint16_t read_u16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void write_u16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static bool get_bit(const uint8_t *data, uint32_t bit) {
    return (data[bit / 8u] & (1u << (bit % 8u))) != 0;
}

static void set_bit(uint8_t *data, uint32_t bit, bool value) {
    const uint8_t mask = (uint8_t)(1u << (bit % 8u));
    if (value) {
        data[bit / 8u] |= mask;
    } else {
        data[bit / 8u] &= (uint8_t)~mask;
    }
}

static void copy_bits(uint8_t *destination, uint32_t destination_bit,
                      const uint8_t *source, uint32_t source_bit,
                      uint32_t bit_count) {
    for (uint32_t bit = 0; bit < bit_count; ++bit) {
        set_bit(destination, destination_bit + bit, get_bit(source, source_bit + bit));
    }
}

static bool tap_goto(uint8_t target) {
    if (target >= 16) {
        return false;
    }
    if (target == native_tap_state) {
        return true;
    }

    uint8_t queue[16];
    uint8_t parent[16];
    uint8_t parent_tms[16];
    bool visited[16] = {false};
    uint32_t queue_read = 0;
    uint32_t queue_write = 0;
    queue[queue_write++] = native_tap_state;
    visited[native_tap_state] = true;

    while (queue_read < queue_write && !visited[target]) {
        const uint8_t state = queue[queue_read++];
        for (uint8_t tms = 0; tms <= 1; ++tms) {
            const uint8_t next = tap_next_state[state][tms];
            if (!visited[next]) {
                visited[next] = true;
                parent[next] = state;
                parent_tms[next] = tms;
                queue[queue_write++] = next;
            }
        }
    }
    if (!visited[target]) {
        return false;
    }

    uint8_t reversed[16];
    uint32_t path_length = 0;
    for (uint8_t state = target; state != native_tap_state; state = parent[state]) {
        reversed[path_length++] = parent_tms[state];
    }
    uint8_t tms_bits[2] = {0};
    for (uint32_t bit = 0; bit < path_length; ++bit) {
        set_bit(tms_bits, bit, reversed[path_length - bit - 1u] != 0);
    }
    dap_jtag_sequence_bits(path_length, tms_bits, NULL, NULL);
    native_tap_state = target;
    return true;
}

static void tap_reset(void) {
    const uint8_t tms_bits = 0x1fu;
    dap_jtag_sequence_bits(5, &tms_bits, NULL, NULL);
    native_tap_state = TAP_TEST_LOGIC_RESET;
}

static uint32_t active_ir_padding_before(void) {
    uint32_t bits = 0;
    for (uint32_t index = 0; index < native_active_device; ++index) {
        bits += native_chain[index].ir_length;
    }
    return bits;
}

static uint32_t active_ir_padding_after(void) {
    uint32_t bits = 0;
    for (uint32_t index = native_active_device + 1u; index < native_chain_count; ++index) {
        bits += native_chain[index].ir_length;
    }
    return bits;
}

static bool scan_register(bool instruction, uint32_t bit_count,
                          const uint8_t *tdi, uint8_t *tdo,
                          uint8_t end_state) {
    const uint8_t shift_state = instruction ? TAP_SHIFT_IR : TAP_SHIFT_DR;
    const uint8_t exit_state = instruction ? TAP_EXIT1_IR : TAP_EXIT1_DR;
    uint32_t active_offset = 0;
    uint32_t total_bits = bit_count;

    if (native_chain_configured) {
        if (instruction) {
            active_offset = active_ir_padding_before();
            total_bits += active_offset + active_ir_padding_after();
        } else {
            active_offset = native_active_device;
            total_bits += native_chain_count - 1u;
        }
    }
    if (total_bits == 0 || total_bits > NATIVE_MAX_SCAN_BITS) {
        return false;
    }

    static uint8_t scan_tdi[NATIVE_MAX_SCAN_BYTES];
    static uint8_t scan_tdo[NATIVE_MAX_SCAN_BYTES];
    static uint8_t scan_tms[NATIVE_MAX_SCAN_BYTES];
    const uint32_t scan_bytes = (total_bits + 7u) / 8u;
    memset(scan_tdi, instruction ? 0 : 0xff, scan_bytes);
    memset(scan_tdo, 0, scan_bytes);
    memset(scan_tms, 0, scan_bytes);

    if (native_chain_configured && instruction) {
        uint32_t offset = 0;
        for (uint32_t index = 0; index < native_chain_count; ++index) {
            const uint32_t length = native_chain[index].ir_length;
            if (index == native_active_device) {
                copy_bits(scan_tdi, offset, tdi, 0, bit_count);
            } else {
                for (uint32_t bit = 0; bit < length; ++bit) {
                    set_bit(scan_tdi, offset + bit,
                            (native_chain[index].bypass_opcode & (1u << bit)) != 0);
                }
            }
            offset += length;
        }
    } else {
        copy_bits(scan_tdi, active_offset, tdi, 0, bit_count);
    }
    set_bit(scan_tms, total_bits - 1u, true);

    if (!tap_goto(shift_state)) {
        return false;
    }
    dap_jtag_sequence_bits(total_bits, scan_tms, scan_tdi, tdo != NULL ? scan_tdo : NULL);
    native_tap_state = exit_state;

    if (tdo != NULL) {
        memset(tdo, 0, (bit_count + 7u) / 8u);
        copy_bits(tdo, 0, scan_tdo, active_offset, bit_count);
    }
    return tap_goto(end_state);
}

static uint8_t detect_chain(uint32_t *idcodes, uint8_t *device_count,
                            uint16_t *total_ir_bits) {
    uint8_t tdi[NATIVE_CHAIN_DETECT_BITS / 8u];
    uint8_t tdo[NATIVE_CHAIN_DETECT_BITS / 8u];
    uint8_t tms[NATIVE_CHAIN_DETECT_BITS / 8u] = {0};
    memset(tdi, 0xff, sizeof(tdi));
    set_bit(tms, NATIVE_CHAIN_DETECT_BITS - 1u, true);

    tap_reset();
    if (!tap_goto(TAP_SHIFT_DR)) {
        return NATIVE_STATUS_TDO_STUCK;
    }
    dap_jtag_sequence_bits(NATIVE_CHAIN_DETECT_BITS, tms, tdi, tdo);
    native_tap_state = TAP_EXIT1_DR;

    uint32_t bit = 0;
    uint8_t count = 0;
    while (bit < NATIVE_CHAIN_DETECT_BITS && count < NATIVE_MAX_CHAIN_DEVICES) {
        if (!get_bit(tdo, bit)) {
            idcodes[count++] = 0;
            ++bit;
            continue;
        }
        if (bit + 32u > NATIVE_CHAIN_DETECT_BITS) {
            return NATIVE_STATUS_BUFFER_OVERFLOW;
        }
        uint32_t idcode = 0;
        for (uint32_t id_bit = 0; id_bit < 32; ++id_bit) {
            if (get_bit(tdo, bit + id_bit)) {
                idcode |= 1u << id_bit;
            }
        }
        if (idcode == UINT32_MAX) {
            break;
        }
        idcodes[count++] = idcode;
        bit += 32u;
    }

    tap_reset();
    uint8_t ir_tdi[NATIVE_IR_DETECT_BITS / 8u] = {0};
    uint8_t ir_tdo[NATIVE_IR_DETECT_BITS / 8u];
    uint8_t ir_tms[NATIVE_IR_DETECT_BITS / 8u] = {0};
    static const uint8_t marker[NATIVE_IR_MARKER_BITS / 8u] = {
        0x48, 0x20, 0x7f, 0x1b, 0xe9, 0xc6, 0xa5, 0xd3,
    };
    for (uint32_t index = 0; index < NATIVE_IR_DETECT_BITS; ++index) {
        set_bit(ir_tdi, index, get_bit(marker, index % NATIVE_IR_MARKER_BITS));
    }
    set_bit(ir_tms, NATIVE_IR_DETECT_BITS - 1u, true);
    if (!tap_goto(TAP_SHIFT_IR)) {
        return NATIVE_STATUS_TDO_STUCK;
    }
    dap_jtag_sequence_bits(NATIVE_IR_DETECT_BITS, ir_tms, ir_tdi, ir_tdo);
    native_tap_state = TAP_EXIT1_IR;

    bool marker_found = false;
    for (uint32_t offset = 0; offset <= NATIVE_MAX_CHAIN_IR_BITS; ++offset) {
        bool match = true;
        for (uint32_t marker_bit = 0; marker_bit < NATIVE_IR_MARKER_BITS; ++marker_bit) {
            if (get_bit(ir_tdo, offset + marker_bit) !=
                get_bit(marker, marker_bit)) {
                match = false;
                break;
            }
        }
        if (match) {
            *total_ir_bits = (uint16_t)offset;
            marker_found = true;
            break;
        }
    }
    tap_reset();
    if (!marker_found || count == 0) {
        return NATIVE_STATUS_TDO_STUCK;
    }
    *device_count = count;
    return NATIVE_STATUS_OK;
}

static uint16_t read_vtref_mv(void) {
    adc_select_input(0);
    const uint16_t raw = adc_read();
    return (uint16_t)(((uint32_t)raw * 6600u + 2047u) / 4095u);
}

static uint32_t dispatch_command(uint8_t cmd, const uint8_t *payload,
                                 uint32_t length, uint8_t *response,
                                 uint32_t capacity, bool allow_batch);

static uint32_t dispatch_batch(const uint8_t *payload, uint32_t length,
                               uint8_t *response, uint32_t capacity) {
    if (length == 0 || capacity < 1) {
        response[0] = NATIVE_STATUS_BAD_LEN;
        return 1;
    }

    response[0] = NATIVE_STATUS_OK;
    uint32_t input_offset = 0;
    uint32_t output_offset = 1;
    uint32_t command_count = 0;
    uint8_t subresponse[NATIVE_MAX_SCAN_BYTES + 1u];
    while (input_offset < length) {
        if (length - input_offset < 3 || command_count >= NATIVE_BATCH_MAX_COMMANDS) {
            response[0] = command_count >= NATIVE_BATCH_MAX_COMMANDS ?
                NATIVE_STATUS_BUFFER_OVERFLOW : NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        const uint8_t subcommand = payload[input_offset++];
        const uint16_t sublength = read_u16(payload + input_offset);
        input_offset += 2;
        if (sublength > length - input_offset ||
            output_offset + 3u > capacity) {
            response[0] = NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        if (subcommand < NATIVE_CMD_TAP_RESET || subcommand > NATIVE_CMD_RUNTEST) {
            response[0] = NATIVE_STATUS_BAD_CMD;
            return 1;
        }
        const uint32_t subresponse_length = dispatch_command(
            subcommand, payload + input_offset, sublength,
            subresponse, sizeof(subresponse), false);
        if (subresponse_length > UINT16_MAX ||
            output_offset + 3u + subresponse_length > capacity) {
            response[0] = NATIVE_STATUS_BUFFER_OVERFLOW;
            return 1;
        }
        response[output_offset++] = subcommand;
        write_u16(response + output_offset, (uint16_t)subresponse_length);
        output_offset += 2;
        memcpy(response + output_offset, subresponse, subresponse_length);
        output_offset += subresponse_length;
        input_offset += sublength;
        ++command_count;
    }
    return output_offset;
}

static uint32_t dispatch_command(uint8_t cmd, const uint8_t *payload,
                                 uint32_t length, uint8_t *response,
                                 uint32_t capacity, bool allow_batch) {
    if (capacity == 0) {
        return 0;
    }
    response[0] = NATIVE_STATUS_OK;

    switch (cmd) {
    case NATIVE_CMD_GET_INFO: {
        if (length != 0 || capacity < 26) {
            response[0] = NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        write_u16(response + 1, 0x0100);
        response[3] = 0x01;
        response[4] = 0;
        write_u32(response + 5, 0x0000001fu);
        write_u32(response + 9, NATIVE_MAX_SCAN_BITS);
        write_u16(response + 13, NATIVE_MAX_PAYLOAD);
        write_u16(response + 15, NATIVE_RESPONSE_CAPACITY);
        response[17] = PICO_UNIQUE_BOARD_ID_SIZE_BYTES;
        pico_unique_board_id_t unique_id;
        pico_get_unique_board_id(&unique_id);
        memcpy(response + 18, unique_id.id, PICO_UNIQUE_BOARD_ID_SIZE_BYTES);
        return 18u + PICO_UNIQUE_BOARD_ID_SIZE_BYTES;
    }
    case NATIVE_CMD_SET_TCK:
        if (length != 4 || capacity < 5) {
            response[0] = NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        if (!dap_jtag_set_tck(read_u32(payload))) {
            response[0] = NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        write_u32(response + 1, dap_jtag_get_tck());
        return 5;
    case NATIVE_CMD_GET_VTREF:
        if (length != 0 || capacity < 5) {
            response[0] = NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        write_u16(response + 1, read_vtref_mv());
        write_u16(response + 3, 0);
        return 5;
    case NATIVE_CMD_TAP_RESET:
        if (length != 0) {
            response[0] = NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        tap_reset();
        return 1;
    case NATIVE_CMD_TAP_GOTO:
        if (length != 1 || payload[0] >= 16) {
            response[0] = NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        if (!tap_goto(payload[0])) {
            response[0] = NATIVE_STATUS_TDO_STUCK;
        }
        return 1;
    case NATIVE_CMD_SCAN_IR:
    case NATIVE_CMD_SCAN_DR: {
        const bool instruction = cmd == NATIVE_CMD_SCAN_IR;
        const uint32_t header_length = instruction ? 4u : 6u;
        if (length < header_length) {
            response[0] = NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        const uint32_t bit_count = instruction ? read_u16(payload) : read_u32(payload);
        const uint8_t end_state = payload[instruction ? 2u : 4u];
        const uint8_t flags = payload[instruction ? 3u : 5u];
        const uint32_t scan_bytes = (bit_count + 7u) / 8u;
        if (bit_count == 0 || bit_count > NATIVE_MAX_SCAN_BITS || end_state >= 16 ||
            (flags & ~1u) != 0 || length != header_length + scan_bytes) {
            response[0] = NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        if (instruction && native_chain_configured &&
            bit_count != native_chain[native_active_device].ir_length) {
            response[0] = NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        uint8_t captured[NATIVE_MAX_SCAN_BYTES];
        uint8_t *capture = (flags & 1u) != 0 ? captured : NULL;
        if (!scan_register(instruction, bit_count, payload + header_length,
                           capture, end_state)) {
            response[0] = NATIVE_STATUS_BUFFER_OVERFLOW;
            return 1;
        }
        if (capture != NULL) {
            if (capacity < 1u + scan_bytes) {
                response[0] = NATIVE_STATUS_BUFFER_OVERFLOW;
                return 1;
            }
            memcpy(response + 1, captured, scan_bytes);
            return 1u + scan_bytes;
        }
        return 1;
    }
    case NATIVE_CMD_SCAN_DR_CHECK: {
        if (length < 4) {
            response[0] = NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        const uint32_t bit_count = read_u32(payload);
        const uint32_t scan_bytes = (bit_count + 7u) / 8u;
        if (bit_count == 0 || bit_count > NATIVE_MAX_SCAN_BITS ||
            length != 4u + 3u * scan_bytes || capacity < 5) {
            response[0] = NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        uint8_t captured[NATIVE_MAX_SCAN_BYTES];
        if (!scan_register(false, bit_count, payload + 4, captured, TAP_RUN_TEST_IDLE)) {
            response[0] = NATIVE_STATUS_BUFFER_OVERFLOW;
            return 1;
        }
        uint32_t mismatch = UINT32_MAX;
        const uint8_t *expected = payload + 4u + scan_bytes;
        const uint8_t *mask = expected + scan_bytes;
        for (uint32_t bit = 0; bit < bit_count; ++bit) {
            if (get_bit(mask, bit) && get_bit(captured, bit) != get_bit(expected, bit)) {
                mismatch = bit;
                response[0] = NATIVE_STATUS_CHECK_MISMATCH;
                break;
            }
        }
        write_u32(response + 1, mismatch);
        return 5;
    }
    case NATIVE_CMD_RUNTEST: {
        if (length != 9 || payload[8] >= 16 || capacity < 1) {
            response[0] = NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        const uint32_t cycles = read_u32(payload);
        const uint32_t minimum_us = read_u32(payload + 4);
        if (cycles > 10000000u || minimum_us > 10000000u) {
            response[0] = NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        if (!tap_goto(TAP_RUN_TEST_IDLE)) {
            response[0] = NATIVE_STATUS_TDO_STUCK;
            return 1;
        }
        static const uint8_t zero_tms[8] = {0};
        static const uint8_t one_tdi[8] = {0xff, 0xff, 0xff, 0xff,
                                            0xff, 0xff, 0xff, 0xff};
        uint32_t remaining = cycles;
        while (remaining != 0) {
            const uint32_t chunk = remaining > 64u ? 64u : remaining;
            dap_jtag_sequence_bits(chunk, zero_tms, one_tdi, NULL);
            remaining -= chunk;
        }
        if (minimum_us != 0) {
            sleep_us(minimum_us);
        }
        if (!tap_goto(payload[8])) {
            response[0] = NATIVE_STATUS_TDO_STUCK;
        }
        return 1;
    }
    case NATIVE_CMD_BATCH:
        if (!allow_batch) {
            response[0] = NATIVE_STATUS_BAD_CMD;
            return 1;
        }
        return dispatch_batch(payload, length, response, capacity);
    case NATIVE_CMD_CHAIN_DETECT: {
        if (length != 0 || capacity < 1u + 2u + 4u * NATIVE_MAX_CHAIN_DEVICES) {
            response[0] = NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        uint32_t idcodes[NATIVE_MAX_CHAIN_DEVICES] = {0};
        uint8_t count = 0;
        uint16_t ir_length = 0;
        const uint8_t status = detect_chain(idcodes, &count, &ir_length);
        response[0] = status;
        if (status != NATIVE_STATUS_OK) {
            return 1;
        }
        response[1] = count;
        uint32_t offset = 2;
        for (uint32_t index = 0; index < count; ++index) {
            write_u32(response + offset, idcodes[index]);
            offset += 4;
        }
        write_u16(response + offset, ir_length);
        native_detected_count = count;
        return offset + 2;
    }
    case NATIVE_CMD_CHAIN_CONFIG: {
        if (length < 2) {
            response[0] = NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        const uint8_t count = payload[0];
        const uint8_t active = payload[1];
        if (count == 0 || count > NATIVE_MAX_CHAIN_DEVICES || active >= count ||
            length != 2u + 5u * count ||
            (native_detected_count != 0 && count != native_detected_count)) {
            response[0] = NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        uint32_t total_ir = 0;
        for (uint32_t index = 0; index < count; ++index) {
            const uint8_t ir_length = payload[2u + 5u * index];
            const uint32_t bypass_opcode = read_u32(payload + 3u + 5u * index);
            if (ir_length == 0 || ir_length > 32 ||
                (ir_length < 32 && (bypass_opcode >> ir_length) != 0)) {
                response[0] = NATIVE_STATUS_BAD_LEN;
                return 1;
            }
            total_ir += ir_length;
        }
        if (total_ir > NATIVE_MAX_CHAIN_IR_BITS) {
            response[0] = NATIVE_STATUS_BAD_LEN;
            return 1;
        }
        for (uint32_t index = 0; index < count; ++index) {
            native_chain[index].ir_length = payload[2u + 5u * index];
            native_chain[index].bypass_opcode = read_u32(payload + 3u + 5u * index);
        }
        native_chain_count = count;
        native_active_device = active;
        native_chain_configured = true;
        DAP_Data.jtag_dev.count = count;
        DAP_Data.jtag_dev.index = active;
        uint16_t before = 0;
        uint16_t after = (uint16_t)total_ir;
        for (uint32_t index = 0; index < count; ++index) {
            DAP_Data.jtag_dev.ir_length[index] = native_chain[index].ir_length;
            DAP_Data.jtag_dev.ir_before[index] = before;
            DAP_Data.jtag_dev.ir_after[index] = after;
            before = (uint16_t)(before + native_chain[index].ir_length);
            after = (uint16_t)(after - native_chain[index].ir_length);
        }
        return 1;
    }
    default:
        response[0] = NATIVE_STATUS_BAD_CMD;
        return 1;
    }
}

static void write_response_bytes(const uint8_t *data, uint32_t length) {
    absolute_time_t deadline = make_timeout_time_ms(2000);
    uint32_t offset = 0;
    while (offset < length && tud_vendor_n_mounted(NATIVE_INTERFACE)) {
        const uint32_t written = tud_vendor_n_write(NATIVE_INTERFACE,
                                                    data + offset, length - offset);
        if (written == 0) {
            tud_task();
            if (time_reached(deadline)) {
                return;
            }
        } else {
            offset += written;
        }
    }
}

static void send_response(uint8_t cmd, uint8_t seq, const uint8_t *payload,
                          uint32_t length) {
    uint8_t header[NATIVE_HEADER_SIZE];
    header[0] = cmd;
    header[1] = seq;
    header[2] = 0;
    header[3] = 0;
    write_u32(header + 4, length);
    write_response_bytes(header, sizeof(header));
    write_response_bytes(payload, length);
    tud_vendor_n_write_flush(NATIVE_INTERFACE);
}

static void handle_message(uint8_t cmd, uint8_t seq, uint16_t flags,
                           const uint8_t *payload, uint32_t length) {
    const uint32_t response_length = dispatch_command(
        cmd, payload, length, response_payload,
        sizeof(response_payload), true);
    if ((flags & NATIVE_FLAG_NO_RESPONSE) == 0 && response_length != 0) {
        send_response(cmd, seq, response_payload, response_length);
    }
}

static void process_frame(void) {
    if ((frame_flags & ~(NATIVE_FLAG_MORE | NATIVE_FLAG_NO_RESPONSE)) != 0) {
        response_payload[0] = NATIVE_STATUS_BAD_LEN;
        if ((frame_flags & NATIVE_FLAG_NO_RESPONSE) == 0) {
            send_response(frame_cmd, frame_seq, response_payload, 1);
        }
        return;
    }

    if (fragment_active && (frame_cmd != fragment_cmd || frame_seq != fragment_seq)) {
        response_payload[0] = NATIVE_STATUS_BAD_LEN;
        send_response(fragment_cmd, fragment_seq, response_payload, 1);
        fragment_active = false;
        fragment_payload_used = 0;
    }
    if ((frame_flags & NATIVE_FLAG_MORE) != 0) {
        if (!fragment_active) {
            fragment_active = true;
            fragment_cmd = frame_cmd;
            fragment_seq = frame_seq;
            fragment_payload_used = 0;
        }
        if (frame_payload_used > NATIVE_MAX_PAYLOAD - fragment_payload_used) {
            response_payload[0] = NATIVE_STATUS_BUFFER_OVERFLOW;
            if ((frame_flags & NATIVE_FLAG_NO_RESPONSE) == 0) {
                send_response(frame_cmd, frame_seq, response_payload, 1);
            }
            fragment_active = false;
            fragment_payload_used = 0;
            return;
        }
        memcpy(fragment_payload + fragment_payload_used, frame_payload, frame_payload_used);
        fragment_payload_used += frame_payload_used;
        return;
    }

    if (fragment_active) {
        if (frame_cmd != fragment_cmd || frame_seq != fragment_seq ||
            frame_payload_used > NATIVE_MAX_PAYLOAD - fragment_payload_used) {
            response_payload[0] = NATIVE_STATUS_BAD_LEN;
            if ((frame_flags & NATIVE_FLAG_NO_RESPONSE) == 0) {
                send_response(frame_cmd, frame_seq, response_payload, 1);
            }
            fragment_active = false;
            fragment_payload_used = 0;
            return;
        }
        memcpy(fragment_payload + fragment_payload_used, frame_payload, frame_payload_used);
        fragment_payload_used += frame_payload_used;
        handle_message(frame_cmd, frame_seq, frame_flags,
                       fragment_payload, fragment_payload_used);
        fragment_active = false;
        fragment_payload_used = 0;
        return;
    }
    handle_message(frame_cmd, frame_seq, frame_flags, frame_payload, frame_payload_used);
}

static void reset_frame(void) {
    frame_header_used = 0;
    frame_payload_expected = 0;
    frame_payload_used = 0;
}

static void feed_byte(uint8_t byte) {
    if (frame_discard_remaining != 0) {
        --frame_discard_remaining;
        return;
    }
    if (frame_header_used < NATIVE_HEADER_SIZE) {
        frame_header[frame_header_used++] = byte;
        if (frame_header_used != NATIVE_HEADER_SIZE) {
            return;
        }
        frame_cmd = frame_header[0];
        frame_seq = frame_header[1];
        frame_flags = read_u16(frame_header + 2);
        frame_payload_expected = read_u32(frame_header + 4);
        if (frame_payload_expected > NATIVE_MAX_PAYLOAD) {
            response_payload[0] = NATIVE_STATUS_BAD_LEN;
            if ((frame_flags & NATIVE_FLAG_NO_RESPONSE) == 0) {
                send_response(frame_cmd, frame_seq, response_payload, 1);
            }
            frame_discard_remaining = frame_payload_expected;
            reset_frame();
            return;
        }
        if (frame_payload_expected == 0) {
            process_frame();
            reset_frame();
        }
        return;
    }

    frame_payload[frame_payload_used++] = byte;
    if (frame_payload_used == frame_payload_expected) {
        process_frame();
        reset_frame();
    }
}

void native_protocol_init(void) {
    frame_header_used = 0;
    frame_payload_expected = 0;
    frame_payload_used = 0;
    frame_discard_remaining = 0;
    fragment_payload_used = 0;
    fragment_active = false;
    native_tap_state = TAP_TEST_LOGIC_RESET;
    native_chain_count = 1;
    native_active_device = 0;
    native_detected_count = 0;
    native_chain_configured = false;
    tap_reset();
}

void native_protocol_task(void) {
    uint8_t input[64];
    while (tud_vendor_n_available(NATIVE_INTERFACE) != 0) {
        const uint32_t received = tud_vendor_n_read(
            NATIVE_INTERFACE, input, sizeof(input));
        if (received == 0) {
            break;
        }
        for (uint32_t index = 0; index < received; ++index) {
            feed_byte(input[index]);
        }
    }
}