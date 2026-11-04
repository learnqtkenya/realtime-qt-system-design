let spacing = 10;
let rings = 5;

export function init(properties) {
    spacing = properties.spacing ?? spacing;
    rings = properties.rings ?? rings;
}

// API 1.1: input.selectedId.
export function update(input, draw) {
    const tracks = input.tracks;
    const selected = tracks.find(t => t.id === input.selectedId);
    const cx = selected ? selected.x : 0;
    const cy = selected ? selected.y : 0;
    draw.color("#A038BDF8");
    for (let k = 1; k <= rings; ++k)
        draw.circle(cx, cy, k * spacing);
    draw.color("#7DD3FC");
    for (let k = 1; k <= rings; ++k)
        draw.text(cx + k * spacing * 0.7071 + 1, cy + k * spacing * 0.7071, (k * spacing) + " m");
    if (!selected)
        return;
    const near = tracks.filter(t => t !== selected)
        .map(t => ({ t: t, d: Math.hypot(t.x - cx, t.y - cy) }))
        .sort((a, b) => a.d - b.d)
        .slice(0, 3);
    draw.color("#FBBF24");
    for (const n of near) {
        draw.line(cx, cy, n.t.x, n.t.y);
        draw.text((cx + n.t.x) / 2, (cy + n.t.y) / 2, n.d.toFixed(1) + " m");
    }
}
