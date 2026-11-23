/*
 * eth_autoip.h - Link-local (APIPA) IPv4 address assignment and peer discovery
 *
 * RFC 3927 / IEEE 802.0: devices in the 169.254.0.0/16 range assign
 * themselves a link-local address via ARP probes ( Duplicate Address
 * Detection ) and gratuitous ARPs on binding.  The Linux kernel handles
 * all of that transparently when you call `ip addr add` — no avahi,
 * no autoipd daemon needed.
 *
 * This module:
 *   1. Derives a stable second octet from the interface MAC (even → .0,
 *      odd → .1) so two directly-linked devices pick different IPs.
 *   2. Calls `ip addr add` to claim the address.
 *   3. Reads `/run/rsos/eth_ip` and writes peer info to
 *      `/run/rsos/eth_peer_ip` via ARP table scanning (ip neigh).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Initialise and attempt link-local assignment on `iface` (e.g. "eth0").
 * Writes this device's IP to /run/rsos/eth_ip on success.
 * Returns 0 on success, -1 if the interface is missing or address already
 * assigned, -2 for a conflict that exceeded retry attempts. */
int eth_autoip_assign(const char *iface);

/* Discover peers on the link-local subnet by scanning the ARP cache.
 * Writes the first non-self IPv4 to /run/rsos/eth_peer_ip and returns
 * its value in `out_peer_ip` (caller buffer, ≥16 bytes).
 * Returns 0 if a peer was found, -ENODATA if no peer yet. */
int eth_autoip_discover_peer(const char *iface, char *out_peer_ip, size_t out_len);

/* Return the stored self IP from /run/rsos/eth_ip, or NULL if not set. */
const char *eth_autoip_self_ip(void);
