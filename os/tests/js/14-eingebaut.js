// Ergänzte eingebaute Funktionen: Array, Math, Object, Set, Promise.any, structuredClone, TextEncoder, Intl
var zeilen = [];
function zeige() { zeilen.push(Array.prototype.slice.call(arguments).join(' ')); }
zeige([3, 1, 2].toSorted().join(), [1, 2, 3].toReversed().join(), [1, 2, 3].toSpliced(1, 1, 'x').join(), [1, 2, 3].with(-1, 9).join(), [1, 2, 3, 4, 5].copyWithin(0, 3).join());
zeige(Math.clz32(1), Math.imul(0xffffffff, 5), Math.sinh(0), Math.cosh(0), Math.tanh(100), Math.asinh(0), Math.round(Math.atanh(0.5) * 1000));
zeige(Object.hasOwn({ a: 1 }, 'a'), Object.hasOwn({}, 'toString'), JSON.stringify(Object.getOwnPropertyDescriptors({ a: 1 })), JSON.stringify(Object.groupBy([1, 2, 3, 4], function (x) { return x % 2 ? 'u' : 'g'; })), Map.groupBy([1, 2, 3], function (x) { return x > 1; }).get(true).join());
var A = new Set([1, 2, 3]), B = new Set([2, 3, 4]);
zeige([...A.union(B)].join(), [...A.intersection(B)].join(), [...A.difference(B)].join(), [...A.symmetricDifference(B)].join(), A.isSubsetOf(B), new Set([2]).isSubsetOf(A), A.isSupersetOf(new Set([1])), A.isDisjointFrom(new Set([9])));
Promise.any([Promise.reject(1), Promise.resolve(2)]).then(function (v) { zeige('any', v); });
Promise.any([Promise.reject(new Error('a'))]).catch(function (e) { zeige('any-fehler', e instanceof AggregateError, e instanceof Error, e.name, e.errors.length); });
var wr = Promise.withResolvers(); wr.promise.then(function (v) { zeige('withResolvers', v); }); wr.resolve('ok');
var ob = { d: new Date(5), m: new Map([[1, { x: 1 }]]), s: new Set([1]), a: [1, [2]], r: /x/gi, n: null }; ob.self = ob;
var cl = structuredClone(ob);
zeige(cl !== ob, cl.self === cl, cl.d.getTime(), cl.m.get(1).x, cl.m.get(1) !== ob.m.get(1), cl.s.has(1), cl.a[1][0], cl.r.flags, cl.n);
try { structuredClone(function () {}); } catch (e) { zeige('E clone'); }
var enc = new TextEncoder().encode('aä€😀'); zeige(enc.length, Array.from(enc).join(), new TextDecoder().decode(enc), new TextDecoder('utf-8').decode(new Uint8Array([72, 105]).buffer));
zeige(typeof WeakRef, new WeakRef(ob).deref() === ob, typeof FinalizationRegistry, typeof new FinalizationRegistry(function () {}).register);
zeige(new Intl.NumberFormat('de-DE').format(1234567.891), new Intl.NumberFormat('en-US').format(1234567.891), new Intl.NumberFormat('de-DE', { style: 'currency', currency: 'EUR' }).format(1234.5), new Intl.NumberFormat('en-US', { style: 'currency', currency: 'USD' }).format(-1234.5));
zeige(new Intl.NumberFormat('de-DE', { style: 'percent' }).format(0.256), new Intl.NumberFormat('en-US', { minimumFractionDigits: 2 }).format(5), new Intl.NumberFormat('de-DE', { maximumFractionDigits: 0 }).format(2.5), (1234.5).toLocaleString('en-US'), (0.5).toLocaleString('de-DE', { style: 'percent' }));
var d = new Date(Date.UTC(2026, 10, 19, 14, 5, 9));
var tz = { timeZone: 'UTC' };
zeige(new Intl.DateTimeFormat('de-DE', tz).format(d), '|', new Intl.DateTimeFormat('en-US', tz).format(d), '|', d.toLocaleDateString('de-DE', { weekday: 'long', year: 'numeric', month: 'long', day: 'numeric', timeZone: 'UTC' }), '|', d.toLocaleDateString('en-US', { weekday: 'long', year: 'numeric', month: 'long', day: 'numeric', timeZone: 'UTC' }));
zeige(d.toLocaleTimeString('de-DE', tz), '|', d.toLocaleTimeString('en-US', tz), '|', d.toLocaleString('de-DE', tz), '|', d.toLocaleString('en-US', tz));
zeige(d.toLocaleDateString('de-DE', { month: 'long', year: 'numeric', timeZone: 'UTC' }), '|', d.toLocaleDateString('en-US', { month: 'short', day: 'numeric', timeZone: 'UTC' }), '|', d.toLocaleDateString('de-DE', { day: '2-digit', month: '2-digit', year: 'numeric', timeZone: 'UTC' }), '|', d.toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit', timeZone: 'UTC' }), '|', d.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit', timeZone: 'UTC' }));
zeige(d.toLocaleDateString('de-DE', { dateStyle: 'full', timeZone: 'UTC' }), '|', d.toLocaleDateString('en-US', { dateStyle: 'medium', timeZone: 'UTC' }), '|', d.toLocaleString('de-DE', { dateStyle: 'short', timeStyle: 'short', timeZone: 'UTC' }), '|', d.toLocaleDateString('de-DE', { month: 'short', day: 'numeric', timeZone: 'UTC' }));
zeige(new Intl.DateTimeFormat('de-DE', tz).formatToParts(d).map(function (p) { return p.type; }).join(), typeof new Intl.DateTimeFormat().resolvedOptions().timeZone, new Intl.DateTimeFormat('en-GB').resolvedOptions().locale);
zeige(['b', 'a', 'ä'].sort(new Intl.Collator('de').compare).join(), ['a10', 'a9', 'a1'].sort(new Intl.Collator(undefined, { numeric: true }).compare).join(), new Intl.PluralRules('de').select(1), new Intl.PluralRules('en').select(2), new Intl.PluralRules('en', { type: 'ordinal' }).select(22));
zeige(new Intl.RelativeTimeFormat('de').format(-3, 'day'), '|', new Intl.RelativeTimeFormat('en').format(2, 'hours'), '|', new Intl.RelativeTimeFormat('de', { numeric: 'auto' }).format(-1, 'day'), '|', new Intl.ListFormat('de', { type: 'conjunction' }).format(['a', 'b', 'c']), '|', new Intl.ListFormat('en', { type: 'disjunction' }).format(['a', 'b', 'c']));
zeige(Object.prototype.toString.call(Intl), typeof Intl.DateTimeFormat.supportedLocalesOf, Intl.NumberFormat.supportedLocalesOf(['de-DE']).join(), Array.from(new Intl.Segmenter('de', { granularity: 'word' }).segment('Hallo Welt!')).filter(function (x) { return x.isWordLike; }).map(function (x) { return x.segment; }).join());
var e2 = {}; Error.captureStackTrace(e2); zeige(typeof e2.stack, String.raw`a\b`);
zeige(['Zebra', 'apfel', 'Äpfel', 'Apfel', 'über', 'Ufer', 'straße', 'strasse', 'Öl', 'ob'].sort(function (a, b) { return a.localeCompare(b); }).join(), 'a'.localeCompare('B'), 'a'.localeCompare('A'), 'ä'.localeCompare('a', 'de', { sensitivity: 'base' }), 'x'.localeCompare('x'));
zeige(['Łódź', 'Lodz', 'İzmir', 'Izmir', 'Ĳssel', 'Zürich', 'Žilina', 'Šibenik', 'Sibiu'].sort(function (a, b) { return a.localeCompare(b, 'de', { sensitivity: 'base' }) || (a < b ? -1 : 1); }).join());

setTimeout(function () {
    var erwartet = [
        "1,2,3 3,2,1 1,x,3 1,2,9 4,5,3,4,5",
        "31 -5 0 1 1 0 549",
        "true false {\"a\":{\"value\":1,\"writable\":true,\"enumerable\":true,\"configurable\":true}} {\"u\":[1,3],\"g\":[2,4]} 2,3",
        "1,2,3,4 2,3 1 1,4 false true true true",
        "true true 5 1 true true 2 gi ",
        "E clone",
        "10 97,195,164,226,130,172,240,159,152,128 aä€😀 Hi",
        "function true function function",
        "1.234.567,891 1,234,567.891 1.234,50 € -$1,234.50",
        "26 % 5.00 3 1,234.5 50 %",
        "19.11.2026 | 11/19/2026 | Donnerstag, 19. November 2026 | Thursday, November 19, 2026",
        "14:05:09 | 2:05:09 PM | 19.11.2026, 14:05:09 | 11/19/2026, 2:05:09 PM",
        "November 2026 | Nov 19 | 19.11.2026 | 14:05 | 02:05 PM",
        "Donnerstag, 19. November 2026 | Nov 19, 2026 | 19.11.26, 14:05 | 19. Nov.",
        "day,literal,month,literal,year string en-GB",
        "a,ä,b a1,a9,a10 one other two",
        "vor 3 Tagen | in 2 hours | gestern | a, b und c | a, b, or c",
        "[object Intl] function de-DE Hallo,Welt",
        "string a\\b",
        "apfel,Apfel,Äpfel,ob,Öl,strasse,straße,über,Ufer,Zebra -1 -1 0 0",
        "Ĳssel,Izmir,İzmir,Lodz,Łódź,Šibenik,Sibiu,Žilina,Zürich",
        "withResolvers ok",
        "any 2",
        "any-fehler true true AggregateError 1"
    ];
    zeilen.sort(); erwartet.sort();        // was auf Zeitgeber wartet, kommt in wechselnder Reihenfolge
    assertEq(zeilen.length, erwartet.length, "Anzahl der Ergebnisse");
    for (var i = 0; i < erwartet.length; i++) assertEq(zeilen[i], erwartet[i], "Ergebnis " + (i + 1));
}, 300);
