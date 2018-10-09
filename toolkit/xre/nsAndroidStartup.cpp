/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <android/log.h>

#include <jni.h>

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "nsTArray.h"
#include "nsString.h"
#include "nsIFile.h"
#include "nsAppRunner.h"
#include "AndroidBridge.h"
#include "APKOpen.h"

#define LOG(args...) __android_log_print(ANDROID_LOG_INFO, MOZ_APP_NAME, args)

// We need to put Goanna on a even more separate thread, because
// otherwise this JNI method never returns; this leads to problems
// with local references overrunning the local refs table, among
// other things, since GC can't ever run on them.

// Note that we don't have xpcom initialized yet, so we can't use the
// thread manager for this.  Instead, we use pthreads directly.

struct AutoAttachJavaThread {
    AutoAttachJavaThread() {
        attached = mozilla_AndroidBridge_SetMainThread(pthread_self());
    }
    ~AutoAttachJavaThread() {
        mozilla_AndroidBridge_SetMainThread(-1);
        attached = false;
    }

    bool attached;
};

extern "C" NS_EXPORT void
GoannaStart(void *data, const nsXREAppData *appData)
{
    AutoAttachJavaThread attacher;
    if (!attacher.attached)
        return;

    if (!data) {
        LOG("Failed to get arguments for GoannaStart\n");
        return;
    }

    nsTArray<char *> targs;
    char *arg = strtok(static_cast<char *>(data), " ");
    while (arg) {
        targs.AppendElement(arg);
        arg = strtok(nullptr, " ");
    }
    targs.AppendElement(static_cast<char *>(nullptr));

    int result = XRE_main(targs.Length() - 1, targs.Elements(), appData, 0);

    if (result)
        LOG("XRE_main returned %d", result);

    mozilla::widget::GoannaAppShell::NotifyXreExit();

    free(targs[0]);
    nsMemory::Free(data);
    return;
}
