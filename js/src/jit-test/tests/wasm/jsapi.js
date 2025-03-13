load(libdir + 'wasm.js');
load(libdir + 'asserts.js');

// Explicitly opt into the new binary format for imports and exports until it
// is used by default everywhere.
const textToBinary = str => wasmTextToBinary(str, 'new-format');

const emptyModule = textToBinary('(module)');

// 'WebAssembly' property on global object
const wasmDesc = Object.getOwnPropertyDescriptor(this, 'WebAssembly');
assertEq(typeof wasmDesc.value, "object");
assertEq(wasmDesc.writable, true);
assertEq(wasmDesc.enumerable, false);
assertEq(wasmDesc.configurable, true);

// 'WebAssembly' object
assertEq(WebAssembly, wasmDesc.value);
assertEq(String(WebAssembly), "[object WebAssembly]");

// 'WebAssembly.Module' property
const moduleDesc = Object.getOwnPropertyDescriptor(WebAssembly, 'Module');
assertEq(typeof moduleDesc.value, "function");
assertEq(moduleDesc.writable, true);
assertEq(moduleDesc.enumerable, false);
assertEq(moduleDesc.configurable, true);

// 'WebAssembly.Module' constructor function
const Module = WebAssembly.Module;
assertEq(Module, moduleDesc.value);
assertEq(Module.length, 1);
assertEq(Module.name, "Module");
assertErrorMessage(() => Module(), TypeError, /constructor without new is forbidden/);
assertErrorMessage(() => new Module(1), TypeError, "first argument must be an ArrayBuffer or typed array object");
assertErrorMessage(() => new Module({}), TypeError, "first argument must be an ArrayBuffer or typed array object");
assertErrorMessage(() => new Module(new Uint8Array()), /* TODO: WebAssembly.CompileError */ TypeError, /compile error/);
assertErrorMessage(() => new Module(new ArrayBuffer()), /* TODO: WebAssembly.CompileError */ TypeError, /compile error/);
assertEq(new Module(emptyModule) instanceof Module, true);
assertEq(new Module(emptyModule.buffer) instanceof Module, true);

// 'WebAssembly.Module.prototype' property
const moduleProtoDesc = Object.getOwnPropertyDescriptor(Module, 'prototype');
assertEq(typeof moduleProtoDesc.value, "object");
assertEq(moduleProtoDesc.writable, false);
assertEq(moduleProtoDesc.enumerable, false);
assertEq(moduleProtoDesc.configurable, false);

// 'WebAssembly.Module.prototype' object
const moduleProto = Module.prototype;
assertEq(moduleProto, moduleProtoDesc.value);
assertEq(String(moduleProto), "[object Object]");
assertEq(Object.getPrototypeOf(moduleProto), Object.prototype);

// 'WebAssembly.Module' instance objects
const m1 = new Module(emptyModule);
assertEq(typeof m1, "object");
assertEq(String(m1), "[object WebAssembly.Module]");
assertEq(Object.getPrototypeOf(m1), moduleProto);

// 'WebAssembly.Instance' property
const instanceDesc = Object.getOwnPropertyDescriptor(WebAssembly, 'Instance');
assertEq(typeof instanceDesc.value, "function");
assertEq(instanceDesc.writable, true);
assertEq(instanceDesc.enumerable, false);
assertEq(instanceDesc.configurable, true);

// 'WebAssembly.Instance' constructor function
const Instance = WebAssembly.Instance;
assertEq(Instance, instanceDesc.value);
assertEq(Instance.length, 1);
assertEq(Instance.name, "Instance");
assertErrorMessage(() => Instance(), TypeError, /constructor without new is forbidden/);
assertErrorMessage(() => new Instance(1), TypeError, "first argument must be a WebAssembly.Module");
assertErrorMessage(() => new Instance({}), TypeError, "first argument must be a WebAssembly.Module");
assertErrorMessage(() => new Instance(m1, null), TypeError, "second argument, if present, must be an object");
assertEq(new Instance(m1) instanceof Instance, true);
assertEq(new Instance(m1, {}) instanceof Instance, true);

// 'WebAssembly.Instance.prototype' property
const instanceProtoDesc = Object.getOwnPropertyDescriptor(Instance, 'prototype');
assertEq(typeof instanceProtoDesc.value, "object");
assertEq(instanceProtoDesc.writable, false);
assertEq(instanceProtoDesc.enumerable, false);
assertEq(instanceProtoDesc.configurable, false);

// 'WebAssembly.Instance.prototype' object
const instanceProto = Instance.prototype;
assertEq(instanceProto, instanceProtoDesc.value);
assertEq(String(instanceProto), "[object Object]");
assertEq(Object.getPrototypeOf(instanceProto), Object.prototype);

// 'WebAssembly.Instance' instance objects
const i1 = new Instance(m1);
assertEq(typeof i1, "object");
assertEq(String(i1), "[object WebAssembly.Instance]");
assertEq(Object.getPrototypeOf(i1), instanceProto);

// 'WebAssembly.Instance' 'exports' property
const exportsDesc = Object.getOwnPropertyDescriptor(i1, 'exports');
assertEq(typeof exportsDesc.value, "object");
assertEq(exportsDesc.writable, true);
assertEq(exportsDesc.enumerable, true);
assertEq(exportsDesc.configurable, true);

// TODO: test export object objects are ES6 module namespace objects.

// 'WebAssembly.Memory' property
const memoryDesc = Object.getOwnPropertyDescriptor(WebAssembly, 'Memory');
assertEq(typeof memoryDesc.value, "function");
assertEq(memoryDesc.writable, true);
assertEq(memoryDesc.enumerable, false);
assertEq(memoryDesc.configurable, true);

// 'WebAssembly.Memory' constructor function
const Memory = WebAssembly.Memory;
assertEq(Memory, memoryDesc.value);
assertEq(Memory.length, 1);
assertEq(Memory.name, "Memory");
assertErrorMessage(() => Memory(), TypeError, /constructor without new is forbidden/);
assertErrorMessage(() => new Memory(1), TypeError, "first argument must be a memory descriptor");
assertErrorMessage(() => new Memory({initial:{valueOf() { throw new Error("here")}}}), Error, "here");
assertErrorMessage(() => new Memory({initial:-1}), TypeError, /bad Memory initial size/);
assertErrorMessage(() => new Memory({initial:Math.pow(2,32)}), TypeError, /bad Memory initial size/);
assertErrorMessage(() => new Memory({initial:Math.pow(2,32)}), TypeError, /bad Memory initial size/);
assertEq(new Memory({initial:1}) instanceof Memory, true);

// 'WebAssembly.Memory.prototype' property
const memoryProtoDesc = Object.getOwnPropertyDescriptor(Memory, 'prototype');
assertEq(typeof memoryProtoDesc.value, "object");
assertEq(memoryProtoDesc.writable, false);
assertEq(memoryProtoDesc.enumerable, false);
assertEq(memoryProtoDesc.configurable, false);

// 'WebAssembly.Memory.prototype' object
const memoryProto = Memory.prototype;
assertEq(memoryProto, memoryProtoDesc.value);
assertEq(String(memoryProto), "[object Object]");
assertEq(Object.getPrototypeOf(memoryProto), Object.prototype);

// 'WebAssembly.Memory' instance objects
const mem1 = new Memory({initial:1});
assertEq(typeof mem1, "object");
assertEq(String(mem1), "[object WebAssembly.Memory]");
assertEq(Object.getPrototypeOf(mem1), memoryProto);

// 'WebAssembly.Memory.prototype.buffer' accessor property
const bufferDesc = Object.getOwnPropertyDescriptor(memoryProto, 'buffer');
assertEq(typeof bufferDesc.get, "function");
assertEq(bufferDesc.set, undefined);
assertEq(bufferDesc.enumerable, false);
assertEq(bufferDesc.configurable, true);

// 'WebAssembly.Memory.prototype.buffer' getter
const bufferGetter = bufferDesc.get;
assertErrorMessage(() => bufferGetter.call(), TypeError, /called on incompatible undefined/);
assertErrorMessage(() => bufferGetter.call({}), TypeError, /called on incompatible Object/);
assertEq(bufferGetter.call(mem1) instanceof ArrayBuffer, true);
assertEq(bufferGetter.call(mem1).byteLength, 64 * 1024);

