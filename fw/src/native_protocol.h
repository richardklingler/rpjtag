#ifndef RPJTAG_NATIVE_PROTOCOL_H
#define RPJTAG_NATIVE_PROTOCOL_H

void native_protocol_init(void);
void native_protocol_task(void);
void native_protocol_get_stats(uint32_t *received_bytes, uint32_t *frames,
							   uint32_t *responses);

#endif