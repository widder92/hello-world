// Koroutinen an ihren Grenzen: tiefe Rekursion, Verschachtelung, viele ruhende
var zeilen = [];
function zeige() { zeilen.push(Array.prototype.slice.call(arguments).join(' ')); }
// tiefe Rekursion in einer Koroutine: sauberer Fehler statt Absturz
function deep(n) { return deep(n + 1) + 1; }
function* gd() { try { deep(0); } catch (e) { yield e.constructor.name; } yield 'weiter'; }
zeige([...gd()].join());
async function ad() { try { deep(0); } catch (e) { return e.constructor.name; } }
ad().then(function (v) { zeige('async deep', v); });
// tief verschachtelte Generatoren (jeder setzt den nächsten fort)
function* chain(n) { if (n === 0) { yield 'boden'; return; } yield* chain(n - 1); }
zeige([...chain(300)].join());
function* chain2(n) { if (n === 0) { yield 'boden2'; return; } for (var x of chain2(n - 1)) yield x; }
zeige([...chain2(200)].join());
// eine Koroutine, die mit viel Stapel ruht
function* fat() { var f = function (n) { return n ? [n, f(n - 1)] : 0; }; yield f(50).length; yield 2; }
zeige([...fat()].join());
// Speicher: viele ruhende Koroutinen
var keep = []; for (var i = 0; i < 2000; i++) { var g = (function* (k) { var a = yield k; yield a; })(i); g.next(); keep.push(g); }
zeige(keep.length, keep[1999].next('x').value, !HELLOOS || gc() < 20e6);
keep = null; gc();
// Ausnahme aus dem Generator heraus, danach ist er fertig
function* boom() { yield 1; throw new Error('kaputt'); }
var b = boom(); b.next(); try { b.next(); } catch (e) { zeige(e.message, JSON.stringify(b.next())); }
// async-Funktion, die nie fertig wird, stört niemanden
(async function () { await new Promise(function () {}); zeige('nie'); })();
// Generator in Ereignis-Rückruf fortsetzen
var gg = (function* () { var v = yield 'warte'; zeige('fortgesetzt mit', v); })(); gg.next();
setTimeout(function () { gg.next('timer'); }, 5);
// await in verschachtelten try/finally mit break/continue
(async function () { var r = []; outer: for (var i = 0; i < 3; i++) { try { for (var j = 0; j < 3; j++) { try { if (j == 1) continue outer; r.push(i + '' + j); await null; } finally { r.push('f'); await null; } } } finally { r.push('F'); } } zeige(r.join()); })();
zeige('ende');

setTimeout(function () {
    var erwartet = [
        "RangeError,weiter",
        "boden",
        "boden2",
        "2,2",
        "2000 x true",
        "kaputt {\"done\":true}",
        "ende",
        "async deep RangeError",
        "00,f,f,F,10,f,f,F,20,f,f,F",
        "fortgesetzt mit timer"
    ];
    zeilen.sort(); erwartet.sort();        // was auf Zeitgeber wartet, kommt in wechselnder Reihenfolge
    assertEq(zeilen.length, erwartet.length, "Anzahl der Ergebnisse");
    for (var i = 0; i < erwartet.length; i++) assertEq(zeilen[i], erwartet[i], "Ergebnis " + (i + 1));
}, 300);
