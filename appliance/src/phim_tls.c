#include "phimantec/phim_tls.h"
#include "phimantec/phim_log.h"
#include "phimantec/phim_string.h"
#include "phimantec/phim_time.h"

#include <errno.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define PHIM_TLS_DOWNSTREAM_CTX_CACHE_SIZE 1024u
#define PHIM_TLS_UPSTREAM_CTX_CACHE_SIZE 256u
#define PHIM_TLS_CTX_CACHE_TTL_MS 604800000ull
#define PHIM_TLS_SESSION_CACHE_SIZE 4096L
#define PHIM_TLS_SESSION_TIMEOUT_SECONDS 300L

#ifndef PHIM_SSL_TRANSIENT_RETRY_MAX
#define PHIM_SSL_TRANSIENT_RETRY_MAX 16u
#endif

typedef struct phim_downstream_ctx_cache_entry_s {
    bool used;
    char hostname[PHIM_MAX_HOST + 1u];
    unsigned char ca_fingerprint[EVP_MAX_MD_SIZE];
    unsigned int ca_fingerprint_len;
    uint64_t inserted_ms;
    uint64_t last_used_ms;
    SSL_CTX *ctx;
} phim_downstream_ctx_cache_entry_t;

typedef struct phim_upstream_ctx_cache_entry_s {
    bool used;
    bool verify_peer;
    char hostname[PHIM_MAX_HOST + 1u];
    uint64_t inserted_ms;
    uint64_t last_used_ms;
    SSL_CTX *ctx;
} phim_upstream_ctx_cache_entry_t;

static pthread_mutex_t g_downstream_ctx_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static phim_downstream_ctx_cache_entry_t g_downstream_ctx_cache[PHIM_TLS_DOWNSTREAM_CTX_CACHE_SIZE];
static pthread_mutex_t g_upstream_ctx_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static phim_upstream_ctx_cache_entry_t g_upstream_ctx_cache[PHIM_TLS_UPSTREAM_CTX_CACHE_SIZE];

int phim_tls_global_init(void)
{
    if (OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL) != 1) {
        return PHIM_ERR;
    }
    return PHIM_OK;
}

static int phim_normalize_hostname(const char *hostname, char *dst, size_t dst_len)
{
    size_t i;
    size_t len;

    if (hostname == NULL || dst == NULL || dst_len == 0u) {
        return PHIM_ERR;
    }
    len = strlen(hostname);
    if (len == 0u || len >= dst_len) {
        return PHIM_ERR;
    }
    for (i = 0u; i < len; i++) {
        dst[i] = (char)phim_ascii_tolower((unsigned char)hostname[i]);
    }
    dst[len] = '\0';
    return PHIM_OK;
}

static int phim_ca_fingerprint_sha256(const phim_ca_t *ca, unsigned char *dst, unsigned int *dst_len)
{
    if (ca == NULL || ca->cert == NULL || dst == NULL || dst_len == NULL) {
        return PHIM_ERR;
    }
    if (X509_digest(ca->cert, EVP_sha256(), dst, dst_len) != 1) {
        return PHIM_ERR;
    }
    return PHIM_OK;
}

static bool phim_tls_cache_entry_expired(uint64_t now_ms, uint64_t inserted_ms)
{
    if (now_ms < inserted_ms) {
        return false;
    }
    return now_ms - inserted_ms > PHIM_TLS_CTX_CACHE_TTL_MS;
}

static void phim_downstream_cache_entry_clear(phim_downstream_ctx_cache_entry_t *entry)
{
    if (entry == NULL) {
        return;
    }
    SSL_CTX_free(entry->ctx);
    memset(entry, 0, sizeof(*entry));
}

static void phim_upstream_cache_entry_clear(phim_upstream_ctx_cache_entry_t *entry)
{
    if (entry == NULL) {
        return;
    }
    SSL_CTX_free(entry->ctx);
    memset(entry, 0, sizeof(*entry));
}

static bool phim_ca_fingerprint_matches(const phim_downstream_ctx_cache_entry_t *entry,
                                        const unsigned char *fingerprint, unsigned int fingerprint_len)
{
    if (entry == NULL || fingerprint == NULL) {
        return false;
    }
    if (entry->ca_fingerprint_len != fingerprint_len) {
        return false;
    }
    return memcmp(entry->ca_fingerprint, fingerprint, fingerprint_len) == 0;
}

static SSL_CTX *phim_downstream_cache_lookup_locked(const char *hostname,
                                                    const unsigned char *fingerprint,
                                                    unsigned int fingerprint_len,
                                                    uint64_t now_ms)
{
    size_t i;
    for (i = 0u; i < PHIM_TLS_DOWNSTREAM_CTX_CACHE_SIZE; i++) {
        phim_downstream_ctx_cache_entry_t *entry = &g_downstream_ctx_cache[i];
        if (!entry->used) {
            continue;
        }
        if (phim_tls_cache_entry_expired(now_ms, entry->inserted_ms)) {
            phim_downstream_cache_entry_clear(entry);
            continue;
        }
        if (strcmp(entry->hostname, hostname) == 0 &&
            phim_ca_fingerprint_matches(entry, fingerprint, fingerprint_len)) {
            if (SSL_CTX_up_ref(entry->ctx) != 1) {
                return NULL;
            }
            entry->last_used_ms = now_ms;
            return entry->ctx;
        }
    }
    return NULL;
}

static size_t phim_downstream_cache_victim_locked(void)
{
    size_t i;
    size_t victim = 0u;
    uint64_t oldest_ms = UINT64_MAX;
    for (i = 0u; i < PHIM_TLS_DOWNSTREAM_CTX_CACHE_SIZE; i++) {
        if (!g_downstream_ctx_cache[i].used) {
            return i;
        }
        if (g_downstream_ctx_cache[i].last_used_ms < oldest_ms) {
            oldest_ms = g_downstream_ctx_cache[i].last_used_ms;
            victim = i;
        }
    }
    return victim;
}

static int phim_downstream_cache_store_locked(const char *hostname,
                                              const unsigned char *fingerprint,
                                              unsigned int fingerprint_len,
                                              SSL_CTX *ctx, uint64_t now_ms)
{
    size_t victim;
    phim_downstream_ctx_cache_entry_t *entry;

    if (hostname == NULL || fingerprint == NULL || ctx == NULL || fingerprint_len > EVP_MAX_MD_SIZE) {
        return PHIM_ERR;
    }
    victim = phim_downstream_cache_victim_locked();
    entry = &g_downstream_ctx_cache[victim];
    phim_downstream_cache_entry_clear(entry);
    if (SSL_CTX_up_ref(ctx) != 1) {
        return PHIM_ERR;
    }
    entry->used = true;
    (void)phim_copy_cstr(entry->hostname, sizeof(entry->hostname), hostname);
    memcpy(entry->ca_fingerprint, fingerprint, fingerprint_len);
    entry->ca_fingerprint_len = fingerprint_len;
    entry->inserted_ms = now_ms;
    entry->last_used_ms = now_ms;
    entry->ctx = ctx;
    return PHIM_OK;
}

static SSL_CTX *phim_tls_create_downstream_ctx_uncached(const phim_ca_t *ca, const char *hostname)
{
    SSL_CTX *ctx = NULL;
    X509 *cert = NULL;
    EVP_PKEY *key = NULL;
    if (phim_generate_leaf_cert(ca, hostname, &cert, &key) != PHIM_OK) {
        return NULL;
    }
    ctx = SSL_CTX_new(TLS_server_method());
    if (ctx == NULL) {
        goto fail;
    }
    if (SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) != 1) {
        goto fail;
    }
    SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION);
    SSL_CTX_set_alpn_select_cb(ctx, NULL, NULL);
    if (SSL_CTX_use_certificate(ctx, cert) != 1) {
        goto fail;
    }
    if (SSL_CTX_use_PrivateKey(ctx, key) != 1) {
        goto fail;
    }
    if (SSL_CTX_check_private_key(ctx) != 1) {
        goto fail;
    }
    X509_free(cert);
    EVP_PKEY_free(key);
    return ctx;

fail:
    PHIM_LOGE("failed to create downstream TLS context for %s", hostname);
    SSL_CTX_free(ctx);
    X509_free(cert);
    EVP_PKEY_free(key);
    return NULL;
}

static int phim_alpn_select_cb(SSL *ssl, const unsigned char **out, unsigned char *outlen,
                               const unsigned char *in, unsigned int inlen, void *arg)
{
    static const unsigned char h1[] = { 8u, 'h', 't', 't', 'p', '/', '1', '.', '1' };
    (void)ssl;
    (void)arg;
    if (SSL_select_next_proto((unsigned char **)out, outlen, h1, sizeof(h1), in, inlen) == OPENSSL_NPN_NEGOTIATED) {
        return SSL_TLSEXT_ERR_OK;
    }
    *out = h1 + 1u;
    *outlen = 8u;
    return SSL_TLSEXT_ERR_OK;
}

SSL_CTX *phim_tls_create_downstream_ctx(const phim_ca_t *ca, const char *hostname)
{
    char normalized_host[PHIM_MAX_HOST + 1u];
    unsigned char fingerprint[EVP_MAX_MD_SIZE];
    unsigned int fingerprint_len = 0u;
    SSL_CTX *ctx;
    uint64_t now_ms;

    if (phim_normalize_hostname(hostname, normalized_host, sizeof(normalized_host)) != PHIM_OK ||
        phim_ca_fingerprint_sha256(ca, fingerprint, &fingerprint_len) != PHIM_OK) {
        return NULL;
    }

    now_ms = phim_now_ms();
    pthread_mutex_lock(&g_downstream_ctx_cache_lock);
    ctx = phim_downstream_cache_lookup_locked(normalized_host, fingerprint, fingerprint_len, now_ms);
    pthread_mutex_unlock(&g_downstream_ctx_cache_lock);
    if (ctx != NULL) {
        SSL_CTX_set_alpn_select_cb(ctx, phim_alpn_select_cb, NULL);
        return ctx;
    }

    ctx = phim_tls_create_downstream_ctx_uncached(ca, normalized_host);
    if (ctx == NULL) {
        return NULL;
    }
    SSL_CTX_set_alpn_select_cb(ctx, phim_alpn_select_cb, NULL);

    now_ms = phim_now_ms();
    pthread_mutex_lock(&g_downstream_ctx_cache_lock);
    {
        SSL_CTX *cached = phim_downstream_cache_lookup_locked(normalized_host, fingerprint, fingerprint_len, now_ms);
        if (cached != NULL) {
            pthread_mutex_unlock(&g_downstream_ctx_cache_lock);
            SSL_CTX_free(ctx);
            SSL_CTX_set_alpn_select_cb(cached, phim_alpn_select_cb, NULL);
            return cached;
        }
    }
    if (phim_downstream_cache_store_locked(normalized_host, fingerprint, fingerprint_len, ctx, now_ms) != PHIM_OK) {
        PHIM_LOGW("downstream TLS context cache store failed for %s", normalized_host);
    } else {
        PHIM_LOGD("downstream TLS context cached for %s", normalized_host);
    }
    pthread_mutex_unlock(&g_downstream_ctx_cache_lock);
    return ctx;
}

static SSL_CTX *phim_upstream_cache_lookup_locked(const char *hostname, bool verify_peer, uint64_t now_ms)
{
    size_t i;
    for (i = 0u; i < PHIM_TLS_UPSTREAM_CTX_CACHE_SIZE; i++) {
        phim_upstream_ctx_cache_entry_t *entry = &g_upstream_ctx_cache[i];
        if (!entry->used) {
            continue;
        }
        if (phim_tls_cache_entry_expired(now_ms, entry->inserted_ms)) {
            phim_upstream_cache_entry_clear(entry);
            continue;
        }
        if (entry->verify_peer == verify_peer && strcmp(entry->hostname, hostname) == 0) {
            if (SSL_CTX_up_ref(entry->ctx) != 1) {
                return NULL;
            }
            entry->last_used_ms = now_ms;
            return entry->ctx;
        }
    }
    return NULL;
}

static size_t phim_upstream_cache_victim_locked(void)
{
    size_t i;
    size_t victim = 0u;
    uint64_t oldest_ms = UINT64_MAX;
    for (i = 0u; i < PHIM_TLS_UPSTREAM_CTX_CACHE_SIZE; i++) {
        if (!g_upstream_ctx_cache[i].used) {
            return i;
        }
        if (g_upstream_ctx_cache[i].last_used_ms < oldest_ms) {
            oldest_ms = g_upstream_ctx_cache[i].last_used_ms;
            victim = i;
        }
    }
    return victim;
}

static int phim_upstream_cache_store_locked(const char *hostname, bool verify_peer, SSL_CTX *ctx, uint64_t now_ms)
{
    size_t victim;
    phim_upstream_ctx_cache_entry_t *entry;

    if (hostname == NULL || ctx == NULL) {
        return PHIM_ERR;
    }
    victim = phim_upstream_cache_victim_locked();
    entry = &g_upstream_ctx_cache[victim];
    phim_upstream_cache_entry_clear(entry);
    if (SSL_CTX_up_ref(ctx) != 1) {
        return PHIM_ERR;
    }
    entry->used = true;
    entry->verify_peer = verify_peer;
    (void)phim_copy_cstr(entry->hostname, sizeof(entry->hostname), hostname);
    entry->inserted_ms = now_ms;
    entry->last_used_ms = now_ms;
    entry->ctx = ctx;
    return PHIM_OK;
}

static SSL_CTX *phim_tls_create_upstream_ctx_uncached(bool verify_peer)
{
    static const unsigned char alpn[] = { 8u, 'h', 't', 't', 'p', '/', '1', '.', '1' };
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (ctx == NULL) {
        return NULL;
    }
    if (SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) != 1) {
        SSL_CTX_free(ctx);
        return NULL;
    }
    SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION);
    (void)SSL_CTX_set_alpn_protos(ctx, alpn, sizeof(alpn));
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_CLIENT);
    (void)SSL_CTX_sess_set_cache_size(ctx, PHIM_TLS_SESSION_CACHE_SIZE);
    (void)SSL_CTX_set_timeout(ctx, PHIM_TLS_SESSION_TIMEOUT_SECONDS);
    if (verify_peer) {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
        if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
            SSL_CTX_free(ctx);
            return NULL;
        }
    } else {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    }
    return ctx;
}

SSL_CTX *phim_tls_create_upstream_ctx(bool verify_peer, const char *hostname)
{
    char normalized_host[PHIM_MAX_HOST + 1u];
    SSL_CTX *ctx;
    uint64_t now_ms;

    if (phim_normalize_hostname(hostname, normalized_host, sizeof(normalized_host)) != PHIM_OK) {
        return NULL;
    }

    now_ms = phim_now_ms();
    pthread_mutex_lock(&g_upstream_ctx_cache_lock);
    ctx = phim_upstream_cache_lookup_locked(normalized_host, verify_peer, now_ms);
    if (ctx != NULL) {
        pthread_mutex_unlock(&g_upstream_ctx_cache_lock);
        return ctx;
    }

    ctx = phim_tls_create_upstream_ctx_uncached(verify_peer);
    if (ctx == NULL) {
        pthread_mutex_unlock(&g_upstream_ctx_cache_lock);
        return NULL;
    }
    if (phim_upstream_cache_store_locked(normalized_host, verify_peer, ctx, now_ms) != PHIM_OK) {
        PHIM_LOGW("upstream TLS context cache store failed for %s", normalized_host);
    } else {
        PHIM_LOGD("upstream TLS context cached for %s", normalized_host);
    }
    pthread_mutex_unlock(&g_upstream_ctx_cache_lock);
    return ctx;
}

SSL *phim_tls_accept_client(int fd, SSL_CTX *ctx)
{
    SSL *ssl = SSL_new(ctx);
    if (ssl == NULL) {
        return NULL;
    }
    SSL_set_fd(ssl, fd);
    if (SSL_accept(ssl) != 1) {
        PHIM_LOGW("client TLS handshake failed: %lu", ERR_get_error());
        SSL_free(ssl);
        return NULL;
    }
    return ssl;
}

SSL *phim_tls_connect_upstream(int fd, SSL_CTX *ctx, const char *sni)
{
    SSL *ssl = SSL_new(ctx);
    if (ssl == NULL) {
        return NULL;
    }
    SSL_set_fd(ssl, fd);
    if (sni != NULL && sni[0] != '\0') {
        (void)SSL_set_tlsext_host_name(ssl, sni);
        (void)SSL_set1_host(ssl, sni);
    }
    if (SSL_connect(ssl) != 1) {
        PHIM_LOGW("upstream TLS handshake failed: %lu", ERR_get_error());
        SSL_free(ssl);
        return NULL;
    }
    return ssl;
}

int phim_ssl_write_all(SSL *ssl, const void *buf, size_t len)
{
    const unsigned char *p = (const unsigned char *)buf;
    size_t off = 0u;
    unsigned int transient_retries = 0u;

    if (ssl == NULL || (buf == NULL && len > 0u)) {
        return PHIM_ERR;
    }

    while (off < len) {
        size_t want = len - off > 16384u ? 16384u : len - off;
        int n;
        int err;
        int saved_errno;

        ERR_clear_error();
        errno = 0;
        n = SSL_write(ssl, p + off, (int)want);
        if (n > 0) {
            off += (size_t)n;
            transient_retries = 0u;
            continue;
        }

        saved_errno = errno;
        err = SSL_get_error(ssl, n);
        switch (err) {
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            if (++transient_retries > PHIM_SSL_TRANSIENT_RETRY_MAX) {
                return PHIM_ERR;
            }
            continue;
        case SSL_ERROR_SYSCALL:
            if (saved_errno == EINTR) {
                if (++transient_retries > PHIM_SSL_TRANSIENT_RETRY_MAX) {
                    return PHIM_ERR;
                }
                continue;
            }
            return PHIM_ERR;
        default:
            return PHIM_ERR;
        }
    }
    return PHIM_OK;
}

ssize_t phim_ssl_read_some(SSL *ssl, void *buf, size_t len)
{
    const int read_len = (int)(len > 16384u ? 16384u : len);
    unsigned int transient_retries = 0u;

    if (ssl == NULL || buf == NULL || len == 0u) {
        return -1;
    }

    for (;;) {
        int n;
        int err;
        int saved_errno;

        ERR_clear_error();
        errno = 0;
        n = SSL_read(ssl, buf, read_len);
        if (n > 0) {
            return (ssize_t)n;
        }

        saved_errno = errno;
        err = SSL_get_error(ssl, n);
        switch (err) {
        case SSL_ERROR_ZERO_RETURN:
            return 0;
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            if (++transient_retries > PHIM_SSL_TRANSIENT_RETRY_MAX) {
                return -1;
            }
            continue;
        case SSL_ERROR_SYSCALL:
            if (saved_errno == EINTR) {
                if (++transient_retries > PHIM_SSL_TRANSIENT_RETRY_MAX) {
                    return -1;
                }
                continue;
            }
            return -1;
        default:
            return -1;
        }
    }
}

int phim_ssl_read_until(SSL *ssl, unsigned char *buf, size_t cap, const char *needle, size_t *out_len)
{
    size_t used = 0u;
    size_t scan_from = 0u;
    size_t needle_len;

    if (ssl == NULL || buf == NULL || needle == NULL || out_len == NULL || cap == 0u) {
        return PHIM_ERR;
    }
    needle_len = strlen(needle);
    if (needle_len == 0u || needle_len > cap) {
        return PHIM_ERR;
    }

    while (used < cap) {
        ssize_t n = phim_ssl_read_some(ssl, buf + used, cap - used);
        if (n <= 0) {
            return PHIM_ERR;
        }
        used += (size_t)n;
        if (used >= needle_len) {
            size_t i;
            for (i = scan_from; i + needle_len <= used; i++) {
                if (memcmp(buf + i, needle, needle_len) == 0) {
                    *out_len = used;
                    return PHIM_OK;
                }
            }
            scan_from = used - needle_len + 1u;
        }
    }
    return PHIM_ERR;
}

void phim_ssl_close(SSL **ssl)
{
    if (ssl != NULL && *ssl != NULL) {
        (void)SSL_shutdown(*ssl);
        SSL_free(*ssl);
        *ssl = NULL;
    }
}

void phim_ssl_ctx_free(SSL_CTX **ctx)
{
    if (ctx != NULL && *ctx != NULL) {
        SSL_CTX_free(*ctx);
        *ctx = NULL;
    }
}
