#pragma once
#include "esp_err.h"
#include <stdint.h>

typedef void esp_netif_t;

typedef struct { uint32_t addr; } esp_ip4_addr_t;

typedef struct {
    esp_ip4_addr_t ip;
    esp_ip4_addr_t netmask;
    esp_ip4_addr_t gw;
} esp_netif_ip_info_t;

static inline esp_netif_t *esp_netif_get_handle_from_ifkey(const char *if_key) {
    (void)if_key; return NULL;
}

static inline esp_err_t esp_netif_get_ip_info(esp_netif_t *netif,
                                               esp_netif_ip_info_t *ip_info) {
    (void)netif; (void)ip_info; return ESP_FAIL;
}

#define IPSTR "%d.%d.%d.%d"
#define IP2STR(ipaddr)                              \
    ((int)(((ipaddr)->addr)        & 0xff)),        \
    ((int)((((ipaddr)->addr) >> 8) & 0xff)),        \
    ((int)((((ipaddr)->addr) >>16) & 0xff)),        \
    ((int)((((ipaddr)->addr) >>24) & 0xff))
