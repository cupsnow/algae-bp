#include "app/app_common.h"
#include "app/app_network.h"

#include "project_config.h"

#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

static const char *tag = "net";

/*
- try provision concept:
  write to wpa_supplicant_try.conf
  trigger reset and run state machine
  if successful connected:
    move wpa_supplicant_try.conf to wpa_supplicant.conf

- try provision memo:
  state machine may housekeeping try.conf
  startup remove try.conf

- WIFI_STATE_INIT:
  if interface is ready:
    goto state WIFI_STATE_RESET

- WIFI_STATE_RESET:
  kill hostapd, dnsmasq, wpa_supplicant, udhcpc (housekeeping to state machine)
  flush ip
  reset interface
  if try.conf is configured:
    start wpa_supplicant
    goto state WIFI_STATE_CONNECTING
  if wpa_supplicant.conf network is configured:
    start wpa_supplicant
    goto state WIFI_STATE_CONNECTING
  start hostapd, dnsmasq
  goto state WIFI_STATE_PROVISIONING

- WIFI_STATE_PROVISIONING
  if wpa_supplicant.conf network is configured:
    goto state WIFI_STATE_RESET

- WIFI_STATE_CONNECTING
  if connected:
    start udhcpc
    goto state WIFI_STATE_IPSETUP
  if timeout:
	remove try.conf
    goto state WIFI_STATE_RESET

- WIFI_STATE_IPSETUP
  if ip is configured
	if try.conf is configured:
		move try.conf to wpa_supplicant.conf
    goto state WIFI_STATE_CONNECTED
  if timeout:
    flush ip
    kill udhcpc (housekeeping to connect)
	if try.conf is configured:
		remove try.conf
		goto state WIFI_STATE_RESET
	goto state WIFI_STATE_CONNECTING

- WIFI_STATE_CONNECTED
  if disconnected:
    goto state WIFI_STATE_RESET
 */

enum {
	WIFI_STATE_INIT = 0,
	WIFI_STATE_RESET,
	WIFI_STATE_PROVISIONING, /**< ie. AP mode */
	WIFI_STATE_CONNECTING,
	WIFI_STATE_IPSETUP,
	WIFI_STATE_CONNECTED,
};

#define WIFI_IFACE                 "wlan0"
#define WIFI_WPASUP_CONF           "/usrdata/wpa_supplicant.conf"
#define WIFI_WPASUP_TRY_CONF       "/usrdata/wpa_supplicant_try.conf"
#define WIFI_WPASUP_CONF_TMP       "/usrdata/wpa_supplicant.conf.tmp"
#define WIFI_HOSTAPD_CONF          "/tmp/hostapd-wlan0.conf"
#define WIFI_DNSMASQ_CONF          "/tmp/dnsmasq-wlan0.conf"
#define WIFI_AP_ADDR               "192.168.17.1"
#define WIFI_AP_NETMASK            "255.255.255.0"
#define WIFI_AP_DHCP_START         "192.168.17.50"
#define WIFI_AP_DHCP_END           "192.168.17.200"
#define WIFI_AP_SSID               "IPCam-Setup"
#define WIFI_CONNECT_TIMEOUT_S     30
#define WIFI_IPSETUP_TIMEOUT_S     30
#define WIFI_LOOP_DELAY_MS         1000

static PlatformTaskHandle wifi_task_handle = NULL;
static volatile int wifi_running = 0;
static volatile int wifi_process_exit = 0;
static volatile int wifi_request_reset = 0;
static int wifi_state = WIFI_STATE_INIT;
static time_t wifi_state_deadline = 0;

static const char *wifi_state_name(int state)
{
	switch (state) {
	case WIFI_STATE_INIT:         return "INIT";
	case WIFI_STATE_RESET:        return "RESET";
	case WIFI_STATE_PROVISIONING: return "PROVISIONING";
	case WIFI_STATE_CONNECTING:   return "CONNECTING";
	case WIFI_STATE_IPSETUP:      return "IPSETUP";
	case WIFI_STATE_CONNECTED:    return "CONNECTED";
	default:                      return "UNKNOWN";
	}
}

static void wifi_set_state(int state, int timeout_s)
{
	if (wifi_state != state) {
		info(tag, "state %s -> %s", wifi_state_name(wifi_state),
				wifi_state_name(state));
	}
	wifi_state = state;
	if (timeout_s > 0) {
		wifi_state_deadline = time(NULL) + timeout_s;
	} else {
		wifi_state_deadline = 0;
	}
}

static int wifi_state_timed_out(void)
{
	if (wifi_state_deadline == 0) {
		return 0;
	}
	return time(NULL) >= wifi_state_deadline;
}

static int wifi_run(const char *cmd) {
	int ret;

	if ((ret = system(cmd)) != 0) {
		debug(tag, "cmd failed (%d): %s", ret, cmd);
	}
	return ret;
}

static int wifi_iface_exists(void) {
	char path[64];
	struct stat st;

	snprintf(path, sizeof(path), "/sys/class/net/%s", WIFI_IFACE);
	return (stat(path, &st) == 0);
}

static int wifi_iface_is_up(void) {
	int fd = -1, ret = 0;
	struct ifreq ifr;

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		goto finally;
	}

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, WIFI_IFACE, IFNAMSIZ - 1);
	if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
		goto finally;
	}
	ret = (ifr.ifr_flags & IFF_UP) ? 1 : 0;
finally:
	if (fd != -1) close(fd);
	return ret;
}

static int wifi_iface_ready(void) {
	return wifi_iface_exists();
}

static void wifi_kill_daemons(void) {
	/* Ignore failures — process may not be running. */
	(void)wifi_run("killall -9 hostapd dnsmasq wpa_supplicant udhcpc >/dev/null 2>&1");
	time_delay_ms(200);
}

static void wifi_flush_ip(void) {
	char cmd[128];

	snprintf(cmd, sizeof(cmd), "ip addr flush dev %s >/dev/null 2>&1", WIFI_IFACE);
	(void)wifi_run(cmd);
}

static void wifi_reset_interface(void) {
	char cmd[128];

	wifi_flush_ip();

	snprintf(cmd, sizeof(cmd), "ip link set %s down >/dev/null 2>&1", WIFI_IFACE);
	(void)wifi_run(cmd);
	time_delay_ms(200);

	snprintf(cmd, sizeof(cmd), "ip link set %s up >/dev/null 2>&1", WIFI_IFACE);
	(void)wifi_run(cmd);
	time_delay_ms(300);
}

/** Check if a wpa_supplicant conf has a usable network.
 * - file readable
 * - network block exists
 * - ssid is not empty
 */
static int wpasup_network_configured(const char *cfg_file) {
	FILE *fp;
	char line[256];
	int in_network = 0;
	int has_ssid = 0;

	if (!cfg_file) {
		return 0;
	}

	fp = fopen(cfg_file, "r");
	if (!fp) {
		return 0;
	}

	while (fgets(line, sizeof(line), fp) != NULL) {
		char *p = line;

		while (*p && isspace((unsigned char)*p)) {
			p++;
		}
		if (*p == '#' || *p == '\0') {
			continue;
		}

		if (!in_network) {
			if (strncmp(p, "network=", 8) == 0) {
				in_network = 1;
				has_ssid = 0;
			}
			continue;
		}

		if (*p == '}') {
			if (has_ssid) {
				fclose(fp);
				return 1;
			}
			in_network = 0;
			continue;
		}

		if (strncmp(p, "ssid=", 5) == 0) {
			p += 5;
			while (*p && isspace((unsigned char)*p)) {
				p++;
			}
			if (*p == '"') {
				p++;
				if (*p != '"' && *p != '\0' && *p != '\n') {
					has_ssid = 1;
				}
			} else if (*p != '\0' && *p != '\n') {
				has_ssid = 1;
			}
		}
	}

	fclose(fp);
	return 0;
}

static void wifi_remove_try_conf(void) {
	if (unlink(WIFI_WPASUP_TRY_CONF) == 0) {
		info(tag, "removed %s", WIFI_WPASUP_TRY_CONF);
	}
}

static int wifi_commit_try_conf(void) {
	if (!wpasup_network_configured(WIFI_WPASUP_TRY_CONF)) {
		return SUCCESS;
	}

	if (rename(WIFI_WPASUP_TRY_CONF, WIFI_WPASUP_CONF) != 0) {
		dk_error(tag, "commit try.conf failed: %s", strerror(errno));
		return FAIL;
	}
	info(tag, "committed try.conf -> %s", WIFI_WPASUP_CONF);
	return SUCCESS;
}

static int wifi_write_file(const char *path, const char *content) {
	FILE *fp = fopen(path, "w");
	if (!fp) {
		dk_error(tag, "fopen(%s) failed: %s", path, strerror(errno));
		return FAIL;
	}
	if (fputs(content, fp) < 0) {
		dk_error(tag, "fputs(%s) failed: %s", path, strerror(errno));
		fclose(fp);
		return FAIL;
	}
	fclose(fp);
	return SUCCESS;
}

static int wifi_start_ap(void) {
	char cmd[256];
	char conf[512];

	snprintf(conf, sizeof(conf),
			"interface=%s\n"
			"driver=nl80211\n"
			"ssid=%s\n"
			"hw_mode=g\n"
			"channel=6\n"
			"auth_algs=1\n"
			"ignore_broadcast_ssid=0\n",
			WIFI_IFACE, WIFI_AP_SSID);
	if (wifi_write_file(WIFI_HOSTAPD_CONF, conf) != SUCCESS) {
		return FAIL;
	}

	snprintf(conf, sizeof(conf),
			"interface=%s\n"
			"bind-interfaces\n"
			"dhcp-range=%s,%s,%s,12h\n",
			WIFI_IFACE, WIFI_AP_DHCP_START, WIFI_AP_DHCP_END, WIFI_AP_NETMASK);
	if (wifi_write_file(WIFI_DNSMASQ_CONF, conf) != SUCCESS) {
		return FAIL;
	}

	snprintf(cmd, sizeof(cmd),
			"ifconfig %s %s netmask %s up >/dev/null 2>&1",
			WIFI_IFACE, WIFI_AP_ADDR, WIFI_AP_NETMASK);
	if (wifi_run(cmd) != 0) {
		warn(tag, "failed to set AP address");
	}

	snprintf(cmd, sizeof(cmd), "hostapd -B %s >/dev/null 2>&1", WIFI_HOSTAPD_CONF);
	if (wifi_run(cmd) != 0) {
		dk_error(tag, "failed to start hostapd");
		return FAIL;
	}

	snprintf(cmd, sizeof(cmd), "dnsmasq -C %s >/dev/null 2>&1", WIFI_DNSMASQ_CONF);
	if (wifi_run(cmd) != 0) {
		dk_error(tag, "failed to start dnsmasq");
		return FAIL;
	}

	info(tag, "AP started ssid=%s ip=%s", WIFI_AP_SSID, WIFI_AP_ADDR);
	return SUCCESS;
}

static int wifi_start_wpa(const char *cfg_file) {
	char cmd[256];

	if (!cfg_file) {
		return FAIL;
	}

	snprintf(cmd, sizeof(cmd),
			 "wpa_supplicant -B -i %s -c %s >/dev/null 2>&1",
			 WIFI_IFACE, cfg_file);
	if (wifi_run(cmd) != 0) {
		dk_error(tag, "failed to start wpa_supplicant (%s)", cfg_file);
		return FAIL;
	}
	info(tag, "wpa_supplicant started (%s)", cfg_file);
	return SUCCESS;
}

static int wifi_start_udhcpc(void) {
	char cmd[128];

	(void)wifi_run("killall -9 udhcpc >/dev/null 2>&1");
	time_delay_ms(100);

	/* Background: state machine polls for IP. */
	snprintf(cmd, sizeof(cmd),
			 "udhcpc -b -i %s -R -t 20 -T 2 -p /var/run/udhcpc.pid >/dev/null 2>&1",
			 WIFI_IFACE);
	if (wifi_run(cmd) != 0) {
		warn(tag, "udhcpc start returned non-zero");
	}
	return SUCCESS;
}

static int wifi_wpa_connected(void) {
	FILE *fp;
	char line[128];
	int completed = 0;

	fp = popen("wpa_cli -i " WIFI_IFACE " status 2>/dev/null", "r");
	if (!fp) {
		return 0;
	}

	while (fgets(line, sizeof(line), fp) != NULL) {
		if (strncmp(line, "wpa_state=COMPLETED", 19) == 0) {
			completed = 1;
			break;
		}
	}
	pclose(fp);
	return completed;
}

static int wifi_ip_configured(void) {
	int fd;
	struct ifreq ifr;
	struct sockaddr_in *addr;

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		return 0;
	}
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, WIFI_IFACE, IFNAMSIZ - 1);
	if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
		close(fd);
		return 0;
	}
	close(fd);
	addr = (struct sockaddr_in *)&ifr.ifr_addr;
	return (addr->sin_addr.s_addr != 0) ? 1 : 0;
}

static int wifi_conf_needs_escape(const char *s) {
	for (; *s; s++) {
		if (*s == '"' || *s == '\\' || *s == '\n' || *s == '\r') {
			return 1;
		}
	}
	return 0;
}

static int wifi_write_provision_conf(const char *ssid, const char *password,
		unsigned flags, const char *cfg_file) {
	int ret = FAIL, r;
	FILE *fp = NULL;
	int open_net = (password == NULL) || (password[0] == '\0');

	if (!ssid || ssid[0] == '\0' || !cfg_file) {
		dk_error(tag, "Invalid argument");
		goto finally;
	}
	if (wifi_conf_needs_escape(ssid)
			|| (!open_net && wifi_conf_needs_escape(password))) {
		dk_error(tag, "ssid/password contains unsupported characters");
		goto finally;
	}
	if (!open_net && (strlen(password) < 8 || strlen(password) > 63)) {
		dk_error(tag, "password length must be 8..63");
		goto finally;
	}

	if (!(fp = fopen(WIFI_WPASUP_CONF_TMP, "w"))) {
		r = errno;
		dk_error(tag, "fopen(%s) failed: %s", WIFI_WPASUP_CONF_TMP, strerror(r));
		goto finally;
	}

	fprintf(fp,
			"ctrl_interface=%s\n"
			"update_config=1\n",
			"/var/run/wpa_supplicant");

	if (open_net) {
		fprintf(fp, "\n"
				"network={\n"
				"  scan_ssid=1\n"
				"  ssid=\"%s\"\n"
				"  key_mgmt=NONE\n"
				"}\n", ssid);
	} else {
		fprintf(fp, "\n"
				"network={\n"
				"  scan_ssid=1\n"
				"  ssid=\"%s\"\n"
				"  key_mgmt=SAE\n"
				"  ieee80211w=2\n"
				"  psk=\"%s\"\n"
				"}\n", ssid, password);

		fprintf(fp, "\n"
				"network={\n"
				"  scan_ssid=1\n"
				"  ssid=\"%s\"\n"
				"  key_mgmt=WPA-PSK\n"
				"  psk=\"%s\"\n"
				"}\n", ssid, password);
	}

	if (rename(WIFI_WPASUP_CONF_TMP, cfg_file) != 0) {
		r = errno;
		dk_error(tag, "rename to %s failed: %s", cfg_file, strerror(r));
		unlink(WIFI_WPASUP_CONF_TMP);
		goto finally;
	}

	info(tag, "Provisioned ssid=\"%s\" open=%d", ssid, open_net);
	ret = SUCCESS;
finally:
	if (fp
//			&& fp != stdout && fp != stderr
	) {
		fclose(fp);
	}
	return ret;
}

static void wifi_handle_init(void) {
	if (wifi_iface_ready()) {
		wifi_set_state(WIFI_STATE_RESET, 0);
	}
}

static void wifi_handle_reset(void) {
	const char *cfg = NULL;

	wifi_kill_daemons();
	wifi_reset_interface();

	if (wpasup_network_configured(WIFI_WPASUP_TRY_CONF)) {
		cfg = WIFI_WPASUP_TRY_CONF;
	} else if (wpasup_network_configured(WIFI_WPASUP_CONF)) {
		cfg = WIFI_WPASUP_CONF;
	}

	if (cfg) {
		if (wifi_start_wpa(cfg) != SUCCESS) {
			warn(tag, "wpa start failed; will retry RESET");
			return;
		}
		wifi_set_state(WIFI_STATE_CONNECTING, WIFI_CONNECT_TIMEOUT_S);
		return;
	}

	if (wifi_start_ap() != SUCCESS) {
		warn(tag, "AP start failed; will retry RESET");
		return;
	}
	wifi_set_state(WIFI_STATE_PROVISIONING, 0);
}

static void wifi_handle_provisioning(void) {
	/* Only official conf leaves AP; try provision requests RESET via API. */
	if (wpasup_network_configured(WIFI_WPASUP_CONF)) {
		info(tag, "credentials present; leaving AP mode");
		wifi_set_state(WIFI_STATE_RESET, 0);
	}
}

static void wifi_handle_connecting(void) {
	if (wifi_wpa_connected()) {
		wifi_start_udhcpc();
		wifi_set_state(WIFI_STATE_IPSETUP, WIFI_IPSETUP_TIMEOUT_S);
		return;
	}
	if (wifi_state_timed_out()) {
		warn(tag, "CONNECTING timeout");
		wifi_remove_try_conf();
		wifi_set_state(WIFI_STATE_RESET, 0);
	}
}

static void wifi_handle_ipsetup(void) {
	if (wifi_ip_configured()) {
		if (wpasup_network_configured(WIFI_WPASUP_TRY_CONF)) {
			(void)wifi_commit_try_conf();
		}
		wifi_set_state(WIFI_STATE_CONNECTED, 0);
		return;
	}
	if (wifi_state_timed_out()) {
		wifi_flush_ip();
		(void)wifi_run("killall -9 udhcpc >/dev/null 2>&1");
		if (wpasup_network_configured(WIFI_WPASUP_TRY_CONF)) {
			warn(tag, "IPSETUP timeout; abort try provision");
			wifi_remove_try_conf();
			wifi_set_state(WIFI_STATE_RESET, 0);
			return;
		}
		warn(tag, "IPSETUP timeout; retry CONNECTING");
		wifi_set_state(WIFI_STATE_CONNECTING, WIFI_CONNECT_TIMEOUT_S);
	}
}

static void wifi_handle_connected(void) {
	if (!wifi_wpa_connected() || !wifi_ip_configured()) {
		warn(tag, "link/ip lost");
		wifi_set_state(WIFI_STATE_RESET, 0);
	}
}

static void *wifi_thread(void *args) {
	(void)args;

	info(tag, "wifi thread started");
	/* Discard incomplete failsafe provision left from a previous boot. */
	wifi_remove_try_conf();
	wifi_set_state(WIFI_STATE_INIT, 0);

	while (!wifi_process_exit) {
		if (wifi_request_reset) {
			wifi_request_reset = 0;
			wifi_set_state(WIFI_STATE_RESET, 0);
		}

		switch (wifi_state) {
		case WIFI_STATE_INIT:
			wifi_handle_init();
			break;
		case WIFI_STATE_RESET:
			wifi_handle_reset();
			break;
		case WIFI_STATE_PROVISIONING:
			wifi_handle_provisioning();
			break;
		case WIFI_STATE_CONNECTING:
			wifi_handle_connecting();
			break;
		case WIFI_STATE_IPSETUP:
			wifi_handle_ipsetup();
			break;
		case WIFI_STATE_CONNECTED:
			wifi_handle_connected();
			break;
		default:
			wifi_set_state(WIFI_STATE_INIT, 0);
			break;
		}

		if (wifi_process_exit) {
			break;
		}
		time_delay_ms(WIFI_LOOP_DELAY_MS);
	}

	wifi_kill_daemons();
	wifi_flush_ip();
	info(tag, "wifi thread exiting");
	return NULL;
}

/** Start wifi management.
 * - if thread is already running, return error
 * - create thread to manage wifi
 */
int app_wifi_init(void) {
	int ret;

	if (wifi_running) {
		warn(tag, "wifi thread already running");
		return FAIL;
	}

	wifi_process_exit = 0;
	wifi_request_reset = 0;
	wifi_state = WIFI_STATE_INIT;
	ret = platform_task_create(wifi_thread, "wifi_mgmt", 32 * 1024,
			NULL, 0, &wifi_task_handle);
	if (ret != SUCCESS) {
		dk_error(tag, "platform_task_create failed (ret=%d)", ret);
		wifi_task_handle = NULL;
		return FAIL;
	}

	wifi_running = 1;
	info(tag, "wifi thread created");
	return SUCCESS;
}

/** Stop wifi management.
 * - if thread is not running, return success
 * - stop thread and join
 * - prepare for next initialization
 */
int app_wifi_deinit(void) {
	if (!wifi_running) {
		return SUCCESS;
	}

	wifi_process_exit = 1;

	if (wifi_task_handle != NULL) {
		pthread_t *task = (pthread_t *)wifi_task_handle;
		pthread_join(*task, NULL);
		wifi_task_handle = NULL;
	}

	wifi_running = 0;
	wifi_process_exit = 0;
	wifi_request_reset = 0;
	wifi_state = WIFI_STATE_INIT;
	info(tag, "wifi thread stopped");
	return SUCCESS;
}

/** Provision wifi network (commit immediately).
 * - write credentials to wpa_supplicant.conf
 * - discard any pending try.conf
 * - if thread is running, trigger RESET
 */
int app_wifi_provision(const char *ssid, const char *password, unsigned flags) {
	if (!ssid || ssid[0] == '\0') {
		info(tag, "Clear wifi credential");
		unlink(WIFI_WPASUP_CONF);
		wifi_remove_try_conf();
		goto finally;
	}
	wifi_remove_try_conf();
	if (wifi_write_provision_conf(ssid, password, flags,
			WIFI_WPASUP_CONF) != SUCCESS) {
		return FAIL;
	}
finally:
	if (wifi_running) {
		wifi_request_reset = 1;
	}
	return SUCCESS;
}

/** Failsafe-provision wifi network.
 * - write credentials to wpa_supplicant_try.conf
 * - if thread is running, trigger RESET (SM commits or discards try.conf)
 */
int app_wifi_provision_fallsafe(const char *ssid, const char *password, unsigned flags) {
	if (!ssid || ssid[0] == '\0') {
		info(tag, "Clear failsafe wifi credential");
		wifi_remove_try_conf();
		goto finally;
	}
	if (wifi_write_provision_conf(ssid, password, flags,
			WIFI_WPASUP_TRY_CONF) != SUCCESS) {
		return FAIL;
	}
finally:
	if (wifi_running) {
		wifi_request_reset = 1;
	}
	return SUCCESS;
}

int app_wifi_get_state(void) {
	return wifi_state;
}

int app_wifi_reload(void) {
	if (wifi_running) {
		wifi_request_reset = 1;
	}
	return 0;
}
