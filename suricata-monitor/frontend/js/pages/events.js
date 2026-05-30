/**
 * SuricataMonitor · Event-Log Page
 */
(function () {
    "use strict";

    const { el, clear, formatTimestamp, escapeHtml, debounce } = SGUtils;

    function severityBadge(sev) {
        const s = Number(sev) || 3;
        const label = s === 1 ? "high" : s === 2 ? "medium" : "low";
        return el("span", { class: "badge severity-" + s, text: label });
    }

    function stageBadge(stage) {
        const cls = "badge stage-" + (stage || "pending");
        return el("span", { class: cls, text: stage || "pending" });
    }

    function buildToolbar(params, onChange) {
        const toolbar = el("div", { class: "page-toolbar",
                                     style: "width:100%; gap:8px" });

        // Search
        const searchWrap = el("div", { class: "input-with-icon",
                                        style: "flex:1; min-width:200px" });
        searchWrap.innerHTML =
            '<span class="icon">' +
            '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg>' +
            '</span>';
        const searchInput = el("input", {
            class: "input", type: "search",
            placeholder: "Events durchsuchen…",
            value: params.q || ""
        });
        searchInput.addEventListener("input", debounce(() => {
            onChange("q", searchInput.value.trim());
        }, 250));
        searchWrap.appendChild(searchInput);
        toolbar.appendChild(searchWrap);

        // Stage Filter
        const stageSelect = el("select", { class: "select",
                                            style: "width:auto" });
        [["", "Alle Stages"], ["pending", "Pending"],
         ["investigation", "Investigation"],
         ["completed", "Completed"]].forEach(([v, l]) => {
            const opt = el("option", { value: v, text: l });
            if (v === (params.stage || "")) { opt.selected = true; }
            stageSelect.appendChild(opt);
         });
        stageSelect.addEventListener("change", () =>
            onChange("stage", stageSelect.value));
        toolbar.appendChild(stageSelect);

        // Severity Filter
        const sevSelect = el("select", { class: "select",
                                          style: "width:auto" });
        [["", "Alle Severities"], ["1", "High"], ["2", "Medium"],
         ["3", "Low"]].forEach(([v, l]) => {
            const opt = el("option", { value: v, text: l });
            if (v === (params.severity || "")) { opt.selected = true; }
            sevSelect.appendChild(opt);
         });
        sevSelect.addEventListener("change", () =>
            onChange("severity", sevSelect.value));
        toolbar.appendChild(sevSelect);

        return toolbar;
    }

    async function showEventModal(eventId) {
        let data;
        try {
            data = await SGApi.get("/events/" + eventId);
        } catch (e) {
            SGComponents.toast("Event-Details konnten nicht geladen werden",
                                "error");
            return;
        }
        const evt = data.event;

        const body = el("div");
        const meta = el("div", { class: "card-grid mb-12" });
        [
            ["Flow-ID", evt.flow_id || "-"],
            ["Timestamp", formatTimestamp(evt.timestamp)],
            ["Severity",
             (Number(evt.severity) || 3) + " (" +
             (Number(evt.severity) === 1 ? "high"
              : Number(evt.severity) === 2 ? "medium" : "low") + ")"],
            ["Proto", evt.proto || "-"],
            ["Source", (evt.src_ip || "-") + ":" + (evt.src_port || "?")],
            ["Target", (evt.dest_ip || "-") + ":" + (evt.dest_port || "?")],
            ["Rule", "SID " + (evt.rule_id || "-")],
            ["Stage", evt.stage || "-"],
            ["Handler", evt.handler_username || "-"]
        ].forEach(([k, v]) => {
            const cell = el("div", { class: "stat-card" });
            cell.appendChild(el("div", { class: "label", text: k }));
            cell.appendChild(el("div", { class: "value fs-13",
                                          text: v, style: "font-size:14px" }));
            meta.appendChild(cell);
        });
        body.appendChild(meta);

        body.appendChild(el("div", { class: "fw-600 mt-24 mb-12",
                                      text: "Signature" }));
        body.appendChild(el("pre", {
            class: "code-block", text: evt.rule_msg || "-"
        }));

        if (evt.raw_alert) {
            body.appendChild(el("div", { class: "fw-600 mt-24 mb-12",
                                          text: "Raw EVE-Alert (JSON)" }));
            body.appendChild(el("pre", {
                class: "code-block",
                text: JSON.stringify(evt.raw_alert, null, 2)
            }));
        }

        if (evt.history && evt.history.length > 0) {
            body.appendChild(el("div", { class: "fw-600 mt-24 mb-12",
                                          text: "Bearbeitungsverlauf" }));
            evt.history.forEach(h => {
                const row = el("div", {
                    style: "padding:8px; border-bottom:1px solid var(--border-default)"
                });
                row.innerHTML = '<span class="fs-12 muted">'
                    + escapeHtml(formatTimestamp(h.created_at))
                    + '</span> · <strong>' + escapeHtml(h.username || "system")
                    + '</strong> · ' + escapeHtml(h.action)
                    + (h.note ? '<div class="fs-13">' + escapeHtml(h.note) + '</div>' : "");
                body.appendChild(row);
            });
        }

        // Process-Form
        const processForm = el("div", { class: "mt-24" });
        processForm.appendChild(el("div", { class: "fw-600 mb-12",
                                             text: "Event bearbeiten" }));

        const stageSelect = el("select", { class: "select",
                                            style: "max-width:240px" });
        ["pending", "investigation", "completed"].forEach(s => {
            const opt = el("option", { value: s, text: s });
            if (s === evt.stage) { opt.selected = true; }
            stageSelect.appendChild(opt);
        });

        const noteInput = el("textarea", { class: "input mt-12",
                                            placeholder: "Notiz (optional)" });

        const takeBtn = el("label", { class: "flex items-center gap-8 mt-12" });
        const takeChk = el("input", { type: "checkbox" });
        takeBtn.appendChild(takeChk);
        takeBtn.appendChild(document.createTextNode("Event uebernehmen"));

        processForm.appendChild(el("label", { class: "fs-12 muted",
                                                text: "Neuer Stage" }));
        processForm.appendChild(stageSelect);
        processForm.appendChild(noteInput);
        processForm.appendChild(takeBtn);

        body.appendChild(processForm);

        const saveBtn = el("button", {
            class: "btn btn-primary", text: "Speichern",
            onclick: async () => {
                try {
                    await SGApi.post("/events/" + eventId + "/process", {
                        stage: stageSelect.value,
                        note: noteInput.value,
                        take_ownership: takeChk.checked
                    });
                    SGComponents.toast("Event aktualisiert", "success");
                    SGComponents.closeModal();
                    SGRouter.navigate("events", currentParams);
                } catch (err) {
                    SGComponents.toast("Fehler: " + (err.message ||
                                                     "unbekannt"), "error");
                }
            }
        });
        const closeBtn = el("button", { class: "btn", text: "Schliessen",
                                         onclick: () => SGComponents.closeModal() });

        SGComponents.showModal({
            title: "Event #" + evt.id, body: body,
            footer: [closeBtn, saveBtn], large: true
        });
    }

    let currentParams = {};

    async function render(params) {
        currentParams = Object.assign({}, params || {});
        const content = document.getElementById("app-content");
        clear(content);

        const header = el("div", { class: "page-header" });
        const titleBlock = el("div");
        titleBlock.appendChild(el("div", { class: "page-title",
                                            text: "Event Management" }));
        titleBlock.appendChild(el("div", { class: "page-subtitle",
                                            text: "Suricata-Alerts mit Filter und Stage-Workflow" }));
        header.appendChild(titleBlock);
        content.appendChild(header);

        const toolbarCard = el("div", { class: "card mb-12" });
        const toolbarBody = el("div", {
            class: "card-body",
            style: "display:flex; align-items:center; gap:8px"
        });
        toolbarBody.appendChild(buildToolbar(currentParams, (k, v) => {
            currentParams[k] = v;
            currentParams.offset = 0;
            SGRouter.navigate("events", currentParams);
        }));
        toolbarCard.appendChild(toolbarBody);
        content.appendChild(toolbarCard);

        const tableWrap = el("div", { class: "table-wrap" });
        const loadingRow = el("div", { class: "empty-state",
                                        html: '<div class="spinner"></div>' });
        tableWrap.appendChild(loadingRow);
        content.appendChild(tableWrap);

        const limit = Number(currentParams.limit) || 50;
        const offset = Number(currentParams.offset) || 0;

        let data;
        try {
            data = await SGApi.get("/events", {
                q: currentParams.q,
                stage: currentParams.stage,
                severity: currentParams.severity,
                limit: limit, offset: offset
            });
        } catch (err) {
            clear(tableWrap);
            tableWrap.appendChild(el("div", { class: "empty-state",
                                               text: "Fehler beim Laden." }));
            return;
        }

        clear(tableWrap);
        if (!data.events || data.events.length === 0) {
            tableWrap.appendChild(el("div", { class: "empty-state",
                                               text: "Keine Events gefunden." }));
        } else {
            const table = el("table", { class: "table" });
            const thead = el("thead");
            thead.innerHTML = "<tr>" +
                "<th>EVENT-ID</th>" +
                "<th>TIMESTAMP</th>" +
                "<th>RULE-ID</th>" +
                "<th>SIGNATURE</th>" +
                "<th>SOURCE</th>" +
                "<th>TARGET</th>" +
                "<th>SEVERITY</th>" +
                "<th>STAGE</th>" +
                "<th>HANDLER</th>" +
                '<th class="col-actions">ACTIONS</th>' +
                "</tr>";
            table.appendChild(thead);

            const tbody = el("tbody");
            data.events.forEach(e => {
                const tr = el("tr");
                const idCell = el("td");
                const idLink = el("a", { class: "row-id", href: "#",
                                          text: String(e.id) });
                idLink.addEventListener("click", (ev) => {
                    ev.preventDefault();
                    showEventModal(e.id);
                });
                idCell.appendChild(idLink);
                tr.appendChild(idCell);

                tr.appendChild(el("td", { class: "mono fs-12",
                                            text: formatTimestamp(e.timestamp) }));
                tr.appendChild(el("td", { class: "mono",
                                            text: String(e.rule_id || "-") }));
                tr.appendChild(el("td", { text: e.rule_msg || "-",
                                            style: "max-width:340px" }));
                tr.appendChild(el("td", { class: "mono fs-12",
                                            text: (e.src_ip || "-") + ":"
                                                + (e.src_port || "?") }));
                tr.appendChild(el("td", { class: "mono fs-12",
                                            text: (e.dest_ip || "-") + ":"
                                                + (e.dest_port || "?") }));
                const sevCell = el("td");
                sevCell.appendChild(severityBadge(e.severity));
                tr.appendChild(sevCell);

                const stageCell = el("td");
                stageCell.appendChild(stageBadge(e.stage));
                tr.appendChild(stageCell);

                tr.appendChild(el("td", { text: e.handler_username || "-" }));

                const actionCell = el("td", { class: "col-actions" });
                const procBtn = el("button", { class: "btn-link",
                                                text: "Process",
                                                onclick: () => showEventModal(e.id) });
                actionCell.appendChild(procBtn);
                tr.appendChild(actionCell);

                tbody.appendChild(tr);
            });
            table.appendChild(tbody);
            tableWrap.appendChild(table);
        }

        // Pagination
        const pag = el("div", { class: "pagination" });
        pag.appendChild(el("div", { class: "info",
            text: data.total + " gesamt · Anzeige " +
                  (data.total > 0 ? (offset + 1) : 0) + "-" +
                  Math.min(offset + limit, data.total) }));
        const ctrl = el("div", { class: "controls" });
        const prevBtn = el("button", {
            class: "btn btn-sm", text: "← Zurueck",
            disabled: offset === 0 ? "disabled" : null,
            onclick: () => {
                currentParams.offset = Math.max(0, offset - limit);
                SGRouter.navigate("events", currentParams);
            }
        });
        const nextBtn = el("button", {
            class: "btn btn-sm", text: "Vor →",
            disabled: offset + limit >= data.total ? "disabled" : null,
            onclick: () => {
                currentParams.offset = offset + limit;
                SGRouter.navigate("events", currentParams);
            }
        });
        ctrl.appendChild(prevBtn);
        ctrl.appendChild(nextBtn);
        pag.appendChild(ctrl);
        content.appendChild(pag);
    }

    window.SGPageEvents = render;
}());
