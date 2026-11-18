// Zeitgeber, Promises und was nacheinander geschieht
var kette = [], zeit = [], rest = [], erstes = "";
function merke(liste, was) { if (!erstes) erstes = was; liste.push(was); }
setTimeout(function () { merke(zeit, "t20"); }, 20);
setTimeout(function () { merke(zeit, "t10"); }, 10);
setTimeout(function (a, b) { merke(zeit, "arg" + a + b); }, 15, 1, 2);
var abgesagt = setTimeout(function () { merke(zeit, "NIE"); }, 5); clearTimeout(abgesagt);
var runden = 0, takt = setInterval(function () { runden++; if (runden == 3) clearInterval(takt); }, 7);
Promise.resolve(1).then(function (v) { merke(kette, "p" + v); return v + 1; }).then(function (v) { merke(kette, "p" + v); throw new Error("x"); })
    .catch(function (e) { merke(kette, "c" + e.message); return "ok"; }).finally(function () { merke(kette, "f"); }).then(function (v) { merke(kette, v); });
new Promise(function (res, rej) { rej("nein"); }).then(null, function (r) { merke(rest, "r" + r); });
Promise.all([1, Promise.resolve(2), new Promise(function (res) { setTimeout(function () { res(3); }, 12); })]).then(function (v) { merke(zeit, "all" + v.join("")); });
Promise.race([new Promise(function (res) { setTimeout(res, 30, "langsam"); }), new Promise(function (res) { setTimeout(res, 3, "schnell"); })]).then(function (v) { merke(zeit, v); });
Promise.allSettled([Promise.reject(1), 2]).then(function (r) { merke(rest, r[0].status + "/" + r[1].status + "/" + r[0].reason + r[1].value); });
var dann = { then: function (res) { res("fremd"); } }; Promise.resolve(dann).then(function (v) { merke(rest, v); });
queueMicrotask(function () { merke(rest, "mikro"); });
new Promise(function () { throw new TypeError("im Aufbau"); }).catch(function (e) { merke(rest, e.name); });
Promise.reject(new Error("weiter")).then(function () { merke(rest, "NIE"); }).catch(function (e) { merke(rest, e.message); });
var spaet = new Promise(function (res) { res("einmal"); res("zweimal"); }); spaet.then(function (v) { merke(rest, v); });
merke(rest, "sofort");
setTimeout(function () {
    assertEq(erstes, "sofort", "erst der laufende Code, dann das Vorgemerkte");
    assertEq(kette.join(" "), "p1 p2 cx f ok", "Kette mit then, catch, finally");
    assertEq(zeit.join(" "), "schnell t10 all123 arg12 t20", "Zeitgeber in der Reihenfolge ihrer Fälligkeit");
    assertEq(rest.slice().sort().join(" "), "TypeError einmal fremd mikro rejected/fulfilled/12 rnein sofort weiter", "alles Übrige kam an");
    assertEq(runden, 3, "setInterval");
}, 100);
assertEq(typeof requestAnimationFrame(function () {}), "number");
