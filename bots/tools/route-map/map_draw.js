// The route drawing of the dungeon map (tickets jvbastel/WOWAI#157, #158). It
// needs the globals of map_page.html (map, route, view) and of map_common.js
// (SVG, el, sx, sy, toMap).
// The page sizes the points (data-r) at each zoom and each redraw.
//
// Draw mode: a click adds a point to the active line, snapped to the floor.
// Where floors stack, a chooser picks the floor of the point. Drag a point to
// move it, right-click it to delete it. Each piece follows the floor path that
// the server finds; a piece with a warning is drawn in another colour. Save
// writes the trace file of the open route.
//
// The drawing is a list of lines: lines[0] is the main line, each later line
// a branch (a later walk of the trace). A point is {x, y, z, floors, joined,
// mark, note}. map_draft.js loads draft lines, places marks and starts
// branches through window.routeDraw.
(() => {
  const style = document.createElement("style");
  style.textContent = `
    .draw-piece { fill: none; stroke: #4fe08a; stroke-width: 2.5; vector-effect: non-scaling-stroke; }
    .draw-piece.branch { stroke: #46c8e8; }
    .draw-piece.warn { stroke: #ff5bd0; stroke-dasharray: 6 3; }
    .draw-point { fill: #4fe08a; stroke: #000; stroke-width: 1; vector-effect: non-scaling-stroke; cursor: move; }
    .draw-point.branch { fill: #46c8e8; }
    .draw-point.mark { fill: #ffe14d; stroke-width: 2; }
    .draw-point.selected { fill: #fff; }
    #draw-chooser { position: fixed; display: none; background: #26292c; border: 1px solid #555; padding: 4px;
                    z-index: 10; }
    #draw-chooser button { display: block; width: 100%; margin: 2px 0; }
    #draw-warnings { color: #ff9ae4; }
    button.on { background: #4fe08a; color: #000; }`;
  document.head.appendChild(style);

  const layer = el("g", {id: "draw-layer"}, map);
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
  // Each line: {points, pieces}. pieces[i] joins points[i] and points[i + 1]:
  // {points, warnings, length}, or null after a gap (points[i + 1].joined false).
  let lines = [{points: [], pieces: []}];
  let active = 0;               // the line that a click adds to
  let selected = null;          // {line, index}
  let fromTrace = false;        // the drawing started from the trace file
  const api = path => `/api/maps/${encodeURIComponent(route)}/${path}`;
  const isSelected = (l, i) => selected && selected.line === l && selected.index === i;

  async function call(path, body) {
    const response = await fetch(api(path), body === undefined ? {} :
      {method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(body)});
    const data = await response.json();
    if (!response.ok) throw new Error(data.detail || response.statusText);
    return data;
  }

  // The faded or hidden regions (region_pick.js): a snap leaves their ground
  // out, a path may still run through them (#185).
  const through = () => (window.regionPick ? window.regionPick.faded() : []);

  async function snap(x, y, z) {
    const params = new URLSearchParams({x, y});
    if (z !== undefined && z !== null) params.set("z", z);
    for (const name of through()) params.append("through", name);
    const found = await call(`snap?${params}`);
    return {...found.point, floors: found.floors, joined: true, mark: false, note: ""};
  }

  async function join(line, i) {
    const a = line.points[i], b = line.points[i + 1];
    line.pieces[i] = b.joined === false ? null :
      await call("path", {start: {x: a.x, y: a.y, z: a.z}, end: {x: b.x, y: b.y, z: b.z}, through: through()});
  }

  // Recompute the pieces next to point i of line l, then redraw.
  async function rejoin(l, i) {
    const line = lines[l];
    line.pieces.length = Math.max(0, line.points.length - 1);
    const jobs = [];
    if (i > 0) jobs.push(join(line, i - 1));
    if (i < line.points.length - 1) jobs.push(join(line, i));
    try { await Promise.all(jobs); } catch (error) { info.textContent = error.message; }
    redraw();
  }

  // A loaded line: its points are already on the floor and close together,
  // so each piece is the straight step. Save joins them along the floor.
  function straight(a, b) {
    return b.joined === false ? null :
      {points: [a, b], warnings: [], length: Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z)};
  }

  function redraw() {
    layer.replaceChildren();
    lines.forEach((line, l) => line.pieces.forEach((piece, i) => {
      if (!piece) return;
      const poly = el("polyline", {class: "draw-piece" + (l ? " branch" : "") + (piece.warnings.length ? " warn" : ""),
                                   points: piece.points.map(p => `${sx(p)},${sy(p)}`).join(" "),
                                   ...pieceMark(line.points[i], line.points[i + 1])}, layer);
      el("title", {}, poly).textContent =
        `${l ? `branch ${l}, ` : ""}piece ${i + 1}: ${Math.round(piece.length * 10) / 10} yards` +
        piece.warnings.map(w => `\n${w.text}`).join("");
    }));
    lines.forEach((line, l) => line.points.forEach((point, i) => {
      const dot = el("circle", {class: "draw-point" + (l ? " branch" : "") + (point.mark ? " mark" : "") +
                                  (isSelected(l, i) ? " selected" : ""), cx: sx(point), cy: sy(point),
                                "data-r": point.mark ? 6 : 4, "data-line": l, "data-index": i,
                                ...regionMark(point)}, layer);
      const title = el("title", {}, dot);
      const floors = point.floors || [];
      title.textContent = `${l ? `branch ${l}, ` : ""}point ${i + 1}: ` +
        `${point.x.toFixed(1)} ${point.y.toFixed(1)} ${point.z.toFixed(1)}` +
        (point.mark ? `\nmark${point.note ? ": " + point.note : ""}` : "") +
        (floors.length > 1 ? ` (floors ${floors.join(", ")})` : "") + "\nDrag to move, right-click to delete.";
      dot.addEventListener("mousedown", startMove);
      dot.addEventListener("contextmenu", event => { event.preventDefault(); remove(l, i); });
    }));
    const count = lines.reduce((sum, line) => sum + line.points.length, 0);
    const length = lines.reduce((sum, line) => sum + line.pieces.reduce((s, p) => s + (p ? p.length : 0), 0), 0);
    const branches = lines.length - 1;
    info.textContent = count ? `${count} points, ${Math.round(length)} yards` +
      (branches ? `, ${branches} branch${branches > 1 ? "es" : ""}` : "") +
      (active ? ` (drawing branch ${active})` : "") : "";
    const warned = lines.flatMap((line, l) => line.pieces.flatMap((p, i) =>
      p ? p.warnings.map(w => `${l ? `branch ${l} ` : ""}piece ${i + 1}: ${w.text}`) : []));
    warningsText.textContent = warned.join("; ");
  }

  // A stacked floor: the player picks the floor of point i of line l.
  function choose(l, i, event) {
    const point = lines[l].points[i];
    if (!point.floors || point.floors.length < 2) { chooser.style.display = "none"; return; }
    chooser.replaceChildren();
    for (const z of [...point.floors].reverse()) {
      const button = document.createElement("button");
      button.textContent = `floor at z ${z}` + (z === point.z ? " (this one)" : "");
      button.addEventListener("click", async () => {
        chooser.style.display = "none";
        lines[l].points[i] = {...point, z};
        try {  // the regions of the new floor (#184)
          lines[l].points[i].regions = (await snap(point.x, point.y, z)).regions;
        } catch (error) { info.textContent = error.message; }
        await rejoin(l, i);
      });
      chooser.appendChild(button);
    }
    chooser.style.left = `${event.clientX + 12}px`;
    chooser.style.top = `${event.clientY + 12}px`;
    chooser.style.display = "block";
  }

  async function add(event) {
    const at = toMap(event);
    const line = lines[active];
    const last = line.points[line.points.length - 1];
    try {
      line.points.push(await snap(-at.y, -at.x, last ? last.z : undefined));
    } catch (error) { info.textContent = error.message; return; }
    selected = {line: active, index: line.points.length - 1};
    await rejoin(active, selected.index);
    choose(active, selected.index, event);
  }

  async function remove(l, i) {
    const line = lines[l];
    const [gone] = line.points.splice(i, 1);
    if (gone.joined === false && i < line.points.length) line.points[i].joined = false;  // keep the gap
    selected = null;
    chooser.style.display = "none";
    if (l > 0 && line.points.length === 0) {  // an empty branch goes
      lines.splice(l, 1);
      active = 0;
      redraw();
      return;
    }
    // The pieces on both sides of the point become one piece.
    if (i === 0) line.pieces.splice(0, 1);
    else if (i === line.points.length) line.pieces.splice(i - 1, 1);
    else line.pieces.splice(i - 1, 2, null);
    if (i > 0 && i < line.points.length) {
      try { await join(line, i - 1); } catch (error) { info.textContent = error.message; }
    }
    redraw();
  }

  let moving = null;
  function startMove(event) {
    if (event.button !== 0) return;
    event.stopPropagation();  // no pan
    down = null;
    moving = {line: Number(event.target.dataset.line), index: Number(event.target.dataset.index), moved: false};
    selected = {line: moving.line, index: moving.index};
  }
  window.addEventListener("mousemove", event => {
    if (!moving) return;
    const at = toMap(event);
    moving.moved = true;
    const dot = layer.querySelector(`circle[data-line="${moving.line}"][data-index="${moving.index}"]`);
    dot.setAttribute("cx", at.x);
    dot.setAttribute("cy", at.y);
  });
  window.addEventListener("mouseup", async event => {
    if (!moving) return;
    const {line, index, moved} = moving;
    moving = null;
    if (!moved) { redraw(); return; }
    const at = toMap(event);
    const old = lines[line].points[index];
    try {
      const point = await snap(-at.y, -at.x, old.z);
      lines[line].points[index] = {...point, joined: old.joined, mark: old.mark, note: old.note};
    } catch (error) { info.textContent = error.message; }
    await rejoin(line, index);
    choose(line, index, event);
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
    if ((event.key === "Delete" || event.key === "Backspace") && selected && event.target === document.body)
      remove(selected.line, selected.index);
    if (event.key === "Escape") chooser.style.display = "none";
  });

  const modeButton = document.getElementById("draw-mode");
  function setDrawing(on) {
    drawing = on;
    modeButton.classList.toggle("on", drawing);
    map.style.cursor = drawing ? "crosshair" : "";
  }
  modeButton.addEventListener("click", () => setDrawing(!drawing));
  document.getElementById("draw-clear").addEventListener("click", () => {
    if (lines.some(l => l.points.length) && !confirm("Clear the drawn line and its branches?")) return;
    lines = [{points: [], pieces: []}]; active = 0; selected = null; fromTrace = false; redraw();
  });
  const sent = p => ({x: p.x, y: p.y, z: p.z, joined: p.joined !== false, mark: !!p.mark, note: p.note || ""});
  document.getElementById("draw-save").addEventListener("click", async () => {
    if (lines[0].points.length < 2) { info.textContent = "Draw at least two points before you save."; return; }
    if (!fromTrace && !confirm(`Replace the trace file of ${route} with the drawn line? ` +
        "The points and marks of the old trace are lost. (Load the trace first to edit it.)")) return;
    try {
      const saved = await call("trace", {points: lines[0].points.map(sent),
        branches: lines.slice(1).filter(line => line.points.length).map(line => line.points.map(sent))});
      info.textContent = `Saved ${saved.points} trace points to ${saved.file}`;
    } catch (error) { info.textContent = `Not saved: ${error.message}`; }
  });

  // The hooks of map_draft.js.
  window.routeDraw = {
    // Replace the drawing with draft lines ({x, y, z, joined, mark, note}
    // points; lines[0] the main line). fromTraceFile: the lines are the
    // trace file's, so Save asks no confirm.
    setLines(draft, fromTraceFile) {
      lines = (draft.length ? draft : [[]]).map(points => {
        const line = {points: points.map(p => ({floors: [], ...p})), pieces: []};
        line.pieces = line.points.slice(1).map((p, i) => straight(line.points[i], p));
        return line;
      });
      active = 0; selected = null; fromTrace = !!fromTraceFile;
      redraw();
    },
    hasPoints: () => lines.some(line => line.points.length > 0),
    selected: () => selected && {...selected, point: lines[selected.line].points[selected.index]},
    // Mark (or unmark) the selected point.
    setMark(mark, note) {
      if (!selected) return false;
      Object.assign(lines[selected.line].points[selected.index], {mark, note: mark ? note : ""});
      redraw();
      return true;
    },
    // A new branch from the selected point of the main line: the next clicks
    // draw it. The build walks it out and back from the main line.
    startBranch() {
      if (!selected || selected.line !== 0) return false;
      const from = lines[0].points[selected.index];
      lines.push({points: [{...from, joined: false, mark: false, note: ""}], pieces: []});
      active = lines.length - 1;
      selected = {line: active, index: 0};
      setDrawing(true);
      redraw();
      return true;
    },
    endBranch() { active = 0; redraw(); },
  };
})();
