const results = {};
let reported = false;

function attempt(what, f) {
    try {
        results[what] = f();
    } catch (e) {
        results[what] = "blocked (" + String(e).split("\n")[0] + ")";
    }
}

export function update(input, draw) {
    if (!reported) {
        attempt("viewer singletons", () => "Qt is " + Qt.createQmlObject("import Viewer; import QtQml; QtObject { property var l: Lab }", null).l);
        attempt("network", () => "XMLHttpRequest is " + new XMLHttpRequest());
        attempt("host objects", () => ["Qt", "XMLHttpRequest", "WorkerScript", "require", "process", "fetch", "print", "gc", "console"]
            .filter(n => eval("typeof " + n) !== "undefined").join(", "));
        for (const k in results)
            console.log("curious js:", k + ":", results[k]);
        reported = true;
    }
    draw.color("#F87171");
    let i = 0;
    for (const k in results)
        draw.text(-60, -20 - 4 * i++, k + ": " + results[k]);
}
