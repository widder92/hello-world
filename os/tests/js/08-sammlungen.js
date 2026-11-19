// Map, Set, WeakMap und WeakSet - auch beim Ändern während des Durchlaufs - und Unterklassen eingebauter Klassen
var zeilen = [];
function zeige() { zeilen.push(Array.prototype.slice.call(arguments).join(' ')); }
var m = new Map([[1, 'a'], ['1', 'b'], [NaN, 'n'], [0, 'z']]);
zeige(m.size, m.get(1), m.get('1'), m.get(NaN), m.get(-0), m.has(2), m instanceof Map, Object.prototype.toString.call(m));
var o1 = {}, o2 = {}; m.set(o1, 1).set(o2, 2); zeige(m.get(o1), m.get(o2), m.get({}), m.delete(o1), m.delete(o1), m.size);
zeige([...m.keys()].length, [...m.values()].join(), JSON.stringify([...m.entries()].slice(0, 2)), typeof m.keys().next);
var seen = []; m.forEach(function (v, k, mm) { seen.push(typeof k + ':' + v + (mm === m)); }); zeige(seen.join());
// Ändern beim Durchlaufen
var s = new Set([1, 2, 3]); var out = [];
for (var x of s) { out.push(x); if (x === 1) { s.delete(2); s.add(4); } if (x === 4 && out.length < 6) s.add(5); }
zeige(out.join(), s.size, [...s].join());
var big = new Map(); for (var i = 0; i < 5000; i++) big.set('k' + i, i); for (i = 0; i < 5000; i += 2) big.delete('k' + i);
var sum = 0; for (var [k, v] of big) sum += v; zeige(big.size, sum, big.get('k4999'), big.get('k4998'));
var it = big.keys(); it.next(); for (i = 5000; i < 20000; i++) big.set('k' + i, i); zeige(it.next().value, big.size);
big.clear(); zeige(big.size, it.next().done, [...big].length);
var st = new Set('hello'); zeige([...st].join(''), st.has('l'), new Set([[1]]).size, new Set().add(1).add(1).size);
zeige(JSON.stringify([...new Set([3, 1, 3]).entries()]), [...new Set([3,1]).keys()].join(), Object.prototype.toString.call(s.values()));
var w = new WeakMap(); var key = {}; w.set(key, 5); zeige(w.get(key), w.has(key), w.has({}), w.delete(key), w.has(key), typeof w.size);
try { w.set(1, 2); } catch (e) { zeige('E', e.message); }
try { Map(); } catch (e) { zeige('E', e.message); }
try { new Map([1]); } catch (e) { zeige('E', e.constructor.name); }
var ws = new WeakSet([key]); zeige(ws.has(key), ws.has({}));
class M2 extends Map { get2(k) { return this.get(k) * 2; } }
var m2 = new M2([[1, 21]]); zeige(m2.get2(1), m2 instanceof M2, m2 instanceof Map, m2.size);
class L extends Array { sum() { return this.reduce((a, b) => a + b, 0); } }
var l = new L(); l.push(1, 2); zeige(l.sum(), l.length, Array.isArray(l), l instanceof L);
class E2 extends Error { constructor(m) { super(m); this.name = 'E2'; } }
class E3 extends E2 { constructor(m) { super(m + '!'); this.extra = 1; } }
var e3 = new E3('x'); zeige(e3 instanceof E3, e3 instanceof E2, e3 instanceof Error, e3.message, e3.name, e3.extra, String(e3));
zeige(new Map([[1, 2]]).entries().next().value.join(), Array.from(new Map([[1, 2]]).values()).join(), new Map(new Map([[1, 2]])).get(1));
var mk = new Map(); mk.set(-0, 'x'); zeige(Object.is([...mk.keys()][0], 0));

setTimeout(function () {
    var erwartet = [
        "4 a b n z false true [object Map]",
        "1 2  true false 5",
        "5 a,b,n,z,2 [[1,\"a\"],[\"1\",\"b\"]] function",
        "number:atrue,string:btrue,number:ntrue,number:ztrue,object:2true",
        "1,3,4,5 4 1,3,4,5",
        "2500 6250000 4999 ",
        "k3 17500",
        "0 true 0",
        "helo true 1 1",
        "[[3,3],[1,1]] 3,1 [object Set Iterator]",
        "5 true false true false undefined",
        "E Invalid value used as weak map key",
        "E Constructor Map requires 'new'",
        "E TypeError",
        "true false",
        "42 true true 1",
        "3 2 true true",
        "true true true x! E2 1 E2: x!",
        "1,2 2 2",
        "true"
    ];
    assertEq(zeilen.length, erwartet.length, "Anzahl der Ergebnisse");
    for (var i = 0; i < erwartet.length; i++) assertEq(zeilen[i], erwartet[i], "Ergebnis " + (i + 1));
}, 300);
