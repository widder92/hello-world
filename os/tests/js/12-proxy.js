// Proxy und Reflect
var zeilen = [];
function zeige() { zeilen.push(Array.prototype.slice.call(arguments).join(' ')); }
var log = [];
var target = { a: 1, b: 2 };
var p = new Proxy(target, {
  get(t, k, r) { log.push('get ' + String(k)); return k in t ? t[k] : 'kein ' + String(k); },
  set(t, k, v, r) { log.push('set ' + k + '=' + v); t[k] = v * 2; return true; },
  has(t, k) { log.push('has ' + k); return k === 'magie' || k in t; },
  deleteProperty(t, k) { log.push('del ' + k); delete t[k]; return true; },
  ownKeys(t) { log.push('keys'); return Reflect.ownKeys(t).concat(['extra']); },
  getOwnPropertyDescriptor(t, k) { return k === 'extra' ? { value: 'x', enumerable: true, configurable: true } : Reflect.getOwnPropertyDescriptor(t, k); }
});
zeige(p.a, p.zzz, (p.c = 5, target.c), 'magie' in p, 'nix' in p, delete p.a, target.a, Object.keys(p).join(), JSON.stringify(p));
zeige(log.join(';')); log = [];
var plain = new Proxy({ x: 1, y: { z: 2 } }, {});
zeige(plain.x, plain.y.z, (plain.w = 3, plain.w), Object.keys(plain).join(), 'x' in plain, delete plain.x, JSON.stringify(plain), typeof plain);
for (var k in plain) log.push(k); zeige(log.join()); log = [];
var fn = new Proxy(function (a, b) { return a + b; }, { apply(t, self, args) { return t.apply(self, args) * 10; }, construct(t, args) { return { made: args.length }; } });
zeige(typeof fn, fn(1, 2), new fn(1, 2, 3).made, fn.call(null, 2, 2), fn.length);
var fp = new Proxy(function (a) { this.a = a; }, {}); zeige(new fp(7).a, typeof fp);
var arr = new Proxy([1, 2, 3], { set(t, k, v) { log.push(k + '=' + v); t[k] = v; return true; } });
arr.push(4); zeige(arr.length, Array.isArray(arr), arr.map(function (x) { return x * 2; }).join(), [...arr].join(), JSON.stringify(arr), log.join(';'));
log = []; arr.length = 2; zeige(arr.join(), arr.indexOf(2), arr.includes(9), log.join());
// reaktiv nach Art von Vue
var deps = new Map(), active = null;
function track(t, k) { if (active) { var m = deps.get(t) || deps.set(t, new Map()).get(t); (m.get(k) || m.set(k, new Set()).get(k)).add(active); } }
function trigger(t, k) { var m = deps.get(t); if (m && m.get(k)) m.get(k).forEach(function (f) { f(); }); }
var cache = new WeakMap();
function reactive(obj) {
  if (cache.has(obj)) return cache.get(obj);
  var px = new Proxy(obj, {
    get(t, k, r) { track(t, k); var v = Reflect.get(t, k, r); return v && typeof v === 'object' ? reactive(v) : v; },
    set(t, k, v, r) { var old = t[k]; var ok = Reflect.set(t, k, v, r); if (old !== v) trigger(t, k); return ok; }
  });
  cache.set(obj, px); return px;
}
function effect(f) { active = f; f(); active = null; }
var state = reactive({ count: 1, user: { name: 'Anna' }, list: [1, 2], get doppelt() { return this.count * 2; } });
var seen = [];
effect(function () { seen.push(state.count + ':' + state.user.name + ':' + state.doppelt); });
state.count++; state.user.name = 'Ben'; state.count = 2; zeige(seen.join(' | '), state.user === state.user, state.list.length);
// Reflect
var ro = { a: 1 };
zeige(Reflect.has(ro, 'a'), Reflect.get(ro, 'a'), Reflect.set(ro, 'b', 2), ro.b, Reflect.ownKeys(ro).join(), Reflect.deleteProperty(ro, 'a'), 'a' in ro);
zeige(Reflect.apply(Math.max, null, [1, 5, 3]), Reflect.construct(Date, [0]).getTime(), Reflect.getPrototypeOf([]) === Array.prototype, Reflect.defineProperty(ro, 'c', { value: 9 }), ro.c);
class RA { constructor(x) { this.x = x; this.nt = new.target.name; } } class RB { }
var made = Reflect.construct(RA, [5], RB); zeige(made.x, made instanceof RB, made instanceof RA);
zeige(Reflect.getOwnPropertyDescriptor(ro, 'b').value, Reflect.isExtensible(ro), Reflect.preventExtensions(ro), Reflect.isExtensible(ro), Reflect.set(Object.freeze({ q: 1 }), 'q', 2));
var getter = { get v() { return this.w; } }; zeige(Reflect.get(getter, 'v', { w: 'empfaenger' }));
var rv = Proxy.revocable({ a: 1 }, {}); zeige(rv.proxy.a); rv.revoke(); try { rv.proxy.a; } catch (e) { zeige('E', e.constructor.name); }
try { new Proxy(1, {}); } catch (e) { zeige('E', e.constructor.name); }
var proto = new Proxy({}, { get(t, k) { return 'aus proxy: ' + String(k); } }); var child = Object.create(proto); child.own = 1; zeige(child.own, child.other);
class Model { constructor() { return new Proxy(this, { get(t, k) { return k in t ? t[k] : 'dyn-' + k; } }); } hello() { return 'hi'; } }
var md = new Model(); zeige(md.hello(), md.foo, md instanceof Model);
zeige(Object.getPrototypeOf(new Proxy([], {})) === Array.prototype, Object.prototype.hasOwnProperty.call(new Proxy({ k: 1 }, {}), 'k'), Object.entries(new Proxy({ k: 1 }, {})).join());
var hp = new Proxy({}, { getPrototypeOf() { return Array.prototype; }, defineProperty(t, k, d) { t[k] = 'def:' + d.value; return true; } });
zeige(Object.getPrototypeOf(hp) === Array.prototype, (Object.defineProperty(hp, 'n', { value: 4 }), hp.n));
zeige(Object.assign({}, new Proxy({ a: 1, b: 2 }, {})).b, { ...new Proxy({ s: 1 }, {}) }.s, new Map([[plain, 1]]).get(plain));

setTimeout(function () {
    var erwartet = [
        "1 kein zzz 10 true false true  b,c,extra {\"b\":2,\"c\":10,\"extra\":\"kein extra\"}",
        "get a;get zzz;set c=5;has magie;has nix;del a;keys;get toJSON;keys;get b;get c;get extra",
        "1 2 3 x,y,w true true {\"y\":{\"z\":2},\"w\":3} object",
        "y,w",
        "function 30 3 40 2",
        "7 function",
        "4 true 2,4,6,8 1,2,3,4 [1,2,3,4] 3=4;length=4",
        "1,2 1 false length=2",
        "1:Anna:2 | 2:Anna:4 | 2:Ben:4 true 2",
        "true 1 true 2 a,b true false",
        "5 0 true true 9",
        "5 true false",
        "2 true true false false",
        "empfaenger",
        "1",
        "E TypeError",
        "E TypeError",
        "1 aus proxy: other",
        "hi dyn-foo true",
        "true true k,1",
        "true def:4",
        "2 1 1"
    ];
    assertEq(zeilen.length, erwartet.length, "Anzahl der Ergebnisse");
    for (var i = 0; i < erwartet.length; i++) assertEq(zeilen[i], erwartet[i], "Ergebnis " + (i + 1));
}, 300);
