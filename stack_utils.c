/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#include "stack_utils.h"
#include <endian.h>
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
        perror("can_open_socket: create failed");
        return -1;
    }

    // Get the interface index
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("SIOCGIFINDEX");
        close(sock);
        return -1;
    }

    // Bind with J1939_IDLE_ADDR — required by the kernel for dynamic
    // address claiming. The preferred address is supplied in the claim frame.
    memset(&addr_can, 0, sizeof(addr_can));
    addr_can.can_family = AF_CAN;
    addr_can.can_ifindex = ifr.ifr_ifindex;
    addr_can.can_addr.j1939.name = name;
    addr_can.can_addr.j1939.addr = J1939_IDLE_ADDR;
    addr_can.can_addr.j1939.pgn = J1939_NO_PGN;

    if (bind(sock, (struct sockaddr*)&addr_can, sizeof(addr_can)) < 0) {
        perror("can_open_socket: bind failed");
        close(sock);
        return -1;
    }

    // Enable SO_BROADCAST (Required for address claiming)
    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        perror("can_open_socket: SO_BROADCAST failed");
        close(sock);
        return -1;
    }

    // Set RX filter to only receive address claiming related PGNs
    // during the claiming process.
    const struct j1939_filter filt[] = {
        {.pgn = J1939_PGN_ADDRESS_CLAIMED, .pgn_mask = J1939_PGN_PDU1_MAX},
        {.pgn = J1939_PGN_REQUEST, .pgn_mask = J1939_PGN_PDU1_MAX},
        {.pgn = J1939_PGN_ADDRESS_COMMANDED, .pgn_mask = J1939_PGN_MAX},
    };
    if (setsockopt(sock, SOL_CAN_J1939, SO_J1939_FILTER, &filt, sizeof(filt)) < 0) {
        perror("can_open_socket: SO_J1939_FILTER failed");
        close(sock);
        return -1;
    }

    // Rebind with preferred address so the kernel uses it as the source
    // address in the claim frame.
    addr_can.can_addr.j1939.addr = addr;
    if (bind(sock, (struct sockaddr*)&addr_can, sizeof(addr_can)) < 0) {
        perror("can_open_socket: rebind with preferred addr failed");
        close(sock);
        return -1;
    }

    // Send the Address Claimed frame.
    // Payload is our 64-bit NAME in little-endian.
    uint64_t name_le = htole64(name);
    struct sockaddr_can claim_addr;
    memset(&claim_addr, 0, sizeof(claim_addr));
    claim_addr.can_family = AF_CAN;
    claim_addr.can_ifindex = ifr.ifr_ifindex;
    claim_addr.can_addr.j1939.addr = J1939_NO_ADDR;
    claim_addr.can_addr.j1939.pgn = J1939_PGN_ADDRESS_CLAIMED;

    if (sendto(sock, &name_le, sizeof(name_le), 0, (struct sockaddr*)&claim_addr,
               sizeof(claim_addr)) < 0) {
        perror("can_open_socket: addr claim failed");
        close(sock);
        return -1;
    }

    // Wait 250ms — if nobody contests the claim the kernel marks
    // the NAME-SA assignment as valid and allows normal sends.
    usleep(250000);

    // Clear the address claim filters — switch to normal operation.
    if (setsockopt(sock, SOL_CAN_J1939, SO_J1939_FILTER, NULL, 0) < 0) {
        perror("can_open_socket: clear SO_J1939_FILTER failed");
        close(sock);
        return -1;
    }

    // Receive all traffic on the bus.
    int promisc = 1;
    if (setsockopt(sock, SOL_CAN_J1939, SO_J1939_PROMISC, &promisc, sizeof(promisc)) < 0) {
        perror("can_open_socket: SO_J1939_PROMISC failed");
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

    // PDU1 (PF < 0xF0): unicast — use dest_addr.
    // PDU2 (PF >= 0xF0): broadcast — dest_addr is ignored.
    if (((pgn >> 8) & 0xFF) < 0xF0) {
        dest.can_addr.j1939.addr = dest_addr;
    } else {
        dest.can_addr.j1939.addr = J1939_NO_ADDR;
    }

    ssize_t sent = sendto(sock, payload, len, 0, (struct sockaddr*)&dest, sizeof(dest));
    if (sent < 0) {
        perror("can_send: sendto failed");
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
        perror("can_receive: recvfrom failed");
        return -1;
    }

    *pgn = src.can_addr.j1939.pgn;
    *src_addr = src.can_addr.j1939.addr;
    *recv_len = (size_t)n;

    return 0;
}
