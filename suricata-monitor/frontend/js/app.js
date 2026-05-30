/**
 * SuricataMonitor-NG · Main App Bootstrap
 *
 * - Auth-Check beim Start
 * - Theme aktivieren
 * - Router registrieren
 * - Sidebar/Theme/Logout wire-up
 */

(function () {
    "use strict";

    SGTheme.init();
    updateThemeLabel(SGTheme.get());
    SGTheme.onChange(updateThemeLabel);

    function updateThemeLabel(theme) {
        const lbl = document.getElementById("theme-label");
        if (lbl) { lbl.textContent = theme === "dark" ? "Dunkel" : "Hell"; }
    }

    // Sidebar Toggle
    const shell = document.getElementById("app-shell");
    document.getElementById("btn-sidebar-toggle").addEventListener("click", () => {
        const collapsed = shell.getAttribute("data-sidebar-collapsed") === "true";
        shell.setAttribute("data-sidebar-collapsed", collapsed ? "false" : "true");
        try { localStorage.setItem("surimon.sidebar",
              collapsed ? "false" : "true"); }
        catch (e) {}
    });
    try {
        const cs = localStorage.getItem("surimon.sidebar");
        if (cs === "true") { shell.setAttribute("data-sidebar-collapsed", "true"); }
    } catch (e) {}

    // Theme Toggle
    document.getElementById("btn-theme-toggle").addEventListener("click", () => {
        SGTheme.toggle();
    });

    // Logout
    document.getElementById("btn-logout").addEventListener("click", async () => {
        const ok = await SGComponents.confirmDialog({
            title: "Abmelden",
            message: "Wirklich abmelden?",
            confirmText: "Abmelden"
        });
        if (!ok) { return; }
        try { await SGApi.logout(); } catch (e) {}
        location.href = "/login";
    });

    // Sidebar Klicks
    document.querySelectorAll(".nav-item[data-route]").forEach(node => {
        node.addEventListener("click", (e) => {
            e.preventDefault();
            const route = node.getAttribute("data-route");
            SGRouter.navigate(route);
        });
    });

    // Router Setup
    SGRouter.setDefault("dashboard");
    SGRouter.register("dashboard", window.SGPageDashboard);
    SGRouter.register("events", window.SGPageEvents);
    SGRouter.register("rulesets", window.SGPageRulesets);
    SGRouter.register("settings", window.SGPageSettings);

    SGRouter.onChange((name) => {
        document.querySelectorAll(".nav-item[data-route]").forEach(n => {
            if (n.getAttribute("data-route") === name) {
                n.classList.add("active");
            } else {
                n.classList.remove("active");
            }
        });
    });

    // Auth bootstrap
    SGApi.whoami().then(res => {
        if (!res || !res.user) {
            location.href = "/login?next=" +
                encodeURIComponent(location.pathname + location.hash);
            return;
        }
        const u = res.user;
        document.getElementById("user-username").textContent = u.username;
        document.getElementById("user-role").textContent = u.role;
        window.SGCurrentUser = u;
        SGRouter.start();
    }).catch(err => {
        console.error("whoami failed", err);
        location.href = "/login";
    });
}());
