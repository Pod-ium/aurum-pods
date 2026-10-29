// PicoPods Web - launcher and pod runner.
//
// A pod is one file, <name>.pod.js (Emscripten, the wasm inside), built by
// tools/build_web.sh. Built-in pods are listed in pods.json next to this page;
// pods the user loads are kept in IndexedDB. The page shows one pod per load:
// starting another reloads the page (#pod-<file> or #lib-<id> picks it), so
// every pod starts from a clean slate, like switching cartridges.
"use strict";

const $ = id => document.getElementById(id);
const BLURBS = {
  podboy: "Game Boy (DMG) emulator. Put .gb ROMs in /roms.",
  podwriter: "Text editor with undo, syntax colours and ePub export.",
  podfiles: "File manager: copy, move, delete. No network here.",
  picocraft: "Voxel sandbox: build and dig in a block world.",
  pd_demo: "Tour of the libraries: keyboard, colours, audio.",
};

// ---------------------------------------------------------------- storage
// Loaded pods live in their own IndexedDB database (the SD card is another).
function libraryDb() {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open("picopods-library", 1);
    req.onupgradeneeded = () => req.result.createObjectStore("pods", { keyPath: "id" });
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}
async function libraryTx(mode, fn) {
  const db = await libraryDb();
  return new Promise((resolve, reject) => {
    const tx = db.transaction("pods", mode);
    const out = fn(tx.objectStore("pods"));
    tx.oncomplete = () => resolve(out && "result" in out ? out.result : undefined);
    tx.onerror = () => reject(tx.error);
  });
}
const libraryList = () => libraryTx("readonly", s => s.getAll()).catch(() => []);
const libraryGet = id => libraryTx("readonly", s => s.get(id));
const libraryPut = pod => libraryTx("readwrite", s => s.put(pod));
const libraryDelete = id => libraryTx("readwrite", s => s.delete(id));

function fmtSize(n) {
  return n >= 1048576 ? (n / 1048576).toFixed(1) + " MB" : n >= 1024 ? Math.round(n / 1024) + " KB" : n + " B";
}
function go(hash) {
  location.hash = hash;
  location.reload();
}

// ---------------------------------------------------------------- launcher
async function showLauncher() {
  $("launcher").hidden = false;
  let builtin = [];
  try {
    const r = await fetch("pods.json", { cache: "no-cache" });
    if (r.ok) builtin = (await r.json()).pods || [];
  } catch (_) {}
  const ul = $("builtin");
  for (const p of builtin) {
    const li = document.createElement("li");
    li.appendChild(podButton(p.title, `v${p.version} · ${fmtSize(p.size)}`, BLURBS[p.name] || "", () => go("pod-" + p.name)));
    ul.appendChild(li);
  }
  $("builtin-empty").hidden = builtin.length > 0;
  await renderLibrary();

  $("load-pod").onclick = () => $("pod-file").click();
  $("pod-file").onchange = async e => {
    const f = e.target.files[0];
    e.target.value = "";
    if (!f) return;
    const text = await f.text();
    if (!text.includes("createPod")) {
      $("library-empty").hidden = false;
      $("library-empty").textContent = `${f.name} is not a pod file. Pod files are the *.pod.js files from tools/build_web.sh.`;
      return;
    }
    const id = f.name.replace(/\.pod\.js$|\.js$/, "").replace(/[^A-Za-z0-9._~-]/g, "_");
    await libraryPut({ id, name: f.name, text, size: f.size, added: Date.now() });
    await renderLibrary();
  };
}

function podButton(title, meta, desc, onClick) {
  const b = document.createElement("button");
  b.type = "button";
  b.className = "pod";
  b.innerHTML = '<span class="pod-name"></span><span class="pod-meta"></span><span class="pod-desc"></span>';
  b.children[0].textContent = title;
  b.children[1].textContent = meta;
  b.children[2].textContent = desc;
  if (!desc) b.children[2].remove();
  b.onclick = onClick;
  return b;
}

async function renderLibrary() {
  const pods = (await libraryList()).sort((a, b) => b.added - a.added);
  const ul = $("library");
  ul.textContent = "";
  for (const p of pods) {
    const li = document.createElement("li");
    li.className = "pod-row";
    li.appendChild(podButton(p.name, fmtSize(p.size), "Loaded " + new Date(p.added).toLocaleDateString(), () => go("lib-" + p.id)));
    const del = document.createElement("button");
    del.type = "button";
    del.className = "pod-remove";
    del.textContent = "✕";
    del.setAttribute("aria-label", "Remove " + p.name);
    del.onclick = async () => {   // two taps: the first arms it
      if (!del.classList.contains("confirm")) { del.classList.add("confirm"); del.textContent = "Del?"; return; }
      await libraryDelete(p.id);
      renderLibrary();
    };
    li.appendChild(del);
    ul.appendChild(li);
  }
  $("library-empty").hidden = pods.length > 0;
}

// ---------------------------------------------------------------- runner
let pod = null;            // the Emscripten module once running
let running = false;
const logLines = [];

function log(line) {
  logLines.push(line);
  if (logLines.length > 300) logLines.shift();
  const el = $("log");
  if (el && !$("panel").hidden) el.textContent = logLines.join("\n");
}

// Web Audio for host_web.cpp: every push becomes a buffer scheduled right
// after the previous one; queued() is what has not played yet.
const audio = {
  ctx: null, rate: 0, next: 0, sources: new Set(),
  open(rate) {
    if (!this.ctx) return 0;
    this.rate = rate;
    this.next = 0;
    return 1;
  },
  close() { this.clear(); this.rate = 0; },
  queue(ptr, frames) {
    if (!this.ctx || !this.rate || frames <= 0) return;
    const h = pod.HEAP16, base = ptr >> 1;
    const buf = this.ctx.createBuffer(2, frames, this.rate);
    const L = buf.getChannelData(0), R = buf.getChannelData(1);
    for (let i = 0; i < frames; i++) {
      L[i] = h[base + 2 * i] / 32768;
      R[i] = h[base + 2 * i + 1] / 32768;
    }
    const now = this.ctx.currentTime;
    if (this.next < now + 0.005) this.next = now + 0.03;   // underrun: small cushion
    const src = this.ctx.createBufferSource();
    src.buffer = buf;
    src.connect(this.ctx.destination);
    src.onended = () => this.sources.delete(src);
    src.start(this.next);
    this.sources.add(src);
    this.next += frames / this.rate;
  },
  queued() {
    if (!this.ctx || !this.rate) return 0;
    return Math.max(0, Math.round((this.next - this.ctx.currentTime) * this.rate));
  },
  clear() {
    for (const s of this.sources) { try { s.stop(); } catch (_) {} }
    this.sources.clear();
    this.next = 0;
  },
};

// /sd: IndexedDB-backed, shared by every pod. Saved every 2 s and when the
// page is hidden.
let sdDirty = false, sdSyncing = false;
function sdSave() {
  if (!pod || sdSyncing) return;
  sdSyncing = true;
  pod.FS.syncfs(false, err => {
    sdSyncing = false;
    if (err) log("[web] SD card save failed: " + err);
  });
}

function loadScript(src) {
  return new Promise((resolve, reject) => {
    const s = document.createElement("script");
    s.src = src;
    s.onload = resolve;
    s.onerror = () => reject(new Error("could not load " + src));
    document.head.appendChild(s);
  });
}

async function showRunner(kind, id) {
  $("runner").hidden = false;
  let title = id, src = null, size = 0;
  if (kind === "pod") {
    src = id + ".pod.js";
    try {
      const r = await fetch("pods.json", { cache: "no-cache" });
      const p = ((await r.json()).pods || []).find(p => p.name === id);
      if (p) title = p.title;
    } catch (_) {}
  } else {
    const p = await libraryGet(id).catch(() => null);
    if (!p) return stopped("Pod not found", "It is no longer in this browser. Load the file again from the pod list.", false);
    title = p.name;
    size = p.size;
    src = URL.createObjectURL(new Blob([p.text], { type: "text/javascript" }));
  }
  document.title = title + " · PicoPods";
  $("pod-title").textContent = title;
  $("start-title").textContent = title;
  $("start-text").textContent = size ? `${fmtSize(size)} pod file. Tap to power on; sound starts with it.` : "Tap to power on. Sound starts with it.";
  $("back").onclick = () => go("");
  $("restart-btn").onclick = () => location.reload();
  fitScreen();
  addEventListener("resize", fitScreen);

  const kbd = new PicoKeyboard($("keyboard"), sendKey);
  attachPcKeyboard(sendKey, () => running && $("panel").hidden);
  $("kbd-btn").onclick = () => {
    const show = $("keyboard").hidden;
    $("keyboard").hidden = !show;
    $("kbd-btn").setAttribute("aria-pressed", String(show));
    kbd.releaseAll();
    fitScreen();
  };
  setupPanel();

  $("start-btn").onclick = async () => {
    $("start-btn").disabled = true;
    $("start-text").textContent = "Starting…";
    try {
      const AC = window.AudioContext || window.webkitAudioContext;
      if (AC) { audio.ctx = new AC(); audio.ctx.resume().catch(() => {}); }
    } catch (_) {}
    try { if (navigator.wakeLock) navigator.wakeLock.request("screen").catch(() => {}); } catch (_) {}
    try {
      await loadScript(src);
      pod = await createPod({
        pdAudio: audio,
        preRun: [m => {
          m.FS.mkdir("/sd");
          m.FS.mount(m.IDBFS, {}, "/sd");
          m.addRunDependency("sd");
          m.FS.syncfs(true, err => {
            if (err) log("[web] SD card load failed: " + err);
            m.removeRunDependency("sd");
          });
        }],
        print: t => log(t),
        printErr: t => log(t),
        onExit: code => { sdSave(); stopped("Pod stopped", `It exited with status ${code}.`, true); },
        onAbort: what => stopped("Pod crashed", String(what || "The pod aborted.") + " Open SD → Pod log for details.", true),
      });
      running = true;
      $("start").hidden = true;
      setInterval(sdSave, 2000);
      document.addEventListener("visibilitychange", () => { if (document.hidden) sdSave(); });
      addEventListener("pagehide", sdSave);
      requestAnimationFrame(draw);
    } catch (err) {
      stopped("Could not start the pod", String(err && err.message || err), true);
    }
  };
}

function sendKey(code, state) {
  if (running && pod) pod._pd_web_key(code, state);
}

function stopped(title, text, canRestart) {
  running = false;
  $("start").hidden = true;
  $("stopped").hidden = false;
  $("stopped-title").textContent = title;
  $("stopped-text").textContent = text;
  $("restart-btn").hidden = !canRestart;
}

// Largest whole-pixel-ish square that leaves the keyboard room to work.
function fitScreen() {
  const kbdShown = !$("keyboard").hidden;
  const w = Math.min(document.querySelector("#runner").clientWidth, 560) - 16
    - parseFloat(getComputedStyle($("runner")).paddingLeft) - parseFloat(getComputedStyle($("runner")).paddingRight);
  const h = innerHeight * (kbdShown ? 0.5 : 0.85) - 16;
  const size = Math.max(160, Math.floor(Math.min(w, h)));
  $("screen").style.setProperty("--screen-size", size + "px");
}

// Copies the pod's framebuffer to the canvas whenever it changed.
let shownGen = -1, image = null;
function draw() {
  if (!running) return;
  const gen = pod._pd_web_frame_generation();
  const w = pod._pd_web_width(), h = pod._pd_web_height(), ptr = pod._pd_web_frame();
  if (gen !== shownGen && ptr && w > 0 && h > 0) {
    shownGen = gen;
    const cv = $("screen");
    if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; image = null; }
    const ctx = cv.getContext("2d");
    if (!image) image = ctx.createImageData(w, h);
    const src = pod.HEAPU8, d = image.data;
    for (let i = 0, j = ptr, n = w * h; i < n; i++, j += 3) {
      d[i * 4] = src[j]; d[i * 4 + 1] = src[j + 1]; d[i * 4 + 2] = src[j + 2]; d[i * 4 + 3] = 255;
    }
    ctx.putImageData(image, 0, 0);
  }
  requestAnimationFrame(draw);
}

// ---------------------------------------------------------------- SD card panel
function setupPanel() {
  const open = () => { $("panel").hidden = false; listFiles(); $("log").textContent = logLines.join("\n"); };
  $("sd-btn").onclick = open;
  $("panel-close").onclick = () => { $("panel").hidden = true; };
  $("panel").onclick = e => { if (e.target === $("panel")) $("panel").hidden = true; };
  for (const b of document.querySelectorAll("[data-dir]")) b.onclick = () => { $("upload-dir").value = b.dataset.dir; };
  $("upload-btn").onclick = () => {
    if (!pod) { $("panel-hint").textContent = "Power the pod on first: the card opens with it."; return; }
    $("upload-file").click();
  };
  $("upload-file").onchange = async e => {
    const files = [...e.target.files];
    e.target.value = "";
    let dir = "/" + $("upload-dir").value.trim().replace(/^\/+|\/+$/g, "");
    if (dir !== "/") mkdirs("/sd" + dir);
    for (const f of files) {
      const data = new Uint8Array(await f.arrayBuffer());
      pod.FS.writeFile("/sd" + (dir === "/" ? "" : dir) + "/" + f.name, data);
    }
    sdSave();
    $("panel-hint").textContent = `Added ${files.length} file${files.length === 1 ? "" : "s"} to ${dir}. Re-open that folder in the pod to see them.`;
    listFiles();
  };
}

function mkdirs(path) {
  let cur = "";
  for (const part of path.split("/").filter(Boolean)) {
    cur += "/" + part;
    try { pod.FS.mkdir(cur); } catch (_) {}   // exists
  }
}

function walk(dir, out) {
  for (const name of pod.FS.readdir(dir)) {
    if (name === "." || name === "..") continue;
    const p = dir + "/" + name;
    const st = pod.FS.stat(p);
    if (pod.FS.isDir(st.mode)) walk(p, out);
    else out.push({ path: p.slice(3), size: st.size });
  }
  return out;
}

function listFiles() {
  const ul = $("files");
  ul.textContent = "";
  if (!pod) { ul.innerHTML = '<li><span class="hint">The card opens when the pod powers on.</span></li>'; return; }
  const files = walk("/sd", []).sort((a, b) => a.path.localeCompare(b.path));
  if (!files.length) { ul.innerHTML = '<li><span class="hint">The card is empty.</span></li>'; return; }
  for (const f of files) {
    const li = document.createElement("li");
    li.innerHTML = '<span class="path"></span><span class="size"></span><span class="row-btns"></span>';
    li.children[0].textContent = f.path;
    li.children[1].textContent = fmtSize(f.size);
    const save = document.createElement("button");
    save.type = "button"; save.className = "icon-btn"; save.textContent = "Save";
    save.onclick = () => {
      const blob = new Blob([pod.FS.readFile("/sd" + f.path)]);
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = f.path.split("/").pop();
      a.click();
      setTimeout(() => URL.revokeObjectURL(a.href), 5000);
    };
    const del = document.createElement("button");
    del.type = "button"; del.className = "icon-btn"; del.textContent = "Del";
    del.onclick = () => {   // two taps
      if (del.textContent === "Del") { del.textContent = "Sure?"; del.style.color = "var(--danger)"; return; }
      pod.FS.unlink("/sd" + f.path);
      sdSave();
      listFiles();
    };
    li.children[2].append(save, del);
    ul.appendChild(li);
  }
}

// ---------------------------------------------------------------- boot
(function boot() {
  const h = location.hash.slice(1);
  if (h.startsWith("pod-")) showRunner("pod", h.slice(4));
  else if (h.startsWith("lib-")) showRunner("lib", h.slice(4));
  else showLauncher();
})();
