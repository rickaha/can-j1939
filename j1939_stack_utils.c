/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include "j1939_stack_utils.h"

int j1939_socket_open(const char *ifname, uint64_t name, uint8_t addr) {
    int sock;
    struct sockaddr_can addr_can;
    struct ifreq ifr;

    // Create the J1939 Socket
    sock = socket(AF_CAN, SOCK_DGRAM, CAN_J1939);
    if (sock < 0) {
        perror("Socket creation failed");
        return -1;
    }

    // Get the interface index (e.g., for "can0")
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("SIOCGIFINDEX");
        close(sock);
        return -1;
    }

    // Configure the J1939 Address/Name
    memset(&addr_can, 0, sizeof(addr_can));
    addr_can.can_family = AF_CAN;
    addr_can.can_ifindex = ifr.ifr_ifindex;
    addr_can.can_addr.j1939.name = name;
    addr_can.can_addr.j1939.addr = addr;
    addr_can.can_addr.j1939.pgn  = J1939_NO_PGN;

    // Tell the kernel to start Address Claiming
    if (bind(sock, (struct sockaddr *)&addr_can, sizeof(addr_can)) < 0) {
        perror("Bind failed");
        close(sock);
        return -1;
    }

    return sock;
}
