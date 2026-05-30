#include "phimantec/phim_config.h"
#include "phimantec/phim_eve.h"
#include "phimantec/phim_flowbus.h"
#include "phimantec/phim_log.h"
#include "phimantec/phim_mem.h"
#include "phimantec/phim_socket.h"
#include "phimantec/phim_string.h"
#include "phimantec/phim_time.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>


typedef struct inspect_arg_s {
    int fd;
    phim_config_t cfg;
} inspect_arg_t;

static void usage(const char *argv0)
{
    (void)fprintf(stderr, "Usage: %s [-c config/phimantec-suricata-node.conf] [-v]\n", argv0);
}

static int json_get_string(const unsigned char *json, size_t len, const char *key, char *dst, size_t dst_len)
{
    char needle[128];
    const char *p;
    size_t i = 0u;
    (void)len;
    if (dst == NULL || dst_len == 0u) {
        return PHIM_ERR;
    }
    dst[0] = '\0';
    (void)snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = phim_strcasestr_ascii((const char *)json, needle);
    if (p == NULL) {
        return PHIM_ERR;
    }
    p = strchr(p + strlen(needle), ':');
    if (p == NULL) {
        return PHIM_ERR;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != '"') {
        return PHIM_ERR;
    }
    p++;
    while (*p != '\0' && *p != '"') {
        if (*p == '\\' && p[1] != '\0') {
            p++;
        }
        if (i + 1u >= dst_len) {
            return PHIM_ERR;
        }
        dst[i++] = *p++;
    }
    dst[i] = '\0';
    return PHIM_OK;
}

static uint32_t phim_inspectord_verdict_timeout_ms(const phim_config_t *cfg)
{
    uint32_t timeout_ms;

    timeout_ms = cfg != NULL ? cfg->verdict_timeout_ms : 0u;
    if (timeout_ms < 250u) {
        timeout_ms = 250u;
    }
    if (timeout_ms > 5000u) {
        timeout_ms = 5000u;
    }
    return timeout_ms;
}

static uint32_t phim_inspectord_no_alert_grace_ms(const phim_config_t *cfg, uint32_t timeout_ms)
{
    uint32_t grace_ms;

    grace_ms = cfg != NULL ? cfg->no_alert_grace_ms : 0u;
    if (grace_ms < 25u) {
        grace_ms = 25u;
    }
    if (grace_ms > 1000u) {
        grace_ms = 1000u;
    }
    if (grace_ms >= timeout_ms) {
        grace_ms = timeout_ms / 2u;
        if (grace_ms < 25u) {
            grace_ms = 25u;
        }
    }
    return grace_ms;
}

static uint32_t phim_min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static phim_eve_flow_state_t wait_for_suricata_flow_status(const phim_config_t *cfg, const char *flow_id,
                                                           uint32_t *sid, char *msg, size_t msg_len)
{
    uint64_t deadline_ms;
    uint64_t no_alert_since_ms = 0u;
    uint32_t timeout_ms;
    uint32_t grace_ms;
    uint32_t sleep_ms = 10u;
    bool saw_no_alert = false;
    phim_eve_flow_status_t status;

    if (sid != NULL) {
        *sid = 0u;
    }
    if (msg != NULL && msg_len > 0u) {
        msg[0] = '\0';
    }
    if (cfg == NULL || flow_id == NULL || flow_id[0] == '\0') {
        return PHIM_EVE_FLOW_UNAVAILABLE;
    }

    timeout_ms = phim_inspectord_verdict_timeout_ms(cfg);
    grace_ms = phim_inspectord_no_alert_grace_ms(cfg, timeout_ms);
    deadline_ms = phim_now_ms() + timeout_ms;
    for (;;) {
        uint64_t now_ms;
        uint32_t delay_ms;

        if (phim_eve_get_flow_status(cfg->eve_path, flow_id, &status) != PHIM_OK) {
            return PHIM_EVE_FLOW_UNAVAILABLE;
        }
        now_ms = phim_now_ms();
        if (status.state == PHIM_EVE_FLOW_ALERT) {
            if (sid != NULL) {
                *sid = status.sid;
            }
            if (msg != NULL && msg_len > 0u) {
                (void)phim_copy_cstr(msg, msg_len, status.msg);
            }
            return PHIM_EVE_FLOW_ALERT;
        }
        if (status.state == PHIM_EVE_FLOW_SEEN_NO_ALERT) {
            saw_no_alert = true;
            if (no_alert_since_ms == 0u) {
                no_alert_since_ms = now_ms;
            }
            if (now_ms >= no_alert_since_ms && now_ms - no_alert_since_ms >= grace_ms) {
                return PHIM_EVE_FLOW_SEEN_NO_ALERT;
            }
        }
        if (now_ms >= deadline_ms) {
            return saw_no_alert ? PHIM_EVE_FLOW_SEEN_NO_ALERT : PHIM_EVE_FLOW_NOT_SEEN;
        }

        delay_ms = sleep_ms;
        if (deadline_ms > now_ms) {
            delay_ms = phim_min_u32(delay_ms, (uint32_t)(deadline_ms - now_ms));
        }
        if (saw_no_alert && no_alert_since_ms > 0u && no_alert_since_ms + grace_ms > now_ms) {
            delay_ms = phim_min_u32(delay_ms, (uint32_t)(no_alert_since_ms + grace_ms - now_ms));
        }
        if (delay_ms == 0u) {
            delay_ms = 1u;
        }
        phim_sleep_ms(delay_ms);
        if (sleep_ms < 20u) {
            sleep_ms += 5u;
        } else if (sleep_ms < 80u) {
            sleep_ms += 15u;
        }
    }
}

static void set_suricata_block_verdict(phim_verdict_t *verdict, uint32_t sid, const char *msg)
{
    verdict->action = PHIM_VERDICT_BLOCK;
    (void)phim_copy_cstr(verdict->reason, sizeof(verdict->reason), "suricata-alert");
    verdict->sid = sid != 0u ? sid : 900100u;
    (void)phim_copy_cstr(verdict->rule_msg, sizeof(verdict->rule_msg),
                         msg != NULL && msg[0] != '\0' ? msg : "suricata-alert");
}

static void set_suricata_allow_verdict(phim_verdict_t *verdict)
{
    verdict->action = PHIM_VERDICT_ALLOW;
    (void)phim_copy_cstr(verdict->reason, sizeof(verdict->reason), "suricata-no-alert-final");
    verdict->sid = 0u;
    verdict->rule_msg[0] = '\0';
}

static void set_suricata_pending_verdict(phim_verdict_t *verdict, const char *reason)
{
    verdict->action = PHIM_VERDICT_PENDING;
    (void)phim_copy_cstr(verdict->reason, sizeof(verdict->reason),
                         reason != NULL && reason[0] != '\0' ? reason : "suricata-pending");
    verdict->sid = 0u;
    verdict->rule_msg[0] = '\0';
}

static void set_suricata_error_verdict(phim_verdict_t *verdict, const char *reason)
{
    verdict->action = PHIM_VERDICT_ERROR;
    (void)phim_copy_cstr(verdict->reason, sizeof(verdict->reason),
                         reason != NULL && reason[0] != '\0' ? reason : "suricata-unavailable");
    verdict->sid = 0u;
    verdict->rule_msg[0] = '\0';
}

static const char *phim_verdict_action_name(phim_verdict_action_t action)
{
    if (action == PHIM_VERDICT_BLOCK) {
        return "block";
    }
    if (action == PHIM_VERDICT_ALLOW) {
        return "allow";
    }
    if (action == PHIM_VERDICT_PENDING) {
        return "pending";
    }
    return "error";
}

static void decide_verdict(const phim_config_t *cfg, const unsigned char *payload, size_t payload_len,
                           phim_verdict_t *verdict, char *flow_id, size_t flow_id_len)
{
    uint32_t sid = 0u;
    char msg[PHIM_MAX_RULE_MSG];
    uint64_t start = phim_now_us();
    memset(verdict, 0, sizeof(*verdict));
    msg[0] = '\0';
    if (json_get_string(payload, payload_len, "flow_id", flow_id, flow_id_len) != PHIM_OK) {
        (void)phim_copy_cstr(flow_id, flow_id_len, "unknown");
    }

    switch (wait_for_suricata_flow_status(cfg, flow_id, &sid, msg, sizeof(msg))) {
    case PHIM_EVE_FLOW_ALERT:
        set_suricata_block_verdict(verdict, sid, msg);
        break;
    case PHIM_EVE_FLOW_SEEN_NO_ALERT:
        set_suricata_allow_verdict(verdict);
        break;
    case PHIM_EVE_FLOW_NOT_SEEN:
        set_suricata_pending_verdict(verdict, "suricata-pending-no-eve-flow");
        break;
    case PHIM_EVE_FLOW_UNAVAILABLE:
    default:
        set_suricata_error_verdict(verdict, "suricata-eve-unavailable");
        break;
    }
    verdict->latency_us = phim_now_us() - start;
}

static void *inspect_client_thread(void *arg)
{
    inspect_arg_t *ia = (inspect_arg_t *)arg;
    int fd = ia->fd;
    phim_config_t cfg = ia->cfg;
    uint64_t last_seq = 0u;
    free(ia);
    for (;;) {
        phim_flowbus_frame_t frame;
        phim_verdict_t verdict;
        char flow_id[PHIM_MAX_FLOW_ID];
        char response[2048];
        phim_flowbus_frame_init(&frame);
        if (phim_flowbus_read_frame(fd, cfg.flowbus_token, cfg.flowbus_timeout_ms, &frame) != PHIM_OK) {
            break;
        }
        if (frame.seq <= last_seq) {
            PHIM_LOGW("replay or sequence rollback detected seq=%llu last=%llu",
                      (unsigned long long)frame.seq, (unsigned long long)last_seq);
            phim_flowbus_frame_free(&frame);
            break;
        }
        last_seq = frame.seq;
        if (frame.type == PHIM_FB_VERDICT_REQUEST) {
            decide_verdict(&cfg, frame.payload, frame.payload_len, &verdict, flow_id, sizeof(flow_id));
            if (phim_verdict_to_json(&verdict, flow_id, response, sizeof(response)) == PHIM_OK) {
                (void)phim_flowbus_write_frame(fd, PHIM_FB_VERDICT_RESPONSE, frame.seq, cfg.flowbus_token,
                                               (const unsigned char *)response, (uint32_t)strlen(response));
                PHIM_LOGI("verdict flow=%s action=%s reason=%s", flow_id,
                          phim_verdict_action_name(verdict.action), verdict.reason);
            }
        } else if (frame.type == PHIM_FB_HEARTBEAT) {
            (void)phim_flowbus_write_frame(fd, PHIM_FB_HEARTBEAT, frame.seq, cfg.flowbus_token,
                                           (const unsigned char *)"{}", 2u);
        }
        phim_flowbus_frame_free(&frame);
    }
    phim_close_fd(&fd);
    return NULL;
}

int main(int argc, char **argv)
{
    const char *config_path = "config/phimantec-suricata-node.conf";
    phim_config_t cfg;
    int fd;
    int i;
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        return 1;
    }
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0) {
            phim_log_set_level(PHIM_LOG_DEBUG);
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (phim_config_load(&cfg, config_path) != PHIM_OK) {
        return 1;
    }
    fd = phim_tcp_listen(cfg.flowbus_host, cfg.flowbus_port, 1024);
    if (fd < 0) {
        PHIM_LOGE("inspectord listen failed on %s:%u", cfg.flowbus_host, (unsigned int)cfg.flowbus_port);
        return 1;
    }
    PHIM_LOGI("phimantec-inspectord listening on %s:%u", cfg.flowbus_host, (unsigned int)cfg.flowbus_port);
    for (;;) {
        int cfd = accept(fd, NULL, NULL);
        pthread_t tid;
        inspect_arg_t *ia;
        if (cfd < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        ia = phim_xcalloc(1u, sizeof(*ia));
        ia->fd = cfd;
        ia->cfg = cfg;
        if (pthread_create(&tid, NULL, inspect_client_thread, ia) == 0) {
            (void)pthread_detach(tid);
        } else {
            phim_close_fd(&cfd);
            free(ia);
        }
    }
    phim_close_fd(&fd);
    return 1;
}
