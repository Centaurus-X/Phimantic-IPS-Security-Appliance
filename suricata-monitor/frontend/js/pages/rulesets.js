/**
 * SuricataMonitor · Rulesets Page
 *
 * Zwei Tabs:
 *   - Rule Files (lokale .rules-Dateien, mit Toggle pro Regel)
 *   - Pattern-Quellen (suricata-update sources, z.B. ET)
 */
(function () {
    "use strict";

    const { el, clear, formatTimestamp, formatBytes,
            escapeHtml, debounce } = SGUtils;

    let currentTab = "files";
    let currentFile = null;
    let currentFileParams = { offset: 0, limit: 100, q: "" };
    let rulesRuntimeDirty = false;
    let lastReloadResult = null;

    function shortText(value, maxLen) {
        const text = String(value || "");
        if (text.length <= maxLen) { return text; }
        return text.slice(0, maxLen - 1) + "…";
    }

    function describeReloadResult(res) {
        if (!res) { return "Noch kein Reload in dieser UI-Sitzung ausgefuehrt."; }
        const stage = res.stage ? ("Stage: " + res.stage + " · ") : "";
        const command = res.command ? ("Command: " + res.command + " · ") : "";
        return shortText(stage + command + (res.message || "ok"), 1200);
    }

    function markReloadRequired(message) {
        rulesRuntimeDirty = true;
        SGComponents.toast(message || "Aenderung gespeichert — Reload erforderlich", "warning", 5000);
    }

    function isAdmin() {
        const u = window.SGCurrentUser;
        return u && u.role === "admin";
    }

    function isWriter() {
        const u = window.SGCurrentUser;
        return u && (u.role === "admin" || u.role === "analyst");
    }

    function appendReloadPanel(container) {
        if (!isWriter()) { return; }
        const reloadCard = el("div", { class: "card mt-24" });
        reloadCard.appendChild(el("div", { class: "card-header" }, [
            el("div", { class: "card-title", text: "Suricata Rules neu laden" })
        ]));
        const rb = el("div", { class: "card-body" });
        const statusClass = rulesRuntimeDirty ? "badge badge-warning" : "badge badge-success";
        const statusText = rulesRuntimeDirty
            ? "Aenderungen gespeichert — Runtime-Reload erforderlich"
            : "Runtime-Status: kein offener UI-Reload bekannt";
        rb.appendChild(el("div", { class: statusClass, text: statusText }));
        rb.appendChild(el("div", { class: "muted mt-12 mb-12",
            text: "PHIMANTEC prueft zuerst suricata -T. Nur bei erfolgreichem Test wird das Ruleset live reloadet. Bei Fehlern bleibt die alte Suricata Detection Engine aktiv." }));
        if (lastReloadResult) {
            rb.appendChild(el("pre", { class: "code-block fs-12 mb-12",
                text: describeReloadResult(lastReloadResult) }));
        }
        const testBtn = el("button", { class: "btn", text: "suricata -T testen",
            onclick: async () => {
                testBtn.disabled = true;
                testBtn.innerHTML = '<span class="spinner"></span> Test…';
                try {
                    const res = await SGApi.post("/rules/test", {});
                    lastReloadResult = res;
                    SGComponents.toast(res.return === "OK"
                        ? "Suricata-Konfiguration OK"
                        : ("Test fehlgeschlagen: " + (res.message || "unbekannt")),
                        res.return === "OK" ? "success" : "error", 8000);
                    render();
                } catch (err) {
                    SGComponents.toast("Fehler: " + (err.message || "unbekannt"), "error");
                } finally {
                    testBtn.disabled = false;
                    testBtn.textContent = "suricata -T testen";
                }
            }
        });
        const btn = el("button", {
            class: rulesRuntimeDirty ? "btn btn-warning" : "btn btn-primary",
            text: "ruleset-reload-rules",
            onclick: async () => {
                btn.disabled = true;
                btn.innerHTML = '<span class="spinner"></span> Reload…';
                try {
                    const res = await SGApi.post("/rules/reload", {});
                    lastReloadResult = res;
                    if (res.return === "OK" || res.ok) {
                        rulesRuntimeDirty = false;
                        SGComponents.toast("Reload erfolgreich", "success");
                    } else {
                        SGComponents.toast("Reload fehlgeschlagen: " +
                            (res.message || res.stage || "unbekannt"), "error", 10000);
                    }
                    render();
                } catch (err) {
                    SGComponents.toast("Fehler: " + (err.message || "unbekannt"), "error");
                } finally {
                    btn.disabled = false;
                    btn.textContent = "ruleset-reload-rules";
                }
            }
        });
        const row = el("div", { style: "display:flex; gap:8px; flex-wrap:wrap; align-items:center" });
        row.appendChild(testBtn);
        row.appendChild(btn);
        rb.appendChild(row);
        reloadCard.appendChild(rb);
        container.appendChild(reloadCard);
    }

    // ====================== File List View ======================

    async function renderFileList(container) {
        clear(container);
        const loading = el("div", { class: "empty-state",
                                     html: '<div class="spinner"></div>' });
        container.appendChild(loading);

        let data;
        try {
            data = await SGApi.get("/rules/files");
        } catch (err) {
            clear(container);
            container.appendChild(el("div", { class: "empty-state",
                                               text: "Fehler beim Laden." }));
            return;
        }

        clear(container);
        if (!data.files || data.files.length === 0) {
            container.appendChild(el("div", { class: "empty-state",
                                               text: "Keine Rule-Dateien gefunden." }));
            return;
        }

        const table = el("table", { class: "table" });
        const thead = el("thead");
        thead.innerHTML =
            '<tr><th>DATEINAME</th><th>AKTIV</th><th>DEAKTIV.</th>'
            + '<th>GROESSE</th><th>GEAENDERT</th>'
            + '<th class="col-actions">AKTION</th></tr>';
        table.appendChild(thead);
        const tbody = el("tbody");
        data.files.forEach(f => {
            const tr = el("tr");
            const nameCell = el("td");
            const nameLink = el("a", { class: "row-id mono fs-13", href: "#",
                                        text: f.filename });
            nameLink.addEventListener("click", (e) => {
                e.preventDefault();
                currentFile = f.filename;
                currentFileParams = { offset: 0, limit: 100, q: "" };
                renderFileDetail(container);
            });
            nameCell.appendChild(nameLink);
            tr.appendChild(nameCell);
            tr.appendChild(el("td", { class: "fw-600",
                                       text: String(f.active_rules) }));
            tr.appendChild(el("td", { class: "muted",
                                       text: String(f.disabled_rules) }));
            tr.appendChild(el("td", { class: "fs-12",
                                       text: formatBytes(f.size_bytes) }));
            tr.appendChild(el("td", { class: "fs-12",
                text: new Date(f.mtime * 1000).toLocaleString() }));
            const act = el("td", { class: "col-actions" });
            act.appendChild(el("button", {
                class: "btn-link", text: "Oeffnen",
                onclick: () => {
                    currentFile = f.filename;
                    currentFileParams = { offset: 0, limit: 100, q: "" };
                    renderFileDetail(container);
                }
            }));
            tr.appendChild(act);
            tbody.appendChild(tr);
        });
        table.appendChild(tbody);
        const wrap = el("div", { class: "table-wrap" }, [table]);
        container.appendChild(wrap);

        appendReloadPanel(container);
    }

    // ====================== File Detail View ======================

    async function renderFileDetail(container) {
        clear(container);

        const header = el("div", { class: "page-header" });
        header.appendChild(el("div", { class: "page-title mono fs-13",
                                        text: currentFile }));

        const tool = el("div", { class: "page-toolbar" });
        const backBtn = el("button", {
            class: "btn", text: "← zur Liste",
            onclick: () => { currentFile = null; renderFileList(container); }
        });
        tool.appendChild(backBtn);

        if (isWriter()) {
            const newBtn = el("button", {
                class: "btn btn-primary", text: "+ Neue Regel",
                onclick: () => showRuleEditor(null, container)
            });
            tool.appendChild(newBtn);
        }
        header.appendChild(tool);
        container.appendChild(header);
        appendReloadPanel(container);

        // Search
        const searchCard = el("div", { class: "card mb-12" });
        const searchBody = el("div", { class: "card-body" });
        const searchWrap = el("div", { class: "input-with-icon" });
        searchWrap.innerHTML = '<span class="icon">'
            + '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg></span>';
        const searchInput = el("input", {
            class: "input", type: "search",
            placeholder: "Suchen nach SID/Message/Inhalt…",
            value: currentFileParams.q
        });
        searchInput.addEventListener("input", debounce(() => {
            currentFileParams.q = searchInput.value.trim();
            currentFileParams.offset = 0;
            loadAndRenderRules(container);
        }, 250));
        searchWrap.appendChild(searchInput);
        searchBody.appendChild(searchWrap);
        searchCard.appendChild(searchBody);
        container.appendChild(searchCard);

        const tableHolder = el("div", { id: "rules-table-holder" });
        container.appendChild(tableHolder);

        await loadAndRenderRules(container);
    }

    async function loadAndRenderRules(container) {
        const holder = container.querySelector("#rules-table-holder");
        if (!holder) { return; }
        clear(holder);
        holder.appendChild(el("div", { class: "empty-state",
                                        html: '<div class="spinner"></div>' }));

        let data;
        try {
            data = await SGApi.get("/rules/files/" +
                encodeURIComponent(currentFile), currentFileParams);
        } catch (err) {
            clear(holder);
            holder.appendChild(el("div", { class: "empty-state",
                                            text: "Fehler beim Laden." }));
            return;
        }

        clear(holder);
        const tableWrap = el("div", { class: "table-wrap" });
        if (!data.rules || data.rules.length === 0) {
            tableWrap.appendChild(el("div", { class: "empty-state",
                                               text: "Keine Regeln gefunden." }));
        } else {
            const table = el("table", { class: "table" });
            const thead = el("thead");
            thead.innerHTML = "<tr><th>STATUS</th><th>SID</th><th>REV</th>"
                + "<th>ACTION</th><th>MESSAGE</th>"
                + '<th class="col-actions">AKTION</th></tr>';
            table.appendChild(thead);

            const tbody = el("tbody");
            data.rules.forEach(r => {
                const tr = el("tr");
                const stateCell = el("td");
                const sw = buildToggleSwitch(r.enabled, async (newState) => {
                    try {
                        await SGApi.post("/rules/files/"
                            + encodeURIComponent(currentFile) + "/toggle",
                            { sid: r.sid, enabled: newState });
                        markReloadRequired(newState
                            ? "Regel aktiviert — Reload erforderlich"
                            : "Regel deaktiviert — Reload erforderlich");
                    } catch (err) {
                        SGComponents.toast("Fehler: " +
                            (err.message || "unbekannt"), "error");
                        throw err;
                    }
                });
                stateCell.appendChild(sw);
                tr.appendChild(stateCell);
                tr.appendChild(el("td", { class: "mono row-id",
                                           text: String(r.sid || "-") }));
                tr.appendChild(el("td", { class: "fs-12",
                                           text: String(r.rev || "-") }));
                tr.appendChild(el("td",
                    { class: "badge badge-info fs-12", text: r.action || "-" }));
                tr.appendChild(el("td", { text: r.msg || "-",
                                           style: "max-width:480px" }));

                const act = el("td", { class: "col-actions" });
                act.appendChild(el("button", {
                    class: "btn-link", text: "Ansehen",
                    onclick: () => showRuleViewer(r)
                }));
                if (isWriter()) {
                    act.appendChild(el("button", {
                        class: "btn-link", text: "Edit",
                        onclick: () => showRuleEditor(r, container)
                    }));
                }
                if (isAdmin()) {
                    act.appendChild(el("button", {
                        class: "btn-link",
                        style: "color: var(--color-danger)",
                        text: "Loeschen",
                        onclick: async () => {
                            const ok = await SGComponents.confirmDialog({
                                title: "Regel loeschen",
                                message: "SID " + r.sid +
                                         " wirklich loeschen?",
                                confirmText: "Loeschen", danger: true });
                            if (!ok) { return; }
                            try {
                                await SGApi.delete("/rules/files/"
                                    + encodeURIComponent(currentFile)
                                    + "/rule/" + r.sid);
                                markReloadRequired("Regel geloescht — Reload erforderlich");
                                loadAndRenderRules(container);
                            } catch (err) {
                                SGComponents.toast("Fehler: " +
                                    (err.message || "unbekannt"), "error");
                            }
                        }
                    }));
                }
                tr.appendChild(act);
                tbody.appendChild(tr);
            });
            table.appendChild(tbody);
            tableWrap.appendChild(table);
        }
        holder.appendChild(tableWrap);

        // Pagination
        const limit = currentFileParams.limit || 100;
        const offset = currentFileParams.offset || 0;
        const pag = el("div", { class: "pagination" });
        pag.appendChild(el("div", { class: "info",
            text: data.total + " Regeln gesamt · Anzeige "
                  + (data.total > 0 ? (offset + 1) : 0) + "-"
                  + Math.min(offset + limit, data.total) }));
        const ctrl = el("div", { class: "controls" });
        const prev = el("button", { class: "btn btn-sm", text: "← Zurueck",
            disabled: offset === 0 ? "disabled" : null,
            onclick: () => { currentFileParams.offset = Math.max(0, offset - limit);
                              loadAndRenderRules(container); } });
        const next = el("button", { class: "btn btn-sm", text: "Vor →",
            disabled: offset + limit >= data.total ? "disabled" : null,
            onclick: () => { currentFileParams.offset = offset + limit;
                              loadAndRenderRules(container); } });
        ctrl.appendChild(prev); ctrl.appendChild(next);
        pag.appendChild(ctrl);
        holder.appendChild(pag);
    }

    function buildToggleSwitch(initial, onToggle) {
        const lbl = el("label", { class: "switch" });
        const inp = el("input", { type: "checkbox" });
        if (initial) { inp.checked = true; }
        if (!isWriter()) { inp.disabled = true; }
        const slider = el("span", { class: "slider" });
        lbl.appendChild(inp);
        lbl.appendChild(slider);
        inp.addEventListener("change", async () => {
            try {
                await onToggle(inp.checked);
            } catch (err) {
                inp.checked = !inp.checked;  // revert
            }
        });
        return lbl;
    }

    function showRuleViewer(rule) {
        const body = el("div");
        body.appendChild(el("div", { class: "fw-600 mb-12",
                                      text: "Raw Rule" }));
        body.appendChild(el("pre", { class: "code-block",
                                      text: rule.raw }));
        if (rule.classtype) {
            body.appendChild(el("div", { class: "mt-12 fs-12 muted",
                                          text: "classtype: " + rule.classtype }));
        }
        const closeBtn = el("button", { class: "btn", text: "Schliessen",
                                         onclick: () => SGComponents.closeModal() });
        SGComponents.showModal({
            title: "Regel SID " + rule.sid, body: body, footer: [closeBtn],
            large: true
        });
    }

    function showRuleEditor(existing, container) {
        const body = el("div");
        body.appendChild(el("div", { class: "muted mb-12",
            text: "Eine Regel pro Zeile. Format gemaess Suricata-Syntax."
                + " Beispiel:" }));
        body.appendChild(el("pre", { class: "code-block fs-12",
            text: 'alert tcp any any -> any any (msg:"Demo"; '
                  + 'content:"abc"; sid:1000001; rev:1;)' }));

        const ta = el("textarea", { class: "input mt-12",
            placeholder: 'alert tcp any any -> any any '
                + '(msg:"..."; sid:NNN; rev:N;)',
            style: "min-height:140px"
        });
        if (existing) { ta.value = existing.raw; }
        body.appendChild(ta);

        const cancelBtn = el("button", { class: "btn", text: "Abbrechen",
                                          onclick: () => SGComponents.closeModal() });
        const saveBtn = el("button", { class: "btn btn-primary",
                                        text: "Speichern",
            onclick: async () => {
                try {
                    const res = await SGApi.post("/rules/files/"
                        + encodeURIComponent(currentFile) + "/rule",
                        { raw: ta.value });
                    if (res.ok) {
                        markReloadRequired("Regel gespeichert ("
                            + (res.action || "ok") + ") — Reload erforderlich");
                        SGComponents.closeModal();
                        loadAndRenderRules(container);
                    } else {
                        SGComponents.toast("Fehler: " +
                            (res.error || "unbekannt"), "error");
                    }
                } catch (err) {
                    SGComponents.toast("Fehler: " +
                        (err.message || "unbekannt"), "error");
                }
            }
        });

        SGComponents.showModal({
            title: existing ? ("Regel bearbeiten · SID " + existing.sid)
                            : "Neue Regel hinzufuegen",
            body: body, footer: [cancelBtn, saveBtn], large: true
        });
    }

    // ====================== Sources Tab ======================

    async function renderSources(container) {
        clear(container);
        container.appendChild(el("div", { class: "empty-state",
                                           html: '<div class="spinner"></div>' }));

        let data;
        try {
            data = await SGApi.get("/rulesets/sources");
        } catch (err) {
            clear(container);
            container.appendChild(el("div", { class: "empty-state",
                                               text: "Fehler beim Laden." }));
            return;
        }

        clear(container);

        // Action bar
        if (isAdmin()) {
            const bar = el("div", { class: "card mb-12" });
            const bb = el("div", { class: "card-body",
                                    style: "display:flex; gap:8px; "
                                    + "align-items:center" });
            bb.appendChild(el("div", {
                style: "flex:1",
                text: "Suricata-Update-Quellen. Bei Aenderungen anschliessend "
                       + "'Pull Rules' druecken, um die Regeln zu aktualisieren."
            }));
            const refreshIdxBtn = el("button", { class: "btn",
                text: "Source-Index neu laden",
                onclick: async () => {
                    refreshIdxBtn.disabled = true;
                    refreshIdxBtn.innerHTML = '<span class="spinner"></span>';
                    try {
                        await SGApi.post("/rulesets/sources/update", {});
                        SGComponents.toast("Index aktualisiert", "success");
                        renderSources(container);
                    } catch (err) {
                        SGComponents.toast("Fehler: " +
                            (err.message || "unbekannt"), "error");
                    } finally {
                        refreshIdxBtn.disabled = false;
                        refreshIdxBtn.textContent = "Source-Index neu laden";
                    }
                }
            });
            const pullBtn = el("button", {
                class: "btn btn-primary", text: "Pull Rules (suricata-update)",
                onclick: async () => {
                    const ok = await SGComponents.confirmDialog({
                        title: "Regeln aktualisieren",
                        message: "Jetzt alle aktiven Quellen abrufen?",
                        confirmText: "Starten" });
                    if (!ok) { return; }
                    pullBtn.disabled = true;
                    pullBtn.innerHTML = '<span class="spinner"></span> Lade…';
                    try {
                        const res = await SGApi.post("/rulesets/update", {});
                        lastReloadResult = res.reload_result || res;
                        if (res.ok) {
                            rulesRuntimeDirty = false;
                            SGComponents.toast("Rules aktualisiert, getestet und reloadet", "success", 6000);
                        } else {
                            rulesRuntimeDirty = true;
                            SGComponents.toast("Fehler: " + (res.message || ""), "error", 10000);
                        }
                    } catch (err) {
                        SGComponents.toast("Fehler: " +
                            (err.message || "unbekannt"), "error");
                    } finally {
                        pullBtn.disabled = false;
                        pullBtn.textContent = "Pull Rules (suricata-update)";
                    }
                }
            });
            bb.appendChild(refreshIdxBtn);
            bb.appendChild(pullBtn);
            bar.appendChild(bb);
            container.appendChild(bar);
        }

        const docCard = el("div", { class: "card mb-12" });
        const docBody = el("div", { class: "card-body" });
        docBody.appendChild(el("div", { class: "fw-600 mb-8", text: "Externe Ruleset- und Pattern-Quellen" }));
        docBody.appendChild(el("div", { class: "muted mb-8",
            text: "Externe Regeln werden nicht ungeprueft live geladen: PHIMANTEC validiert zuerst suricata -T und reloadet erst danach." }));
        const links = el("div", { style: "display:flex; gap:12px; flex-wrap:wrap" });
        [
            ["Suricata Rule Management", "https://docs.suricata.io/en/latest/rule-management/index.html"],
            ["suricata-update", "https://docs.suricata.io/en/latest/rule-management/suricata-update.html"],
            ["Rule Reload", "https://docs.suricata.io/en/latest/rule-management/rule-reload.html"],
            ["Custom Rules", "https://docs.suricata.io/en/latest/rule-management/adding-your-own-rules.html"],
            ["ET Open Index", "https://rules.emergingthreats.net/open/"]
        ].forEach(pair => links.appendChild(el("a", { class: "btn-link", href: pair[1], target: "_blank", rel: "noopener", text: pair[0] + " ↗" })));
        docBody.appendChild(links);
        docCard.appendChild(docBody);
        container.appendChild(docCard);

        if (!data.sources || data.sources.length === 0) {
            container.appendChild(el("div", { class: "empty-state",
                                               text: "Keine Quellen verfuegbar." }));
            return;
        }

        const table = el("table", { class: "table" });
        const thead = el("thead");
        thead.innerHTML = "<tr><th>STATUS</th><th>NAME</th>"
            + "<th>SUMMARY</th><th>VENDOR</th><th>LICENSE</th>"
            + '<th class="col-actions">AKTION</th></tr>';
        table.appendChild(thead);

        const tbody = el("tbody");
        data.sources.forEach(s => {
            const tr = el("tr");
            const stateCell = el("td");
            const sw = buildToggleSwitch(s.enabled, async (on) => {
                const path = "/rulesets/sources/"
                    + encodeURIComponent(s.name)
                    + (on ? "/enable" : "/disable");
                try {
                    const res = await SGApi.post(path, {});
                    if (!res.ok) {
                        throw new Error(res.message || "Fehler");
                    }
                    markReloadRequired(s.name + (on ? " aktiviert" : " deaktiviert")
                        + " — Pull Rules und Reload erforderlich");
                } catch (err) {
                    SGComponents.toast("Fehler: " +
                        (err.message || "unbekannt"), "error");
                    throw err;
                }
            });
            stateCell.appendChild(sw);
            tr.appendChild(stateCell);
            tr.appendChild(el("td", { class: "mono row-id", text: s.name }));
            tr.appendChild(el("td", { text: s.summary || "-",
                                       style: "max-width:380px" }));
            tr.appendChild(el("td", { text: s.vendor || "-" }));
            tr.appendChild(el("td", { class: "fs-12",
                                       text: s.license || "-" }));
            const act = el("td", { class: "col-actions" });
            if (s.url) {
                act.appendChild(el("a", { class: "btn-link", href: s.url,
                                            target: "_blank", rel: "noopener",
                                            text: "Doku ↗" }));
            }
            tr.appendChild(act);
            tbody.appendChild(tr);
        });
        table.appendChild(tbody);
        container.appendChild(el("div", { class: "table-wrap" }, [table]));
    }

    // ====================== Main Render ======================

    async function render(params) {
        const content = document.getElementById("app-content");
        clear(content);

        const header = el("div", { class: "page-header" });
        const titleBlock = el("div");
        titleBlock.appendChild(el("div", { class: "page-title",
                                            text: "Rulesets" }));
        titleBlock.appendChild(el("div", { class: "page-subtitle",
                                            text: "Lokale Regeldateien und externe Pattern-Quellen" }));
        header.appendChild(titleBlock);
        content.appendChild(header);

        // Tabs
        const tabBar = el("div", { class: "tab-bar" });
        const tabFiles = el("a", { class: "tab", text: "Rule Files",
            onclick: (e) => { e.preventDefault(); currentTab = "files";
                              currentFile = null; render(); } });
        const tabSrc = el("a", { class: "tab", text: "Pattern-Quellen",
            onclick: (e) => { e.preventDefault(); currentTab = "sources";
                              render(); } });
        if (currentTab === "files") { tabFiles.classList.add("active"); }
        else { tabSrc.classList.add("active"); }
        tabBar.appendChild(tabFiles);
        tabBar.appendChild(tabSrc);
        content.appendChild(tabBar);

        const body = el("div");
        content.appendChild(body);

        if (currentTab === "files") {
            if (currentFile) { await renderFileDetail(body); }
            else { await renderFileList(body); }
        } else {
            await renderSources(body);
        }
    }

    window.SGPageRulesets = render;
}());
