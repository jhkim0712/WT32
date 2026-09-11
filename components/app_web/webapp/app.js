(function () {
  "use strict";

  function $(id) { return document.getElementById(id); }

  function showMsg(el, text, ok) {
    el.textContent = text;
    el.className = "msg " + (ok ? "ok" : "err");
    setTimeout(function () { el.textContent = ""; el.className = "msg"; }, 4000);
  }

  async function api(path, method, body) {
    const opts = { method: method || "GET" };
    if (body !== undefined) {
      opts.headers = { "Content-Type": "application/json" };
      opts.body = JSON.stringify(body);
    }
    const res = await fetch(path, opts);
    if (!res.ok) {
      throw new Error("HTTP " + res.status);
    }
    return res.json();
  }

  /* ---- Tabs ---- */
  document.querySelectorAll(".tab").forEach(function (btn) {
    btn.addEventListener("click", function () {
      document.querySelectorAll(".tab").forEach(function (b) { b.classList.remove("active"); });
      document.querySelectorAll(".panel").forEach(function (p) { p.classList.remove("active"); });
      btn.classList.add("active");
      $("panel-" + btn.dataset.tab).classList.add("active");
    });
  });

  /* ---- Status ---- */
  function formatUptime(seconds) {
    const d = Math.floor(seconds / 86400);
    const h = Math.floor((seconds % 86400) / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = Math.floor(seconds % 60);
    return (d ? d + "d " : "") + h + "h " + m + "m " + s + "s";
  }

  async function refreshStatus() {
    try {
      const st = await api("/api/status");
      $("statusDot").classList.toggle("online", st.connected);
      $("fwVersion").textContent = "v" + st.fw_version;
      $("stMode").textContent = st.mode === "sta" ? "Connected (station)" : "Setup mode (access point)";
      $("stIp").textContent = st.ip;
      $("stRssi").textContent = st.connected ? (st.rssi + " dBm") : ("AP: " + st.ap_ssid);
      $("stSd").textContent = st.sd_mounted ? "Mounted" : "Not found";
      $("stPhotos").textContent = st.photo_count;
      $("stTime").textContent = st.time_synced ? st.time : (st.time + " (not synced)");
      $("stUptime").textContent = formatUptime(st.uptime_s);
      $("stHeap").textContent = Math.round(st.heap_free / 1024) + " KB";
    } catch (e) {
      $("statusDot").classList.remove("online");
    }
  }

  /* ---- Wi-Fi ---- */
  $("btnScan").addEventListener("click", async function () {
    const list = $("wifiList");
    list.innerHTML = "<li>Scanning...</li>";
    try {
      const nets = await api("/api/wifi/scan");
      list.innerHTML = "";
      nets.sort(function (a, b) { return b.rssi - a.rssi; });
      nets.forEach(function (n) {
        const li = document.createElement("li");
        li.innerHTML = "<span>" + (n.secure ? "🔒 " : "") + escapeHtml(n.ssid) + "</span>" +
                        "<span class='rssi'>" + n.rssi + " dBm</span>";
        li.addEventListener("click", function () { $("wifiSsid").value = n.ssid; });
        list.appendChild(li);
      });
      if (nets.length === 0) {
        list.innerHTML = "<li>No networks found</li>";
      }
    } catch (e) {
      list.innerHTML = "<li>Scan failed</li>";
    }
  });

  function escapeHtml(s) {
    return s.replace(/[&<>"']/g, function (c) {
      return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c];
    });
  }

  $("wifiForm").addEventListener("submit", async function (e) {
    e.preventDefault();
    const msg = $("wifiMsg");
    showMsg(msg, "Connecting...", true);
    try {
      const res = await api("/api/wifi/connect", "POST", {
        ssid: $("wifiSsid").value,
        password: $("wifiPass").value,
      });
      showMsg(msg, res.ok ? "Connected!" : "Could not connect - check the password", !!res.ok);
      refreshStatus();
    } catch (e) {
      showMsg(msg, "Request failed", false);
    }
  });

  /* ---- Config-backed forms ---- */
  let currentConfig = {};

  async function loadConfig() {
    currentConfig = await api("/api/config");
    $("wifiCurrentSsid").textContent = currentConfig.wifi_ssid || "(not set)";
    $("tz").value = currentConfig.tz_posix || "";
    $("ntp").value = currentConfig.ntp_server || "";
    $("time24h").checked = !!currentConfig.time_24h;
    $("chime").checked = !!currentConfig.chime_enabled;
    $("brightness").value = currentConfig.brightness;
    $("brightnessVal").textContent = currentConfig.brightness;
    $("autoCycle").checked = !!currentConfig.auto_cycle_enabled;
    $("cycleSeconds").value = currentConfig.cycle_seconds;
    $("audioMuted").checked = !!currentConfig.audio_muted;
    $("albumInterval").value = currentConfig.album_interval_s;
    $("albumShuffle").checked = !!currentConfig.album_shuffle;
    $("hostname").value = currentConfig.hostname || "";
    $("githubRepo").value = currentConfig.github_repo || "";
  }

  async function saveConfig(patch, msgEl) {
    try {
      await api("/api/config", "POST", patch);
      showMsg(msgEl, "Saved", true);
    } catch (e) {
      showMsg(msgEl, "Failed to save", false);
    }
  }

  $("brightness").addEventListener("input", function () {
    $("brightnessVal").textContent = this.value;
  });

  $("btnSaveClock").addEventListener("click", function () {
    saveConfig({
      tz_posix: $("tz").value,
      ntp_server: $("ntp").value,
      time_24h: $("time24h").checked,
      chime_enabled: $("chime").checked,
    }, $("clockMsg"));
  });

  $("btnSaveDisplay").addEventListener("click", function () {
    saveConfig({
      brightness: parseInt($("brightness").value, 10),
      auto_cycle_enabled: $("autoCycle").checked,
      cycle_seconds: parseInt($("cycleSeconds").value, 10),
      audio_muted: $("audioMuted").checked,
    }, $("displayMsg"));
  });

  $("btnSaveAlbum").addEventListener("click", function () {
    saveConfig({
      album_interval_s: parseInt($("albumInterval").value, 10),
      album_shuffle: $("albumShuffle").checked,
    }, $("albumMsg"));
  });

  $("btnSaveSystem").addEventListener("click", function () {
    saveConfig({ hostname: $("hostname").value }, $("systemMsg"));
  });

  $("btnRestart").addEventListener("click", async function () {
    if (!confirm("Restart the device now?")) return;
    await api("/api/system/restart", "POST");
    alert("Restarting...");
  });

  $("btnFactoryReset").addEventListener("click", async function () {
    if (!confirm("This erases all saved settings (Wi-Fi included). Continue?")) return;
    await api("/api/system/factory_reset", "POST");
    alert("Factory reset - the device will restart in setup mode.");
  });

  /* ---- Firmware / OTA ---- */
  let otaPollTimer = null;
  let latestAssetUrl = null;

  function setFwState(text) {
    $("fwState").textContent = text;
  }

  function setFwProgress(pct) {
    if (pct === null || pct < 0) {
      $("fwProgressWrap").hidden = true;
      return;
    }
    $("fwProgressWrap").hidden = false;
    $("fwProgressBar").style.width = pct + "%";
  }

  async function refreshFwStatus() {
    try {
      const st = await api("/api/ota/status");
      $("fwCurrentVersion").textContent = st.current_version;
      if (st.state === "idle") {
        setFwState("Idle");
        setFwProgress(null);
      } else if (st.state === "downloading") {
        setFwState(st.message || "Downloading...");
        setFwProgress(null);
      } else if (st.state === "writing") {
        setFwState(st.message || "Installing...");
        setFwProgress(st.progress_pct);
      } else if (st.state === "success") {
        setFwState(st.message || "Done - restarting");
        setFwProgress(100);
        stopOtaPolling();
      } else if (st.state === "error") {
        setFwState("Error: " + st.message);
        setFwProgress(null);
        stopOtaPolling();
      }
    } catch (e) {
      /* device may be mid-restart - ignore transient failures while polling */
    }
  }

  function startOtaPolling() {
    if (otaPollTimer) return;
    otaPollTimer = setInterval(refreshFwStatus, 1500);
  }

  function stopOtaPolling() {
    if (otaPollTimer) {
      clearInterval(otaPollTimer);
      otaPollTimer = null;
    }
  }

  $("btnSaveRepo").addEventListener("click", function () {
    saveConfig({ github_repo: $("githubRepo").value }, $("fwCheckMsg"));
  });

  $("btnCheckUpdate").addEventListener("click", async function () {
    const msg = $("fwCheckMsg");
    showMsg(msg, "Checking...", true);
    $("fwUpdateBox").hidden = true;
    try {
      const res = await fetch("/api/ota/check");
      const data = await res.json();
      if (!res.ok || !data.ok) {
        showMsg(msg, "Check failed: " + (data.error || res.status), false);
        return;
      }
      $("fwCurrentVersion").textContent = data.current_version;
      if (data.update_available) {
        latestAssetUrl = data.asset_url;
        $("fwLatestVersion").textContent = data.latest_version;
        $("fwNotes").textContent = data.notes || "";
        $("fwUpdateBox").hidden = false;
        showMsg(msg, "Update available", true);
      } else {
        showMsg(msg, "Already up to date (" + data.latest_version + ")", true);
      }
    } catch (e) {
      showMsg(msg, "Check failed", false);
    }
  });

  $("btnInstallUpdate").addEventListener("click", async function () {
    if (!latestAssetUrl) return;
    if (!confirm("Download and install this update now? The device will restart when done.")) return;
    try {
      const res = await api("/api/ota/install", "POST", { url: latestAssetUrl });
      if (res.ok) {
        setFwState("Starting install...");
        startOtaPolling();
      } else {
        showMsg($("fwCheckMsg"), "Could not start install: " + (res.error || ""), false);
      }
    } catch (e) {
      showMsg($("fwCheckMsg"), "Could not start install", false);
    }
  });

  $("btnUpload").addEventListener("click", function () {
    const fileInput = $("fwFile");
    const msg = $("fwUploadMsg");
    if (!fileInput.files || fileInput.files.length === 0) {
      showMsg(msg, "Choose a .bin file first", false);
      return;
    }
    const file = fileInput.files[0];

    const xhr = new XMLHttpRequest();
    xhr.open("POST", "/api/ota/upload");
    xhr.setRequestHeader("Content-Type", "application/octet-stream");
    setFwProgress(0);
    setFwState("Uploading...");

    xhr.upload.addEventListener("progress", function (e) {
      if (e.lengthComputable) {
        setFwProgress(Math.round((e.loaded / e.total) * 100));
      }
    });

    xhr.addEventListener("load", function () {
      let data = {};
      try { data = JSON.parse(xhr.responseText); } catch (e) { /* ignore */ }
      if (xhr.status === 200 && data.ok) {
        setFwState("Installed - restarting...");
        showMsg(msg, "Success! The device is restarting.", true);
      } else {
        setFwState("Error");
        setFwProgress(null);
        showMsg(msg, "Upload rejected: " + (data.error || xhr.status), false);
      }
    });

    xhr.addEventListener("error", function () {
      setFwState("Error");
      setFwProgress(null);
      showMsg(msg, "Upload failed (connection error)", false);
    });

    xhr.send(file);
  });

  /* ---- Boot ---- */
  loadConfig();
  refreshStatus();
  refreshFwStatus();
  setInterval(refreshStatus, 3000);
})();
