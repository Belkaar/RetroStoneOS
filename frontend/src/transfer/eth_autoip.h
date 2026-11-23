/*
 * eth_autoip.h - Link-local (APIPA) IPv4 address assignment and peer discovery
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

int eth_autoip_assign(const char *iface);
int eth_autoip_discover_peer(const char *iface, char *out_peer_ip, size_t out_len);
const char *eth_autoip_self_ip(void);
