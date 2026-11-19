// Generatoren: function*, yield, yield*, next/return/throw
var zeilen = [];
function zeige() { zeilen.push(Array.prototype.slice.call(arguments).join(' ')); }
var log = [];
function* count(n) { for (var i = 0; i < n; i++) yield i; }
zeige([...count(4)].join(), Array.from(count(3)).join(), Math.max(...count(5)));
var a = []; for (var x of count(3)) a.push(x * 2); zeige(a.join());
function* inf() { var i = 0; while (true) yield i++; }
var [p, q, r] = inf(); zeige(p, q, r);
for (var v of inf()) { if (v > 3) break; } zeige('broke', v);
function* fin() { try { yield 1; yield 2; } finally { log.push('fin'); } }
for (var f of fin()) break; zeige(log.join());
var g = fin(); g.next(); zeige(JSON.stringify(g.return(9)), JSON.stringify(g.next()), log.join());
function* thrower() { try { yield 1; } catch (e) { zeige('caught', e); yield 'after'; } }
g = thrower(); g.next(); zeige(JSON.stringify(g.throw('boom')), JSON.stringify(g.next()));
g = thrower(); try { g.throw(new Error('early')); } catch (e) { zeige('early:', e.message); } zeige(JSON.stringify(g.next()));
function* inner() { var x = yield 'a'; zeige('inner got', x); return 'ret'; }
function* outer() { var r = yield* inner(); zeige('outer r', r); yield* [1, 2]; return 'end'; }
g = outer(); zeige(JSON.stringify(g.next()), JSON.stringify(g.next('X')), JSON.stringify(g.next()), JSON.stringify(g.next()), JSON.stringify(g.next()));
function* fy() { try { yield 1; } finally { yield 'cleanup'; } }
g = fy(); g.next(); zeige(JSON.stringify(g.return(5)), JSON.stringify(g.next()), JSON.stringify(g.next()));
var o = { *gen() { yield this.v; }, v: 42 }; zeige([...o.gen()].join());
class C { *[Symbol.iterator]() { yield 1; yield 2; } static *s() { yield 's'; } }
zeige([...new C()].join(), [...C.s()].join());
zeige(Object.prototype.toString.call(count(1)), typeof count(1).next, count(1)[Symbol.iterator]() !== undefined, count.prototype !== undefined);
g = count(1); zeige(g[Symbol.iterator]() === g, g instanceof count, Object.getPrototypeOf(g) === count.prototype);
function* rec(n) { if (n > 0) { yield n; yield* rec(n - 1); } }
zeige([...rec(50)].length, [...rec(5)].join());
function* reent() { try { it2.next(); } catch (e) { yield e.constructor.name; } } var it2 = reent(); zeige(it2.next().value);
try { new count(1); } catch (e) { zeige('E', e.constructor.name); }
function* args(a, b = a + 1, ...rest) { yield a; yield b; yield rest.length; yield arguments.length; }
zeige([...args(1, undefined, 3, 4)].join());
var gexpr = function* () { yield* 'ab'; }; zeige([...gexpr()].join());
function* big() { for (var i = 0; i < 20000; i++) yield i; } var sum = 0; for (var n of big()) sum += n; zeige(sum);
function* nest() { var m = new Map([[1, 2]]); for (var [k, v] of m) { yield k + v; } yield [...count(2)].length; }
zeige([...nest()].join());
// Generatoren, die einander rufen (verschachtelte Wechsel)
function* evens() { for (var x of count(6)) if (x % 2 == 0) yield x; }
function* squares() { for (var x of evens()) yield x * x; }
zeige([...squares()].join());
// return im finally bei Ausnahme
function* tf() { try { throw new Error('x'); } finally { yield 'f'; } }
g = tf(); zeige(JSON.stringify(g.next())); try { g.next(); } catch (e) { zeige('rethrown', e.message); }
// Abgebrochene Generatoren: liegen bleiben, nichts stürzt ab
(function () { for (var i = 0; i < 200; i++) { var z = inf(); z.next(); z.next(); } })();
gc(); zeige('gc ok');
// yield als Ausdruck an verschiedenen Stellen
function* ex() { var o = { a: yield 1, b: [yield 2, yield 3] }; var s = `${yield 4}!`; return [o.a, o.b.join(), s, (yield 5) + (yield 6)].join('|'); }
g = ex(); g.next(); g.next('A'); g.next('B'); g.next('C'); g.next('D'); g.next(10); zeige(JSON.stringify(g.next(20)));

setTimeout(function () {
    var erwartet = [
        "0,1,2,3 0,1,2 4",
        "0,2,4",
        "0 1 2",
        "broke 4",
        "fin",
        "{\"value\":9,\"done\":true} {\"done\":true} fin,fin",
        "caught boom",
        "{\"value\":\"after\",\"done\":false} {\"done\":true}",
        "early: early",
        "{\"done\":true}",
        "inner got X",
        "outer r ret",
        "{\"value\":\"a\",\"done\":false} {\"value\":1,\"done\":false} {\"value\":2,\"done\":false} {\"value\":\"end\",\"done\":true} {\"done\":true}",
        "{\"value\":\"cleanup\",\"done\":false} {\"value\":5,\"done\":true} {\"done\":true}",
        "42",
        "1,2 s",
        "[object Generator] function true true",
        "true true true",
        "50 5,4,3,2,1",
        "TypeError",
        "E TypeError",
        "1,2,2,4",
        "a,b",
        "199990000",
        "3,2",
        "0,4,16",
        "{\"value\":\"f\",\"done\":false}",
        "rethrown x",
        "gc ok",
        "{\"value\":\"A|B,C|D!|30\",\"done\":true}"
    ];
    assertEq(zeilen.length, erwartet.length, "Anzahl der Ergebnisse");
    for (var i = 0; i < erwartet.length; i++) assertEq(zeilen[i], erwartet[i], "Ergebnis " + (i + 1));
}, 300);
