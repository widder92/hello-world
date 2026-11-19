// async/await, for await und async-Generatoren
var zeilen = [];
function zeige() { zeilen.push(Array.prototype.slice.call(arguments).join(' ')); }
function later(v, ms) { return new Promise(function (res) { setTimeout(function () { res(v); }, ms || 1); }); }
function failLater(m) { return new Promise(function (_, rej) { setTimeout(function () { rej(new Error(m)); }, 1); }); }
async function basic() { return 1; }
zeige(basic() instanceof Promise, typeof basic.prototype);
basic().then(function (v) { zeige('basic', v); });
async function seq() { zeige('seq start'); var a = await later(1, 5); zeige('seq a', a); var b = await 2; zeige('seq b', b); var c = await later(3); return a + b + c; }
zeige('before'); seq().then(function (v) { zeige('seq result', v); }); zeige('after');
async function errs() { try { await failLater('nope'); } catch (e) { zeige('caught', e.message); return 'recovered'; } finally { zeige('finally'); } }
errs().then(function (v) { zeige(v); });
async function thrower() { await null; throw new TypeError('bad'); }
thrower().catch(function (e) { zeige('rejected', e.constructor.name, e.message); });
var arrow = async (x) => { var y = await later(x); return y * 2; };
var arrow2 = async x => await later(x + 1);
arrow(4).then(function (v) { zeige('arrow', v); }); arrow2(4).then(function (v) { zeige('arrow2', v); });
var obj = { v: 7, async m() { return (await later(this.v)) + 1; }, async *ag() { yield 1; yield await later(2); } };
obj.m().then(function (v) { zeige('method', v); });
class K { constructor() { this.n = 3; } async get() { await null; return this.n; } static async make() { return new K(); } }
K.make().then(function (k) { return k.get(); }).then(function (v) { zeige('class', v); });
async function par() { var rs = await Promise.all([later(1, 3), later(2, 1), 3]); return rs.join(); }
par().then(function (v) { zeige('all', v); });
async function loop() { var s = 0; for (var i = 0; i < 5; i++) s += await later(i); return s; }
loop().then(function (v) { zeige('loop', v); });
async function nested() { async function inner(n) { return n <= 0 ? 0 : n + await inner(n - 1); } return await inner(20); }
nested().then(function (v) { zeige('nested', v); });
async function* agen() { for (var i = 0; i < 3; i++) { yield await later(i * 10); } return 'done'; }
(async function () { var a = []; for await (var x of agen()) a.push(x); zeige('for await', a.join()); })();
(async function () { var a = []; for await (var x of [later(1), 2, later(3)]) a.push(x); zeige('for await sync', a.join()); })();
(async function () { var g = agen(); var r1 = await g.next(); var r2 = await g.return('early'); var r3 = await g.next(); zeige('agen', JSON.stringify(r1), JSON.stringify(r2), JSON.stringify(r3)); })();
(async function () { for await (var x of obj.ag()) zeige('ag', x); })();
(async function () { try { for await (var x of agen()) { if (x === 10) throw new Error('stop'); } } catch (e) { zeige('for await throw', e.message); } })();
async function awaitThenable() { return await { then: function (res) { res('thenable'); } }; }
awaitThenable().then(zeige);
async function manyAwaits() { var n = 0; for (var i = 0; i < 300; i++) n += await i; return n; }
manyAwaits().then(function (v) { zeige('many', v); });
// viele gleichzeitig
Promise.all(Array.from({ length: 200 }, function (_, i) { return (async function () { await later(i % 7); return i; })(); })).then(function (rs) { zeige('parallel', rs.length, rs[199]); });
async function retInFinally() { try { return await later('a'); } finally { await later('b'); zeige('fin done'); } }
retInFinally().then(function (v) { zeige('retInFinally', v); });
zeige(Object.prototype.toString.call(agen()), typeof agen().next, agen()[Symbol.asyncIterator] !== undefined);
(async () => { try { await Promise.reject(new Error('r1')); } catch (e) { zeige('reject caught', e.message); } })();
async function gen2() { var it = (function* () { yield 1; yield 2; })(); var s = 0; for (var x of it) s += await later(x); return s; }
gen2().then(function (v) { zeige('gen in async', v); });
zeige('sync end');

setTimeout(function () {
    var erwartet = [
        "true undefined",
        "before",
        "seq start",
        "after",
        "[object AsyncGenerator] function true",
        "sync end",
        "basic 1",
        "reject caught r1",
        "rejected TypeError bad",
        "ag 1",
        "thenable",
        "class 3",
        "nested 210",
        "many 44850",
        "caught nope",
        "finally",
        "recovered",
        "arrow 8",
        "arrow2 5",
        "method 8",
        "for await sync 1,2,3",
        "agen {\"value\":0,\"done\":false} {\"value\":\"early\",\"done\":true} {\"done\":true}",
        "parallel 200 199",
        "ag 2",
        "for await throw stop",
        "all 1,2,3",
        "fin done",
        "retInFinally a",
        "gen in async 3",
        "for await 0,10,20",
        "seq a 1",
        "seq b 2",
        "loop 10",
        "seq result 6"
    ];
    zeilen.sort(); erwartet.sort();        // was auf Zeitgeber wartet, kommt in wechselnder Reihenfolge
    assertEq(zeilen.length, erwartet.length, "Anzahl der Ergebnisse");
    for (var i = 0; i < erwartet.length; i++) assertEq(zeilen[i], erwartet[i], "Ergebnis " + (i + 1));
}, 300);
