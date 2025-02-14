/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "V8Initializer.h"

#include "BindingState.h"
#include "DOMWindow.h"
#include "Document.h"
#include "Frame.h"
#include "MemoryUsageSupport.h"
#include "ScriptCallStack.h"
#include "ScriptCallStackFactory.h"
#include "ScriptProfiler.h"
#include "V8Binding.h"
#include "V8DOMWindow.h"
#include "V8GCController.h"
#include "V8History.h"
#include "V8Location.h"
#include <v8.h>
#include <wtf/RefPtr.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

static Frame* findFrame(v8::Local<v8::Object> host, v8::Local<v8::Value> data)
{
    WrapperTypeInfo* type = WrapperTypeInfo::unwrap(data);

    if (V8DOMWindow::info.equals(type)) {
        v8::Handle<v8::Object> windowWrapper = V8DOMWrapper::lookupDOMWrapper(V8DOMWindow::GetTemplate(), host);
        if (windowWrapper.IsEmpty())
            return 0;
        return V8DOMWindow::toNative(windowWrapper)->frame();
    }

    if (V8History::info.equals(type))
        return V8History::toNative(host)->frame();

    if (V8Location::info.equals(type))
        return V8Location::toNative(host)->frame();

    // This function can handle only those types listed above.
    ASSERT_NOT_REACHED();
    return 0;
}

static void reportFatalError(const char* location, const char* message)
{
    int memoryUsageMB = MemoryUsageSupport::actualMemoryUsageMB();
    printf("V8 error: %s (%s).  Current memory usage: %d MB\n", message, location, memoryUsageMB);
    CRASH();
}

static void reportUncaughtException(v8::Handle<v8::Message> message, v8::Handle<v8::Value> data)
{
    DOMWindow* firstWindow = firstDOMWindow(BindingState::instance());
    if (!firstWindow->isCurrentlyDisplayedInFrame())
        return;

    String errorMessage = toWebCoreString(message->Get());

    v8::Handle<v8::StackTrace> stackTrace = message->GetStackTrace();
    RefPtr<ScriptCallStack> callStack;
    // Currently stack trace is only collected when inspector is open.
    if (!stackTrace.IsEmpty() && stackTrace->GetFrameCount() > 0)
        callStack = createScriptCallStack(stackTrace, ScriptCallStack::maxCallStackSizeToCapture);

    v8::Handle<v8::Value> resourceName = message->GetScriptResourceName();
    bool shouldUseDocumentURL = resourceName.IsEmpty() || !resourceName->IsString();
    String resource = shouldUseDocumentURL ? firstWindow->document()->url() : toWebCoreString(resourceName);
    firstWindow->document()->reportException(errorMessage, message->GetLineNumber(), resource, callStack);
}

static void reportUnsafeJavaScriptAccess(v8::Local<v8::Object> host, v8::AccessType type, v8::Local<v8::Value> data)
{
    Frame* target = findFrame(host, data);
    if (!target)
        return;
    DOMWindow* targetWindow = target->document()->domWindow();
    targetWindow->printErrorMessage(targetWindow->crossDomainAccessErrorMessage(activeDOMWindow(BindingState::instance())));
}

void V8Initializer::initializeMainThreadIfNeeded()
{
    ASSERT(isMainThread());

    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

    v8::V8::IgnoreOutOfMemoryException();
    v8::V8::SetFatalErrorHandler(reportFatalError);
    v8::V8::AddGCPrologueCallback(&V8GCController::gcPrologue);
    v8::V8::AddGCEpilogueCallback(&V8GCController::gcEpilogue);
    v8::V8::AddMessageListener(&reportUncaughtException);
    v8::V8::SetFailedAccessCheckCallbackFunction(reportUnsafeJavaScriptAccess);
#if ENABLE(JAVASCRIPT_DEBUGGER)
    ScriptProfiler::initialize();
#endif
    V8PerIsolateData::ensureInitialized(v8::Isolate::GetCurrent());

    // FIXME: Remove the following 2 lines when V8 default has changed.
    const char es5ReadonlyFlag[] = "--es5_readonly";
    v8::V8::SetFlagsFromString(es5ReadonlyFlag, sizeof(es5ReadonlyFlag));
}

} // namespace WebCore
