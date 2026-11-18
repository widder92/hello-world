/* Typisierte Felder: ArrayBuffer, Uint8Array und Verwandte, DataView */
(function () {
    var b = new ArrayBuffer(8);
    assertEq(b.byteLength, 8, 'ArrayBuffer: Länge');
    assertEq(Object.prototype.toString.call(b), '[object ArrayBuffer]', 'ArrayBuffer: Art');
    assertEq(b instanceof ArrayBuffer, true, 'ArrayBuffer: instanceof');
    assertEq(typeof ArrayBuffer.prototype.slice, 'function', 'ArrayBuffer.prototype.slice');

    var u8 = new Uint8Array(b);
    assertEq(u8.length, 8, 'Uint8Array über Puffer: Länge');
    assertEq(u8[0], 0, 'anfangs null');
    u8[0] = 257; u8[1] = -1; u8[2] = 3.9; u8[3] = '7';
    assertEq(u8[0] + ',' + u8[1] + ',' + u8[2] + ',' + u8[3], '1,255,3,7', 'Uint8Array: Werte laufen um');
    u8[100] = 5;
    assertEq(u8[100], undefined, 'außerhalb: nichts');
    assertEq(u8.length, 8, 'Länge bleibt');
    assertEq(u8.buffer === b, true, 'buffer');
    assertEq(u8.byteLength + ':' + u8.byteOffset + ':' + u8.BYTES_PER_ELEMENT, '8:0:1', 'Maße');

    var u32 = new Uint32Array(b);
    assertEq(u32.length, 2, 'Uint32Array: zwei Glieder');
    assertEq(u32[0], 1 + 255 * 256 + 3 * 65536 + 7 * 16777216, 'dieselben Bytes als 32-Bit-Zahl');
    u32[1] = 4294967296 + 5;
    assertEq(u32[1], 5, 'Uint32: läuft um');
    u32[1] = -1;
    assertEq(u32[1], 4294967295, 'Uint32: -1');
    assertEq(u8[4] + u8[5] + u8[6] + u8[7], 1020, 'gemeinsamer Speicher');

    var i8 = new Int8Array([127, 128, 255, -129]);
    assertEq(Array.prototype.join.call(i8, ','), '127,-128,-1,127', 'Int8Array aus einem Feld');
    var i16 = new Int16Array(2); i16[0] = 40000; i16[1] = -40000;
    assertEq(i16[0] + ',' + i16[1], '-25536,25536', 'Int16Array');
    var i32 = new Int32Array(1); i32[0] = 2147483648;
    assertEq(i32[0], -2147483648, 'Int32Array');
    var c = new Uint8ClampedArray([300, -5, 1.5, 2.5, 0.4, NaN]);
    assertEq(Array.prototype.join.call(c, ','), '255,0,2,2,0,0', 'Uint8ClampedArray: begrenzt und gerundet');
    var f32 = new Float32Array([1.5, 0.1]);
    assertEq(f32[0], 1.5, 'Float32Array: genau');
    assertEq(f32[1] !== 0.1 && Math.abs(f32[1] - 0.1) < 1e-7, true, 'Float32Array: einfache Genauigkeit');
    var f64 = new Float64Array([0.1, -Infinity]);
    assertEq(f64[0] + ',' + f64[1], '0.1,-Infinity', 'Float64Array');

    var part = new Uint8Array(b, 2, 3);
    assertEq(part.length + ':' + part.byteOffset + ':' + part[0], '3:2:3', 'Sicht auf einen Teil');
    var sub = u8.subarray(1, 4);
    sub[0] = 9;
    assertEq(u8[1], 9, 'subarray teilt den Speicher');
    assertEq(sub.length, 3, 'subarray: Länge');
    var copy = u8.slice(1, 4);
    copy[0] = 77;
    assertEq(u8[1], 9, 'slice kopiert');
    assertEq(copy instanceof Uint8Array, true, 'slice: selbe Art');
    assertEq(u8.subarray(-2).length, 2, 'subarray mit negativem Anfang');

    var t = new Uint8Array(5);
    t.set([1, 2, 3]);
    t.set(new Uint8Array([8, 9]), 3);
    assertEq(Array.prototype.join.call(t, ''), '12389', 'set');
    var threw = false;
    try { t.set([1, 2, 3], 4); } catch (e) { threw = e instanceof RangeError; }
    assert(threw, 'set über das Ende hinaus: RangeError');
    assertEq(t.fill(4, 1, 3), t, 'fill gibt das Feld zurück');
    assertEq(Array.prototype.join.call(t, ''), '14489', 'fill');
    assertEq(t.join('-'), '1-4-4-8-9', 'join');
    assertEq(t.indexOf(8), 3, 'indexOf');
    var sum = 0; t.forEach(function (x) { sum += x; });
    assertEq(sum, 26, 'forEach');
    assertEq(t.reduce(function (a, x) { return a + x; }, 0), 26, 'reduce');
    assertEq(Array.isArray(t), false, 'kein Array');
    assertEq(Object.prototype.toString.call(t), '[object Uint8Array]', 'Art');
    assertEq(ArrayBuffer.isView(t) + ',' + ArrayBuffer.isView(b) + ',' + ArrayBuffer.isView([]), 'true,false,false', 'isView');
    assertEq(Uint8Array.from([1, 2, 3]).length, 3, 'from');
    assertEq(Array.prototype.join.call(Uint16Array.from([1, 2], function (x) { return x * 1000; }), ','), '1000,2000', 'from mit Funktion');
    assertEq(Array.prototype.join.call(Int8Array.of(1, -1), ','), '1,-1', 'of');
    assertEq(Uint32Array.BYTES_PER_ELEMENT + Float64Array.BYTES_PER_ELEMENT, 12, 'BYTES_PER_ELEMENT');
    assertEq(new Uint8Array().length + new Uint8Array(0).length, 0, 'leer');
    var s = b.slice(1, 3);
    assertEq(s.byteLength, 2, 'ArrayBuffer.slice');
    assertEq(new Uint8Array(s)[0], 9, 'ArrayBuffer.slice: Inhalt');

    threw = false;
    try { new Uint32Array(b, 1); } catch (e) { threw = e instanceof RangeError; }
    assert(threw, 'schiefer Anfang: RangeError');
    threw = false;
    try { new Uint8Array(b, 4, 5); } catch (e) { threw = e instanceof RangeError; }
    assert(threw, 'zu lang: RangeError');
    threw = false;
    try { new Uint8Array(-1); } catch (e) { threw = e instanceof RangeError; }
    assert(threw, 'negative Länge: RangeError');
    threw = false;
    try { Uint8Array(4); } catch (e) { threw = e instanceof TypeError; }
    assert(threw, 'ohne new: TypeError');

    var v = new DataView(new ArrayBuffer(16));
    v.setUint16(0, 0x1234);
    assertEq(v.getUint8(0) + ',' + v.getUint8(1), '18,52', 'DataView: sonst die hohe Stelle zuerst');
    v.setUint16(2, 0x1234, true);
    assertEq(v.getUint8(2) + ',' + v.getUint8(3), '52,18', 'DataView: littleEndian');
    assertEq(v.getUint16(2, true), 0x1234, 'DataView: zurücklesen');
    v.setInt32(4, -2);
    assertEq(v.getInt32(4) + ',' + v.getUint32(4), '-2,4294967294', 'DataView: Int32/Uint32');
    v.setFloat64(8, Math.PI);
    assertEq(v.getFloat64(8), Math.PI, 'DataView: Float64');
    v.setFloat32(8, 0.5, true);
    assertEq(v.getFloat32(8, true), 0.5, 'DataView: Float32');
    v.setInt8(15, -3);
    assertEq(v.getInt8(15) + ',' + v.getUint8(15), '-3,253', 'DataView: Int8');
    assertEq(v.byteLength + ':' + v.byteOffset, '16:0', 'DataView: Maße');
    threw = false;
    try { v.getUint32(14); } catch (e) { threw = e instanceof RangeError; }
    assert(threw, 'DataView über das Ende: RangeError');
    threw = false;
    try { v.setInt8(-1, 0); } catch (e) { threw = e instanceof RangeError; }
    assert(threw, 'DataView vor dem Anfang: RangeError');
    var v2 = new DataView(b, 4, 2);
    assertEq(v2.byteLength + ':' + v2.byteOffset, '2:4', 'DataView auf einem Teil');

    /* viele Puffer: Der Speicher wird wieder frei */
    var keep = null;
    for (var i = 0; i < 300; i++) { keep = new Uint8Array(100000); keep[0] = i; }
    gc();
    assertEq(keep[0], 299 % 256, 'viele große Felder nacheinander');

    /* Feldmethoden, die ändern, wirken auf den Speicher */
    var r = new Uint8Array([3, 1, 2]);
    Array.prototype.reverse.call(r);
    assertEq(Array.prototype.join.call(r, ''), '213', 'reverse');
    Array.prototype.sort.call(r);
    assertEq(Array.prototype.join.call(r, ''), '123', 'sort');
})();
