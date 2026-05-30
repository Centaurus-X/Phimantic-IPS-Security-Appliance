/**
 * SuricataMonitor-NG · Theme Switcher
 *
 * Persistiert die Wahl im localStorage. Initialer Default folgt dem
 * Betriebssystem-Praeferenz (prefers-color-scheme).
 *
 * Verwendung:
 *     SGTheme.init();
 *     SGTheme.toggle();
 *     SGTheme.set("dark");
 *     SGTheme.get();
 */

(function (global) {
    "use strict";

    const STORAGE_KEY = "surimon.theme";
    const VALID = ["light", "dark"];
    const listeners = [];

    function detectInitial() {
        try {
            const stored = localStorage.getItem(STORAGE_KEY);
            if (VALID.indexOf(stored) >= 0) { return stored; }
        } catch (e) { /* ignore */ }
        if (window.matchMedia &&
            window.matchMedia("(prefers-color-scheme: dark)").matches) {
            return "dark";
        }
        return "light";
    }

    function apply(theme) {
        if (VALID.indexOf(theme) < 0) { theme = "light"; }
        document.documentElement.setAttribute("data-theme", theme);
        try { localStorage.setItem(STORAGE_KEY, theme); } catch (e) {}
        listeners.forEach(fn => { try { fn(theme); } catch (e) {} });
    }

    const theme = {
        init: function () {
            apply(detectInitial());
        },
        get: function () {
            return document.documentElement.getAttribute("data-theme") || "light";
        },
        set: function (t) { apply(t); },
        toggle: function () {
            apply(this.get() === "dark" ? "light" : "dark");
        },
        onChange: function (fn) { listeners.push(fn); }
    };

    global.SGTheme = theme;

}(window));
