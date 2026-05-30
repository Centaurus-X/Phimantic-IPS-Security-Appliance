#include "phimantec/phim_eve.h"
#include "phimantec/phim_string.h"
#include "phimantec/phim_time.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define PHIM_EVE_LINE_MAX 262144u
#define PHIM_EVE_INDEX_SIZE 4096u
#define PHIM_EVE_ALERT_TTL_MS 120000ull

typedef struct phim_eve_alert_entry_s {
    bool used;
    bool alert;
    char flow_id[PHIM_MAX_FLOW_ID];
    uint32_t sid;
    char msg[PHIM_MAX_RULE_MSG];
    uint64_t seen_ms;
} phim_eve_alert_entry_t;

typedef struct phim_eve_index_s {
    bool initialized;
    char path[512];
    dev_t dev;
    ino_t ino;
    off_t offset;
    phim_eve_alert_entry_t entries[PHIM_EVE_INDEX_SIZE];
} phim_eve_index_t;

static pthread_mutex_t g_eve_index_lock = PTHREAD_MUTEX_INITIALIZER;
static phim_eve_index_t g_eve_index;

static uint32_t phim_line_extract_sid(const char *line)
{
    const char *p = phim_strcasestr_ascii(line, "\"signature_id\"");
    if (p == NULL) {
        p = phim_strcasestr_ascii(line, "\"sid\"");
    }
    if (p == NULL) {
        return 0u;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return 0u;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return (uint32_t)strtoul(p, NULL, 10);
}

static int phim_line_extract_string(const char *line, const char *key, char *dst, size_t dst_len)
{
    char needle[128];
    const char *p;
    size_t i = 0u;
    if (dst == NULL || dst_len == 0u) {
        return PHIM_ERR;
    }
    dst[0] = '\0';
    (void)snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = phim_strcasestr_ascii(line, needle);
    if (p == NULL) {
        return PHIM_ERR;
    }
    p = strchr(p + strlen(needle), ':');
    if (p == NULL) {
        return PHIM_ERR;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
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
    return i > 0u ? PHIM_OK : PHIM_ERR;
}

static int phim_is_flow_id_hex(int c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static int phim_copy_generated_flow_id_after_marker(const char *p, char *dst, size_t dst_len)
{
    static const size_t group_lengths[] = {8u, 4u, 4u, 4u, 12u};
    char tmp[PHIM_MAX_FLOW_ID];
    size_t out = 0u;
    size_t group;
    size_t i;

    if (p == NULL || dst == NULL || dst_len == 0u) {
        return PHIM_ERR;
    }
    dst[0] = '\0';
    while (*p == ' ' || *p == '\t' || *p == '"') {
        p++;
    }

    /* Production Flow-IDs are generated as UUID-like lowercase hex values:
     *   8-4-4-4-12
     * per-request and response-segment subflows may append numeric suffixes,
     * e.g. -0001-90000.
     *
     * Do not copy arbitrary alpha-numeric characters until the next slash.
     * Suricata payload_printable can collapse CR/LF between HTTP header lines,
     * so the value may appear as
     *   X-Phim-Flow-Id: <uuid>TP/1.1
     * where TP/1.1 is the tail of the next HTTP/1.1 line. A strict shape
     * parser deterministically extracts only the generated Flow-ID.
     */
    for (group = 0u; group < sizeof(group_lengths) / sizeof(group_lengths[0]); group++) {
        for (i = 0u; i < group_lengths[group]; i++) {
            if (!phim_is_flow_id_hex((unsigned char)*p) || out + 1u >= sizeof(tmp)) {
                return PHIM_ERR;
            }
            tmp[out++] = *p++;
        }
        if (group + 1u < sizeof(group_lengths) / sizeof(group_lengths[0])) {
            if (*p != '-' || out + 1u >= sizeof(tmp)) {
                return PHIM_ERR;
            }
            tmp[out++] = *p++;
        }
    }

    while (*p == '-') {
        const char *suffix = p + 1;
        size_t suffix_len = 0u;
        while (suffix[suffix_len] >= '0' && suffix[suffix_len] <= '9' && suffix_len < 8u) {
            suffix_len++;
        }
        if (suffix_len < 4u) {
            break;
        }
        if (out + suffix_len + 2u > sizeof(tmp)) {
            return PHIM_ERR;
        }
        tmp[out++] = *p++;
        for (i = 0u; i < suffix_len; i++) {
            tmp[out++] = *p++;
        }
    }

    if (out + 1u > dst_len) {
        return PHIM_ERR;
    }
    tmp[out] = '\0';
    memcpy(dst, tmp, out + 1u);
    return PHIM_OK;
}

static int phim_line_extract_flow_id_from_lane_path(const char *line, const char *marker,
                                                    char *dst, size_t dst_len)
{
    const char *p;

    p = phim_strcasestr_ascii(line, marker);
    if (p == NULL) {
        return PHIM_ERR;
    }
    p += strlen(marker);

    /* URL format: /phimantec/lane/<direction>/<flow_id>
     * Skip the direction segment first. The same marker can occur in the
     * HTTP URL field or in payload_printable. This is intentionally redundant
     * to survive TCP segmentation where the Flow-ID header and the body match
     * appear in different packets.
     */
    while (*p != '\0' && *p != '/' && *p != '\\') {
        p++;
    }
    if (*p == '\0') {
        return PHIM_ERR;
    }
    if (*p == '\\' && p[1] == '/') {
        p += 2;
    } else {
        p++;
    }
    return phim_copy_generated_flow_id_after_marker(p, dst, dst_len);
}

static int phim_line_extract_flow_id(const char *line, char *dst, size_t dst_len)
{
    static const char marker_header[] = "X-Phim-Flow-Id:";
    static const char marker_url[] = "/phimantec/lane/";
    static const char marker_json_escaped_url[] = "\\/phimantec\\/lane\\/";
    const char *p;

    if (line == NULL || dst == NULL || dst_len == 0u) {
        return PHIM_ERR;
    }
    dst[0] = '\0';

    if (phim_line_extract_flow_id_from_lane_path(line, marker_url, dst, dst_len) == PHIM_OK) {
        return PHIM_OK;
    }
    if (phim_line_extract_flow_id_from_lane_path(line, marker_json_escaped_url, dst, dst_len) == PHIM_OK) {
        return PHIM_OK;
    }

    p = phim_strcasestr_ascii(line, marker_header);
    if (p != NULL) {
        p += strlen(marker_header);
        if (phim_copy_generated_flow_id_after_marker(p, dst, dst_len) == PHIM_OK) {
            return PHIM_OK;
        }
    }

    return PHIM_ERR;
}

static bool phim_line_is_alert(const char *line)
{
    if (line == NULL) {
        return false;
    }
    return phim_strcasestr_ascii(line, "\"event_type\":\"alert\"") != NULL ||
           phim_strcasestr_ascii(line, "\"alert\"") != NULL;
}

static bool phim_line_may_contain_lane_flow(const char *line)
{
    if (line == NULL) {
        return false;
    }
    return phim_strcasestr_ascii(line, "X-Phim-Flow-Id:") != NULL ||
           phim_strcasestr_ascii(line, "/phimantec/lane/") != NULL ||
           phim_strcasestr_ascii(line, "\\/phimantec\\/lane\\/") != NULL;
}

static bool phim_eve_entry_expired(uint64_t now_ms, uint64_t seen_ms)
{
    if (now_ms < seen_ms) {
        return false;
    }
    return now_ms - seen_ms > PHIM_EVE_ALERT_TTL_MS;
}

static void phim_eve_index_clear_locked(void)
{
    memset(&g_eve_index, 0, sizeof(g_eve_index));
}

static size_t phim_eve_index_victim_locked(uint64_t now_ms)
{
    size_t i;
    size_t victim = 0u;
    uint64_t oldest_ms = UINT64_MAX;

    for (i = 0u; i < PHIM_EVE_INDEX_SIZE; i++) {
        if (!g_eve_index.entries[i].used || phim_eve_entry_expired(now_ms, g_eve_index.entries[i].seen_ms)) {
            return i;
        }
        if (g_eve_index.entries[i].seen_ms < oldest_ms) {
            oldest_ms = g_eve_index.entries[i].seen_ms;
            victim = i;
        }
    }
    return victim;
}

static void phim_eve_index_upsert_locked(const char *flow_id, bool alert, uint32_t sid, const char *msg, uint64_t now_ms)
{
    size_t i;
    size_t slot;

    if (flow_id == NULL || flow_id[0] == '\0') {
        return;
    }
    for (i = 0u; i < PHIM_EVE_INDEX_SIZE; i++) {
        if (g_eve_index.entries[i].used && strcmp(g_eve_index.entries[i].flow_id, flow_id) == 0) {
            if (alert || !g_eve_index.entries[i].alert) {
                g_eve_index.entries[i].alert = alert;
                g_eve_index.entries[i].sid = alert ? sid : 0u;
                if (alert) {
                    (void)phim_copy_cstr(g_eve_index.entries[i].msg, sizeof(g_eve_index.entries[i].msg),
                                         msg != NULL && msg[0] != '\0' ? msg : "suricata-alert");
                } else {
                    g_eve_index.entries[i].msg[0] = '\0';
                }
            }
            g_eve_index.entries[i].seen_ms = now_ms;
            return;
        }
    }

    slot = phim_eve_index_victim_locked(now_ms);
    memset(&g_eve_index.entries[slot], 0, sizeof(g_eve_index.entries[slot]));
    g_eve_index.entries[slot].used = true;
    g_eve_index.entries[slot].alert = alert;
    (void)phim_copy_cstr(g_eve_index.entries[slot].flow_id, sizeof(g_eve_index.entries[slot].flow_id), flow_id);
    g_eve_index.entries[slot].sid = alert ? sid : 0u;
    if (alert) {
        (void)phim_copy_cstr(g_eve_index.entries[slot].msg, sizeof(g_eve_index.entries[slot].msg),
                             msg != NULL && msg[0] != '\0' ? msg : "suricata-alert");
    } else {
        g_eve_index.entries[slot].msg[0] = '\0';
    }
    g_eve_index.entries[slot].seen_ms = now_ms;
}

static int phim_eve_index_lookup_locked(const char *flow_id, phim_eve_flow_status_t *status)
{
    size_t i;
    uint64_t now_ms = phim_now_ms();

    if (status == NULL) {
        return PHIM_ERR;
    }

    for (i = 0u; i < PHIM_EVE_INDEX_SIZE; i++) {
        phim_eve_alert_entry_t *entry = &g_eve_index.entries[i];
        if (!entry->used) {
            continue;
        }
        if (phim_eve_entry_expired(now_ms, entry->seen_ms)) {
            memset(entry, 0, sizeof(*entry));
            continue;
        }
        if (strcmp(entry->flow_id, flow_id) == 0) {
            status->state = entry->alert ? PHIM_EVE_FLOW_ALERT : PHIM_EVE_FLOW_SEEN_NO_ALERT;
            status->sid = entry->sid;
            (void)phim_copy_cstr(status->msg, sizeof(status->msg), entry->msg);
            status->seen_ms = entry->seen_ms;
            return PHIM_OK;
        }
    }
    return PHIM_ERR;
}

static int phim_eve_index_reset_if_needed_locked(const char *path, const struct stat *st)
{
    if (path == NULL || st == NULL) {
        return PHIM_ERR;
    }
    if (!g_eve_index.initialized || strcmp(g_eve_index.path, path) != 0 ||
        g_eve_index.dev != st->st_dev || g_eve_index.ino != st->st_ino || st->st_size < g_eve_index.offset) {
        phim_eve_index_clear_locked();
        g_eve_index.initialized = true;
        (void)phim_copy_cstr(g_eve_index.path, sizeof(g_eve_index.path), path);
        g_eve_index.dev = st->st_dev;
        g_eve_index.ino = st->st_ino;
        g_eve_index.offset = 0;
    }
    return PHIM_OK;
}

static int phim_eve_index_refresh_locked(const char *path, bool *out_cursor_advanced)
{
    struct stat st;
    FILE *f;
    char line[PHIM_EVE_LINE_MAX];
    uint64_t now_ms;
    off_t next_offset;

    if (out_cursor_advanced != NULL) {
        *out_cursor_advanced = false;
    }
    if (path == NULL) {
        return PHIM_ERR;
    }
    if (stat(path, &st) != 0) {
        return PHIM_ERR;
    }
    if (phim_eve_index_reset_if_needed_locked(path, &st) != PHIM_OK) {
        return PHIM_ERR;
    }
    if (st.st_size == g_eve_index.offset) {
        return PHIM_OK;
    }

    f = fopen(path, "r");
    if (f == NULL) {
        return PHIM_ERR;
    }
    if (fseeko(f, g_eve_index.offset, SEEK_SET) != 0) {
        fclose(f);
        return PHIM_ERR;
    }
    now_ms = phim_now_ms();
    next_offset = g_eve_index.offset;
    while (fgets(line, sizeof(line), f) != NULL) {
        off_t line_end;
        size_t line_len;
        char flow_id[PHIM_MAX_FLOW_ID];
        uint32_t line_sid;
        char line_msg[PHIM_MAX_RULE_MSG];
        bool is_alert;

        line_len = strlen(line);
        line_end = ftello(f);
        if (line_len == 0u) {
            continue;
        }

        /* Do not advance the EVE cursor over an unterminated writer-side line.
         * Suricata can append long JSON alert events while inspectord is already
         * polling. Treating a partial EOF line as final creates an allow-before-
         * alert TOCTOU. Keeping the cursor at the start of that line makes the
         * next poll re-read the completed event deterministically.
         */
        if (line[line_len - 1u] != '\n' && feof(f)) {
            break;
        }

        next_offset = line_end;

        if (!phim_line_may_contain_lane_flow(line)) {
            continue;
        }

        if (phim_line_extract_flow_id(line, flow_id, sizeof(flow_id)) != PHIM_OK) {
            continue;
        }
        is_alert = phim_line_is_alert(line);
        if (is_alert) {
            line_sid = phim_line_extract_sid(line);
            if (phim_line_extract_string(line, "signature", line_msg, sizeof(line_msg)) != PHIM_OK &&
                phim_line_extract_string(line, "msg", line_msg, sizeof(line_msg)) != PHIM_OK) {
                (void)phim_copy_cstr(line_msg, sizeof(line_msg), "suricata-alert");
            }
        } else {
            line_sid = 0u;
            line_msg[0] = '\0';
        }
        phim_eve_index_upsert_locked(flow_id, is_alert, line_sid, line_msg, now_ms);
    }
    if (next_offset > g_eve_index.offset) {
        g_eve_index.offset = next_offset;
        if (out_cursor_advanced != NULL) {
            *out_cursor_advanced = true;
        }
    }
    fclose(f);
    return PHIM_OK;
}

int phim_eve_get_flow_status(const char *path, const char *flow_id,
                             phim_eve_flow_status_t *status)
{
    bool cursor_advanced = false;

    if (status == NULL) {
        return PHIM_ERR;
    }
    memset(status, 0, sizeof(*status));
    status->state = PHIM_EVE_FLOW_UNAVAILABLE;
    if (path == NULL || flow_id == NULL || flow_id[0] == '\0') {
        return PHIM_ERR;
    }

    pthread_mutex_lock(&g_eve_index_lock);
    if (phim_eve_index_refresh_locked(path, &cursor_advanced) != PHIM_OK) {
        pthread_mutex_unlock(&g_eve_index_lock);
        return PHIM_ERR;
    }
    status->cursor_advanced = cursor_advanced;
    status->state = PHIM_EVE_FLOW_NOT_SEEN;
    (void)phim_eve_index_lookup_locked(flow_id, status);
    status->cursor_advanced = status->cursor_advanced || cursor_advanced;
    pthread_mutex_unlock(&g_eve_index_lock);
    return PHIM_OK;
}

int phim_eve_find_alert_for_flow(const char *path, const char *flow_id,
                                 uint32_t *sid, char *msg, size_t msg_len)
{
    phim_eve_flow_status_t status;

    if (sid != NULL) {
        *sid = 0u;
    }
    if (msg != NULL && msg_len > 0u) {
        msg[0] = '\0';
    }
    if (path == NULL || flow_id == NULL || flow_id[0] == '\0') {
        return PHIM_ERR;
    }

    if (phim_eve_get_flow_status(path, flow_id, &status) != PHIM_OK ||
        status.state != PHIM_EVE_FLOW_ALERT) {
        return PHIM_ERR;
    }
    if (sid != NULL) {
        *sid = status.sid;
    }
    if (msg != NULL && msg_len > 0u) {
        (void)phim_copy_cstr(msg, msg_len, status.msg);
    }
    return PHIM_OK;
}
