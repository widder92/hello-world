// Zahlentrenner, f`...`, private Mitglieder (#x), static-Blöcke, new.target
var zeilen = [];
function zeige() { zeilen.push(Array.prototype.slice.call(arguments).join(' ')); }
zeige(1_000_000, 0xFF_FF, 0b1010_1010, 1_0.5_0, 1e1_0, .5, 5., 1_2e-1);
function tag(strings, ...vals) { return strings.raw.join('|') + '#' + strings.join('|') + '#' + vals.join(','); }
zeige(tag`a${1}b${2}c`, tag`\n${'x'}\t`, tag``, String.raw`a\n${1 + 1}A`);
function same() { return (s => s)`x${1}y`; } zeige(same() === same(), Object.isFrozen(same()), same().raw.length);
var o = { t(s) { return this.v + s[0]; }, v: 'V' }; zeige(o.t`!`, o['t']`?`);
class Counter {
  #count = 0; static #instances = 0; static total = 10;
  #secret() { return 'geheim' + this.#count; }
  get #doubled() { return this.#count * 2; }
  set #doubled(v) { this.#count = v / 2; }
  static #bump() { return ++Counter.#instances; }
  static { Counter.total += 5; Counter.ready = this === Counter; }
  constructor() { Counter.#bump(); }
  inc() { this.#count++; return this; }
  add(n) { this.#count += n; return this; }
  get value() { return this.#count; }
  secret() { return this.#secret(); }
  double() { this.#doubled = 20; return this.#doubled; }
  static count() { return Counter.#instances; }
  static has(x) { return #count in x; }
  eq(other) { return this.#count === other.#count; }
}
var c = new Counter().inc().inc().add(3); new Counter();
zeige(c.value, c.secret(), c.double(), c.value, Counter.count(), Counter.total, Counter.ready, Counter.has(c), Counter.has({}), c.eq(new Counter().add(10)));
zeige(Object.keys(c).length, JSON.stringify(c), Object.getOwnPropertyNames(c).length, Object.getOwnPropertySymbols(c).length, c['#count']);
try { Counter.prototype.inc.call({}); } catch (e) { zeige('E', e.constructor.name); }
class Sub extends Counter { #own = 'sub'; own() { return this.#own + this.value; } }
zeige(new Sub().inc().own(), Counter.has(new Sub()));
class St { static a = 1; static b = this.a + 1; static f = () => this.a; static [`c${1}`] = 3; }
zeige(St.a, St.b, St.f(), St.c1);
function F() { if (!new.target) return new F(); this.made = new.target === F; } zeige(F().made, new F().made);
class Base { constructor() { this.kind = new.target.name; } } class Derived extends Base {} class D2 extends Derived { constructor() { super(); } }
zeige(new Base().kind, new Derived().kind, new D2().kind);
function nt() { return (() => new.target)(); } zeige(nt() === undefined, new nt() === undefined);
zeige(10n + 5n, BigInt(7) * 2n, BigInt('12'), 2n ** 10n);
class A2 { x = 1; y = this.x + 1; fn = () => this.y; } zeige(new A2().y, new A2().fn());
class Acc { static #n = 1; static get n() { return Acc.#n; } static set n(v) { Acc.#n = v; } } Acc.n = 5; zeige(Acc.n);
var cls = class { #p = 1; static make() { return new this(); } p() { return this.#p; } }; zeige(cls.make().p());
zeige((class { static x = 1; static { this.y = this.x + 1; } }).y);

setTimeout(function () {
    var erwartet = [
        "1000000 65535 170 10.5 10000000000 0.5 5 1.2",
        "a|b|c#a|b|c#1,2 \\n|\\t#\n|\t#x ## a\\n2A",
        "true true 2",
        "V! V?",
        "5 geheim5 20 10 2 15 true true false true",
        "0 {} 0 0 ",
        "E TypeError",
        "sub1 true",
        "1 2 1 3",
        "true true",
        "Base Derived D2",
        "true false",
        "15 14 12 1024",
        "2 2",
        "5",
        "1",
        "2"
    ];
    assertEq(zeilen.length, erwartet.length, "Anzahl der Ergebnisse");
    for (var i = 0; i < erwartet.length; i++) assertEq(zeilen[i], erwartet[i], "Ergebnis " + (i + 1));
}, 300);
