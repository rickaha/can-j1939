#ifndef J1939_STACK_UTILS_H
#define J1939_STACK_UTILS_H

#include <stdint.h>
#include <linux/can/j1939.h>

/**
 * J1939 NAME Structure
 * This 64-bit value is used for Address Claiming.
 */
typedef struct {
    uint32_t identity_number : 21;
    uint16_t mfg_code        : 11;
    uint8_t  function_inst   : 5;
    uint8_t  function        : 8;
    uint8_t  reserved        : 1;
    uint8_t  vehicle_system  : 7;
    uint8_t  system_inst     : 4;
    uint8_t  industry_group  : 3;
    uint8_t  arbitrary_addr  : 1;
} j1939_name_t;

// Function prototypes for our utility file
int j1939_socket_open(const char *ifname, uint64_t name, uint8_t addr);
void j1939_make_name(j1939_name_t *name_struct, uint32_t id);

#endif
