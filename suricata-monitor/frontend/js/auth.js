/**
 * Login-Seite Logic.
 */
(function () {
    "use strict";

    const form = document.getElementById("login-form");
    const errEl = document.getElementById("login-error");
    const btn = document.getElementById("btn-login");

    function showError(msg) {
        errEl.textContent = msg;
        errEl.classList.add("visible");
    }
    function clearError() {
        errEl.textContent = "";
        errEl.classList.remove("visible");
    }

    function nextUrl() {
        const params = new URLSearchParams(location.search);
        const next = params.get("next");
        // einfache Whitelist: muss mit "/" beginnen, kein Protokoll
        if (next && /^\/[^/]/.test(next)) { return next; }
        return "/";
    }

    form.addEventListener("submit", async (e) => {
        e.preventDefault();
        clearError();
        btn.disabled = true;
        btn.innerHTML = '<span class="spinner"></span> Pruefe…';

        const username = document.getElementById("username").value.trim();
        const password = document.getElementById("password").value;

        try {
            await SGApi.login(username, password);
            location.href = nextUrl();
        } catch (err) {
            if (err.status === 401) {
                showError("Falscher Benutzername oder Passwort.");
            } else if (err.kind === "network") {
                showError("Server nicht erreichbar.");
            } else {
                showError("Fehler: " + (err.message || "unbekannt"));
            }
            btn.disabled = false;
            btn.textContent = "Anmelden";
        }
    });
}());
