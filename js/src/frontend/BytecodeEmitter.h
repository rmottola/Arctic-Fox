/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef frontend_BytecodeEmitter_h
#define frontend_BytecodeEmitter_h

/*
 * JS bytecode generation.
 */

#include "jscntxt.h"
#include "jsopcode.h"
#include "jsscript.h"

#include "frontend/ParseMaps.h"
#include "frontend/Parser.h"
#include "frontend/SharedContext.h"
#include "frontend/SourceNotes.h"

namespace js {

class ScopeObject;

namespace frontend {

class FullParseHandler;
class ObjectBox;
class ParseNode;
template <typename ParseHandler> class Parser;
class SharedContext;
class TokenStream;

class CGConstList {
    Vector<Value> list;
  public:
    explicit CGConstList(ExclusiveContext* cx) : list(cx) {}
    MOZ_MUST_USE bool append(Value v) {
        MOZ_ASSERT_IF(v.isString(), v.toString()->isAtom());
        return list.append(v);
    }
    size_t length() const { return list.length(); }
    void finish(ConstArray* array);
};

struct CGObjectList {
    uint32_t            length;     /* number of emitted so far objects */
    ObjectBox*          lastbox;   /* last emitted object */

    CGObjectList() : length(0), lastbox(nullptr) {}

    unsigned add(ObjectBox* objbox);
    unsigned indexOf(JSObject* obj);
    void finish(ObjectArray* array);
    ObjectBox* find(uint32_t index);
};

struct CGTryNoteList {
    Vector<JSTryNote> list;
    explicit CGTryNoteList(ExclusiveContext* cx) : list(cx) {}

    MOZ_MUST_USE bool append(JSTryNoteKind kind, uint32_t stackDepth, size_t start, size_t end);
    size_t length() const { return list.length(); }
    void finish(TryNoteArray* array);
};

struct CGBlockScopeNote : public BlockScopeNote
{
    // The end offset. Used to compute the length; may need adjusting first if
    // in the prologue.
    uint32_t end;

    // Is the start offset in the prologue?
    bool startInPrologue;

    // Is the end offset in the prologue?
    bool endInPrologue;
};

struct CGBlockScopeList {
    Vector<CGBlockScopeNote> list;
    explicit CGBlockScopeList(ExclusiveContext* cx) : list(cx) {}

    MOZ_MUST_USE bool append(uint32_t scopeObjectIndex, uint32_t offset, bool inPrologue,
                             uint32_t parent);
    uint32_t findEnclosingScope(uint32_t index);
    void recordEnd(uint32_t index, uint32_t offset, bool inPrologue);
    size_t length() const { return list.length(); }
    void finish(BlockScopeArray* array, uint32_t prologueLength);
};

struct CGYieldOffsetList {
    Vector<uint32_t> list;
    explicit CGYieldOffsetList(ExclusiveContext* cx) : list(cx) {}

    MOZ_MUST_USE bool append(uint32_t offset) { return list.append(offset); }
    size_t length() const { return list.length(); }
    void finish(YieldOffsetArray& array, uint32_t prologueLength);
};

struct LoopStmtInfo;
struct StmtInfoBCE;

// Use zero inline elements because these go on the stack and affect how many
// nested functions are possible.
typedef Vector<jsbytecode, 0> BytecodeVector;
typedef Vector<jssrcnote, 0> SrcNotesVector;

// This enum tells BytecodeEmitter::emitVariables and the destructuring
// methods how emit the given Parser::variables parse tree.
enum VarEmitOption {
    // The normal case. Emit code to evaluate initializer expressions and
    // assign them to local variables. Also emit JSOP_DEF{VAR,LET,CONST}
    // opcodes in the prologue if the declaration occurs at toplevel.
    InitializeVars,

    // Emit only JSOP_DEFVAR opcodes, in the prologue, if necessary. This is
    // used in one case: `for (var $BindingPattern in/of obj)`. If we're at
    // toplevel, the variable(s) must be defined with JSOP_DEFVAR, but they're
    // populated inside the loop, via emitAssignment.
    DefineVars,

    // Emit code to evaluate initializer expressions and leave those values on
    // the stack. This is used to implement `for (let/const ...;;)` and
    // deprecated `let` blocks.
    PushInitialValues,

    // Like InitializeVars, but bind using BINDVAR instead of
    // BINDNAME/BINDGNAME. Only used for emitting declarations synthesized for
    // Annex B block-scoped function semantics.
    AnnexB,
};

// Linked list of jump instructions that need to be patched. The linked list is
// stored in the bytes of the incomplete bytecode that will be patched, so no
// extra memory is needed, and patching the instructions destroys the list.
//
// Example:
//
//     JumpList brList;
//     if (!emitJump(JSOP_IFEQ, &brList))
//         return false;
//     ...
//     JumpTarget label;
//     if (!emitJumpTarget(&label))
//         return false;
//     ...
//     if (!emitJump(JSOP_GOTO, &brList))
//         return false;
//     ...
//     patchJumpsToTarget(brList, label);
//
//                 +-> -1
//                 |
//                 |
//    ifeq ..   <+ +                +-+   ifeq ..
//    ..         |                  |     ..
//  label:       |                  +-> label:
//    jumptarget |                  |     jumptarget
//    ..         |                  |     ..
//    goto .. <+ +                  +-+   goto .. <+
//             |                                   |
//             |                                   |
//             +                                   +
//           brList                              brList
//
//       |                                  ^
//       +------- patchJumpsToTarget -------+
//

// Offset of a jump target instruction, used for patching jump instructions.
struct JumpTarget {
    ptrdiff_t offset;
};

struct JumpList {
    // -1 is used to mark the end of jump lists.
    JumpList() : offset(-1) {}
    ptrdiff_t offset;

    // Add a jump instruction to the list.
    void push(jsbytecode* code, ptrdiff_t jumpOffset);

    // Patch all jump instructions in this list to jump to `target`.  This
    // clobbers the list.
    void patchAll(jsbytecode* code, JumpTarget target);
};

struct BytecodeEmitter
{
    SharedContext* const sc;      /* context shared between parsing and bytecode generation */

    ExclusiveContext* const cx;

    BytecodeEmitter* const parent;  /* enclosing function or global context */

    Rooted<JSScript*> script;       /* the JSScript we're ultimately producing */

    Rooted<LazyScript*> lazyScript; /* the lazy script if mode is LazyFunction,
                                        nullptr otherwise. */

    struct EmitSection {
        BytecodeVector code;        /* bytecode */
        SrcNotesVector notes;       /* source notes, see below */
        ptrdiff_t   lastNoteOffset; /* code offset for last source note */
        uint32_t    currentLine;    /* line number for tree-based srcnote gen */
        uint32_t    lastColumn;     /* zero-based column index on currentLine of
                                       last SRC_COLSPAN-annotated opcode */

        EmitSection(ExclusiveContext* cx, uint32_t lineNum)
          : code(cx), notes(cx), lastNoteOffset(0), currentLine(lineNum), lastColumn(0)
        {}
    };
    EmitSection prologue, main, *current;

    /* the parser */
    Parser<FullParseHandler>* const parser;

    HandleScript    evalCaller;     /* scripted caller info for eval and dbgapi */

    StmtInfoStack<StmtInfoBCE> stmtStack;

    OwnedAtomIndexMapPtr atomIndices; /* literals indexed for mapping */
    unsigned        firstLine;      /* first line, for JSScript::initFromEmitter */

    /*
     * Only unaliased locals have stack slots assigned to them. This vector is
     * used to map a local index (which includes unaliased and aliased locals)
     * to its stack slot index.
     */
    Vector<uint32_t, 16> localsToFrameSlots_;

    int32_t         stackDepth;     /* current stack depth in script frame */
    uint32_t        maxStackDepth;  /* maximum stack depth so far */

    uint32_t        arrayCompDepth; /* stack depth of array in comprehension */

    unsigned        emitLevel;      /* emitTree recursion level */

    CGConstList     constList;      /* constants to be included with the script */

    CGObjectList    objectList;     /* list of emitted objects */
    CGTryNoteList   tryNoteList;    /* list of emitted try notes */
    CGBlockScopeList blockScopeList;/* list of emitted block scope notes */

    /*
     * For each yield op, map the yield index (stored as bytecode operand) to
     * the offset of the next op.
     */
    CGYieldOffsetList yieldOffsetList;

    uint16_t        typesetCount;   /* Number of JOF_TYPESET opcodes generated */

    bool            hasSingletons:1;    /* script contains singleton initializer JSOP_OBJECT */

    bool            hasTryFinally:1;    /* script contains finally block */

    bool            emittingForInit:1;  /* true while emitting init expr of for; exclude 'in' */

    bool            emittingRunOnceLambda:1; /* true while emitting a lambda which is only
                                                expected to run once. */

    bool isRunOnceLambda();

    bool            insideEval:1;       /* True if compiling an eval-expression or a function
                                           nested inside an eval. */

    const bool      insideNonGlobalEval:1;  /* True if this is a direct eval
                                               call in some non-global scope. */

    bool            insideModule:1;     /* True if compiling inside a module. */

    enum EmitterMode {
        Normal,

        /*
         * Emit JSOP_GETINTRINSIC instead of JSOP_GETNAME and assert that
         * JSOP_GETNAME and JSOP_*GNAME don't ever get emitted. See the comment
         * for the field |selfHostingMode| in Parser.h for details.
         */
        SelfHosting,

        /*
         * Check the static scope chain of the root function for resolving free
         * variable accesses in the script.
         */
        LazyFunction
    };

    const EmitterMode emitterMode;

    // The end location of a function body that is being emitted.
    uint32_t functionBodyEndPos;
    // Whether functionBodyEndPos was set.
    bool functionBodyEndPosSet;

    /*
     * Note that BytecodeEmitters are magic: they own the arena "top-of-stack"
     * space above their tempMark points. This means that you cannot alloc from
     * tempLifoAlloc and save the pointer beyond the next BytecodeEmitter
     * destruction.
     */
    BytecodeEmitter(BytecodeEmitter* parent, Parser<FullParseHandler>* parser, SharedContext* sc,
                    HandleScript script, Handle<LazyScript*> lazyScript,
                    bool insideEval, HandleScript evalCaller,
                    bool insideNonGlobalEval, uint32_t lineNum, EmitterMode emitterMode = Normal);

    // An alternate constructor that uses a TokenPos for the starting
    // line and that sets functionBodyEndPos as well.
    BytecodeEmitter(BytecodeEmitter* parent, Parser<FullParseHandler>* parser, SharedContext* sc,
                    HandleScript script, Handle<LazyScript*> lazyScript,
                    bool insideEval, HandleScript evalCaller,
                    bool insideNonGlobalEval, TokenPos bodyPosition, EmitterMode emitterMode = Normal);

    MOZ_MUST_USE bool init();
    MOZ_MUST_USE bool updateLocalsToFrameSlots();

    StmtInfoBCE* innermostStmt() const { return stmtStack.innermost(); }
    StmtInfoBCE* innermostScopeStmt() const { return stmtStack.innermostScopeStmt(); }
    JSObject* innermostStaticScope() const;
    JSObject* blockScopeOfDef(Definition* dn) const {
        return parser->blockScopes[dn->pn_blockid];
    }

    bool atBodyLevel(StmtInfoBCE* stmt) const;
    bool atBodyLevel() const {
        return atBodyLevel(innermostStmt());
    }
    uint32_t computeHops(ParseNode* pn, BytecodeEmitter** bceOfDefOut);
    bool isAliasedName(BytecodeEmitter* bceOfDef, ParseNode* pn);
    MOZ_MUST_USE bool computeDefinitionIsAliased(BytecodeEmitter* bceOfDef, Definition* dn, JSOp* op);

    MOZ_ALWAYS_INLINE
    MOZ_MUST_USE bool makeAtomIndex(JSAtom* atom, jsatomid* indexp) {
        AtomIndexAddPtr p = atomIndices->lookupForAdd(atom);
        if (p) {
            *indexp = p.value();
            return true;
        }

        jsatomid index = atomIndices->count();
        if (!atomIndices->add(p, atom, index))
            return false;

        *indexp = index;
        return true;
    }

    bool isInLoop();
    MOZ_MUST_USE bool checkSingletonContext();

    // Check whether our function is in a run-once context (a toplevel
    // run-one script or a run-once lambda).
    MOZ_MUST_USE bool checkRunOnceContext();

    bool needsImplicitThis();

    void tellDebuggerAboutCompiledScript(ExclusiveContext* cx);

    inline TokenStream* tokenStream();

    BytecodeVector& code() const { return current->code; }
    jsbytecode* code(ptrdiff_t offset) const { return current->code.begin() + offset; }
    ptrdiff_t offset() const { return current->code.end() - current->code.begin(); }
    ptrdiff_t prologueOffset() const { return prologue.code.end() - prologue.code.begin(); }
    void switchToMain() { current = &main; }
    void switchToPrologue() { current = &prologue; }
    bool inPrologue() const { return current == &prologue; }

    SrcNotesVector& notes() const { return current->notes; }
    ptrdiff_t lastNoteOffset() const { return current->lastNoteOffset; }
    unsigned currentLine() const { return current->currentLine; }
    unsigned lastColumn() const { return current->lastColumn; }

    void setFunctionBodyEndPos(TokenPos pos) {
        functionBodyEndPos = pos.end;
        functionBodyEndPosSet = true;
    }

    bool reportError(ParseNode* pn, unsigned errorNumber, ...);
    bool reportStrictWarning(ParseNode* pn, unsigned errorNumber, ...);
    bool reportStrictModeError(ParseNode* pn, unsigned errorNumber, ...);

    // If pn contains a useful expression, return true with *answer set to true.
    // If pn contains a useless expression, return true with *answer set to
    // false. Return false on error.
    //
    // The caller should initialize *answer to false and invoke this function on
    // an expression statement or similar subtree to decide whether the tree
    // could produce code that has any side effects.  For an expression
    // statement, we define useless code as code with no side effects, because
    // the main effect, the value left on the stack after the code executes,
    // will be discarded by a pop bytecode.
    MOZ_MUST_USE bool checkSideEffects(ParseNode* pn, bool* answer);

#ifdef DEBUG
    MOZ_MUST_USE bool checkStrictOrSloppy(JSOp op);
#endif

    // Append a new source note of the given type (and therefore size) to the
    // notes dynamic array, updating noteCount. Return the new note's index
    // within the array pointed at by current->notes as outparam.
    MOZ_MUST_USE bool newSrcNote(SrcNoteType type, unsigned* indexp = nullptr);
    MOZ_MUST_USE bool newSrcNote2(SrcNoteType type, ptrdiff_t offset, unsigned* indexp = nullptr);
    MOZ_MUST_USE bool newSrcNote3(SrcNoteType type, ptrdiff_t offset1, ptrdiff_t offset2,
                                  unsigned* indexp = nullptr);

    void copySrcNotes(jssrcnote* destination, uint32_t nsrcnotes);
    MOZ_MUST_USE bool setSrcNoteOffset(unsigned index, unsigned which, ptrdiff_t offset);

    // NB: this function can add at most one extra extended delta note.
    MOZ_MUST_USE bool addToSrcNoteDelta(jssrcnote* sn, ptrdiff_t delta);

    // Finish taking source notes in cx's notePool. If successful, the final
    // source note count is stored in the out outparam.
    MOZ_MUST_USE bool finishTakingSrcNotes(uint32_t* out);

    // Control whether emitTree emits a line number note.
    enum EmitLineNumberNote {
        EMIT_LINENOTE,
        SUPPRESS_LINENOTE
    };

    // Emit code for the tree rooted at pn.
    MOZ_MUST_USE bool emitTree(ParseNode* pn, EmitLineNumberNote emitLineNote = EMIT_LINENOTE);

    // Emit function code for the tree rooted at body.
    MOZ_MUST_USE bool emitFunctionScript(ParseNode* body);

    // Emit module code for the tree rooted at body.
    MOZ_MUST_USE bool emitModuleScript(ParseNode* body);

    // If op is JOF_TYPESET (see the type barriers comment in TypeInference.h),
    // reserve a type set to store its result.
    void checkTypeSet(JSOp op);

    void updateDepth(ptrdiff_t target);
    MOZ_MUST_USE bool updateLineNumberNotes(uint32_t offset);
    MOZ_MUST_USE bool updateSourceCoordNotes(uint32_t offset);

    MOZ_MUST_USE bool bindNameToSlot(ParseNode* pn);
    MOZ_MUST_USE bool bindNameToSlotHelper(ParseNode* pn);

    void strictifySetNameNode(ParseNode* pn);
    JSOp strictifySetNameOp(JSOp op);

    MOZ_MUST_USE bool tryConvertFreeName(ParseNode* pn);

    bool popStatement();
    bool popStatement(JumpTarget breakTarget);
    void pushStatement(StmtInfoBCE* stmt, StmtType type, JumpTarget top);
    void pushStatementInner(StmtInfoBCE* stmt, StmtType type, JumpTarget top);
    void pushLoopStatement(LoopStmtInfo* stmt, StmtType type, JumpTarget top);

    MOZ_MUST_USE bool enterNestedScope(StmtInfoBCE* stmt, ObjectBox* objbox, StmtType stmtType);
    MOZ_MUST_USE bool leaveNestedScope(StmtInfoBCE* stmt);

    MOZ_MUST_USE bool enterBlockScope(StmtInfoBCE* stmtInfo, ObjectBox* objbox, JSOp initialValueOp,
                                      unsigned alreadyPushed = 0);

    MOZ_MUST_USE bool computeAliasedSlots(Handle<StaticBlockScope*> blockScope);

    MOZ_MUST_USE bool lookupAliasedName(HandleScript script, PropertyName* name, uint32_t* pslot,
                                        ParseNode* pn = nullptr);
    MOZ_MUST_USE bool lookupAliasedNameSlot(PropertyName* name, ScopeCoordinate* sc);

    // In a function, block-scoped locals go after the vars, and form part of the
    // fixed part of a stack frame.  Outside a function, there are no fixed vars,
    // but block-scoped locals still form part of the fixed part of a stack frame
    // and are thus addressable via GETLOCAL and friends.
    void computeLocalOffset(Handle<StaticBlockScope*> blockScope);

    MOZ_MUST_USE bool flushPops(int* npops);

    MOZ_MUST_USE bool emitCheck(ptrdiff_t delta, ptrdiff_t* offset);

    // Emit one bytecode.
    MOZ_MUST_USE bool emit1(JSOp op);

    // Emit two bytecodes, an opcode (op) with a byte of immediate operand
    // (op1).
    MOZ_MUST_USE bool emit2(JSOp op, uint8_t op1);

    // Emit three bytecodes, an opcode with two bytes of immediate operands.
    MOZ_MUST_USE bool emit3(JSOp op, jsbytecode op1, jsbytecode op2);

    // Helper to emit JSOP_DUPAT. The argument is the value's depth on the
    // JS stack, as measured from the top.
    MOZ_MUST_USE bool emitDupAt(unsigned slotFromTop);

    // Emit a bytecode followed by an uint16 immediate operand stored in
    // big-endian order.
    MOZ_MUST_USE bool emitUint16Operand(JSOp op, uint32_t operand);

    // Emit a bytecode followed by an uint32 immediate operand.
    MOZ_MUST_USE bool emitUint32Operand(JSOp op, uint32_t operand);

    // Emit (1 + extra) bytecodes, for N bytes of op and its immediate operand.
    MOZ_MUST_USE bool emitN(JSOp op, size_t extra, ptrdiff_t* offset = nullptr);

    MOZ_MUST_USE bool emitNumberOp(double dval);

    MOZ_MUST_USE bool emitThisLiteral(ParseNode* pn);
    MOZ_MUST_USE bool emitCreateFunctionThis();
    MOZ_MUST_USE bool emitGetFunctionThis(ParseNode* pn);
    MOZ_MUST_USE bool emitGetThisForSuperBase(ParseNode* pn);
    MOZ_MUST_USE bool emitSetThis(ParseNode* pn);

    // These functions are used to emit GETLOCAL/GETALIASEDVAR or
    // SETLOCAL/SETALIASEDVAR for a particular binding on a function's
    // CallObject.
    MOZ_MUST_USE bool emitLoadFromEnclosingFunctionScope(BindingIter& bi);
    MOZ_MUST_USE bool emitStoreToEnclosingFunctionScope(BindingIter& bi);

    uint32_t computeHopsToEnclosingFunction();

    // Handle jump opcodes and jump targets.
    MOZ_MUST_USE bool emitJumpTarget(JumpTarget* target);
    MOZ_MUST_USE bool emitJumpNoFallthrough(JSOp op, JumpList* jump);
    MOZ_MUST_USE bool emitJump(JSOp op, JumpList* jump);
    MOZ_MUST_USE bool emitBackwardJump(JSOp op, JumpTarget target, JumpList* jump,
                                       JumpTarget* fallthrough);
    void patchJumpsToTarget(JumpList jump, JumpTarget target);
    MOZ_MUST_USE bool emitJumpTargetAndPatch(JumpList jump);

    MOZ_MUST_USE bool emitCall(JSOp op, uint16_t argc, ParseNode* pn = nullptr);

    MOZ_MUST_USE bool emitLoopHead(ParseNode* nextpn, JumpTarget* top);
    MOZ_MUST_USE bool emitLoopEntry(ParseNode* nextpn, JumpList entryJump);

    void setContinueTarget(StmtInfoBCE* stmt, JumpTarget target);
    void setContinueHere(StmtInfoBCE* stmt);

    MOZ_MUST_USE bool emitGoto(StmtInfoBCE* toStmt, JumpList* jumplist,
                               SrcNoteType noteType = SRC_NULL);

    MOZ_MUST_USE bool emitIndex32(JSOp op, uint32_t index);
    MOZ_MUST_USE bool emitIndexOp(JSOp op, uint32_t index);

    MOZ_MUST_USE bool emitAtomOp(JSAtom* atom, JSOp op);
    MOZ_MUST_USE bool emitAtomOp(ParseNode* pn, JSOp op);

    MOZ_MUST_USE bool emitArrayLiteral(ParseNode* pn);
    MOZ_MUST_USE bool emitArray(ParseNode* pn, uint32_t count, JSOp op);
    MOZ_MUST_USE bool emitArrayComp(ParseNode* pn);

    MOZ_MUST_USE bool emitInternedObjectOp(uint32_t index, JSOp op);
    MOZ_MUST_USE bool emitObjectOp(ObjectBox* objbox, JSOp op);
    MOZ_MUST_USE bool emitObjectPairOp(ObjectBox* objbox1, ObjectBox* objbox2, JSOp op);
    MOZ_MUST_USE bool emitRegExp(uint32_t index);

    MOZ_NEVER_INLINE MOZ_MUST_USE bool emitFunction(ParseNode* pn, bool needsProto = false);
    MOZ_NEVER_INLINE MOZ_MUST_USE bool emitObject(ParseNode* pn);

    MOZ_MUST_USE bool emitHoistedFunctionsInList(ParseNode* pn);

    MOZ_MUST_USE bool emitPropertyList(ParseNode* pn, MutableHandlePlainObject objp,
                                       PropListType type);

    // To catch accidental misuse, emitUint16Operand/emit3 assert that they are
    // not used to unconditionally emit JSOP_GETLOCAL. Variable access should
    // instead be emitted using EmitVarOp. In special cases, when the caller
    // definitely knows that a given local slot is unaliased, this function may be
    // used as a non-asserting version of emitUint16Operand.
    MOZ_MUST_USE bool emitLocalOp(JSOp op, uint32_t slot);

    MOZ_MUST_USE bool emitScopeCoordOp(JSOp op, ScopeCoordinate sc);
    MOZ_MUST_USE bool emitAliasedVarOp(JSOp op, ParseNode* pn);
    MOZ_MUST_USE bool emitAliasedVarOp(JSOp op, ScopeCoordinate sc, MaybeCheckLexical checkLexical);
    MOZ_MUST_USE bool emitUnaliasedVarOp(JSOp op, uint32_t slot, MaybeCheckLexical checkLexical);

    MOZ_MUST_USE bool emitVarOp(ParseNode* pn, JSOp op);
    MOZ_MUST_USE bool emitVarIncDec(ParseNode* pn);

    MOZ_MUST_USE bool emitNameOp(ParseNode* pn, bool callContext);
    MOZ_MUST_USE bool emitNameIncDec(ParseNode* pn);

    MOZ_MUST_USE bool maybeEmitVarDecl(JSOp prologueOp, ParseNode* pn, jsatomid* result);
    MOZ_MUST_USE bool emitVariables(ParseNode* pn, VarEmitOption emitOption);
    MOZ_MUST_USE bool emitSingleVariable(ParseNode* pn, ParseNode* binding, ParseNode* initializer,
                                         VarEmitOption emitOption);

    MOZ_MUST_USE bool emitNewInit(JSProtoKey key);
    MOZ_MUST_USE bool emitSingletonInitialiser(ParseNode* pn);

    MOZ_MUST_USE bool emitPrepareIteratorResult();
    MOZ_MUST_USE bool emitFinishIteratorResult(bool done);
    MOZ_MUST_USE bool iteratorResultShape(unsigned* shape);

    MOZ_MUST_USE bool emitYield(ParseNode* pn);
    MOZ_MUST_USE bool emitYieldOp(JSOp op);
    MOZ_MUST_USE bool emitYieldStar(ParseNode* iter, ParseNode* gen);

    MOZ_MUST_USE bool emitPropLHS(ParseNode* pn);
    MOZ_MUST_USE bool emitPropOp(ParseNode* pn, JSOp op);
    MOZ_MUST_USE bool emitPropIncDec(ParseNode* pn);

    MOZ_MUST_USE bool emitComputedPropertyName(ParseNode* computedPropName);

    // Emit bytecode to put operands for a JSOP_GETELEM/CALLELEM/SETELEM/DELELEM
    // opcode onto the stack in the right order. In the case of SETELEM, the
    // value to be assigned must already be pushed.
    enum class EmitElemOption { Get, Set, Call, IncDec, CompoundAssign };
    MOZ_MUST_USE bool emitElemOperands(ParseNode* pn, EmitElemOption opts);

    MOZ_MUST_USE bool emitElemOpBase(JSOp op);
    MOZ_MUST_USE bool emitElemOp(ParseNode* pn, JSOp op);
    MOZ_MUST_USE bool emitElemIncDec(ParseNode* pn);

    MOZ_MUST_USE bool emitCatch(ParseNode* pn);
    MOZ_MUST_USE bool emitIf(ParseNode* pn);
    MOZ_MUST_USE bool emitWith(ParseNode* pn);

    MOZ_NEVER_INLINE MOZ_MUST_USE bool emitLabeledStatement(const LabeledStatement* pn);
    MOZ_NEVER_INLINE MOZ_MUST_USE bool emitLetBlock(ParseNode* pnLet);
    MOZ_NEVER_INLINE MOZ_MUST_USE bool emitLexicalScope(ParseNode* pn);
    MOZ_NEVER_INLINE MOZ_MUST_USE bool emitSwitch(ParseNode* pn);
    MOZ_NEVER_INLINE MOZ_MUST_USE bool emitTry(ParseNode* pn);

    // EmitDestructuringLHS assumes the to-be-destructured value has been pushed on
    // the stack and emits code to destructure a single lhs expression (either a
    // name or a compound []/{} expression).
    //
    // If emitOption is InitializeVars, the to-be-destructured value is assigned to
    // locals and ultimately the initial slot is popped (-1 total depth change).
    //
    // If emitOption is PushInitialValues, the to-be-destructured value is replaced
    // with the initial values of the N (where 0 <= N) variables assigned in the
    // lhs expression. (Same post-condition as EmitDestructuringOpsHelper)
    MOZ_MUST_USE bool emitDestructuringLHS(ParseNode* target, VarEmitOption emitOption);

    MOZ_MUST_USE bool emitDestructuringOps(ParseNode* pattern, bool isLet = false);
    MOZ_MUST_USE bool emitDestructuringOpsHelper(ParseNode* pattern, VarEmitOption emitOption);
    MOZ_MUST_USE bool emitDestructuringOpsArrayHelper(ParseNode* pattern, VarEmitOption emitOption);
    MOZ_MUST_USE bool emitDestructuringOpsObjectHelper(ParseNode* pattern,
                                                       VarEmitOption emitOption);

    typedef bool
    (*DestructuringDeclEmitter)(BytecodeEmitter* bce, JSOp prologueOp, ParseNode* pn);

    template <DestructuringDeclEmitter EmitName>
    MOZ_MUST_USE bool emitDestructuringDeclsWithEmitter(JSOp prologueOp, ParseNode* pattern);

    MOZ_MUST_USE bool emitDestructuringDecls(JSOp prologueOp, ParseNode* pattern);

    // Emit code to initialize all destructured names to the value on the top of
    // the stack.
    MOZ_MUST_USE bool emitInitializeDestructuringDecls(JSOp prologueOp, ParseNode* pattern);

    // Throw a TypeError if the value atop the stack isn't convertible to an
    // object, with no overall effect on the stack.
    MOZ_MUST_USE bool emitRequireObjectCoercible();

    // emitIterator expects the iterable to already be on the stack.
    // It will replace that stack value with the corresponding iterator
    MOZ_MUST_USE bool emitIterator();

    // Pops iterator from the top of the stack. Pushes the result of |.next()|
    // onto the stack.
    MOZ_MUST_USE bool emitIteratorNext(ParseNode* pn, bool allowSelfHosted = false);

    // Check if the value on top of the stack is "undefined". If so, replace
    // that value on the stack with the value defined by |defaultExpr|.
    MOZ_MUST_USE bool emitDefault(ParseNode* defaultExpr);

    MOZ_MUST_USE bool emitCallSiteObject(ParseNode* pn);
    MOZ_MUST_USE bool emitTemplateString(ParseNode* pn);
    MOZ_MUST_USE bool emitAssignment(ParseNode* lhs, JSOp op, ParseNode* rhs);

    MOZ_MUST_USE bool emitReturn(ParseNode* pn);
    MOZ_MUST_USE bool emitStatement(ParseNode* pn);
    MOZ_MUST_USE bool emitStatementList(ParseNode* pn);

    MOZ_MUST_USE bool emitDeleteName(ParseNode* pn);
    MOZ_MUST_USE bool emitDeleteProperty(ParseNode* pn);
    MOZ_MUST_USE bool emitDeleteElement(ParseNode* pn);
    MOZ_MUST_USE bool emitDeleteExpression(ParseNode* pn);

    // |op| must be JSOP_TYPEOF or JSOP_TYPEOFEXPR.
    MOZ_MUST_USE bool emitTypeof(ParseNode* node, JSOp op);

    MOZ_MUST_USE bool emitUnary(ParseNode* pn);
    MOZ_MUST_USE bool emitRightAssociative(ParseNode* pn);
    MOZ_MUST_USE bool emitLeftAssociative(ParseNode* pn);
    MOZ_MUST_USE bool emitLogical(ParseNode* pn);
    MOZ_MUST_USE bool emitSequenceExpr(ParseNode* pn);

    MOZ_NEVER_INLINE MOZ_MUST_USE bool emitIncOrDec(ParseNode* pn);

    MOZ_MUST_USE bool emitConditionalExpression(ConditionalExpression& conditional);

    MOZ_MUST_USE bool isRestParameter(ParseNode* pn, bool* result);
    MOZ_MUST_USE bool emitOptimizeSpread(ParseNode* arg0, JumpList* jmp, bool* emitted);

    MOZ_MUST_USE bool emitCallOrNew(ParseNode* pn);
    MOZ_MUST_USE bool emitDebugOnlyCheckSelfHosted();
    MOZ_MUST_USE bool emitSelfHostedCallFunction(ParseNode* pn);
    MOZ_MUST_USE bool emitSelfHostedResumeGenerator(ParseNode* pn);
    MOZ_MUST_USE bool emitSelfHostedForceInterpreter(ParseNode* pn);
    MOZ_MUST_USE bool emitSelfHostedAllowContentSpread(ParseNode* pn);

    MOZ_MUST_USE bool emitComprehensionFor(ParseNode* compFor);
    MOZ_MUST_USE bool emitComprehensionForIn(ParseNode* pn);
    MOZ_MUST_USE bool emitComprehensionForInOrOfVariables(ParseNode* pn, bool* letBlockScope);
    MOZ_MUST_USE bool emitComprehensionForOf(ParseNode* pn);

    MOZ_MUST_USE bool emitDo(ParseNode* pn);
    MOZ_MUST_USE bool emitFor(ParseNode* pn);
    MOZ_MUST_USE bool emitForIn(ParseNode* pn);
    MOZ_MUST_USE bool emitForInOrOfVariables(ParseNode* pn);
    MOZ_MUST_USE bool emitCStyleFor(ParseNode* pn);
    MOZ_MUST_USE bool emitWhile(ParseNode* pn);

    MOZ_MUST_USE bool emitBreak(PropertyName* label);
    MOZ_MUST_USE bool emitContinue(PropertyName* label);

    MOZ_MUST_USE bool emitArgsBody(ParseNode* pn);
    MOZ_MUST_USE bool emitDefaultsAndDestructuring(ParseNode* pn);
    MOZ_MUST_USE bool emitLexicalInitialization(ParseNode* pn, JSOp globalDefOp);

    MOZ_MUST_USE bool pushInitialConstants(JSOp op, unsigned n);
    MOZ_MUST_USE bool initializeBlockScopedLocalsFromStack(Handle<StaticBlockScope*> blockScope);

    // Emit bytecode for the spread operator.
    //
    // emitSpread expects the current index (I) of the array, the array itself
    // and the iterator to be on the stack in that order (iterator on the bottom).
    // It will pop the iterator and I, then iterate over the iterator by calling
    // |.next()| and put the results into the I-th element of array with
    // incrementing I, then push the result I (it will be original I +
    // iteration count). The stack after iteration will look like |ARRAY INDEX|.
    MOZ_MUST_USE bool emitSpread(bool allowSelfHosted = false);

    // Emit bytecode for a for-of loop.  pn should be PNK_FOR, and pn->pn_left
    // should be PNK_FOROF.
    MOZ_MUST_USE bool emitForOf(ParseNode* pn);

    MOZ_MUST_USE bool emitClass(ParseNode* pn);
    MOZ_MUST_USE bool emitSuperPropLHS(ParseNode* superBase, bool isCall = false);
    MOZ_MUST_USE bool emitSuperPropOp(ParseNode* pn, JSOp op, bool isCall = false);
    MOZ_MUST_USE bool emitSuperElemOperands(ParseNode* pn,
                                            EmitElemOption opts = EmitElemOption::Get);
    MOZ_MUST_USE bool emitSuperElemOp(ParseNode* pn, JSOp op, bool isCall = false);
};

} /* namespace frontend */
} /* namespace js */

#endif /* frontend_BytecodeEmitter_h */
