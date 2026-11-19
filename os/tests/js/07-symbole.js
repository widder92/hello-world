// Symbole und das Durchlaufen von Werten: Symbol, for-of, Spread, Zerlegung, Iteratoren
var zeilen = [];
function zeige() { zeilen.push(Array.prototype.slice.call(arguments).join(' ')); }
var s = Symbol('x'), t = Symbol('x');
zeige(typeof s, s === t, s === s, String(s), s.toString(), s.description, Symbol().description);
var o = {}; o[s] = 1; o.a = 2;
zeige(Object.keys(o).join(), JSON.stringify(o), o[s], Object.getOwnPropertySymbols(o).length, s in o);
for (var k in o) zeige('key', k);
zeige(Symbol.for('a') === Symbol.for('a'), Symbol.keyFor(Symbol.for('a')), Symbol.keyFor(s));
try { s + ''; } catch (e) { zeige('E1', e.message); }
try { +s; } catch (e) { zeige('E2', e.message); }
try { new Symbol(); } catch (e) { zeige('E3', e.message); }
zeige(Object.prototype.toString.call(s), Object(s) instanceof Symbol, typeof Object(s));
class A { get [Symbol.toStringTag]() { return 'A'; } static [Symbol.hasInstance](x) { return x === 1; } }
zeige(Object.prototype.toString.call(new A()), 1 instanceof A, 2 instanceof A);
var p = { [Symbol.toPrimitive](h) { return h === 'number' ? 42 : h === 'string' ? 'str' : 'def'; } };
zeige(+p, `${p}`, p + '', p * 2);
var it = { [Symbol.iterator]() { var i = 0; return { next() { return i < 3 ? { value: i++, done: false } : { done: true }; },
   return() { zeige('closed'); return {}; } }; } };
zeige([...it].join(), Array.from(it).join());
for (var x of it) { if (x == 1) break; }
var [a, b] = it; zeige(a, b);
var [c, , ...d] = [1, 2, 3, 4]; zeige(c, d.join());
var ai = [7, 8][Symbol.iterator](); zeige(JSON.stringify(ai.next()), JSON.stringify(ai.next()), JSON.stringify(ai.next()), JSON.stringify(ai.next()));
zeige(JSON.stringify([...[5, 6].entries()]), [...[5, 6].keys()].join(), [...'aäb'].join('|'), typeof [].values().next);
zeige(Object.prototype.toString.call([].values()), [].values()[Symbol.iterator]() !== undefined);
var m = new Map([[1, 2], [3, 4]]); for (var [k2, v2] of m) zeige(k2, v2);
zeige([...new Set([1, 1, 2])].join(), Array.from(new Map([[1, 2]])).join());
function f() { return [...arguments].length; } zeige(f(1, 2, 3));
try { for (var z of 5) {} } catch (e) { zeige('E4', e.constructor.name); }
try { var [q] = null; } catch (e) { zeige('E5', e.message); }
var o2 = Object.assign({}, o); zeige(o2[s]);
var o3 = { ...o }; zeige(o3[s], s == s, s == 'x', s != t);
zeige([1,2,3].includes(2), Array.from({length: 2}).length, Array.from('ab').join());
function ret() { for (var x of it) { return x; } } zeige(ret());
outer: for (var i = 0; i < 2; i++) { for (var x of it) { continue outer; } }
try { for (var x of it) { throw new Error('boom'); } } catch (e) { zeige(e.message); }
zeige(Symbol.iterator.toString(), Symbol.iterator.description);

setTimeout(function () {
    var erwartet = [
        "symbol false true Symbol(x) Symbol(x) x ",
        "a {\"a\":2} 1 1 true",
        "key a",
        "true a ",
        "E1 Cannot convert a Symbol value to a string",
        "E2 Cannot convert a Symbol value to a number",
        "E3 Symbol is not a constructor",
        "[object Symbol] true object",
        "[object A] true false",
        "42 str def 84",
        "0,1,2 0,1,2",
        "closed",
        "closed",
        "0 1",
        "1 3,4",
        "{\"value\":7,\"done\":false} {\"value\":8,\"done\":false} {\"done\":true} {\"done\":true}",
        "[[0,5],[1,6]] 0,1 a|ä|b function",
        "[object Array Iterator] true",
        "1 2",
        "3 4",
        "1,2 1,2",
        "3",
        "E4 TypeError",
        "E5 null is not iterable",
        "1",
        "1 true false true",
        "true 2 a,b",
        "closed",
        "0",
        "closed",
        "closed",
        "closed",
        "boom",
        "Symbol(Symbol.iterator) Symbol.iterator"
    ];
    assertEq(zeilen.length, erwartet.length, "Anzahl der Ergebnisse");
    for (var i = 0; i < erwartet.length; i++) assertEq(zeilen[i], erwartet[i], "Ergebnis " + (i + 1));
}, 300);
