/*
 * Der in JavaScript geschriebene Teil der JavaScript-Maschine: Promise, die
 * Antreiber für async-Funktionen und allerlei eingebaute Funktionen, die sich
 * in der Sprache selbst kürzer schreiben lassen als in C. Wird beim Bauen
 * fest eingebaut (tools/embed.py) und läuft einmal, wenn eine Maschine
 * entsteht - vor jedem Skript einer Seite.
 */
(function (global) {
  'use strict';
  function hide(o, name, value) {
    Object.defineProperty(o, name, { value: value, writable: true, configurable: true });
  }
  function Promise(executor) {
    if (!(this instanceof Promise)) throw new TypeError('Promise needs new');
    if (typeof executor !== 'function') throw new TypeError('Promise needs a function');
    hide(this, '_s', 0); hide(this, '_v', undefined); hide(this, '_c', []);
    var self = this, done = false;
    function resolve(v) { if (!done) { done = true; settle(self, v); } }
    function reject(e) { if (!done) { done = true; finish(self, 2, e); } }
    try { executor(resolve, reject); } catch (e) { reject(e); }
  }
  function settle(p, v) {
    if (v === p) return finish(p, 2, new TypeError('Chaining cycle detected for promise'));
    if (v !== null && (typeof v === 'object' || typeof v === 'function')) {
      var then;
      try { then = v.then; } catch (e) { return finish(p, 2, e); }
      if (typeof then === 'function') {
        var called = false;
        queueMicrotask(function () {
          try {
            then.call(v, function (x) { if (!called) { called = true; settle(p, x); } },
                         function (e) { if (!called) { called = true; finish(p, 2, e); } });
          } catch (e) { if (!called) { called = true; finish(p, 2, e); } }
        });
        return;
      }
    }
    finish(p, 1, v);
  }
  function finish(p, state, v) {
    p._s = state; p._v = v;
    var waiting = p._c;
    p._c = [];
    for (var i = 0; i < waiting.length; i++) schedule(p, waiting[i]);
  }
  function schedule(p, h) {
    queueMicrotask(function () {
      var fn = p._s === 1 ? h.ok : h.fail, r;
      if (typeof fn !== 'function') {
        if (p._s === 1) h.resolve(p._v); else h.reject(p._v);
        return;
      }
      try { r = fn(p._v); } catch (e) { h.reject(e); return; }
      h.resolve(r);
    });
  }
  var P = Promise.prototype;
  P.then = function (ok, fail) {
    var p = this, h = { ok: ok, fail: fail };
    var next = new Promise(function (res, rej) { h.resolve = res; h.reject = rej; });
    if (p._s === 0) p._c.push(h); else schedule(p, h);
    return next;
  };
  P['catch'] = function (fail) { return this.then(undefined, fail); };
  P['finally'] = function (fn) {
    return this.then(
      function (v) { return Promise.resolve(fn()).then(function () { return v; }); },
      function (e) { return Promise.resolve(fn()).then(function () { throw e; }); });
  };
  Promise.resolve = function (v) {
    return v instanceof Promise ? v : new Promise(function (res) { res(v); });
  };
  Promise.reject = function (e) { return new Promise(function (_, rej) { rej(e); }); };
  Promise.all = function (list) {
    return new Promise(function (res, rej) {
      var items = Array.from(list), out = [], left = items.length;
      if (!left) return res(out);
      items.forEach(function (item, i) {
        Promise.resolve(item).then(function (v) { out[i] = v; if (--left === 0) res(out); }, rej);
      });
    });
  };
  Promise.allSettled = function (list) {
    return Promise.all(Array.from(list).map(function (item) {
      return Promise.resolve(item).then(
        function (v) { return { status: 'fulfilled', value: v }; },
        function (e) { return { status: 'rejected', reason: e }; });
    }));
  };
  Promise.race = function (list) {
    return new Promise(function (res, rej) {
      Array.from(list).forEach(function (item) { Promise.resolve(item).then(res, rej); });
    });
  };
  global.Promise = Promise;

  hide(String, 'raw', function (strings) {
    var raw = strings.raw, out = '', n = raw.length;
    for (var i = 0; i < n; i++) {
      out += raw[i];
      if (i + 1 < n && i + 1 < arguments.length) out += arguments[i + 1];
    }
    return out;
  });
  /* Große Ganzzahlen gibt es nur dem Namen nach: gewöhnliche Zahlen, über 2^53 ungenau. */
  function BigInt(v) {
    var n = Math.trunc(Number(v));
    if (n !== n) throw new SyntaxError('Cannot convert ' + v + ' to a BigInt');
    return n;
  }
  BigInt.asIntN = BigInt.asUintN = function (bits, n) { return n; };
  global.BigInt = BigInt;

  /* async-Funktionen: Die Maschine führt sie als Koroutinen aus; hier werden sie angetrieben. */
  var resume = global.__co.resume, register = global.__co.register;
  delete global.__co;
  register('asyncRun', function (co) {
    return new Promise(function (resolve, reject) {
      function step(mode, arg) {
        var r;
        try { r = resume(co, mode, arg); } catch (e) { reject(e); return; }
        if (r.k === 2) { resolve(r.v); return; }
        Promise.resolve(r.v).then(next, fail);
      }
      function next(v) { step(0, v); }
      function fail(e) { step(1, e); }
      step(0);
    });
  });

  /* async-Generatoren: next/throw/return stellen sich an; jeder Aufruf bekommt sein Promise. */
  function agDrain(g) {
    if (!g._q.length) { g._busy = false; return; }
    g._busy = true;
    agStep(g, g._q[0].mode, g._q[0].value);
  }
  function agStep(g, mode, value) {
    var r, q;
    try { r = resume(g._co, mode, value); }
    catch (e) { q = g._q.shift(); q.reject(e); agDrain(g); return; }
    if (r.k === 0) {
      Promise.resolve(r.v).then(function (v) { agStep(g, 0, v); }, function (e) { agStep(g, 1, e); });
      return;
    }
    q = g._q.shift();
    if (r.k === 2) { q.resolve({ value: r.v, done: true }); agDrain(g); return; }
    Promise.resolve(r.v).then(
      function (v) { q.resolve({ value: v, done: false }); agDrain(g); },
      function (e) { g._q.unshift(q); agStep(g, 1, e); });
  }
  function agEnqueue(g, mode, value) {
    return new Promise(function (resolve, reject) {
      g._q.push({ mode: mode, value: value, resolve: resolve, reject: reject });
      if (!g._busy) agDrain(g);
    });
  }
  var AG = {};
  hide(AG, 'next', function (v) { return agEnqueue(this, 0, v); });
  hide(AG, 'throw', function (e) { return agEnqueue(this, 1, e); });
  hide(AG, 'return', function (v) { return agEnqueue(this, 2, v); });
  hide(AG, Symbol.asyncIterator, function () { return this; });
  Object.defineProperty(AG, Symbol.toStringTag, { value: 'AsyncGenerator', configurable: true });
  register('AsyncGeneratorPrototype', AG);
  register('asyncGenerator', function (co, proto) {
    var g = Object.create(proto || AG);
    hide(g, '_co', co); hide(g, '_q', []); hide(g, '_busy', false);
    return g;
  });

  /* import("..."): das Ergebnis als Promise - fertig, sobald das Modul durchgelaufen ist */
  register('importResult', function (promise, ns) {
    return Promise.resolve(promise).then(function () { return ns; });
  });
  register('importFailed', function (reason) { return Promise.reject(reason); });

  /* --- Ergänzungen zu Array, Math, Object, Set ----------------------------------------------- */

  var AP = Array.prototype;
  hide(AP, 'toSorted', function (cmp) { return Array.from(this).sort(cmp); });
  hide(AP, 'toReversed', function () { return Array.from(this).reverse(); });
  hide(AP, 'toSpliced', function () { var a = Array.from(this); a.splice.apply(a, arguments); return a; });
  hide(AP, 'with', function (i, v) {
    var a = Array.from(this), n = Math.trunc(i) || 0;
    if (n < 0) n += a.length;
    if (n < 0 || n >= a.length) throw new RangeError('Invalid index : ' + i);
    a[n] = v;
    return a;
  });
  hide(AP, 'copyWithin', function (target, start, end) {
    var len = this.length;
    var to = target < 0 ? Math.max(len + target, 0) : Math.min(target || 0, len);
    var from = start < 0 ? Math.max(len + start, 0) : Math.min(start || 0, len);
    var fin = end === undefined ? len : end < 0 ? Math.max(len + end, 0) : Math.min(end, len);
    var part = AP.slice.call(this, from, fin);
    for (var i = 0; i < part.length && to + i < len; i++) this[to + i] = part[i];
    return this;
  });

  hide(Math, 'clz32', function (x) {
    x = x >>> 0;
    if (!x) return 32;
    var n = 0;
    while (!(x & 0x80000000)) { x <<= 1; n++; }
    return n;
  });
  hide(Math, 'imul', function (a, b) {
    var ah = (a >>> 16) & 0xffff, al = a & 0xffff, bh = (b >>> 16) & 0xffff, bl = b & 0xffff;
    return (al * bl + (((ah * bl + al * bh) << 16) >>> 0)) | 0;
  });
  hide(Math, 'sinh', function (x) { return (Math.exp(x) - Math.exp(-x)) / 2; });
  hide(Math, 'cosh', function (x) { return (Math.exp(x) + Math.exp(-x)) / 2; });
  hide(Math, 'tanh', function (x) {
    if (x > 20) return 1;
    if (x < -20) return -1;
    var a = Math.exp(x), b = Math.exp(-x);
    return (a - b) / (a + b);
  });
  hide(Math, 'asinh', function (x) { return x === -Infinity ? x : Math.log(x + Math.sqrt(x * x + 1)); });
  hide(Math, 'acosh', function (x) { return Math.log(x + Math.sqrt(x * x - 1)); });
  hide(Math, 'atanh', function (x) { return Math.log((1 + x) / (1 - x)) / 2; });

  hide(Object, 'hasOwn', function (o, k) { return Object.prototype.hasOwnProperty.call(Object(o), k); });
  hide(Object, 'getOwnPropertyDescriptors', function (o) {
    var out = {};
    Reflect.ownKeys(o).forEach(function (k) { out[k] = Object.getOwnPropertyDescriptor(o, k); });
    return out;
  });
  hide(Object, 'groupBy', function (items, fn) {
    var out = Object.create(null), i = 0;
    for (var item of items) { var k = fn(item, i++); (out[k] || (out[k] = [])).push(item); }
    return out;
  });
  hide(Map, 'groupBy', function (items, fn) {
    var out = new Map(), i = 0;
    for (var item of items) {
      var k = fn(item, i++), list = out.get(k);
      if (list) list.push(item); else out.set(k, [item]);
    }
    return out;
  });

  var SP = Set.prototype;
  hide(SP, 'union', function (o) { var r = new Set(this); for (var v of o.keys()) r.add(v); return r; });
  hide(SP, 'intersection', function (o) { var r = new Set(); for (var v of this) if (o.has(v)) r.add(v); return r; });
  hide(SP, 'difference', function (o) { var r = new Set(); for (var v of this) if (!o.has(v)) r.add(v); return r; });
  hide(SP, 'symmetricDifference', function (o) {
    var r = new Set(this);
    for (var v of o.keys()) { if (r.has(v)) r.delete(v); else r.add(v); }
    return r;
  });
  hide(SP, 'isSubsetOf', function (o) { for (var v of this) if (!o.has(v)) return false; return true; });
  hide(SP, 'isSupersetOf', function (o) { for (var v of o.keys()) if (!this.has(v)) return false; return true; });
  hide(SP, 'isDisjointFrom', function (o) { for (var v of this) if (o.has(v)) return false; return true; });

  /* --- Promise.any, WeakRef und Verwandte ------------------------------------------------------ */

  function AggregateError(errors, message) {
    var e = new Error(message === undefined ? '' : message);
    Object.setPrototypeOf(e, AggregateError.prototype);
    hide(e, 'errors', Array.from(errors));
    return e;
  }
  AggregateError.prototype = Object.create(Error.prototype);
  hide(AggregateError.prototype, 'constructor', AggregateError);
  hide(AggregateError.prototype, 'name', 'AggregateError');
  global.AggregateError = AggregateError;

  Promise.any = function (list) {
    return new Promise(function (res, rej) {
      var items = Array.from(list), errors = [], left = items.length;
      if (!left) return rej(new AggregateError([], 'All promises were rejected'));
      items.forEach(function (item, i) {
        Promise.resolve(item).then(res, function (e) {
          errors[i] = e;
          if (--left === 0) rej(new AggregateError(errors, 'All promises were rejected'));
        });
      });
    });
  };
  Promise.withResolvers = function () {
    var r = {};
    r.promise = new Promise(function (res, rej) { r.resolve = res; r.reject = rej; });
    return r;
  };

  /* Schwache Verweise gibt es nur dem Namen nach: Sie halten ihr Ziel fest. */
  function WeakRef(target) {
    if (!(this instanceof WeakRef)) throw new TypeError("Constructor WeakRef requires 'new'");
    if (target === null || (typeof target !== 'object' && typeof target !== 'function'))
      throw new TypeError('WeakRef: target must be an object');
    hide(this, '_t', target);
  }
  WeakRef.prototype.deref = function () { return this._t; };
  global.WeakRef = WeakRef;
  function FinalizationRegistry(fn) {
    if (!(this instanceof FinalizationRegistry)) throw new TypeError("Constructor FinalizationRegistry requires 'new'");
  }
  FinalizationRegistry.prototype.register = function () {};
  FinalizationRegistry.prototype.unregister = function () { return false; };
  global.FinalizationRegistry = FinalizationRegistry;

  hide(Error, 'captureStackTrace', function (o) {
    if (o && typeof o === 'object' && !('stack' in o)) hide(o, 'stack', '');
  });

  /* --- structuredClone, TextEncoder, TextDecoder ------------------------------------------------ */

  global.structuredClone = function (value) {
    var seen = new Map();
    function copy(v) {
      if (typeof v === 'function' || typeof v === 'symbol') throw new Error('The value could not be cloned');
      if (v === null || typeof v !== 'object') return v;
      if (seen.has(v)) return seen.get(v);
      var out, i;
      if (Array.isArray(v)) {
        out = [];
        seen.set(v, out);
        for (i = 0; i < v.length; i++) out[i] = copy(v[i]);
        return out;
      }
      if (v instanceof Date) return new Date(v.getTime());
      if (v instanceof RegExp) return new RegExp(v.source, v.flags);
      if (v instanceof Map) {
        out = new Map();
        seen.set(v, out);
        v.forEach(function (val, key) { out.set(copy(key), copy(val)); });
        return out;
      }
      if (v instanceof Set) {
        out = new Set();
        seen.set(v, out);
        v.forEach(function (val) { out.add(copy(val)); });
        return out;
      }
      if (v instanceof Error) {
        out = new Error(v.message);
        hide(out, 'name', v.name);
        seen.set(v, out);
        return out;
      }
      if (v instanceof ArrayBuffer) return v.slice(0);
      if (ArrayBuffer.isView(v) && !(v instanceof DataView)) return new v.constructor(v);
      out = {};
      seen.set(v, out);
      Object.keys(v).forEach(function (key) { out[key] = copy(v[key]); });
      return out;
    }
    return copy(value);
  };

  /* Zeichenketten sind hier ohnehin UTF-8; über escape/unescape kommt man an die einzelnen Bytes. */
  function TextEncoder() {}
  TextEncoder.prototype.encoding = 'utf-8';
  TextEncoder.prototype.encode = function (s) {
    var bin = unescape(encodeURIComponent(s === undefined ? '' : String(s))), out = new Uint8Array(bin.length);
    for (var i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
  };
  TextEncoder.prototype.encodeInto = function (s, dest) {
    var b = this.encode(s), n = Math.min(b.length, dest.length);
    for (var i = 0; i < n; i++) dest[i] = b[i];
    return { read: String(s).length, written: n };
  };
  global.TextEncoder = TextEncoder;
  function TextDecoder(label) {
    hide(this, 'encoding', String(label || 'utf-8').toLowerCase());
  }
  TextDecoder.prototype.decode = function (data) {
    if (data === undefined) return '';
    var bytes = data instanceof ArrayBuffer ? new Uint8Array(data)
              : new Uint8Array(data.buffer, data.byteOffset, data.byteLength), bin = '';
    for (var i = 0; i < bytes.length; i += 4096)
      bin += String.fromCharCode.apply(null, AP.slice.call(bytes, i, i + 4096));
    if (this.encoding !== 'utf-8' && this.encoding !== 'utf8') return bin;
    try { return decodeURIComponent(escape(bin)); } catch (e) { return bin; }
  };
  global.TextDecoder = TextDecoder;

  /* --- Intl: Zahlen, Daten und Listen nach Landesart - für Deutsch und Englisch ----------------- */

  var MONTHS = {
    de: ['Januar', 'Februar', 'März', 'April', 'Mai', 'Juni', 'Juli', 'August', 'September', 'Oktober', 'November', 'Dezember'],
    en: ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December']
  };
  var MONTHS_SHORT = {
    de: ['Jan.', 'Feb.', 'März', 'Apr.', 'Mai', 'Juni', 'Juli', 'Aug.', 'Sept.', 'Okt.', 'Nov.', 'Dez.'],
    en: ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec']
  };
  var DAYS = {
    de: ['Sonntag', 'Montag', 'Dienstag', 'Mittwoch', 'Donnerstag', 'Freitag', 'Samstag'],
    en: ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday']
  };
  var DAYS_SHORT = {
    de: ['So.', 'Mo.', 'Di.', 'Mi.', 'Do.', 'Fr.', 'Sa.'],
    en: ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat']
  };
  function localeName(locale) {
    var l = Array.isArray(locale) ? locale[0] : locale;
    return l ? String(l) : 'de-DE';
  }
  function lang(locale) { return localeName(locale).slice(0, 2).toLowerCase() === 'de' ? 'de' : 'en'; }
  function supported(locales) { return Array.isArray(locales) ? locales.slice() : locales ? [String(locales)] : []; }
  function pad2(n) { return (n < 10 ? '0' : '') + n; }

  function formatNumber(n, l, o) {
    n = Number(n);
    if (n !== n) return 'NaN';
    var style = o.style || 'decimal', min = o.minimumFractionDigits, max = o.maximumFractionDigits;
    if (style === 'currency') { if (min === undefined) min = 2; if (max === undefined) max = Math.max(2, min); }
    if (style === 'percent') { n *= 100; if (max === undefined) max = Math.max(0, min || 0); }
    if (min === undefined) min = 0;
    if (max === undefined) max = Math.max(3, min);
    var negative = n < 0, out;
    n = Math.abs(n);
    if (n === Infinity) {
      out = '∞';
    } else {
      var parts = n.toFixed(Math.min(max, 20)).split('.'), whole = parts[0], frac = parts[1] || '';
      while (frac.length > min && frac.charAt(frac.length - 1) === '0') frac = frac.slice(0, -1);
      if (o.useGrouping !== false) whole = whole.replace(/\B(?=(\d{3})+(?!\d))/g, l === 'de' ? '.' : ',');
      out = whole + (frac ? (l === 'de' ? ',' : '.') + frac : '');
    }
    if (style === 'percent') out += l === 'de' ? ' %' : '%';
    if (style === 'currency') {
      var code = String(o.currency || 'EUR').toUpperCase();
      var sign = o.currencyDisplay === 'code' ? code : ({ EUR: '€', USD: '$', GBP: '£', JPY: '¥' })[code] || code;
      out = l === 'de' ? out + ' ' + sign : sign + out;
    }
    if (style === 'unit' && o.unit) out += ' ' + o.unit;
    return (negative ? '-' : '') + out;
  }
  function NumberFormat(locale, options) {
    if (!(this instanceof NumberFormat)) return new NumberFormat(locale, options);
    hide(this, '_l', localeName(locale));
    hide(this, '_o', options || {});
    var self = this;
    hide(this, 'format', function (n) { return formatNumber(n, lang(self._l), self._o); });
  }
  NumberFormat.prototype.formatToParts = function (n) { return [{ type: 'integer', value: this.format(n) }]; };
  NumberFormat.prototype.resolvedOptions = function () {
    return { locale: this._l, numberingSystem: 'latn', style: this._o.style || 'decimal',
             minimumFractionDigits: this._o.minimumFractionDigits || 0,
             maximumFractionDigits: this._o.maximumFractionDigits === undefined ? 3 : this._o.maximumFractionDigits };
  };
  NumberFormat.supportedLocalesOf = supported;

  /* Die Teile eines Datums, wie die Angaben in o sie verlangen: [{ type, value }] */
  function dateParts(value, l, o, what) {
    var d = value === undefined ? new Date() : new Date(value);
    if (isNaN(d.getTime())) throw new RangeError('Invalid time value');
    var utc = o.timeZone === 'UTC';
    var Y = utc ? d.getUTCFullYear() : d.getFullYear(), M = utc ? d.getUTCMonth() : d.getMonth();
    var D = utc ? d.getUTCDate() : d.getDate(), W = utc ? d.getUTCDay() : d.getDay();
    var h = utc ? d.getUTCHours() : d.getHours(), mi = utc ? d.getUTCMinutes() : d.getMinutes();
    var s = utc ? d.getUTCSeconds() : d.getSeconds();
    var want = {}, k;
    for (k in o) want[k] = o[k];
    if (o.dateStyle) {
      want.year = o.dateStyle === 'short' ? '2-digit' : 'numeric';
      want.day = l === 'de' && (o.dateStyle === 'short' || o.dateStyle === 'medium') ? '2-digit' : 'numeric';
      want.month = o.dateStyle === 'short' ? (l === 'de' ? '2-digit' : 'numeric')
                 : o.dateStyle === 'medium' ? (l === 'de' ? '2-digit' : 'short') : 'long';
      if (o.dateStyle === 'full') want.weekday = 'long';
    }
    if (o.timeStyle) {
      want.hour = l === 'de' ? '2-digit' : 'numeric';
      want.minute = '2-digit';
      if (o.timeStyle !== 'short') want.second = '2-digit';
    }
    var anyDate = want.weekday || want.year || want.month || want.day;
    var anyTime = want.hour || want.minute || want.second;
    if (!anyDate && !anyTime) {
      if (what !== 'time') { want.year = want.month = want.day = 'numeric'; anyDate = true; }
      if (what !== 'date') { want.hour = l === 'de' ? '2-digit' : 'numeric'; want.minute = want.second = '2-digit'; anyTime = true; }
    }
    var parts = [];
    function put(type, text) { parts.push({ type: type, value: String(text) }); }
    function lit(text) { if (parts.length) put('literal', text); }
    var textual = want.month === 'long' || want.month === 'short' || want.month === 'narrow';
    var year = want.year === '2-digit' ? pad2(Y % 100) : Y;
    var day = want.day === '2-digit' ? pad2(D) : D;
    var month = want.month === 'long' ? MONTHS[l][M] : want.month === 'short' ? MONTHS_SHORT[l][M]
              : want.month === 'narrow' ? MONTHS[l][M].charAt(0) : want.month === '2-digit' ? pad2(M + 1) : M + 1;
    if (want.weekday) put('weekday', want.weekday === 'long' ? DAYS[l][W] : want.weekday === 'narrow' ? DAYS[l][W].charAt(0) : DAYS_SHORT[l][W]);
    if (l === 'de') {
      if (want.day) { lit(', '); put('day', day); if (textual || want.month) put('literal', textual ? '. ' : '.'); }
      else if (want.month || want.year) lit(', ');
      if (want.month) { put('month', month); if (want.year) put('literal', textual ? ' ' : '.'); }
      if (want.year) put('year', year);
    } else if (textual) {
      if (want.month) { lit(', '); put('month', month); }
      if (want.day) { lit(' '); put('day', day); }
      if (want.year) { lit(want.day ? ', ' : ' '); put('year', year); }
    } else {
      if (want.month) { lit(', '); put('month', month); }
      if (want.day) { lit('/'); put('day', day); }
      if (want.year) { lit(want.month || want.day ? '/' : ', '); put('year', year); }
    }
    if (anyTime) {
      var twelve = o.hour12 === undefined ? (l !== 'de' && o.hourCycle !== 'h23' && o.hourCycle !== 'h24') : !!o.hour12;
      var hour = twelve ? (h % 12 || 12) : h;
      lit(', ');
      if (want.hour) put('hour', want.hour === '2-digit' || (l === 'de' && want.minute) ? pad2(hour) : hour);
      if (want.minute) { if (want.hour) put('literal', ':'); put('minute', want.hour ? pad2(mi) : mi); }
      if (want.second) { if (want.hour || want.minute) put('literal', ':'); put('second', want.hour || want.minute ? pad2(s) : s); }
      if (twelve && want.hour) { put('literal', ' '); put('dayPeriod', h < 12 ? 'AM' : 'PM'); }
      else if (l === 'de' && want.hour && !want.minute) { put('literal', ' '); put('literal', 'Uhr'); }
    }
    return parts;
  }
  function formatDate(value, locale, options, what) {
    return dateParts(value, lang(locale), options || {}, what).map(function (p) { return p.value; }).join('');
  }
  function DateTimeFormat(locale, options) {
    if (!(this instanceof DateTimeFormat)) return new DateTimeFormat(locale, options);
    hide(this, '_l', localeName(locale));
    hide(this, '_o', options || {});
    var self = this;
    hide(this, 'format', function (d) { return formatDate(d, self._l, self._o, 'date'); });
  }
  DateTimeFormat.prototype.formatToParts = function (d) { return dateParts(d, lang(this._l), this._o, 'date'); };
  DateTimeFormat.prototype.formatRange = function (a, b) { return this.format(a) + ' – ' + this.format(b); };
  DateTimeFormat.prototype.resolvedOptions = function () {
    var off = -new Date().getTimezoneOffset() / 60;
    return { locale: this._l, calendar: 'gregory', numberingSystem: 'latn',
             timeZone: this._o.timeZone || (off === 0 ? 'UTC' : 'Etc/GMT' + (off > 0 ? '-' : '+') + Math.abs(off)) };
  };
  DateTimeFormat.supportedLocalesOf = supported;

  /*
   * Sortieren nach dem Wörterbuch: erst nach den Grundbuchstaben (ä wie a, é
   * wie e, ß wie ss), bei Gleichstand ohne Akzent vor mit Akzent, zuletzt
   * klein vor groß.
   */
  var BASE_LATIN1 = 'AAAAAAACEEEEIIIIDNOOOOO\u00D7OUUUUYTsaaaaaaaceeeeiiiidnooooo\u00F7ouuuuyty';
  var BASE_LATIN_A = 'AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlL' +
                     'lLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs';
  function baseLetters(s) {
    var out = '';
    for (var i = 0; i < s.length; i++) {
      var c = s.charCodeAt(i);
      out += c < 0xC0 ? s.charAt(i) : c === 0xDF ? 'ss' : c <= 0xFF ? BASE_LATIN1.charAt(c - 0xC0)
           : c >= 0x100 && c <= 0x17F ? BASE_LATIN_A.charAt(c - 0x100) : s.charAt(i);
    }
    return out;
  }
  function order(a, b) { return a < b ? -1 : a > b ? 1 : 0; }
  function collate(a, b, level) {
    var c = order(baseLetters(a).toLowerCase(), baseLetters(b).toLowerCase());
    if (c || level === 1) return c;
    c = order(a.toLowerCase(), b.toLowerCase());
    if (c || level === 2) return c;
    for (var i = 0; i < a.length && i < b.length; i++) {
      var x = a.charAt(i), y = b.charAt(i);
      if (x !== y) return x === x.toLowerCase() ? -1 : 1;
    }
    return order(a.length, b.length);
  }
  function Collator(locale, options) {
    if (!(this instanceof Collator)) return new Collator(locale, options);
    var o = options || {};
    var level = o.sensitivity === 'base' ? 1 : o.sensitivity === 'accent' ? 2 : 3;
    hide(this, '_l', localeName(locale));
    hide(this, 'compare', function (a, b) {
      a = String(a); b = String(b);
      if (o.numeric) {
        var x = a.match(/\d+|\D+/g) || [], y = b.match(/\d+|\D+/g) || [];
        for (var i = 0; i < x.length && i < y.length; i++) {
          var p = x[i], q = y[i], c = /^\d/.test(p) && /^\d/.test(q) ? Number(p) - Number(q) : collate(p, q, level);
          if (c) return c < 0 ? -1 : 1;
        }
        return x.length === y.length ? 0 : x.length < y.length ? -1 : 1;
      }
      return collate(a, b, level);
    });
  }
  hide(String.prototype, 'localeCompare', function (other, locale, options) {
    var o = options || {};
    return collate(String(this), String(other), o.sensitivity === 'base' ? 1 : o.sensitivity === 'accent' ? 2 : 3);
  });
  Collator.prototype.resolvedOptions = function () { return { locale: this._l, usage: 'sort', sensitivity: 'variant' }; };
  Collator.supportedLocalesOf = supported;

  function PluralRules(locale, options) {
    if (!(this instanceof PluralRules)) throw new TypeError("Constructor Intl.PluralRules requires 'new'");
    hide(this, '_l', localeName(locale));
    hide(this, '_ordinal', !!options && options.type === 'ordinal');
  }
  PluralRules.prototype.select = function (n) {
    n = Number(n);
    if (this._ordinal) {
      if (lang(this._l) === 'de') return 'other';
      var one = n % 10, ten = n % 100;
      return one === 1 && ten !== 11 ? 'one' : one === 2 && ten !== 12 ? 'two' : one === 3 && ten !== 13 ? 'few' : 'other';
    }
    return n === 1 ? 'one' : 'other';
  };
  PluralRules.prototype.resolvedOptions = function () { return { locale: this._l, type: this._ordinal ? 'ordinal' : 'cardinal' }; };
  PluralRules.supportedLocalesOf = supported;

  var UNITS = {
    de: { second: ['Sekunde', 'Sekunden'], minute: ['Minute', 'Minuten'], hour: ['Stunde', 'Stunden'], day: ['Tag', 'Tagen'],
          week: ['Woche', 'Wochen'], month: ['Monat', 'Monaten'], quarter: ['Quartal', 'Quartalen'], year: ['Jahr', 'Jahren'] },
    en: { second: ['second', 'seconds'], minute: ['minute', 'minutes'], hour: ['hour', 'hours'], day: ['day', 'days'],
          week: ['week', 'weeks'], month: ['month', 'months'], quarter: ['quarter', 'quarters'], year: ['year', 'years'] }
  };
  function RelativeTimeFormat(locale, options) {
    if (!(this instanceof RelativeTimeFormat)) throw new TypeError("Constructor Intl.RelativeTimeFormat requires 'new'");
    hide(this, '_l', localeName(locale));
    hide(this, '_auto', !!options && options.numeric === 'auto');
  }
  RelativeTimeFormat.prototype.format = function (value, unit) {
    var l = lang(this._l), n = Number(value), names = UNITS[l][String(unit).replace(/s$/, '')];
    if (!names) throw new RangeError('Invalid unit argument for format() ' + unit);
    if (this._auto && String(unit).replace(/s$/, '') === 'day' && Math.abs(n) <= 1)
      return l === 'de' ? (n < 0 ? 'gestern' : n > 0 ? 'morgen' : 'heute') : (n < 0 ? 'yesterday' : n > 0 ? 'tomorrow' : 'today');
    var amount = formatNumber(Math.abs(n), l, {}) + ' ' + names[Math.abs(n) === 1 ? 0 : 1];
    if (l === 'de') return n < 0 || Object.is(n, -0) ? 'vor ' + amount : 'in ' + amount;
    return n < 0 || Object.is(n, -0) ? amount + ' ago' : 'in ' + amount;
  };
  RelativeTimeFormat.supportedLocalesOf = supported;

  function ListFormat(locale, options) {
    if (!(this instanceof ListFormat)) throw new TypeError("Constructor Intl.ListFormat requires 'new'");
    hide(this, '_l', localeName(locale));
    hide(this, '_or', !!options && options.type === 'disjunction');
  }
  ListFormat.prototype.format = function (list) {
    var items = Array.from(list, String), l = lang(this._l);
    var word = l === 'de' ? (this._or ? 'oder' : 'und') : (this._or ? 'or' : 'and');
    if (items.length < 2) return items.join('');
    if (items.length === 2) return items[0] + ' ' + word + ' ' + items[1];
    return items.slice(0, -1).join(', ') + (l === 'de' ? ' ' : ', ') + word + ' ' + items[items.length - 1];
  };
  ListFormat.supportedLocalesOf = supported;

  function DisplayNames(locale, options) {
    if (!(this instanceof DisplayNames)) throw new TypeError("Constructor Intl.DisplayNames requires 'new'");
  }
  DisplayNames.prototype.of = function (code) { return String(code); };

  function Segmenter(locale, options) {
    if (!(this instanceof Segmenter)) throw new TypeError("Constructor Intl.Segmenter requires 'new'");
    hide(this, '_words', !!options && options.granularity === 'word');
  }
  Segmenter.prototype.segment = function (text) {
    var s = String(text), out = [], at = 0;
    var pieces = this._words ? s.match(/[\p{L}\p{N}_]+|[^\p{L}\p{N}_]/gu) || [] : Array.from(s);
    for (var i = 0; i < pieces.length; i++) {
      var item = { segment: pieces[i], index: at, input: s };
      if (this._words) item.isWordLike = /[\p{L}\p{N}]/u.test(pieces[i]);
      out.push(item);
      at += pieces[i].length;
    }
    return out;
  };

  var Intl = {
    NumberFormat: NumberFormat, DateTimeFormat: DateTimeFormat, Collator: Collator, PluralRules: PluralRules,
    RelativeTimeFormat: RelativeTimeFormat, ListFormat: ListFormat, DisplayNames: DisplayNames, Segmenter: Segmenter,
    getCanonicalLocales: supported,
    supportedValuesOf: function () { return []; }
  };
  Object.defineProperty(Intl, Symbol.toStringTag, { value: 'Intl', configurable: true });
  global.Intl = Intl;

  hide(Number.prototype, 'toLocaleString', function (locale, options) {
    return formatNumber(Number(this), lang(locale), options || {});
  });
  hide(Date.prototype, 'toLocaleDateString', function (locale, options) {
    return isNaN(this.getTime()) ? 'Invalid Date' : formatDate(this, locale, options, 'date');
  });
  hide(Date.prototype, 'toLocaleTimeString', function (locale, options) {
    return isNaN(this.getTime()) ? 'Invalid Date' : formatDate(this, locale, options, 'time');
  });
  hide(Date.prototype, 'toLocaleString', function (locale, options) {
    return isNaN(this.getTime()) ? 'Invalid Date' : formatDate(this, locale, options, 'both');
  });
})(this);
