// The pack order of the route on the dungeon map (ticket jvbastel/WOWAI#155).
// It needs the page global `route`; map_page.html sends the "mapdata" event
// (with data.packs and data.pack_order) after it draws its overlay.
//
// The side list shows the hand order (the packs of the order file, numbered)
// and the generator's order (the other packs of the route, in route order).
// Order mode: each click on a pack of the route gives it the next number of
// the hand order. Drag a pack in the list to a new place, or from the
// generator's part into the hand order. The x button takes a pack out of the
// hand order: it goes back to the generator's order. Save order writes the
// hand pack order file of the route (pack_order.py).
(() => {
  const SVG = "http://www.w3.org/2000/svg";
  const style = document.createElement("style");
  style.textContent = `
    #po-panel { position: fixed; right: 8px; top: 52px; width: 270px; max-height: calc(100% - 64px);
                overflow-y: auto; background: rgba(38, 41, 44, 0.95); border: 1px solid #555; padding: 6px 8px;
                z-index: 5; font-size: 13px; }
    #po-panel h2 { font-size: 14px; margin: 2px 0 6px; }
    #po-panel h3 { font-size: 12px; margin: 8px 0 2px; color: var(--muted); font-weight: normal; }
    #po-panel ol { margin: 0; padding: 0; list-style: none; min-height: 18px; }
    #po-panel li { display: flex; gap: 6px; align-items: center; padding: 2px 4px; border: 1px solid transparent;
                   cursor: grab; }
    #po-panel li:hover { border-color: #666; }
    #po-panel li.dragging { opacity: 0.4; }
    #po-panel li .n { min-width: 2.2em; text-align: right; color: #7cf0a0; font-weight: bold; }
    #po-panel li.gen .n { color: var(--muted); font-weight: normal; }
    #po-panel li .name { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    #po-panel li.gen .name { color: var(--muted); }
    #po-panel li button { padding: 0 5px; }
    #po-panel .drop-before { border-top: 2px solid #7cf0a0; }
    #po-warnings { color: #ffcc66; white-space: pre-wrap; }
    #po-info { color: var(--muted); }
    #po-info.unsaved { color: #ffcc66; }
    .po-ring { fill: none; stroke: #7cf0a0; stroke-width: 2.5; vector-effect: non-scaling-stroke; }
    .po-hit { fill: transparent; stroke: none; cursor: pointer; }
    .po-num { fill: #7cf0a0; font-weight: bold; paint-order: stroke; stroke: #000; pointer-events: none; }
    button.po-on { background: #7cf0a0; color: #000; }`;
  document.head.appendChild(style);

  const panel = document.createElement("div");
  panel.id = "po-panel";
  panel.innerHTML = `<h2>Pack order</h2>
    <button id="po-mode" title="Click packs of the route one after another: each click gives the next number">Order mode</button>
    <button id="po-save" title="Write the hand pack order file of the route">Save order</button>
    <div id="po-info"></div><div id="po-warnings"></div>
    <h3>Hand order (drag to move, x to give back)</h3><ol id="po-hand"></ol>
    <h3>Generator's order</h3><ol id="po-gen"></ol>`;
  document.body.appendChild(panel);
  const handList = document.getElementById("po-hand");
  const genList = document.getElementById("po-gen");
  const info = document.getElementById("po-info");
  const modeButton = document.getElementById("po-mode");

  let packs = new Map();   // key -> pack of the load-map call
  let routeOrder = [];     // keys of the route's packs, in route order
  let hand = [];           // keys of the hand order
  let file = "";
  let saved = "[]";        // the hand order as last loaded or saved
  let ordering = false;
  let group = null;
  let yardsPerPixel = 1;
  const sx = p => -p.y, sy = p => -p.x;

  function el(name, attrs, parent) {
    const node = document.createElementNS(SVG, name);
    for (const [k, v] of Object.entries(attrs)) node.setAttribute(k, v);
    parent.appendChild(node);
    return node;
  }

  const generator = () => routeOrder.filter(key => !hand.includes(key));
  const nameOf = pack => pack.label || (pack.mobs[0] ? `${pack.mobs[0].name} (off the route)` : `pack ${pack.key}`);

  function listItem(key, number, gen) {
    const pack = packs.get(key);
    const li = document.createElement("li");
    li.draggable = true;
    li.dataset.key = key;
    li.className = gen ? "gen" : "hand";
    li.title = `key ${key}` + (pack ? "\n" + pack.mobs.map(m => `${m.name} (guid ${m.guid})`).join("\n") : "");
    li.innerHTML = `<span class="n"></span><span class="name"></span>`;
    li.querySelector(".n").textContent = gen ? `(${number})` : number;
    li.querySelector(".name").textContent = pack ? nameOf(pack) : `key ${key}`;
    if (!gen) {
      const remove = document.createElement("button");
      remove.textContent = "x";
      remove.title = "Take it out of the hand order: it keeps the generator's order";
      remove.addEventListener("click", () => { hand = hand.filter(k => k !== key); changed(); });
      li.appendChild(remove);
    }
    return li;
  }

  function drawList() {
    handList.replaceChildren(...hand.map((key, i) => listItem(key, i + 1, false)));
    genList.replaceChildren(...generator().map(key => listItem(key, routeOrder.indexOf(key) + 1, true)));
    const unsaved = JSON.stringify(hand) !== saved;
    info.className = unsaved ? "unsaved" : "";
    info.textContent = `${hand.length} by hand, ${generator().length} by the generator; ${file}` +
      (unsaved ? " (not saved)" : "");
  }

  // The map marks: a ring and the hand number on each pack of the hand
  // order, and in order mode a click target on each pack of the route.
  function drawMarks() {
    if (!group) return;
    group.replaceChildren();
    hand.forEach((key, i) => {
      const pack = packs.get(key);
      if (!pack) return;
      el("circle", {class: "po-ring", cx: sx(pack), cy: sy(pack), r: 10 * yardsPerPixel, "data-r": 10}, group);
      const text = el("text", {class: "po-num", x: sx(pack), y: sy(pack), "text-anchor": "end",
                               "font-size": 13 * yardsPerPixel, "data-po-size": 13}, group);
      text.style.strokeWidth = 3 * yardsPerPixel;
      text.setAttribute("dx", -9 * yardsPerPixel);
      text.setAttribute("dy", 14 * yardsPerPixel);
      text.textContent = `h${i + 1}`;
    });
    if (!ordering) return;
    for (const key of routeOrder) {
      const pack = packs.get(key);
      const hit = el("circle", {class: "po-hit", cx: sx(pack), cy: sy(pack), r: 9 * yardsPerPixel, "data-r": 9,
                                "data-key": key}, group);
      el("title", {}, hit).textContent = `${nameOf(pack)}: click to give it the next number`;
    }
  }

  function changed() { drawList(); drawMarks(); }

  function sizeMarks() {
    if (!group) return;
    for (const node of group.querySelectorAll("[data-po-size]")) {
      node.setAttribute("font-size", node.dataset.poSize * yardsPerPixel);
      node.style.strokeWidth = 3 * yardsPerPixel;
      node.setAttribute("dx", -9 * yardsPerPixel);
      node.setAttribute("dy", 14 * yardsPerPixel);
    }
  }

  // Order mode: a click (not a pan) on a pack of the route puts it last in
  // the hand order. The click does not reach the map, so draw mode does not
  // add a point.
  let down = null;
  document.getElementById("map").addEventListener("mousedown", e => { down = {x: e.clientX, y: e.clientY}; }, true);
  document.getElementById("map").addEventListener("click", e => {
    if (!ordering || !e.target.classList.contains("po-hit")) return;
    e.stopPropagation();
    if (down && Math.hypot(e.clientX - down.x, e.clientY - down.y) > 4) return;
    const key = Number(e.target.dataset.key);
    hand = hand.filter(k => k !== key).concat([key]);
    changed();
  }, true);

  function setOrdering(on) {
    ordering = on;
    modeButton.classList.toggle("po-on", on);
    drawMarks();
  }
  modeButton.addEventListener("click", () => setOrdering(!ordering));

  // Drag in the list: drop before the item under the pointer, or at the end
  // of the hand order. A pack dragged from the generator's part joins the
  // hand order; one dragged to the generator's part leaves it.
  let dragged = null;
  panel.addEventListener("dragstart", e => {
    const li = e.target.closest && e.target.closest("li");
    if (!li) return;
    dragged = Number(li.dataset.key);
    li.classList.add("dragging");
    e.dataTransfer.effectAllowed = "move";
    e.dataTransfer.setData("text/plain", String(dragged));
  });
  panel.addEventListener("dragend", () => {
    dragged = null;
    for (const node of panel.querySelectorAll(".dragging, .drop-before")) node.classList.remove("dragging", "drop-before");
  });
  for (const list of [handList, genList]) {
    list.addEventListener("dragover", e => {
      if (dragged === null) return;
      e.preventDefault();
      for (const node of panel.querySelectorAll(".drop-before")) node.classList.remove("drop-before");
      const li = e.target.closest("li");
      if (li && list === handList) li.classList.add("drop-before");
    });
    list.addEventListener("drop", e => {
      if (dragged === null) return;
      e.preventDefault();
      const key = dragged;
      hand = hand.filter(k => k !== key);
      if (list === handList) {
        const li = e.target.closest("li");
        const at = li ? hand.indexOf(Number(li.dataset.key)) : -1;
        hand.splice(at < 0 ? hand.length : at, 0, key);
      }
      changed();
    });
  }

  document.getElementById("po-save").addEventListener("click", async () => {
    info.textContent = "Saving...";
    try {
      const response = await fetch(`/api/maps/${encodeURIComponent(route)}/order`, {
        method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify({packs: hand})});
      const result = await response.json();
      if (!response.ok) throw new Error(result.detail || response.statusText);
      saved = JSON.stringify(hand);
      drawList();
      info.textContent = `Saved ${result.packs} packs to ${result.file}.`;
    } catch (error) {
      info.className = "unsaved";
      info.textContent = `Not saved: ${error.message}`;
    }
  });

  window.addEventListener("beforeunload", e => { if (JSON.stringify(hand) !== saved) e.preventDefault(); });

  document.addEventListener("mapdata", e => {
    const data = e.detail;
    const order = data.pack_order;
    packs = new Map(data.packs.map(p => [p.key, p]));
    routeOrder = data.packs.filter(p => p.on_route).sort((a, b) => a.order - b.order).map(p => p.key);
    // A reload (Show) keeps the unsaved hand order of the page.
    if (JSON.stringify(hand) === saved) hand = [...order.hand];
    saved = JSON.stringify(order.hand);
    file = order.file + (order.exists ? "" : " (no file yet)");
    document.getElementById("po-warnings").textContent = order.warnings.join("\n");
    group = el("g", {id: "po-marks"}, document.getElementById("overlay"));
    changed();
  });
  document.addEventListener("mapview", e => { yardsPerPixel = e.detail; sizeMarks(); });
})();
