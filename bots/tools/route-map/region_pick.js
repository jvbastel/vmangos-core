// Pick a region on the dungeon map (ticket jvbastel/WOWAI#184, spec #179):
// the player picks one region, or "None", in the regions panel, and every
// other region fades, or hides with the switch. The picked region and the
// ground in no region show as normal. "None" shows the whole map. The pick
// is not saved: the page opens on "None".
//
// The server owns the rules: the map data gives one ground image per region
// (an <image data-region> in #floor), and each position carries its regions,
// which the drawing scripts put on their marks (regionMark and pieceMark of
// map_common.js: data-regions, data-ends). This part only sets the opacity
// of the images and marks, with no new request, and keeps the view.
// A mark in two regions takes the more visible state; a line piece fades
// only when both of its ends are in faded regions.
//
// Other scripts read the pick through window.regionPick (picked, hide,
// faded: the names of the regions not shown as normal) or the "regionpick"
// event, sent at each change.
(() => {
  const style = document.createElement("style");
  style.textContent = `
    #rp-box { margin: 0 0 6px; }
    #rp-list label { display: block; cursor: pointer; }
    #rp-box .rp-switch { color: var(--muted); display: block; margin-top: 4px; }
    body:not(:has(#rg-mode.rg-on)) #rg-list { display: none; }`;
  document.head.appendChild(style);

  const OPACITY = {normal: "", faded: "0.2", hidden: "0"};
  const RANK = {hidden: 0, faded: 1, normal: 2};
  let names = [];      // the regions of the map data, as its ground images
  let picked = null;   // the picked region, or null for "None"
  let hide = false;    // hide the other regions instead of fading them

  const box = document.createElement("div");
  box.id = "rp-box";
  box.innerHTML = `<div id="rp-list"></div>
    <label class="rp-switch" title="Hide the regions you did not pick, instead of fading them">
      <input type="checkbox" id="rp-hide"> Hide the other regions</label>`;
  const panel = document.getElementById("rg-panel");
  panel.querySelector("h2").after(box);
  const list = document.getElementById("rp-list");
  const hideBox = document.getElementById("rp-hide");

  const stateOf = region => picked === null || region === picked ? "normal" : hide ? "hidden" : "faded";
  const best = states => states.reduce((a, b) => RANK[a] >= RANK[b] ? a : b, "hidden");
  // A position in no region shows as normal; in several, the most visible state wins.
  const markState = regions => regions.length ? best(regions.map(stateOf)) : "normal";

  function apply() {
    for (const node of map.querySelectorAll("[data-region]")) node.style.opacity = OPACITY[stateOf(node.dataset.region)];
    for (const node of map.querySelectorAll("[data-regions]")) {
      node.style.opacity = OPACITY[markState(JSON.parse(node.dataset.regions))];
    }
    for (const node of map.querySelectorAll("[data-ends]")) {
      node.style.opacity = OPACITY[best(JSON.parse(node.dataset.ends).map(markState))];
    }
  }

  const faded = () => names.filter(name => stateOf(name) !== "normal");

  function changed() {
    apply();
    document.dispatchEvent(new CustomEvent("regionpick", {detail: {picked, hide, faded: faded()}}));
  }

  function drawList() {
    list.replaceChildren(...[null, ...names].map(name => {
      const label = document.createElement("label");
      const input = Object.assign(document.createElement("input"),
                                  {type: "radio", name: "rp-pick", checked: name === picked});
      input.addEventListener("change", () => { picked = name; changed(); });
      label.append(input, " ", name === null ? "None" : name);
      return label;
    }));
    box.style.display = names.length ? "" : "none";
  }

  hideBox.addEventListener("change", () => { hide = hideBox.checked; changed(); });
  document.addEventListener("mapdata", e => {
    const images = e.detail.floor.images || [];
    names = images.map(i => i.region).filter(name => name !== null);
    if (!names.includes(picked)) picked = null;
    drawList();
    if (names.length) panel.style.display = "block";
    changed();
  });
  // A save of the regions changes the ground images and the marks: load the
  // map data again (the page's load), keeping the view.
  document.addEventListener("regionssaved", () => load(true));
  // Marks drawn later (a draft, a walk, a redraw) get the state at once.
  new MutationObserver(apply).observe(map, {childList: true, subtree: true});

  window.regionPick = {picked: () => picked, hide: () => hide, faded};
})();
