/**
 * SuricataMonitor-NG · API Client
 *
 * Dunne Schicht ueber fetch():
 *   - JSON Body / Response automatisch
 *   - 401 fuehrt zu Redirect nach /login
 *   - Methoden fuer GET / POST / PATCH / DELETE
 *
 * KEIN Framework, kein Build-Step.
 */

(function (global) {
    "use strict";

    const API_BASE = "/api/v1";

    function buildUrl(path, params) {
        let url = API_BASE + path;
        if (params && typeof params === "object") {
            const qs = Object.entries(params)
                .filter(([_, v]) => v !== undefined && v !== null && v !== "")
                .map(([k, v]) => encodeURIComponent(k) + "=" +
                                  encodeURIComponent(v))
                .join("&");
            if (qs) { url += "?" + qs; }
        }
        return url;
    }

    async function request(method, path, opts) {
        opts = opts || {};
        const init = {
            method: method,
            credentials: "same-origin",
            headers: { "Accept": "application/json" }
        };
        if (opts.body !== undefined) {
            init.headers["Content-Type"] = "application/json";
            init.body = JSON.stringify(opts.body);
        }
        const url = buildUrl(path, opts.params);
        let resp;
        try {
            resp = await fetch(url, init);
        } catch (err) {
            const error = new Error("Network error: " + err.message);
            error.kind = "network";
            throw error;
        }

        if (resp.status === 401 && !opts.allowUnauthenticated) {
            if (location.pathname !== "/login") {
                location.href = "/login?next=" +
                    encodeURIComponent(location.pathname + location.hash);
            }
            const err = new Error("Unauthenticated");
            err.kind = "auth";
            throw err;
        }

        let data;
        try {
            data = await resp.json();
        } catch (e) {
            data = {};
        }

        if (!resp.ok) {
            const err = new Error(data.error || data.message ||
                                  "Request failed: " + resp.status);
            err.kind = "api";
            err.status = resp.status;
            err.data = data;
            throw err;
        }
        return data;
    }

    const api = {
        get:    (path, params, opts) => request("GET", path,
                                {params: params, ...(opts || {})}),
        post:   (path, body, opts) => request("POST", path,
                                {body: body, ...(opts || {})}),
        patch:  (path, body, opts) => request("PATCH", path,
                                {body: body, ...(opts || {})}),
        delete: (path, opts) => request("DELETE", path, opts || {}),

        // High-Level Helpers
        whoami: () => request("GET", "/auth/whoami",
                              {allowUnauthenticated: true})
                        .catch(e => { if (e.kind === "auth") return null;
                                      throw e; }),
        login:  (username, password) => request("POST", "/auth/login",
                              {body: {username, password},
                               allowUnauthenticated: true}),
        logout: () => request("POST", "/auth/logout",
                              {allowUnauthenticated: true})
    };

    global.SGApi = api;

}(window));
