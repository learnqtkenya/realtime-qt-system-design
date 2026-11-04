import QtQml

QtObject {
    property int after: 60
    property int count: 0

    function update(input, draw) {
        if (++count > after) {
            let i = 0;
            while (true)
                ++i;
        }
        draw.text(0, 0, "update " + count);
    }
}
