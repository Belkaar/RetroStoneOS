/*
 * eth_autoip.c - Link-local (APIPA) IPv4 address assignment and peer discovery
 *
 * RFC 3927 / IEEE 802.0: devices in the 169.254.0.0/16 range assign
 * themselves a link-local address via ARP probes (Duplicate Address Detection)
 * and gratuitous ARPs on binding. The Linux kernel handles all of that
 * transparently when you call `ip addr add` — no avahi, no autoipd daemon
 * needed.
 *
 * This module:
 *   1. Derives a stable second octet from the interface MAC (even → .0,
 *      odd → .1) so two directly-linked devices pick different IPs.
 *   2. Calls `ip addr add` to claim the address.
 *   3. Reads /run/rsos/eth_ip and writes peer info to
 *      /run/rsos/eth_peer_ip via ARP table scanning (ip neigh).
 */
#include "eth_autoip.h"

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define RUNDIR   "/run/rsos"
#define ETH_IP   RUNDIR "/eth_ip"
#define PEER_IP  RUNDIR "/eth_peer_ip"
#define STATUS   RUNDIR "/eth_status"

static char self_ip[16];  /* null-terminated, last seen */

/* --------------------------------------------------------------- helpers */

/* Write a string to a file, atomically via tmp+rename. Returns 0 on success. */
static int write_file_atomic(const char *path, const char *content)
{
	char tmp[1024];
	size_t clen = strlen(content);

	(void)clen;  /* used below */
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);

	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return -errno;

	if (write(fd, content, clen) != (ssize_t)clen) {
		close(fd); unlink(tmp); return -EIO;
	}
	close(fd);
	sync();
	if (rename(tmp, path) < 0) { unlink(tmp); return -errno; }
	return 0;
}

/* Read a single line from /run/rsos/eth_ip into `buf`. Returns 0 on success. */
static int read_eth_ip(char *buf, size_t bufsz)
{
	FILE *f = fopen(ETH_IP, "r");
	if (!f) return -ENOENT;
	size_t n = fread(buf, 1, bufsz - 1, f);
	buf[n >= bufsz ? bufsz - 1 : n] = '\0';
	/* Trim trailing whitespace */
	while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' '))
		buf[--n] = '\0';
	fclose(f);
	return n > 0 ? 0 : -EIO;
}

/* Execute a command via system(3). Returns the shell exit status. */
static int run_cmd(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static int run_cmd(const char *fmt, ...)
{
	char cmd[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(cmd, sizeof(cmd), fmt, ap);
	va_end(ap);
	return system(cmd);
}

/* ----------------------------------------------------- eth_autoip_assign */

int eth_autoip_assign(const char *iface)
{
	if (!iface || !*iface) return -EINVAL;

	/* Check if interface exists */
	char path[256];
	snprintf(path, sizeof(path), "/sys/class/net/%s", iface);
	if (access(path, F_OK) != 0) {
		return -ENODEV;
	}

	/* Already have an IP from a prior assignment this session? */
	if (self_ip[0]) return 0;

	char ip_buf[16];
	if (read_eth_ip(ip_buf, sizeof(ip_buf)) == 0 && ip_buf[0]) {
		/* Already assigned by this process earlier in the session. */
		strncpy(self_ip, ip_buf, sizeof(self_ip) - 1);
		self_ip[sizeof(self_ip) - 1] = '\0';
		return 0;
	}

	/* Derive second octet from MAC: even → .0, odd → .1 */
	int mac_last = -1;
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd >= 0) {
		struct ifreq ifr;
		memset(&ifr, 0, sizeof(ifr));
		strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
		ifr.ifr_name[IFNAMSIZ - 1] = '\0';
		if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
			mac_last = (unsigned char)ifr.ifr_hwaddr.sa_data[5];
		}
		close(fd);
	}

	uint8_t second_octet;
	if (mac_last >= 0 && mac_last % 2 != 0) {
		second_octet = 1;
	} else {
		second_octet = 0;
	}

	char ip_str[32];
	snprintf(ip_str, sizeof(ip_str), "169.254.%u.0/16", second_octet);

	/* Try to add the address (idempotent: succeeds if already present). */
	int rc = run_cmd("ip addr add %s dev %s 2>/dev/null; exit 0", ip_str, iface);
	(void)rc;

	/* Read it back from `ip addr show` so we know what the kernel actually assigned. */
	char tmp[128];
	int fd2 = popen("sh -c 'ip addr show $1 | grep \"inet 169\\.254\" | head -1 | awk \'{print $2}\' | cut -d/ -f1' _ " iface, "r");
	if (fd2) {
		if (fgets(tmp, sizeof(tmp), fd2)) {
			size_t len = strlen(tmp);
			while (len > 0 && (tmp[len-1] == '\n' || tmp[len-1] == '\r'))
				tmp[--len] = '\0';
			if (len > 0) {
				strncpy(self_ip, tmp, sizeof(self_ip) - 1);
				self_ip[sizeof(self_ip) - 1] = '\0';
			}
		}
		pclose(fd2);
	}

	if (!self_ip[0]) {
		/* Fallback to our guessed address. */
		snprintf(self_ip, sizeof(self_ip), "169.254.%u.0", second_octet);
	}

	/* Persist it. */
	write_file_atomic(ETH_IP, self_ip);
	write_file_atomic(STATUS, "waiting");

	return 0;
}

/* ------------------------------------------------ eth_autoip_discover_peer */

int eth_autoip_discover_peer(const char *iface, char *out_peer_ip, size_t out_len)
{
	if (!iface || !out_peer_ip || !out_len) return -EINVAL;

	/* Scan the ARP cache for peers in the 169.254.x.x range.
	 * Output of `ip neigh show dev eth0`:
	 *   <IP> dev eth0 lladdr <MAC> REACHABLE|STALE|DELAY|PROBE|FAILED */
	char tmp[512];
	int fd = popen("sh -c 'ip neigh show dev $1 2>/dev/null | grep \"169\\.254\"' _ " iface, "r");
	if (!fd) return -EIO;

	while (fgets(tmp, sizeof(tmp), fd)) {
		/* Parse the first whitespace-delimited token (the IP). */
		char *ip = strtok(tmp, " \t\n\r");
		if (!ip || strncmp(ip, "169.", 4) != 0) continue;

		/* Don't report ourselves as peer. */
		if (self_ip[0] && strcmp(ip, self_ip) == 0) continue;

		strncpy(out_peer_ip, ip, out_len - 1);
		out_peer_ip[out_len - 1] = '\0';

		/* Persist peer IP and update status. */
		write_file_atomic(PEER_IP, out_peer_ip);
		write_file_atomic(STATUS, "connected");

		pclose(fd);
		return 0;
	}

	pclose(fd);
	return -ENODATA;
}

/* --------------------------------------------------- eth_autoip_self_ip */

const char *eth_autoip_self_ip(void)
{
	if (self_ip[0]) return self_ip;

	/* Read from file if not yet cached. */
	if (read_eth_ip(self_ip, sizeof(self_ip)) == 0 && self_ip[0]) {
		return self_ip;
	}
	return NULL;
}
