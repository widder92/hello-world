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
for (const name of fs.readdirSync(dir).filter(n => n.endsWith(".js")).sort()) {
    const sandbox = {
        HELLOOS: false, console: { log() {} }, setTimeout, setInterval, clearTimeout, clearInterval,
        queueMicrotask, requestAnimationFrame: fn => { setTimeout(fn, 16); return 1; },
        gc() {},
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
}
setTimeout(() => {
    console.log(`${checks} Erwartungen unter Node geprüft, ${failures} falsch`);
    process.exit(failures ? 1 : 0);
}, 400);
