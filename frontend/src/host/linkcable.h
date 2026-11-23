/*
 * linkcable.h - Gambatte Network GameLink support for RetroStoneOS
 *
 * When the "linkcable_auto" setting is enabled and a direct Ethernet link
 * exists, this module:
 *   1. Reads /run/rsos/eth_ip (self) and /run/rsos/eth_peer_ip (peer).
 *   2. Determines server/client by IP comparison (lower = server).
 *   3. Injects Gambatte environment variables before retro_load_game():
 *      - gambatte_gb_link_mode: "Network Server" or "Network Client"
 *      - gambatte_gb_link_network_port: 12345
 *      - gambatte_gb_link_network_server_ip_N: peer IP octets (client only)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Initialise linkcable support. Called once at host startup. Returns 0. */
int linkcable_init(void);

/* Determine if netplay is available and should be enabled.
 * Checks /run/rsos/eth_ip and /run/rsos/eth_peer_ip.
 * Returns true if both files exist and contain valid IPs. */
bool linkcable_available(void);

/* Set the Gambatte network variables via retro_environment(SET_VARIABLE).
 * Call this from within the core environment callback, before
 * retro_load_game(). The env_cb pointer is the standard libretro
 * environment callback function.
 * Returns true if variables were set (netplay mode active), false otherwise. */
bool linkcable_set_variables(retro_environment_t env_cb);

/* Return the current link status string for UI display.
 * Values: "connected", "waiting", "disconnected", or NULL. */
const char *linkcable_status(void);
