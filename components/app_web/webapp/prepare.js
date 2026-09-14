(function () {
  "use strict";

  function $(id) { return document.getElementById(id); }

  function showMsg(el, text, ok) {
    el.textContent = text;
    el.className = "msg " + (ok ? "ok" : "err");
    if (text) {
      setTimeout(function () { el.textContent = ""; el.className = "msg"; }, 4000);
    }
  }

  const OUT_W = 480;
  const OUT_H = 320;
  const MAX_ZOOM_MULT = 4; /* how far past "fills the frame" the slider/wheel can go */

  const canvas = $("cropCanvas");
  const ctx = canvas.getContext("2d");

  let sourceImg = null;   /* HTMLImageElement or offscreen <canvas> (after rotation) */
  let srcW = 0, srcH = 0; /* current natural size of sourceImg */
  let coverScale = 1, maxScale = 1, scale = 1;
  let tx = 0, ty = 0;     /* top-left of the drawn image, in canvas pixels */
  let objectUrl = null;

  /* ---- Loading a source image ---- */

  function baseName(name) {
    const dot = name.lastIndexOf(".");
    return dot > 0 ? name.slice(0, dot) : name;
  }

  function loadFile(file) {
    if (!file || file.type.indexOf("image/") !== 0) {
      return;
    }
    if (objectUrl) {
      URL.revokeObjectURL(objectUrl);
    }
    objectUrl = URL.createObjectURL(file);
    const img = new Image();
    img.onload = function () {
      sourceImg = img;
      srcW = img.naturalWidth;
      srcH = img.naturalHeight;
      $("outName").value = baseName(file.name) || "photo";
      $("cropStage").hidden = false;
      resetTransform();
      render();
    };
    img.onerror = function () {
      showMsg($("uploadMsg"), "Could not read that image", false);
    };
    img.src = objectUrl;
  }

  $("srcFile").addEventListener("change", function (e) {
    if (e.target.files && e.target.files[0]) {
      loadFile(e.target.files[0]);
    }
  });

  const dropZone = $("prepDropZone");
  ["dragenter", "dragover"].forEach(function (evt) {
    dropZone.addEventListener(evt, function (e) {
      e.preventDefault();
      dropZone.classList.add("drag-over");
    });
  });
  dropZone.addEventListener("dragleave", function (e) {
    if (!dropZone.contains(e.relatedTarget)) {
      dropZone.classList.remove("drag-over");
    }
  });
  dropZone.addEventListener("drop", function (e) {
    e.preventDefault();
    dropZone.classList.remove("drag-over");
    if (e.dataTransfer && e.dataTransfer.files.length > 0) {
      loadFile(e.dataTransfer.files[0]);
    }
  });

  /* ---- Transform: pan/zoom so the image always fully covers the 480x320 frame ---- */

  function resetTransform() {
    coverScale = Math.max(OUT_W / srcW, OUT_H / srcH);
    maxScale = coverScale * MAX_ZOOM_MULT;
    scale = coverScale;
    tx = (OUT_W - srcW * scale) / 2;
    ty = (OUT_H - srcH * scale) / 2;
    $("zoomRange").value = 0;
    $("dimsInfo").textContent = srcW + "×" + srcH + " → " + OUT_W + "×" + OUT_H;
  }

  function clampPan() {
    const drawW = srcW * scale;
    const drawH = srcH * scale;
    tx = Math.min(0, Math.max(OUT_W - drawW, tx));
    ty = Math.min(0, Math.max(OUT_H - drawH, ty));
  }

  function syncZoomSlider() {
    const range = maxScale - coverScale;
    const v = range > 0 ? (scale - coverScale) / range : 0;
    $("zoomRange").value = Math.round(v * 1000);
  }

  function zoomTo(newScale, pivotX, pivotY) {
    newScale = Math.min(maxScale, Math.max(coverScale, newScale));
    /* Keep the image point under (pivotX, pivotY) fixed while the scale changes. */
    const ix = (pivotX - tx) / scale;
    const iy = (pivotY - ty) / scale;
    scale = newScale;
    tx = pivotX - ix * scale;
    ty = pivotY - iy * scale;
    clampPan();
    render();
  }

  function render() {
    if (!sourceImg) return;
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, OUT_W, OUT_H);
    ctx.drawImage(sourceImg, tx, ty, srcW * scale, srcH * scale);
  }

  /* ---- Rotate ---- */

  function rotate(deg) {
    if (!sourceImg) return;
    const off = document.createElement("canvas");
    off.width = srcH;
    off.height = srcW;
    const octx = off.getContext("2d");
    octx.translate(off.width / 2, off.height / 2);
    octx.rotate(deg * Math.PI / 180);
    octx.drawImage(sourceImg, -srcW / 2, -srcH / 2);
    sourceImg = off;
    const swapped = srcW;
    srcW = srcH;
    srcH = swapped;
    resetTransform();
    render();
  }

  $("btnRotateCcw").addEventListener("click", function () { rotate(-90); });
  $("btnRotateCw").addEventListener("click", function () { rotate(90); });
  $("btnReset").addEventListener("click", function () { resetTransform(); render(); });

  /* ---- Zoom: slider (about canvas center) and mouse wheel (about the cursor) ---- */

  $("zoomRange").addEventListener("input", function () {
    const range = maxScale - coverScale;
    const newScale = coverScale + (this.value / 1000) * range;
    zoomTo(newScale, OUT_W / 2, OUT_H / 2);
  });

  function canvasPoint(clientX, clientY) {
    const rect = canvas.getBoundingClientRect();
    return {
      x: (clientX - rect.left) * (OUT_W / rect.width),
      y: (clientY - rect.top) * (OUT_H / rect.height),
    };
  }

  canvas.addEventListener("wheel", function (e) {
    if (!sourceImg) return;
    e.preventDefault();
    const p = canvasPoint(e.clientX, e.clientY);
    const factor = Math.pow(1.0015, -e.deltaY);
    zoomTo(scale * factor, p.x, p.y);
    syncZoomSlider();
  }, { passive: false });

  /* ---- Pan: drag with mouse or touch (Pointer Events cover both) ---- */

  let dragging = false;
  let dragStart = null;

  canvas.addEventListener("pointerdown", function (e) {
    if (!sourceImg) return;
    dragging = true;
    canvas.classList.add("dragging");
    canvas.setPointerCapture(e.pointerId);
    dragStart = { x: e.clientX, y: e.clientY, tx: tx, ty: ty };
  });
  canvas.addEventListener("pointermove", function (e) {
    if (!dragging) return;
    const rect = canvas.getBoundingClientRect();
    const scaleFactor = OUT_W / rect.width;
    tx = dragStart.tx + (e.clientX - dragStart.x) * scaleFactor;
    ty = dragStart.ty + (e.clientY - dragStart.y) * scaleFactor;
    clampPan();
    render();
  });
  function endDrag(e) {
    if (!dragging) return;
    dragging = false;
    canvas.classList.remove("dragging");
    try { canvas.releasePointerCapture(e.pointerId); } catch (err) { /* ignore */ }
  }
  canvas.addEventListener("pointerup", endDrag);
  canvas.addEventListener("pointercancel", endDrag);

  /* ---- Format / quality ---- */

  function syncFormatUi() {
    $("qualityRow").hidden = $("outFormat").value !== "image/jpeg";
  }
  $("outFormat").addEventListener("change", syncFormatUi);
  $("qualityRange").addEventListener("input", function () {
    $("qualityVal").textContent = this.value;
  });
  syncFormatUi();

  function ensureExtension(name, mime) {
    const ext = mime === "image/png" ? ".png" : ".jpg";
    const stripped = (name || "").trim().replace(/[\\/]/g, "").replace(/\.(jpe?g|png|bmp|gif)$/i, "");
    return (stripped || "photo") + ext;
  }

  function exportBlob() {
    return new Promise(function (resolve) {
      const format = $("outFormat").value;
      const quality = format === "image/jpeg" ? (+$("qualityRange").value) / 100 : undefined;
      canvas.toBlob(function (blob) { resolve(blob); }, format, quality);
    });
  }

  /* ---- Download ---- */

  $("btnDownload").addEventListener("click", async function () {
    if (!sourceImg) return;
    const blob = await exportBlob();
    if (!blob) return;
    const name = ensureExtension($("outName").value, $("outFormat").value);
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = name;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(function () { URL.revokeObjectURL(url); }, 2000);
  });

  /* ---- Upload straight to the device (same endpoint the Files tab uses) ---- */

  $("btnUpload").addEventListener("click", async function () {
    const msg = $("uploadMsg");
    if (!sourceImg) {
      showMsg(msg, "Choose a photo first", false);
      return;
    }
    const dir = $("uploadPath").value.trim() || "/photos";
    const name = ensureExtension($("outName").value, $("outFormat").value);
    const blob = await exportBlob();
    if (!blob) {
      showMsg(msg, "Could not encode image", false);
      return;
    }

    const url = "/api/files/upload?path=" + encodeURIComponent(dir) + "&name=" + encodeURIComponent(name);
    const progressWrap = $("uploadProgressWrap");
    const progressBar = $("uploadProgressBar");
    progressWrap.hidden = false;
    progressBar.style.width = "0%";

    await new Promise(function (resolve) {
      const xhr = new XMLHttpRequest();
      xhr.open("POST", url);
      xhr.upload.addEventListener("progress", function (e) {
        if (e.lengthComputable) {
          progressBar.style.width = Math.round((e.loaded / e.total) * 100) + "%";
        }
      });
      xhr.addEventListener("load", function () {
        let data = {};
        try { data = JSON.parse(xhr.responseText); } catch (err) { /* ignore */ }
        if (xhr.status === 200 && data.ok) {
          showMsg(msg, "Uploaded " + dir + "/" + name, true);
        } else {
          showMsg(msg, "Upload failed: " + (data.error || xhr.status), false);
        }
        resolve();
      });
      xhr.addEventListener("error", function () {
        showMsg(msg, "Upload failed: connection error", false);
        resolve();
      });
      xhr.send(blob);
    });

    progressWrap.hidden = true;
  });
})();
