/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_embedding_PrintingParent_h
#define mozilla_embedding_PrintingParent_h

#include "mozilla/dom/PBrowserParent.h"
#include "mozilla/embedding/PPrintingParent.h"

class nsPIDOMWindowOuter;
class PPrintProgressDialogParent;
class PPrintSettingsDialogParent;

namespace mozilla {
namespace layout {
class PRemotePrintJobParent;
}

namespace embedding {

class PrintingParent final : public PPrintingParent
{
public:
    NS_INLINE_DECL_REFCOUNTING(PrintingParent)

    virtual bool
    RecvShowProgress(PBrowserParent* parent,
                     PPrintProgressDialogParent* printProgressDialog,
                     const bool& isForPrinting,
                     bool* notifyOnOpen,
                     nsresult* result);
    virtual bool
    RecvShowPrintDialog(PPrintSettingsDialogParent* aDialog,
                        PBrowserParent* aParent,
                        const PrintData& aData);

    virtual bool
    RecvSavePrintSettings(const PrintData& data,
                          const bool& usePrinterNamePrefix,
                          const uint32_t& flags,
                          nsresult* rv);

    virtual PPrintProgressDialogParent*
    AllocPPrintProgressDialogParent();

    virtual bool
    DeallocPPrintProgressDialogParent(PPrintProgressDialogParent* aActor);

    virtual PPrintSettingsDialogParent*
    AllocPPrintSettingsDialogParent();

    virtual bool
    DeallocPPrintSettingsDialogParent(PPrintSettingsDialogParent* aActor);

    virtual PRemotePrintJobParent*
    AllocPRemotePrintJobParent();

    virtual bool
    DeallocPRemotePrintJobParent(PRemotePrintJobParent* aActor);

    virtual void
    ActorDestroy(ActorDestroyReason aWhy);

    MOZ_IMPLICIT PrintingParent();

private:
    virtual ~PrintingParent();

    nsPIDOMWindowOuter*
    DOMWindowFromBrowserParent(PBrowserParent* parent);

    nsresult
    ShowPrintDialog(PBrowserParent* parent,
                    const PrintData& data,
                    PrintData* result);
};

} // namespace embedding
} // namespace mozilla

#endif
