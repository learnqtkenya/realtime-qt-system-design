import QtQml

QtObject {
    id: probe

    property string url
    property var results: ({})
    property bool reported: false
    property int updates: 0

    function attempt(what, f) {
        try {
            results[what] = f();
        } catch (e) {
            results[what] = "blocked (" + String(e).split("\n")[0] + ")";
        }
    }

    function request(what, target) {
        const xhr = new XMLHttpRequest();
        xhr.onreadystatechange = function () {
            if (xhr.readyState === XMLHttpRequest.DONE)
                results[what] = xhr.status === 200 ? "read " + xhr.responseText.length + " bytes" : "blocked (status " + xhr.status + ")";
        };
        try {
            xhr.open("GET", target);
            xhr.send();
            results[what] = "pending";
        } catch (e) {
            results[what] = "blocked (" + String(e).split("\n")[0] + ")";
        }
    }

    function update(input, draw) {
        if (Object.keys(results).length === 0) {
            attempt("viewer singletons", () => "Lab is " + Qt.createQmlObject("import Viewer; import QtQml; QtObject { property var l: Lab }", probe).l);
            attempt("viewer modules", () => "AppOptions is " + Qt.createQmlObject("import QtQMLPro.Common; import QtQml; QtObject { property var o: AppOptions }", probe).o);
            request("network", url);
            request("file", "file:///etc/hostname");
            return;
        }
        if (!reported && ++updates > 20) {
            for (const k in results)
                console.log("curious script:", k + ":", results[k]);
            reported = true;
        }
        draw.color("#F87171");
        let i = 0;
        for (const k in results)
            draw.text(-60, -20 - 4 * i++, k + ": " + results[k]);
    }
}
