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
      $("apSsidHint").textContent = st.ap_ssid;
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

  /* ---- Timezone: plain UTC offset picker instead of a raw POSIX string ----
   * POSIX TZ offsets are inverted from how everyone actually says them
   * (UTC+9 is written "UTC-9" in a POSIX TZ string) - that's the "correction"
   * done here so the person picking a timezone never has to know that. No
   * DST rules are applied for these plain-offset entries; pick "Custom" for
   * a POSIX rule that handles DST on its own (e.g. US/EU presets below). */
  var TZ_CUSTOM = "__custom__";
  var TZ_PRESETS = [
    ["UTC12",     "UTC−12:00 — Baker Island"],
    ["UTC11",     "UTC−11:00 — Midway, Samoa"],
    ["UTC10",     "UTC−10:00 — Hawaii"],
    ["UTC9:30",   "UTC−09:30 — Marquesas Islands"],
    ["UTC9",      "UTC−09:00 — Alaska"],
    ["UTC8",      "UTC−08:00 — Pacific Time (US)"],
    ["UTC7",      "UTC−07:00 — Mountain Time (US)"],
    ["UTC6",      "UTC−06:00 — Central Time (US), Mexico City"],
    ["UTC5",      "UTC−05:00 — Eastern Time (US), Bogotá"],
    ["UTC4:30",   "UTC−04:30 — Caracas (pre-2016)"],
    ["UTC4",      "UTC−04:00 — Atlantic Time, Santiago"],
    ["UTC3:30",   "UTC−03:30 — Newfoundland"],
    ["UTC3",      "UTC−03:00 — Argentina, São Paulo"],
    ["UTC2",      "UTC−02:00 — Mid-Atlantic"],
    ["UTC1",      "UTC−01:00 — Azores"],
    ["UTC0",      "UTC±00:00 — London, Lisbon"],
    ["UTC-1",     "UTC+01:00 — Berlin, Paris, Lagos"],
    ["UTC-2",     "UTC+02:00 — Cairo, Athens, Johannesburg"],
    ["UTC-3",     "UTC+03:00 — Moscow, Riyadh, Nairobi"],
    ["UTC-3:30",  "UTC+03:30 — Tehran"],
    ["UTC-4",     "UTC+04:00 — Dubai, Baku"],
    ["UTC-4:30",  "UTC+04:30 — Kabul"],
    ["UTC-5",     "UTC+05:00 — Karachi, Tashkent"],
    ["UTC-5:30",  "UTC+05:30 — India, Sri Lanka"],
    ["UTC-5:45",  "UTC+05:45 — Nepal"],
    ["UTC-6",     "UTC+06:00 — Dhaka, Almaty"],
    ["UTC-6:30",  "UTC+06:30 — Yangon"],
    ["UTC-7",     "UTC+07:00 — Bangkok, Jakarta"],
    ["UTC-8",     "UTC+08:00 — Beijing, Singapore, Perth"],
    ["UTC-8:45",  "UTC+08:45 — Eucla"],
    ["UTC-9",     "UTC+09:00 — Seoul, Tokyo"],
    ["UTC-9:30",  "UTC+09:30 — Adelaide, Darwin"],
    ["UTC-10",    "UTC+10:00 — Sydney, Brisbane"],
    ["UTC-10:30", "UTC+10:30 — Lord Howe Island"],
    ["UTC-11",    "UTC+11:00 — Solomon Islands"],
    ["UTC-12",    "UTC+12:00 — Auckland, Fiji"],
    ["UTC-12:45", "UTC+12:45 — Chatham Islands"],
    ["UTC-13",    "UTC+13:00 — Tonga, Apia"],
    ["UTC-14",    "UTC+14:00 — Kiritimati"],
    ["PST8PDT,M3.2.0,M11.1.0",       "US Pacific (auto DST)"],
    ["MST7MDT,M3.2.0,M11.1.0",       "US Mountain (auto DST)"],
    ["CST6CDT,M3.2.0,M11.1.0",       "US Central (auto DST)"],
    ["EST5EDT,M3.2.0,M11.1.0",       "US Eastern (auto DST)"],
    ["CET-1CEST,M3.5.0,M10.5.0/3",   "Central Europe (auto DST)"],
    ["GMT0BST,M3.5.0/1,M10.5.0",     "UK (auto DST)"],
  ];

  function buildTzOptions() {
    var sel = $("tzOffset");
    TZ_PRESETS.forEach(function (entry) {
      var opt = document.createElement("option");
      opt.value = entry[0];
      opt.textContent = entry[1];
      sel.appendChild(opt);
    });
    var customOpt = document.createElement("option");
    customOpt.value = TZ_CUSTOM;
    customOpt.textContent = "Custom (advanced POSIX TZ string)…";
    sel.appendChild(customOpt);
  }
  buildTzOptions();

  function setTzFromConfig(tzPosix) {
    var known = TZ_PRESETS.some(function (entry) { return entry[0] === tzPosix; });
    if (known) {
      $("tzOffset").value = tzPosix;
      $("tzCustomRow").hidden = true;
    } else {
      $("tzOffset").value = TZ_CUSTOM;
      $("tzCustom").value = tzPosix || "";
      $("tzCustomRow").hidden = false;
    }
  }

  $("tzOffset").addEventListener("change", function () {
    $("tzCustomRow").hidden = this.value !== TZ_CUSTOM;
  });

  /* ---- Config-backed forms ---- */
  let currentConfig = {};

  async function loadConfig() {
    currentConfig = await api("/api/config");
    $("wifiCurrentSsid").textContent = currentConfig.wifi_ssid || "(not set)";
    setTzFromConfig(currentConfig.tz_posix);
    $("ntp").value = currentConfig.ntp_server || "";
    $("time24h").checked = !!currentConfig.time_24h;
    $("chime").checked = !!currentConfig.chime_enabled;
    $("brightness").value = currentConfig.brightness;
    $("brightnessVal").textContent = currentConfig.brightness;
    $("displayTheme").value = currentConfig.display_theme || "dark";
    $("themeDayStart").value = currentConfig.theme_day_start || "07:00";
    $("themeNightStart").value = currentConfig.theme_night_start || "20:00";
    updateThemeAutoRows();
    $("autoCycle").checked = !!currentConfig.auto_cycle_enabled;
    $("cycleSeconds").value = currentConfig.cycle_seconds;
    $("audioMuted").checked = !!currentConfig.audio_muted;
    $("albumInterval").value = currentConfig.album_interval_s;
    $("albumShuffle").checked = !!currentConfig.album_shuffle;
    $("weatherEnabled").checked = !!currentConfig.weather_enabled;
    $("weatherApiKey").value = currentConfig.weather_api_key || "";
    $("weatherCityId").value = currentConfig.weather_city_id || "";
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

  function updateThemeAutoRows() {
    var isAuto = $("displayTheme").value === "auto";
    $("themeDayRow").hidden = !isAuto;
    $("themeNightRow").hidden = !isAuto;
  }
  $("displayTheme").addEventListener("change", updateThemeAutoRows);

  $("btnSaveClock").addEventListener("click", function () {
    var tzSelected = $("tzOffset").value;
    var tzPosix = tzSelected === TZ_CUSTOM ? $("tzCustom").value.trim() : tzSelected;
    saveConfig({
      tz_posix: tzPosix,
      ntp_server: $("ntp").value,
      time_24h: $("time24h").checked,
      chime_enabled: $("chime").checked,
    }, $("clockMsg"));
  });

  $("btnSaveDisplay").addEventListener("click", function () {
    saveConfig({
      brightness: parseInt($("brightness").value, 10),
      display_theme: $("displayTheme").value,
      theme_day_start: $("themeDayStart").value,
      theme_night_start: $("themeNightStart").value,
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

  $("btnSaveWeather").addEventListener("click", function () {
    saveConfig({
      weather_enabled: $("weatherEnabled").checked,
      weather_api_key: $("weatherApiKey").value,
      weather_city_id: $("weatherCityId").value,
    }, $("weatherMsg"));
    setTimeout(refreshWeatherStatus, 1000);
  });

  /* ---- Weather status preview ---- */
  async function refreshWeatherStatus() {
    try {
      const w = await api("/api/weather");
      if (!w.enabled) {
        $("wxStatus").textContent = "Off";
      } else if (!w.configured) {
        $("wxStatus").textContent = "Missing API key / city ID";
      } else if (w.valid) {
        $("wxStatus").textContent = w.have_error ? "Stale (last fetch failed)" : "OK";
      } else {
        $("wxStatus").textContent = w.have_error ? ("Error: " + w.error) : "Waiting for first fetch...";
      }
      $("wxCity").textContent = w.city_name || "-";
      $("wxTemp").textContent = w.valid ? Math.round(w.temp_c) + " °C (feels " + Math.round(w.feels_like_c) + " °C)" : "-";
      $("wxDesc").textContent = w.description || "-";
      $("wxHumidity").textContent = w.valid ? w.humidity_pct + "%" : "-";
      $("wxWind").textContent = w.valid ? w.wind_speed_ms + " m/s" : "-";
    } catch (e) {
      $("wxStatus").textContent = "-";
    }
  }

  document.querySelector('.tab[data-tab="weather"]').addEventListener("click", refreshWeatherStatus);

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

  $("btnSaveApPassword").addEventListener("click", async function () {
    const msg = $("apPasswordMsg");
    const pw = $("apPassword").value;
    if (pw && pw.length < 8) {
      showMsg(msg, "Password must be at least 8 characters (or leave blank for open)", false);
      return;
    }
    try {
      const res = await api("/api/system/ap_password", "POST", { password: pw });
      if (res.ok) {
        showMsg(msg, pw ? "AP password set" : "AP is now open (no password)", true);
        $("apPassword").value = "";
      } else {
        showMsg(msg, "Failed: " + (res.error || ""), false);
      }
    } catch (e) {
      showMsg(msg, "Request failed", false);
    }
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

  /* ---- Files ---- */
  let filesCurrentPath = "/";

  function joinPath(dir, name) {
    return dir === "/" ? "/" + name : dir + "/" + name;
  }

  function formatSize(bytes) {
    if (bytes < 1024) return bytes + " B";
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
    return (bytes / (1024 * 1024)).toFixed(1) + " MB";
  }

  function renderBreadcrumb(path) {
    const wrap = $("filesBreadcrumb");
    wrap.innerHTML = "";
    const parts = path.split("/").filter(Boolean);
    let acc = "";

    const rootBtn = document.createElement("button");
    rootBtn.className = "crumb";
    rootBtn.textContent = "/ (SD card root)";
    rootBtn.addEventListener("click", function () { loadFiles("/"); });
    wrap.appendChild(rootBtn);

    parts.forEach(function (part) {
      acc += "/" + part;
      const b = document.createElement("button");
      b.className = "crumb";
      b.textContent = part;
      const target = acc;
      b.addEventListener("click", function () { loadFiles(target); });
      wrap.appendChild(b);
    });
  }

  async function loadFiles(path) {
    filesCurrentPath = path;
    renderBreadcrumb(path);
    const tbody = $("filesList");
    tbody.innerHTML = "<tr><td>Loading...</td></tr>";
    try {
      const data = await api("/api/files/list?path=" + encodeURIComponent(path));
      tbody.innerHTML = "";
      const entries = (data.entries || []).slice().sort(function (a, b) {
        if (a.is_dir !== b.is_dir) return a.is_dir ? -1 : 1;
        return a.name.localeCompare(b.name);
      });
      $("filesEmptyHint").hidden = entries.length > 0;
      entries.forEach(function (entry) { tbody.appendChild(renderFileRow(entry)); });
    } catch (e) {
      tbody.innerHTML = "<tr><td>Failed to load (is the SD card mounted?)</td></tr>";
    }
  }

  function renderFileRow(entry) {
    const tr = document.createElement("tr");
    const fullPath = joinPath(filesCurrentPath, entry.name);

    const nameTd = document.createElement("td");
    const nameSpan = document.createElement("span");
    nameSpan.className = "f-name" + (entry.is_dir ? " is-dir" : "");
    nameSpan.textContent = (entry.is_dir ? "📁 " : "📄 ") + entry.name;
    if (entry.is_dir) {
      nameSpan.addEventListener("click", function () { loadFiles(fullPath); });
    } else {
      nameSpan.addEventListener("click", function () {
        window.open("/api/files/download?path=" + encodeURIComponent(fullPath), "_blank");
      });
    }
    nameTd.appendChild(nameSpan);
    tr.appendChild(nameTd);

    const sizeTd = document.createElement("td");
    sizeTd.className = "f-size";
    sizeTd.textContent = entry.is_dir ? "" : formatSize(entry.size);
    tr.appendChild(sizeTd);

    const actionsTd = document.createElement("td");
    actionsTd.className = "f-actions";

    const renameBtn = document.createElement("button");
    renameBtn.title = "Rename";
    renameBtn.textContent = "✏️";
    renameBtn.addEventListener("click", async function () {
      const newName = prompt("Rename \"" + entry.name + "\" to:", entry.name);
      if (!newName || newName === entry.name) return;
      try {
        const res = await api("/api/files/rename", "POST", { from: fullPath, to: joinPath(filesCurrentPath, newName) });
        if (res.ok) { loadFiles(filesCurrentPath); } else { showMsg($("filesMsg"), "Rename failed: " + (res.error || ""), false); }
      } catch (e) { showMsg($("filesMsg"), "Rename failed", false); }
    });
    actionsTd.appendChild(renameBtn);

    const deleteBtn = document.createElement("button");
    deleteBtn.className = "danger";
    deleteBtn.title = "Delete";
    deleteBtn.textContent = "🗑️";
    deleteBtn.addEventListener("click", async function () {
      if (!confirm("Delete \"" + entry.name + "\"? This can't be undone.")) return;
      try {
        const res = await api("/api/files/delete", "POST", { path: fullPath });
        if (res.ok) { loadFiles(filesCurrentPath); } else { showMsg($("filesMsg"), "Delete failed: " + (res.error || ""), false); }
      } catch (e) { showMsg($("filesMsg"), "Delete failed", false); }
    });
    actionsTd.appendChild(deleteBtn);

    tr.appendChild(actionsTd);
    return tr;
  }

  /* Uploads one file, resolving (never rejecting) with {ok, error} so a
   * multi-file batch (see uploadFiles()) can keep going past one failure
   * instead of the whole queue stopping on it. */
  function uploadOneFile(file) {
    return new Promise(function (resolve) {
      const url = "/api/files/upload?path=" + encodeURIComponent(filesCurrentPath) +
                  "&name=" + encodeURIComponent(file.name);
      const xhr = new XMLHttpRequest();
      xhr.open("POST", url);

      xhr.upload.addEventListener("progress", function (e) {
        if (e.lengthComputable) {
          $("filesUploadProgressBar").style.width = Math.round((e.loaded / e.total) * 100) + "%";
        }
      });

      xhr.addEventListener("load", function () {
        let data = {};
        try { data = JSON.parse(xhr.responseText); } catch (e) { /* ignore */ }
        resolve(xhr.status === 200 && data.ok ? { ok: true } : { ok: false, error: data.error || String(xhr.status) });
      });

      xhr.addEventListener("error", function () {
        resolve({ ok: false, error: "connection error" });
      });

      xhr.send(file);
    });
  }

  /* Uploads a FileList/array one at a time (the device only has one SD card
   * SPI bus and one httpd worker anyway, so there's no throughput to gain
   * from parallel requests) - used by both the file picker and drag/drop. */
  async function uploadFiles(fileList) {
    if (!fileList || fileList.length === 0) {
      showMsg($("filesMsg"), "Choose a file first", false);
      return;
    }

    const queueMsg = $("filesUploadQueueMsg");
    $("filesUploadProgressWrap").hidden = false;
    let failed = 0;

    for (let i = 0; i < fileList.length; i++) {
      const file = fileList[i];
      queueMsg.textContent = fileList.length > 1 ? "Uploading " + (i + 1) + " of " + fileList.length + ": " + file.name : "";
      $("filesUploadProgressBar").style.width = "0%";
      const result = await uploadOneFile(file);
      if (!result.ok) {
        failed++;
        showMsg($("filesMsg"), "Failed to upload " + file.name + ": " + result.error, false);
      }
    }

    $("filesUploadProgressWrap").hidden = true;
    queueMsg.textContent = "";
    if (failed === 0) {
      showMsg($("filesMsg"), fileList.length === 1 ? "Uploaded " + fileList[0].name : "Uploaded " + fileList.length + " files", true);
    } else if (failed < fileList.length) {
      showMsg($("filesMsg"), failed + " of " + fileList.length + " uploads failed", false);
    }
    loadFiles(filesCurrentPath);
  }

  $("btnFilesUpload").addEventListener("click", function () {
    const input = $("filesUploadInput");
    uploadFiles(input.files).then(function () { input.value = ""; });
  });

  /* ---- Drag and drop files anywhere onto the Files card ---- */
  const filesDropZone = $("filesDropZone");
  ["dragenter", "dragover"].forEach(function (evt) {
    filesDropZone.addEventListener(evt, function (e) {
      e.preventDefault(); /* required, or the browser refuses the drop entirely */
      filesDropZone.classList.add("drag-over");
    });
  });
  filesDropZone.addEventListener("dragleave", function (e) {
    /* Children re-fire dragenter/dragleave as the pointer crosses them -
     * ignore it unless the pointer actually left the whole drop zone. */
    if (!filesDropZone.contains(e.relatedTarget)) {
      filesDropZone.classList.remove("drag-over");
    }
  });
  filesDropZone.addEventListener("drop", function (e) {
    e.preventDefault();
    filesDropZone.classList.remove("drag-over");
    if (e.dataTransfer && e.dataTransfer.files.length > 0) {
      uploadFiles(e.dataTransfer.files);
    }
  });

  $("btnFilesMkdir").addEventListener("click", async function () {
    const nameInput = $("filesNewFolderName");
    const msg = $("filesMsg");
    const name = nameInput.value.trim();
    if (!name || name.indexOf("/") !== -1) {
      showMsg(msg, "Enter a valid folder name", false);
      return;
    }
    try {
      const res = await api("/api/files/mkdir", "POST", { path: joinPath(filesCurrentPath, name) });
      if (res.ok) {
        nameInput.value = "";
        loadFiles(filesCurrentPath);
      } else {
        showMsg(msg, "Could not create folder: " + (res.error || ""), false);
      }
    } catch (e) {
      showMsg(msg, "Could not create folder", false);
    }
  });

  $("btnSdFormat").addEventListener("click", async function () {
    const msg = $("sdFormatMsg");
    if (!confirm("This ERASES EVERYTHING on the SD card (all photos and files) and can't be undone. Continue?")) return;
    if (!confirm("Really format the SD card? This is your last chance to cancel.")) return;
    try {
      const res = await api("/api/sdcard/format", "POST");
      if (res.ok) {
        showMsg(msg, "SD card formatted", true);
        loadFiles("/");
        refreshStatus();
      } else {
        showMsg(msg, "Format failed: " + (res.error || ""), false);
      }
    } catch (e) {
      showMsg(msg, "Format failed (connection error)", false);
    }
  });

  document.querySelector('.tab[data-tab="files"]').addEventListener("click", function () {
    loadFiles(filesCurrentPath);
  });

  /* ---- Boot ---- */
  loadConfig();
  refreshStatus();
  refreshFwStatus();
  setInterval(refreshStatus, 3000);
})();
