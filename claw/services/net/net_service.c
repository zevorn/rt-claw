/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Network service — Ethernet init and HTTP connectivity test.
 */

#include "osal/claw_os.h"
#include "claw/services/net/net_service.h"

#include <string.h>

#define TAG "net"

#if defined(CLAW_PLATFORM_ESP_IDF) && defined(CONFIG_ETH_ENABLED)
/*
 * QEMU Ethernet path — OpenCores MAC + hardcoded DNS.
 * Real hardware uses WiFi (initialized in platform main.c),
 * so this block is compiled only when Ethernet is enabled.
 */

#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_eth_mac_openeth.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "lwip/dns.h"

static struct claw_sem *s_got_ip_sem;
static esp_netif_t *s_eth_netif;

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        CLAW_LOGI(TAG, "Ethernet link up");
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        CLAW_LOGW(TAG, "Ethernet link down");
        break;
    default:
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    if (event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        CLAW_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        CLAW_LOGI(TAG, "netmask: " IPSTR,
                  IP2STR(&event->ip_info.netmask));
        CLAW_LOGI(TAG, "gateway: " IPSTR, IP2STR(&event->ip_info.gw));
        claw_sem_give(s_got_ip_sem);
    }
}

static int eth_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);
    esp_netif_t *netif = s_eth_netif;

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_openeth(&mac_cfg);
    if (!mac) {
        CLAW_LOGE(TAG, "failed to create OpenCores MAC");
        return CLAW_ERROR;
    }

    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.autonego_timeout_ms = 100;
    esp_eth_phy_t *phy = esp_eth_phy_new_dp83848(&phy_cfg);

    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_cfg, &eth_handle));
    ESP_ERROR_CHECK(esp_netif_attach(netif,
                    esp_eth_new_netif_glue(eth_handle)));

    ESP_ERROR_CHECK(esp_event_handler_register(
        ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_ETH_GOT_IP, &ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
    return CLAW_OK;
}

int net_service_init(void)
{
    s_got_ip_sem = claw_sem_create("net_ip", 0);
    if (!s_got_ip_sem) {
        CLAW_LOGE(TAG, "failed to create semaphore");
        return CLAW_ERROR;
    }

    if (eth_init() != CLAW_OK) {
        CLAW_LOGE(TAG, "Ethernet init failed");
        return CLAW_ERROR;
    }

    /*
     * DHCP wait: 2000ms gives SLIRP enough time even under
     * instruction-counted mode (-icount).  On mcast-socket
     * networks (no DHCP server), this falls through to the
     * static IP fallback.
     */
    CLAW_LOGI(TAG, "waiting for IP address (DHCP) ...");
    int ret = claw_sem_take(s_got_ip_sem, 2000);
    if (ret != CLAW_OK) {
        CLAW_LOGW(TAG, "DHCP timeout, falling back to static IP");

        esp_netif_ip_info_t ip_info;
#ifdef CONFIG_ETH_USE_OPENETH
        /*
         * QEMU SLIRP defaults: the user-mode network stack
         * provides NAT at 10.0.2.2 and DNS at 10.0.2.3.
         * Use a static IP in the SLIRP subnet so that
         * gateway routing and DNS resolution work even
         * when DHCP times out under -icount.
         */
        IP4_ADDR(&ip_info.ip,      10, 0, 2, 15);
        IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
        IP4_ADDR(&ip_info.gw,      10, 0, 2, 2);
#else
        /*
         * Non-QEMU Ethernet: link-local address derived from
         * MAC for swarm UDP on isolated networks.
         */
        uint8_t mac[6];
        esp_efuse_mac_get_default(mac);
        IP4_ADDR(&ip_info.ip,      169, 254, mac[4], mac[5] ? mac[5] : 1);
        IP4_ADDR(&ip_info.netmask, 255, 255, 0, 0);
        IP4_ADDR(&ip_info.gw,      0, 0, 0, 0);
#endif

        esp_netif_dhcpc_stop(s_eth_netif);
        esp_netif_set_ip_info(s_eth_netif, &ip_info);
        CLAW_LOGI(TAG, "static ip: " IPSTR, IP2STR(&ip_info.ip));
    }

    /* QEMU user-mode NAT DNS is at 10.0.2.3 */
    {
        ip_addr_t dns_addr;
        ipaddr_aton("10.0.2.3", &dns_addr);
        dns_setserver(0, &dns_addr);
        CLAW_LOGI(TAG, "DNS server set to 10.0.2.3");
    }

    CLAW_LOGI(TAG, "network service ready");
    return CLAW_OK;
}

void net_print_ipinfo(void)
{
    esp_netif_ip_info_t info;

    if (s_eth_netif &&
        esp_netif_get_ip_info(s_eth_netif, &info) == ESP_OK &&
        info.ip.addr != 0) {
        printf("  ip:      " IPSTR "\n", IP2STR(&info.ip));
        printf("  netmask: " IPSTR "\n", IP2STR(&info.netmask));
        printf("  gateway: " IPSTR "\n", IP2STR(&info.gw));
    } else {
        printf("  (no IP address)\n");
    }
}

#elif defined(CLAW_PLATFORM_ESP_IDF)
/*
 * ESP-IDF without Ethernet (WiFi-only, real hardware).
 * Network is initialized by platform main.c (wifi_manager).
 */

#include "esp_netif.h"

int net_service_init(void)
{
    CLAW_LOGI(TAG, "network service ready (WiFi managed by platform)");
    return CLAW_OK;
}

void net_print_ipinfo(void)
{
    esp_netif_t *netif = NULL;
    int found = 0;

    while ((netif = esp_netif_next_unsafe(netif)) != NULL) {
        esp_netif_ip_info_t info;
        if (esp_netif_get_ip_info(netif, &info) == ESP_OK &&
            info.ip.addr != 0) {
            const char *desc = esp_netif_get_desc(netif);
            printf("  %s:\n", desc ? desc : "netif");
            printf("    ip:      " IPSTR "\n", IP2STR(&info.ip));
            printf("    netmask: " IPSTR "\n", IP2STR(&info.netmask));
            printf("    gateway: " IPSTR "\n", IP2STR(&info.gw));
            found = 1;
        }
    }
    if (!found) {
        printf("  (no IP address)\n");
    }
}

#elif defined(CLAW_PLATFORM_RTTHREAD)

#include "claw/platform_net.h"

#include <stdio.h>
#include <rtthread.h>
#include <netif/ethernetif.h>
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"

const char *claw_platform_net_device_name(void) __attribute__((weak));
void claw_platform_net_prepare(void) __attribute__((weak));

const char *claw_platform_net_device_name(void)
{
    return "e0";
}

void claw_platform_net_prepare(void)
{
}

static void net_link_thread(void *arg)
{
    (void)arg;
    claw_platform_net_prepare();
}

int net_service_init(void)
{
    const char *dev_name = claw_platform_net_device_name();

    if (!dev_name || dev_name[0] == '\0') {
        dev_name = "e0";
    }

    CLAW_LOGI(TAG, "waiting for network interface %s ...", dev_name);

    /*
     * RT-Thread + lwIP auto-initializes the NIC via INIT_APP_EXPORT.
     * The smc911x driver registers device "e0" but defers link-up
     * so that the boot sequence is not blocked by DHCP processing.
     */
    rt_device_t dev = RT_NULL;
    int timeout = 15;

    while (timeout > 0) {
        dev = rt_device_find(dev_name);
        if (dev) {
            CLAW_LOGI(TAG, "network interface %s found", dev_name);
            break;
        }
        claw_thread_delay_ms(1000);
        timeout--;
    }

    if (!dev) {
        CLAW_LOGW(TAG, "no network interface %s found (continuing without net)",
                  dev_name);
        return CLAW_OK;
    }

    /*
     * Bring up link via eth_device_linkchange in a helper thread.
     * This triggers the standard erx thread to call
     * netifapi_netif_set_link_up, which starts DHCP.
     */
    {
        rt_thread_t t = rt_thread_create("netlink",
                                         net_link_thread, RT_NULL,
                                         2048, 20, 10);
        if (t) {
            rt_thread_startup(t);
        }
    }

    /* Wait for DHCP to assign IP */
    CLAW_LOGI(TAG, "waiting for DHCP ...");
    {
        struct eth_device *ethdev = (struct eth_device *)dev;
        int dhcp_wait = 0;
        while (dhcp_wait < 15) {
            claw_thread_delay_ms(1000);
            dhcp_wait++;
            if (ethdev->netif && ethdev->netif->ip_addr.addr != 0) {
                break;
            }
        }
        if (ethdev->netif && ethdev->netif->ip_addr.addr != 0) {
            CLAW_LOGI(TAG, "got ip: %s",
                      ip4addr_ntoa(&ethdev->netif->ip_addr));
            CLAW_LOGI(TAG, "gateway: %s",
                      ip4addr_ntoa(&ethdev->netif->gw));
        } else {
            CLAW_LOGW(TAG, "DHCP timeout (15s), no IP acquired");
        }
    }

    CLAW_LOGI(TAG, "network service ready");
    return CLAW_OK;
}

void net_print_ipinfo(void)
{
    struct netif *nif = netif_default;

    if (nif && nif->ip_addr.addr != 0) {
        printf("  ip:      %s\n", ip4addr_ntoa(&nif->ip_addr));
        printf("  netmask: %s\n", ip4addr_ntoa(&nif->netmask));
        printf("  gateway: %s\n", ip4addr_ntoa(&nif->gw));
    } else {
        printf("  (no IP address)\n");
    }
}

#elif defined(CLAW_PLATFORM_LINUX)

#include <stdio.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>

int net_service_init(void)
{
    CLAW_LOGI(TAG, "network service ready (Linux native)");
    return CLAW_OK;
}

void net_print_ipinfo(void)
{
    struct ifaddrs *ifaddr, *ifa;
    int found = 0;

    if (getifaddrs(&ifaddr) == -1) {
        printf("  (getifaddrs failed)\n");
        return;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr ||
            ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if (ifa->ifa_flags & IFF_LOOPBACK) {
            continue;
        }

        char addr[INET_ADDRSTRLEN];
        struct sockaddr_in *sa =
            (struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &sa->sin_addr,
                  addr, sizeof(addr));

        char mask[INET_ADDRSTRLEN];
        struct sockaddr_in *nm =
            (struct sockaddr_in *)ifa->ifa_netmask;
        inet_ntop(AF_INET, &nm->sin_addr,
                  mask, sizeof(mask));

        printf("  %s:\n", ifa->ifa_name);
        printf("    ip:      %s\n", addr);
        printf("    netmask: %s\n", mask);
        found = 1;
    }
    freeifaddrs(ifaddr);

    if (!found) {
        printf("  (no non-loopback IPv4 address)\n");
    }
}

#else /* unknown platform */

#include <stdio.h>

int net_service_init(void)
{
    CLAW_LOGI(TAG, "service initialized (network backend pending)");
    return CLAW_OK;
}

void net_print_ipinfo(void)
{
    printf("  (not available on this platform)\n");
}

#endif

/* OOP service registration */
#include "claw/core/claw_service.h"
CLAW_DEFINE_SIMPLE_SERVICE(net, "net",
    net_service_init, NULL, NULL, NULL);
