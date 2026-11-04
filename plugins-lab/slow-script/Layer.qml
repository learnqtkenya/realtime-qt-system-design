import QtQml

QtObject {
    property real ms: 30

    function update(input, draw) {
        const end = Date.now() + ms;
        while (Date.now() < end) {}
        draw.color("#94A3B8");
        draw.text(-20, -20, "slow layer");
    }
}
