// Zeichenketten
var s = "Hällo Wörld"; assertEq(s.length, 11, "Länge in Zeichen"); assertEq(s.charAt(1), "ä"); assertEq(s[7], "ö"); assertEq(s.charCodeAt(1), 228);
assertEq(s.indexOf("W"), 6); assertEq(s.lastIndexOf("l"), 9); assertEq(s.slice(1, 5), "ällo"); assertEq(s.slice(-5), "Wörld"); assertEq(s.substring(6), "Wörld"); assertEq(s.substr(1, 4), "ällo");
assertEq(s.toUpperCase(), "HÄLLO WÖRLD"); assertEq("STRASSE Ä".toLowerCase(), "strasse ä"); assertEq("ß".toUpperCase(), "SS");
assert(s.includes("llo") && s.startsWith("Hä") && s.endsWith("ld") && !s.startsWith("W") && s.startsWith("W", 6));
assertEq("  x \n".trim(), "x"); assertEq(" x ".trimStart(), "x "); assertEq(" x ".trimEnd(), " x"); assertEq("5".padStart(3, "0"), "005"); assertEq("ab".padEnd(5, "xy"), "abxyx");
assertEq("ab".repeat(3), "ababab"); assertEq("a,b,,c".split(",").length, 4); assertEq("abc".split("").join("-"), "a-b-c"); assertEq("a b c".split(" ", 2).join(), "a,b"); assertEq("abc".split().length, 1);
assertEq("x".concat("y", 1), "xy1"); assertEq("b".localeCompare("a"), 1); assertEq(String.fromCharCode(72, 228), "Hä"); assertEq(String.fromCodePoint(128512).length, HELLOOS ? 1 : 2, "HelloOS zählt Zeichen, nicht UTF-16-Einheiten");
assertEq("😀".codePointAt(0), 128512); assertEq("aä\u{1F600}".length, HELLOOS ? 3 : 4); assertEq("\x41\t".charCodeAt(1), 9); assertEq('"' + "'" + `\``, "\"'`");
assertEq("abc".at(-1), "c"); assertEq("a-b-c".replace("-", "+"), "a+b-c"); assertEq("a-b-c".replaceAll("-", "+"), "a+b+c"); assertEq("abc".replace("b", "[$&]"), "a[b]c");
assertEq("abc" < "abd", true); assertEq("Z" < "a", true); assertEq("ä" > "z", true); assertEq(encodeURIComponent("ä b&c/d"), "%C3%A4%20b%26c%2Fd"); assertEq(decodeURIComponent("%C3%A4%20b"), "ä b");
assertEq(encodeURI("http://x/ä b?c=d&e"), "http://x/%C3%A4%20b?c=d&e"); assertEq(escape("ä b"), "%E4%20b"); assertEq(unescape("%E4%20b%u20AC"), "ä b€");
try { decodeURIComponent("%"); assert(false); } catch (e) { assert(e instanceof URIError); }
var lang = ""; for (var i = 0; i < 2000; i++) lang += "ab"; assertEq(lang.length, 4000); assertEq(lang.indexOf("ba", 3000), 3001);
// Reguläre Ausdrücke
assert(/^\d{3}-\d{4}$/.test("555-1234") && !/^\d{3}-\d{4}$/.test("5555-123")); assert(/hällo/i.test("HÄLLO")); assert(/a.c/.test("abc") && !/a.c/.test("a\nc") && /a.c/s.test("a\nc"));
var m = /(\d+)-(\w+)?(x)?/.exec("nr 42-abc!"); assertEq(m[0], "42-abc"); assertEq(m[1], "42"); assertEq(m[2], "abc"); assertEq(m[3], undefined); assertEq(m.index, 3); assertEq(m.input, "nr 42-abc!");
assertEq("2026-11-15".replace(/(\d+)-(\d+)-(\d+)/, "$3.$2.$1"), "15.11.2026"); assertEq("a1b22c333".replace(/\d+/g, n => "<" + n.length + ">"), "a<1>b<2>c<3>");
assertEq("aaa".replace(/a/g, "b"), "bbb"); assertEq("abc".replace(/(?:)/g, "-"), "-a-b-c-"); assertEq("x".replace(/y/, "z"), "x"); assertEq("a.b".replace(/\./g, "\\$&"), "a\\.b");
assertEq("eins zwei  drei".split(/\s+/).join("|"), "eins|zwei|drei"); assertEq("a1b2c".split(/(\d)/).join(), "a,1,b,2,c"); assertEq("abc".split(/(?:)/).length, 3);
assertEq("The Quick brown".match(/[A-Z]/g).join(), "T,Q"); assertEq("abc".match(/x/g), null); assertEq("abc".match(/b/).index, 1); assertEq("xAbc".search(/[A-Z]/), 1); assertEq("abc".search(/z/), -1);
var g = /a(\d)/g, treffer = [], t; while ((t = g.exec("a1 a2 a3")) !== null) treffer.push(t[1] + "@" + t.index + "/" + g.lastIndex); assertEq(treffer.join(), "1@0/2,2@3/5,3@6/8"); assertEq(g.lastIndex, 0);
assertEq([..."a1b2".matchAll(/[a-z](\d)/g)].map(x => x[1]).join(), "1,2"); assert(/^[\w.+-]+@[\w-]+\.[\w.]+$/.test("erika.muster+x@example.co.uk"), "E-Mail");
assert(/\bwort\b/.test("ein wort hier") && !/\bwort\b/.test("einwort")); assert(/^(?:a|ab)(?:c|bcd)(?:d*)$/.test("abcd")); assertEq(/(a+)+b/.exec("aaab")[1], "aaa");
assertEq(/(a)|(b)/.exec("b")[1], undefined); assertEq(/a{2,3}/.exec("aaaa")[0], "aaa"); assertEq(/a{2,3}?/.exec("aaaa")[0], "aa"); assertEq(/<.+?>/.exec("<a><b>")[0], "<a>"); assertEq(/<.+>/.exec("<a><b>")[0], "<a><b>");
assertEq(/(\w)\1/.exec("hallo")[0], "ll", "Rückbezug"); assertEq(/x(?=y)/.exec("xz xy").index, 3, "Vorausschau"); assertEq(/x(?!y)/.exec("xy xz").index, 3); assertEq(/^b/m.exec("a\nb").index, 2, "mehrzeilig");
assertEq(/[^a-c]+/.exec("abcxyz")[0], "xyz"); assertEq(/[\d\s]+/.exec("ab 12 3x")[0], " 12 3"); assertEq(/[a\-z]+/.exec("x-a")[0], "-a"); assertEq(/ä+/.exec("xää")[0], "ää"); assertEq(/[ß-ÿ]+/.exec("grüße")[0], "üß");
assertEq(new RegExp("a+", "g").source, "a+"); assertEq(new RegExp("\\d+").exec("x12")[0], "12"); assertEq(String(/a\/b/gi), "/a\\/b/gi"); assert(/a/g.global && !/a/.global && /a/i.ignoreCase); assertEq(/a/gim.flags, "gim");
assertEq(/(?<jahr>\d{4})/.exec("im Jahr 2026")[1], "2026", "benannte Gruppe"); assertEq(/(a*)*b/.exec("aab")[0], "aab", "leere Schleife endet"); assertEq(/(?:a?)*?b/.exec("aab")[0], "aab");
try { new RegExp("("); assert(false); } catch (e) { assert(e instanceof SyntaxError, "kaputtes Muster"); } try { new RegExp("a", "q"); assert(false); } catch (e) { assert(e instanceof SyntaxError); }
try { /(x+x+)+y/.test("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"); } catch (e) { assert(e instanceof Error, "bösartiges Muster wird abgebrochen"); }
assertEq("Preis: 12,50 €".replace(/(\d+),(\d+)/, (ganz, a, b, stelle) => a + "." + b + "@" + stelle), "Preis: 12.50@7 €");
assertEq("  a  b ".replace(/^\s+|\s+$/g, "").replace(/\s+/g, " "), "a b"); assertEq("camelCaseText".replace(/[A-Z]/g, c => "-" + c.toLowerCase()), "camel-case-text");
// Datum
var d = new Date(2026, 10, 15, 15, 30, 45, 123); assertEq(d.getFullYear(), 2026); assertEq(d.getMonth(), 10); assertEq(d.getDate(), 15); assertEq(d.getDay(), 0, "Sonntag"); assertEq(d.getHours(), 15); assertEq(d.getMilliseconds(), 123);
assertEq(d.toISOString(), "2026-11-15T15:30:45.123Z"); assertEq(JSON.stringify({ d: d }), '{"d":"2026-11-15T15:30:45.123Z"}'); assertEq(new Date(0).toISOString(), "1970-01-01T00:00:00.000Z");
assertEq(new Date("2026-11-15T10:00:00Z").getTime(), 1794736800000); assertEq(new Date("2024-02-29").getDate(), 29); assertEq(Date.UTC(2000, 0, 1), 946684800000); assertEq(new Date(2026, 0, 31 + 29).getMonth(), 2, "Überlauf der Tage");
assertEq(new Date("Nov 15, 2026 08:05:00").getHours(), 8); assertEq(new Date("15 Nov 2026").getMonth(), 10); assert(isNaN(new Date("Quatsch").getTime())); assertEq(String(new Date(NaN)), "Invalid Date");
var e = new Date(2026, 11, 31, 23, 59, 59); e.setSeconds(e.getSeconds() + 1); assertEq(e.getFullYear(), 2027, "Jahreswechsel"); e.setMonth(1); e.setDate(30); assertEq(e.getMonth(), 2);
assertEq(new Date(2026, 10, 15, 9, 5, 7).toLocaleString(), "15.11.2026, 09:05:07"); assertEq(new Date(2026, 10, 15).toLocaleDateString(), "15.11.2026"); assert(Date.now() > 1700000000000); assertEq(typeof Date(), "string");
assertEq(new Date(2026, 10, 15) - new Date(2026, 10, 14), 86400000); assert(new Date(2026, 1, 1) < new Date(2026, 1, 2)); assertEq(new Date(1969, 11, 31, 23, 59, 59).getTime(), -1000); assertEq(new Date(-1).getFullYear(), 1969);
