// PicoPods Web - the PicoCalc keyboard on a touch screen, and the PC keyboard.
//
// Both send pd::key codes (lib/hardware/input/include/pd/keys.h) as press /
// release pairs, like the PicoCalc's keyboard controller: printable keys send
// the character they type (Shift+a is 'A'), modifiers send their own events.
// On the touch keyboard Shift, Ctrl and Alt are sticky: tap once for the next
// key, twice to lock. Keys stay down while touched, so PodBoy's buttons hold.
"use strict";

const PD_KEY = {
  Backspace: 0x08, Tab: 0x09, Enter: 0x0A, Esc: 0xB1,
  Left: 0xB4, Up: 0xB5, Down: 0xB6, Right: 0xB7,
  Break: 0xD0, Insert: 0xD1, Home: 0xD2, Del: 0xD4, End: 0xD5, PageUp: 0xD6, PageDown: 0xD7,
  F1: 0x81, F2: 0x82, F3: 0x83, F4: 0x84, F5: 0x85, F6: 0x86, F7: 0x87, F8: 0x88, F9: 0x89, F10: 0x90,
  Alt: 0xA1, ShiftL: 0xA2, ShiftR: 0xA3, Ctrl: 0xA5, CapsLock: 0xC1,
};
const PRESSED = 1, RELEASED = 3;

// US shifted pairs, as printed on the PicoCalc's keys.
const SHIFTED = {
  "1": "!", "2": "@", "3": "#", "4": "$", "5": "%", "6": "^", "7": "&", "8": "*", "9": "(", "0": ")",
  "-": "_", "=": "+", "[": "{", "]": "}", "\\": "|", ";": ":", "'": "\"", "`": "~", ",": "<", ".": ">", "/": "?",
};

// Rows of the touch keyboard. A string is a character key; an object is a
// special key: {l: legend, k: code or modifier name, w: width in columns,
// s: legend/code with Shift, fn: dim cap}. Rows 1-4 swap with the Sym layer.
const K = (l, k, extra) => Object.assign({ l, k }, extra);
const FN_ROW = [
  K("Esc", PD_KEY.Esc, { fn: 1 }), K("Tab", PD_KEY.Tab, { fn: 1 }),
  K("F1", PD_KEY.F1, { fn: 1, s: ["F6", PD_KEY.F6] }), K("F2", PD_KEY.F2, { fn: 1, s: ["F7", PD_KEY.F7] }),
  K("F3", PD_KEY.F3, { fn: 1, s: ["F8", PD_KEY.F8] }), K("F4", PD_KEY.F4, { fn: 1, s: ["F9", PD_KEY.F9] }),
  K("F5", PD_KEY.F5, { fn: 1, s: ["F10", PD_KEY.F10] }), K("Del", PD_KEY.Del, { fn: 1 }),
  K("Bksp", PD_KEY.Backspace, { fn: 1, w: 2 }),
];
const ENTER = K("Enter", PD_KEY.Enter, { fn: 1 });
const SHIFT = K("Shift", "shift", { fn: 1 });
const UP = K("▲", PD_KEY.Up, { fn: 1 });
const BOTTOM_ROW = [
  K("Ctrl", "ctrl", { fn: 1 }), K("Alt", "alt", { fn: 1 }), K("Sym", "sym", { fn: 1 }),
  K("space", " ", { w: 3 }), ",",
  K("◀", PD_KEY.Left, { fn: 1 }), K("▼", PD_KEY.Down, { fn: 1 }), K("▶", PD_KEY.Right, { fn: 1 }),
];
const LAYERS = {
  abc: [
    [..."1234567890"],
    [..."qwertyuiop"],
    [..."asdfghjkl", ENTER],
    [SHIFT, ..."zxcvbnm", UP, "."],
  ],
  sym: [
    [..."!@#$%^&*()"],
    [..."-_=+[]{}\\|"],
    [...";:'\"`~<>?", ENTER],
    [SHIFT, K("Home", PD_KEY.Home, { fn: 1 }), K("End", PD_KEY.End, { fn: 1 }), K("PgUp", PD_KEY.PageUp, { fn: 1 }),
     K("PgDn", PD_KEY.PageDown, { fn: 1 }), K("Ins", PD_KEY.Insert, { fn: 1 }), "/", K("Brk", PD_KEY.Break, { fn: 1 }), UP, "."],
  ],
};

class PicoKeyboard {
  constructor(root, send) {
    this.root = root;
    this.send = send;   // (code, state)
    this.layer = "abc";
    this.mods = { shift: 0, ctrl: 0, alt: 0 };   // 0 off, 1 next key, 2 locked
    this.lastTap = {};
    this.active = new Map();   // pointerId -> {el, code, mods}
    this.render();
    root.addEventListener("pointerdown", e => this.down(e));
    for (const t of ["pointerup", "pointercancel", "lostpointercapture"]) root.addEventListener(t, e => this.up(e));
    root.addEventListener("contextmenu", e => e.preventDefault());
  }

  render() {
    const rows = [FN_ROW, ...LAYERS[this.layer], BOTTOM_ROW];
    this.root.textContent = "";
    for (const row of rows) {
      const r = document.createElement("div");
      r.className = "krow";
      for (const spec of row) r.appendChild(this.cap(typeof spec === "string" ? K(spec, spec) : spec));
      this.root.appendChild(r);
    }
    this.paint();
  }

  cap(spec) {
    const el = document.createElement("div");
    el.className = "key" + (spec.fn ? " fn" : "") + (spec.w ? " wide" + spec.w : "");
    el.setAttribute("role", "button");
    el._spec = spec;
    el.dataset.legend = spec.l;
    el.textContent = spec.l;
    if (typeof spec.k === "string" && spec.k.length === 1 && SHIFTED[spec.k]) {
      const alt = document.createElement("span");
      alt.className = "alt";
      alt.textContent = SHIFTED[spec.k];
      el.appendChild(alt);
    }
    return el;
  }

  // Legends follow Shift (letters upper case, F1-F5 become F6-F10).
  paint() {
    const shift = this.mods.shift > 0;
    for (const el of this.root.querySelectorAll(".key")) {
      const s = el._spec;
      const m = s.k === "shift" || s.k === "ctrl" || s.k === "alt" ? this.mods[s.k] : 0;
      el.classList.toggle("armed", m === 1);
      el.classList.toggle("locked", m === 2 || (s.k === "sym" && this.layer === "sym"));
      if (s.s) el.firstChild.textContent = shift ? s.s[0] : s.l;
      else if (typeof s.k === "string" && /^[a-z]$/.test(s.k)) el.firstChild.textContent = shift ? s.l.toUpperCase() : s.l;
    }
  }

  // Code for a key with the current modifiers (null for layer / modifier keys).
  codeFor(spec) {
    const shift = this.mods.shift > 0;
    if (typeof spec.k === "number") return shift && spec.s ? spec.s[1] : spec.k;
    if (spec.k.length !== 1) return null;
    let c = spec.k;
    if (shift) c = /[a-z]/.test(c) ? c.toUpperCase() : (SHIFTED[c] || c);
    return c.charCodeAt(0);
  }

  toggle(name) {
    const now = performance.now();
    const double = now - (this.lastTap[name] || 0) < 350;
    this.lastTap[name] = now;
    this.mods[name] = this.mods[name] ? (double && this.mods[name] === 1 ? 2 : 0) : 1;
    this.paint();
  }

  down(e) {
    const el = e.target.closest(".key");
    if (!el) return;
    e.preventDefault();
    const spec = el._spec;
    if (spec.k === "sym") { this.layer = this.layer === "sym" ? "abc" : "sym"; this.render(); return; }
    if (spec.k === "shift" || spec.k === "ctrl" || spec.k === "alt") { this.toggle(spec.k); buzz(); return; }
    const code = this.codeFor(spec);
    if (code == null) return;
    try { el.setPointerCapture(e.pointerId); } catch (_) {}
    const mods = [];
    if (this.mods.ctrl) mods.push(PD_KEY.Ctrl);
    if (this.mods.alt) mods.push(PD_KEY.Alt);
    if (this.mods.shift) mods.push(PD_KEY.ShiftL);
    for (const m of mods) this.send(m, PRESSED);
    this.send(code, PRESSED);
    el.classList.add("down");
    this.active.set(e.pointerId, { el, code, mods });
    buzz();
  }

  up(e) {
    const a = this.active.get(e.pointerId);
    if (!a) return;
    this.active.delete(e.pointerId);
    a.el.classList.remove("down");
    this.send(a.code, RELEASED);
    for (const m of a.mods.reverse()) this.send(m, RELEASED);
    // One-shot modifiers are used up by the key; locked ones stay.
    let changed = false;
    for (const n of ["shift", "ctrl", "alt"]) if (this.mods[n] === 1) { this.mods[n] = 0; changed = true; }
    if (changed) this.paint();
  }

  releaseAll() {
    for (const id of [...this.active.keys()]) this.up({ pointerId: id });
  }
}

function buzz() {
  try { if (navigator.vibrate) navigator.vibrate(6); } catch (_) {}
}

// PC keyboard (desktop browsers, or a Bluetooth keyboard on the phone).
const PC_SPECIAL = {
  ArrowUp: PD_KEY.Up, ArrowDown: PD_KEY.Down, ArrowLeft: PD_KEY.Left, ArrowRight: PD_KEY.Right,
  Enter: PD_KEY.Enter, Escape: PD_KEY.Esc, Backspace: PD_KEY.Backspace, Tab: PD_KEY.Tab,
  Delete: PD_KEY.Del, Insert: PD_KEY.Insert, Home: PD_KEY.Home, End: PD_KEY.End,
  PageUp: PD_KEY.PageUp, PageDown: PD_KEY.PageDown, Pause: PD_KEY.Break, CapsLock: PD_KEY.CapsLock,
  F1: PD_KEY.F1, F2: PD_KEY.F2, F3: PD_KEY.F3, F4: PD_KEY.F4, F5: PD_KEY.F5,
  F6: PD_KEY.F6, F7: PD_KEY.F7, F8: PD_KEY.F8, F9: PD_KEY.F9, F10: PD_KEY.F10,
};

function pcKeyCode(e) {
  if (e.code === "ShiftLeft") return PD_KEY.ShiftL;
  if (e.code === "ShiftRight") return PD_KEY.ShiftR;
  if (e.key === "Control") return PD_KEY.Ctrl;
  if (e.code === "AltLeft") return PD_KEY.Alt;   // right Alt is AltGr: its characters come through e.key
  if (PC_SPECIAL[e.key] !== undefined) return PC_SPECIAL[e.key];
  if (e.key.length === 1) {
    const c = e.key.charCodeAt(0);
    if (c >= 0x20 && c < 0x7F) return c;   // ASCII only, like the PicoCalc
  }
  return null;
}

// Sends PC key events while `enabled()` is true. Returns nothing; releases are
// matched to presses by physical key, so a key released after Shift still
// releases the character it pressed.
function attachPcKeyboard(send, enabled) {
  const held = new Map();   // e.code -> pd code
  addEventListener("keydown", e => {
    if (!enabled()) return;
    const code = pcKeyCode(e);
    if (code == null) return;
    e.preventDefault();
    if (e.repeat || held.has(e.code)) return;   // the pod does its own auto-repeat
    held.set(e.code, code);
    send(code, PRESSED);
  });
  addEventListener("keyup", e => {
    const code = held.get(e.code);
    if (code === undefined) return;
    held.delete(e.code);
    e.preventDefault();
    send(code, RELEASED);
  });
  addEventListener("blur", () => {
    for (const code of held.values()) send(code, RELEASED);
    held.clear();
  });
}
