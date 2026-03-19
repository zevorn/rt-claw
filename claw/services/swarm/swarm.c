/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Swarm service — heartbeat broadcast and node discovery via UDP.
 */

#include "osal/claw_os.h"
#include "claw_config.h"
#include "osal/claw_net.h"
#include "claw/services/swarm/swarm.h"
#include "claw/tools/claw_tools.h"
#ifdef CONFIG_RTCLAW_HEARTBEAT_ENABLE
#include "claw/core/heartbeat.h"
#endif

#include <string.h>
#include <stdio.h>

/* BSD socket API — included directly, not via claw_net.h */
#ifdef CLAW_PLATFORM_ESP_IDF
#include "lwip/sockets.h"
#elif defined(CLAW_PLATFORM_RTTHREAD) || \
    defined(CLAW_PLATFORM_LINUX)
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#define TAG "swarm"

static struct swarm_node nodes[CLAW_SWARM_MAX_NODES];
static int node_count;
static struct claw_mutex *swarm_lock;
static uint32_t s_self_id;

/* Node ID generation — platform-specific hardware identity */
#ifdef CLAW_PLATFORM_ESP_IDF
#include "esp_mac.h"
#include "esp_random.h"

static uint32_t generate_node_id(void)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    uint32_t id = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
                  ((uint32_t)mac[4] << 8)  | (uint32_t)mac[5];
    /*
     * Mix in hardware random bits so QEMU instances (which share
     * the same efuse MAC) get distinct node IDs.
     */
    id ^= (esp_random() & 0xFFFF);
    return id;
}

#elif defined(CLAW_PLATFORM_RTTHREAD)

static uint32_t generate_node_id(void)
{
    return 0x52540000 | (claw_tick_ms() & 0xFFFF);
}

#elif defined(CLAW_PLATFORM_LINUX)

static uint32_t generate_node_id(void)
{
    FILE *f = fopen("/etc/machine-id", "r");
    if (f) {
        char buf[33];
        if (fgets(buf, sizeof(buf), f)) {
            fclose(f);
            uint32_t hash = 0;
            for (int i = 0; buf[i]; i++) {
                hash = hash * 31 + (uint8_t)buf[i];
            }
            return hash;
        }
        fclose(f);
    }
    return 0x4C4E5800 | (claw_tick_ms() & 0xFFFF);
}

#else

static uint32_t generate_node_id(void)
{
    return 0x0000DEAD;
}

#endif

/*
 * Shared socket-based swarm implementation.
 * Requires POSIX socket API.
 */
#if defined(CLAW_PLATFORM_ESP_IDF) || \
    defined(CLAW_PLATFORM_RTTHREAD) || \
    defined(CLAW_PLATFORM_LINUX)

static int s_sock = -1;
static struct claw_timer *s_hb_timer;
static struct claw_thread *s_rx_thread;

/* --- RPC state --- */
static struct claw_sem *s_rpc_sem;
static struct claw_mutex *s_rpc_lock;
static uint16_t     s_rpc_seq;
static uint16_t     s_rpc_pending_seq;
static char         s_rpc_result[SWARM_RPC_PAYLOAD_MAX];
static uint8_t      s_rpc_status;

static uint8_t build_capabilities(void);

static uint8_t resolve_tool_caps(const char *name)
{
    const claw_tool_t *tool = claw_tool_find(name);
    if (tool && tool->required_caps) {
        return tool->required_caps;
    }
    /* Fallback: prefix-based heuristic for unregistered tools */
    if (strncmp(name, "gpio_", 5) == 0) {
        return SWARM_CAP_GPIO;
    }
    if (strncmp(name, "lcd_", 4) == 0) {
        return SWARM_CAP_LCD;
    }
    return SWARM_CAP_AI;
}

static uint8_t determine_self_role(void)
{
    uint8_t caps = build_capabilities();
    if ((caps & SWARM_CAP_AI) && (caps & SWARM_CAP_INTERNET)) {
        return SWARM_ROLE_THINKER;
    }
    if (caps & SWARM_CAP_GPIO) {
        return SWARM_ROLE_WORKER;
    }
    return SWARM_ROLE_OBSERVER;
}

static uint8_t count_active_tasks(void)
{
    /* Approximate: count non-idle RPC state */
    return 0;
}

static void notify_node_event(uint32_t node_id, int joined)
{
    CLAW_LOGI(TAG, "node 0x%08x %s",
              (unsigned)node_id, joined ? "joined" : "left");
    /*
     * Also print via claw_log_raw so it's visible even when
     * esp_log is silenced (shell mode).
     */
    claw_log_raw("  [swarm] node 0x%08x %s\n",
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

static uint8_t build_capabilities(void)
{
    uint8_t caps = 0;

#ifdef CONFIG_RTCLAW_TOOL_GPIO
    caps |= SWARM_CAP_GPIO;
#endif
#ifdef CONFIG_RTCLAW_LCD_ENABLE
    caps |= SWARM_CAP_LCD;
#endif
#ifdef CONFIG_RTCLAW_AI_API_KEY
    if (CONFIG_RTCLAW_AI_API_KEY[0] != '\0') {
        caps |= SWARM_CAP_AI;
    }
#endif
    /* WiFi-capable boards have internet when connected */
#if defined(CONFIG_RTCLAW_WIFI_ENABLE) || \
    defined(CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM)
    caps |= SWARM_CAP_INTERNET;
#endif

    return caps;
}

static void heartbeat_send(void)
{
    if (s_sock < 0) {
        return;
    }

    struct swarm_heartbeat hb;
    memset(&hb, 0, sizeof(hb));
    hb.magic = SWARM_HEARTBEAT_MAGIC;
    hb.node_id = s_self_id;
    hb.uptime_s = claw_tick_ms() / 1000;
    hb.capabilities = build_capabilities();
    hb.load = 0;
    hb.port = CLAW_SWARM_PORT;
    hb.role = determine_self_role();
    hb.active_tasks = count_active_tasks();

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

static void send_rpc_to(const struct sockaddr_in *dest,
                        const struct swarm_rpc_msg *msg)
{
    sendto(s_sock, msg, sizeof(*msg), 0,
           (const struct sockaddr *)dest, sizeof(*dest));
}

static void handle_rpc_request(const struct swarm_rpc_msg *req,
                               const struct sockaddr_in *src)
{
    struct swarm_rpc_msg resp;
    memset(&resp, 0, sizeof(resp));
    resp.magic    = SWARM_RPC_MAGIC;
    resp.src_node = s_self_id;
    resp.dst_node = req->src_node;
    resp.seq      = req->seq;
    resp.type     = SWARM_RPC_RESPONSE;
    snprintf(resp.tool_name, sizeof(resp.tool_name),
             "%s", req->tool_name);

    const claw_tool_t *tool = claw_tool_find(req->tool_name);
    if (!tool) {
        resp.status = SWARM_RPC_NOT_FOUND;
        snprintf(resp.payload, sizeof(resp.payload),
                 "{\"error\":\"tool not found on this node\"}");
    } else {
        cJSON *params = cJSON_Parse(req->payload);
        cJSON *result = cJSON_CreateObject();

        int rc = tool->execute(params ? params : cJSON_CreateObject(),
                               result);
        resp.status = (rc == CLAW_OK) ? SWARM_RPC_OK : SWARM_RPC_ERROR;

        char *rs = cJSON_PrintUnformatted(result);
        if (rs) {
            snprintf(resp.payload, sizeof(resp.payload), "%s", rs);
            cJSON_free(rs);
        }
        cJSON_Delete(result);
        if (params) {
            cJSON_Delete(params);
        }
    }

    CLAW_LOGI(TAG, "rpc exec: %s -> status=%d", req->tool_name,
              resp.status);
    send_rpc_to(src, &resp);
}

static void handle_rpc_response(const struct swarm_rpc_msg *resp)
{
    claw_mutex_lock(s_rpc_lock, CLAW_WAIT_FOREVER);
    if (resp->seq == s_rpc_pending_seq) {
        s_rpc_status = resp->status;
        snprintf(s_rpc_result, sizeof(s_rpc_result),
                 "%s", resp->payload);
        claw_sem_give(s_rpc_sem);
    }
    claw_mutex_unlock(s_rpc_lock);
}

/*
 * Unified receiver — dispatch by magic number.
 * Buffer is large enough for the biggest message type (RPC).
 */
static void receiver_thread(void *arg)
{
    (void)arg;
    uint8_t buf[sizeof(struct swarm_rpc_msg)];
    struct sockaddr_in src;
    socklen_t src_len;

    while (!claw_thread_should_exit()) {
        src_len = sizeof(src);
        int n = recvfrom(s_sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&src, &src_len);
        if (n < 0 && claw_thread_should_exit()) {
            break;
        }
        if (n < 4) {
            continue;
        }

        uint32_t magic;
        memcpy(&magic, buf, 4);

        if (magic == SWARM_HEARTBEAT_MAGIC &&
            n == (int)sizeof(struct swarm_heartbeat)) {
            struct swarm_heartbeat hb;
            memcpy(&hb, buf, sizeof(hb));

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
                nodes[idx].role = hb.role;
                nodes[idx].active_tasks = hb.active_tasks;

                if (is_new) {
                    CLAW_LOGI(TAG, "node 0x%08x joined from %s",
                              (unsigned)hb.node_id,
                              inet_ntoa(src.sin_addr));
                    notify_node_event(hb.node_id, 1);
                }
            }
            claw_mutex_unlock(swarm_lock);
        } else if (magic == SWARM_RPC_MAGIC &&
                   n == (int)sizeof(struct swarm_rpc_msg)) {
            struct swarm_rpc_msg rpc;
            memcpy(&rpc, buf, sizeof(rpc));

            if (rpc.dst_node != 0 && rpc.dst_node != s_self_id) {
                continue;
            }

            if (rpc.type == SWARM_RPC_REQUEST) {
                handle_rpc_request(&rpc, &src);
            } else if (rpc.type == SWARM_RPC_RESPONSE) {
                handle_rpc_response(&rpc);
            }
        }
    }
}

/*
 * Find the best capable node — lowest load among online nodes
 * matching the required capability bitmap.
 */
static int find_best_node(uint8_t cap, uint32_t *out_id,
                          uint32_t *out_ip, uint16_t *out_port)
{
    int best = -1;
    int min_load = 101;

    for (int i = 0; i < CLAW_SWARM_MAX_NODES; i++) {
        if (nodes[i].state != SWARM_NODE_ONLINE) {
            continue;
        }
        if (nodes[i].id == s_self_id) {
            continue;
        }
        if ((nodes[i].capabilities & cap) != cap) {
            continue;
        }
        if (nodes[i].load < min_load) {
            min_load = nodes[i].load;
            best = i;
        }
    }

    if (best < 0) {
        return CLAW_ERROR;
    }

    *out_id   = nodes[best].id;
    *out_ip   = nodes[best].ip_addr;
    *out_port = nodes[best].port;
    return CLAW_OK;
}

int swarm_rpc_call(const char *tool_name, const char *params,
                   char *result, size_t result_sz)
{
    if (!tool_name || !result || result_sz == 0) {
        return CLAW_ERROR;
    }
    if (s_sock < 0) {
        return CLAW_ERROR;
    }

    /* Check tool flags — refuse to delegate local-only tools */
    const claw_tool_t *tool = claw_tool_find(tool_name);
    if (tool && (tool->flags & CLAW_TOOL_LOCAL_ONLY)) {
        return CLAW_ERROR;
    }

    uint8_t cap = resolve_tool_caps(tool_name);

    /* Find the best capable node (load-aware) */
    claw_mutex_lock(swarm_lock, CLAW_WAIT_FOREVER);
    uint32_t target_id = 0;
    uint32_t target_ip = 0;
    uint16_t target_port = 0;
    int found = find_best_node(cap, &target_id, &target_ip, &target_port);
    claw_mutex_unlock(swarm_lock);

    if (found != CLAW_OK) {
        CLAW_LOGD(TAG, "rpc: no node with cap 0x%02x for %s",
                  cap, tool_name);
        return CLAW_ERROR;
    }

    /* Build RPC request */
    struct swarm_rpc_msg req;
    memset(&req, 0, sizeof(req));
    req.magic    = SWARM_RPC_MAGIC;
    req.src_node = s_self_id;
    req.dst_node = target_id;
    req.type     = SWARM_RPC_REQUEST;
    snprintf(req.tool_name, sizeof(req.tool_name), "%s", tool_name);
    if (params) {
        snprintf(req.payload, sizeof(req.payload), "%s", params);
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(target_port);
    dest.sin_addr.s_addr = htonl(target_ip);

    /* Send with retry (exponential backoff) */
    for (int attempt = 0; attempt < SWARM_RPC_MAX_RETRIES; attempt++) {
        claw_mutex_lock(s_rpc_lock, CLAW_WAIT_FOREVER);
        s_rpc_seq++;
        req.seq = s_rpc_seq;
        s_rpc_pending_seq = s_rpc_seq;
        s_rpc_result[0] = '\0';
        s_rpc_status = SWARM_RPC_ERROR;
        claw_mutex_unlock(s_rpc_lock);

        CLAW_LOGI(TAG, "rpc: %s -> node 0x%08x (attempt %d/%d)",
                  tool_name, (unsigned)target_id,
                  attempt + 1, SWARM_RPC_MAX_RETRIES);
        send_rpc_to(&dest, &req);

        if (claw_sem_take(s_rpc_sem, SWARM_RPC_TIMEOUT_MS) == CLAW_OK) {
            claw_mutex_lock(s_rpc_lock, CLAW_WAIT_FOREVER);
            snprintf(result, result_sz, "%s", s_rpc_result);
            int ok = (s_rpc_status == SWARM_RPC_OK);
            claw_mutex_unlock(s_rpc_lock);
            return ok ? CLAW_OK : CLAW_ERROR;
        }

        CLAW_LOGW(TAG, "rpc timeout: %s (attempt %d/%d)",
                  tool_name, attempt + 1, SWARM_RPC_MAX_RETRIES);

        if (attempt < SWARM_RPC_MAX_RETRIES - 1) {
            claw_thread_delay_ms(SWARM_RPC_RETRY_BASE_MS << attempt);
        }
    }

    return CLAW_ERROR;
}

int swarm_start(void)
{
    s_rpc_sem = claw_sem_create("rpc", 0);
    s_rpc_lock = claw_mutex_create("rpc");
    s_rpc_seq = 0;

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        CLAW_LOGE(TAG, "socket create failed");
        return CLAW_ERROR;
    }

    int opt = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
    setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Recv timeout so recvfrom() can be interrupted for shutdown */
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO,
               &tv, sizeof(tv));

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
    heartbeat_send(); /* first heartbeat immediately, don't wait for timer */

    s_rx_thread = claw_thread_create("swarm_rx", receiver_thread,
                                      NULL,
                                      CLAW_SWARM_THREAD_STACK,
                                      CLAW_SWARM_THREAD_PRIO);
    if (!s_rx_thread) {
        CLAW_LOGE(TAG, "rx thread create failed");
        close(s_sock);
        s_sock = -1;
        return CLAW_ERROR;
    }

    CLAW_LOGI(TAG, "heartbeat started, port=%d", CLAW_SWARM_PORT);
    return CLAW_OK;
}

void swarm_stop(void)
{
    /* Close socket first to unblock recvfrom() */
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }

    claw_thread_delete(s_rx_thread);
    s_rx_thread = NULL;

    if (s_hb_timer) {
        claw_timer_stop(s_hb_timer);
        claw_timer_delete(s_hb_timer);
        s_hb_timer = NULL;
    }

    if (s_rpc_sem) {
        claw_sem_delete(s_rpc_sem);
        s_rpc_sem = NULL;
    }
    if (s_rpc_lock) {
        claw_mutex_delete(s_rpc_lock);
        s_rpc_lock = NULL;
    }

    CLAW_LOGI(TAG, "stopped");
}

#else /* no socket support */

int swarm_start(void)
{
    CLAW_LOGI(TAG, "heartbeat not available on this platform");
    return CLAW_OK;
}

void swarm_stop(void) {}

int swarm_rpc_call(const char *tool_name, const char *params,
                   char *result, size_t result_sz)
{
    (void)tool_name;
    (void)params;
    (void)result;
    (void)result_sz;
    return CLAW_ERROR;
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

static const char *role_name(uint8_t role)
{
    switch (role) {
    case SWARM_ROLE_COORDINATOR: return "coord";
    case SWARM_ROLE_THINKER:    return "think";
    case SWARM_ROLE_WORKER:     return "work";
    case SWARM_ROLE_OBSERVER:   return "obs";
    default:                    return "?";
    }
}

void swarm_list_nodes(void)
{
    claw_mutex_lock(swarm_lock, CLAW_WAIT_FOREVER);

    printf("nodes: %d/%d (self=0x%08x)\n",
           node_count, CLAW_SWARM_MAX_NODES, (unsigned)s_self_id);

    for (int i = 0; i < CLAW_SWARM_MAX_NODES; i++) {
        if (nodes[i].state != SWARM_NODE_OFFLINE) {
            const char *state_str =
                (nodes[i].id == s_self_id) ? "self" :
                (nodes[i].state == SWARM_NODE_ONLINE) ? "online" : "disc";
            printf("  [%d] id=0x%08x  %-6s  cap=0x%02x  "
                   "load=%d%%  role=%-5s  tasks=%d  up=%us\n",
                   i, (unsigned)nodes[i].id, state_str,
                   nodes[i].capabilities,
                   nodes[i].load,
                   role_name(nodes[i].role),
                   nodes[i].active_tasks,
                   (unsigned)nodes[i].uptime_s);
        }
    }

    claw_mutex_unlock(swarm_lock);
}

/* OOP service registration */
#include "claw/core/claw_service.h"
static const char *swarm_deps[] = { "gateway", NULL };
CLAW_DEFINE_SIMPLE_SERVICE(swarm, "swarm",
    swarm_init, swarm_start, swarm_stop, swarm_deps);
