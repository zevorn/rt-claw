/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Swarm service — heartbeat broadcast and node discovery via UDP.
 */

#include "osal/claw_os.h"
#include "claw/claw_config.h"
#include "osal/claw_net.h"
#include "claw/services/swarm/swarm.h"
#ifdef CONFIG_RTCLAW_HEARTBEAT_ENABLE
#include "claw/core/heartbeat.h"
#endif

#include <string.h>
#include <stdio.h>

#define TAG "swarm"

static struct swarm_node nodes[CLAW_SWARM_MAX_NODES];
static int node_count;
static claw_mutex_t swarm_lock;
static uint32_t s_self_id;

/* Node ID generation — platform-specific hardware identity */
#ifdef CLAW_PLATFORM_ESP_IDF
#include "esp_mac.h"

static uint32_t generate_node_id(void)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    return ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
           ((uint32_t)mac[4] << 8)  | (uint32_t)mac[5];
}

#elif defined(CLAW_PLATFORM_RTTHREAD)

static uint32_t generate_node_id(void)
{
    return 0x52540000 | (claw_tick_ms() & 0xFFFF);
}

#else

static uint32_t generate_node_id(void)
{
    return 0x0000DEAD;
}

#endif

/*
 * Shared socket-based swarm implementation.
 * Requires POSIX socket API (lwIP on ESP-IDF or SAL on RT-Thread).
 */
#if defined(CLAW_PLATFORM_ESP_IDF) || defined(CLAW_PLATFORM_RTTHREAD)

static int s_sock = -1;
static claw_timer_t s_hb_timer;

static void notify_node_event(uint32_t node_id, int joined)
{
    CLAW_LOGI(TAG, "node 0x%08x %s",
              (unsigned)node_id, joined ? "joined" : "left");
#ifdef CONFIG_RTCLAW_HEARTBEAT_ENABLE
    char msg[48];
    snprintf(msg, sizeof(msg), "node 0x%08x %s",
             (unsigned)node_id, joined ? "joined" : "left");
    heartbeat_post("swarm", msg);
#endif
}

static int find_or_add_node(uint32_t node_id)
{
    for (int i = 0; i < CLAW_SWARM_MAX_NODES; i++) {
        if (nodes[i].state != SWARM_NODE_OFFLINE && nodes[i].id == node_id) {
            return i;
        }
    }
    for (int i = 0; i < CLAW_SWARM_MAX_NODES; i++) {
        if (nodes[i].state == SWARM_NODE_OFFLINE) {
            nodes[i].id = node_id;
            nodes[i].state = SWARM_NODE_ONLINE;
            node_count++;
            return i;
        }
    }
    return -1;
}

static void heartbeat_send(void)
{
    if (s_sock < 0) {
        return;
    }

    struct swarm_heartbeat hb;
    hb.magic = SWARM_HEARTBEAT_MAGIC;
    hb.node_id = s_self_id;
    hb.uptime_s = claw_tick_ms() / 1000;
    hb.capabilities = 0;
    hb.load = 0;
    hb.port = CLAW_SWARM_PORT;

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(CLAW_SWARM_PORT);
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    sendto(s_sock, &hb, sizeof(hb), 0,
           (struct sockaddr *)&dest, sizeof(dest));
}

static void check_timeouts(void)
{
    uint32_t now = claw_tick_ms();

    claw_mutex_lock(swarm_lock, CLAW_WAIT_FOREVER);

    for (int i = 0; i < CLAW_SWARM_MAX_NODES; i++) {
        if (nodes[i].state == SWARM_NODE_OFFLINE) {
            continue;
        }
        if (nodes[i].id == s_self_id) {
            continue;
        }
        if ((now - nodes[i].last_seen) > CLAW_SWARM_TIMEOUT_MS) {
            CLAW_LOGI(TAG, "node 0x%08x timed out",
                      (unsigned)nodes[i].id);
            nodes[i].state = SWARM_NODE_OFFLINE;
            node_count--;
            notify_node_event(nodes[i].id, 0);
        }
    }

    claw_mutex_unlock(swarm_lock);
}

static void heartbeat_timer_cb(void *arg)
{
    (void)arg;
    heartbeat_send();
    check_timeouts();
}

static void receiver_thread(void *arg)
{
    (void)arg;
    struct swarm_heartbeat hb;
    struct sockaddr_in src;
    socklen_t src_len;

    while (1) {
        src_len = sizeof(src);
        int n = recvfrom(s_sock, &hb, sizeof(hb), 0,
                         (struct sockaddr *)&src, &src_len);

        if (n != sizeof(hb) || hb.magic != SWARM_HEARTBEAT_MAGIC) {
            continue;
        }

        if (hb.node_id == s_self_id) {
            continue;
        }

        claw_mutex_lock(swarm_lock, CLAW_WAIT_FOREVER);

        int idx = find_or_add_node(hb.node_id);
        if (idx >= 0) {
            int is_new = (nodes[idx].last_seen == 0);
            nodes[idx].last_seen = claw_tick_ms();
            nodes[idx].ip_addr = ntohl(src.sin_addr.s_addr);
            nodes[idx].port = ntohs(hb.port);
            nodes[idx].capabilities = hb.capabilities;
            nodes[idx].load = hb.load;
            nodes[idx].uptime_s = hb.uptime_s;

            if (is_new) {
                CLAW_LOGI(TAG, "node 0x%08x joined from %s",
                          (unsigned)hb.node_id, inet_ntoa(src.sin_addr));
                notify_node_event(hb.node_id, 1);
            }
        }

        claw_mutex_unlock(swarm_lock);
    }
}

int swarm_start(void)
{
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        CLAW_LOGE(TAG, "socket create failed");
        return CLAW_ERROR;
    }

    int opt = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
    setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CLAW_SWARM_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        CLAW_LOGE(TAG, "bind port %d failed", CLAW_SWARM_PORT);
        close(s_sock);
        s_sock = -1;
        return CLAW_ERROR;
    }

    claw_mutex_lock(swarm_lock, CLAW_WAIT_FOREVER);
    int idx = find_or_add_node(s_self_id);
    if (idx >= 0) {
        nodes[idx].last_seen = claw_tick_ms();
        nodes[idx].port = CLAW_SWARM_PORT;
    }
    claw_mutex_unlock(swarm_lock);

    s_hb_timer = claw_timer_create("swarm_hb", heartbeat_timer_cb, NULL,
                                    CLAW_SWARM_HEARTBEAT_MS, 1);
    if (!s_hb_timer) {
        CLAW_LOGE(TAG, "timer create failed");
        close(s_sock);
        s_sock = -1;
        return CLAW_ERROR;
    }
    claw_timer_start(s_hb_timer);

    claw_thread_t rx = claw_thread_create("swarm_rx", receiver_thread,
                                           NULL,
                                           CLAW_SWARM_THREAD_STACK,
                                           CLAW_SWARM_THREAD_PRIO);
    if (!rx) {
        CLAW_LOGE(TAG, "rx thread create failed");
        close(s_sock);
        s_sock = -1;
        return CLAW_ERROR;
    }

    CLAW_LOGI(TAG, "heartbeat started, port=%d", CLAW_SWARM_PORT);
    return CLAW_OK;
}

#else /* no socket support */

int swarm_start(void)
{
    CLAW_LOGI(TAG, "heartbeat not available on this platform");
    return CLAW_OK;
}

#endif

int swarm_init(void)
{
    swarm_lock = claw_mutex_create("swarm");
    if (!swarm_lock) {
        CLAW_LOGE(TAG, "mutex create failed");
        return CLAW_ERROR;
    }

    memset(nodes, 0, sizeof(nodes));
    node_count = 0;
    s_self_id = generate_node_id();

    CLAW_LOGI(TAG, "initialized, self_id=0x%08x, max_nodes=%d",
              (unsigned)s_self_id, CLAW_SWARM_MAX_NODES);
    return CLAW_OK;
}

uint32_t swarm_self_id(void)
{
    return s_self_id;
}

int swarm_node_count(void)
{
    return node_count;
}

void swarm_list_nodes(void)
{
    claw_mutex_lock(swarm_lock, CLAW_WAIT_FOREVER);

    CLAW_LOGI(TAG, "nodes: %d/%d (self=0x%08x)",
              node_count, CLAW_SWARM_MAX_NODES, (unsigned)s_self_id);

    for (int i = 0; i < CLAW_SWARM_MAX_NODES; i++) {
        if (nodes[i].state != SWARM_NODE_OFFLINE) {
            const char *state_str =
                (nodes[i].id == s_self_id) ? "self" :
                (nodes[i].state == SWARM_NODE_ONLINE) ? "online" : "disc";
            CLAW_LOGI(TAG, "  [%d] id=0x%08x  %-6s  load=%d%%  up=%us",
                      i, (unsigned)nodes[i].id, state_str,
                      nodes[i].load, (unsigned)nodes[i].uptime_s);
        }
    }

    claw_mutex_unlock(swarm_lock);
}
