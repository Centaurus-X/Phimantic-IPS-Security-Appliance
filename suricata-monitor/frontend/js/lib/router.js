/**
 * SuricataMonitor-NG · Hash Router
 *
 * Hash-basiert (kein History-API), damit kein Server-Rewrite noetig ist.
 * Routes registrieren mit:
 *     SGRouter.register("dashboard", renderDashboard);
 * Programmatisch navigieren:
 *     SGRouter.navigate("events?stage=pending");
 * Auf Aenderungen reagieren:
 *     SGRouter.onChange((route, params) => { ... });
 */

(function (global) {
    "use strict";

    const routes = {};
    const changeListeners = [];
    let currentRoute = null;
    let defaultRoute = "dashboard";

    function parseHash() {
        const raw = (location.hash || "").replace(/^#\/?/, "");
        if (!raw) {
            return { name: defaultRoute, params: {} };
        }
        const [name, query] = raw.split("?");
        const params = {};
        if (query) {
            query.split("&").forEach(pair => {
                const [k, v] = pair.split("=");
                if (k) { params[decodeURIComponent(k)] =
                    v ? decodeURIComponent(v) : ""; }
            });
        }
        return { name: name || defaultRoute, params: params };
    }

    function buildHash(name, params) {
        let h = "#/" + name;
        if (params && Object.keys(params).length > 0) {
            const qs = Object.entries(params)
                .filter(([_, v]) => v !== undefined && v !== null && v !== "")
                .map(([k, v]) => encodeURIComponent(k) + "=" +
                                  encodeURIComponent(v))
                .join("&");
            if (qs) { h += "?" + qs; }
        }
        return h;
    }

    function dispatch() {
        const route = parseHash();
        currentRoute = route;
        changeListeners.forEach(fn => {
            try { fn(route.name, route.params); }
            catch (err) { console.error("Router listener error:", err); }
        });
        const handler = routes[route.name];
        if (typeof handler === "function") {
            try { handler(route.params); }
            catch (err) { console.error("Route handler error:", err); }
        } else if (route.name !== defaultRoute) {
            location.hash = buildHash(defaultRoute, {});
        }
    }

    const router = {
        register: function (name, handler) {
            routes[name] = handler;
        },
        setDefault: function (name) { defaultRoute = name; },
        navigate: function (name, params) {
            const newHash = buildHash(name, params || {});
            if (location.hash === newHash) {
                dispatch();  // gleicher Hash - manuell triggern
            } else {
                location.hash = newHash;
            }
        },
        current: function () { return currentRoute; },
        onChange: function (fn) { changeListeners.push(fn); },
        start: function () {
            window.addEventListener("hashchange", dispatch);
            dispatch();
        }
    };

    global.SGRouter = router;

}(window));
