var g = newGlobal();

function cloneableFunction(body) {
    return evaluate("(function () { " + body + " })", {compileAndGo: false});
}

g.f = cloneableFunction('return function(x) { return x };');
assertEq(g.eval("clone(f)()(9)"), 9);

g.f = cloneableFunction('return function(x) { { let y = x+1; return y } };');
assertEq(g.eval("clone(f)()(9)"), 10);

g.f = cloneableFunction('return function(x) { { let y = x, z = 1; return y+z } };');
assertEq(g.eval("clone(f)()(9)"), 10);

g.f = cloneableFunction('return function(x) { return x.search(/ponies/) };');
assertEq(g.eval("clone(f)()('123ponies')"), 3);

g.f = cloneableFunction('return function(x,y) { return x.search(/a/) + y.search(/b/) };');
assertEq(g.eval("clone(f)()('12a','foo')"), 1);

g.f = cloneableFunction('return function(x) { switch(x) { case "a": return "b"; case null: return "c" } };');
assertEq(g.eval("clone(f)()('a')"), "b");
assertEq(g.eval("clone(f)()(null)"), "c");
assertEq(g.eval("clone(f)()(3)"), undefined);
