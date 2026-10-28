// Regions mode of the dungeon map (ticket jvbastel/WOWAI#182, spec #179):
// the player outlines regions of stacked ground and saves them with the
// dungeon. regions.py owns the regions file and its rules; this page part
// only shows the regions and sends them. It needs the globals of
// map_page.html (map, route, view) and of map_common.js (el, sx, sy, toMap),
// and the Draw button of map_draw.js (the Regions button sits beside it).
//
// In Regions mode a click does not add a route point. Clicks make the
// outline of a new region; a click on its first point (or "Close") closes
// it. The next click on the ground sets the height range: 5 yd below to
// 5 yd above the ground there (where ground stacks, the panel asks which).
// A new region is named "Region N". "Save regions" writes the regions file
// of the map; the server refuses a region that is not valid.
(() => {
  const style = document.createElement("style");
  style.textContent = `
    .rg-area { fill: rgba(120, 190, 255, 0.12); stroke: #78beff; stroke-width: 1.5; vector-effect: non-scaling-stroke;
               pointer-events: none; }
    .rg-new { fill: rgba(255, 225, 77, 0.10); stroke: #ffe14d; stroke-width: 2; vector-effect: non-scaling-stroke;
              stroke-dasharray: 5 3; pointer-events: none; }
    .rg-point { fill: #ffe14d; stroke: #000; stroke-width: 1; vector-effect: non-scaling-stroke; pointer-events: none; }
    .rg-point.first { fill: #fff; cursor: pointer; pointer-events: all; }
    .rg-name { fill: #bfe0ff; paint-order: stroke; stroke: #000; stroke-linejoin: round; pointer-events: none; }
    button.rg-on { background: #78beff; color: #000; }
    #rg-panel { position: fixed; left: 8px; bottom: 8px; width: 240px; max-height: 40%; overflow-y: auto;
                background: rgba(38, 41, 44, 0.95); border: 1px solid #555; padding: 6px 8px; display: none; }
    #rg-panel h2 { font-size: 14px; margin: 2px 0 6px; }
    #rg-panel ol { margin: 6px 0; padding-left: 1.4em; }
    #rg-panel .heights { color: var(--muted); }
    #rg-info { color: var(--muted); margin: 4px 0; white-space: pre-wrap; }
    #rg-info.error { color: #ff8a80; }`;
  document.head.appendChild(style);

  const modeButton = document.createElement("button");
  modeButton.id = "rg-mode";
  modeButton.textContent = "Regions";
  modeButton.title = "Outline regions of stacked ground: clicks make an outline, they add no route point";
  const drawButton = document.getElementById("draw-mode");
  drawButton.after(" ", modeButton);

  const panel = document.createElement("div");
  panel.id = "rg-panel";
  panel.innerHTML = `<h2>Regions</h2>
    <div id="rg-step"></div>
    <button id="rg-close" title="Close the outline (or click its first point)">Close</button>
    <button id="rg-cancel" title="Drop the outline being drawn (Esc)">Cancel</button>
    <div id="rg-floors"></div>
    <ol id="rg-list"></ol>
    <button id="rg-save" title="Write the regions file of the map">Save regions</button>
    <div id="rg-info"></div>`;
  document.body.appendChild(panel);
  const step = document.getElementById("rg-step");
  const floorsBox = document.getElementById("rg-floors");
  const list = document.getElementById("rg-list");
  const info = document.getElementById("rg-info");
  const layer = el("g", {id: "rg-layer"}, map);

  const api = path => `/api/maps/${encodeURIComponent(route)}/${path}`;
  let regions = [];     // {name, low, high, outline: [[x, y], ...]}, as the server gives them
  let file = "";
  let on = false;
  let outline = [];     // the WoW (x, y) points of the outline being drawn
  let closed = false;   // the outline is closed: the next click sets the height range

  function say(text, error) {
    info.textContent = text;
    info.className = error ? "error" : "";
  }

  // The SVG points of WoW (x, y) pairs.
  const svgPoints = points => points.map(([x, y]) => `${sx({y})},${sy({x})}`).join(" ");

  function redraw() {
    layer.replaceChildren();
    for (const region of regions) {
      el("polygon", {class: "rg-area", points: svgPoints(region.outline)}, layer);
      const n = region.outline.length;
      const mid = region.outline.reduce(([a, b], [x, y]) => [a + x / n, b + y / n], [0, 0]);
      el("text", {class: "rg-name", x: sx({y: mid[1]}), y: sy({x: mid[0]}), "data-size": 12, "data-halo": 3,
                  "data-dx": 0, "data-dy": 0, "text-anchor": "middle"}, layer).textContent = region.name;
    }
    if (outline.length) {
      el(closed ? "polygon" : "polyline", {class: "rg-new", points: svgPoints(outline)}, layer);
      outline.forEach(([x, y], i) => el("circle", {class: "rg-point" + (i === 0 && !closed ? " first" : ""),
        cx: sx({y}), cy: sy({x}), "data-r": i === 0 ? 6 : 4}, layer));
    }
    list.replaceChildren(...regions.map(region => {
      const li = document.createElement("li");
      li.innerHTML = `<span class="name"></span> <span class="heights"></span>`;
      li.querySelector(".name").textContent = region.name;
      li.querySelector(".heights").textContent =
        `z ${region.low} to ${region.high}, ${region.outline.length} points`;
      return li;
    }));
    step.textContent = !on ? "" : closed ? "Click the ground of the region to set its height range."
      : outline.length ? `${outline.length} points: click on, then click the first point or Close.`
      : "Click the map to start the outline of a new region.";
  }

  function newName() {
    const names = new Set(regions.map(r => r.name));
    let n = regions.length + 1;
    while (names.has(`Region ${n}`)) n++;
    return `Region ${n}`;
  }

  function closeOutline() {
    if (outline.length < 3) { say("An outline needs at least 3 points.", true); return; }
    closed = true;
    say("");
    redraw();
  }

  function cancel() {
    outline = []; closed = false; floorsBox.replaceChildren();
    redraw();
  }

  // The height range of the new region: 5 yd below to 5 yd above the ground z.
  function finish(z) {
    const round = v => Math.round(v * 100) / 100;
    const region = {name: newName(), low: round(z - 5), high: round(z + 5), outline};
    regions.push(region);
    say(`${region.name}: z ${region.low} to ${region.high}. Not saved yet.`);
    cancel();
  }

  async function setHeight(event) {
    const at = toMap(event);
    const params = new URLSearchParams({x: -at.y, y: -at.x});
    const response = await fetch(api(`snap?${params}`));
    const found = await response.json();
    if (!response.ok) { say(found.detail || response.statusText, true); return; }
    if (found.floors.length <= 1) { finish(found.point.z); return; }
    // Stacked ground: the player picks the layer of the region.
    floorsBox.replaceChildren("Ground at z: ", ...found.floors.slice().reverse().map(z => {
      const button = document.createElement("button");
      button.textContent = z;
      button.addEventListener("click", () => finish(z));
      return button;
    }));
  }

  // A click (not a pan) in Regions mode. It reaches no other mode: draw mode
  // is off while Regions mode is on, and the event stops here.
  let down = null;
  map.addEventListener("mousedown", e => { down = {x: e.clientX, y: e.clientY}; }, true);
  map.addEventListener("click", e => {
    if (!on || !view) return;
    e.stopPropagation();
    if (down && Math.hypot(e.clientX - down.x, e.clientY - down.y) > 4) return;  // a pan
    if (closed) { setHeight(e); return; }
    if (e.target.classList.contains("first")) { closeOutline(); return; }
    const at = toMap(e);
    outline.push([Math.round(-at.y * 100) / 100, Math.round(-at.x * 100) / 100]);
    redraw();
  }, true);
  window.addEventListener("keydown", e => { if (on && e.key === "Escape") cancel(); });

  function setOn(next) {
    on = next;
    modeButton.classList.toggle("rg-on", on);
    panel.style.display = on || regions.length ? "block" : "none";
    for (const id of ["rg-close", "rg-cancel"]) document.getElementById(id).style.display = on ? "" : "none";
    if (on && drawButton.classList.contains("on")) drawButton.click();  // Draw mode off
    if (on) map.style.cursor = "crosshair";
    else if (!drawButton.classList.contains("on")) map.style.cursor = "";
    if (!on) cancel(); else redraw();
  }
  modeButton.addEventListener("click", () => setOn(!on));
  drawButton.addEventListener("click", () => { if (on && drawButton.classList.contains("on")) setOn(false); });
  document.getElementById("rg-close").addEventListener("click", closeOutline);
  document.getElementById("rg-cancel").addEventListener("click", cancel);

  document.getElementById("rg-save").addEventListener("click", async () => {
    const response = await fetch(api("regions"), {method: "POST", headers: {"Content-Type": "application/json"},
                                                  body: JSON.stringify({regions})});
    const data = await response.json();
    if (!response.ok) { say(data.detail || response.statusText, true); return; }
    regions = data.regions;
    say(`Saved ${regions.length} regions to ${data.file}.`);
    redraw();
  });

  // The regions of the map, once per page: every route of the map shares them.
  async function load() {
    if (!route) return;
    const response = await fetch(api("regions"));
    const data = await response.json();
    if (!response.ok) {
      say(`The regions file does not read: ${data.detail || response.statusText}`, true);
      panel.style.display = "block";
      return;
    }
    regions = data.regions; file = data.file;
    if (regions.length) say(`${regions.length} regions from ${file}.`);
    setOn(on);
  }
  load();
})();
