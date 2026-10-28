// The walked path layer of the dungeon map (jvbastel/WOWAI#152): where each
// group member really walked in a run, from a run trace file. The player
// gives the trace path (or ?trace=<path> in the address), and turns the
// layer on and off. map_page.html loads this script after map_common.js
// (SVG, el, sx, sy); it adds its own controls to the header and its own SVG
// group over the map.
(() => {
  const COLOURS = ["#7CFC8A", "#62D2FF", "#E58CFF", "#FFE36E", "#FF9F6B", "#B0B8FF"];
  const map = document.getElementById("map");
  const header = document.querySelector("header");
  const status = document.getElementById("status");
  const route = new URLSearchParams(location.search).get("route");

  const style = document.createElement("style");
  style.textContent = `
    .walked { fill: none; stroke-width: 1.5; vector-effect: non-scaling-stroke; opacity: 0.9;
              stroke-linejoin: round; stroke-dasharray: 6 2; }
    #walked-trace { width: 22em; }
    #walked-show { width: auto; }
    #walked-legend span { margin-right: 8px; }`;
  document.head.appendChild(style);

  const controls = document.createElement("span");
  controls.id = "walked-controls";
  controls.innerHTML = `
    <label>Run trace <input id="walked-trace" placeholder="path of a .trace.txt"></label>
    <button id="walked-load">Load walk</button>
    <label><input type="checkbox" id="walked-show" checked disabled> Walked path</label>
    <span id="walked-legend"></span>`;
  header.insertBefore(controls, status);

  const layer = el("g", {id: "walked"}, map);

  const input = document.getElementById("walked-trace");
  const show = document.getElementById("walked-show");
  const legend = document.getElementById("walked-legend");
  input.value = new URLSearchParams(location.search).get("trace") || "";

  show.addEventListener("change", () => { layer.style.display = show.checked ? "" : "none"; });

  // The map draws its first view (its viewBox) once its own load is done;
  // a walk given in the address waits for it.
  function mapReady() {
    if (map.hasAttribute("viewBox")) return Promise.resolve();
    return new Promise(resolve => {
      const watch = new MutationObserver(() => {
        if (map.hasAttribute("viewBox")) { watch.disconnect(); resolve(); }
      });
      watch.observe(map, {attributes: true, attributeFilter: ["viewBox"]});
    });
  }

  async function loadWalk() {
    const trace = input.value.trim();
    if (!route || !trace) return;
    await mapReady();
    const response = await fetch(`/api/maps/${encodeURIComponent(route)}/walked?trace=${encodeURIComponent(trace)}`);
    const data = await response.json();
    if (!response.ok) {
      status.className = "error";
      status.textContent = data.detail || response.statusText;
      return;
    }
    layer.replaceChildren();
    legend.replaceChildren();
    data.members.forEach((member, i) => {
      const colour = COLOURS[i % COLOURS.length];
      const line = drawLine(member.points, {class: "walked", stroke: colour}, layer);
      el("title", {}, line).textContent = `${member.name}: ${member.points.length} positions, ` +
        `${member.points[0].t} s to ${member.points[member.points.length - 1].t} s`;
      const key = document.createElement("span");
      key.style.color = colour;
      key.textContent = member.name;
      legend.appendChild(key);
    });
    show.disabled = false;
    show.checked = true;
    layer.style.display = "";
    status.className = "";
    status.textContent = `Walked path of ${data.trace}: ${data.members.length} members`;
  }

  document.getElementById("walked-load").addEventListener("click", loadWalk);
  input.addEventListener("keydown", e => { if (e.key === "Enter") loadWalk(); });
  if (input.value) loadWalk();
})();
