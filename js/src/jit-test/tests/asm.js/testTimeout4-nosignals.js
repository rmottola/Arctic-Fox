// |jit-test| exitstatus: 6;

load(libdir + "asm.js");

suppressSignalHandlers(true);
var g = asmLink(asmCompile(USE_ASM + "function f(d) { d=+d; d=d*.1; d=d/.4; return +d } function g() { while(1) { +f(1.1) } } return g"));
timeout(1);
g();
assertEq(true, false);
