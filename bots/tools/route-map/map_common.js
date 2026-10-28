// The shared helpers of the dungeon map page. map_page.html loads this
// script first; every other script uses these globals and defines no copy.
//
// Axes: WoW positions to SVG, north (+x) up, west (+y) left: SVG x = -y,
// SVG y = -x. floor_image.py draws the floor image on the same axes.
const SVG = "http://www.w3.org/2000/svg";
const sx = p => -p.y;
const sy = p => -p.x;

// A new SVG node with these attributes, added to parent.
function el(name, attrs, parent) {
  const node = document.createElementNS(SVG, name);
  for (const [k, v] of Object.entries(attrs)) node.setAttribute(k, v);
  parent.appendChild(node);
  return node;
}

// The SVG point (in yards) under a mouse event, and the yards per pixel k.
// It reads the page globals `map` (the SVG) and `view` (its viewBox).
function toMap(event) {
  const r = map.getBoundingClientRect(), k = Math.max(view.w / r.width, view.h / r.height);
  const ox = view.x + (view.w - r.width * k) / 2, oy = view.y + (view.h - r.height * k) / 2;
  return {x: ox + (event.clientX - r.left) * k, y: oy + (event.clientY - r.top) * k, k};
}

// Marks keep their size on screen at every zoom. The sizes are in pixels:
// - data-r: the radius of a circle;
// - data-size: the font size of a text, data-dx and data-dy its offset
//   (7 and -5 when not given), data-halo the width of its stroke halo;
// - data-cross="x y [half]": an X path at (x, y), half pixels each way (4).
function sizeMarks(root, yardsPerPixel) {
  const k = yardsPerPixel;
  for (const node of root.querySelectorAll("[data-r]")) node.setAttribute("r", node.dataset.r * k);
  for (const node of root.querySelectorAll("[data-size]")) {
    node.setAttribute("font-size", node.dataset.size * k);
    node.setAttribute("dx", (node.dataset.dx ?? 7) * k);
    node.setAttribute("dy", (node.dataset.dy ?? -5) * k);
    if (node.dataset.halo) node.style.strokeWidth = node.dataset.halo * k;
  }
  for (const node of root.querySelectorAll("[data-cross]")) {
    const [x, y, half = 4] = node.dataset.cross.split(" ").map(Number), d = half * k;
    node.setAttribute("d", `M${x - d} ${y - d}L${x + d} ${y + d}M${x - d} ${y + d}L${x + d} ${y - d}`);
  }
}
