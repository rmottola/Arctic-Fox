/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "StructuredCloneData.h"

#include "nsIDOMDOMException.h"
#include "nsIMutable.h"
#include "nsIXPConnect.h"

#include "ipc/IPCMessageUtils.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/BlobBinding.h"
#include "mozilla/dom/File.h"
#include "mozilla/dom/ToJSValue.h"
#include "nsContentUtils.h"
#include "nsJSEnvironment.h"
#include "MainThreadUtils.h"
#include "StructuredCloneTags.h"
#include "jsapi.h"

namespace mozilla {
namespace dom {
namespace ipc {

bool
StructuredCloneData::Copy(const StructuredCloneData& aData)
{
  if (!aData.Data()) {
    return true;
  }

  if (aData.SharedData()) {
    mSharedData = aData.SharedData();
  } else {
    mSharedData =
      SharedJSAllocatedData::CreateFromExternalData(aData.Data(),
                                                    aData.DataLength());
    NS_ENSURE_TRUE(mSharedData, false);
  }

  PortIdentifiers().AppendElements(aData.PortIdentifiers());

  MOZ_ASSERT(BlobImpls().IsEmpty());
  BlobImpls().AppendElements(aData.BlobImpls());

  MOZ_ASSERT(GetSurfaces().IsEmpty());

  return true;
}

void
StructuredCloneData::Read(JSContext* aCx,
                          JS::MutableHandle<JS::Value> aValue,
                          ErrorResult &aRv)
{
  MOZ_ASSERT(Data());

  nsIGlobalObject *global = xpc::NativeGlobal(JS::CurrentGlobalOrNull(aCx));
  MOZ_ASSERT(global);

  ReadFromBuffer(global, aCx, Data(), DataLength(), aValue, aRv);
}

void
StructuredCloneData::Write(JSContext* aCx,
                           JS::Handle<JS::Value> aValue,
                           ErrorResult &aRv)
{
  Write(aCx, aValue, JS::UndefinedHandleValue, aRv);
}

void
StructuredCloneData::Write(JSContext* aCx,
                           JS::Handle<JS::Value> aValue,
                           JS::Handle<JS::Value> aTransfer,
                           ErrorResult &aRv)
{
  MOZ_ASSERT(!Data());

  StructuredCloneHolder::Write(aCx, aValue, aTransfer, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }

  uint64_t* data = nullptr;
  size_t dataLength = 0;
  mBuffer->steal(&data, &dataLength);
  mBuffer = nullptr;
  mSharedData = new SharedJSAllocatedData(data, dataLength);
}

void
StructuredCloneData::WriteIPCParams(IPC::Message* aMsg) const
{
  WriteParam(aMsg, DataLength());

  if (DataLength()) {
    aMsg->WriteBytes(Data(), DataLength());
  }
}

bool
StructuredCloneData::ReadIPCParams(const IPC::Message* aMsg,
                                   PickleIterator* aIter)
{
  MOZ_ASSERT(!Data());

  size_t dataLength = 0;
  if (!ReadParam(aMsg, aIter, &dataLength)) {
    return false;
  }

  if (!dataLength) {
    return true;
  }

  mSharedData = SharedJSAllocatedData::AllocateForExternalData(dataLength);
  NS_ENSURE_TRUE(mSharedData, false);

  if (!aMsg->ReadBytesInto(aIter, mSharedData->Data(), dataLength)) {
    mSharedData = nullptr;
    return false;
  }

  return true;
}

bool
StructuredCloneData::CopyExternalData(const void* aData,
                                      size_t aDataLength)
{
  MOZ_ASSERT(!Data());
  mSharedData = SharedJSAllocatedData::CreateFromExternalData(aData,
                                                              aDataLength);
  NS_ENSURE_TRUE(mSharedData, false);
  return true;
}

} // namespace ipc
} // namespace dom
} // namespace mozilla
