/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * JS execution context.
 */

#include "jscntxtinlines.h"

#include "mozilla/ArrayUtils.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/MemoryReporting.h"

#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#ifdef ANDROID
# include <android/log.h>
# include <fstream>
# include <string>
#endif  // ANDROID

#include "jsatom.h"
#include "jscompartment.h"
#include "jsexn.h"
#include "jsfun.h"
#include "jsgc.h"
#include "jsiter.h"
#include "jsobj.h"
#include "jsopcode.h"
#include "jsprf.h"
#include "jspubtd.h"
#include "jsscript.h"
#include "jsstr.h"
#include "jstypes.h"
#include "jswatchpoint.h"

#include "gc/Marking.h"
#include "jit/Ion.h"
#include "js/CharacterEncoding.h"
#include "vm/HelperThreads.h"
#include "vm/Shape.h"

#include "jsobjinlines.h"
#include "jsscriptinlines.h"

#include "vm/Stack-inl.h"

using namespace js;
using namespace js::gc;

using mozilla::DebugOnly;
using mozilla::PodArrayZero;
using mozilla::PointerRangeSize;

bool
js::AutoCycleDetector::init()
{
    AutoCycleDetector::Set& set = cx->cycleDetectorSet;
    hashsetAddPointer = set.lookupForAdd(obj);
    if (!hashsetAddPointer) {
        if (!set.add(hashsetAddPointer, obj))
            return false;
        cyclic = false;
        hashsetGenerationAtInit = set.generation();
    }
    return true;
}

js::AutoCycleDetector::~AutoCycleDetector()
{
    if (!cyclic) {
        if (hashsetGenerationAtInit == cx->cycleDetectorSet.generation())
            cx->cycleDetectorSet.remove(hashsetAddPointer);
        else
            cx->cycleDetectorSet.remove(obj);
    }
}

void
js::TraceCycleDetectionSet(JSTracer* trc, AutoCycleDetector::Set& set)
{
    for (AutoCycleDetector::Set::Enum e(set); !e.empty(); e.popFront())
        TraceRoot(trc, &e.mutableFront(), "cycle detector table entry");
}

bool
JSContext::init(uint32_t maxBytes, uint32_t maxNurseryBytes)
{
    if (!JSRuntime::init(maxBytes, maxNurseryBytes))
        return false;

    if (!caches.init())
        return false;

    return true;
}

JSContext*
js::NewContext(uint32_t maxBytes, uint32_t maxNurseryBytes, JSRuntime* parentRuntime)
{
    JSContext* cx = js_new<JSContext>(parentRuntime);
    if (!cx)
        return nullptr;

    if (!cx->init(maxBytes, maxNurseryBytes)) {
        js_delete(cx);
        return nullptr;
    }

    return cx;
}

void
js::DestroyContext(JSContext* cx)
{
    JS_AbortIfWrongThread(cx);

    if (cx->outstandingRequests != 0)
        MOZ_CRASH("Attempted to destroy a context while it is in a request.");

    cx->roots.checkNoGCRooters();
    cx->roots.finishPersistentRoots();

    /*
     * Dump remaining type inference results while we still have a context.
     * This printing depends on atoms still existing.
     */
    for (CompartmentsIter c(cx, SkipAtoms); !c.done(); c.next())
        PrintTypes(cx, c, false);

    js_delete_poison(cx);
}

void
RootLists::checkNoGCRooters() {
#ifdef DEBUG
    for (auto const& stackRootPtr : stackRoots_) {
        MOZ_ASSERT(stackRootPtr == nullptr);
    }
#endif
}

bool
AutoResolving::alreadyStartedSlow() const
{
    MOZ_ASSERT(link);
    AutoResolving* cursor = link;
    do {
        MOZ_ASSERT(this != cursor);
        if (object.get() == cursor->object && id.get() == cursor->id && kind == cursor->kind)
            return true;
    } while (!!(cursor = cursor->link));
    return false;
}

static void
ReportError(JSContext* cx, const char* message, JSErrorReport* reportp,
            JSErrorCallback callback, void* userRef)
{
    /*
     * Check the error report, and set a JavaScript-catchable exception
     * if the error is defined to have an associated exception.  If an
     * exception is thrown, then the JSREPORT_EXCEPTION flag will be set
     * on the error report, and exception-aware hosts should ignore it.
     */
    MOZ_ASSERT(reportp);
    if ((!callback || callback == GetErrorMessage) &&
        reportp->errorNumber == JSMSG_UNCAUGHT_EXCEPTION)
    {
        reportp->flags |= JSREPORT_EXCEPTION;
    }

    if (JSREPORT_IS_WARNING(reportp->flags)) {
        CallWarningReporter(cx, message, reportp);
        return;
    }

    ErrorToException(cx, message, reportp, callback, userRef);
}

/*
 * The given JSErrorReport object have been zeroed and must not outlive
 * cx->fp() (otherwise owned fields may become invalid).
 */
static void
PopulateReportBlame(JSContext* cx, JSErrorReport* report)
{
    JSCompartment* compartment = cx->compartment();
    if (!compartment)
        return;

    /*
     * Walk stack until we find a frame that is associated with a non-builtin
     * rather than a builtin frame and which we're allowed to know about.
     */
    NonBuiltinFrameIter iter(cx, compartment->principals());
    if (iter.done())
        return;

    report->filename = iter.filename();
    report->lineno = iter.computeLine(&report->column);
    // XXX: Make the column 1-based as in other browsers, instead of 0-based
    // which is how SpiderMonkey stores it internally. This will be
    // unnecessary once bug 1144340 is fixed.
    report->column++;
    report->isMuted = iter.mutedErrors();
}

/*
 * Since memory has been exhausted, avoid the normal error-handling path which
 * allocates an error object, report and callstack. If code is running, simply
 * throw the static atom "out of memory". If code is not running, call the
 * error reporter directly.
 *
 * Furthermore, callers of ReportOutOfMemory (viz., malloc) assume a GC does
 * not occur, so GC must be avoided or suppressed.
 */
void
js::ReportOutOfMemory(ExclusiveContext* cxArg)
{
#ifdef JS_MORE_DETERMINISTIC
    /*
     * OOMs are non-deterministic, especially across different execution modes
     * (e.g. interpreter vs JIT). In more-deterministic builds, print to stderr
     * so that the fuzzers can detect this.
     */
    fprintf(stderr, "ReportOutOfMemory called\n");
#endif

    if (!cxArg->isJSContext())
        return cxArg->addPendingOutOfMemory();

    JSContext* cx = cxArg->asJSContext();
    cx->runtime()->hadOutOfMemory = true;
    AutoSuppressGC suppressGC(cx);

    /* Report the oom. */
    if (JS::OutOfMemoryCallback oomCallback = cx->runtime()->oomCallback)
        oomCallback(cx, cx->runtime()->oomCallbackData);

    cx->setPendingException(StringValue(cx->names().outOfMemory));
}

void
js::ReportOverRecursed(JSContext* maybecx, unsigned errorNumber)
{
#ifdef JS_MORE_DETERMINISTIC
    /*
     * We cannot make stack depth deterministic across different
     * implementations (e.g. JIT vs. interpreter will differ in
     * their maximum stack depth).
     * However, we can detect externally when we hit the maximum
     * stack depth which is useful for external testing programs
     * like fuzzers.
     */
    fprintf(stderr, "ReportOverRecursed called\n");
#endif
    if (maybecx) {
        JS_ReportErrorNumber(maybecx, GetErrorMessage, nullptr, errorNumber);
        maybecx->overRecursed_ = true;
    }
}

JS_FRIEND_API(void)
js::ReportOverRecursed(JSContext* maybecx)
{
    ReportOverRecursed(maybecx, JSMSG_OVER_RECURSED);
}

void
js::ReportOverRecursed(ExclusiveContext* cx)
{
    if (cx->isJSContext())
        ReportOverRecursed(cx->asJSContext());
    else
        cx->addPendingOverRecursed();
}

void
js::ReportAllocationOverflow(ExclusiveContext* cxArg)
{
    if (!cxArg)
        return;

    if (!cxArg->isJSContext())
        return;
    JSContext* cx = cxArg->asJSContext();

    AutoSuppressGC suppressGC(cx);
    JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_ALLOC_OVERFLOW);
}

/*
 * Given flags and the state of cx, decide whether we should report an
 * error, a warning, or just continue execution normally.  Return
 * true if we should continue normally, without reporting anything;
 * otherwise, adjust *flags as appropriate and return false.
 */
static bool
checkReportFlags(JSContext* cx, unsigned* flags)
{
    if (JSREPORT_IS_STRICT_MODE_ERROR(*flags)) {
        /*
         * Error in strict code; warning with extra warnings option; okay
         * otherwise.  We assume that if the top frame is a native, then it is
         * strict if the nearest scripted frame is strict, see bug 536306.
         */
        jsbytecode* pc;
        JSScript* script = cx->currentScript(&pc);
        if (script && IsCheckStrictOp(JSOp(*pc)))
            *flags &= ~JSREPORT_WARNING;
        else if (cx->compartment()->behaviors().extraWarnings(cx))
            *flags |= JSREPORT_WARNING;
        else
            return true;
    } else if (JSREPORT_IS_STRICT(*flags)) {
        /* Warning/error only when JSOPTION_STRICT is set. */
        if (!cx->compartment()->behaviors().extraWarnings(cx))
            return true;
    }

    /* Warnings become errors when JSOPTION_WERROR is set. */
    if (JSREPORT_IS_WARNING(*flags) && cx->options().werror())
        *flags &= ~JSREPORT_WARNING;

    return false;
}

bool
js::ReportErrorVA(JSContext* cx, unsigned flags, const char* format, va_list ap)
{
    char* message;
    char16_t* ucmessage;
    size_t messagelen;
    JSErrorReport report;
    bool warning;

    if (checkReportFlags(cx, &flags))
        return true;

    message = JS_vsmprintf(format, ap);
    if (!message) {
        ReportOutOfMemory(cx);
        return false;
    }
    messagelen = strlen(message);

    report.flags = flags;
    report.errorNumber = JSMSG_USER_DEFINED_ERROR;
    report.ucmessage = ucmessage = InflateString(cx, message, &messagelen);
    PopulateReportBlame(cx, &report);

    warning = JSREPORT_IS_WARNING(report.flags);

    ReportError(cx, message, &report, nullptr, nullptr);
    js_free(message);
    js_free(ucmessage);
    return warning;
}

/* |callee| requires a usage string provided by JS_DefineFunctionsWithHelp. */
void
js::ReportUsageError(JSContext* cx, HandleObject callee, const char* msg)
{
    const char* usageStr = "usage";
    PropertyName* usageAtom = Atomize(cx, usageStr, strlen(usageStr))->asPropertyName();
    RootedId id(cx, NameToId(usageAtom));
    DebugOnly<Shape*> shape = static_cast<Shape*>(callee->as<JSFunction>().lookup(cx, id));
    MOZ_ASSERT(!shape->configurable());
    MOZ_ASSERT(!shape->writable());
    MOZ_ASSERT(shape->hasDefaultGetter());

    RootedValue usage(cx);
    if (!JS_GetProperty(cx, callee, "usage", &usage))
        return;

    if (!usage.isString()) {
        JS_ReportError(cx, "%s", msg);
    } else {
        JSString* str = usage.toString();
        if (!str->ensureFlat(cx))
            return;
        AutoStableStringChars chars(cx);
        if (!chars.initTwoByte(cx, str))
            return;

        JS_ReportError(cx, "%s. Usage: %hs", msg, chars.twoByteRange().start().get());
    }
}

bool
js::PrintError(JSContext* cx, FILE* file, const char* message, JSErrorReport* report,
               bool reportWarnings)
{
    if (!report) {
        fprintf(file, "%s\n", message);
        fflush(file);
        return false;
    }

    /* Conditionally ignore reported warnings. */
    if (JSREPORT_IS_WARNING(report->flags) && !reportWarnings)
        return false;

    char* prefix = nullptr;
    if (report->filename)
        prefix = JS_smprintf("%s:", report->filename);
    if (report->lineno) {
        char* tmp = prefix;
        prefix = JS_smprintf("%s%u:%u ", tmp ? tmp : "", report->lineno, report->column);
        JS_free(cx, tmp);
    }
    if (JSREPORT_IS_WARNING(report->flags)) {
        char* tmp = prefix;
        prefix = JS_smprintf("%s%swarning: ",
                             tmp ? tmp : "",
                             JSREPORT_IS_STRICT(report->flags) ? "strict " : "");
        JS_free(cx, tmp);
    }

    /* embedded newlines -- argh! */
    const char* ctmp;
    while ((ctmp = strchr(message, '\n')) != 0) {
        ctmp++;
        if (prefix)
            fputs(prefix, file);
        fwrite(message, 1, ctmp - message, file);
        message = ctmp;
    }

    /* If there were no filename or lineno, the prefix might be empty */
    if (prefix)
        fputs(prefix, file);
    fputs(message, file);

    if (const char16_t* linebuf = report->linebuf()) {
        size_t n = report->linebufLength();

        fputs(":\n", file);
        if (prefix)
            fputs(prefix, file);

        for (size_t i = 0; i < n; i++)
            fputc(static_cast<char>(linebuf[i]), file);

        // linebuf usually ends with a newline. If not, add one here.
        if (n == 0 || linebuf[n-1] != '\n')
            fputc('\n', file);

        if (prefix)
            fputs(prefix, file);

        n = report->tokenOffset();
        for (size_t i = 0, j = 0; i < n; i++) {
            if (linebuf[i] == '\t') {
                for (size_t k = (j + 8) & ~7; j < k; j++)
                    fputc('.', file);
                continue;
            }
            fputc('.', file);
            j++;
        }
        fputc('^', file);
    }
    fputc('\n', file);
    fflush(file);
    JS_free(cx, prefix);
    return true;
}

/*
 * The arguments from ap need to be packaged up into an array and stored
 * into the report struct.
 *
 * The format string addressed by the error number may contain operands
 * identified by the format {N}, where N is a decimal digit. Each of these
 * is to be replaced by the Nth argument from the va_list. The complete
 * message is placed into reportp->ucmessage converted to a JSString.
 *
 * Returns true if the expansion succeeds (can fail if out of memory).
 */
bool
js::ExpandErrorArgumentsVA(ExclusiveContext* cx, JSErrorCallback callback,
                           void* userRef, const unsigned errorNumber,
                           char** messagep, ErrorArgumentsType argumentsType,
                           JSErrorReport* reportp, va_list ap)
{
    const JSErrorFormatString* efs;
    uint16_t argCount;
    bool messageArgsPassed = !!reportp->messageArgs;

    *messagep = nullptr;

    if (!callback)
        callback = GetErrorMessage;

    {
        AutoSuppressGC suppressGC(cx);
        efs = callback(userRef, errorNumber);
    }

    if (efs) {
        reportp->exnType = efs->exnType;

        size_t totalArgsLength = 0;
        size_t argLengths[JS::MaxNumErrorArguments]; /* only {0} thru {9} supported */
        argCount = efs->argCount;
        MOZ_RELEASE_ASSERT(argCount <= JS::MaxNumErrorArguments);
        if (argCount > 0) {
            /*
             * Gather the arguments into an array, and accumulate
             * their sizes. We allocate 1 more than necessary and
             * null it out to act as the caboose when we free the
             * pointers later.
             */
            if (messageArgsPassed) {
                MOZ_ASSERT(!reportp->messageArgs[argCount]);
            } else {
                reportp->messageArgs = cx->pod_malloc<const char16_t*>(argCount + 1);
                if (!reportp->messageArgs)
                    return false;
                /* nullptr-terminate for easy copying. */
                reportp->messageArgs[argCount] = nullptr;
            }
            for (uint16_t i = 0; i < argCount; i++) {
                if (messageArgsPassed) {
                    /* Do nothing. */
                } else if (argumentsType == ArgumentsAreASCII) {
                    char* charArg = va_arg(ap, char*);
                    size_t charArgLength = strlen(charArg);
                    reportp->messageArgs[i] = InflateString(cx, charArg, &charArgLength);
                    if (!reportp->messageArgs[i])
                        goto error;
                } else {
                    reportp->messageArgs[i] = va_arg(ap, char16_t*);
                }
                argLengths[i] = js_strlen(reportp->messageArgs[i]);
                totalArgsLength += argLengths[i];
            }
        }
        /*
         * Parse the error format, substituting the argument X
         * for {X} in the format.
         */
        if (argCount > 0) {
            if (efs->format) {
                char16_t* buffer;
                char16_t* fmt;
                char16_t* out;
                int expandedArgs = 0;
                size_t expandedLength;
                size_t len = strlen(efs->format);

                buffer = fmt = InflateString(cx, efs->format, &len);
                if (!buffer)
                    goto error;
                expandedLength = len
                                 - (3 * argCount)       /* exclude the {n} */
                                 + totalArgsLength;

                /*
                * Note - the above calculation assumes that each argument
                * is used once and only once in the expansion !!!
                */
                reportp->ucmessage = out = cx->pod_malloc<char16_t>(expandedLength + 1);
                if (!out) {
                    js_free(buffer);
                    goto error;
                }
                while (*fmt) {
                    if (*fmt == '{') {
                        if (isdigit(fmt[1])) {
                            int d = JS7_UNDEC(fmt[1]);
                            MOZ_RELEASE_ASSERT(d < argCount);
                            js_strncpy(out, reportp->messageArgs[d],
                                       argLengths[d]);
                            out += argLengths[d];
                            fmt += 3;
                            expandedArgs++;
                            continue;
                        }
                    }
                    *out++ = *fmt++;
                }
                MOZ_ASSERT(expandedArgs == argCount);
                *out = 0;
                js_free(buffer);
                size_t msgLen = PointerRangeSize(static_cast<const char16_t*>(reportp->ucmessage),
                                                 static_cast<const char16_t*>(out));
                mozilla::Range<const char16_t> ucmsg(reportp->ucmessage, msgLen);
                *messagep = JS::LossyTwoByteCharsToNewLatin1CharsZ(cx, ucmsg).c_str();
                if (!*messagep)
                    goto error;
            }
        } else {
            /* Non-null messageArgs should have at least one non-null arg. */
            MOZ_ASSERT(!reportp->messageArgs);
            /*
             * Zero arguments: the format string (if it exists) is the
             * entire message.
             */
            if (efs->format) {
                size_t len;
                *messagep = DuplicateString(cx, efs->format).release();
                if (!*messagep)
                    goto error;
                len = strlen(*messagep);
                reportp->ucmessage = InflateString(cx, *messagep, &len);
                if (!reportp->ucmessage)
                    goto error;
            }
        }
    }
    if (*messagep == nullptr) {
        /* where's the right place for this ??? */
        const char* defaultErrorMessage
            = "No error message available for error number %d";
        size_t nbytes = strlen(defaultErrorMessage) + 16;
        *messagep = cx->pod_malloc<char>(nbytes);
        if (!*messagep)
            goto error;
        JS_snprintf(*messagep, nbytes, defaultErrorMessage, errorNumber);
    }
    return true;

error:
    if (!messageArgsPassed && reportp->messageArgs) {
        /* free the arguments only if we allocated them */
        if (argumentsType == ArgumentsAreASCII) {
            uint16_t i = 0;
            while (reportp->messageArgs[i])
                js_free((void*)reportp->messageArgs[i++]);
        }
        js_free((void*)reportp->messageArgs);
        reportp->messageArgs = nullptr;
    }
    if (reportp->ucmessage) {
        js_free((void*)reportp->ucmessage);
        reportp->ucmessage = nullptr;
    }
    if (*messagep) {
        js_free((void*)*messagep);
        *messagep = nullptr;
    }
    return false;
}

bool
js::ReportErrorNumberVA(JSContext* cx, unsigned flags, JSErrorCallback callback,
                        void* userRef, const unsigned errorNumber,
                        ErrorArgumentsType argumentsType, va_list ap)
{
    JSErrorReport report;
    char* message;
    bool warning;

    if (checkReportFlags(cx, &flags))
        return true;
    warning = JSREPORT_IS_WARNING(flags);

    report.flags = flags;
    report.errorNumber = errorNumber;
    PopulateReportBlame(cx, &report);

    if (!ExpandErrorArgumentsVA(cx, callback, userRef, errorNumber,
                                &message, argumentsType, &report, ap)) {
        return false;
    }

    ReportError(cx, message, &report, callback, userRef);

    js_free(message);
    if (report.messageArgs) {
        /*
         * ExpandErrorArgumentsVA owns its messageArgs only if it had to
         * inflate the arguments (from regular |char*|s).
         */
        if (argumentsType == ArgumentsAreASCII) {
            int i = 0;
            while (report.messageArgs[i])
                js_free((void*)report.messageArgs[i++]);
        }
        js_free((void*)report.messageArgs);
    }
    js_free((void*)report.ucmessage);

    return warning;
}

static bool
ExpandErrorArguments(ExclusiveContext* cx, JSErrorCallback callback,
                     void* userRef, const unsigned errorNumber,
                     char** messagep, ErrorArgumentsType argumentsType,
                     JSErrorReport* reportp, ...)
{
    va_list ap;
    va_start(ap, reportp);
    bool expanded = js::ExpandErrorArgumentsVA(cx, callback, userRef, errorNumber,
                                               messagep, argumentsType, reportp, ap);
    va_end(ap);
    return expanded;
}

bool
js::ReportErrorNumberUCArray(JSContext* cx, unsigned flags, JSErrorCallback callback,
                             void* userRef, const unsigned errorNumber,
                             const char16_t** args)
{
    if (checkReportFlags(cx, &flags))
        return true;
    bool warning = JSREPORT_IS_WARNING(flags);

    JSErrorReport report;
    report.flags = flags;
    report.errorNumber = errorNumber;
    PopulateReportBlame(cx, &report);
    report.messageArgs = args;

    char* message;
    if (!ExpandErrorArguments(cx, callback, userRef, errorNumber,
                              &message, ArgumentsAreUnicode, &report)) {
        return false;
    }

    ReportError(cx, message, &report, callback, userRef);

    js_free(message);
    js_free((void*)report.ucmessage);

    return warning;
}

void
js::CallWarningReporter(JSContext* cx, const char* message, JSErrorReport* reportp)
{
    MOZ_ASSERT(message);
    MOZ_ASSERT(reportp);
    MOZ_ASSERT(JSREPORT_IS_WARNING(reportp->flags));

    if (JS::WarningReporter warningReporter = cx->runtime()->warningReporter)
        warningReporter(cx, message, reportp);
}

bool
js::ReportIsNotDefined(JSContext* cx, HandleId id)
{
    JSAutoByteString printable;
    if (ValueToPrintable(cx, IdToValue(id), &printable))
        JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_NOT_DEFINED, printable.ptr());
    return false;
}

bool
js::ReportIsNotDefined(JSContext* cx, HandlePropertyName name)
{
    RootedId id(cx, NameToId(name));
    return ReportIsNotDefined(cx, id);
}

bool
js::ReportIsNullOrUndefined(JSContext* cx, int spindex, HandleValue v,
                            HandleString fallback)
{
    bool ok;

    UniqueChars bytes = DecompileValueGenerator(cx, spindex, v, fallback);
    if (!bytes)
        return false;

    if (strcmp(bytes.get(), js_undefined_str) == 0 ||
        strcmp(bytes.get(), js_null_str) == 0) {
        ok = JS_ReportErrorFlagsAndNumber(cx, JSREPORT_ERROR,
                                          GetErrorMessage, nullptr,
                                          JSMSG_NO_PROPERTIES, bytes.get(),
                                          nullptr, nullptr);
    } else if (v.isUndefined()) {
        ok = JS_ReportErrorFlagsAndNumber(cx, JSREPORT_ERROR,
                                          GetErrorMessage, nullptr,
                                          JSMSG_UNEXPECTED_TYPE, bytes.get(),
                                          js_undefined_str, nullptr);
    } else {
        MOZ_ASSERT(v.isNull());
        ok = JS_ReportErrorFlagsAndNumber(cx, JSREPORT_ERROR,
                                          GetErrorMessage, nullptr,
                                          JSMSG_UNEXPECTED_TYPE, bytes.get(),
                                          js_null_str, nullptr);
    }

    return ok;
}

void
js::ReportMissingArg(JSContext* cx, HandleValue v, unsigned arg)
{
    char argbuf[11];
    UniqueChars bytes;

    JS_snprintf(argbuf, sizeof argbuf, "%u", arg);
    if (IsFunctionObject(v)) {
        RootedAtom name(cx, v.toObject().as<JSFunction>().name());
        bytes = DecompileValueGenerator(cx, JSDVG_SEARCH_STACK, v, name);
        if (!bytes)
            return;
    }
    JS_ReportErrorNumber(cx, GetErrorMessage, nullptr,
                         JSMSG_MISSING_FUN_ARG, argbuf,
                         bytes ? bytes.get() : "");
}

bool
js::ReportValueErrorFlags(JSContext* cx, unsigned flags, const unsigned errorNumber,
                          int spindex, HandleValue v, HandleString fallback,
                          const char* arg1, const char* arg2)
{
    UniqueChars bytes;
    bool ok;

    MOZ_ASSERT(js_ErrorFormatString[errorNumber].argCount >= 1);
    MOZ_ASSERT(js_ErrorFormatString[errorNumber].argCount <= 3);
    bytes = DecompileValueGenerator(cx, spindex, v, fallback);
    if (!bytes)
        return false;

    ok = JS_ReportErrorFlagsAndNumber(cx, flags, GetErrorMessage,
                                      nullptr, errorNumber, bytes.get(), arg1, arg2);
    return ok;
}

const JSErrorFormatString js_ErrorFormatString[JSErr_Limit] = {
#define MSG_DEF(name, count, exception, format) \
    { #name, format, count, exception } ,
#include "js.msg"
#undef MSG_DEF
};

JS_FRIEND_API(const JSErrorFormatString*)
js::GetErrorMessage(void* userRef, const unsigned errorNumber)
{
    if (errorNumber > 0 && errorNumber < JSErr_Limit)
        return &js_ErrorFormatString[errorNumber];
    return nullptr;
}

ExclusiveContext::ExclusiveContext(JSRuntime* rt, PerThreadData* pt, ContextKind kind,
                                   const JS::ContextOptions& options)
  : ContextFriendFields(rt),
    helperThread_(nullptr),
    contextKind_(kind),
    options_(options),
    perThreadData(pt),
    arenas_(nullptr),
    enterCompartmentDepth_(0)
{
}

void
ExclusiveContext::recoverFromOutOfMemory()
{
    if (JSContext* maybecx = maybeJSContext()) {
        if (maybecx->isExceptionPending()) {
            MOZ_ASSERT(maybecx->isThrowingOutOfMemory());
            maybecx->clearPendingException();
        }
        return;
    }
    // Keep in sync with addPendingOutOfMemory.
    if (ParseTask* task = helperThread()->parseTask())
        task->outOfMemory = false;
}

JSContext::JSContext(JSRuntime* parentRuntime)
  : ExclusiveContext(this, &this->JSRuntime::mainThread, Context_JS, JS::ContextOptions()),
    JSRuntime(parentRuntime),
    throwing(false),
    unwrappedException_(this),
    overRecursed_(false),
    propagatingForcedReturn_(false),
    liveVolatileJitFrameIterators_(nullptr),
    reportGranularity(JS_DEFAULT_JITREPORT_GRANULARITY),
    resolvingList(nullptr),
    generatingError(false),
    cycleDetectorSet(this),
    outstandingRequests(0),
    jitIsBroken(false)
{
    MOZ_ASSERT(static_cast<ContextFriendFields*>(this) ==
               ContextFriendFields::get(this));
}

JSContext::~JSContext()
{
    destroyRuntime();

    /* Free the stuff hanging off of cx. */
    MOZ_ASSERT(!resolvingList);
}

bool
JSContext::getPendingException(MutableHandleValue rval)
{
    MOZ_ASSERT(throwing);
    rval.set(unwrappedException_);
    if (IsAtomsCompartment(compartment()))
        return true;
    bool wasOverRecursed = overRecursed_;
    clearPendingException();
    if (!compartment()->wrap(this, rval))
        return false;
    assertSameCompartment(this, rval);
    setPendingException(rval);
    overRecursed_ = wasOverRecursed;
    return true;
}

bool
JSContext::isThrowingOutOfMemory()
{
    return throwing && unwrappedException_ == StringValue(names().outOfMemory);
}

bool
JSContext::isClosingGenerator()
{
    return throwing && unwrappedException_.isMagic(JS_GENERATOR_CLOSING);
}

bool
JSContext::isThrowingDebuggeeWouldRun()
{
    return throwing &&
           unwrappedException_.isObject() &&
           unwrappedException_.toObject().is<ErrorObject>() &&
           unwrappedException_.toObject().as<ErrorObject>().type() == JSEXN_DEBUGGEEWOULDRUN;
}

bool
JSContext::currentlyRunning() const
{
    return !!activation();
}

static bool
ComputeIsJITBroken()
{
#if !defined(ANDROID)
    return false;
#else  // ANDROID
    if (getenv("JS_IGNORE_JIT_BROKENNESS")) {
        return false;
    }

    std::string line;

    // Check for the known-bad kernel version (2.6.29).
    std::ifstream osrelease("/proc/sys/kernel/osrelease");
    std::getline(osrelease, line);
    __android_log_print(ANDROID_LOG_INFO, "Gecko", "Detected osrelease `%s'",
                        line.c_str());

    if (line.npos == line.find("2.6.29")) {
        // We're using something other than 2.6.29, so the JITs should work.
        __android_log_print(ANDROID_LOG_INFO, "Gecko", "JITs are not broken");
        return false;
    }

    // We're using 2.6.29, and this causes trouble with the JITs on i9000.
    line = "";
    bool broken = false;
    std::ifstream cpuinfo("/proc/cpuinfo");
    do {
        if (0 == line.find("Hardware")) {
            static const char* const blacklist[] = {
                "SCH-I400",     // Samsung Continuum
                "SGH-T959",     // Samsung i9000, Vibrant device
                "SGH-I897",     // Samsung i9000, Captivate device
                "SCH-I500",     // Samsung i9000, Fascinate device
                "SPH-D700",     // Samsung i9000, Epic device
                "GT-I9000",     // Samsung i9000, UK/Europe device
                nullptr
            };
            for (const char* const* hw = &blacklist[0]; *hw; ++hw) {
                if (line.npos != line.find(*hw)) {
                    __android_log_print(ANDROID_LOG_INFO, "Gecko",
                                        "Blacklisted device `%s'", *hw);
                    broken = true;
                    break;
                }
            }
            break;
        }
        std::getline(cpuinfo, line);
    } while(!cpuinfo.fail() && !cpuinfo.eof());

    __android_log_print(ANDROID_LOG_INFO, "Gecko", "JITs are %sbroken",
                        broken ? "" : "not ");

    return broken;
#endif  // ifndef ANDROID
}

static bool
IsJITBrokenHere()
{
    static bool computedIsBroken = false;
    static bool isBroken = false;
    if (!computedIsBroken) {
        isBroken = ComputeIsJITBroken();
        computedIsBroken = true;
    }
    return isBroken;
}

void
JSContext::updateJITEnabled()
{
    jitIsBroken = IsJITBrokenHere();
}

size_t
JSContext::sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const
{
    /*
     * There are other JSContext members that could be measured; the following
     * ones have been found by DMD to be worth measuring.  More stuff may be
     * added later.
     */
    return cycleDetectorSet.sizeOfExcludingThis(mallocSizeOf);
}

void
JSContext::mark(JSTracer* trc)
{
    if (cycleDetectorSet.initialized())
        TraceCycleDetectionSet(trc, cycleDetectorSet);

    if (compartment_)
        compartment_->mark();
}

void*
ExclusiveContext::stackLimitAddressForJitCode(StackKind kind)
{
#ifdef JS_SIMULATOR
    return runtime_->addressOfSimulatorStackLimit();
#else
    return stackLimitAddress(kind);
#endif
}

JSVersion
JSContext::findVersion() const
{
    if (JSScript* script = currentScript(nullptr, ALLOW_CROSS_COMPARTMENT))
        return script->getVersion();

    if (compartment() && compartment()->behaviors().version() != JSVERSION_UNKNOWN)
        return compartment()->behaviors().version();

    return defaultVersion();
}

#ifdef DEBUG

JS::AutoCheckRequestDepth::AutoCheckRequestDepth(JSContext* cx)
    : cx(cx)
{
    MOZ_ASSERT(cx->runtime()->requestDepth || cx->runtime()->isHeapBusy());
    MOZ_ASSERT(CurrentThreadCanAccessRuntime(cx->runtime()));
    cx->runtime()->checkRequestDepth++;
}

JS::AutoCheckRequestDepth::AutoCheckRequestDepth(ContextFriendFields* cxArg)
    : cx(static_cast<ExclusiveContext*>(cxArg)->maybeJSContext())
{
    if (cx) {
        MOZ_ASSERT(cx->runtime()->requestDepth || cx->runtime()->isHeapBusy());
        MOZ_ASSERT(CurrentThreadCanAccessRuntime(cx->runtime()));
        cx->runtime()->checkRequestDepth++;
    }
}

JS::AutoCheckRequestDepth::~AutoCheckRequestDepth()
{
    if (cx) {
        MOZ_ASSERT(cx->runtime()->checkRequestDepth != 0);
        cx->runtime()->checkRequestDepth--;
    }
}

#endif

#ifdef JS_CRASH_DIAGNOSTICS
void
CompartmentChecker::check(InterpreterFrame* fp)
{
    if (fp)
        check(fp->scopeChain());
}

void
CompartmentChecker::check(AbstractFramePtr frame)
{
    if (frame)
        check(frame.scopeChain());
}
#endif

void
AutoEnterOOMUnsafeRegion::crash(const char* reason)
{
    char msgbuf[1024];
    JS_snprintf(msgbuf, sizeof(msgbuf), "[unhandlable oom] %s", reason);
    MOZ_ReportAssertionFailure(msgbuf, __FILE__, __LINE__);
    MOZ_CRASH();
}

AutoEnterOOMUnsafeRegion::AnnotateOOMAllocationSizeCallback
AutoEnterOOMUnsafeRegion::annotateOOMSizeCallback = nullptr;

void
AutoEnterOOMUnsafeRegion::crash(size_t size, const char* reason)
{
    {
        JS::AutoSuppressGCAnalysis suppress;
        if (annotateOOMSizeCallback)
            annotateOOMSizeCallback(size);
    }
    crash(reason);
}
