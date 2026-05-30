/**
 * SuricataMonitor-NG · UI Components
 * Toast, Modal, Confirm-Dialog.  Alles ueber DOM-API, kein Framework.
 */

(function (global) {
    "use strict";

    const { el, clear } = global.SGUtils;

    // ============================== Toasts ==============================

    let toastContainer = null;
    function ensureToastContainer() {
        if (toastContainer) { return toastContainer; }
        toastContainer = el("div", { class: "toast-container" });
        document.body.appendChild(toastContainer);
        return toastContainer;
    }

    function toast(message, kind, durationMs) {
        const container = ensureToastContainer();
        const node = el("div", { class: "toast " + (kind || ""),
                                  text: message });
        container.appendChild(node);
        setTimeout(() => {
            node.style.opacity = "0";
            setTimeout(() => {
                if (node.parentNode) {
                    node.parentNode.removeChild(node);
                }
            }, 200);
        }, durationMs || 3500);
    }

    // ============================== Modal ===============================

    let activeBackdrop = null;

    function closeModal() {
        if (activeBackdrop && activeBackdrop.parentNode) {
            activeBackdrop.parentNode.removeChild(activeBackdrop);
        }
        activeBackdrop = null;
    }

    /**
     * showModal({ title, body (string|HTMLElement), footer (HTMLElement[]),
     *             large?, onClose? })
     * Liefert das modal-body Element zurueck, damit man es nach Erstellung
     * weiter befuellen kann.
     */
    function showModal(opts) {
        closeModal();
        opts = opts || {};

        const titleEl = el("div", { class: "modal-title",
                                    text: opts.title || "" });
        const closeBtn = el("button", { class: "close-btn",
                                         "aria-label": "Close",
                                         text: "✕",
                                         onclick: () => {
                                             closeModal();
                                             if (opts.onClose) { opts.onClose(); }
                                         } });

        const headerEl = el("div", { class: "modal-header" },
                            [titleEl, closeBtn]);

        const bodyEl = el("div", { class: "modal-body" });
        if (typeof opts.body === "string") {
            bodyEl.innerHTML = opts.body;
        } else if (opts.body instanceof Node) {
            bodyEl.appendChild(opts.body);
        }

        const children = [headerEl, bodyEl];
        if (opts.footer && opts.footer.length) {
            const footerEl = el("div", { class: "modal-footer" });
            opts.footer.forEach(f => footerEl.appendChild(f));
            children.push(footerEl);
        }

        const modal = el("div", {
            class: "modal" + (opts.large ? " modal-lg" : "")
        }, children);

        const backdrop = el("div", {
            class: "modal-backdrop",
            onclick: (e) => {
                if (e.target === backdrop) {
                    closeModal();
                    if (opts.onClose) { opts.onClose(); }
                }
            }
        }, [modal]);

        document.body.appendChild(backdrop);
        activeBackdrop = backdrop;
        return { bodyEl: bodyEl, close: closeModal };
    }

    /**
     * confirmDialog({ title, message, confirmText, danger }) -> Promise<bool>
     */
    function confirmDialog(opts) {
        opts = opts || {};
        return new Promise((resolve) => {
            const okBtn = el("button", {
                class: "btn " + (opts.danger ? "btn-danger" : "btn-primary"),
                text: opts.confirmText || "Bestaetigen",
                onclick: () => { closeModal(); resolve(true); }
            });
            const cancelBtn = el("button", {
                class: "btn",
                text: "Abbrechen",
                onclick: () => { closeModal(); resolve(false); }
            });
            const bodyText = el("div", {
                text: opts.message || "Bist du sicher?"
            });
            showModal({
                title: opts.title || "Bestaetigen",
                body: bodyText,
                footer: [cancelBtn, okBtn],
                onClose: () => resolve(false)
            });
        });
    }

    // Keyboard support
    document.addEventListener("keydown", (e) => {
        if (e.key === "Escape" && activeBackdrop) {
            closeModal();
        }
    });

    global.SGComponents = {
        toast, showModal, closeModal, confirmDialog
    };

}(window));
