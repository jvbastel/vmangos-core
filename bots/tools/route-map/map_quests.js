// The quest objectives on the map (ticket jvbastel/WOWAI#154): a pin at each
// quest visit point, a quest badge on each pack that holds a mob to kill or
// loot for a quest (hover: the quest and the count in that pack), and a list
// of the objectives that the route does not meet, with the packs that could
// meet them (click one to show it). map_page.html sends the "mapdata" event
// after it draws the route, and the "mapview" event with the yards per pixel
// at each zoom; the badges and pins keep their size on screen here.
(() => {
  const style = document.createElement("style");
  style.textContent = `
    .q-badge { fill: #ffd84a; stroke: #000; stroke-width: 1; vector-effect: non-scaling-stroke; cursor: help; }
    .q-badge.met { fill: #8fd16a; }
    .q-badge-text { fill: #000; font-weight: bold; text-anchor: middle; dominant-baseline: central;
                    pointer-events: none; }
    .q-pin { fill: #ffd84a; stroke: #000; stroke-width: 1; vector-effect: non-scaling-stroke; cursor: help; }
    .q-pin.met { fill: #8fd16a; }
    .q-flash { fill: none; stroke: #ffd84a; stroke-width: 3; vector-effect: non-scaling-stroke; }
    #quests-legend { color: #ffd84a; }
    #quests-panel { position: fixed; right: 8px; max-width: 340px; max-height: 70%;
                    overflow: auto; background: rgba(38, 41, 44, 0.94); border: 1px solid #555; padding: 6px 10px;
                    font-size: 13px; }
    #quests-panel h2 { font-size: 13px; margin: 2px 0 6px; color: #ffd84a; }
    #quests-panel ul { margin: 0 0 6px; padding-left: 16px; }
    #quests-panel .q-pack { color: var(--pack); cursor: pointer; text-decoration: underline dotted; }
    #quests-panel .muted { color: var(--muted); }`;
  document.head.appendChild(style);

  const controls = document.createElement("label");
  controls.id = "quests-legend";
  controls.innerHTML = `<input type="checkbox" checked> Quests`;
  document.querySelector("header").insertBefore(controls, document.getElementById("status"));
  const panel = document.createElement("div");
  panel.id = "quests-panel";
  document.body.appendChild(panel);

  let group = null, shown = true, yardsPerPixel = 1, packsByKey = new Map();

  // A pin: a drop shape whose tip is on the visit point, 14 pixels high.
  function pinPath(x, y) {
    const k = yardsPerPixel, r = 5 * k, h = 14 * k;
    return `M${x} ${y}L${x - r} ${y - h + r}A${r} ${r} 0 1 1 ${x + r} ${y - h + r}Z`;
  }

  // Badges sit up and left of their pack's dot, a fixed number of pixels off.
  function place() {
    if (!group) return;
    const k = yardsPerPixel;
    for (const node of group.querySelectorAll("[data-badge]")) {
      const [x, y] = node.dataset.badge.split(" ").map(Number);
      const cx = x - 9 * k, cy = y - 9 * k;
      if (node.tagName === "circle") { node.setAttribute("cx", cx); node.setAttribute("cy", cy); node.setAttribute("r", 5 * k); }
      else { node.setAttribute("x", cx); node.setAttribute("y", cy); node.setAttribute("font-size", 8 * k); }
    }
    for (const node of group.querySelectorAll("[data-pin]")) {
      const [x, y] = node.dataset.pin.split(" ").map(Number);
      node.setAttribute("d", pinPath(x, y));
    }
  }

  function show(key) {
    const pack = packsByKey.get(key);
    if (!pack || typeof setView !== "function" || !view) return;  // the page's view (map_page.html)
    setView({...view, x: sx(pack) - view.w / 2, y: sy(pack) - view.h / 2});
    const ring = el("circle", {class: "q-flash", cx: sx(pack), cy: sy(pack), r: 14 * yardsPerPixel}, group);
    setTimeout(() => ring.remove(), 1500);
  }

  function packName(key) {
    const pack = packsByKey.get(key);
    if (!pack) return `pack ${key}`;
    const mobs = [...new Set(pack.mobs.map(m => m.name || `#${m.entry}`))].join(", ");
    return pack.bosses.length ? pack.bosses.join(", ") : mobs;
  }

  function objectiveText(o) {
    if (o.kind === "visit") return `visit ${o.name}`;
    const what = o.kind === "kill" ? `kill ${o.needed} ${o.name}` : `loot ${o.needed} ${o.name}`;
    return `${what}: ${o.on_route} on the route`;
  }

  function drawPanel(quests) {
    panel.replaceChildren();
    const unmet = quests.filter(q => !q.met);
    const title = document.createElement("h2");
    title.textContent = unmet.length ? `Quest objectives the route does not meet (${unmet.length} of ${quests.length} quests)`
                                     : `The route meets all ${quests.length} quests`;
    panel.appendChild(title);
    for (const q of unmet) {
      const head = document.createElement("div");
      head.textContent = `${q.quest} ${q.name}`;
      panel.appendChild(head);
      const list = document.createElement("ul");
      for (const o of q.objectives.filter(o => !o.met)) {
        const item = document.createElement("li");
        item.textContent = objectiveText(o);
        if (o.packs.length) {
          // The packs with the most quest mobs first, at most SHOWN of them.
          const SHOWN = 8;
          const best = [...o.packs].sort((a, b) => b.count - a.count);
          item.append(document.createElement("br"), `${o.packs.length} pack${o.packs.length > 1 ? "s" : ""} could meet it: `);
          best.slice(0, SHOWN).forEach((p, i) => {
            const link = document.createElement("span");
            link.className = "q-pack";
            link.textContent = `${p.count} in ${packName(p.key)}`;
            link.title = `pack key ${p.key}: click to show it`;
            link.addEventListener("click", () => show(p.key));
            item.append(i ? "; " : "", link);
          });
          if (best.length > SHOWN) item.append(`; and ${best.length - SHOWN} more`);
        } else if (o.kind !== "visit") {
          item.append(" ", Object.assign(document.createElement("span"), {className: "muted", textContent: "(no other pack)"}));
        }
        list.appendChild(item);
      }
      panel.appendChild(list);
    }
    panel.style.display = shown && quests.length ? "" : "none";
    panel.style.top = `${document.querySelector("header").getBoundingClientRect().bottom + 8}px`;
  }

  function draw(data) {
    const quests = data.quests || [];
    packsByKey = new Map(data.packs.map(p => [p.key, p]));
    group = el("g", {id: "quests"}, document.getElementById("overlay"));
    const metQuests = new Set(quests.filter(q => q.met).map(q => q.quest));

    // One badge per pack; its hover text lists every quest line of the pack.
    const byPack = new Map();
    for (const b of data.quest_badges || []) byPack.set(b.pack, [...(byPack.get(b.pack) || []), b]);
    for (const [key, badges] of byPack) {
      const pack = packsByKey.get(key);
      if (!pack) continue;
      const at = `${sx(pack)} ${sy(pack)}`;
      const met = badges.every(b => metQuests.has(b.quest));
      const dot = el("circle", {class: "q-badge" + (met ? " met" : ""), "data-badge": at, ...regionMark(badges[0])}, group);
      el("title", {}, dot).textContent = badges.map(b => b.text).join("\n");
      el("text", {class: "q-badge-text", "data-badge": at, ...regionMark(badges[0])}, group).textContent = "!";
    }
    for (const q of quests) for (const o of q.objectives) {
      if (o.kind !== "visit") continue;
      const pin = el("path", {class: "q-pin" + (o.met ? " met" : ""), "data-pin": `${sx(o)} ${sy(o)}`, ...regionMark(o)}, group);
      el("title", {}, pin).textContent = `${q.quest} ${q.name}: visit ${o.name}` +
        (o.met ? " (on the route)" : ` (the route does not come within ${o.range} yd)`);
    }
    group.style.display = shown ? "" : "none";
    place();
    drawPanel(quests);
  }

  controls.addEventListener("change", e => {
    shown = e.target.checked;
    if (group) group.style.display = shown ? "" : "none";
    panel.style.display = shown && panel.childElementCount ? "" : "none";
  });
  document.addEventListener("mapdata", e => draw(e.detail));
  document.addEventListener("mapview", e => { yardsPerPixel = e.detail; place(); });
})();
