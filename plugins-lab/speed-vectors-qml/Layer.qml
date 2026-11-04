import QtQml

QtObject {
    function update(input, draw) {
        const tracks = input.tracks;
        for (let i = 0; i < tracks.length; ++i) {
            const t = tracks[i];
            draw.color(t.speed < 5 ? "#34D399" : t.speed < 12 ? "#FBBF24" : "#F87171");
            draw.line(t.x, t.y, t.x + 2 * t.speed * Math.cos(t.heading), t.y + 2 * t.speed * Math.sin(t.heading));
        }
    }
}
