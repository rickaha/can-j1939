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

/* HELPER */

/*
 * Performs a single claim attempt for @addr.
 * Rebinds the socket with @name and @addr, sets socket options,
 * sends the Address Claimed frame and waits 250ms.
 *
 * Note: the kernel does not provide a synchronous "you won" signal.
 * After 250ms with no contest the NAME-SA mapping is considered valid.
 *
 * Returns 0 on success, -1 on error.
 */
static int try_claim(int sock, uint64_t name, uint8_t addr) {
    // Retrieve the interface index stored during can_socket_create().
    struct sockaddr_can bound;
    memset(&bound, 0, sizeof(bound));
    socklen_t bound_len = sizeof(bound);
    if (getsockname(sock, (struct sockaddr*)&bound, &bound_len) < 0) {
        perror("try_claim: getsockname");
        return -1;
    }

    struct sockaddr_can addr_can;
    memset(&addr_can, 0, sizeof(addr_can));
    addr_can.can_family = AF_CAN;
    addr_can.can_ifindex = bound.can_ifindex;
    addr_can.can_addr.j1939.name = name;
    addr_can.can_addr.j1939.addr = addr;
    addr_can.can_addr.j1939.pgn = J1939_NO_PGN;

    if (bind(sock, (struct sockaddr*)&addr_can, sizeof(addr_can)) < 0) {
        perror("try_claim: bind");
        return -1;
    }

    // Enable SO_BROADCAST (Required for address claiming)
    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        perror("try_claim: SO_BROADCAST");
        return -1;
    }

    // Filter to address claiming PGNs only during negotiation.
    const struct j1939_filter filt[] = {
        {.pgn = J1939_PGN_ADDRESS_CLAIMED, .pgn_mask = J1939_PGN_PDU1_MAX},
        {.pgn = J1939_PGN_REQUEST, .pgn_mask = J1939_PGN_PDU1_MAX},
        {.pgn = J1939_PGN_ADDRESS_COMMANDED, .pgn_mask = J1939_PGN_MAX},
    };
    if (setsockopt(sock, SOL_CAN_J1939, SO_J1939_FILTER, &filt, sizeof(filt)) < 0) {
        perror("try_claim: SO_J1939_FILTER");
        return -1;
    }

    // Send the Address Claimed frame.
    // Payload is our 64-bit NAME in little-endian.
    uint64_t name_le = htole64(name);
    struct sockaddr_can claim_addr;
    memset(&claim_addr, 0, sizeof(claim_addr));
    claim_addr.can_family = AF_CAN;
    claim_addr.can_addr.j1939.addr = J1939_NO_ADDR;
    claim_addr.can_addr.j1939.pgn = J1939_PGN_ADDRESS_CLAIMED;
    if (sendto(sock, &name_le, sizeof(name_le), 0, (struct sockaddr*)&claim_addr,
               sizeof(claim_addr)) < 0) {
        perror("try_claim: sendto");
        return -1;
    }

    // Wait 250ms — if nobody contests the claim the kernel marks
    // the NAME-SA assignment as valid and allows normal sends.
    usleep(250000);

    // Clear claim filters and enable promiscuous mode for normal operation.
    if (setsockopt(sock, SOL_CAN_J1939, SO_J1939_FILTER, NULL, 0) < 0) {
        perror("try_claim: clear SO_J1939_FILTER");
        return -1;
    }

    int promisc = 1;
    if (setsockopt(sock, SOL_CAN_J1939, SO_J1939_PROMISC, &promisc, sizeof(promisc)) < 0) {
        perror("try_claim: SO_J1939_PROMISC");
        return -1;
    }

    return 0;
}

/* SOCKET */

int can_socket_create(const char* ifname) {
    struct ifreq ifr;

    // Create the J1939 Socket
    int sock = socket(AF_CAN, SOCK_DGRAM, CAN_J1939);
    if (sock < 0) {
        perror("can_socket_create: socket");
        return -1;
    }

    // Get the interface index
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("can_socket_create: SIOCGIFINDEX");
        close(sock);
        return -1;
    }

    // Bind with J1939_NO_NAME and J1939_IDLE_ADDR to register the
    // interface index with the socket before address claiming.
    struct sockaddr_can addr_can;
    memset(&addr_can, 0, sizeof(addr_can));
    addr_can.can_family = AF_CAN;
    addr_can.can_ifindex = ifr.ifr_ifindex;
    addr_can.can_addr.j1939.name = J1939_NO_NAME;
    addr_can.can_addr.j1939.addr = J1939_IDLE_ADDR;
    addr_can.can_addr.j1939.pgn = J1939_NO_PGN;

    if (bind(sock, (struct sockaddr*)&addr_can, sizeof(addr_can)) < 0) {
        perror("can_socket_create: bind");
        close(sock);
        return -1;
    }

    return sock;
}

int can_address_claim(int sock, uint64_t name, uint8_t addr) {
    if (try_claim(sock, name, addr) < 0) {
        fprintf(stderr, "can_address_claim: failed to claim 0x%02X\n", addr);
        return -1;
    }
    return addr;
}

int can_address_claim_dynamic(int sock, uint64_t name, uint8_t preferred_addr) {
    for (uint8_t addr = preferred_addr; addr <= 247; addr++) {
        if (try_claim(sock, name, addr) == 0) {
            printf("can_address_claim_dynamic: claimed address 0x%02X\n", addr);
            return addr;
        }
        fprintf(stderr, "can_address_claim_dynamic: 0x%02X failed, trying next\n", addr);
    }

    // Range exhausted — send Cannot Claim Address (source = J1939_IDLE_ADDR = 0xFE).
    struct sockaddr_can bound;
    memset(&bound, 0, sizeof(bound));
    socklen_t bound_len = sizeof(bound);
    getsockname(sock, (struct sockaddr*)&bound, &bound_len);

    struct sockaddr_can idle;
    memset(&idle, 0, sizeof(idle));
    idle.can_family = AF_CAN;
    idle.can_ifindex = bound.can_ifindex;
    idle.can_addr.j1939.name = name;
    idle.can_addr.j1939.addr = J1939_IDLE_ADDR;
    idle.can_addr.j1939.pgn = J1939_NO_PGN;
    if (bind(sock, (struct sockaddr*)&idle, sizeof(idle))) {
        perror("can_address_dynamic: bind");
        return -1;
    }

    uint64_t name_le = htole64(name);
    const struct sockaddr_can cannot_claim = {
        .can_family = AF_CAN,
        .can_addr.j1939.pgn = J1939_PGN_ADDRESS_CLAIMED,
        .can_addr.j1939.addr = J1939_NO_ADDR,
    };
    sendto(sock, &name_le, sizeof(name_le), 0, (const struct sockaddr*)&cannot_claim,
           sizeof(cannot_claim));

    fprintf(stderr, "can_address_claim_dynamic: address range exhausted\n");
    return -1;
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
