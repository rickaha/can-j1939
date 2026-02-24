/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#include "stack_utils.h"
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

/* SOCKET */

int can_open_socket(const char* ifname, uint64_t name, uint8_t addr) {
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
    addr_can.can_addr.j1939.pgn = J1939_NO_PGN;

    // Tell the kernel to start Address Claiming
    if (bind(sock, (struct sockaddr*)&addr_can, sizeof(addr_can)) < 0) {
        perror("Bind failed");
        close(sock);
        return -1;
    }

    return sock;
}

/* SEND */

int can_send(int sock, uint32_t pgn, uint8_t dest_addr, const void* payload, size_t len) {
    struct sockaddr_can dest = {0};
    dest.can_family = AF_CAN;
    dest.can_addr.j1939.name = J1939_NO_NAME;
    dest.can_addr.j1939.pgn = pgn;

    /*
     * If the upper byte of the PGN (bits 17-16) is 0, it is PDU1
     * and we use the supplied dest_addr. Otherwise force global broadcast (0xFF).
     */
    if ((pgn & 0x030000U) == 0) {
        dest.can_addr.j1939.addr = dest_addr;
    } else {
        dest.can_addr.j1939.addr = J1939_NO_ADDR;
    }

    ssize_t sent = sendto(sock, payload, len, 0, (struct sockaddr*)&dest, sizeof(dest));
    if (sent < 0) {
        perror("can_send: sendto");
        return -1;
    }

    return 0;
}

/* RECEIVE */

int can_receive(int sock, uint32_t* pgn, uint8_t* src_addr, uint8_t* buf, size_t buf_len,
                size_t* recv_len) {
    struct sockaddr_can src = {0};
    socklen_t src_len = sizeof(src);

    ssize_t n = recvfrom(sock, buf, buf_len, 0, (struct sockaddr*)&src, &src_len);
    if (n < 0) {
        perror("can_receive: recvfrom");
        return -1;
    }

    *pgn = src.can_addr.j1939.pgn;
    *src_addr = src.can_addr.j1939.addr;
    *recv_len = (size_t)n;

    return 0;
}
