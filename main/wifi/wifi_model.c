#include "wifi_model.h"
#include "esp_log.h"

#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "ping/ping_sock.h"

struct indicator_wifi
{
	struct view_data_wifi_st st;
	bool is_cfg;
	int retry_num;   /* fast-retry attempts used in the current burst */
	int retry_max;   /* fast-retry budget per burst (-1 = unlimited) */
	int idle_ticks;  /* 5 s ticks spent disconnected after a burst */
};

static struct indicator_wifi _g_wifi_model;
static SemaphoreHandle_t _g_data_mutex;
static SemaphoreHandle_t _g_net_check_sem;

/* One-way latch set by _wifi_shutdown(): the DISCONNECTED triggered by the
 * shutdown esp_wifi_stop() must not be mistaken for a link loss and retried
 * against a stopped driver. Atomic: written on the view_event loop, read on
 * the Wi-Fi event task and _indicator_wifi_task. */
static _Atomic bool _g_shutting_down = false;

/* All blocking esp_wifi_* control ops (scan/connect/restore/stop) run on a single
 * worker task fed by this queue, NEVER on the view_event_handle loop. Running
 * them on that loop stalls it for seconds; its fixed-size queue then fills and
 * any portMAX_DELAY post back to it (from the loop itself or from the LVGL task
 * while it holds the lvgl_port lock) blocks forever — which froze the whole UI
 * after a connect. Off-loading keeps the loop draining so posts never block. */
typedef enum {
	WIFI_CMD_SCAN = 0,
	WIFI_CMD_CONNECT,
	WIFI_CMD_RESTORE,
	WIFI_CMD_SHUTDOWN,
} wifi_cmd_type_t;

typedef struct {
	wifi_cmd_type_t type;
	struct view_data_wifi_config cfg;  /* valid for WIFI_CMD_CONNECT */
} wifi_cmd_t;

static QueueHandle_t _g_wifi_cmd_q;

static bool _g_ping_done = true;

static const char* TAG = "wifi-model";

static int min(int a, int b) {
	return (a < b) ? a : b;
}

static void _wifi_st_set(struct view_data_wifi_st* p_st) {
	xSemaphoreTake(_g_data_mutex, portMAX_DELAY);
	memcpy(&_g_wifi_model.st, p_st, sizeof(struct view_data_wifi_st));
	xSemaphoreGive(_g_data_mutex);
}

static void _wifi_st_get(struct view_data_wifi_st* p_st) {
	xSemaphoreTake(_g_data_mutex, portMAX_DELAY);
	memcpy(p_st, &_g_wifi_model.st, sizeof(struct view_data_wifi_st));
	xSemaphoreGive(_g_data_mutex);
}

static void _wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
	switch(event_id)
	{
		case WIFI_EVENT_STA_START:
		{
			ESP_LOGI(TAG, "wifi event: WIFI_EVENT_STA_START");
			struct view_data_wifi_st st;
			st.is_connected = false;
			st.is_network = false;
			st.is_connecting = true;
			memset(st.ssid, 0, sizeof(st.ssid));
			st.rssi = 0;
			_wifi_st_set(&st);

			esp_wifi_connect();
			break;
		}
		case WIFI_EVENT_STA_CONNECTED:
		{
			ESP_LOGI(TAG, "wifi event: WIFI_EVENT_STA_CONNECTED");
			wifi_event_sta_connected_t* event = (wifi_event_sta_connected_t*)event_data;
			struct view_data_wifi_st st;

			_wifi_st_get(&st);
			memset(st.ssid, 0, sizeof(st.ssid));
			memcpy(st.ssid, event->ssid, event->ssid_len);
			st.rssi = -50; // todo
			st.is_connected = true;
			st.is_connecting = false;
			_wifi_st_set(&st);

			esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st,
							  sizeof(struct view_data_wifi_st), portMAX_DELAY);

			struct view_data_wifi_connet_ret_msg msg;
			msg.ret = 0;
			strcpy(msg.msg, "Connection successful");
			esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT_RET, &msg, sizeof(msg),
							  portMAX_DELAY);
			break;
		}
		case WIFI_EVENT_STA_DISCONNECTED:
		{
			wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*)event_data;
			ESP_LOGI(TAG, "wifi event: WIFI_EVENT_STA_DISCONNECTED, reason=%d", event->reason);

			/* Single retry state machine: every transition updates the model
			 * AND is broadcast, so the UI/MQTT layers never see a stale
			 * "connected". A burst of retry_max immediate retries runs here;
			 * once the budget is spent the link stays down (is_connecting
			 * false) until _indicator_wifi_task re-arms it after a backoff.
			 * The counter fields live inside _g_data_mutex's critical
			 * section together with st. */
			struct view_data_wifi_st st;
			bool do_retry = false;
			bool do_fail = false;
			int retry_num = 0;
			int retry_max = 0;

			xSemaphoreTake(_g_data_mutex, portMAX_DELAY);
			_g_wifi_model.st.is_connected = false;
			_g_wifi_model.st.is_network = false;
			if(_g_shutting_down || !_g_wifi_model.is_cfg)
			{
				_g_wifi_model.st.is_connecting = false;
			}
			else if((_g_wifi_model.retry_max == -1) || (_g_wifi_model.retry_num < _g_wifi_model.retry_max))
			{
				_g_wifi_model.retry_num++;
				_g_wifi_model.st.is_connecting = true; /* explicit "retrying" state */
				do_retry = true;
			}
			else
			{
				_g_wifi_model.st.is_connecting = false;
				_g_wifi_model.idle_ticks = 0;
				do_fail = true;
			}
			retry_num = _g_wifi_model.retry_num;
			retry_max = _g_wifi_model.retry_max;
			st = _g_wifi_model.st;
			xSemaphoreGive(_g_data_mutex);

			if(!_g_shutting_down)
			{
				esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st,
								  sizeof(struct view_data_wifi_st), portMAX_DELAY);
			}

			if(do_retry)
			{
				ESP_LOGI(TAG, "retry to connect to the AP (%d/%d)", retry_num, retry_max);
				esp_wifi_connect();
			}
			else if(do_fail)
			{
				struct view_data_wifi_connet_ret_msg msg;
				msg.ret = 1;
				strcpy(msg.msg, "Connection failure");
				esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT_RET, &msg, sizeof(msg),
								  portMAX_DELAY);
			}
			break;
		}
		default:
			break;
	}
}

/* Start SNTP once we first have an IP so the system clock syncs to real wall
 * time. _timestamp_s() (sen5x_mqtt.c) then reports true Unix epoch seconds
 * instead of seconds-since-boot. Payload timestamps are UTC epoch, so no
 * timezone setup is needed. Requires the network to reach the NTP server. */
static void _sntp_start_once(void) {
	static bool started = false;
	if(started) return;
	esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
	esp_sntp_setservername(0, "pool.ntp.org");
	esp_sntp_init();
	started = true;
	ESP_LOGI(TAG, "SNTP started (pool.ntp.org) — system time will sync shortly");
}

static void _ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
	if(event_id == IP_EVENT_STA_GOT_IP)
	{
		ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));

		xSemaphoreTake(_g_data_mutex, portMAX_DELAY);
		_g_wifi_model.retry_num = 0;
		_g_wifi_model.idle_ticks = 0;
		xSemaphoreGive(_g_data_mutex);

		_sntp_start_once();
		xSemaphoreGive(_g_net_check_sem);
	}
}

static int _wifi_scan(wifi_ap_record_t* p_ap_info, uint16_t number) {
	uint16_t ap_count = 0;

	/* esp_wifi_scan_start fails when the station is mid-(re)connect — the
	 * common case when the user opens the Wi-Fi screen right after boot while
	 * the saved-AP connect is still retrying. Never ESP_ERROR_CHECK these:
	 * abort() reboots the device and shows up as the "black screen" freeze.
	 * Report 0 APs instead so the UI renders an empty list and stays alive. */
	esp_err_t err = esp_wifi_scan_start(NULL, true);
	if(err != ESP_OK)
	{
		ESP_LOGW(TAG, "wifi scan start failed: %s", esp_err_to_name(err));
		return 0;
	}

	err = esp_wifi_scan_get_ap_num(&ap_count);
	if(err != ESP_OK)
	{
		ESP_LOGW(TAG, "wifi scan get ap num failed: %s", esp_err_to_name(err));
		return 0;
	}
	err = esp_wifi_scan_get_ap_records(&number, p_ap_info);
	if(err != ESP_OK)
	{
		ESP_LOGW(TAG, "wifi scan get ap records failed: %s", esp_err_to_name(err));
		return 0;
	}
	ESP_LOGI(TAG, "Total APs scanned = %u, actual AP number ap_info holds = %u", ap_count, number);

	for(int i = 0; (i < number) && (i < ap_count); i++)
	{
		ESP_LOGI(TAG, "SSID: %s, RSSI:%d, Channel: %d", p_ap_info[i].ssid, p_ap_info[i].rssi, p_ap_info[i].primary);
	}
	return ap_count;
}

static int _wifi_connect(const char* p_ssid, const char* p_password, int retry_num) {
	wifi_config_t wifi_config = {0};
	strlcpy((char*)wifi_config.sta.ssid, p_ssid, sizeof(wifi_config.sta.ssid));
	ESP_LOGI(TAG, "ssid: %s", p_ssid);
	if(p_password)
	{
		strlcpy((char*)wifi_config.sta.password, p_password, sizeof(wifi_config.sta.password));
		wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	}
	else
	{
		wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
	}
	wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

	esp_wifi_stop();
	/* Never ESP_ERROR_CHECK here (same policy as _wifi_scan): an abort()
	 * reboots the device and shows up as the "black screen" freeze. */
	esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "wifi set mode failed: %s", esp_err_to_name(err));
		return -1;
	}
	err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "wifi set config failed: %s", esp_err_to_name(err));
		return -1;
	}

	xSemaphoreTake(_g_data_mutex, portMAX_DELAY);
	_g_wifi_model.is_cfg = true;
	_g_wifi_model.retry_max = retry_num;
	_g_wifi_model.retry_num = 0;
	_g_wifi_model.idle_ticks = 0;
	memset(&_g_wifi_model.st, 0, sizeof(_g_wifi_model.st));
	xSemaphoreGive(_g_data_mutex);

	err = esp_wifi_start();
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "wifi start failed: %s", esp_err_to_name(err));
		return -1;
	}

	ESP_LOGI(TAG, "connect...");
	return 0;
}

static void _wifi_cfg_restore(void) {
	struct view_data_wifi_st st;

	xSemaphoreTake(_g_data_mutex, portMAX_DELAY);
	_g_wifi_model.is_cfg = false;
	memset(&_g_wifi_model.st, 0, sizeof(_g_wifi_model.st));
	st = _g_wifi_model.st;
	xSemaphoreGive(_g_data_mutex);

	esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st),
					  portMAX_DELAY);

	esp_wifi_restore();
}

static void _wifi_shutdown(void) {
	struct view_data_wifi_st st;

	/* Latch BEFORE enqueueing the stop: the DISCONNECTED that esp_wifi_stop()
	 * triggers must be ignored by the retry state machine (the driver is
	 * going away, a connect against it would just error). */
	_g_shutting_down = true;

	xSemaphoreTake(_g_data_mutex, portMAX_DELAY);
	_g_wifi_model.is_cfg = false;
	memset(&_g_wifi_model.st, 0, sizeof(_g_wifi_model.st));
	st = _g_wifi_model.st;
	xSemaphoreGive(_g_data_mutex);

	/* Runs on the view_event loop (VIEW_EVENT_SHUTDOWN handler), so this is a
	 * post to our own queue: portMAX_DELAY here self-deadlocks the loop when
	 * the queue is full, freezing the whole UI. The screen is going off
	 * anyway, so a dropped status update is harmless. */
	esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st),
					  0);

	/* esp_wifi_stop() blocks for the duration of the driver teardown, so it
	 * must not run on the view_event loop — the cmd worker owns all blocking
	 * esp_wifi_* calls (see the _g_wifi_cmd_q comment). */
	wifi_cmd_t cmd = { .type = WIFI_CMD_SHUTDOWN };
	if(xQueueSend(_g_wifi_cmd_q, &cmd, 0) != pdTRUE)
		ESP_LOGW(TAG, "wifi cmd queue full, dropped SHUTDOWN");
}

static void _ping_end(esp_ping_handle_t hdl, void* args) {
	ip_addr_t target_addr;
	uint32_t transmitted = 0;
	uint32_t received = 0;
	uint32_t total_time_ms = 0;
	uint32_t loss = 0;
	esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
	esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
	esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
	esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));

	if(transmitted > 0)
	{
		loss = (uint32_t)((1 - ((float)received) / transmitted) * 100);
	}
	else
	{
		loss = 100;
	}

	if(IP_IS_V4(&target_addr))
	{
		printf("\n--- %s ping statistics ---\n", inet_ntoa(*ip_2_ip4(&target_addr)));
	}
	else
	{
		printf("\n--- %s ping statistics ---\n", inet6_ntoa(*ip_2_ip6(&target_addr)));
	}
	printf("%d packets transmitted, %d received, %d%% packet loss, time %dms\n", transmitted, received, loss,
		   total_time_ms);

	esp_ping_delete_session(hdl);

	struct view_data_wifi_st st;
	if(received > 0)
	{
		_wifi_st_get(&st);
		st.is_network = true;
		_wifi_st_set(&st);
	}
	else
	{
		_wifi_st_get(&st);
		st.is_network = false;
		_wifi_st_set(&st);
	}
	esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st),
					  portMAX_DELAY);
	_g_ping_done = true;
}

static void _ping_start(void) {
	esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();

	ip_addr_t target_addr;
	ipaddr_aton("1.1.1.1", &target_addr);

	config.target_addr = target_addr;

	esp_ping_callbacks_t cbs = {
		.cb_args = NULL, .on_ping_success = NULL, .on_ping_timeout = NULL, .on_ping_end = _ping_end};
	esp_ping_handle_t ping;
	esp_err_t err = esp_ping_new_session(&config, &cbs, &ping);
	if(err != ESP_OK)
	{
		/* Must NOT clear _g_ping_done on failure: the old code set it false
		 * unconditionally, so a failed session alloc permanently stalled the
		 * is_network re-check loop. */
		ESP_LOGW(TAG, "ping session create failed: %s", esp_err_to_name(err));
		return;
	}
	_g_ping_done = false;
	esp_ping_start(ping);
}

static void _indicator_wifi_task(void* p_arg) {
	int cnt = 0;
	struct view_data_wifi_st st;

	while(1)
	{
		xSemaphoreTake(_g_net_check_sem, pdMS_TO_TICKS(5000));
		_wifi_st_get(&st);

		if(st.is_connected)
		{
			if(_g_ping_done)
			{
				if(st.is_network)
				{
					cnt++;
					if(cnt > 60)
					{
						cnt = 0;
						ESP_LOGI(TAG, "Network normal last time, retry check network...");
						_ping_start();
					}
				}
				else
				{
					ESP_LOGI(TAG, "Last network exception, check network...");
					_ping_start();
				}
			}
		}
		else if(!st.is_connecting)
		{
			/* Link down with the fast-retry burst spent: wait out the backoff
			 * (~30 s at this 5 s tick), then re-arm the burst and kick a fresh
			 * connect. Plain esp_wifi_connect() suffices — after DISCONNECTED
			 * the driver is idle, so the old stop()/start() cycle only reset
			 * counters across tasks without adding recovery value. This is
			 * the same state machine as the DISCONNECTED handler, just the
			 * slow path of it. */
			bool kick = false;
			struct view_data_wifi_st kick_st;

			xSemaphoreTake(_g_data_mutex, portMAX_DELAY);
			if(_g_wifi_model.is_cfg && !_g_shutting_down)
			{
				_g_wifi_model.idle_ticks++;
				if(_g_wifi_model.idle_ticks > 5)
				{
					_g_wifi_model.idle_ticks = 0;
					_g_wifi_model.retry_num = 0;
					_g_wifi_model.st.is_connecting = true;
					kick_st = _g_wifi_model.st;
					kick = true;
				}
			}
			xSemaphoreGive(_g_data_mutex);

			if(kick)
			{
				ESP_LOGI(TAG, "wifi reconnect backoff elapsed, retrying...");
				esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &kick_st,
								  sizeof(struct view_data_wifi_st), portMAX_DELAY);
				esp_wifi_connect();
			}
		}
	}
}

/* Perform a blocking Wi-Fi scan, dedupe by SSID and publish the result as
 * VIEW_EVENT_WIFI_LIST. Must run on its own task — never on the view_event_handle
 * loop — see the VIEW_EVENT_WIFI_LIST_REQ handler for why. */
static void _do_scan_and_publish(void) {
	uint16_t number = WIFI_SCAN_LIST_SIZE;
	uint16_t ap_count = 0;
	wifi_ap_record_t ap_info[WIFI_SCAN_LIST_SIZE];
	ap_count = _wifi_scan(ap_info, number);

	struct view_data_wifi_list list;
	struct view_data_wifi_st st;

	memset(&list, 0, sizeof(struct view_data_wifi_list));

	_wifi_st_get(&st);

	list.is_connect = st.is_connected;
	if(st.is_connected)
	{
		strlcpy((char*)list.connect.ssid, (char*)st.ssid, sizeof(list.connect.ssid));
		list.connect.auth_mode = false;
		list.connect.rssi = st.rssi;
	}

	ap_count = min(number, ap_count);

	bool is_exist = false;
	int list_cnt = 0;
	for(int i = 0; i < ap_count; i++)
	{
		is_exist = false;
		for(int j = 0; j < list_cnt; j++)
		{
			if(strcmp(list.aps[j].ssid, ap_info[i].ssid) == 0)
			{
				ESP_LOGI(TAG, "list exit ap:%s", ap_info[i].ssid);
				is_exist = true;
				break;
			}
		}
		if(!is_exist)
		{
			/* strlcpy, not strcpy: wifi_ap_record_t.ssid is 33 bytes but
			 * view_data_wifi_item.ssid is 32 — a 32-char AP name would
			 * overflow by one. */
			strlcpy(list.aps[list_cnt].ssid, (const char*)ap_info[i].ssid, sizeof(list.aps[list_cnt].ssid));
			list.aps[list_cnt].rssi = ap_info[i].rssi;
			list.aps[list_cnt].auth_mode = (ap_info[i].authmode != WIFI_AUTH_OPEN);
			list_cnt++;
		}
	}
	list.cnt = list_cnt;
	esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST, &list,
					  sizeof(struct view_data_wifi_list), portMAX_DELAY);
}

static void _wifi_cmd_task(void* p_arg) {
	wifi_cmd_t cmd;
	while(1)
	{
		if(xQueueReceive(_g_wifi_cmd_q, &cmd, portMAX_DELAY) != pdTRUE) continue;
		switch(cmd.type)
		{
			case WIFI_CMD_SCAN:
				_do_scan_and_publish();
				break;
			case WIFI_CMD_CONNECT:
				_wifi_connect(cmd.cfg.ssid,
							  cmd.cfg.have_password ? (const char*)cmd.cfg.password : NULL, 3);
				break;
			case WIFI_CMD_RESTORE:
				_wifi_cfg_restore();
				break;
			case WIFI_CMD_SHUTDOWN:
				esp_wifi_stop();
				break;
		}
	}
}

static void _view_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data) {
	switch(id)
	{
		/* These three cases run on the view_event_handle loop. They must NOT call
		 * the blocking esp_wifi_* helpers directly — that would stall the loop and
		 * can deadlock the UI (see the _g_wifi_cmd_q comment). Enqueue to the
		 * worker non-blocking instead so the loop returns immediately. */
		case VIEW_EVENT_WIFI_LIST_REQ:
		{
			ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_LIST_REQ");
			wifi_cmd_t cmd = { .type = WIFI_CMD_SCAN };
			if(xQueueSend(_g_wifi_cmd_q, &cmd, 0) != pdTRUE)
				ESP_LOGW(TAG, "wifi cmd queue full, dropped SCAN");
			break;
		}
		case VIEW_EVENT_WIFI_CONNECT:
		{
			ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_CONNECT");
			wifi_cmd_t cmd = { .type = WIFI_CMD_CONNECT };
			memcpy(&cmd.cfg, event_data, sizeof(cmd.cfg));
			if(xQueueSend(_g_wifi_cmd_q, &cmd, 0) != pdTRUE)
				ESP_LOGW(TAG, "wifi cmd queue full, dropped CONNECT");
			break;
		}
		case VIEW_EVENT_WIFI_CFG_DELETE:
		{
			ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_CFG_DELETE");
			wifi_cmd_t cmd = { .type = WIFI_CMD_RESTORE };
			if(xQueueSend(_g_wifi_cmd_q, &cmd, 0) != pdTRUE)
				ESP_LOGW(TAG, "wifi cmd queue full, dropped RESTORE");
			break;
		}
		case VIEW_EVENT_SHUTDOWN:
		{
			ESP_LOGI(TAG, "event: VIEW_EVENT_SHUTDOWN");
			_wifi_shutdown();
			break;
		}
		default:
			break;
	}
}

int indicator_wifi_model_init(void) {
	_g_data_mutex = xSemaphoreCreateMutex();
	_g_net_check_sem = xSemaphoreCreateBinary();
	_g_wifi_cmd_q = xQueueCreate(4, sizeof(wifi_cmd_t));

	memset(&_g_wifi_model, 0, sizeof(_g_wifi_model));
	_g_wifi_model.retry_max = 3; /* default burst budget; _wifi_connect() overrides */

	xTaskCreate(&_indicator_wifi_task, "_indicator_wifi_task", 1024 * 5, NULL, 10, NULL);
	xTaskCreate(&_wifi_cmd_task, "_wifi_cmd_task", 1024 * 5, NULL, 5, NULL);

	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;

	ESP_ERROR_CHECK(
		esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &_wifi_event_handler, 0, &instance_any_id));

	ESP_ERROR_CHECK(
		esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &_ip_event_handler, 0, &instance_got_ip));

	ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST_REQ, _view_event_handler, NULL, NULL));

	ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT, _view_event_handler, NULL, NULL));

	ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CFG_DELETE, _view_event_handler, NULL, NULL));

	ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SHUTDOWN,
															 _view_event_handler, NULL, NULL));

	wifi_config_t wifi_cfg;
	esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);

	if(strlen((const char*)wifi_cfg.sta.ssid))
	{
		xSemaphoreTake(_g_data_mutex, portMAX_DELAY);
		_g_wifi_model.is_cfg = true;
		xSemaphoreGive(_g_data_mutex);
		ESP_LOGI(TAG, "last config ssid: %s", wifi_cfg.sta.ssid);
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
		ESP_ERROR_CHECK(esp_wifi_start());
	}
	else
	{
		ESP_LOGI(TAG, "Not config wifi, Entry wifi config screen");
		uint8_t screen = SCREEN_WIFI_CONFIG;
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
		ESP_ERROR_CHECK(esp_wifi_start());
		esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_START, &screen, sizeof(screen),
						  portMAX_DELAY);
	}

	return 0;
}
