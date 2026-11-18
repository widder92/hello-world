// Variablen und Gültigkeit
var a = 1; { var a = 2; } assertEq(a, 2, "var kennt keine Blöcke");
let b = 1; { let b = 2; assertEq(b, 2); } assertEq(b, 1, "let kennt Blöcke");
const c = 5; try { c = 6; assert(false, "const änderbar"); } catch (e) { assert(e instanceof TypeError, "const wirft TypeError"); }
assertEq(typeof hoisted, "function", "Funktionen gelten ab Beginn"); function hoisted() { return 1; }
assertEq(spaeter, undefined, "var gilt ab Beginn"); var spaeter = 3;
function zaehler() { var n = 0; return function () { return ++n; }; }
var z1 = zaehler(), z2 = zaehler(); z1(); z1(); assertEq(z1(), 3, "Funktion merkt sich ihre Umgebung"); assertEq(z2(), 1);
var fs = []; for (let i = 0; i < 3; i++) fs.push(() => i); assertEq(fs.map(f => f()).join(), "0,1,2", "let je Runde");
var gs = []; for (var j = 0; j < 3; j++) gs.push(function () { return j; }); assertEq(gs[0](), 3, "var einmal für alle");
// Funktionen
function summe() { var s = 0; for (var i = 0; i < arguments.length; i++) s += arguments[i]; return s; }
assertEq(summe(1, 2, 3, 4), 10, "arguments"); assertEq(summe.length, 0); assertEq(hoisted.name, "hoisted");
function vorgabe(a, b = a * 2, ...rest) { return a + b + rest.length; }
assertEq(vorgabe(1), 3); assertEq(vorgabe(1, 5), 6); assertEq(vorgabe(1, 2, 9, 9), 5); assertEq(vorgabe.length, 1);
var pfeil = (x, y) => x * y; assertEq(pfeil(3, 4), 12); var einer = x => x + 1; assertEq(einer(1), 2);
var block = x => { return { v: x }; }; assertEq(block(7).v, 7); var objekt = () => ({ a: 1 }); assertEq(objekt().a, 1);
var obj = { v: 10, normal: function () { return this.v; }, pfeil: () => typeof this, kurz() { return this.v + 1; } };
assertEq(obj.normal(), 10, "this in Methode"); assertEq(obj.kurz(), 11);
var frei = obj.normal; assertEq(frei.call({ v: 3 }), 3, "call"); assertEq(frei.apply({ v: 4 }, []), 4, "apply");
var gebunden = frei.bind({ v: 5 }); assertEq(gebunden(), 5, "bind");
function addiere(a, b, c) { return a + b + c; } assertEq(addiere.bind(null, 1, 2)(3), 6, "bind mit Argumenten");
assertEq(Math.max.apply(null, [1, 9, 3]), 9); assertEq(Math.max(...[1, 9, 3], 4), 9, "spread im Aufruf");
function Aussen() { this.v = 1; var f = () => this.v; return f(); } assertEq(new Aussen().v, 1); 
var selbst = function fak(n) { return n <= 1 ? 1 : n * fak(n - 1); }; assertEq(selbst(5), 120, "benannter Funktionsausdruck");
assertEq((function () { return typeof arguments; })(), "object");
assertEq((function (a, b) { return arguments.length; })(1, 2, 3), 3);
assertEq(new Function("a", "b", "return a * b")(6, 7), 42, "new Function");
assert(String(function f(x) { return x; }).indexOf("return x") > 0, "toString zeigt Quelltext");
// Kontrollfluss
var n = 0; for (var i = 0; i < 10; i++) { if (i % 2) continue; if (i > 6) break; n += i; } assertEq(n, 12, "continue/break");
aussen: for (var i = 0; i < 3; i++) { for (var j = 0; j < 3; j++) { if (j == 1) continue aussen; if (i == 2) break aussen; n++; } } assertEq(n, 14, "Marken");
var w = 0; while (true) { if (++w > 4) break; } assertEq(w, 5); var d = 0; do { d++; } while (d < 3); assertEq(d, 3);
function schalter(x) { switch (x) { case 1: return "eins"; case 2: case 3: return "zwei-drei"; default: return "sonst"; case 4: return "vier"; } }
assertEq(schalter(1), "eins"); assertEq(schalter(3), "zwei-drei"); assertEq(schalter(9), "sonst"); assertEq(schalter(4), "vier");
var fall = ""; switch (2) { case 1: fall += "a"; case 2: fall += "b"; case 3: fall += "c"; break; case 4: fall += "d"; } assertEq(fall, "bc", "durchfallen");
var keys = []; for (var k in { x: 1, y: 2 }) keys.push(k); assertEq(keys.join(), "x,y", "for-in");
var arr = []; for (var v of [10, 20]) arr.push(v); for (const ch of "äb") arr.push(ch); assertEq(arr.join(), "10,20,ä,b", "for-of");
var paare = []; for (const [k2, v2] of Object.entries({ a: 1 })) paare.push(k2 + v2); assertEq(paare[0], "a1");
// Ausnahmen
function wirft() { throw new RangeError("zu groß"); }
try { wirft(); assert(false); } catch (e) { assert(e instanceof RangeError && e instanceof Error); assertEq(e.message, "zu groß"); assertEq(e.name, "RangeError"); assertEq(String(e), "RangeError: zu groß"); }
var ablauf = ""; function tf() { try { ablauf += "t"; return "r"; } finally { ablauf += "f"; } } assertEq(tf(), "r"); assertEq(ablauf, "tf", "finally nach return");
function tf2() { try { throw 1; } catch (e) { return "c" + e; } finally { ablauf += "!"; } } assertEq(tf2(), "c1"); assertEq(ablauf, "tf!");
function tf3() { for (var i = 0; i < 3; i++) { try { if (i == 1) continue; ablauf += i; } finally { ablauf += "."; } } } tf3(); assertEq(ablauf, "tf!0..2.");
try { try { throw "innen"; } finally { ablauf = "f"; } } catch (e) { assertEq(e, "innen", "Ausnahme übersteht finally"); }
try { undefiniert(); } catch (e) { assert(e instanceof ReferenceError, "ReferenceError"); }
try { null.x; } catch (e) { assert(e instanceof TypeError, "TypeError bei null"); }
try { (void 0)(); } catch (e) { assert(e instanceof TypeError, "kein Aufruf"); }
try { throw { code: 7 }; } catch ({ code }) { assertEq(code, 7, "catch zerlegt"); }
try { JSON.parse("{"); } catch (e) { assert(e instanceof SyntaxError, "JSON-Fehler"); }
try { eval("var 1x;"); assert(false); } catch (e) { assert(e instanceof SyntaxError, "Syntaxfehler in eval"); }
function tief(n) { return tief(n + 1); } try { tief(0); assert(false); } catch (e) { assert(e instanceof RangeError, "zu tiefe Rekursion wird aufgefangen"); }
class MeinFehler extends Error { constructor(m) { super(m); this.name = "MeinFehler"; } }
try { throw new MeinFehler("eigen"); } catch (e) { assert(e instanceof MeinFehler && e instanceof Error); assertEq(e.message, "eigen"); assertEq(String(e), "MeinFehler: eigen"); }
// Zerlegung und Spread
var [x1, , x3 = 9, ...xr] = [1, 2, undefined, 4, 5]; assertEq(x1 + x3 + xr.length, 12);
var { p, q: { r = 5, s } = {}, ...uebrig } = { p: 1, q: { s: 2 }, t: 3, u: 4 }; assertEq(p + r + s, 8); assertEq(Object.keys(uebrig).join(), "t,u");
var t1 = 1, t2 = 2; [t1, t2] = [t2, t1]; assertEq(t1 * 10 + t2, 21, "tauschen"); ({ a: t1, b: t2 = 7 } = { a: 5 }); assertEq(t1 + t2, 12);
function zerlegt({ a, b = 2 }, [c, d]) { return a + b + c + d; } assertEq(zerlegt({ a: 1 }, [3, 4]), 10);
var kopie = { ...{ a: 1, b: 2 }, b: 3 }; assertEq(kopie.a + kopie.b, 4); assertEq([...[1, 2], ..."ab", 3].join(), "1,2,a,b,3");
var name = "k"; var dyn = { [name + 1]: 5, name }; assertEq(dyn.k1, 5); assertEq(dyn.name, "k");
// Vorlagen
var wer = "Welt"; assertEq(`Hallo ${wer}! ${1 + 1} ${`innen ${wer}`}`, "Hallo Welt! 2 innen Welt"); assertEq(`a
b`, "a\nb"); assertEq(`\${x}`, "${x}");
// Zugriffsfunktionen
var konto = { _s: 1, get stand() { return this._s; }, set stand(v) { this._s = v * 2; } }; konto.stand = 5; assertEq(konto.stand, 10);
var def = {}; Object.defineProperty(def, "fest", { value: 1 }); def.fest = 2; assertEq(def.fest, 1, "nicht schreibbar"); assertEq(Object.keys(def).length, 0, "nicht aufzählbar");
Object.defineProperty(def, "rechne", { get: function () { return 42; }, enumerable: true }); assertEq(def.rechne, 42);
// delete, in, instanceof
var del = { a: 1 }; assert("a" in del); delete del.a; assert(!("a" in del)); assert(del.hasOwnProperty("a") === false); assert("toString" in del);
assert([] instanceof Array && [] instanceof Object); assert(!(1 instanceof Number)); assert(new Number(1) instanceof Number);
// Zeilenenden ohne Semikolon
var asi = 1
var asi2 = asi
+ 1
assertEq(asi2, 2)
function ret() { return
  5 }
assertEq(ret(), undefined, "return am Zeilenende")
var i1 = 1; var i2 = i1
++i1
assertEq(i1, 2); assertEq(i2, 1)
