/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 *
 * Swarm service — heartbeat broadcast and node discovery via UDP.
 */

#include <string.h>
#include "claw_os.h"
#include "claw_config.h"
#include "swarm.h"
#include "gateway.h"

#define TAG "swarm"

static struct swarm_node nodes[CLAW_SWARM_MAX_NODES];
static int node_count = 0;
static claw_mutex_t swarm_lock;
static uint32_t s_self_id;

#ifdef CLAW_PLATFORM_ESP_IDF

#include "esp_mac.h"
#include "lwip/sockets.h"

static int s_sock = -1;
static claw_timer_t s_hb_timer;

/* Generate node ID from MAC address */
static uint32_t generate_node_id(void)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    /* Use last 4 bytes of MAC as ID */
    return ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
           ((uint32_t)mac[4] << 8)  | (uint32_t)mac[5];
}

static void notify_node_event(uint32_t node_id, int joined)
{
    struct gateway_msg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = GW_MSG_SWARM;
    msg.len = snprintf((char *)msg.payload, sizeof(msg.payload),
                       "node 0x%08x %s", (unsigned)node_id,
                       joined ? "joined" : "left");
    gateway_send(&msg);
}

static int find_or_add_node(uint32_t node_id)
{
    /* Find existing */
    for (int i = 0; i < CLAW_SWARM_MAX_NODES; i++) {
        if (nodes[i].state != SWARM_NODE_OFFLINE && nodes[i].id == node_id) {
            return i;
        }
    }

    /* Find free slot */
    for (int i = 0; i < CLAW_SWARM_MAX_NODES; i++) {
        if (nodes[i].state == SWARM_NODE_OFFLINE) {
            nodes[i].id = node_id;
            nodes[i].state = SWARM_NODE_ONLINE;
            node_count++;
            return i;
        }
    }

    return -1;  /* table full */
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

    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port = htons(CLAW_SWARM_PORT),
        .sin_addr.s_addr = htonl(INADDR_BROADCAST),
    };

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
            continue;   /* don't timeout self */
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

        /* Ignore self */
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
    /* Create UDP socket */
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        CLAW_LOGE(TAG, "socket create failed");
        return CLAW_ERROR;
    }

    /* Enable broadcast */
    int opt = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

    /* Allow address reuse */
    setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Bind to heartbeat port */
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(CLAW_SWARM_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        CLAW_LOGE(TAG, "bind port %d failed", CLAW_SWARM_PORT);
        close(s_sock);
        s_sock = -1;
        return CLAW_ERROR;
    }

    /* Register self in node table */
    claw_mutex_lock(swarm_lock, CLAW_WAIT_FOREVER);
    int idx = find_or_add_node(s_self_id);
    if (idx >= 0) {
        nodes[idx].last_seen = claw_tick_ms();
        nodes[idx].port = CLAW_SWARM_PORT;
    }
    claw_mutex_unlock(swarm_lock);

    /* Start heartbeat timer */
    s_hb_timer = claw_timer_create("swarm_hb", heartbeat_timer_cb, NULL,
                                    CLAW_SWARM_HEARTBEAT_MS, 1);
    claw_timer_start(s_hb_timer);

    /* Start receiver thread */
    claw_thread_create("swarm_rx", receiver_thread, NULL,
                       CLAW_SWARM_THREAD_STACK, CLAW_SWARM_THREAD_PRIO);

    CLAW_LOGI(TAG, "heartbeat started, port=%d", CLAW_SWARM_PORT);
    return CLAW_OK;
}

#else /* non-ESP-IDF platforms */

static uint32_t generate_node_id(void)
{
    return 0x0000DEAD;
}

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
