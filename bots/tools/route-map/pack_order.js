// The pack order of the route on the dungeon map (tickets jvbastel/WOWAI#155,
// #156: skipped and added packs).
// It needs the page global `route`; map_page.html sends the "mapdata" event
// (with data.packs and data.pack_order) after it draws its overlay.
//
// The side list shows the hand order (the packs of the order file, numbered)
// and the generator's order (the other packs of the route, in route order).
// Order mode: each click on a pack gives it the next number of the hand
// order; a pack off the route is added to the route there. Skip mode: a
// click on a pack marks it skipped (off the route, out of the hand order),
// or a second click takes the mark off. Each skipped pack shows its aggro
// circle, red when the route line comes within it (it can join a fight as an
// extra pack). Drag a pack in the list to a new place, or from the
// generator's part into the hand order. The x button takes a pack out of the
// hand order (it goes back to the generator's order) or out of the skipped
// packs. Save order writes the hand pack order file of the route
// (pack_order.py): `pack` lines, then `skip` lines.
(() => {
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
    button.po-on { background: #7cf0a0; color: #000; }
    #po-skip.po-on { background: #ff9a6b; }
    #po-panel li.skip .n { color: #ff9a6b; }
    .po-aggro { fill: rgba(255, 154, 107, 0.12); stroke: #ff9a6b; stroke-width: 1.5; stroke-dasharray: 4 3;
                vector-effect: non-scaling-stroke; pointer-events: none; }
    .po-aggro.near { fill: rgba(255, 70, 70, 0.18); stroke: #ff4646; stroke-dasharray: none; }
    .po-cross { stroke: #ff9a6b; stroke-width: 2.5; vector-effect: non-scaling-stroke; pointer-events: none; }`;
  document.head.appendChild(style);

  const panel = document.createElement("div");
  panel.id = "po-panel";
  panel.innerHTML = `<h2>Pack order</h2>
    <button id="po-mode" title="Click packs one after another: each click gives the next number; a pack off the route is added">Order mode</button>
    <button id="po-skip" title="Click a pack to skip it (the route does not pull it), click again to take it back">Skip mode</button>
    <button id="po-save" title="Write the hand pack order file of the route">Save order</button>
    <div id="po-info"></div><div id="po-warnings"></div>
    <h3>Hand order (drag to move, x to give back)</h3><ol id="po-hand"></ol>
    <h3>Generator's order</h3><ol id="po-gen"></ol>
    <h3>Skipped (x to take back)</h3><ol id="po-skipped"></ol>`;
  document.body.appendChild(panel);
  const handList = document.getElementById("po-hand");
  const genList = document.getElementById("po-gen");
  const info = document.getElementById("po-info");
  const modeButton = document.getElementById("po-mode");
  const skipButton = document.getElementById("po-skip");
  const skipList = document.getElementById("po-skipped");
  const warningBox = document.getElementById("po-warnings");

  let packs = new Map();   // key -> pack of the load-map call
  let routeOrder = [];     // keys of the route's packs, in route order
  let hand = [];           // keys of the hand order
  let skipped = [];        // keys of the skipped packs
  let near = new Set();    // keys of the skipped packs that the route line comes within the aggro range of
  let loadWarnings = [];
  let file = "";
  let saved = "[[],[]]";   // the hand order and the skipped packs as last loaded or saved
  let mode = "";           // "", "order" or "skip"
  let group = null;

  const generator = () => routeOrder.filter(key => !hand.includes(key) && !skipped.includes(key));
  const state = () => JSON.stringify([hand, skipped]);
  const nameOf = pack => pack.label || (pack.mobs[0] ? `${pack.mobs[0].name} (off the route)` : `pack ${pack.key}`);

  function listItem(key, number, kind) {
    const gen = kind === "gen";
    const pack = packs.get(key);
    const li = document.createElement("li");
    li.dataset.key = key;
    li.draggable = kind !== "skip";
    li.className = kind;
    li.title = `key ${key}` + (pack ? "\n" + pack.mobs.map(m => `${m.name} (guid ${m.guid})`).join("\n") : "");
    li.innerHTML = `<span class="n"></span><span class="name"></span>`;
    li.querySelector(".n").textContent = gen ? `(${number})` : kind === "skip" ? (near.has(key) ? "near" : "-") : number;
    li.querySelector(".name").textContent = pack ? nameOf(pack) : `key ${key}`;
    if (!gen) {
      const remove = document.createElement("button");
      remove.textContent = "x";
      remove.title = kind === "skip" ? "Take the skip off: the pack keeps the generator's order"
                                     : "Take it out of the hand order: it keeps the generator's order";
      remove.addEventListener("click", () => {
        if (kind === "skip") skipped = skipped.filter(k => k !== key); else hand = hand.filter(k => k !== key);
        changed();
      });
      li.appendChild(remove);
    }
    return li;
  }

  function drawList() {
    handList.replaceChildren(...hand.map((key, i) => listItem(key, i + 1, "hand")));
    genList.replaceChildren(...generator().map(key => listItem(key, routeOrder.indexOf(key) + 1, "gen")));
    skipList.replaceChildren(...skipped.map(key => listItem(key, 0, "skip")));
    const unsaved = state() !== saved;
    const added = hand.filter(key => !routeOrder.includes(key)).length;
    info.className = unsaved ? "unsaved" : "";
    info.textContent = `${hand.length} by hand (${added} added), ${generator().length} by the generator, ` +
      `${skipped.length} skipped; ${file}` + (unsaved ? " (not saved)" : "");
  }

  // The map marks: the aggro circle and a cross on each skipped pack, a ring
  // and the hand number on each pack of the hand order, and in order or skip
  // mode a click target on each pack.
  function drawMarks() {
    if (!group) return;
    group.replaceChildren();
    for (const key of skipped) {
      const pack = packs.get(key);
      if (!pack) continue;
      const circle = el("circle", {class: "po-aggro" + (near.has(key) ? " near" : ""), cx: sx(pack), cy: sy(pack),
                                   r: pack.aggro_radius}, group);
      el("title", {}, circle).textContent = `${nameOf(pack)}: skipped, aggro range ${pack.aggro_radius} yards`;
      el("path", {class: "po-cross", "data-cross": `${sx(pack)} ${sy(pack)} 5`}, group);
    }
    hand.forEach((key, i) => {
      const pack = packs.get(key);
      if (!pack) return;
      el("circle", {class: "po-ring", cx: sx(pack), cy: sy(pack), "data-r": 10}, group);
      el("text", {class: "po-num", x: sx(pack), y: sy(pack), "text-anchor": "end", "data-size": 13, "data-dx": -9,
                  "data-dy": 14, "data-halo": 3}, group).textContent = `h${i + 1}`;
    });
    if (!mode) return;
    for (const pack of packs.values()) {
      if (mode === "order" && skipped.includes(pack.key)) continue;
      const hit = el("circle", {class: "po-hit", cx: sx(pack), cy: sy(pack), "data-r": 9, "data-key": pack.key},
                     group);
      el("title", {}, hit).textContent = `${nameOf(pack)}: ` + (mode === "skip"
        ? (skipped.includes(pack.key) ? "click to take the skip off" : "click to skip it")
        : routeOrder.includes(pack.key) ? "click to give it the next number"
        : "click to add it to the route with the next number");
    }
  }

  function changed() {
    drawList();
    drawMarks();
  }

  // Order mode: a click (not a pan) on a pack puts it last in the hand order
  // (a pack off the route is added). Skip mode: a click skips the pack or
  // takes its skip off. The click does not reach the map, so draw mode does
  // not add a point.
  let down = null;
  document.getElementById("map").addEventListener("mousedown", e => { down = {x: e.clientX, y: e.clientY}; }, true);
  document.getElementById("map").addEventListener("click", e => {
    if (!mode || !e.target.classList.contains("po-hit")) return;
    e.stopPropagation();
    if (down && Math.hypot(e.clientX - down.x, e.clientY - down.y) > 4) return;
    const key = Number(e.target.dataset.key);
    hand = hand.filter(k => k !== key);
    if (mode === "order") hand.push(key);
    else if (skipped.includes(key)) skipped = skipped.filter(k => k !== key);
    else skipped.push(key);
    changed();
  }, true);

  function setMode(next) {
    mode = next;
    modeButton.classList.toggle("po-on", mode === "order");
    skipButton.classList.toggle("po-on", mode === "skip");
    drawMarks();
  }
  modeButton.addEventListener("click", () => setMode(mode === "order" ? "" : "order"));
  skipButton.addEventListener("click", () => setMode(mode === "skip" ? "" : "skip"));

  function showWarnings(skippedNear) {
    near = new Set(skippedNear.map(n => n.key));
    warningBox.textContent = loadWarnings.concat(skippedNear.map(n => n.text)).join("\n");
  }

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
      skipped = skipped.filter(k => k !== key);
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
        method: "POST", headers: {"Content-Type": "application/json"},
        body: JSON.stringify({packs: hand, skips: skipped})});
      const result = await response.json();
      if (!response.ok) throw new Error(result.detail || response.statusText);
      saved = state();
      showWarnings(result.skipped_near);
      changed();
      info.textContent = `Saved ${result.packs} packs and ${result.skips} skipped to ${result.file}.`;
    } catch (error) {
      info.className = "unsaved";
      info.textContent = `Not saved: ${error.message}`;
    }
  });

  window.addEventListener("beforeunload", e => { if (state() !== saved) e.preventDefault(); });

  document.addEventListener("mapdata", e => {
    const data = e.detail;
    const order = data.pack_order;
    packs = new Map(data.packs.map(p => [p.key, p]));
    routeOrder = data.packs.filter(p => p.on_route).sort((a, b) => a.order - b.order).map(p => p.key);
    // A reload (Show) keeps the unsaved hand order and skips of the page.
    if (state() === saved) { hand = [...order.hand]; skipped = [...order.skipped]; }
    saved = JSON.stringify([order.hand, order.skipped]);
    file = order.file + (order.exists ? "" : " (no file yet)");
    loadWarnings = data.pack_warnings.concat(order.warnings);
    showWarnings(order.skipped_near);
    group = el("g", {id: "po-marks"}, document.getElementById("overlay"));
    changed();
  });
})();
