// Enable "let" in shell builds. So silly.
if (typeof version != 'undefined')
  version(185);

function classesEnabled() {
    try {
        new Function("class Foo { constructor() { } }");
        return true;
    } catch (e if e instanceof SyntaxError) {
        return false;
    }
}

function assertThrownErrorContains(thunk, substr) {
    try {
        thunk();
    } catch (e) {
        if (e.message.indexOf(substr) !== -1)
            return;
        throw new Error("Expected error containing " + substr + ", got " + e);
    }
    throw new Error("Expected error containing " + substr + ", no exception thrown");
}
