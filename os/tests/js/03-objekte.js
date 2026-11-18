// Objekte und Prototypen
function Tier(name) { this.name = name; } Tier.prototype.laut = function () { return this.name + " macht " + this.ton; }; Tier.prototype.ton = "?";
function Hund(name) { Tier.call(this, name); } Hund.prototype = Object.create(Tier.prototype); Hund.prototype.constructor = Hund; Hund.prototype.ton = "Wau";
var h = new Hund("Rex"); assertEq(h.laut(), "Rex macht Wau"); assert(h instanceof Hund && h instanceof Tier); assertEq(Object.getPrototypeOf(h), Hund.prototype);
assert(h.hasOwnProperty("name") && !h.hasOwnProperty("ton")); assert(Tier.prototype.isPrototypeOf(h)); assertEq(h.constructor, Hund);
assertEq(Object.keys({ b: 1, a: 2, 1: 3, 0: 4 }).join(), "0,1,b,a", "Reihenfolge der Namen"); assertEq(Object.values({ a: 1, b: 2 }).join(), "1,2");
assertEq(Object.entries({ a: 1 })[0].join("="), "a=1"); var ziel = Object.assign({ a: 1 }, { b: 2 }, null, { a: 3 }); assertEq(ziel.a + ziel.b, 5);
var fest = Object.freeze({ x: 1 }); fest.x = 2; fest.y = 3; assertEq(fest.x, 1); assertEq(fest.y, undefined); assert(Object.isFrozen(fest));
assertEq(Object.prototype.toString.call([]), "[object Array]"); assertEq(Object.prototype.toString.call(null), "[object Null]");
assertEq(Object.getOwnPropertyNames([7]).join(), "0,length"); var d = Object.getOwnPropertyDescriptor({ get x() { return 1; } }, "x"); assertEq(typeof d.get, "function");
var ohne = Object.create(null); ohne.a = 1; assertEq("toString" in ohne, false); assertEq(Object.fromEntries([["a", 1], ["b", 2]]).b, 2);
var proto = { gruss() { return "hallo " + this.n; } }; var kind = { __proto__: proto, n: "du" }; assertEq(kind.gruss(), "hallo du"); assertEq(kind.__proto__, proto);
// Klassen
class Form { constructor(n) { this.n = n; } flaeche() { return 0; } beschreibe() { return this.n + ": " + this.flaeche(); } static von(n) { return new this(n); } get kurz() { return this.n[0]; } }
class Kreis extends Form { constructor(r) { super("Kreis"); this.r = r; } flaeche() { return Math.round(Math.PI * this.r * this.r); } beschreibe() { return "[" + super.beschreibe() + "]"; } }
var k = new Kreis(2); assertEq(k.beschreibe(), "[Kreis: 13]"); assertEq(k.kurz, "K"); assert(k instanceof Form); assertEq(Form.von("x").n, "x"); assertEq(Kreis.von(1).r, 1, "statische Methoden werden vererbt");
assertEq(typeof Form, "function"); assertEq(Form.name, "Form"); assertEq(Object.keys(k).join(), "n,r"); assertEq(Object.getOwnPropertyNames(Form.prototype).sort().join(), "beschreibe,constructor,flaeche,kurz");
class Zaehler { anzahl = 0; static stufen = 3; plus() { this.anzahl++; return this; } } var zz = new Zaehler().plus().plus(); assertEq(zz.anzahl, 2, "Felder"); assertEq(Zaehler.stufen, 3);
class Erbe extends Zaehler { extra = this.anzahl + 10; } assertEq(new Erbe().extra, 10, "Felder nach super()");
var Anonym = class { wert() { return 7; } }; assertEq(new Anonym().wert(), 7);
class Stapel extends Array { oben() { return this[this.length - 1]; } } assertEq(typeof Stapel.prototype.oben, "function");
// Felder
var a = [3, 1, 2]; assertEq(a.length, 3); a.push(4, 5); assertEq(a.pop(), 5); assertEq(a.shift(), 3); a.unshift(9); assertEq(a.join("-"), "9-1-2-4");
assertEq(a.slice(1, 3).join(), "1,2"); assertEq(a.slice(-2).join(), "2,4"); assertEq(a.splice(1, 2, "x", "y", "z").join(), "1,2"); assertEq(a.join(), "9,x,y,z,4");
assertEq(a.indexOf("y"), 2); assertEq(a.lastIndexOf("q"), -1); assert(a.includes("z")); assert([NaN].includes(NaN) && [NaN].indexOf(NaN) === -1);
assertEq([1, 2, 3].map(x => x * 2).filter(x => x > 2).reduce((s, x) => s + x, 0), 10); assertEq([1, 2, 3].reduceRight((s, x) => s + x, ""), "321");
assertEq([5, 1, 10].sort().join(), "1,10,5", "sortiert als Text"); assertEq([5, 1, 10].sort((x, y) => x - y).join(), "1,5,10");
var stabil = [{ k: 1, n: "a" }, { k: 0, n: "b" }, { k: 1, n: "c" }, { k: 0, n: "d" }].sort((x, y) => x.k - y.k).map(o => o.n).join(""); assertEq(stabil, "bdac", "stabil");
assert([1, 2].some(x => x > 1) && ![1, 2].every(x => x > 1)); assertEq([1, 2, 3].find(x => x > 1), 2); assertEq([1, 2, 3].findIndex(x => x > 5), -1);
assertEq([1, [2, [3, [4]]]].flat().length, 3); assertEq([1, [2, [3, [4]]]].flat(Infinity).join(), "1,2,3,4"); assertEq([1, 2].flatMap(x => [x, x]).join(), "1,1,2,2");
assertEq([1, 2, 3].reverse().join(), "3,2,1"); assertEq(new Array(3).fill(0).join(), "0,0,0"); assertEq(Array.from("abc").join(), "a,b,c"); assertEq(Array.from({ length: 2, 0: "x", 1: "y" }).join(), "x,y");
assertEq(Array.from([1, 2], x => x * 3).join(), "3,6"); assertEq(Array.of(7, 8).length, 2); assert(Array.isArray([]) && !Array.isArray({ length: 0 })); assertEq([1, 2, 3].at(-1), 3);
assertEq([].concat([1], 2, [[3]]).length, 3); var loch = []; loch[3] = 1; assertEq(loch.length, 4); assertEq(loch[1], undefined); loch.length = 1; assertEq(loch.length, 1);
assertEq(Array.prototype.slice.call({ 0: "a", 1: "b", length: 2 }).join(), "a,b", "feldähnlich"); assertEq(Array.prototype.map.call("ab", c => c + c).join(), "aa,bb");
var gezaehlt = 0; [1, 2, 3].forEach(function (v, i, arr) { gezaehlt += v + i + arr.length; }); assertEq(gezaehlt, 18);
assertEq([...[3, 4].entries()][1].join(), "1,4"); assertEq([..."ab"].length, 2); assertEq([, 1].length, 2); assertEq(String([1, null, undefined, 2]), "1,,,2");
var verschachtelt = [1]; verschachtelt.push(verschachtelt); assertEq(typeof String(verschachtelt), "string", "Feld in sich selbst");
// JSON
assertEq(JSON.stringify({ a: [1, "x", null, true], b: { c: undefined, d: function () {} }, e: 1.5 }), '{"a":[1,"x",null,true],"b":{},"e":1.5}');
assertEq(JSON.stringify("a\"b\n\u0001"), '"a\\"b\\n\\u0001"'); assertEq(JSON.stringify([undefined, NaN]), "[null,null]"); assertEq(JSON.stringify({ a: 1, b: [2] }, null, 2), '{\n  "a": 1,\n  "b": [\n    2\n  ]\n}');
assertEq(JSON.stringify({ a: 1, b: 2 }, ["b"]), '{"b":2}'); assertEq(JSON.stringify({ toJSON() { return 5; } }), "5"); assertEq(JSON.stringify(undefined), undefined);
var gelesen = JSON.parse(' {"a": [1, 2.5e1, -3], "b": {"c": "d\\u00e4\\n"}, "t": true, "n": null} '); assertEq(gelesen.a[1], 25); assertEq(gelesen.b.c, "dä\n"); assertEq(gelesen.n, null);
assertEq(JSON.parse(JSON.stringify({ x: [1, { y: "z" }] })).x[1].y, "z"); var kreis = {}; kreis.selbst = kreis; try { JSON.stringify(kreis); assert(false); } catch (e) { assert(e instanceof TypeError, "Kreis erkannt"); }
["{a:1}", "[1,]", "'x'", "01x", "", "{\"a\":1,}", "nul"].forEach(function (t) { try { JSON.parse(t); assert(false, "ungültig: " + t); } catch (e) { assert(e instanceof SyntaxError); } });
// Map und Set
var m = new Map([[1, "eins"], ["1", "Text"]]); m.set(NaN, "nan").set(m, "selbst"); assertEq(m.size, 4); assertEq(m.get(1), "eins"); assertEq(m.get("1"), "Text"); assertEq(m.get(NaN), "nan"); assertEq(m.get(m), "selbst");
assert(m.has(1) && !m.has(2)); assert(m.delete(1) && !m.delete(1)); assertEq(m.size, 3); assertEq([...m.keys()].length, 3); var mk = []; for (var [mkk, mkv] of m) mk.push(typeof mkk); assertEq(mk.join(), "string,number,object");
var s = new Set([1, 2, 2, 3]); assertEq(s.size, 3); s.add(4).delete(1); assertEq([...s].join(), "2,3,4"); assert(s.has(3)); var sv = 0; s.forEach(v => sv += v); assertEq(sv, 9);
var viele = new Map(); for (var i = 0; i < 300; i++) viele.set("k" + i, i); for (var i = 0; i < 250; i++) viele.delete("k" + i); assertEq(viele.size, 50); assertEq(viele.get("k299"), 299);
// Speicher: viel Abfall
var summe = 0; for (var i = 0; i < 20000; i++) { var tmp = { i: i, t: "x" + i, a: [i, i + 1] }; summe += tmp.a[1] - tmp.i; } assertEq(summe, 20000);
var kette = null; for (var i = 0; i < 5000; i++) kette = { next: kette, v: i }; gc(); var laenge = 0; for (var p = kette; p; p = p.next) laenge++; assertEq(laenge, 5000, "lange Kette übersteht das Aufräumen");

/* Feldmethoden auf Feldähnlichem: So benutzen Bibliotheken wie jQuery sie */
(function () {
    var o = { length: 0 };
    assertEq(Array.prototype.push.call(o, 'a', 'b'), 2, 'push.call: neue Länge');
    assertEq(o.length, 2, 'push.call: length am Objekt');
    assertEq(o[0] + o[1], 'ab', 'push.call: Glieder am Objekt');
    Array.prototype.push.apply(o, ['c', 'd']);
    assertEq(o.length + o[3], '4d', 'push.apply');
    assertEq(Array.prototype.pop.call(o), 'd', 'pop.call');
    assertEq(o.length, 3, 'pop.call: length');
    assertEq(3 in o, false, 'pop.call: Glied entfernt');
    assertEq(Array.prototype.shift.call(o), 'a', 'shift.call');
    assertEq(o[0] + o[1] + o.length, 'bc2', 'shift.call: nachgerückt');
    assertEq(Array.prototype.unshift.call(o, 'z'), 3, 'unshift.call');
    assertEq(o[0], 'z', 'unshift.call: vorn');
    var removed = Array.prototype.splice.call(o, 1, 1, 'x', 'y');
    assertEq(removed.join(), 'b', 'splice.call: entfernt');
    assertEq(Array.prototype.join.call(o, ''), 'zxyc', 'splice.call: Ergebnis');
    assertEq(Array.prototype.reverse.call(o), o, 'reverse.call gibt das Objekt zurück');
    assertEq(Array.prototype.join.call(o, ''), 'cyxz', 'reverse.call');
    assertEq(Array.prototype.sort.call(o), o, 'sort.call gibt das Objekt zurück');
    assertEq(Array.prototype.join.call(o, ''), 'cxyz', 'sort.call');
    var leer = {};
    Array.prototype.push.call(leer, 7);
    assertEq(leer.length + ':' + leer[0], '1:7', 'push.call auf einem Objekt ohne length');
    var a = [3, 1, 2];
    assertEq(a.sort(), a, 'sort gibt das Feld selbst zurück');
    assertEq(a.splice(0, 1).join() + '|' + a.join(), '1|2,3', 'splice auf einem Feld');
    var threw = false;
    try { Array.prototype.push.call(null, 1); } catch (e) { threw = e instanceof TypeError; }
    assert(threw, 'push.call(null) wirft TypeError');
})();

/* JSON.parse mit Nachbearbeitung (reviver) - samt Quelltext einfacher Werte */
(function () {
    var seen = [];
    var r = JSON.parse('{"a":1,"b":[2,3,{"c":"x"}],"d":null,"e":true}', function (key, value) {
        seen.push(key + '=' + (typeof value === 'object' && value !== null ? (Array.isArray(value) ? 'Feld' : 'Objekt') : String(value)));
        return typeof value === 'number' ? value * 10 : value;
    });
    assertEq(seen.join(' '), 'a=1 0=2 1=3 c=x 2=Objekt b=Feld d=null e=true =Objekt', 'reviver: innere Glieder zuerst, zuletzt das Ganze');
    assertEq(JSON.stringify(r), '{"a":10,"b":[20,30,{"c":"x"}],"d":null,"e":true}', 'reviver: Ergebnis eingesetzt');
    var cut = JSON.parse('{"a":1,"weg":2,"b":{"weg":3,"c":4}}', function (key, value) { return key === 'weg' ? undefined : value; });
    assertEq(JSON.stringify(cut), '{"a":1,"b":{"c":4}}', 'reviver: undefined entfernt das Glied');
    assertEq(JSON.parse('5', function (key, value) { return key === '' ? value + 1 : value; }), 6, 'reviver am einfachen Wert');
    var holder = null;
    JSON.parse('{"x":{"y":1}}', function (key, value) { if (key === 'y') holder = this; return value; });
    assertEq(JSON.stringify(holder), '{"y":1}', 'reviver: this ist das umgebende Objekt');
    var dates = JSON.parse('{"wann":"2026-11-15T10:00:00.000Z"}', function (key, value) {
        return typeof value === 'string' && /^\d{4}-\d\d-\d\dT/.test(value) ? new Date(value) : value;
    });
    assertEq(dates.wann instanceof Date && dates.wann.getUTCFullYear(), 2026, 'reviver: Datum wiederherstellen');
    assertEq(JSON.parse('[1,2]', 'kein reviver').length, 2, 'was keine Funktion ist, wird übergangen');
    var threw = false;
    try { JSON.parse('[1,2]', function () { throw new Error('halt'); }); } catch (e) { threw = e.message === 'halt'; }
    assert(threw, 'reviver: Ausnahmen kommen durch');

    /* der Quelltext einfacher Werte (dritter Parameter) */
    var sources = [];
    JSON.parse('{"gross":9007199254740993,"t":"a\\u0041","w":true,"n":null,"f":[1.50,-0],"o":{}}', function (key, value, context) {
        sources.push(key + ':' + (context && context.source !== undefined ? context.source : '-'));
        return value;
    });
    assertEq(sources.join(' '), 'gross:9007199254740993 t:"a\\u0041" w:true n:null 0:1.50 1:-0 f:- o:- :-',
             'Quelltext: bei einfachen Werten wörtlich, bei Feldern und Objekten keiner');
    var big = JSON.parse('{"id":12345678901234567890}', function (key, value, context) {
        return key === 'id' ? context.source : value;
    });
    assertEq(big.id, '12345678901234567890', 'große Zahl ohne Verlust über den Quelltext');
    var changed = [];
    JSON.parse('{"a":1,"b":2}', function (key, value, context) {
        if (key === 'a') this.b = 99;
        changed.push(key + ':' + (context.source === undefined ? '-' : context.source));
        return value;
    });
    assertEq(changed.join(' '), 'a:1 b:- :-', 'geänderte Werte haben keinen Quelltext mehr');
})();
