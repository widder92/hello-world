/*
 * Gegenprobe für tests/js/*.js: Dieselben Skripte laufen unter Node.js. Was
 * dort eine Erwartung verfehlt, ist ein Fehler im Test, nicht in HelloOS.
 * Wo HelloOS bewusst abweicht, fragen die Skripte die Variable HELLOOS ab.
 *
 *   TZ=UTC LANG=de_DE.UTF-8 node tests/js_oracle.js
 */
const fs = require("fs"), path = require("path"), vm = require("vm");
const dir = path.join(__dirname, "js");
let checks = 0, failures = 0;

/* Ein Skript nach dem anderen - jedes bekommt Zeit für seine Zeitgeber, ehe das nächste rechnet. */
function runOne(name) {
    const sandbox = {
        HELLOOS: false, console: { log() {} }, setTimeout, setInterval, clearTimeout, clearInterval,
        queueMicrotask, requestAnimationFrame: fn => { setTimeout(fn, 16); return 1; },
        gc() {}, TextEncoder, TextDecoder, structuredClone,
        assert(ok, what) {
            checks++;
            if (!ok) { failures++; console.log(`ERWARTUNG FALSCH in ${name}: ${what}`); }
        },
        assertEq(got, want, what) {
            checks++;
            if (!(got === want || (got !== got && want !== want))) {
                failures++;
                console.log(`ERWARTUNG FALSCH in ${name}: ${what || ""} - Node liefert "${got}", der Test will "${want}"`);
            }
        },
    };
    try {
        vm.runInNewContext(fs.readFileSync(path.join(dir, name), "utf8"), sandbox, { filename: name });
    } catch (e) {
        failures++;
        console.log(`ERWARTUNG FALSCH in ${name}: Node bricht ab mit ${e}`);
    }
    return new Promise(done => setTimeout(done, 450));
}

(async () => {
    for (const name of fs.readdirSync(dir).filter(n => n.endsWith(".js")).sort())
        await runOne(name);
    console.log(`${checks} Erwartungen unter Node geprüft, ${failures} falsch`);
    process.exit(failures ? 1 : 0);
})();
