/**
 * SuricataMonitor · Dashboard (Original-Layout)
 *
 * Layout:
 *   ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
 *   │ Server Status│  │ Log Summary  │  │ Event Summary│
 *   └──────────────┘  └──────────────┘  └──────────────┘
 *                                       ┌──────────────┐
 *                                       │ Recent Events│
 *                                       └──────────────┘
 */
(function () {
    "use strict";

    const { el, clear, formatTimestamp, formatBytes, formatDuration } = SGUtils;

    function svgIcon(paths, color) {
        const ns = "http://www.w3.org/2000/svg";
        const svg = document.createElementNS(ns, "svg");
        svg.setAttribute("width", "20"); svg.setAttribute("height", "20");
        svg.setAttribute("viewBox", "0 0 24 24");
        svg.setAttribute("fill", "none");
        svg.setAttribute("stroke", color || "currentColor");
        svg.setAttribute("stroke-width", "2");
        svg.setAttribute("stroke-linecap", "round");
        svg.setAttribute("stroke-linejoin", "round");
        (Array.isArray(paths) ? paths : [paths]).forEach(d => {
            const p = document.createElementNS(ns, "path");
            p.setAttribute("d", d);
            svg.appendChild(p);
        });
        return svg;
    }

    function statusBadge(status) {
        const map = {
            active: ["badge-success", "active"],
            inactive: ["badge-neutral", "inactive"],
            failed: ["badge-danger", "failed"]
        };
        const [cls, lbl] = map[status] || ["badge-neutral", status || "unknown"];
        return el("span", { class: "badge " + cls, text: lbl });
    }

    function severityChip(sev) {
        const s = Number(sev) || 3;
        const lbl = s === 1 ? "high" : s === 2 ? "medium" : "low";
        return el("span", { class: "badge severity-" + s, text: lbl });
    }

    function metricRow(label, value, isLast) {
        const row = el("div", { class: "metric-row" + (isLast ? " last" : "") });
        row.appendChild(el("div", { class: "metric-label", text: label }));
        row.appendChild(el("div", { class: "metric-value", text: value }));
        return row;
    }

    function dashCard(title, headerExtra, body) {
        const card = el("div", { class: "dash-card" });
        const h = el("div", { class: "dash-card-header" });
        h.appendChild(el("div", { class: "dash-card-title", text: title }));
        if (headerExtra) { h.appendChild(headerExtra); }
        card.appendChild(h);
        card.appendChild(body);
        return card;
    }

    function miniTile(icon, label, value, kind) {
        const t = el("div", { class: "mini-tile" + (kind ? " kind-" + kind : "") });
        const iconWrap = el("div", { class: "mini-tile-icon" });
        iconWrap.appendChild(icon);
        t.appendChild(iconWrap);
        const txt = el("div", { class: "mini-tile-text" });
        txt.appendChild(el("div", { class: "mini-tile-label", text: label }));
        txt.appendChild(el("div", { class: "mini-tile-value", text: value }));
        t.appendChild(txt);
        return t;
    }

    async function render() {
        const content = document.getElementById("app-content");
        clear(content);

        const header = el("div", { class: "page-header" });
        const titleBlock = el("div");
        titleBlock.appendChild(el("div", { class: "page-title", text: "Dashboard" }));
        titleBlock.appendChild(el("div", { class: "page-subtitle",
            text: "Server Status · Log Summary · Event Summary" }));
        header.appendChild(titleBlock);

        const refreshBtn = el("button", {
            class: "btn", text: "Aktualisieren",
            onclick: () => render()
        });
        header.appendChild(el("div", { class: "page-toolbar" }, [refreshBtn]));
        content.appendChild(header);

        const loading = el("div", { class: "loading-block",
            html: '<div class="spinner"></div><div class="mt-12">Lade…</div>' });
        content.appendChild(loading);

        let data;
        try {
            data = await SGApi.get("/dashboard/overview");
        } catch (err) {
            content.removeChild(loading);
            SGComponents.toast("Konnte Dashboard-Daten nicht laden", "error");
            return;
        }

        // Events fuer "Recent" + Stage-Counts
        let events = { events: [] };
        try {
            events = await SGApi.get("/events", { limit: 7 });
        } catch (err) { /* nicht kritisch */ }

        content.removeChild(loading);

        // Top-Reihe mit den drei Hauptkarten
        const topRow = el("div", { class: "dash-top-grid" });

        // ===== Server Status =====
        const serverBody = el("div", { class: "dash-card-body" });
        serverBody.appendChild(metricRow("Uptime",
            formatDuration(data.service.uptime_seconds)));
        serverBody.appendChild(metricRow("Version",
            data.service.version || "-"));
        serverBody.appendChild(metricRow("Run-Mode",
            data.service.run_mode || "-"));
        serverBody.appendChild(metricRow("Capture-Mode",
            data.service.capture_mode || "-", true));
        topRow.appendChild(dashCard("Server Status",
            statusBadge(data.service.status), serverBody));

        // ===== Log Summary =====
        const logBody = el("div", { class: "dash-card-body" });
        const logGrid = el("div", { class: "mini-tile-grid" });

        const evePath = data.eve.path || "-";
        const eveSize = data.eve.exists
            ? formatBytes(data.eve.size_bytes) : "kein File";

        const topRules = data.stats.top_rules || [];
        const totalHits = Number(data.eve.alert_count || 0);
        const httpHits = Number(data.eve.http_count || 0);
        const dnsHits = Number(data.eve.dns_count || 0);
        const totalEveLogs = Number(data.eve.total_lines || data.stats.total_events || 0);

        logGrid.appendChild(miniTile(
            svgIcon("M10.29 3.86 1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z M12 9 L12 13 M12 17 L12.01 17", "var(--color-warning)"),
            "Alert Logs", String(totalHits), "warning"));

        logGrid.appendChild(miniTile(
            svgIcon(["M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"], "var(--color-info)"),
            "DNS Logs", String(dnsHits), "info"));

        logGrid.appendChild(miniTile(
            svgIcon(["M2 12 a 10 10 0 1 0 20 0 a 10 10 0 0 0 -20 0", "M2 12 h 20", "M12 2 a 15.3 15.3 0 0 1 4 10 a 15.3 15.3 0 0 1 -4 10 a 15.3 15.3 0 0 1 -4 -10 a 15.3 15.3 0 0 1 4 -10 z"], "var(--color-success)"),
            "HTTP Hits", String(httpHits), "success"));

        logGrid.appendChild(miniTile(
            svgIcon(["M12 2L3 7l9 5 9-5-9-5z", "M3 17l9 5 9-5", "M3 12l9 5 9-5"], "var(--text-link)"),
            "Total Logs", String(totalEveLogs)));

        logBody.appendChild(logGrid);
        logBody.appendChild(el("div", { class: "dash-card-footer fs-12 muted",
            text: "Letzte Aktualisierung: " + formatTimestamp(data.eve.mtime_iso) }));

        const evePathLink = el("div", { class: "fs-12 muted mono",
            style: "margin-top:6px", text: evePath + " (" + eveSize + ")" });
        logBody.appendChild(evePathLink);

        topRow.appendChild(dashCard("Log Summary",
            svgIcon(["M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z",
                     "M14 2v6h6", "M16 13H8", "M16 17H8", "M10 9H8"],
                    "var(--color-primary)"),
            logBody));

        // ===== Event Summary =====
        const eventBody = el("div", { class: "dash-card-body" });
        const eventGrid = el("div", { class: "mini-tile-grid" });

        // Stage-Counts aus by_stage
        const stageCounts = { pending: 0, investigation: 0, completed: 0 };
        (data.stats.by_stage || []).forEach(r => {
            if (r.stage in stageCounts) { stageCounts[r.stage] = r.c; }
        });
        // Severity counts (Critical = severity 1)
        const sevCounts = { 1: 0, 2: 0, 3: 0 };
        (data.stats.by_severity || []).forEach(r => {
            if (r.severity in sevCounts) { sevCounts[r.severity] = r.c; }
        });

        eventGrid.appendChild(miniTile(
            svgIcon(["M12 8v4 l 3 3"], "var(--color-warning)"),
            "Pending", String(stageCounts.pending), "warning"));
        eventGrid.appendChild(miniTile(
            svgIcon(["M21 21l-4.35-4.35", "M11 19a8 8 0 1 1 0-16 8 8 0 0 1 0 16z"],
                    "var(--color-info)"),
            "Investigation", String(stageCounts.investigation), "info"));
        eventGrid.appendChild(miniTile(
            svgIcon(["M20 6L9 17l-5-5"], "var(--color-success)"),
            "Completed", String(stageCounts.completed), "success"));
        eventGrid.appendChild(miniTile(
            svgIcon(["M10.29 3.86 1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z",
                     "M12 9v4", "M12 17h.01"], "var(--color-danger)"),
            "Critical (High)", String(sevCounts[1] || 0), "danger"));

        eventBody.appendChild(eventGrid);
        eventBody.appendChild(el("div", { class: "dash-card-footer fs-12 muted",
            text: "Real-time monitoring" }));

        const evtAllLink = el("a", { class: "btn-link", href: "#/events",
            text: "View All Events →" });
        const evtHeaderExtra = svgIcon(["M22 11.08V12a10 10 0 1 1-5.93-9.14",
                                          "M22 4L12 14.01l-3-3"],
                                       "var(--color-primary)");
        topRow.appendChild(dashCard("Event Summary", evtHeaderExtra, eventBody));

        content.appendChild(topRow);

        // ===== Recent Events Card =====
        const recentBody = el("div", { class: "dash-card-body tight" });
        if (!events.events || events.events.length === 0) {
            recentBody.appendChild(el("div", { class: "empty-state",
                text: "Noch keine Events." }));
        } else {
            const tbl = el("table", { class: "table table-compact" });
            const thead = el("thead");
            thead.innerHTML = "<tr><th>SEVERITY</th><th>SOURCE</th>"
                + "<th>STAGE</th><th>TIME</th></tr>";
            tbl.appendChild(thead);
            const tbody = el("tbody");
            events.events.slice(0, 7).forEach(e => {
                const tr = el("tr");
                const td1 = el("td"); td1.appendChild(severityChip(e.severity));
                tr.appendChild(td1);
                tr.appendChild(el("td", { class: "mono fs-12",
                    text: (e.src_ip || "-") + ":" + (e.src_port || "?") }));
                const td3 = el("td");
                td3.appendChild(el("span", {
                    class: "badge stage-" + (e.stage || "pending"),
                    text: e.stage || "pending" }));
                tr.appendChild(td3);
                tr.appendChild(el("td", { class: "fs-12 muted",
                    text: formatTimestamp(e.timestamp) }));
                tbody.appendChild(tr);
            });
            tbl.appendChild(tbody);
            recentBody.appendChild(el("div", { class: "table-wrap-flush" }, [tbl]));
        }
        const recentHeader = el("a", { class: "btn-link", href: "#/events",
            text: "View All →" });
        const recentCard = dashCard("Recent Events", recentHeader, recentBody);
        recentCard.classList.add("dash-card-wide");
        content.appendChild(recentCard);
    }

    window.SGPageDashboard = render;
}());
