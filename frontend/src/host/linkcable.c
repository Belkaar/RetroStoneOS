/*
 * linkcable.c - Gambatte Network GameLink support.
 */
#include "linkcable.h"
#include "../transfer/eth_autoip.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RUNDIR "/run/rsos"
#define ETH_IP  RUNDIR "/eth_ip"
#define PEER_IP RUNDIR "/eth_peer_ip"
#define STATUS  RUNDIR "/eth_status"
#define SET_VAR 16

static char self[16], peer[16];

/* Compare two dotted-quad IPs numerically. Returns: -1, 0, 1. */
static int ip_cmp(const char *a, const char *b)
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

/* Strip trailing whitespace from string in place. */
static void strip(char *s)
{
	size_t n = strlen(s);
	while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' '))
		s[--n] = '\0';
}

static void send_variable(retro_environment_t env_cb, const char *key, const char *value)
{
	struct retro_variable v;
	v.key = key;
	v.value = value;
	env_cb(SET_VAR, &v);
}

int linkcable_init(void)
{
	char buf[32];
	FILE *f;

	if ((f = fopen(ETH_IP, "r")) != NULL) { (void)fgets(buf, sizeof(buf), f); fclose(f); strip(buf); strlcpy(self, buf, sizeof(self)); }
	if ((f = fopen(PEER_IP, "r")) != NULL) { (void)fgets(buf, sizeof(buf), f); fclose(f); strip(buf); strlcpy(peer, buf, sizeof(peer)); }
	return 0;
}

bool linkcable_available(void)
{
	char buf[16];
	FILE *f;

	if (!self[0]) {
		const char *s = eth_autoip_self_ip();
		if (s && s[0]) strlcpy(self, s, sizeof(self));
	}
	if (!peer[0] && access(PEER_IP, F_OK) == 0) {
		if ((f = fopen(PEER_IP, "r")) != NULL) { (void)fgets(buf, sizeof(buf), f); fclose(f); strip(buf); strlcpy(peer, buf, sizeof(peer)); }
	}
	return self[0] && peer[0];
}

bool linkcable_set_variables(retro_environment_t env_cb)
{
	bool is_server = ip_cmp(self, peer) < 0;
	char tmp[4];

	if (!linkcable_available()) return false;

	send_variable(env_cb, "gambatte_gb_link_mode", is_server ? "Network Server" : "Network Client");
	send_variable(env_cb, "gambatte_gb_link_network_port", "12345");

	if (!is_server) {
		unsigned oct[4];
		sscanf(peer, "%u.%u.%u.%u", &oct[0], &oct[1], &oct[2], &oct[3]);
		snprintf(tmp, sizeof(tmp), "%u", oct[0]);
		send_variable(env_cb, "gambatte_gb_link_network_server_ip_1", tmp);
		snprintf(tmp, sizeof(tmp), "%u", oct[1]);
		send_variable(env_cb, "gambatte_gb_link_network_server_ip_2", tmp);
		snprintf(tmp, sizeof(tmp), "%u", oct[2]);
		send_variable(env_cb, "gambatte_gb_link_network_server_ip_3", tmp);
	}

	return true;
}

const char *linkcable_status(void)
{
	static const char connected_str[]    = "connected";
	static const char waiting_str[]      = "waiting";
	static const char disconnected_str[] = "disconnected";

	char buf[32];
	FILE *f;

	if (!self[0] && !peer[0]) {
		const char *s = eth_autoip_self_ip();
		if (s && s[0]) strlcpy(self, s, sizeof(self));
		if ((f = fopen(PEER_IP, "r")) != NULL) { (void)fgets(buf, sizeof(buf), f); fclose(f); strip(buf); strlcpy(peer, buf, sizeof(peer)); }
	}
	if (!self[0] || !peer[0]) return self[0] ? waiting_str : disconnected_str;

	if ((f = fopen(STATUS, "r")) != NULL) { (void)fgets(buf, sizeof(buf), f); fclose(f); strip(buf); if (buf[0]) return strdup(buf); }
	return connected_str;
}

const char *linkcable_peer_ip(void)
{
	return peer[0] ? peer : NULL;
}

bool linkcable_is_server(void)
{
	return ip_cmp(self, peer) < 0;
}
