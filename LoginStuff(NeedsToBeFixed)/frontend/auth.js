"use strict";

async function postJson(url, body) {
  const res = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
    credentials: "same-origin",
  });
  let data = {};
  try { data = await res.json(); } catch (_) { /* empty or non-JSON body */ }
  return { ok: res.ok, data };
}

// Login and signup forms
const form = document.getElementById("auth-form");
if (form) {
  const errorBox = document.getElementById("form-error");
  const button = form.querySelector("button[type=submit]");
  const idleLabel = button.textContent;

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    errorBox.hidden = true;
    button.disabled = true;
    button.textContent = form.dataset.busy || "Working...";
    try {
      const result = await postJson(form.dataset.endpoint, Object.fromEntries(new FormData(form)));
      if (result.ok) {
        window.location.href = "/Dashboard.html";
        return;
      }
      errorBox.textContent = result.data.error || "Something went wrong. Try again.";
    } catch (_) {
      errorBox.textContent = "Couldn't reach the server. Check your connection and try again.";
    }
    errorBox.hidden = false;
    button.disabled = false;
    button.textContent = idleLabel;
  });
}

// Dashboard: show who is signed in, bounce to login if nobody is
const me = document.getElementById("me");
if (me) {
  fetch("/api/me", { credentials: "same-origin" })
    .then((res) => {
      if (res.status === 401) { window.location.href = "/Login.html"; return null; }
      return res.json();
    })
    .then((data) => { if (data) me.textContent = data.user.username; });

  document.getElementById("logout").addEventListener("click", async () => {
    await fetch("/api/logout", { method: "POST", credentials: "same-origin" });
    window.location.href = "/Login.html";
  });
}
