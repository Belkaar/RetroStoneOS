/*
 * eth_autoip.c - Link-local (APIPA) IPv4 address assignment and peer discovery
 */
#include "eth_autoip.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
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

static char self_ip[16];

/* --------------------------------------------------------------- helpers */

static int write_file_atomic(const char *path, const char *content)
{
	char tmp[1024];
	size_t clen = strlen(content);

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

static int read_eth_ip(char *buf, size_t bufsz)
{
	FILE *f = fopen(ETH_IP, "r");
	if (!f) return -ENOENT;
	size_t n = fread(buf, 1, bufsz - 1, f);
	buf[n >= bufsz ? bufsz - 1 : n] = '\0';
	while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' '))
		buf[--n] = '\0';
	fclose(f);
	return n > 0 ? 0 : -EIO;
}

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

	char path[256];
	snprintf(path, sizeof(path), "/sys/class/net/%s", iface);
	if (access(path, F_OK) != 0) {
		return -ENODEV;
	}

	if (self_ip[0]) return 0;

	char ip_buf[16];
	if (read_eth_ip(ip_buf, sizeof(ip_buf)) == 0 && ip_buf[0]) {
		strncpy(self_ip, ip_buf, sizeof(self_ip) - 1);
		self_ip[sizeof(self_ip) - 1] = '\0';
		return 0;
	}

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

	uint8_t second_octet = (mac_last >= 0 && mac_last % 2 != 0) ? 1 : 0;

	char ip_str[32];
	snprintf(ip_str, sizeof(ip_str), "169.254.%u.0/16", second_octet);

	int rc = run_cmd("ip addr add %s dev %s 2>/dev/null; exit 0", ip_str, iface);
	(void)rc;

	char tmp[128];
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "sh -c 'ip addr show $1 | grep \"inet 169\\.254\" | head -1 | awk \'{print $2}\' | cut -d/ -f1' _ %s", iface);
	FILE *fd2 = popen(cmd, "r");
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
		snprintf(self_ip, sizeof(self_ip), "169.254.%u.0", second_octet);
	}

	write_file_atomic(ETH_IP, self_ip);
	write_file_atomic(STATUS, "waiting");

	return 0;
}

/* ------------------------------------------------ eth_autoip_discover_peer */

int eth_autoip_discover_peer(const char *iface, char *out_peer_ip, size_t out_len)
{
	if (!iface || !out_peer_ip || !out_len) return -EINVAL;

	char tmp[512];
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "sh -c 'ip neigh show dev $1 2>/dev/null | grep \"169\\.254\"' _ %s", iface);
	FILE *fd = popen(cmd, "r");
	if (!fd) return -EIO;

	while (fgets(tmp, sizeof(tmp), fd)) {
		char *ip = strtok(tmp, " \t\n\r");
		if (!ip || strncmp(ip, "169.", 4) != 0) continue;
		if (self_ip[0] && strcmp(ip, self_ip) == 0) continue;

		strncpy(out_peer_ip, ip, out_len - 1);
		out_peer_ip[out_len - 1] = '\0';

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

	if (read_eth_ip(self_ip, sizeof(self_ip)) == 0 && self_ip[0]) {
		return self_ip;
	}
	return NULL;
}
