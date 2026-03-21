#include <endian.h>
#include <linux/can/j1939.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
    int sock = socket(AF_CAN, SOCK_DGRAM, CAN_J1939);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct ifreq ifr = {0};
    strncpy(ifr.ifr_name, "vcan0", IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);

    struct sockaddr_can src = {0};
    src.can_family = AF_CAN;
    src.can_ifindex = ifr.ifr_ifindex;
    src.can_addr.j1939.addr = 0x01;
    src.can_addr.j1939.name = J1939_NO_NAME;
    src.can_addr.j1939.pgn = J1939_NO_PGN;
    if (bind(sock, (struct sockaddr*)&src, sizeof(src)) < 0) {
        perror("bind");
        return 1;
    }

    int bc = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &bc, sizeof(bc));

    uint64_t name = 0x1A0003207FE03039ULL;
    uint64_t name_le = htole64(name);
    uint8_t payload[9];
    memcpy(payload, &name_le, 8);
    payload[8] = 0x90;

    struct sockaddr_can dst = {0};
    dst.can_family = AF_CAN;
    dst.can_addr.j1939.addr = 0x80;
    dst.can_addr.j1939.name = J1939_NO_NAME;
    dst.can_addr.j1939.pgn = 0x00FED8;

    ssize_t n = sendto(sock, payload, sizeof(payload), 0, (struct sockaddr*)&dst, sizeof(dst));
    if (n < 0) {
        perror("sendto");
        return 1;
    }

    printf("Sent Commanded Address: NAME=0x%016llX new_addr=0x90\n", (unsigned long long)name);
    close(sock);
    return 0;
}
