/**
 * SuricataMonitor · Settings Page
 *
 * Vier Tabs:
 *   - Suricata Config (Schema-Form, primaer)
 *   - Raw YAML        (Power-User)
 *   - Service Control
 *   - Benutzer        (admin only)
 */
(function () {
    "use strict";

    const { el, clear, formatTimestamp, escapeHtml, debounce } = SGUtils;

    let currentTab = "form";
    let activeSection = null;       // bei Form-Tab: aktive Section
    let formSchema = null;          // gecachtes Schema
    let formValues = {};            // aktuell angezeigte Werte
    let formDirty = {};             // path -> true falls geaendert
    let cachedInterfaces = null;    // Cache fuer Interface-Dropdown

    function isAdmin() {
        const u = window.SGCurrentUser;
        return u && u.role === "admin";
    }

    // =========================================================================
    // Tab: Suricata Config (Schema-Form)
    // =========================================================================

    async function renderFormTab(container) {
        clear(container);
        container.appendChild(el("div", { class: "empty-state",
            html: '<div class="spinner"></div>' }));

        let data;
        try {
            data = await SGApi.get("/settings/suricata/structured");
        } catch (err) {
            clear(container);
            container.appendChild(el("div", { class: "empty-state",
                text: "Fehler beim Laden: " + (err.message || "") }));
            return;
        }
        if (!data.ok) {
            clear(container);
            container.appendChild(el("div", { class: "empty-state",
                text: "Fehler: " + (data.error || "unbekannt") }));
            return;
        }
        formSchema = data.schema;
        formValues = JSON.parse(JSON.stringify(data.values));
        formDirty = {};

        clear(container);

        // Warnungen (Edge-Case-Sections in YAML)
        if (data.warnings && data.warnings.length > 0) {
            const warn = el("div", { class: "card mb-12",
                style: "background: var(--color-warning-bg); "
                       + "border-color: var(--color-warning); color: "
                       + "var(--color-warning-text)" });
            const wb = el("div", { class: "card-body" });
            wb.appendChild(el("div", { class: "fw-600 mb-12",
                text: "Hinweis zu fortgeschrittenen Konfigurations-Sektionen" }));
            data.warnings.forEach(w => {
                wb.appendChild(el("div", { class: "fs-13", text: "• " + w }));
            });
            warn.appendChild(wb);
            container.appendChild(warn);
        }

        if (data.phimantec_notice) {
            const profileInfo = el("div", { class: "warning-banner mb-12" });
            profileInfo.textContent = data.phimantec_notice;
            container.appendChild(profileInfo);
        }

        // Info-Banner: Form schreibt YAML neu, Kommentare gehen verloren
        const info = el("div", { class: "info-banner mb-12" });
        info.innerHTML =
            '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" '
            + 'stroke="currentColor" stroke-width="2" '
            + 'stroke-linecap="round" stroke-linejoin="round" '
            + 'style="flex-shrink:0">'
            + '<circle cx="12" cy="12" r="10"/>'
            + '<line x1="12" y1="16" x2="12" y2="12"/>'
            + '<line x1="12" y1="8" x2="12.01" y2="8"/></svg>'
            + '<div><strong>Form-Editor</strong>: Speichern schreibt die '
            + 'ganze YAML neu. Handgepflegte Kommentare gehen dabei verloren '
            + '(Backup wird automatisch angelegt). Fuer Edge-Cases den '
            + '<em>Raw YAML</em>-Tab nutzen.</div>';
        container.appendChild(info);

        // Hauptlayout: links Sections-Liste (mit Gruppen-Headers), rechts Form
        const layout = el("div", { class: "settings-layout" });

        const sidebar = el("div", { class: "settings-sidebar" });
        const groups = formSchema.groups || [
            {"key": "_default", "label": ""}
        ];
        const groupMap = {};
        groups.forEach(g => { groupMap[g.key] = g; });

        // Sections nach Gruppe ordnen, in der Reihenfolge der groups[]
        const sectionsByGroup = {};
        formSchema.sections.forEach(sec => {
            const g = sec.group || "_default";
            if (!sectionsByGroup[g]) { sectionsByGroup[g] = []; }
            sectionsByGroup[g].push(sec);
        });

        // Erste Section ist Default, falls keine aktive gesetzt
        if (activeSection === null
            && formSchema.sections.length > 0) {
            activeSection = formSchema.sections[0].key;
        }

        groups.forEach(group => {
            const groupSections = sectionsByGroup[group.key] || [];
            if (groupSections.length === 0) { return; }

            // Gruppen-Header (nicht klickbar)
            if (group.label) {
                const header = el("div", { class: "settings-group-header",
                    text: group.label.toUpperCase() });
                if (group.help) {
                    header.title = group.help;
                }
                sidebar.appendChild(header);
            }

            groupSections.forEach(sec => {
                const a = el("a", {
                    class: "settings-nav-item",
                    href: "#",
                    "data-section-key": sec.key,
                    onclick: (ev) => {
                        ev.preventDefault();
                        activeSection = sec.key;
                        renderActiveSection();
                    }
                });
                a.appendChild(el("span", { class: "settings-nav-label",
                    text: sec.label }));
                if (activeSection === sec.key) {
                    a.classList.add("active");
                }
                sidebar.appendChild(a);
            });
        });
        layout.appendChild(sidebar);

        const main = el("div", { class: "settings-main", id: "settings-main" });
        layout.appendChild(main);
        container.appendChild(layout);

        // Save-Bar unten
        const saveBar = el("div", { class: "save-bar", id: "save-bar" });
        const dirtyCount = el("span", { class: "dirty-count",
            id: "dirty-count", text: "0 Aenderungen" });
        const resetBtn = el("button", { class: "btn", text: "Zuruecksetzen",
            onclick: () => renderFormTab(container) });
        const saveBtn = el("button", { class: "btn btn-primary",
            text: "Aenderungen speichern",
            onclick: () => saveFormChanges(container) });
        saveBar.appendChild(dirtyCount);
        saveBar.appendChild(resetBtn);
        saveBar.appendChild(saveBtn);
        container.appendChild(saveBar);

        renderActiveSection();
    }

    function renderActiveSection() {
        const main = document.getElementById("settings-main");
        if (!main || !formSchema) { return; }
        clear(main);
        // Sidebar-Active-Highlight aktualisieren
        document.querySelectorAll(".settings-nav-item").forEach(node => {
            const key = node.getAttribute("data-section-key");
            if (key === activeSection) { node.classList.add("active"); }
            else { node.classList.remove("active"); }
        });

        const section = formSchema.sections.find(s => s.key === activeSection);
        if (!section) { return; }

        main.appendChild(el("h2", { class: "section-title",
            text: section.label }));
        if (section.description) {
            main.appendChild(el("div", { class: "section-description",
                text: section.description }));
        }

        section.fields.forEach(field => {
            main.appendChild(buildField(field));
        });
    }

    function buildField(field) {
        const wrap = el("div", { class: "field-row" });
        const lblWrap = el("div", { class: "field-label-wrap" });
        lblWrap.appendChild(el("label", { class: "field-label",
            text: field.label }));
        if (field.help) {
            lblWrap.appendChild(el("div", { class: "field-help",
                text: field.help }));
        }
        if (field.phimantec_warning) {
            lblWrap.appendChild(el("div", {
                class: "field-warning",
                text: "PHIMANTEC-Hinweis: " + field.phimantec_warning
            }));
        }
        wrap.appendChild(lblWrap);

        const inputWrap = el("div", { class: "field-input-wrap" });
        const value = formValues[field.path];
        let inputEl;

        if (field.type === "bool") {
            inputEl = buildBoolInput(field, value);
        } else if (field.type === "enum") {
            inputEl = buildEnumInput(field, value);
        } else if (field.type === "interface-select") {
            inputEl = buildInterfaceInput(field, value);
        } else if (field.type === "int") {
            inputEl = buildIntInput(field, value);
        } else if (field.type === "range") {
            inputEl = buildRangeInput(field, value);
        } else if (field.type === "range-or-auto") {
            inputEl = buildRangeOrAutoInput(field, value);
        } else if (field.type === "list-of-strings") {
            inputEl = buildListOfStrings(field, value);
        } else if (field.type === "list-of-objects") {
            inputEl = buildListOfObjects(field, value);
        } else if (field.type === "text") {
            inputEl = buildTextArea(field, value);
        } else {
            inputEl = buildStringInput(field, value);
        }

        inputWrap.appendChild(inputEl);
        wrap.appendChild(inputWrap);
        return wrap;
    }

    function markDirty(path) {
        formDirty[path] = true;
        const count = Object.keys(formDirty).length;
        const dc = document.getElementById("dirty-count");
        if (dc) {
            dc.textContent = count + " Aenderung" + (count !== 1 ? "en" : "");
            dc.classList.toggle("has-changes", count > 0);
        }
    }

    async function loadInterfaces() {
        if (cachedInterfaces !== null) { return cachedInterfaces; }
        try {
            const res = await SGApi.get("/system/interfaces");
            cachedInterfaces = res.interfaces || [];
        } catch (err) {
            console.warn("Interface-Liste konnte nicht geladen werden:", err);
            cachedInterfaces = [];
        }
        return cachedInterfaces;
    }

    function buildInterfaceInput(field, value) {
        // HTML5 datalist: Dropdown + freier Text moeglich
        const wrap = el("div", { style: "flex:1; max-width:420px;"
                                       + "position:relative" });
        const inp = el("input", {
            class: "input", type: "text",
            value: value !== null && value !== undefined ? String(value) : "",
            placeholder: field.placeholder || "z.B. ens18",
            "list": "iflist-" + Math.random().toString(36).slice(2, 10),
        });
        if (!isAdmin()) { inp.disabled = true; }
        const datalist = el("datalist", { id: inp.getAttribute("list") });
        wrap.appendChild(inp);
        wrap.appendChild(datalist);

        // asynchron Optionen nachladen
        loadInterfaces().then(ifaces => {
            ifaces.forEach(iface => {
                const opt = el("option", { value: iface.name });
                opt.textContent = iface.name + " ("
                    + (iface.state || "?") + ")";
                datalist.appendChild(opt);
            });
        });

        inp.addEventListener("input", () => {
            formValues[field.path] = inp.value;
            markDirty(field.path);
        });
        return wrap;
    }

    function buildBoolInput(field, value) {
        const lbl = el("label", { class: "switch" });
        const inp = el("input", { type: "checkbox" });
        if (value === true || value === "yes" || value === "true") {
            inp.checked = true;
        }
        if (!isAdmin()) { inp.disabled = true; }
        const slider = el("span", { class: "slider" });
        lbl.appendChild(inp);
        lbl.appendChild(slider);
        inp.addEventListener("change", () => {
            formValues[field.path] = inp.checked;
            markDirty(field.path);
        });
        return lbl;
    }

    function buildEnumInput(field, value) {
        const sel = el("select", { class: "select" });
        if (!isAdmin()) { sel.disabled = true; }
        const cur = (value !== null && value !== undefined)
            ? String(value) : (field.default || "");
        (field.options || []).forEach(opt => {
            const o = el("option", { value: opt,
                text: opt === "" ? "(leer)" : opt });
            if (String(opt) === cur) { o.selected = true; }
            sel.appendChild(o);
        });
        sel.addEventListener("change", () => {
            formValues[field.path] = sel.value;
            markDirty(field.path);
        });
        return sel;
    }

    function buildIntInput(field, value) {
        const inp = el("input", {
            class: "input", type: "number",
            value: value !== null && value !== undefined
                ? String(value) : String(field.default || ""),
        });
        if (field.min !== undefined) { inp.min = String(field.min); }
        if (field.max !== undefined) { inp.max = String(field.max); }
        if (!isAdmin()) { inp.disabled = true; }
        inp.addEventListener("input", () => {
            const n = Number(inp.value);
            formValues[field.path] = isNaN(n) ? field.default : n;
            markDirty(field.path);
        });
        return inp;
    }

    function buildStringInput(field, value) {
        const inp = el("input", {
            class: "input", type: "text",
            value: value !== null && value !== undefined
                ? String(value) : "",
        });
        if (field.placeholder) { inp.placeholder = field.placeholder; }
        if (!isAdmin()) { inp.disabled = true; }
        inp.addEventListener("input", () => {
            formValues[field.path] = inp.value;
            markDirty(field.path);
        });
        return inp;
    }

    function buildRangeInput(field, value) {
        return buildRangeControl(field, value, val => {
            formValues[field.path] = val;
            markDirty(field.path);
        });
    }

    function buildRangeControl(field, value, onChange) {
        const wrap = el("div", { style: "display:flex; gap:10px; "
                                      + "align-items:center; max-width:520px" });
        const min = field.min !== undefined ? Number(field.min) : 0;
        const max = field.max !== undefined ? Number(field.max) : 100;
        const step = field.step !== undefined ? Number(field.step) : 1;
        let cur = Number(value !== null && value !== undefined ? value
                                                               : field.default);
        if (isNaN(cur)) { cur = Number(field.default || min); }
        const slider = el("input", { type: "range", min: String(min),
            max: String(max), step: String(step), value: String(cur),
            style: "flex:1" });
        const num = el("input", { class: "input", type: "number",
            min: String(min), max: String(max), step: String(step),
            value: String(cur), style: "max-width:110px" });
        if (!isAdmin()) { slider.disabled = true; num.disabled = true; }
        function setValue(raw) {
            const n = Number(raw);
            const v = isNaN(n) ? Number(field.default || min) : n;
            slider.value = String(v);
            num.value = String(v);
            onChange(v);
        }
        slider.addEventListener("input", () => setValue(slider.value));
        num.addEventListener("input", () => setValue(num.value));
        wrap.appendChild(slider);
        wrap.appendChild(num);
        return wrap;
    }

    function buildRangeOrAutoInput(field, value) {
        return buildRangeOrAutoControl(field, value, val => {
            formValues[field.path] = val;
            markDirty(field.path);
        });
    }

    function buildRangeOrAutoControl(field, value, onChange) {
        const wrap = el("div", { style: "display:flex; gap:10px; "
                                      + "align-items:center; max-width:620px" });
        const min = field.min !== undefined ? Number(field.min) : 1;
        const max = field.max !== undefined ? Number(field.max) : 16;
        const step = field.step !== undefined ? Number(field.step) : 1;
        const auto = value === undefined || value === null || String(value) === "auto";
        let cur = auto ? min : Number(value);
        if (isNaN(cur)) { cur = min; }
        const autoLabel = el("label", { style: "display:flex; gap:6px; "
                                             + "align-items:center" });
        const autoBox = el("input", { type: "checkbox", checked: auto });
        autoLabel.appendChild(autoBox);
        autoLabel.appendChild(el("span", { class: "fs-12", text: "auto" }));
        const slider = el("input", { type: "range", min: String(min),
            max: String(max), step: String(step), value: String(cur),
            style: "flex:1" });
        const num = el("input", { class: "input", type: "number",
            min: String(min), max: String(max), step: String(step),
            value: String(cur), style: "max-width:90px" });
        function syncDisabled() {
            slider.disabled = !isAdmin() || autoBox.checked;
            num.disabled = !isAdmin() || autoBox.checked;
            autoBox.disabled = !isAdmin();
        }
        function emitValue() {
            if (autoBox.checked) {
                onChange("auto");
                syncDisabled();
                return;
            }
            const n = Number(num.value);
            const v = isNaN(n) ? min : n;
            slider.value = String(v);
            num.value = String(v);
            onChange(v);
            syncDisabled();
        }
        autoBox.addEventListener("change", emitValue);
        slider.addEventListener("input", () => {
            num.value = slider.value;
            emitValue();
        });
        num.addEventListener("input", emitValue);
        syncDisabled();
        wrap.appendChild(autoLabel);
        wrap.appendChild(slider);
        wrap.appendChild(num);
        return wrap;
    }

    function buildTextArea(field, value) {
        const ta = el("textarea", {
            class: "input", style: "min-height: 80px",
            value: value !== null && value !== undefined
                ? String(value) : "",
        });
        if (!isAdmin()) { ta.disabled = true; }
        ta.addEventListener("input", () => {
            formValues[field.path] = ta.value;
            markDirty(field.path);
        });
        return ta;
    }

    function buildListOfStrings(field, value) {
        const wrap = el("div", { class: "list-of-strings" });
        let items = [];
        if (Array.isArray(value)) { items = value.slice(); }
        else if (typeof value === "string") {
            // YAML kann Strings wie "[a,b,c]" liefern
            const v = value.trim();
            if (v.startsWith("[") && v.endsWith("]")) {
                items = v.slice(1, -1).split(",").map(s => s.trim());
            } else if (v.length > 0) {
                items = [v];
            }
        }

        function refresh() {
            clear(wrap);
            items.forEach((item, idx) => {
                const row = el("div", { class: "los-row" });
                const inp = el("input", { class: "input", type: "text",
                    value: String(item) });
                inp.addEventListener("input", () => {
                    items[idx] = inp.value;
                    formValues[field.path] = items.slice();
                    markDirty(field.path);
                });
                row.appendChild(inp);
                if (isAdmin()) {
                    const rm = el("button", { class: "btn btn-sm",
                        type: "button", text: "✕",
                        onclick: () => {
                            items.splice(idx, 1);
                            formValues[field.path] = items.slice();
                            markDirty(field.path);
                            refresh();
                        }
                    });
                    row.appendChild(rm);
                }
                wrap.appendChild(row);
            });
            if (isAdmin()) {
                const add = el("button", { class: "btn btn-sm",
                    type: "button", text: "+ Eintrag hinzufuegen",
                    onclick: () => {
                        items.push("");
                        formValues[field.path] = items.slice();
                        markDirty(field.path);
                        refresh();
                    }
                });
                wrap.appendChild(add);
            }
        }
        refresh();
        return wrap;
    }

    function buildListOfObjects(field, value) {
        const wrap = el("div", { class: "list-of-objects" });
        let items = Array.isArray(value) ? value.slice() : [];

        function refresh() {
            clear(wrap);
            items.forEach((item, idx) => {
                const card = el("div", { class: "loo-card" });
                const cardHeader = el("div", { class: "loo-card-header" });
                cardHeader.appendChild(el("div", { class: "fw-600 fs-13",
                    text: "Eintrag #" + (idx + 1)
                        + (item.interface ? " · " + item.interface : "") }));
                if (isAdmin()) {
                    const rm = el("button", { class: "btn btn-sm btn-danger",
                        type: "button", text: "Loeschen",
                        onclick: () => {
                            if (confirm("Eintrag wirklich loeschen?")) {
                                items.splice(idx, 1);
                                formValues[field.path] = items.slice();
                                markDirty(field.path);
                                refresh();
                            }
                        }
                    });
                    cardHeader.appendChild(rm);
                }
                card.appendChild(cardHeader);

                (field.item_fields || []).forEach(subField => {
                    const sub = buildSubField(subField, item, (key, val) => {
                        item[key] = val;
                        formValues[field.path] = items.slice();
                        markDirty(field.path);
                    });
                    card.appendChild(sub);
                });
                wrap.appendChild(card);
            });
            if (isAdmin()) {
                const add = el("button", { class: "btn", type: "button",
                    text: "+ Eintrag hinzufuegen",
                    onclick: () => {
                        const newItem = {};
                        (field.item_fields || []).forEach(f => {
                            if (f.default !== undefined) {
                                newItem[f.path] = f.default;
                            }
                        });
                        items.push(newItem);
                        formValues[field.path] = items.slice();
                        markDirty(field.path);
                        refresh();
                    }
                });
                wrap.appendChild(add);
            }
        }
        refresh();
        return wrap;
    }

    function buildSubField(field, parentItem, onChange) {
        const row = el("div", { class: "field-row sub-field" });
        const lblWrap = el("div", { class: "field-label-wrap" });
        lblWrap.appendChild(el("label", { class: "field-label",
            text: field.label || field.path }));
        if (field.help) {
            lblWrap.appendChild(el("div", { class: "field-help",
                text: field.help }));
        }
        if (field.phimantec_warning) {
            lblWrap.appendChild(el("div", {
                class: "field-warning",
                text: "PHIMANTEC-Hinweis: " + field.phimantec_warning
            }));
        }
        row.appendChild(lblWrap);
        const inpWrap = el("div", { class: "field-input-wrap" });
        const cur = parentItem[field.path];

        let inp;
        if (field.type === "bool") {
            const lbl = el("label", { class: "switch" });
            inp = el("input", { type: "checkbox" });
            if (cur === true || cur === "yes" || cur === "true") {
                inp.checked = true;
            }
            if (!isAdmin()) { inp.disabled = true; }
            const slider = el("span", { class: "slider" });
            lbl.appendChild(inp); lbl.appendChild(slider);
            inp.addEventListener("change", () => onChange(field.path, inp.checked));
            inpWrap.appendChild(lbl);
        } else if (field.type === "enum") {
            inp = el("select", { class: "select" });
            if (!isAdmin()) { inp.disabled = true; }
            (field.options || []).forEach(opt => {
                const o = el("option", { value: opt, text: opt });
                if (String(opt) === String(cur)) { o.selected = true; }
                inp.appendChild(o);
            });
            inp.addEventListener("change", () => onChange(field.path, inp.value));
            inpWrap.appendChild(inp);
        } else if (field.type === "interface-select") {
            // Wie buildInterfaceInput, aber gegen parentItem callback
            const wrap = el("div", { style: "position:relative" });
            const dlId = "iflist-sub-" +
                Math.random().toString(36).slice(2, 10);
            inp = el("input", { class: "input", type: "text",
                value: cur !== undefined ? String(cur) : "",
                placeholder: field.placeholder || "z.B. ens18",
                "list": dlId });
            const dl = el("datalist", { id: dlId });
            wrap.appendChild(inp); wrap.appendChild(dl);
            loadInterfaces().then(ifaces => {
                ifaces.forEach(iface => {
                    const opt = el("option", { value: iface.name });
                    opt.textContent = iface.name + " ("
                        + (iface.state || "?") + ")";
                    dl.appendChild(opt);
                });
            });
            if (!isAdmin()) { inp.disabled = true; }
            inp.addEventListener("input", () =>
                onChange(field.path, inp.value));
            inpWrap.appendChild(wrap);
        } else if (field.type === "int") {
            inp = el("input", { class: "input", type: "number",
                value: cur !== undefined ? String(cur)
                    : String(field.default || "") });
            if (field.min !== undefined) { inp.min = String(field.min); }
            if (field.max !== undefined) { inp.max = String(field.max); }
            if (!isAdmin()) { inp.disabled = true; }
            inp.addEventListener("input", () => {
                const n = Number(inp.value);
                onChange(field.path, isNaN(n) ? field.default : n);
            });
            inpWrap.appendChild(inp);
        } else if (field.type === "range-or-auto") {
            inpWrap.appendChild(buildRangeOrAutoControl(field, cur,
                val => onChange(field.path, val)));
        } else if (field.type === "range") {
            inpWrap.appendChild(buildRangeControl(field, cur,
                val => onChange(field.path, val)));
        } else {
            inp = el("input", { class: "input", type: "text",
                value: cur !== undefined ? String(cur) : "" });
            if (field.placeholder) { inp.placeholder = field.placeholder; }
            if (!isAdmin()) { inp.disabled = true; }
            inp.addEventListener("input", () => onChange(field.path, inp.value));
            inpWrap.appendChild(inp);
        }
        row.appendChild(inpWrap);
        return row;
    }

    async function saveFormChanges(container) {
        const changes = {};
        Object.keys(formDirty).forEach(p => { changes[p] = formValues[p]; });
        if (Object.keys(changes).length === 0) {
            SGComponents.toast("Keine Aenderungen", "warning");
            return;
        }
        const ok = await SGComponents.confirmDialog({
            title: "Konfiguration schreiben",
            message: Object.keys(changes).length
                + " Aenderung(en) anwenden? Suricata -T wird vorab "
                + "ausgefuehrt. Bei Erfolg wird die Datei ueberschrieben "
                + "(Backup wird automatisch angelegt).",
            confirmText: "Speichern"
        });
        if (!ok) { return; }
        try {
            const res = await SGApi.post("/settings/suricata/structured",
                { updates: changes });
            if (res.ok) {
                const restartNeeded = Object.keys(changes).some(p =>
                    p === "mpm-algo" || p === "spm-algo"
                    || p === "runmode" || p.indexOf("af-packet") === 0);
                SGComponents.toast(
                    "Gespeichert und persistiert: " + res.applied
                    + " Aenderungen. Backup: " + (res.backup || "-")
                    + (restartNeeded
                        ? " Runtime-Aenderung: Container-Neustart erforderlich."
                        : ""),
                    "success", restartNeeded ? 9000 : 6000);
                formDirty = {};
                renderFormTab(container);
            } else {
                SGComponents.toast("Fehler: " +
                    (res.error || res.details || ""),
                    "error", 8000);
            }
        } catch (err) {
            SGComponents.toast("Fehler: " + (err.message || ""), "error");
        }
    }

    // =========================================================================
    // Tab: Raw YAML
    // =========================================================================

    async function renderYamlTab(container) {
        clear(container);
        container.appendChild(el("div", { class: "empty-state",
            html: '<div class="spinner"></div>' }));

        let data;
        try { data = await SGApi.get("/settings/suricata"); }
        catch (err) {
            clear(container);
            container.appendChild(el("div", { class: "empty-state",
                text: "Fehler beim Laden." }));
            return;
        }
        clear(container);

        const card = el("div", { class: "card" });
        const ch = el("div", { class: "card-header" });
        ch.appendChild(el("div", { class: "card-title",
            text: "Raw YAML-Editor" }));
        ch.appendChild(el("span", { class: "fs-12 muted mono",
            text: data.path || "?" }));
        card.appendChild(ch);

        const cb = el("div", { class: "card-body" });
        cb.appendChild(el("div", { class: "muted fs-12 mb-12",
            text: "Volle Kontrolle inklusive Kommentare. Der Editor ist "
                  + "standardmaessig schreibgeschuetzt; Schreiben muss "
                  + "bewusst aktiviert werden und triggert suricata -T." }));

        const guard = el("div", { class: "info-banner mb-12" });
        const writeEnabled = el("input", { type: "checkbox" });
        const editLabel = el("label", { class: "switch" }, [
            writeEnabled,
            el("span", { class: "slider" })
        ]);
        const guardText = el("div", { class: "fs-13",
            text: "Raw YAML schreiben aktivieren" });
        guard.appendChild(editLabel);
        guard.appendChild(guardText);
        if (isAdmin()) { cb.appendChild(guard); }

        const ta = el("textarea", { class: "input",
            style: "min-height:520px; font-family: var(--font-mono); "
                + "font-size:12px", readOnly: true });
        ta.value = data.content || "";
        cb.appendChild(ta);

        if (isAdmin()) {
            const actions = el("div", { class: "form-actions" });
            const valBtn = el("button", { class: "btn",
                text: "Validieren (suricata -T)",
                onclick: async () => {
                    valBtn.disabled = true;
                    valBtn.innerHTML = '<span class="spinner"></span>';
                    try {
                        const res = await SGApi.post(
                            "/settings/suricata/validate", {});
                        SGComponents.toast(
                            res.ok ? "Config valide"
                                : "Validation fehlgeschlagen: "
                                + (res.message || ""),
                            res.ok ? "success" : "error", 6000);
                    } catch (err) {
                        SGComponents.toast("Fehler: " +
                            (err.message || ""), "error");
                    } finally {
                        valBtn.disabled = false;
                        valBtn.textContent = "Validieren (suricata -T)";
                    }
                }
            });
            const saveBtn = el("button", { class: "btn btn-primary",
                text: "Speichern", disabled: true,
                onclick: async () => {
                    if (ta.readOnly) {
                        SGComponents.toast("Raw YAML ist noch schreibgeschuetzt", "warning");
                        return;
                    }
                    const ok = await SGComponents.confirmDialog({
                        title: "YAML speichern",
                        message: "suricata -T -Validierung laeuft vor "
                                + "dem Schreiben. Bei Erfolg wird die "
                                + "Datei ueberschrieben (Backup "
                                + "automatisch). Fortfahren?",
                        confirmText: "Speichern" });
                    if (!ok) { return; }
                    saveBtn.disabled = true;
                    saveBtn.innerHTML = '<span class="spinner"></span>';
                    try {
                        const res = await SGApi.post("/settings/suricata",
                            { content: ta.value });
                        if (res.ok) {
                            SGComponents.toast("Gespeichert. Backup: "
                                + (res.backup || "-"), "success", 5000);
                        } else {
                            SGComponents.toast("Fehler: "
                                + (res.error || res.details || ""),
                                "error", 8000);
                        }
                    } catch (err) {
                        SGComponents.toast("Fehler: " +
                            (err.message || ""), "error");
                    } finally {
                        saveBtn.disabled = false;
                        saveBtn.textContent = "Speichern";
                    }
                }
            });
            writeEnabled.addEventListener("change", () => {
                ta.readOnly = !writeEnabled.checked;
                saveBtn.disabled = !writeEnabled.checked;
                guardText.textContent = writeEnabled.checked
                    ? "Raw YAML ist schreibbar. Bitte vorsichtig speichern."
                    : "Raw YAML schreiben aktivieren";
            });
            actions.appendChild(valBtn);
            actions.appendChild(saveBtn);
            cb.appendChild(actions);
        } else {
            cb.appendChild(el("div", { class: "muted fs-12 mt-12",
                text: "Nur Administratoren koennen die Konfiguration aendern." }));
        }
        card.appendChild(cb);
        container.appendChild(card);
    }

    // =========================================================================
    // Tab: Service Control
    // =========================================================================

    async function renderServiceTab(container) {
        clear(container);
        container.appendChild(el("div", { class: "empty-state",
            html: '<div class="spinner"></div>' }));

        let status;
        try { status = await SGApi.get("/settings/service/status"); }
        catch (err) {
            clear(container);
            container.appendChild(el("div", { class: "empty-state",
                text: "Fehler." }));
            return;
        }

        clear(container);
        const statusCard = el("div", { class: "stat-card" });
        statusCard.appendChild(el("div", { class: "label",
            text: "Suricata Service" }));
        statusCard.appendChild(el("div", { class: "value",
            text: status.status || "unknown" }));
        statusCard.appendChild(el("div", { class: "delta",
            text: status.message || "" }));
        container.appendChild(statusCard);

        if (!isAdmin()) {
            container.appendChild(el("div", { class: "muted fs-12 mt-12",
                text: "Nur Administratoren koennen den Service steuern." }));
            return;
        }

        const actionCard = el("div", { class: "card mt-24" });
        actionCard.appendChild(el("div", { class: "card-header" }, [
            el("div", { class: "card-title", text: "Service-Aktionen" })
        ]));
        const cb = el("div", { class: "card-body" });
        cb.appendChild(el("div", { class: "mb-12",
            text: "ruleset-reload-rules: testet die Suricata-Konfiguration "
                  + "und aktiviert geaenderte Regeln ohne Service-Restart." }));
        const reloadBtn = el("button", { class: "btn btn-primary",
            text: "ruleset-reload-rules",
            onclick: async () => {
                reloadBtn.disabled = true;
                reloadBtn.innerHTML = '<span class="spinner"></span>';
                try {
                    const res = await SGApi.post("/settings/service",
                        { action: "reload" });
                    SGComponents.toast(
                        res.return === "OK" || res.ok
                            ? "Rules neu geladen"
                            : ("Fehler: " + (res.stage ? res.stage + ": " : "") + (res.message || "")),
                        res.return === "OK" || res.ok
                            ? "success" : "error");
                } catch (err) {
                    SGComponents.toast("Fehler: " +
                        (err.message || ""), "error");
                } finally {
                    reloadBtn.disabled = false;
                    reloadBtn.textContent = "ruleset-reload-rules";
                }
            }
        });
        cb.appendChild(reloadBtn);

        cb.appendChild(el("div", { class: "mt-24 mb-12 muted",
            text: "restart: vollstaendiger Service-Neustart. Kurze "
                  + "Aufnahme-Unterbrechung." }));
        const restartBtn = el("button", { class: "btn btn-danger",
            text: "Service restart",
            onclick: async () => {
                const ok = await SGComponents.confirmDialog({
                    title: "Suricata neu starten",
                    message: "Wirklich Service-Restart ausfuehren?",
                    confirmText: "Restart", danger: true });
                if (!ok) { return; }
                restartBtn.disabled = true;
                restartBtn.innerHTML = '<span class="spinner"></span>';
                try {
                    const res = await SGApi.post("/settings/service",
                        { action: "restart" });
                    const success = res.return === "OK" || res.ok;
                    SGComponents.toast(
                        success
                            ? (res.requires_container_restart
                                ? (res.message || "Container-Neustart erforderlich")
                                : "Service neu gestartet")
                            : ("Fehler: " + (res.message || "")),
                        success ? (res.requires_container_restart ? "warning" : "success") : "error",
                        res.requires_container_restart ? 9000 : 5000);
                } catch (err) {
                    SGComponents.toast("Fehler: " +
                        (err.message || ""), "error");
                } finally {
                    restartBtn.disabled = false;
                    restartBtn.textContent = "Service restart";
                }
            }
        });
        cb.appendChild(restartBtn);

        actionCard.appendChild(cb);
        container.appendChild(actionCard);
    }

    // =========================================================================
    // Tab: Users
    // =========================================================================

    async function renderUsersTab(container) {
        clear(container);
        if (!isAdmin()) {
            container.appendChild(el("div", { class: "empty-state",
                text: "Nur Administratoren sehen das User-Management." }));
            return;
        }
        container.appendChild(el("div", { class: "empty-state",
            html: '<div class="spinner"></div>' }));

        let data;
        try { data = await SGApi.get("/users"); }
        catch (err) {
            clear(container);
            container.appendChild(el("div", { class: "empty-state",
                text: "Fehler beim Laden." }));
            return;
        }
        clear(container);

        const top = el("div", { class: "card mb-12" });
        const tb = el("div", { class: "card-body",
            style: "display:flex; align-items:center; gap:8px" });
        tb.appendChild(el("div", { style: "flex:1",
            text: data.users.length + " Benutzer · Rollen: admin / "
                  + "analyst / viewer" }));
        const addBtn = el("button", { class: "btn btn-primary",
            text: "+ Neuer Benutzer",
            onclick: () => showUserForm(null, () =>
                renderUsersTab(container)) });
        tb.appendChild(addBtn);
        top.appendChild(tb);
        container.appendChild(top);

        const table = el("table", { class: "table" });
        const thead = el("thead");
        thead.innerHTML = "<tr><th>USERNAME</th><th>NAME</th>"
            + "<th>EMAIL</th><th>ROLLE</th><th>STATUS</th>"
            + "<th>LETZTES LOGIN</th>"
            + '<th class="col-actions">AKTION</th></tr>';
        table.appendChild(thead);
        const tbody = el("tbody");
        data.users.forEach(u => {
            const tr = el("tr");
            tr.appendChild(el("td", { class: "fw-600 mono", text: u.username }));
            tr.appendChild(el("td", { text: u.full_name || "-" }));
            tr.appendChild(el("td", { class: "fs-12", text: u.email || "-" }));
            const roleClass = u.role === "admin" ? "badge-info"
                            : u.role === "analyst" ? "badge-warning"
                            : "badge-neutral";
            tr.appendChild(el("td", null, [
                el("span", { class: "badge " + roleClass, text: u.role })
            ]));
            tr.appendChild(el("td", null, [
                el("span", { class: "badge " + (u.active ? "badge-success"
                                                          : "badge-danger"),
                    text: u.active ? "aktiv" : "deaktiviert" })
            ]));
            tr.appendChild(el("td", { class: "fs-12",
                text: u.last_login_at ? formatTimestamp(u.last_login_at) : "-" }));
            const act = el("td", { class: "col-actions" });
            act.appendChild(el("button", { class: "btn-link", text: "Edit",
                onclick: () => showUserForm(u, () =>
                    renderUsersTab(container)) }));
            act.appendChild(el("button", { class: "btn-link", text: "Passwort",
                onclick: () => showPasswordResetForm(u) }));
            const me = window.SGCurrentUser;
            if (!me || me.id !== u.id) {
                act.appendChild(el("button", { class: "btn-link",
                    style: "color: var(--color-danger)", text: "Loeschen",
                    onclick: async () => {
                        const ok = await SGComponents.confirmDialog({
                            title: "Benutzer loeschen",
                            message: 'Wirklich "' + u.username + '" loeschen?',
                            confirmText: "Loeschen", danger: true });
                        if (!ok) { return; }
                        try {
                            await SGApi.delete("/users/" + u.id);
                            SGComponents.toast("Benutzer geloescht", "success");
                            renderUsersTab(container);
                        } catch (err) {
                            SGComponents.toast("Fehler: " +
                                (err.message || ""), "error");
                        }
                    }
                }));
            }
            tr.appendChild(act);
            tbody.appendChild(tr);
        });
        table.appendChild(tbody);
        container.appendChild(el("div", { class: "table-wrap" }, [table]));
    }

    function showUserForm(existing, onSaved) {
        const body = el("div");
        const usernameInput = el("input", { class: "input",
            value: existing ? existing.username : "",
            disabled: existing ? "disabled" : null,
            placeholder: "Benutzername" });
        const fullNameInput = el("input", { class: "input",
            value: existing && existing.full_name || "",
            placeholder: "Vollstaendiger Name" });
        const emailInput = el("input", { class: "input", type: "email",
            value: existing && existing.email || "",
            placeholder: "E-Mail" });
        const roleSelect = el("select", { class: "select" });
        ["admin", "analyst", "viewer"].forEach(r => {
            const opt = el("option", { value: r, text: r });
            if (existing && existing.role === r) { opt.selected = true; }
            roleSelect.appendChild(opt);
        });
        const activeChk = el("input", { type: "checkbox" });
        if (!existing || existing.active) { activeChk.checked = true; }
        const pwInput = el("input", { class: "input", type: "password",
            placeholder: existing ? "(unveraendert lassen)"
                                   : "Passwort (min. 8 Zeichen)" });

        function row(label, node) {
            const r = el("div", { class: "form-row" });
            r.appendChild(el("label", { text: label }));
            r.appendChild(node);
            return r;
        }
        body.appendChild(row("Benutzername", usernameInput));
        body.appendChild(row("Vollstaendiger Name", fullNameInput));
        body.appendChild(row("E-Mail", emailInput));
        body.appendChild(row("Rolle", roleSelect));
        const activeRow = row("Aktiv", activeChk);
        activeRow.style.flexDirection = "row";
        activeRow.style.alignItems = "center";
        activeRow.style.gap = "10px";
        body.appendChild(activeRow);
        if (!existing) {
            body.appendChild(row("Passwort", pwInput));
        }

        const cancelBtn = el("button", { class: "btn", text: "Abbrechen",
            onclick: () => SGComponents.closeModal() });
        const saveBtn = el("button", { class: "btn btn-primary",
            text: "Speichern",
            onclick: async () => {
                try {
                    if (existing) {
                        await SGApi.patch("/users/" + existing.id, {
                            full_name: fullNameInput.value,
                            email: emailInput.value,
                            role: roleSelect.value,
                            active: activeChk.checked
                        });
                        SGComponents.toast("Benutzer aktualisiert", "success");
                    } else {
                        await SGApi.post("/users", {
                            username: usernameInput.value.trim(),
                            password: pwInput.value,
                            full_name: fullNameInput.value,
                            email: emailInput.value,
                            role: roleSelect.value
                        });
                        SGComponents.toast("Benutzer angelegt", "success");
                    }
                    SGComponents.closeModal();
                    if (onSaved) { onSaved(); }
                } catch (err) {
                    SGComponents.toast("Fehler: " +
                        (err.message || ""), "error");
                }
            }
        });
        SGComponents.showModal({
            title: existing ? "Benutzer bearbeiten" : "Neuer Benutzer",
            body: body, footer: [cancelBtn, saveBtn]
        });
    }

    function showPasswordResetForm(user) {
        const body = el("div");
        const pwInput = el("input", { class: "input", type: "password",
            placeholder: "Neues Passwort (min. 8 Zeichen)" });
        body.appendChild(el("div", { class: "muted mb-12",
            text: "Passwort fuer " + user.username + " zuruecksetzen. "
                  + "Alle Sessions werden ungueltig." }));
        body.appendChild(pwInput);
        const cancelBtn = el("button", { class: "btn", text: "Abbrechen",
            onclick: () => SGComponents.closeModal() });
        const saveBtn = el("button", { class: "btn btn-primary",
            text: "Setzen",
            onclick: async () => {
                if (pwInput.value.length < 8) {
                    SGComponents.toast("Passwort zu kurz (min. 8)",
                        "warning");
                    return;
                }
                try {
                    await SGApi.post("/users/" + user.id + "/password",
                        { password: pwInput.value });
                    SGComponents.toast("Passwort gesetzt", "success");
                    SGComponents.closeModal();
                } catch (err) {
                    SGComponents.toast("Fehler: " +
                        (err.message || ""), "error");
                }
            }
        });
        SGComponents.showModal({
            title: "Passwort zuruecksetzen", body: body,
            footer: [cancelBtn, saveBtn]
        });
    }

    // =========================================================================
    // Main Render
    // =========================================================================

    async function render(params) {
        const content = document.getElementById("app-content");
        clear(content);

        const header = el("div", { class: "page-header" });
        const tb = el("div");
        tb.appendChild(el("div", { class: "page-title", text: "Settings" }));
        tb.appendChild(el("div", { class: "page-subtitle",
            text: "Suricata-Konfiguration · Service Control · Benutzer" }));
        header.appendChild(tb);
        content.appendChild(header);

        const tabBar = el("div", { class: "tab-bar" });
        const tabs = [
            ["form", "Suricata Config"],
            ["yaml", "Raw YAML"],
            ["service", "Service Control"],
            ["users", "Benutzer"],
        ];
        tabs.forEach(([k, lbl]) => {
            const t = el("a", { class: "tab", text: lbl,
                onclick: (e) => {
                    e.preventDefault(); currentTab = k; render();
                }
            });
            if (currentTab === k) { t.classList.add("active"); }
            tabBar.appendChild(t);
        });
        content.appendChild(tabBar);

        const body = el("div");
        content.appendChild(body);

        if (currentTab === "form") { await renderFormTab(body); }
        else if (currentTab === "yaml") { await renderYamlTab(body); }
        else if (currentTab === "service") { await renderServiceTab(body); }
        else if (currentTab === "users") { await renderUsersTab(body); }
    }

    window.SGPageSettings = render;
}());
