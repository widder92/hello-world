// Reguläre Ausdrücke: benannte Gruppen, Rückwärtsblick, \\p{...}
var zeilen = [];
function zeige() { zeilen.push(Array.prototype.slice.call(arguments).join(' ')); }
var m = /(?<jahr>\d{4})-(?<monat>\d{2})(?:-(?<tag>\d{2}))?/.exec('am 2026-11 und');
zeige(m.groups.jahr, m.groups.monat, m.groups.tag, m.index, Object.keys(m.groups).join(), m.length);
zeige('2026-11-19'.replace(/(?<j>\d+)-(?<m>\d+)-(?<t>\d+)/, '$<t>.$<m>.$<j>'), 'aXbX'.replace(/(?<x>X)/g, '[$<x>]'), 'a-b'.replace(/(a)-(b)/, '$2-$1$<nix>'));
zeige(/(?<w>a)\k<w>/.test('aa'), /(?<w>a)\k<w>/.test('ab'), /(a)(?<n>b)\2/.exec('abb')[0], /x/.exec('x').groups);
zeige(/(?<=\$)\d+/.exec('kostet $42 oder €17')[0], /(?<!\$)\b\d+/.exec('kostet $42 oder 17')[0], 'a1b2'.replace(/(?<=[a-z])\d/g, '#'), /(?<=ab+)c/.test('abbbc'), /(?<=^|,)x/.test(',x'));
zeige('price: 100 dollars'.match(/(?<=price: )\d+/)[0], /(?<!foo)bar/.test('foobar'), /(?<!foo)bar/.test('bazbar'), /(?<=(\d)(\d))x/.exec('12x').slice(1).join());
zeige(/\p{L}+/u.exec('  Größe123')[0], /\p{Lu}/u.test('ä'), /\p{Lu}/u.test('Ä'), /[\p{L}\d_]+/u.exec('--añb_9!')[0], /\P{L}+/u.exec('ab12cd')[0], /\p{N}+/u.exec('x٣٤y')[0].length);
zeige(/\p{Script=Greek}+/u.exec('abc αβγ')[0], /\p{sc=Cyrillic}/u.test('д'), /\p{Emoji}/u.test('x😀'), /^\p{P}+$/u.test('!?.,'), /\p{White_Space}/u.test('a b'), /[^\p{L}]/u.test('abc'));
try { new RegExp('\\p{Unbekannt}', 'u'); } catch (e) { zeige('E', e.constructor.name); }
zeige(/\p{L}/.test('p{L}'), [...'a1b22c'.matchAll(/\d+/g)].map(function (x) { return x[0] + '@' + x.index; }).join(), typeof 'x'.matchAll(/x/g).next);
var it = 'a-b'.matchAll(/(?<c>[a-z])/g); zeige(it.next().value.groups.c, it.next().value.groups.c, it.next().done);
zeige(new RegExp('(?<a>x)', 'gd').flags.replace('d', ''), /a/v.test('a'), 'x'.replace(/(?<k>x)/, function (all, k, idx, str) { return k + idx + str; }));
zeige('2020-01-05'.split(/(?<=-)/).join('|'), 'CamelCaseText'.replace(/(?<=[a-z])(?=[A-Z])/g, '_'));

setTimeout(function () {
    var erwartet = [
        "2026 11  3 jahr,monat,tag 4",
        "19.11.2026 a[X]b[X] b-a$<nix>",
        "true false abb ",
        "42 17 a#b# true true",
        "100 false true 1,2",
        "Größe false true añb_9 12 2",
        "αβγ true true true true false",
        "E SyntaxError",
        "true 1@1,22@3 function",
        "a b true",
        "g true x0x",
        "2020-|01-|05 Camel_Case_Text"
    ];
    assertEq(zeilen.length, erwartet.length, "Anzahl der Ergebnisse");
    for (var i = 0; i < erwartet.length; i++) assertEq(zeilen[i], erwartet[i], "Ergebnis " + (i + 1));
}, 300);
