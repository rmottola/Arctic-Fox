// |reftest| skip-if(!this.hasOwnProperty("SIMD"))

/*
 * Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/
 */

// Our array for Int32x4 and Float32x4 will have 16 elements
const SIZE_8_ARRAY = 64;
const SIZE_16_ARRAY = 32;
const SIZE_32_ARRAY = 16;
const SIZE_64_ARRAY = 8;

const SIZE_BYTES = SIZE_32_ARRAY * 4;

function MakeComparator(kind, arr, shared) {
    var bpe = arr.BYTES_PER_ELEMENT;
    var uint8 = (bpe != 1) ? new Uint8Array(arr.buffer) : arr;

    // Size in bytes of a single element in the SIMD vector.
    var sizeOfLaneElem;
    // Typed array constructor corresponding to the SIMD kind.
    var typedArrayCtor;
    switch (kind) {
      case 'Int8x16':
        sizeOfLaneElem = 1;
        typedArrayCtor = Int8Array;
        break;
      case 'Int16x8':
        sizeOfLaneElem = 2;
        typedArrayCtor = Int16Array;
        break;
      case 'Int32x4':
        sizeOfLaneElem = 4;
        typedArrayCtor = Int32Array;
        break;
      case 'Uint8x16':
        sizeOfLaneElem = 1;
        typedArrayCtor = Uint8Array;
        break;
      case 'Uint16x8':
        sizeOfLaneElem = 2;
        typedArrayCtor = Uint16Array;
        break;
      case 'Uint32x4':
        sizeOfLaneElem = 4;
        typedArrayCtor = Uint32Array;
        break;
      case 'Float32x4':
        sizeOfLaneElem = 4;
        typedArrayCtor = Float32Array;
        break;
      case 'Float64x2':
        sizeOfLaneElem = 8;
        typedArrayCtor = Float64Array;
        break;
      default:
        assertEq(true, false, "unknown SIMD kind");
    }
    var lanes = 16 / sizeOfLaneElem;
    // Reads (numElemToRead * sizeOfLaneElem) bytes in arr, and reinterprets
    // these bytes as a typed array equivalent to the typed SIMD vector.
    var slice = function(start, numElemToRead) {
        // Read enough bytes
        var startBytes = start * bpe;
        var endBytes = startBytes + numElemToRead * sizeOfLaneElem;
        var asArray = Array.prototype.slice.call(uint8, startBytes, endBytes);

        // If length is less than SIZE_BYTES bytes, fill with 0.
        // This is needed for load1, load2, load3 which do only partial
        // reads.
        for (var i = asArray.length; i < SIZE_BYTES; i++) asArray[i] = 0;
        assertEq(asArray.length, SIZE_BYTES);

        return new typedArrayCtor(new Uint8Array(asArray).buffer);
    }

    var assertFunc = getAssertFuncFromLength(lanes);
    var type = SIMD[kind];
    return {
        load1: function(index) {
            if (lanes >= 8) // Int8x16 and Int16x8 only support load, no load1/load2/etc.
                return
            var v = type.load1(arr, index);
            assertFunc(v, slice(index, 1));
        },

        load2: function(index) {
            if (lanes !== 4)
                return;
            var v = type.load2(arr, index);
            assertFunc(v, slice(index, 2));
        },

       load3: function(index) {
           if (lanes !== 4)
               return;
           var v = type.load3(arr, index);
           assertFunc(v, slice(index, 3));
        },

        load: function(index) {
           var v = type.load(arr, index);
           assertFunc(v, slice(index, lanes));
        }
    }
}

function testLoad(kind, TA) {
    var lanes = TA.length / 4;
    for (var i = TA.length; i--;)
        TA[i] = i;

    for (var ta of [
                    new Uint8Array(TA.buffer),
                    new Int8Array(TA.buffer),
                    new Uint16Array(TA.buffer),
                    new Int16Array(TA.buffer),
                    new Uint32Array(TA.buffer),
                    new Int32Array(TA.buffer),
                    new Float32Array(TA.buffer),
                    new Float64Array(TA.buffer)
                   ])
    {
        // Invalid args
        assertThrowsInstanceOf(() => SIMD[kind].load(), TypeError);
        assertThrowsInstanceOf(() => SIMD[kind].load(ta), TypeError);
        assertThrowsInstanceOf(() => SIMD[kind].load("hello", 0), TypeError);
        // Indexes must be integers, there is no rounding.
        assertThrowsInstanceOf(() => SIMD[kind].load(ta, 1.5), RangeError);
        assertThrowsInstanceOf(() => SIMD[kind].load(ta, -1), RangeError);
        assertThrowsInstanceOf(() => SIMD[kind].load(ta, "hello"), RangeError);
        assertThrowsInstanceOf(() => SIMD[kind].load(ta, NaN), RangeError);
        // Try to trip up the bounds checking. Int32 is enough for everybody.
        assertThrowsInstanceOf(() => SIMD[kind].load(ta, 0x100000000), RangeError);
        assertThrowsInstanceOf(() => SIMD[kind].load(ta, 0x80000000), RangeError);
        assertThrowsInstanceOf(() => SIMD[kind].load(ta, 0x40000000), RangeError);
        assertThrowsInstanceOf(() => SIMD[kind].load(ta, 0x20000000), RangeError);
        assertThrowsInstanceOf(() => SIMD[kind].load(ta, (1<<30) * (1<<23) - 1), RangeError);
        assertThrowsInstanceOf(() => SIMD[kind].load(ta, (1<<30) * (1<<23)), RangeError);

        // Valid and invalid reads
        var C = MakeComparator(kind, ta);
        var bpe = ta.BYTES_PER_ELEMENT;

        var lastValidArgLoad1   = (SIZE_BYTES - (16 / lanes))  / bpe | 0;
        var lastValidArgLoad2   = (SIZE_BYTES - 8)  / bpe | 0;
        var lastValidArgLoad3   = (SIZE_BYTES - 12) / bpe | 0;
        var lastValidArgLoad    = (SIZE_BYTES - 16) / bpe | 0;

        C.load(0);
        C.load(1);
        C.load(2);
        C.load(3);
        C.load(lastValidArgLoad);

        C.load1(0);
        C.load1(1);
        C.load1(2);
        C.load1(3);
        C.load1(lastValidArgLoad1);

        C.load2(0);
        C.load2(1);
        C.load2(2);
        C.load2(3);
        C.load2(lastValidArgLoad2);

        C.load3(0);
        C.load3(1);
        C.load3(2);
        C.load3(3);
        C.load3(lastValidArgLoad3);

        assertThrowsInstanceOf(() => SIMD[kind].load(ta, lastValidArgLoad + 1), RangeError);
        if (lanes <= 4) {
            assertThrowsInstanceOf(() => SIMD[kind].load1(ta, lastValidArgLoad1 + 1), RangeError);
        }
        if (lanes == 4) {
            assertThrowsInstanceOf(() => SIMD[kind].load2(ta, lastValidArgLoad2 + 1), RangeError);
            assertThrowsInstanceOf(() => SIMD[kind].load3(ta, lastValidArgLoad3 + 1), RangeError);
        }

        // Indexes are coerced with ToNumber. Try some strings that
        // CanonicalNumericIndexString() would reject.
        C.load("1.0e0");
        C.load(" 2");
    }

    if (lanes == 4) {
        // Test ToNumber behavior.
        var obj = {
            valueOf: function() { return 12 }
        }
        var v = SIMD[kind].load(TA, obj);
        assertEqX4(v, [12, 13, 14, 15]);
    }

    var obj = {
        valueOf: function() { throw new TypeError("i ain't a number"); }
    }
    assertThrowsInstanceOf(() => SIMD[kind].load(TA, obj), TypeError);
}

function testSharedArrayBufferCompat() {
    var TA = new Float32Array(new SharedArrayBuffer(16*4));
    for (var i = 0; i < 16; i++)
        TA[i] = i + 1;

    for (var ta of [
                    new Uint8Array(TA.buffer),
                    new Int8Array(TA.buffer),
                    new Uint16Array(TA.buffer),
                    new Int16Array(TA.buffer),
                    new Uint32Array(TA.buffer),
                    new Int32Array(TA.buffer),
                    new Float32Array(TA.buffer),
                    new Float64Array(TA.buffer)
                   ])
    {
        for (var kind of ['Int32x4', 'Uint32x4', 'Float32x4', 'Float64x2']) {
            var comp = MakeComparator(kind, ta);
            comp.load(0);
            comp.load1(0);
            comp.load2(0);
            comp.load3(0);

            comp.load(3);
            comp.load1(3);
            comp.load2(3);
            comp.load3(3);
        }

        assertThrowsInstanceOf(() => SIMD.Int32x4.load(ta, 1024), RangeError);
        assertThrowsInstanceOf(() => SIMD.Uint32x4.load(ta, 1024), RangeError);
        assertThrowsInstanceOf(() => SIMD.Float32x4.load(ta, 1024), RangeError);
        assertThrowsInstanceOf(() => SIMD.Float64x2.load(ta, 1024), RangeError);
    }
}

testLoad('Float32x4', new Float32Array(SIZE_32_ARRAY));
testLoad('Float64x2', new Float64Array(SIZE_64_ARRAY));
testLoad('Int8x16', new Int8Array(SIZE_8_ARRAY));
testLoad('Int16x8', new Int16Array(SIZE_16_ARRAY));
testLoad('Int32x4', new Int32Array(SIZE_32_ARRAY));
testLoad('Uint8x16', new Uint8Array(SIZE_8_ARRAY));
testLoad('Uint16x8', new Uint16Array(SIZE_16_ARRAY));
testLoad('Uint32x4', new Uint32Array(SIZE_32_ARRAY));

if (typeof SharedArrayBuffer != "undefined") {
  testLoad('Float32x4', new Float32Array(new SharedArrayBuffer(SIZE_8_ARRAY)));
  testLoad('Float64x2', new Float64Array(new SharedArrayBuffer(SIZE_8_ARRAY)));
  testLoad('Int8x16', new Int8Array(new SharedArrayBuffer(SIZE_8_ARRAY)));
  testLoad('Int16x8', new Int16Array(new SharedArrayBuffer(SIZE_8_ARRAY)));
  testLoad('Int32x4', new Int32Array(new SharedArrayBuffer(SIZE_8_ARRAY)));
  testLoad('Uint8x16', new Uint8Array(new SharedArrayBuffer(SIZE_8_ARRAY)));
  testLoad('Uint16x8', new Uint16Array(new SharedArrayBuffer(SIZE_8_ARRAY)));
  testLoad('Uint32x4', new Uint32Array(new SharedArrayBuffer(SIZE_8_ARRAY)));
}

testSharedArrayBufferCompat();

if (typeof reportCompare === "function")
    reportCompare(true, true);
