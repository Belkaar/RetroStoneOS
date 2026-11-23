/*
 * linkcable.c - Gambatte Network GameLink support for RetroStoneOS
 */
#include "linkcable.h"
#include "../transfer/eth_autoip.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUNDIR "/run/rsos"
#define ETH_IP   RUNDIR "/eth_ip"
#define PEER_IP  RUNDIR "/eth_peer_ip"
#define STATUS   RUNDIR "/eth_status"

/* Compare two dotted-quad IPs numerically. Returns:
 *   -1 if a < b, 0 if equal, 1 if a > b */
static int ip_compare(const char *a, const char *b)
{
	unsigned pa[4], pb[4];
	if (sscanf(a, "%u.%u.%u.%u", &pa[0], &pa[1], &pa[2], &pa[3]) != 4) return -1;
	if (sscanf(b, "%u.%u.%u.%u", &pb[0], &pb[1], &pb[2], &pb[3]) != 4) return -1;

	for (int i = 0; i < 4; i++) {
		if (pa[i] < pb[i]) return -1;
		if (pa[i] > pb[i]) return 1;
	}
	return 0;
}

/* ------------------------------------------------------- static data */

static char self[16];
static char peer[16];

/* ----------------------------------------------------- linkcable_init */

int linkcable_init(void)
{
	/* Read current state from /run/rsos if already present */
	FILE *f = fopen(ETH_IP, "r");
	if (f) {
		fgets(self, sizeof(self), f);
		fclose(f);
		size_t len = strlen(self);
		while (len > 0 && (self[len-1] == '\n' || self[len-1] == ' '))
			self[--len] = '\0';
	}

	f = fopen(PEER_IP, "r");
	if (f) {
		fgets(peer, sizeof(peer), f);
		fclose(f);
		size_t len = strlen(peer);
		while (len > 0 && (peer[len-1] == '\n' || peer[len-1] == ' '))
			peer[--len] = '\0';
	}
	return 0;
}

/* ------------------------------------------------ linkcable_available */

bool linkcable_available(void)
{
	/* Must have both self IP and peer IP */
	if (!self[0] || !peer[0]) {
		/* Try reading from files if not cached */
		const char *s = eth_autoip_self_ip();
		if (s && strlen(s)) strncpy(self, s, sizeof(self)-1);

		if (access(PEER_IP, F_OK) == 0) {
			FILE *f = fopen(PEER_IP, "r");
			if (f) {
				fgets(peer, sizeof(peer), f);
				fclose(f);
				size_t len = strlen(peer);
				while (len > 0 && (peer[len-1] == '\n' || peer[len-1] == ' '))
					peer[--len] = '\0';
			}
		}
	}

	return self[0] != '\0' && peer[0] != '\0';
}

/* ------------------------------------------------ linkcable_set_variables */

bool linkcable_set_variables(void *env_cb)
{
	struct retro_variable v;
	bool is_server = ip_compare(self, peer) < 0;
	char tmp[4];
	int (*set_var)(const struct retro_variable *) = env_cb;

	if (!linkcable_available()) return false;

	/* gambatte_gb_link_mode */
	v.key = "gambatte_gb_link_mode";
	v.value = is_server ? "Network Server" : "Network Client";
	set_var(&v);

	/* gambatte_gb_link_network_port */
	v.key = "gambatte_gb_link_network_port";
	v.value = "12345";
	set_var(&v);

	if (!is_server) {
		/* Set server IP for client: split into 3 variables */
		unsigned oct[4];
		sscanf(peer, "%u.%u.%u.%u", &oct[0], &oct[1], &oct[2], &oct[3]);

		snprintf(tmp, sizeof(tmp), "%u", oct[0]);
		v.key = "gambatte_gb_link_network_server_ip_1"; v.value = tmp;
		set_var(&v);
		snprintf(tmp, sizeof(tmp), "%u", oct[1]);
		v.key = "gambatte_gb_link_network_server_ip_2"; v.value = tmp;
		set_var(&v);
		snprintf(tmp, sizeof(tmp), "%u", oct[2]);
		v.key = "gambatte_gb_link_network_server_ip_3"; v.value = tmp;
		set_var(&v);
	}

	return true;
}

/* ------------------------------------------------ linkcable_status */

const char *linkcable_status(void)
{
	if (!self[0] && !peer[0]) {
		/* Try to read from files */
		const char *s = eth_autoip_self_ip();
		if (s) strncpy(self, s, sizeof(self)-1);

		FILE *f = fopen(STATUS, "r");
		if (f) {
			char buf[32];
			fgets(buf, sizeof(buf), f);
			fclose(f);
			size_t len = strlen(buf);
			while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == ' '))
				buf[--len] = '\0';
			if (strlen(buf) > 0) return buf;
		}

		f = fopen(PEER_IP, "r");
		if (f) {
			fgets(peer, sizeof(peer), f);
			fclose(f);
			size_t len = strlen(peer);
			while (len > 0 && (peer[len-1] == '\n' || peer[len-1] == ' '))
				peer[--len] = '\0';
		}
	}

	if (self[0] && peer[0]) {
		/* Check the status file */
		FILE *f = fopen(STATUS, "r");
		if (f) {
			char buf[32];
			fgets(buf, sizeof(buf), f);
			fclose(f);
			size_t len = strlen(buf);
			while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == ' '))
				buf[--len] = '\0';
			if (strlen(buf) > 0) return buf;
		}
		return "connected";
	}

	if (self[0]) return "waiting";
	return "disconnected";
}
