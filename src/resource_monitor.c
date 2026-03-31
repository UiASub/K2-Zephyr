/*
 * Resource Monitor — periodic system telemetry sent to topside via UDP.
 *
 * Reports CPU usage, stack/RAM stats, thread count, and UDP packet counters.
 * Reuses the shared CRC32 and network constants from net.h.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <string.h>

#include "resource_monitor.h"
#include "net.h"

LOG_MODULE_DECLARE(k2_app, LOG_LEVEL_INF);

#define TELEMETRY_INTERVAL_MS  1000
#define MONITOR_STACK_SIZE     3072
#define MONITOR_PRIORITY       9
#define DIAG_LOG_EVERY_N       10   /* print diagnostics every N telemetry cycles */

K_THREAD_STACK_DEFINE(monitor_stack, MONITOR_STACK_SIZE);
static struct k_thread monitor_thread_data;

static uint32_t telemetry_seq;
static int      telem_sock = -1;

static atomic_t udp_rx_count  = ATOMIC_INIT(0);
static atomic_t udp_rx_errors = ATOMIC_INIT(0);

static uint8_t  cpu_usage_percent;
static uint32_t prev_wall_cycles;
static uint64_t prev_idle_cycles;

/* ------------------------------------------------------------------ */
/*  CPU usage measurement                                              */
/* ------------------------------------------------------------------ */

#ifdef CONFIG_THREAD_RUNTIME_STATS

static struct k_thread *idle_thread;
static bool idle_search_done;

/*
 * Callback for k_thread_foreach(): find the Zephyr idle thread.
 * Match by the canonical name "idle" (Zephyr names it "idle 00" on
 * single-core or just "idle").
 */
static void find_idle_cb(const struct k_thread *t, void *out)
{
    struct k_thread **p = out;
    if (*p) return;                          /* already found */

    const char *name = k_thread_name_get((k_tid_t)t);
    if (name && strncmp(name, "idle", 4) == 0) {
        *p = (struct k_thread *)t;
    }
}

/*
 * CPU % = (wall_clock − idle_cycles_delta) / wall_clock × 100
 *
 * We use k_cycle_get_32() for wall-clock and
 * k_thread_runtime_stats_get() on the idle thread for idle cycles.
 * k_thread_runtime_stats_all_get() returns the SUM of all threads'
 * scheduled cycles, which on a lightly loaded system barely changes
 * and gives a misleading denominator — wall-clock is the correct base.
 */
static void update_cpu_usage(void)
{
    /* Try to locate the idle thread once */
    if (!idle_thread && !idle_search_done) {
        k_thread_foreach(find_idle_cb, &idle_thread);
        idle_search_done = true;
        if (!idle_thread) {
            LOG_WRN("Idle thread not found — CPU %% will read 0");
        } else {
            LOG_INF("Idle thread located: %s",
                    k_thread_name_get(idle_thread));
        }
        /* Seed baselines */
        prev_wall_cycles = k_cycle_get_32();
        if (idle_thread) {
            k_thread_runtime_stats_t s;
            if (k_thread_runtime_stats_get(idle_thread, &s) == 0) {
                prev_idle_cycles = s.execution_cycles;
            }
        }
        return;   /* first sample is a baseline only */
    }

    if (!idle_thread) {
        cpu_usage_percent = 0;
        return;
    }

    /* Current wall-clock cycles (wraps on 32-bit, delta still correct) */
    uint32_t now_wall = k_cycle_get_32();
    uint32_t wall_delta = now_wall - prev_wall_cycles;
    prev_wall_cycles = now_wall;

    /* Current idle-thread execution cycles */
    uint64_t idle_now = 0;
    k_thread_runtime_stats_t idle_stats;
    if (k_thread_runtime_stats_get(idle_thread, &idle_stats) == 0) {
        idle_now = idle_stats.execution_cycles;
    }
    uint64_t idle_delta = idle_now - prev_idle_cycles;
    prev_idle_cycles = idle_now;

    if (wall_delta > 0) {
        /* Clamp idle_delta to wall_delta (rounding / measurement jitter) */
        if (idle_delta > (uint64_t)wall_delta) {
            idle_delta = wall_delta;
        }
        uint64_t busy = (uint64_t)wall_delta - idle_delta;
        cpu_usage_percent = (uint8_t)MIN((busy * 100) / wall_delta, 100);
    }
}

#else /* CONFIG_THREAD_RUNTIME_STATS not set */
static void update_cpu_usage(void) { cpu_usage_percent = 0; }
#endif

/* ------------------------------------------------------------------ */
/*  Thread / stack statistics                                          */
/* ------------------------------------------------------------------ */

struct thread_stats {
    uint8_t  count;
    uint32_t stack_total;
    uint32_t stack_used;
};

static void thread_stats_cb(const struct k_thread *t, void *out)
{
    struct thread_stats *s = out;
    s->count++;

#ifdef CONFIG_THREAD_STACK_INFO
    size_t unused;
    if (k_thread_stack_space_get((struct k_thread *)t, &unused) == 0) {
        size_t size = t->stack_info.size;
        s->stack_total += size;
        s->stack_used  += (size - unused);
    }
#endif
}

static struct thread_stats get_thread_stats(void)
{
    struct thread_stats s = {0};
    k_thread_foreach(thread_stats_cb, &s);
    return s;
}

/* ------------------------------------------------------------------ */
/*  Telemetry packet build & send                                      */
/* ------------------------------------------------------------------ */

static void build_telemetry(telemetry_packet_t *p)
{
    memset(p, 0, sizeof(*p));

    p->sequence          = htonl(telemetry_seq);
    p->uptime_ms         = htonl((uint32_t)k_uptime_get());
    p->cpu_usage_percent = cpu_usage_percent;

    /* Aggregate stack watermarks across all threads */
    struct thread_stats ts = get_thread_stats();
    p->thread_count = ts.count;

    /*
     * RAM budget.  CONFIG_SRAM_SIZE is the full physical SRAM on the
     * MCU (e.g. 512 KB for STM32F767).  We report it as total so the
     * topside can gauge overall memory pressure.
     *
     * "used" is the sum of per-thread stack high-water marks.
     * It does NOT include heap allocations — Zephyr has no single
     * system-wide heap query.  The field is therefore a lower bound.
     */
#ifdef CONFIG_SRAM_SIZE
    uint16_t total_kb = CONFIG_SRAM_SIZE;
#else
    uint16_t total_kb = 512;
#endif
    p->ram_total_kb = htons(total_kb);

    /*
     * Compute percentage from bytes to avoid double integer truncation.
     * Old code: used_kb = stack_used/1024; pct = (used_kb*100)/total_kb
     * → anything under ~8 KB always rounded to 1%.
     */
    uint32_t total_bytes = (uint32_t)total_kb * 1024;
    uint8_t  used_pct    = (total_bytes > 0)
                         ? (uint8_t)((ts.stack_used * 100U) / total_bytes)
                         : 0;
    uint16_t used_kb     = (uint16_t)(ts.stack_used / 1024);
    uint16_t free_kb     = (total_kb > used_kb) ? (total_kb - used_kb) : 0;
    p->ram_free_kb       = htons(free_kb);
    p->ram_used_percent  = used_pct;

    p->udp_rx_count  = htonl((uint32_t)atomic_get(&udp_rx_count));
    p->udp_rx_errors = htonl((uint32_t)atomic_get(&udp_rx_errors));

    size_t crc_len = sizeof(*p) - sizeof(p->crc32);
    p->crc32 = htonl(crc32_calc(p, crc_len));
}

static int send_telemetry(const struct sockaddr_in *dest)
{
    telemetry_packet_t pkt;
    build_telemetry(&pkt);

    int ret = zsock_sendto(telem_sock, &pkt, sizeof(pkt), 0,
                           (struct sockaddr *)dest, sizeof(*dest));
    if (ret < 0) {
        LOG_ERR("telemetry send: %d", ret);
        return ret;
    }
    telemetry_seq++;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Monitor thread                                                     */
/* ------------------------------------------------------------------ */

static void monitor_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

    /* Wait for the network stack */
    while (!network_ready) {
        k_sleep(K_MSEC(100));
    }

    /* Open a UDP socket for telemetry — retry on failure */
    while (telem_sock < 0) {
        telem_sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (telem_sock < 0) {
            LOG_ERR("telemetry socket: %d  (retry in 1 s)", telem_sock);
            k_sleep(K_MSEC(1000));
        }
    }

    /* Build destination address once */
    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port   = htons(TELEMETRY_UDP_PORT),
    };
    zsock_inet_pton(AF_INET, TOPSIDE_IP, &dest.sin_addr);

    int on = 1;
    zsock_setsockopt(telem_sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));

    LOG_INF("Resource monitor started (port %d)", TELEMETRY_UDP_PORT);

    uint32_t diag_counter = 0;

    while (1) {
        update_cpu_usage();
        send_telemetry(&dest);

        /* Print raw diagnostics every DIAG_LOG_EVERY_N seconds so the
         * serial console can confirm the values are actually changing. */
        if (++diag_counter >= DIAG_LOG_EVERY_N) {
            diag_counter = 0;
            struct thread_stats ts = get_thread_stats();
#ifdef CONFIG_SRAM_SIZE
            uint32_t total_b = (uint32_t)CONFIG_SRAM_SIZE * 1024;
#else
            uint32_t total_b = 512U * 1024;
#endif
            LOG_INF("[diag] cpu=%u%%  stack_used=%u B /%u B  "
                    "threads=%u  udp_rx=%u  seq=%u",
                    cpu_usage_percent,
                    ts.stack_used, total_b,
                    ts.count,
                    (uint32_t)atomic_get(&udp_rx_count),
                    telemetry_seq);
        }

        k_sleep(K_MSEC(TELEMETRY_INTERVAL_MS));
    }
}

void resource_monitor_start(void)
{
    k_tid_t tid = k_thread_create(&monitor_thread_data,
                                   monitor_stack,
                                   K_THREAD_STACK_SIZEOF(monitor_stack),
                                   monitor_thread,
                                   NULL, NULL, NULL,
                                   MONITOR_PRIORITY, 0, K_NO_WAIT);
    if (tid) {
        k_thread_name_set(tid, "res_monitor");
    }
}

void resource_monitor_inc_udp_rx(void)    { atomic_inc(&udp_rx_count);  }
void resource_monitor_inc_udp_errors(void){ atomic_inc(&udp_rx_errors); }
