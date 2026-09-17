// SocketCAN, once, for every port that runs on Linux.
//
// It was inside port/csp_linux.c, where it was the only home a host build
// needed. port/csp_webots.c is the second one -- a simulated drone whose ground
// link is a real bus is how a wx app in Erlang gets to fly it -- and copying a
// socket setup is how two copies drift.
//
// WHAT STAYED BEHIND is what differs per port: WHERE a frame comes from when
// there is no bus. csp_linux.c answers out of its -F stimulus queue and
// csp_webots.c off the keyboard, and neither belongs in a file about sockets.
// So this offers the bus and nothing else; the port decides what else it tries
// first.
//
// Linux only, and that is not a limitation to work around: SocketCAN IS a Linux
// kernel facility. A port on another host has no bus to open and links the
// stubs at the bottom.

#include "csp.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#if defined(__linux__)

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

static int can_fd = -1;

// A BUS BY NAME. `vcan0` from a command line, controllerArgs, or wherever the
// port got it -- this does not care, and returns 0 for "no interface asked
// for" so that a program declaring CAN still parses and runs dry.
int csp_socketcan_open(const char* iface)
{
    struct sockaddr_can addr;
    struct ifreq ifr;

    if ((iface == NULL) || (*iface == '\0'))
	return 0;
    if ((can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
	perror("can: socket");
	return -1;
    }
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ-1);
    if (ioctl(can_fd, SIOCGIFINDEX, &ifr) < 0) {
	fprintf(stderr, "can: no interface '%s': %s\n", iface, strerror(errno));
	close(can_fd);
	can_fd = -1;
	return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(can_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
	perror("can: bind");
	close(can_fd);
	can_fd = -1;
	return -1;
    }
    // Non-blocking: the caller polls once per cycle and must never stall it.
    fcntl(can_fd, F_SETFL, fcntl(can_fd, F_GETFL, 0) | O_NONBLOCK);
    return 0;
}

// The socket, so a main loop can WAIT on frames instead of spinning. -1 means
// there is nothing to wait on.
int csp_socketcan_fd(void)
{
    return can_fd;
}

// 1 = a frame, 0 = nothing waiting, -1 = the bus is broken. Never blocks.
int csp_socketcan_recv(uint32_t* id, uint8_t* data, uint8_t* len)
{
    struct can_frame f;
    ssize_t n;

    if (can_fd < 0)
	return 0;
    if ((n = read(can_fd, &f, sizeof(f))) != (ssize_t)sizeof(f)) {
	if ((n < 0) && (errno != EAGAIN) && (errno != EWOULDBLOCK))
	    return -1;
	return 0;
    }
    *id  = f.can_id & (f.can_id & CAN_EFF_FLAG ? CAN_EFF_MASK : CAN_SFF_MASK);
    *len = f.can_dlc;
    memcpy(data, f.data, f.can_dlc);
    return 1;
}

int csp_socketcan_send(uint32_t id, const uint8_t* data, uint8_t len)
{
    struct can_frame f;

    if (can_fd < 0)
	return 0;
    memset(&f, 0, sizeof(f));
    // Anything that does not fit the 11-bit standard id goes out extended.
    f.can_id  = (id > CAN_SFF_MASK) ? (id | CAN_EFF_FLAG) : id;
    f.can_dlc = (len > 8) ? 8 : len;    // classic CAN via this socket type
    memcpy(f.data, data, f.can_dlc);
    if (write(can_fd, &f, sizeof(f)) != (ssize_t)sizeof(f))
	return -1;
    return 0;
}

#else   /* not Linux: there is no bus to open */

int csp_socketcan_open(const char* iface) { (void)iface; return 0; }
int csp_socketcan_fd(void) { return -1; }
int csp_socketcan_recv(uint32_t* id, uint8_t* data, uint8_t* len)
{
    (void)id; (void)data; (void)len;
    return 0;
}
int csp_socketcan_send(uint32_t id, const uint8_t* data, uint8_t len)
{
    (void)id; (void)data; (void)len;
    return 0;
}

#endif
