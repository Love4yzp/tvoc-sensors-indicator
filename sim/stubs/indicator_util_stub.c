#include "indicator_util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int wifi_rssi_level_get(int rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -67) return 3;
    if (rssi >= -79) return 2;
    return 1;
}

bool isValidIP(const char *input) {
    return input && strlen(input) > 0;
}

bool isValidDomain(const char *input) {
    return input && strlen(input) > 0;
}

bool is_valid_ipv4(const char *ip) {
    if (!ip) return false;
    int a, b, c, d, n;
    n = sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d);
    return n == 4 && a >= 0 && a <= 255;
}

bool extract_ip_from_url(const char *url, char *ip, size_t ip_size) {
    if (!url || !ip || ip_size == 0) return false;
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    size_t len = strlen(p);
    if (len >= ip_size) len = ip_size - 1;
    memcpy(ip, p, len);
    ip[len] = '\0';
    return true;
}

void assemble_broker_url(const char *ip, char *url, size_t url_size) {
    if (!ip || !url || url_size == 0) return;
    snprintf(url, url_size, "mqtt://%s", ip);
}
