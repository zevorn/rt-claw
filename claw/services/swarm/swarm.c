/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Swarm service — heartbeat broadcast and node discovery via UDP.
 * OOP: private context struct embedding struct claw_service.
 */

#include "osal/claw_os.h"
#include "claw_config.h"
#include "osal/claw_net.h"
#include "claw/services/swarm/swarm.h"
#include "claw/tools/claw_tools.h"
#include "claw/core/claw_service.h"
#include "utils/list.h"
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

/* ------------------------------------------------------------------ */
/* Swarm context — all state lives here, no file-scope globals.       */
/* ------------------------------------------------------------------ */

struct swarm_ctx {
    struct claw_service   base;     /* must be first member */

    struct swarm_node     nodes[CLAW_SWARM_MAX_NODES];
    int                   node_count;
    struct claw_mutex    *lock;
    uint32_t              self_id;

#if defined(CLAW_PLATFORM_ESP_IDF) || \
    defined(CLAW_PLATFORM_RTTHREAD) || \
    defined(CLAW_PLATFORM_LINUX)
    int                   sock;
    struct claw_timer    *hb_timer;
    struct claw_thread   *rx_thread;

    /* RPC state */
    struct claw_sem      *rpc_sem;
    struct claw_mutex    *rpc_lock;
    uint16_t              rpc_seq;
    uint16_t              rpc_pending_seq;
    char                  rpc_result[SWARM_RPC_PAYLOAD_MAX];
    uint8_t               rpc_status;
#endif
};

CLAW_ASSERT_EMBEDDED_FIRST(struct swarm_ctx, base);

static struct swarm_ctx s_ctx;

/* ------------------------------------------------------------------ */
/* Node ID generation — platform-specific hardware identity           */
/* ------------------------------------------------------------------ */

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

static int find_or_add_node(struct swarm_ctx *ctx, uint32_t node_id)
{
    for (int i = 0; i < CLAW_SWARM_MAX_NODES; i++) {
        if (ctx->nodes[i].state != SWARM_NODE_OFFLINE &&
            ctx->nodes[i].id == node_id) {
            return i;
        }
    }
    for (int i = 0; i < CLAW_SWARM_MAX_NODES; i++) {
        if (ctx->nodes[i].state == SWARM_NODE_OFFLINE) {
            ctx->nodes[i].id = node_id;
            ctx->nodes[i].state = SWARM_NODE_ONLINE;
            ctx->node_count++;
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

static void heartbeat_send(struct swarm_ctx *ctx)
{
    if (ctx->sock < 0) {
        return;
    }

    struct swarm_heartbeat hb;
    memset(&hb, 0, sizeof(hb));
    hb.magic = SWARM_HEARTBEAT_MAGIC;
    hb.node_id = ctx->self_id;
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

    sendto(ctx->sock, &hb, sizeof(hb), 0,
           (struct sockaddr *)&dest, sizeof(dest));
}

static void check_timeouts(struct swarm_ctx *ctx)
{
    uint32_t now = claw_tick_ms();

    claw_mutex_lock(ctx->lock, CLAW_WAIT_FOREVER);

    for (int i = 0; i < CLAW_SWARM_MAX_NODES; i++) {
        if (ctx->nodes[i].state == SWARM_NODE_OFFLINE) {
            continue;
        }
        if (ctx->nodes[i].id == ctx->self_id) {
            continue;
        }
        if ((now - ctx->nodes[i].last_seen) > CLAW_SWARM_TIMEOUT_MS) {
            CLAW_LOGI(TAG, "node 0x%08x timed out",
                      (unsigned)ctx->nodes[i].id);
            ctx->nodes[i].state = SWARM_NODE_OFFLINE;
            ctx->node_count--;
            notify_node_event(ctx->nodes[i].id, 0);
        }
    }

    claw_mutex_unlock(ctx->lock);
}

static void heartbeat_timer_cb(void *arg)
{
    struct swarm_ctx *ctx = (struct swarm_ctx *)arg;
    heartbeat_send(ctx);
    check_timeouts(ctx);
}

static void send_rpc_to(struct swarm_ctx *ctx,
                         const struct sockaddr_in *dest,
                         const struct swarm_rpc_msg *msg)
{
    sendto(ctx->sock, msg, sizeof(*msg), 0,
           (const struct sockaddr *)dest, sizeof(*dest));
}

static void handle_rpc_request(struct swarm_ctx *ctx,
                                const struct swarm_rpc_msg *req,
                                const struct sockaddr_in *src)
{
    struct swarm_rpc_msg resp;
    memset(&resp, 0, sizeof(resp));
    resp.magic    = SWARM_RPC_MAGIC;
    resp.src_node = ctx->self_id;
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
    send_rpc_to(ctx, src, &resp);
}

static void handle_rpc_response(struct swarm_ctx *ctx,
                                 const struct swarm_rpc_msg *resp)
{
    claw_mutex_lock(ctx->rpc_lock, CLAW_WAIT_FOREVER);
    if (resp->seq == ctx->rpc_pending_seq) {
        ctx->rpc_status = resp->status;
        snprintf(ctx->rpc_result, sizeof(ctx->rpc_result),
                 "%s", resp->payload);
        claw_sem_give(ctx->rpc_sem);
    }
    claw_mutex_unlock(ctx->rpc_lock);
}

/*
 * Unified receiver — dispatch by magic number.
 * Buffer is large enough for the biggest message type (RPC).
 */
static void receiver_thread(void *arg)
{
    struct swarm_ctx *ctx = (struct swarm_ctx *)arg;
    uint8_t buf[sizeof(struct swarm_rpc_msg)];
    struct sockaddr_in src;
    socklen_t src_len;

    while (!claw_thread_should_exit()) {
        src_len = sizeof(src);
        int n = recvfrom(ctx->sock, buf, sizeof(buf), 0,
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

            if (hb.node_id == ctx->self_id) {
                continue;
            }

            claw_mutex_lock(ctx->lock, CLAW_WAIT_FOREVER);
            int idx = find_or_add_node(ctx, hb.node_id);
            if (idx >= 0) {
                int is_new = (ctx->nodes[idx].last_seen == 0);
                ctx->nodes[idx].last_seen = claw_tick_ms();
                ctx->nodes[idx].ip_addr = ntohl(src.sin_addr.s_addr);
                ctx->nodes[idx].port = ntohs(hb.port);
                ctx->nodes[idx].capabilities = hb.capabilities;
                ctx->nodes[idx].load = hb.load;
                ctx->nodes[idx].uptime_s = hb.uptime_s;
                ctx->nodes[idx].role = hb.role;
                ctx->nodes[idx].active_tasks = hb.active_tasks;

                if (is_new) {
                    CLAW_LOGI(TAG, "node 0x%08x joined from %s",
                              (unsigned)hb.node_id,
                              inet_ntoa(src.sin_addr));
                    notify_node_event(hb.node_id, 1);
                }
            }
            claw_mutex_unlock(ctx->lock);
        } else if (magic == SWARM_RPC_MAGIC &&
                   n == (int)sizeof(struct swarm_rpc_msg)) {
            struct swarm_rpc_msg rpc;
            memcpy(&rpc, buf, sizeof(rpc));

            if (rpc.dst_node != 0 && rpc.dst_node != ctx->self_id) {
                continue;
            }

            if (rpc.type == SWARM_RPC_REQUEST) {
                handle_rpc_request(ctx, &rpc, &src);
            } else if (rpc.type == SWARM_RPC_RESPONSE) {
                handle_rpc_response(ctx, &rpc);
            }
        }
    }
}

/*
 * Find the best capable node — lowest load among online nodes
 * matching the required capability bitmap.
 */
static int find_best_node(struct swarm_ctx *ctx, uint8_t cap,
                           uint32_t *out_id, uint32_t *out_ip,
                           uint16_t *out_port)
{
    int best = -1;
    int min_load = 101;

    for (int i = 0; i < CLAW_SWARM_MAX_NODES; i++) {
        if (ctx->nodes[i].state != SWARM_NODE_ONLINE) {
            continue;
        }
        if (ctx->nodes[i].id == ctx->self_id) {
            continue;
        }
        if ((ctx->nodes[i].capabilities & cap) != cap) {
            continue;
        }
        if (ctx->nodes[i].load < min_load) {
            min_load = ctx->nodes[i].load;
            best = i;
        }
    }

    if (best < 0) {
        return CLAW_ERROR;
    }

    *out_id   = ctx->nodes[best].id;
    *out_ip   = ctx->nodes[best].ip_addr;
    *out_port = ctx->nodes[best].port;
    return CLAW_OK;
}

int swarm_rpc_call(const char *tool_name, const char *params,
                   char *result, size_t result_sz)
{
    struct swarm_ctx *ctx = &s_ctx;

    if (!tool_name || !result || result_sz == 0) {
        return CLAW_ERROR;
    }
    if (ctx->sock < 0) {
        return CLAW_ERROR;
    }

    /* Check tool flags — refuse to delegate local-only tools */
    const claw_tool_t *tool = claw_tool_find(tool_name);
    if (tool && (tool->flags & CLAW_TOOL_LOCAL_ONLY)) {
        return CLAW_ERROR;
    }

    uint8_t cap = resolve_tool_caps(tool_name);

    /* Find the best capable node (load-aware) */
    claw_mutex_lock(ctx->lock, CLAW_WAIT_FOREVER);
    uint32_t target_id = 0;
    uint32_t target_ip = 0;
    uint16_t target_port = 0;
    int found = find_best_node(ctx, cap, &target_id, &target_ip,
                               &target_port);
    claw_mutex_unlock(ctx->lock);

    if (found != CLAW_OK) {
        CLAW_LOGD(TAG, "rpc: no node with cap 0x%02x for %s",
                  cap, tool_name);
        return CLAW_ERROR;
    }

    /* Build RPC request */
    struct swarm_rpc_msg req;
    memset(&req, 0, sizeof(req));
    req.magic    = SWARM_RPC_MAGIC;
    req.src_node = ctx->self_id;
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
        claw_mutex_lock(ctx->rpc_lock, CLAW_WAIT_FOREVER);
        ctx->rpc_seq++;
        req.seq = ctx->rpc_seq;
        ctx->rpc_pending_seq = ctx->rpc_seq;
        ctx->rpc_result[0] = '\0';
        ctx->rpc_status = SWARM_RPC_ERROR;
        claw_mutex_unlock(ctx->rpc_lock);

        CLAW_LOGI(TAG, "rpc: %s -> node 0x%08x (attempt %d/%d)",
                  tool_name, (unsigned)target_id,
                  attempt + 1, SWARM_RPC_MAX_RETRIES);
        send_rpc_to(ctx, &dest, &req);

        if (claw_sem_take(ctx->rpc_sem,
                          SWARM_RPC_TIMEOUT_MS) == CLAW_OK) {
            claw_mutex_lock(ctx->rpc_lock, CLAW_WAIT_FOREVER);
            snprintf(result, result_sz, "%s", ctx->rpc_result);
            int ok = (ctx->rpc_status == SWARM_RPC_OK);
            claw_mutex_unlock(ctx->rpc_lock);
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

static claw_err_t swarm_svc_start(struct claw_service *svc)
{
    struct swarm_ctx *ctx = container_of(svc, struct swarm_ctx, base);

    ctx->rpc_sem = claw_sem_create("rpc", 0);
    ctx->rpc_lock = claw_mutex_create("rpc");
    ctx->rpc_seq = 0;

    ctx->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ctx->sock < 0) {
        CLAW_LOGE(TAG, "socket create failed");
        return CLAW_ERR_IO;
    }

    int opt = 1;
    setsockopt(ctx->sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
    setsockopt(ctx->sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Recv timeout so recvfrom() can be interrupted for shutdown */
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(ctx->sock, SOL_SOCKET, SO_RCVTIMEO,
               &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CLAW_SWARM_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(ctx->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        CLAW_LOGE(TAG, "bind port %d failed", CLAW_SWARM_PORT);
        close(ctx->sock);
        ctx->sock = -1;
        return CLAW_ERR_IO;
    }

    claw_mutex_lock(ctx->lock, CLAW_WAIT_FOREVER);
    int idx = find_or_add_node(ctx, ctx->self_id);
    if (idx >= 0) {
        ctx->nodes[idx].last_seen = claw_tick_ms();
        ctx->nodes[idx].port = CLAW_SWARM_PORT;
    }
    claw_mutex_unlock(ctx->lock);

    ctx->hb_timer = claw_timer_create("swarm_hb", heartbeat_timer_cb,
                                       ctx,
                                       CLAW_SWARM_HEARTBEAT_MS, 1);
    if (!ctx->hb_timer) {
        CLAW_LOGE(TAG, "timer create failed");
        close(ctx->sock);
        ctx->sock = -1;
        return CLAW_ERR_NOMEM;
    }
    claw_timer_start(ctx->hb_timer);
    heartbeat_send(ctx);

    ctx->rx_thread = claw_thread_create("swarm_rx", receiver_thread,
                                         ctx,
                                         CLAW_SWARM_THREAD_STACK,
                                         CLAW_SWARM_THREAD_PRIO);
    if (!ctx->rx_thread) {
        CLAW_LOGE(TAG, "rx thread create failed");
        close(ctx->sock);
        ctx->sock = -1;
        return CLAW_ERR_NOMEM;
    }

    CLAW_LOGI(TAG, "heartbeat started, port=%d", CLAW_SWARM_PORT);
    return CLAW_OK;
}

static void swarm_svc_stop(struct claw_service *svc)
{
    struct swarm_ctx *ctx = container_of(svc, struct swarm_ctx, base);

    /* Close socket first to unblock recvfrom() */
    if (ctx->sock >= 0) {
        close(ctx->sock);
        ctx->sock = -1;
    }

    claw_thread_delete(ctx->rx_thread);
    ctx->rx_thread = NULL;

    if (ctx->hb_timer) {
        claw_timer_stop(ctx->hb_timer);
        claw_timer_delete(ctx->hb_timer);
        ctx->hb_timer = NULL;
    }

    if (ctx->rpc_sem) {
        claw_sem_delete(ctx->rpc_sem);
        ctx->rpc_sem = NULL;
    }
    if (ctx->rpc_lock) {
        claw_mutex_delete(ctx->rpc_lock);
        ctx->rpc_lock = NULL;
    }

    CLAW_LOGI(TAG, "stopped");
}

#else /* no socket support */

static claw_err_t swarm_svc_start(struct claw_service *svc)
{
    (void)svc;
    CLAW_LOGI(TAG, "heartbeat not available on this platform");
    return CLAW_OK;
}

static void swarm_svc_stop(struct claw_service *svc)
{
    (void)svc;
}

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

/* ------------------------------------------------------------------ */
/* OOP lifecycle: init (common to all platforms)                       */
/* ------------------------------------------------------------------ */

static claw_err_t swarm_svc_init(struct claw_service *svc)
{
    struct swarm_ctx *ctx = container_of(svc, struct swarm_ctx, base);

    ctx->lock = claw_mutex_create("swarm");
    if (!ctx->lock) {
        CLAW_LOGE(TAG, "mutex create failed");
        return CLAW_ERR_NOMEM;
    }

    memset(ctx->nodes, 0, sizeof(ctx->nodes));
    ctx->node_count = 0;
    ctx->self_id = generate_node_id();

    CLAW_LOGI(TAG, "initialized, self_id=0x%08x, max_nodes=%d",
              (unsigned)ctx->self_id, CLAW_SWARM_MAX_NODES);
    return CLAW_OK;
}

/* ------------------------------------------------------------------ */
/* Public API — thin wrappers via static singleton                    */
/* ------------------------------------------------------------------ */

int swarm_init(void)
{
    return swarm_svc_init(&s_ctx.base) == CLAW_OK ? CLAW_OK : CLAW_ERROR;
}

int swarm_start(void)
{
    return swarm_svc_start(&s_ctx.base) == CLAW_OK ? CLAW_OK : CLAW_ERROR;
}

void swarm_stop(void)
{
    swarm_svc_stop(&s_ctx.base);
}

uint32_t swarm_self_id(void)
{
    return s_ctx.self_id;
}

int swarm_node_count(void)
{
    return s_ctx.node_count;
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
    struct swarm_ctx *ctx = &s_ctx;

    claw_mutex_lock(ctx->lock, CLAW_WAIT_FOREVER);

    printf("nodes: %d/%d (self=0x%08x)\n",
           ctx->node_count, CLAW_SWARM_MAX_NODES,
           (unsigned)ctx->self_id);

    for (int i = 0; i < CLAW_SWARM_MAX_NODES; i++) {
        if (ctx->nodes[i].state != SWARM_NODE_OFFLINE) {
            const char *state_str =
                (ctx->nodes[i].id == ctx->self_id) ? "self" :
                (ctx->nodes[i].state == SWARM_NODE_ONLINE) ?
                    "online" : "disc";
            printf("  [%d] id=0x%08x  %-6s  cap=0x%02x  "
                   "load=%d%%  role=%-5s  tasks=%d  up=%us\n",
                   i, (unsigned)ctx->nodes[i].id, state_str,
                   ctx->nodes[i].capabilities,
                   ctx->nodes[i].load,
                   role_name(ctx->nodes[i].role),
                   ctx->nodes[i].active_tasks,
                   (unsigned)ctx->nodes[i].uptime_s);
        }
    }

    claw_mutex_unlock(ctx->lock);
}

/* ------------------------------------------------------------------ */
/* OOP service registration                                           */
/* ------------------------------------------------------------------ */

static const char *swarm_deps[] = { "gateway", NULL };

static const struct claw_service_ops swarm_svc_ops = {
    .init  = swarm_svc_init,
    .start = swarm_svc_start,
    .stop  = swarm_svc_stop,
};

static struct swarm_ctx s_ctx = {
    .base = {
        .name  = "swarm",
        .ops   = &swarm_svc_ops,
        .deps  = swarm_deps,
        .state = CLAW_SVC_CREATED,
    },
};

CLAW_SERVICE_REGISTER(swarm, &s_ctx.base);
