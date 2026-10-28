// The route drawing of the dungeon map (ticket jvbastel/WOWAI#157). It needs
// the globals of map_page.html: map, route, view, toMap, SVG, sx, sy. The
// page sends the "mapview" event with the yards per pixel at each zoom.
//
// Draw mode: a click adds a point, snapped to the floor. Where floors stack,
// a chooser picks the floor of the point. Drag a point to move it, right-click
// it to delete it. Each piece follows the floor path that the server finds;
// a piece with a warning is drawn in another colour. Save writes the trace
// file of the open route.
(() => {
  const style = document.createElement("style");
  style.textContent = `
    .draw-piece { fill: none; stroke: #4fe08a; stroke-width: 2.5; vector-effect: non-scaling-stroke; }
    .draw-piece.warn { stroke: #ff5bd0; stroke-dasharray: 6 3; }
    .draw-point { fill: #4fe08a; stroke: #000; stroke-width: 1; vector-effect: non-scaling-stroke; cursor: move; }
    .draw-point.selected { fill: #fff; }
    #draw-chooser { position: fixed; display: none; background: #26292c; border: 1px solid #555; padding: 4px;
                    z-index: 10; }
    #draw-chooser button { display: block; width: 100%; margin: 2px 0; }
    #draw-warnings { color: #ff9ae4; }
    button.on { background: #4fe08a; color: #000; }`;
  document.head.appendChild(style);

  const layer = document.createElementNS(SVG, "g");
  layer.id = "draw-layer";
  map.appendChild(layer);
  const tools = document.createElement("span");
  tools.innerHTML = `<button id="draw-mode" title="Click the map to add points">Draw</button>
    <button id="draw-save">Save trace</button>
    <button id="draw-clear">Clear line</button>
    <span id="draw-info"></span> <span id="draw-warnings"></span>`;
  document.querySelector("header").appendChild(tools);
  const chooser = document.createElement("div");
  chooser.id = "draw-chooser";
  document.body.appendChild(chooser);
  const info = document.getElementById("draw-info");
  const warningsText = document.getElementById("draw-warnings");

  let drawing = false;
  let points = [];   // the clicked points {x, y, z, floors}
  let pieces = [];   // pieces[i] joins points[i] and points[i + 1]: {points, warnings, length}
  let selected = -1;
  let yardsPerPixel = 1;
  const api = path => `/api/maps/${encodeURIComponent(route)}/${path}`;

  async function call(path, body) {
    const response = await fetch(api(path), body === undefined ? {} :
      {method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(body)});
    const data = await response.json();
    if (!response.ok) throw new Error(data.detail || response.statusText);
    return data;
  }

  async function snap(x, y, z) {
    const params = new URLSearchParams({x, y});
    if (z !== undefined && z !== null) params.set("z", z);
    const found = await call(`snap?${params}`);
    return {...found.point, floors: found.floors};
  }

  async function join(i) {
    const a = points[i], b = points[i + 1];
    pieces[i] = await call("path", {start: {x: a.x, y: a.y, z: a.z}, end: {x: b.x, y: b.y, z: b.z}});
  }

  // Recompute the pieces next to point i, then redraw.
  async function rejoin(i) {
    pieces.length = Math.max(0, points.length - 1);
    const jobs = [];
    if (i > 0) jobs.push(join(i - 1));
    if (i < points.length - 1) jobs.push(join(i));
    try { await Promise.all(jobs); } catch (error) { info.textContent = error.message; }
    redraw();
  }

  function redraw() {
    layer.replaceChildren();
    pieces.forEach((piece, i) => {
      if (!piece) return;
      const line = document.createElementNS(SVG, "polyline");
      line.setAttribute("class", "draw-piece" + (piece.warnings.length ? " warn" : ""));
      line.setAttribute("points", piece.points.map(p => `${sx(p)},${sy(p)}`).join(" "));
      const title = document.createElementNS(SVG, "title");
      title.textContent = `piece ${i + 1}: ${piece.length} yards` +
        piece.warnings.map(w => `\n${w.text}`).join("");
      line.appendChild(title);
      layer.appendChild(line);
    });
    points.forEach((point, i) => {
      const dot = document.createElementNS(SVG, "circle");
      dot.setAttribute("class", "draw-point" + (i === selected ? " selected" : ""));
      dot.setAttribute("cx", sx(point));
      dot.setAttribute("cy", sy(point));
      dot.setAttribute("r", 4 * yardsPerPixel);
      dot.dataset.index = i;
      const title = document.createElementNS(SVG, "title");
      title.textContent = `point ${i + 1}: ${point.x.toFixed(1)} ${point.y.toFixed(1)} ${point.z.toFixed(1)}` +
        (point.floors.length > 1 ? ` (floors ${point.floors.join(", ")})` : "") + "\nDrag to move, right-click to delete.";
      dot.appendChild(title);
      dot.addEventListener("mousedown", startMove);
      dot.addEventListener("contextmenu", event => { event.preventDefault(); remove(i); });
      layer.appendChild(dot);
    });
    const length = pieces.reduce((sum, p) => sum + (p ? p.length : 0), 0);
    info.textContent = points.length ? `${points.length} points, ${Math.round(length)} yards` : "";
    const warned = pieces.flatMap((p, i) => p ? p.warnings.map(w => `piece ${i + 1}: ${w.text}`) : []);
    warningsText.textContent = warned.join("; ");
  }

  // A stacked floor: the player picks the floor of point i.
  function choose(i, event) {
    const point = points[i];
    if (point.floors.length < 2) { chooser.style.display = "none"; return; }
    chooser.replaceChildren();
    for (const z of [...point.floors].reverse()) {
      const button = document.createElement("button");
      button.textContent = `floor at z ${z}` + (z === point.z ? " (this one)" : "");
      button.addEventListener("click", async () => {
        chooser.style.display = "none";
        points[i] = {...point, z};
        await rejoin(i);
      });
      chooser.appendChild(button);
    }
    chooser.style.left = `${event.clientX + 12}px`;
    chooser.style.top = `${event.clientY + 12}px`;
    chooser.style.display = "block";
  }

  async function add(event) {
    const at = toMap(event);
    const last = points[points.length - 1];
    try {
      points.push(await snap(-at.y, -at.x, last ? last.z : undefined));
    } catch (error) { info.textContent = error.message; return; }
    selected = points.length - 1;
    await rejoin(selected);
    choose(selected, event);
  }

  async function remove(i) {
    points.splice(i, 1);
    pieces = [];
    selected = -1;
    chooser.style.display = "none";
    try { await Promise.all(points.slice(1).map((_, k) => join(k))); } catch (error) { info.textContent = error.message; }
    redraw();
  }

  let moving = null;
  function startMove(event) {
    if (event.button !== 0) return;
    event.stopPropagation();  // no pan
    down = null;
    moving = {index: Number(event.target.dataset.index), moved: false};
    selected = moving.index;
  }
  window.addEventListener("mousemove", event => {
    if (!moving) return;
    const at = toMap(event);
    moving.moved = true;
    const dot = layer.querySelector(`circle[data-index="${moving.index}"]`);
    dot.setAttribute("cx", at.x);
    dot.setAttribute("cy", at.y);
  });
  window.addEventListener("mouseup", async event => {
    if (!moving) return;
    const {index, moved} = moving;
    moving = null;
    if (!moved) { redraw(); return; }
    const at = toMap(event);
    try {
      points[index] = await snap(-at.y, -at.x, points[index].z);
    } catch (error) { info.textContent = error.message; }
    await rejoin(index);
    choose(index, event);
  });

  let down = null;
  map.addEventListener("mousedown", event => { down = {x: event.clientX, y: event.clientY}; });
  map.addEventListener("click", event => {
    if (!drawing || !down || !view || event.target.classList.contains("draw-point")) return;
    if (Math.hypot(event.clientX - down.x, event.clientY - down.y) > 4) return;  // a pan, not a click
    chooser.style.display = "none";
    add(event);
  });
  window.addEventListener("keydown", event => {
    if ((event.key === "Delete" || event.key === "Backspace") && selected >= 0 && event.target === document.body)
      remove(selected);
    if (event.key === "Escape") chooser.style.display = "none";
  });

  document.addEventListener("mapview", event => {
    yardsPerPixel = event.detail;
    for (const dot of layer.querySelectorAll(".draw-point")) dot.setAttribute("r", 4 * yardsPerPixel);
  });
  const modeButton = document.getElementById("draw-mode");
  modeButton.addEventListener("click", () => {
    drawing = !drawing;
    modeButton.classList.toggle("on", drawing);
    map.style.cursor = drawing ? "crosshair" : "";
  });
  document.getElementById("draw-clear").addEventListener("click", () => {
    if (points.length && !confirm("Clear the drawn line?")) return;
    points = []; pieces = []; selected = -1; redraw();
  });
  document.getElementById("draw-save").addEventListener("click", async () => {
    if (points.length < 2) { info.textContent = "Draw at least two points before you save."; return; }
    try {
      const saved = await call("trace", {points: points.map(p => ({x: p.x, y: p.y, z: p.z}))});
      info.textContent = `Saved ${saved.points} trace points to ${saved.file}`;
    } catch (error) { info.textContent = `Not saved: ${error.message}`; }
  });
})();
