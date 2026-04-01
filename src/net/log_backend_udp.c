/*
 * UDP Log Backend — forwards all Zephyr log messages to topside as plain text.
 *
 * Runs inside the existing Zephyr deferred-log processing thread (no extra thread).
 * Activated from main() after network_init() via log_backend_udp_topside_start().
 *
 * IMPORTANT: This file must NOT use LOG_INF/LOG_ERR/LOG_WRN/LOG_DBG — doing so
 * would cause the logging subsystem to recurse back into this backend.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_backend_std.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/logging/log_core.h>
#include <zephyr/net/socket.h>

#include "net.h"

static int  log_sock = -1;
static bool in_panic = false;
static struct sockaddr_in log_dest;
static uint8_t log_out_buf[192];
static uint32_t current_format = LOG_OUTPUT_TEXT;

/* Output callback — sends formatted text as a single UDP datagram */
static int log_udp_out(uint8_t *data, size_t length, void *ctx)
{
    ARG_UNUSED(ctx);

    if (log_sock < 0 || in_panic || length == 0) {
        return (int)length;
    }

    zsock_sendto(log_sock, data, length, ZSOCK_MSG_DONTWAIT,
                 (struct sockaddr *)&log_dest, sizeof(log_dest));

    return (int)length;
}

LOG_OUTPUT_DEFINE(log_output_udp, log_udp_out, log_out_buf, sizeof(log_out_buf));

static void udp_log_process(const struct log_backend *const backend,
                            union log_msg_generic *msg)
{
    ARG_UNUSED(backend);

    if (in_panic || log_sock < 0) {
        return;
    }

    uint32_t flags = log_backend_std_get_flags();
    log_format_func_t fmt = log_format_func_t_get(current_format);
    fmt(&log_output_udp, &msg->log, flags);
}

static void udp_log_init(const struct log_backend *const backend)
{
    ARG_UNUSED(backend);
    /* Network not ready at logger init time — stay inactive until
     * log_backend_udp_topside_start() is called from main(). */
}

static void udp_log_panic(const struct log_backend *const backend)
{
    ARG_UNUSED(backend);
    in_panic = true;
}

static void udp_log_dropped(const struct log_backend *const backend,
                            uint32_t cnt)
{
    ARG_UNUSED(backend);
    if (log_sock >= 0 && !in_panic) {
        log_backend_std_dropped(&log_output_udp, cnt);
    }
}

static int udp_log_format_set(const struct log_backend *const backend,
                              uint32_t log_type)
{
    ARG_UNUSED(backend);
    current_format = log_type;
    return 0;
}

static const struct log_backend_api log_backend_udp_api = {
    .process    = udp_log_process,
    .panic      = udp_log_panic,
    .init       = udp_log_init,
    .dropped    = udp_log_dropped,
    .format_set = udp_log_format_set,
};

LOG_BACKEND_DEFINE(log_backend_udp_topside, log_backend_udp_api, false);

/* Called from main() after network_init() to open the socket and activate */
void log_backend_udp_topside_start(void)
{
    log_sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (log_sock < 0) {
        return;
    }

    int on = 1;
    zsock_setsockopt(log_sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));

    log_dest.sin_family = AF_INET;
    log_dest.sin_port   = htons(LOG_UDP_PORT);
    zsock_inet_pton(AF_INET, TOPSIDE_IP, &log_dest.sin_addr);

    log_backend_activate(&log_backend_udp_topside, NULL);
}
