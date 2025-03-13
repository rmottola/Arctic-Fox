load(libdir + 'wasm.js');
load(libdir + 'asserts.js');

const Module = WebAssembly.Module;
const Instance = WebAssembly.Instance;
const Memory = WebAssembly.Memory;

const mem1Page = new Memory({initial:1});
const mem2Page = new Memory({initial:2});
const mem3Page = new Memory({initial:3});
const mem4Page = new Memory({initial:4});

// Explicitly opt into the new binary format for imports and exports until it
// is used by default everywhere.
const textToBinary = str => wasmTextToBinary(str, 'new-format');

const m1 = new Module(textToBinary('(module (import "foo" "bar") (import "baz" "quux"))'));
assertErrorMessage(() => new Instance(m1), TypeError, /no import object given/);
assertErrorMessage(() => new Instance(m1, {foo:null}), TypeError, /import object field is not an Object/);
assertErrorMessage(() => new Instance(m1, {foo:{bar:{}}}), TypeError, /import object field is not a Function/);
assertErrorMessage(() => new Instance(m1, {foo:{bar:()=>{}}, baz:null}), TypeError, /import object field is not an Object/);
assertErrorMessage(() => new Instance(m1, {foo:{bar:()=>{}}, baz:{}}), TypeError, /import object field is not a Function/);
assertEq(new Instance(m1, {foo:{bar:()=>{}}, baz:{quux:()=>{}}}) instanceof Instance, true);

const m2 = new Module(textToBinary('(module (import "x" "y" (memory 2 3)))'));
assertErrorMessage(() => new Instance(m2), TypeError, /no import object given/);
assertErrorMessage(() => new Instance(m2, {x:null}), TypeError, /import object field is not an Object/);
assertErrorMessage(() => new Instance(m2, {x:{y:{}}}), TypeError, /import object field is not a Memory/);
assertErrorMessage(() => new Instance(m2, {x:{y:mem1Page}}), TypeError, /imported Memory with incompatible size/);
assertErrorMessage(() => new Instance(m2, {x:{y:mem4Page}}), TypeError, /imported Memory with incompatible size/);
assertEq(new Instance(m2, {x:{y:mem2Page}}) instanceof Instance, true);
assertEq(new Instance(m2, {x:{y:mem3Page}}) instanceof Instance, true);

const m3 = new Module(textToBinary('(module (import "foo" "bar" (memory 1 1)) (import "baz" "quux"))'));
assertErrorMessage(() => new Instance(m3), TypeError, /no import object given/);
assertErrorMessage(() => new Instance(m3, {foo:null}), TypeError, /import object field is not an Object/);
assertErrorMessage(() => new Instance(m3, {foo:{bar:{}}}), TypeError, /import object field is not a Memory/);
assertErrorMessage(() => new Instance(m3, {foo:{bar:mem1Page}, baz:null}), TypeError, /import object field is not an Object/);
assertErrorMessage(() => new Instance(m3, {foo:{bar:mem1Page}, baz:{quux:mem1Page}}), TypeError, /import object field is not a Function/);
assertEq(new Instance(m3, {foo:{bar:mem1Page}, baz:{quux:()=>{}}}) instanceof Instance, true);

const m4 = new Module(textToBinary('(module (import "baz" "quux") (import "foo" "bar" (memory 1 1)))'));
assertErrorMessage(() => new Instance(m4), TypeError, /no import object given/);
assertErrorMessage(() => new Instance(m4, {baz:null}), TypeError, /import object field is not an Object/);
assertErrorMessage(() => new Instance(m4, {baz:{quux:{}}}), TypeError, /import object field is not a Function/);
assertErrorMessage(() => new Instance(m4, {baz:{quux:()=>{}}, foo:null}), TypeError, /import object field is not an Object/);
assertErrorMessage(() => new Instance(m4, {baz:{quux:()=>{}}, foo:{bar:()=>{}}}), TypeError, /import object field is not a Memory/);
assertEq(new Instance(m3, {baz:{quux:()=>{}}, foo:{bar:mem1Page}}) instanceof Instance, true);

const m5 = new Module(textToBinary('(module (import "a" "b" (memory 2)))'));
assertErrorMessage(() => new Instance(m5, {a:{b:mem1Page}}), TypeError, /imported Memory with incompatible size/);
assertEq(new Instance(m5, {a:{b:mem2Page}}) instanceof Instance, true);
assertEq(new Instance(m5, {a:{b:mem3Page}}) instanceof Instance, true);
assertEq(new Instance(m5, {a:{b:mem4Page}}) instanceof Instance, true);

assertErrorMessage(() => new Module(textToBinary('(module (memory 2 1))')), TypeError, /maximum memory size less than initial memory size/);
assertErrorMessage(() => new Module(textToBinary('(module (import "a" "b" (memory 2 1)))')), TypeError, /maximum memory size less than initial memory size/);

// Import order:

var arr = [];
var importObj = {
    get foo() { arr.push("foo") },
    get baz() { arr.push("bad") },
};
assertErrorMessage(() => new Instance(m1, importObj), TypeError, /import object field is not an Object/);
assertEq(arr.join(), "foo");

var arr = [];
var importObj = {
    get foo() {
        arr.push("foo");
        return { get bar() { arr.push("bar"); return null } }
    },
    get baz() { arr.push("bad") },
};
assertErrorMessage(() => new Instance(m1, importObj), TypeError, /import object field is not a Function/);
assertEq(arr.join(), "foo,bar");

var arr = [];
var importObj = {
    get foo() {
        arr.push("foo");
        return { get bar() { arr.push("bar"); return () => arr.push("bad") } }
    },
    get baz() {
        arr.push("baz");
        return { get quux() { arr.push("quux"); return () => arr.push("bad") } }
    }
};
assertEq(new Instance(m1, importObj) instanceof Instance, true);
assertEq(arr.join(), "foo,bar,baz,quux");

var arr = [];
var importObj = {
    get foo() {
        arr.push("foo");
        return { get bar() { arr.push("bar"); return new WebAssembly.Memory({initial:1}) } }
    },
    get baz() {
        arr.push("baz");
        return { get quux() { arr.push("quux"); return () => arr.push("bad") } }
    }
};
assertEq(new Instance(m3, importObj) instanceof Instance, true);
assertEq(arr.join(), "foo,bar,baz,quux");
arr = [];
assertEq(new Instance(m4, importObj) instanceof Instance, true);
assertEq(arr.join(), "baz,quux,foo,bar");

// Export key order:

var code = textToBinary('(module)');
var e = new Instance(new Module(code)).exports;
assertEq(Object.keys(e).length, 0);

var code = textToBinary('(module (func) (export "foo" 0))');
var e = new Instance(new Module(code)).exports;
assertEq(Object.keys(e).join(), "foo");
assertEq(e.foo(), undefined);

var code = textToBinary('(module (func) (export "foo" 0) (export "bar" 0))');
var e = new Instance(new Module(code)).exports;
assertEq(Object.keys(e).join(), "foo,bar");
assertEq(e.foo(), undefined);
assertEq(e.bar(), undefined);
assertEq(e.foo, e.bar);

var code = textToBinary('(module (memory 1 1) (export "memory" memory))');
var e = new Instance(new Module(code)).exports;
assertEq(Object.keys(e).join(), "memory");

var code = textToBinary('(module (memory 1 1) (export "foo" memory) (export "bar" memory))');
var e = new Instance(new Module(code)).exports;
assertEq(Object.keys(e).join(), "foo,bar");
assertEq(e.foo, e.bar);
assertEq(e.foo instanceof Memory, true);
assertEq(e.foo.buffer.byteLength, 64*1024);

var code = textToBinary('(module (memory 1 1) (func) (export "foo" 0) (export "bar" memory))');
var e = new Instance(new Module(code)).exports;
assertEq(Object.keys(e).join(), "foo,bar");
assertEq(e.foo(), undefined);
assertEq(e.bar instanceof Memory, true);
assertEq(e.bar instanceof Memory, true);
assertEq(e.bar.buffer.byteLength, 64*1024);

var code = textToBinary('(module (memory 1 1) (func) (export "bar" memory) (export "foo" 0))');
var e = new Instance(new Module(code)).exports;
assertEq(Object.keys(e).join(), "bar,foo");
assertEq(e.foo(), undefined);
assertEq(e.bar.buffer.byteLength, 64*1024);

var code = textToBinary('(module (memory 1 1) (export "" memory))');
var e = new Instance(new Module(code)).exports;
assertEq(Object.keys(e).length, 1);
assertEq(String(Object.keys(e)), "");
assertEq(e[""] instanceof Memory, true);

// Re-exports:

var code = textToBinary('(module (import "a" "b" (memory 1 1)) (export "foo" memory) (export "bar" memory))');
var mem = new Memory({initial:1});
var e = new Instance(new Module(code), {a:{b:mem}}).exports;
assertEq(mem, e.foo);
assertEq(mem, e.bar);

// Default memory rules

assertErrorMessage(() => new Module(textToBinary('(module (import "a" "b" (memory 1 1)) (memory 1 1))')), TypeError, /already have default memory/);
assertErrorMessage(() => new Module(textToBinary('(module (import "a" "b" (memory 1 1)) (import "x" "y" (memory 2 2)))')), TypeError, /already have default memory/);

// Data segments on imports

var m = new Module(textToBinary(`
    (module
        (import "a" "b" (memory 1 1))
        (segment 0 "\\0a\\0b")
        (segment 100 "\\0c\\0d")
        (func $get (param $p i32) (result i32)
            (i32.load8_u (get_local $p)))
        (export "get" $get))
`));
var mem = new Memory({initial:1});
var {get} = new Instance(m, {a:{b:mem}}).exports;
assertEq(get(0), 0xa);
assertEq(get(1), 0xb);
assertEq(get(2), 0x0);
assertEq(get(100), 0xc);
assertEq(get(101), 0xd);
assertEq(get(102), 0x0);
var i8 = new Uint8Array(mem.buffer);
assertEq(i8[0], 0xa);
assertEq(i8[1], 0xb);
assertEq(i8[2], 0x0);
assertEq(i8[100], 0xc);
assertEq(i8[101], 0xd);
assertEq(i8[102], 0x0);
