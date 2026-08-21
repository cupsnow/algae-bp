/* $Id$
 *
 * @author joelai
 */

#ifndef __APP_NETWORK_H__
#define __APP_NETWORK_H__

#ifdef __cplusplus
extern "C" {
#endif

int app_wifi_init(void);
int app_wifi_deinit(void);

/** Write STA credentials to wpa_supplicant.conf and request reconnect.
 *
 * @param ssid AP SSID; empty/NULL clears official + try configuration
 * @param password PSK; empty string for open network
 */
int app_wifi_provision(const char *ssid, const char *password, unsigned flags);

/** Failsafe-provision STA credentials and request reconnect.
 *
 * Writes wpa_supplicant_try.conf; on DHCP success the state machine commits
 * it to wpa_supplicant.conf. On failure try.conf is discarded and previous
 * config (or softAP) is restored.
 *
 * @param ssid AP SSID; empty/NULL clears try configuration only
 * @param password PSK; empty string for open network
 */
int app_wifi_provision_fallsafe(const char *ssid, const char *password, unsigned flags);

/** Current wifi_state enum value (for debug / UI). */
int app_wifi_get_state(void);

/** Reload configuration and start AP mode or reconnect to router. */
int app_wifi_reload(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __APP_NETWORK_H__
