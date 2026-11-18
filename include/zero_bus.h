#ifndef ZERO_BUS_H
#define ZERO_BUS_H

#include <stdint.h>
#include <stddef.h>

#define ZB_MAGIC 0x5A423031  // "ZB01"
#define ZB_SOCKET_PATH "zero_bus.sock"

typedef enum {
    ZB_PING   = 0x01,
    ZB_EXEC   = 0x02,
    ZB_DATA   = 0x03,
    ZB_ERROR  = 0xFF
} zb_msg_type_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;       
    uint8_t  version;     
    uint8_t  type;        
    uint16_t flags;       
    uint32_t payload_len; 
    uint32_t checksum;    
} zb_header_t;

int zb_validate_header(const uint8_t *buffer, size_t len, zb_header_t *out_header);
size_t zb_serialize_packet(uint8_t *dest, uint8_t type, const uint8_t *payload, uint32_t payload_len);

int zb_server_init(void);
int zb_client_connect(void);

#endif
