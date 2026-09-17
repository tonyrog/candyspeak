// UDP, once, for every port that has a socket.
//
// It was inside port/csp_linux.c, where a host build was the only thing that
// needed it. port/csp_webots.c is the second: Webots' snap is strictly confined
// and plugs only `network`, so a controller it launches cannot open a raw
// PF_CAN socket -- but an ordinary datagram socket is exactly what that plug
// allows, and is what the prompt on 2323 proves every time it answers.
//
// So a program declares
//
//     #buffer Command:16 in udp 3333
//
// and it works the same in ./csp, in Webots, and on any board with a stack.
// That is UDP RAKT AV rather than a bridge that repackages datagrams as CAN
// frames: the transport is a property of the buffer, which is where
// utils/layout.terms already puts it, and no port has to translate.
//
// Nothing here is host-specific beyond BSD sockets. A port supplies nothing;
// it links this file.

#include "csp.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static struct { uint16_t port; int fd; } udp_sock[CSP_UDP_MAXSOCK];
static int udp_nsock = 0;
static int udp_tx_fd = -1;

// Ports we tried to bind and could not. Kept so the message is printed once
// rather than every cycle, and so a doomed bind is not retried a hundred times
// a second for the life of the program.
static uint16_t udp_dead[CSP_UDP_MAXSOCK];
static int udp_ndead = 0;

// A BUS ADDRESS, and it is the one thing that can never be a SENDER: no
// datagram arrives from a broadcast address. So the address on an `in` buffer
// carries two disjoint meanings and needs no keyword to tell them apart --
// a host address is the peer to accept, a broadcast address names the BUS.
//
// The test is the low octet, which covers the two forms anyone writes:
// 255.255.255.255 and a /24's own broadcast (192.168.1.255, and 127.255.255.255
// for a laptop running several nodes against loopback). A host address never
// ends in .255 on a /24, so nothing legitimate is caught by it.
static int udp_is_bus(uint32_t a)
{
    return (a == 0xFFFFFFFFu) || ((a & 0xffu) == 0xffu);
}

static int udp_find(uint16_t port)
{
    int i;
    for (i = 0; i < udp_nsock; i++)
	if (udp_sock[i].port == port)
	    return udp_sock[i].fd;
    return -1;
}

static int udp_gave_up(uint16_t port)
{
    int i;
    for (i = 0; i < udp_ndead; i++)
	if (udp_dead[i] == port)
	    return 1;
    return 0;
}

static void udp_give_up(uint16_t port)
{
    if (udp_ndead < CSP_UDP_MAXSOCK)
	udp_dead[udp_ndead++] = port;
}

int csp_udp_open(csp_rt_t* st, uint16_t port)
{
    return csp_udp_open_bus(st, port, 0);
}

int csp_udp_open_bus(csp_rt_t* st, uint16_t port, int bus)
{
    struct sockaddr_in a;
    int fd, on = 1;
    (void)st;

    if (udp_find(port) >= 0)
	return 0;                      // already listening -- see above
    if (udp_gave_up(port))
	return -1;                     // said why once; not saying it again
    if (udp_nsock >= CSP_UDP_MAXSOCK)
	return -1;
    if ((fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)) < 0)
	return -1;
    // SO_REUSEADDR ONLY ON A BUS, and the difference is the whole point.
    //
    // On UNICAST it means several processes may bind the same port and the
    // kernel hands each datagram to exactly ONE of them -- whichever it likes.
    // A forgotten csp still holding port 12345 then makes the next one bind
    // successfully, receive nothing, and report nothing. That is not a
    // hypothetical: it is what "my program does not get the datagram" turned
    // out to be, and every symptom pointed at the sender. UDP has no TIME_WAIT
    // either, so the restart case the flag is usually there for does not exist.
    //
    // On BROADCAST it means the opposite: every socket bound to the port gets a
    // COPY. That is exactly a bus, and it is the only way several nodes run on
    // one laptop. So the flag follows the declaration -- an `in udp <port>
    // <broadcast>` buffer asks for it, and nothing else gets it.
    //
    // SO_BROADCAST is unconditional: it is needed to SEND to a broadcast
    // address, and an out buffer's socket may be any of these.
    if (bus)
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(fd, (struct sockaddr*)&a, sizeof(a)) < 0) {
	// SAY SO, once, and on STDERR -- like the CAN errors above it. Not
	// through csp_print_*: that is the PROGRAM's output stream, it is NULL
	// until the driver opens it, and the first bind happens on the first
	// cycle. A message written there arrives nowhere, which is precisely the
	// failure this line exists to prevent.
	//
	// Once, because this is polled every cycle: unconditional would be a
	// hundred lines a second, and silence is what made the REUSEADDR bug
	// above take an evening to find.
	fprintf(stderr, "udp: cannot listen on port %u -- %s\n",
		(unsigned)port, strerror(errno));
	close(fd);
	udp_give_up(port);
	return -1;
    }
    udp_sock[udp_nsock].port = port;
    udp_sock[udp_nsock].fd = fd;
    udp_nsock++;
    if (udp_tx_fd < 0)
	udp_tx_fd = fd;
    return 0;
}

int csp_udp_recv(csp_rt_t* st, uint16_t port, uint32_t accept,
		 uint8_t* data, uint16_t* len)
{
    struct sockaddr_in a;
    socklen_t alen;
    ssize_t n;
    int fd, guard;
    uint8_t peek;
    (void)st;

    // Open on first use rather than at setup: the runtime knows which ports a
    // program wants only after it has built its buffer table, and a program
    // edited in the REPL changes that table while running.
    if ((fd = udp_find(port)) < 0) {
	if (csp_udp_open_bus(st, port, udp_is_bus(accept)) < 0)
	    return -1;
	fd = udp_find(port);
    }
    // A bus address is not a peer to match -- see udp_is_bus. Everyone on the
    // bus is welcome; who a message is FOR is the program's business, which is
    // what an id in the payload is for.
    if ((accept == 0) || udp_is_bus(accept)) {
	// No filter: one syscall, straight into the caller's buffer.
	alen = sizeof(a);
	if ((n = recvfrom(fd, data, *len, 0, (struct sockaddr*)&a, &alen)) < 0)
	    return 0;                  // EAGAIN: nothing pending
	*len = (uint16_t)n;
	return 1;
    }
    // FILTERED: PEEK THE SENDER FIRST. `data` is the buffer's own shadow, so a
    // datagram from the wrong peer must not be read into it even to be thrown
    // away -- it would overwrite the last good one with bytes nothing marks.
    // MSG_PEEK fills the address without consuming, so the decision is made
    // before anything lands.
    //
    // Bounded like the core's drain: a flood from the wrong peer must not
    // starve the right one, but it must not own the loop either.
    for (guard = 0; guard < CSP_UDP_RX_BURST; guard++) {
	alen = sizeof(a);
	memset(&a, 0, sizeof(a));
	if (recvfrom(fd, &peek, 1, MSG_PEEK, (struct sockaddr*)&a, &alen) < 0)
	    return 0;                  // EAGAIN: nothing pending
	// Stored in HOST order, which is how `1.2.3.4` and 0x01020304 both read.
	if (ntohl(a.sin_addr.s_addr) != accept) {
	    // Consume and throw away. A UDP read takes the WHOLE datagram
	    // however small the buffer, so one byte drops it -- and it has to be
	    // dropped rather than left, or it sits at the head of the queue and
	    // stalls the port behind it for good.
	    (void)recv(fd, &peek, 1, 0);
	    continue;
	}
	alen = sizeof(a);
	if ((n = recvfrom(fd, data, *len, 0, (struct sockaddr*)&a, &alen)) < 0)
	    return 0;
	*len = (uint16_t)n;
	return 1;
    }
    return 0;
}

// The listening sockets, so the loop can WAIT on a datagram instead of looking
// again every hundred milliseconds -- csp_can_pollfd's counterpart, and what a
// program whose only input is UDP needs to stop spinning.
//
// ENUMERATED rather than handed over as a set, and re-read every time round the
// loop, because a port is bound on FIRST USE: the runtime knows which ports the
// program wants only after it has built its buffer table, and the REPL changes
// that table while running. A set collected once at start would be empty.
//
// slot 0, 1, 2... in open order; -1 past the end.
int csp_udp_pollfd(int slot)
{
    if ((slot < 0) || (slot >= udp_nsock))
	return -1;
    return udp_sock[slot].fd;
}

int csp_udp_send(csp_rt_t* st, uint32_t addr, uint16_t port,
		 const uint8_t* data, uint16_t len)
{
    struct sockaddr_in a;
    (void)st;

    if (udp_tx_fd < 0) {
	int on = 1;
	if ((udp_tx_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)) < 0)
	    return -1;
	setsockopt(udp_tx_fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    }
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    // The address is stored in HOST order -- `udp 0xC0A80102` reads as
    // 192.168.1.2 in the source and has to mean that on a little-endian host
    // too, so the conversion is here and not in the declaration.
    a.sin_addr.s_addr = htonl(addr);
    a.sin_port = htons(port);
    if (sendto(udp_tx_fd, data, len, 0, (struct sockaddr*)&a, sizeof(a)) < 0)
	return -1;
    return 0;
}
