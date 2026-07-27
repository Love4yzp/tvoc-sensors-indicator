#include "indicator_util.h"
#include <esp_system.h>
#include <esp_log.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>

static const char* TAG = "indicator-utils";

int wifi_rssi_level_get(int rssi) {
	//    0    rssi<=-100
	//    1    (-100, -88]
	//    2    (-88, -77]
	//    3    (-66, -55]
	//    4    rssi>=-55
	if(rssi > -66)
	{
		return 3;
	}
	else if(rssi > -88)
	{
		return 2;
	}
	else
	{
		return 1;
	}
}

bool is_valid_ipv4(const char* ip_address) {
	regex_t regex;
	int reti;
	bool is_valid = false;

	// IPv4 address pattern
	// Matches numbers 0-255 for each octet
	const char* pattern =
		"^([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.([0-9]|"
		"[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])$";

	// Compile regular expression
	reti = regcomp(&regex, pattern, REG_EXTENDED);
	if(reti)
	{
		ESP_LOGE(TAG, "Could not compile regex");
		return false;
	}

	// Execute regular expression
	reti = regexec(&regex, ip_address, 0, NULL, 0);
	if(!reti)
	{
		is_valid = true;
	}

	// Free compiled regular expression
	regfree(&regex);

	return is_valid;
}

bool extract_ip_from_url(const char* url, char* ip, size_t ip_size) {
	regex_t regex;
	regmatch_t matches[2];
	const char* pattern = "([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)";

	if(regcomp(&regex, pattern, REG_EXTENDED) != 0)
	{
		ESP_LOGE(TAG, "Failed to compile regex");
		return false;
	}

	if(regexec(&regex, url, 2, matches, 0) == 0)
	{
		size_t len = matches[1].rm_eo - matches[1].rm_so;
		if(len < ip_size)
		{
			strncpy(ip, url + matches[1].rm_so, len);
			ip[len] = '\0';
			regfree(&regex);
			return true;
		}
	}

	regfree(&regex);
	return false;
}

bool extract_port_from_url(const char* url, char* port, size_t port_size) {
	if(!url || !port || port_size == 0)
	{
		return false;
	}

	/* Skip the scheme ("mqtt://") if present, then look for ":<digits>" at
	 * the end. No port in the URL means the MQTT default (1883) applies —
	 * report false and let the caller substitute it. */
	const char* host = strstr(url, "://");
	host = host ? host + 3 : url;

	const char* colon = strrchr(host, ':');
	if(!colon || colon[1] == '\0')
	{
		return false;
	}

	const char* digits = colon + 1;
	for(const char* c = digits; *c; c++)
	{
		if(*c < '0' || *c > '9')
		{
			return false;
		}
	}

	size_t len = strlen(digits);
	if(len >= port_size)
	{
		return false;
	}
	memcpy(port, digits, len + 1);
	return true;
}

#define MQTT_DEFAULT_PORT "1883"

void assemble_broker_url(const char* ip_address, const char* port, char* broker_url, size_t broker_url_size) {
	if(!port || port[0] == '\0')
	{
		port = MQTT_DEFAULT_PORT;
	}
	snprintf(broker_url, broker_url_size, "mqtt://%s:%s", ip_address, port);
}
