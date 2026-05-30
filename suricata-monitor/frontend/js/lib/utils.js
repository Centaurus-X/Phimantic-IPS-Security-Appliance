/**
 * SuricataMonitor-NG · Utils
 * Kleine Helfer ohne externen Abhaengigkeiten.
 */

(function (global) {
    "use strict";

    function escapeHtml(str) {
        if (str === null || str === undefined) { return ""; }
        return String(str)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/"/g, "&quot;")
            .replace(/'/g, "&#39;");
    }

    function formatTimestamp(iso) {
        if (!iso) { return "-"; }
        try {
            const d = new Date(iso);
            if (isNaN(d.getTime())) { return iso; }
            const pad = n => String(n).padStart(2, "0");
            return d.getFullYear() + "-" +
                   pad(d.getMonth() + 1) + "-" +
                   pad(d.getDate()) + " " +
                   pad(d.getHours()) + ":" +
                   pad(d.getMinutes()) + ":" +
                   pad(d.getSeconds());
        } catch (e) { return iso; }
    }

    function formatBytes(bytes) {
        if (!bytes && bytes !== 0) { return "-"; }
        const units = ["B", "KB", "MB", "GB", "TB"];
        let n = Number(bytes);
        let i = 0;
        while (n >= 1024 && i < units.length - 1) { n /= 1024; i++; }
        return n.toFixed(i === 0 ? 0 : 1) + " " + units[i];
    }

    function formatDuration(seconds) {
        if (!seconds && seconds !== 0) { return "-"; }
        const s = Math.floor(Number(seconds));
        if (s < 60) { return s + "s"; }
        if (s < 3600) { return Math.floor(s / 60) + "m " + (s % 60) + "s"; }
        if (s < 86400) {
            return Math.floor(s / 3600) + "h " +
                   Math.floor((s % 3600) / 60) + "m";
        }
        const days = Math.floor(s / 86400);
        return days + "d " + Math.floor((s % 86400) / 3600) + "h";
    }

    function severityLabel(sev) {
        const s = Number(sev);
        if (s === 1) { return "high"; }
        if (s === 2) { return "medium"; }
        if (s === 3) { return "low"; }
        return "info";
    }

    function debounce(fn, ms) {
        let t = null;
        return function () {
            const args = arguments;
            if (t) { clearTimeout(t); }
            t = setTimeout(() => fn.apply(null, args), ms);
        };
    }

    function el(tag, attrs, children) {
        const node = document.createElement(tag);
        if (attrs) {
            Object.keys(attrs).forEach(k => {
                if (k === "class") { node.className = attrs[k]; }
                else if (k === "html") { node.innerHTML = attrs[k]; }
                else if (k === "text") {
                    node.appendChild(document.createTextNode(attrs[k]));
                }
                else if (attrs[k] === null || attrs[k] === undefined) { return; }
                else if (k === "value") { node.value = attrs[k]; }
                else if (k === "checked") { node.checked = Boolean(attrs[k]); }
                else if (k === "disabled") { node.disabled = Boolean(attrs[k]); }
                else if (k === "readOnly" || k === "readonly") {
                    node.readOnly = Boolean(attrs[k]);
                }
                else if (k.indexOf("on") === 0 &&
                         typeof attrs[k] === "function") {
                    node.addEventListener(k.slice(2), attrs[k]);
                }
                else { node.setAttribute(k, attrs[k]); }
            });
        }
        if (children) {
            (Array.isArray(children) ? children : [children]).forEach(c => {
                if (c === null || c === undefined) { return; }
                if (typeof c === "string") {
                    node.appendChild(document.createTextNode(c));
                } else {
                    node.appendChild(c);
                }
            });
        }
        return node;
    }

    function clear(node) {
        while (node.firstChild) { node.removeChild(node.firstChild); }
    }

    global.SGUtils = {
        escapeHtml, formatTimestamp, formatBytes, formatDuration,
        severityLabel, debounce, el, clear
    };

}(window));
