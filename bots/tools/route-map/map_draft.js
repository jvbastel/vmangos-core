// Draft lines, quest visit marks and branches of the route drawing (ticket
// jvbastel/WOWAI#158). It needs map_draw.js (window.routeDraw) and the page
// global `route`.
//
// Load route line: the line of the generated route file, as the draft.
// Load trace: the line of the route's trace file, with its marks and walks.
// Mark: select a point (click it), then Mark makes it a quest visit mark.
// Branch: select a point of the main line, then Branch starts a branch there;
// the next clicks draw it, to a side room. The build walks it out and back.
(() => {
  const draw = window.routeDraw;
  const tools = document.createElement("span");
  tools.innerHTML = `<button id="draft-route" title="Start from the line of the generated route">Load route line</button>
    <button id="draft-trace" title="Edit the trace drawn or walked before, with its marks">Load trace</button>
    <button id="draft-mark" title="Make the selected point a quest visit mark">Mark</button>
    <button id="draft-branch" title="Start a branch from the selected point of the main line">Branch</button>
    <button id="draft-end" title="Go back to drawing the main line">End branch</button>`;
  document.querySelector("header").appendChild(tools);
  const info = document.getElementById("draw-info");

  async function load(source) {
    if (draw.hasPoints() && !confirm("Replace the drawn line with the draft line?")) return;
    try {
      const response = await fetch(`/api/maps/${encodeURIComponent(route)}/draft?source=${source}`);
      const data = await response.json();
      if (!response.ok) throw new Error(data.detail || response.statusText);
      draw.setLines(data.lines, source === "trace");
      const marks = data.lines.flat().filter(p => p.mark).length;
      info.textContent += ` (draft from the ${source === "trace" ? "trace" : "route"} file, ${marks} marks)`;
    } catch (error) { info.textContent = `No draft: ${error.message}`; }
  }

  document.getElementById("draft-route").addEventListener("click", () => load("route"));
  document.getElementById("draft-trace").addEventListener("click", () => load("trace"));
  document.getElementById("draft-mark").addEventListener("click", () => {
    const chosen = draw.selected();
    if (!chosen) { info.textContent = "Click a point of the line first, then Mark."; return; }
    if (chosen.point.mark) { draw.setMark(false, ""); return; }
    const note = prompt("Quest visit mark: a note, for example q5722 Maur Grimtotem", "q");
    if (note !== null) draw.setMark(true, note);
  });
  document.getElementById("draft-branch").addEventListener("click", () => {
    if (!draw.startBranch()) info.textContent = "Click a point of the main line first, then Branch.";
  });
  document.getElementById("draft-end").addEventListener("click", () => draw.endBranch());
})();
