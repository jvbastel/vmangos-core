// Every pack of the dungeon on the map (ticket jvbastel/WOWAI#153): the
// packs on the route (filled, labelled with their place in the pack order
// and their `packN` name), the packs off the route (hollow and dim), boss
// names on boss packs, patrol paths with arrows in their walk direction,
// and the wander circle of each wandering mob. map_page.html sends the
// "mapdata" event after it draws the route into its overlay, and the
// "mapview" event with the yards per pixel at each zoom. The page sizes the
// nodes with data-r, data-size and data-halo (map_common.js sizeMarks); the
// arrows are sized here. Text strokes do not keep their size on screen (no
// non-scaling-stroke on text in Chrome), so each label's halo is data-halo.
(() => {
  const style = document.createElement("style");
  style.textContent = `
    .pk { stroke: #000; stroke-width: 1; vector-effect: non-scaling-stroke; }
    .pk.on { fill: var(--pack); }
    .pk.on.boss { fill: var(--boss); }
    .pk.off { fill: rgba(200, 200, 200, 0.15); stroke: #c8c8c8; stroke-width: 1.5; stroke-dasharray: 3 2; }
    .pk.off.boss { stroke: var(--boss); }
    .pk-label { fill: #fff; paint-order: stroke; stroke: #000; stroke-linejoin: round; pointer-events: none; }
    .pk-label.off { fill: #bdb8ae; }
    .pk-label .mobs { fill: #d8d2c4; }
    .pk-label .bossname { fill: #ffb27a; font-weight: bold; }
    .patrol { fill: none; stroke: #c792ff; stroke-width: 1.5; vector-effect: non-scaling-stroke;
              stroke-dasharray: 5 3; }
    .patrol-arrow { fill: #c792ff; stroke: none; }
    .wander { fill: rgba(127, 219, 202, 0.08); stroke: #7fdbca; stroke-width: 1; vector-effect: non-scaling-stroke; }
    #packs-legend .key-on { color: var(--pack); } #packs-legend .key-off { color: #c8c8c8; }
    #packs-legend .key-patrol { color: #c792ff; } #packs-legend .key-wander { color: #7fdbca; }`;
  document.head.appendChild(style);

  const layers = {off: "Off-route packs", patrol: "Patrols", wander: "Wanderers"};
  const shown = {off: true, patrol: true, wander: true};
  const controls = document.createElement("span");
  controls.id = "packs-legend";
  controls.innerHTML = `<span class="key-on">&#9679; on route</span> ` + Object.entries(layers).map(([k, name]) =>
    `<label class="key-${k}"><input type="checkbox" data-layer="${k}" checked> ${name}</label>`).join(" ");
  const header = document.querySelector("header");
  header.insertBefore(controls, document.getElementById("status"));

  let groups = {};
  let yardsPerPixel = 1;

  // A first word that 2 or more mob names of the map share ("Ragefire"
  // Trogg, Shaman, ...) is left out of the pack labels, as in the prototype.
  let prefixes = new Set();
  function sharedPrefixes(packs) {
    const names = new Map();
    for (const pack of packs) for (const m of pack.mobs) {
      const words = (m.name || "").split(" ");
      if (words.length > 1) names.set(words[0], (names.get(words[0]) || new Set()).add(m.name));
    }
    return new Set([...names].filter(([, all]) => all.size >= 2).map(([word]) => word));
  }

  // "Shaman, 2x Trogg": the mob names of a pack, counted.
  function mobSummary(pack) {
    const counts = new Map();
    for (const m of pack.mobs) {
      const words = (m.name || `#${m.entry}`).split(" ");
      const name = words.length > 1 && prefixes.has(words[0]) ? words.slice(1).join(" ") : words.join(" ");
      counts.set(name, (counts.get(name) || 0) + 1);
    }
    return [...counts].map(([name, n]) => (n > 1 ? `${n}x ` : "") + name).join(", ");
  }

  // An arrow head at (x, y) in SVG yards, pointing at angle a, 7 pixels long.
  function arrowPath(x, y, a) {
    const d = 7 * yardsPerPixel, w = 3.5 * yardsPerPixel, c = Math.cos(a), s = Math.sin(a);
    return `M${x + c * d / 2} ${y + s * d / 2}L${x - c * d / 2 - s * w} ${y - s * d / 2 + c * w}` +
           `L${x - c * d / 2 + s * w} ${y - s * d / 2 - c * w}Z`;
  }

  function sizeArrows() {
    for (const node of document.querySelectorAll(".patrol-arrow")) {
      const [x, y, a] = node.dataset.arrow.split(" ").map(Number);
      node.setAttribute("d", arrowPath(x, y, a));
    }
  }

  function drawPatrol(patrol, parent) {
    const points = patrol.points.map(p => [sx(p), sy(p)]);
    const line = drawLine(patrol.points, {class: "patrol"}, parent);
    el("title", {}, line).textContent = `Patrol: ${patrol.name} (guid ${patrol.guid}, pack ${patrol.pack})`;
    // An arrow in the middle of each leg, in walk order.
    for (let i = 0; i + 1 < points.length; i++) {
      const [x1, y1] = points[i], [x2, y2] = points[i + 1];
      if (Math.hypot(x2 - x1, y2 - y1) < 1) continue;
      el("path", {class: "patrol-arrow",
                  "data-arrow": `${(x1 + x2) / 2} ${(y1 + y2) / 2} ${Math.atan2(y2 - y1, x2 - x1)}`,
                  ...pieceMark(patrol.points[i], patrol.points[i + 1])}, parent);
    }
  }

  function drawPack(pack, parent) {
    const kind = pack.on_route ? "on" : "off";
    const boss = pack.bosses.length > 0;
    const dot = el("circle", {class: `pk ${kind}` + (boss ? " boss" : ""), cx: sx(pack), cy: sy(pack),
                              "data-r": pack.on_route ? 6 : 5, ...regionMark(pack)}, parent);
    el("title", {}, dot).textContent =
      (pack.on_route ? `${pack.label} (#${pack.order} in the pack order)` : "Not on the route") +
      `\nkey ${pack.key}\n` + pack.mobs.map(m => `${m.name} (${m.entry}, guid ${m.guid})`).join("\n");
    const label = el("text", {class: `pk-label ${kind}`, x: sx(pack), y: sy(pack),
                              "data-size": pack.on_route ? 12 : 10, "data-halo": 3, ...regionMark(pack)}, parent);
    const first = pack.on_route ? `${pack.order} ${pack.label}` : (boss ? "" : mobSummary(pack));
    const lines = [];
    if (first) lines.push([first, ""]);
    if (boss) lines.push([pack.bosses.join(", "), "bossname"]);
    else if (pack.on_route) lines.push([mobSummary(pack), "mobs"]);
    lines.forEach(([text, cls], i) => {
      const span = el("tspan", i === 0 ? {} : {x: sx(pack), dx: "0.58em", dy: "1.15em"}, label);
      if (cls) span.setAttribute("class", cls);
      span.textContent = text;
    });
  }

  function draw(data) {
    const overlay = document.getElementById("overlay");
    groups = {};
    prefixes = sharedPrefixes(data.packs);
    for (const name of ["wander", "patrol", "off", "on"]) {
      groups[name] = el("g", {id: `packs-${name}`}, overlay);
    }
    for (const w of data.wanderers || []) {
      const circle = el("circle", {class: "wander", cx: sx(w), cy: sy(w), r: w.radius, ...regionMark(w)}, groups.wander);
      el("title", {}, circle).textContent = `${w.name} wanders ${w.radius} yd (guid ${w.guid})`;
    }
    for (const p of data.patrols || []) drawPatrol(p, groups.patrol);
    // Off-route packs first, so the route's packs and labels sit on top.
    for (const pack of data.packs) if (!pack.on_route) drawPack(pack, groups.off);
    for (const pack of data.packs) if (pack.on_route) drawPack(pack, groups.on);
    for (const [k, on] of Object.entries(shown)) groups[k].style.display = on ? "" : "none";
    sizeArrows();
  }

  controls.addEventListener("change", e => {
    const k = e.target.dataset.layer;
    if (!k) return;
    shown[k] = e.target.checked;
    if (groups[k]) groups[k].style.display = shown[k] ? "" : "none";
  });
  document.addEventListener("mapdata", e => draw(e.detail));
  document.addEventListener("mapview", e => { yardsPerPixel = e.detail; sizeArrows(); });
})();
