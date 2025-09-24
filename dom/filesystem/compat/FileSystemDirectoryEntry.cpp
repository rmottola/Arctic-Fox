/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FileSystemDirectoryEntry.h"
#include "CallbackRunnables.h"
#include "FileSystemDirectoryReader.h"
#include "mozilla/dom/Directory.h"
#include "mozilla/dom/FileSystemDirectoryEntryBinding.h"
#include "mozilla/dom/FileSystemUtils.h"

namespace mozilla {
namespace dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(FileSystemDirectoryEntry, FileSystemEntry,
                                   mDirectory)

NS_IMPL_ADDREF_INHERITED(FileSystemDirectoryEntry, FileSystemEntry)
NS_IMPL_RELEASE_INHERITED(FileSystemDirectoryEntry, FileSystemEntry)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(FileSystemDirectoryEntry)
NS_INTERFACE_MAP_END_INHERITING(FileSystemEntry)

FileSystemDirectoryEntry::FileSystemDirectoryEntry(nsIGlobalObject* aGlobal,
                                                   Directory* aDirectory,
                                                   FileSystem* aFileSystem)
  : FileSystemEntry(aGlobal, aFileSystem)
  , mDirectory(aDirectory)
{
  MOZ_ASSERT(aGlobal);
}

FileSystemDirectoryEntry::~FileSystemDirectoryEntry()
{}

JSObject*
FileSystemDirectoryEntry::WrapObject(JSContext* aCx,
                                     JS::Handle<JSObject*> aGivenProto)
{
  return FileSystemDirectoryEntryBinding::Wrap(aCx, this, aGivenProto);
}

void
FileSystemDirectoryEntry::GetName(nsAString& aName, ErrorResult& aRv) const
{
  MOZ_ASSERT(mDirectory);
  mDirectory->GetName(aName, aRv);
}

void
FileSystemDirectoryEntry::GetFullPath(nsAString& aPath, ErrorResult& aRv) const
{
  MOZ_ASSERT(mDirectory);
  mDirectory->GetPath(aPath, aRv);
}

already_AddRefed<FileSystemDirectoryReader>
FileSystemDirectoryEntry::CreateReader() const
{
  MOZ_ASSERT(mDirectory);

  RefPtr<FileSystemDirectoryReader> reader =
    new FileSystemDirectoryReader(GetParentObject(), Filesystem(), mDirectory);
  return reader.forget();
}

void
FileSystemDirectoryEntry::GetInternal(const nsAString& aPath,
                                      const FileSystemFlags& aFlag,
                                      const Optional<OwningNonNull<FileSystemEntryCallback>>& aSuccessCallback,
                                      const Optional<OwningNonNull<ErrorCallback>>& aErrorCallback,
                                      GetInternalType aType) const
{
  MOZ_ASSERT(mDirectory);

  if (!aSuccessCallback.WasPassed() && !aErrorCallback.WasPassed()) {
    return;
  }

  if (aFlag.mCreate) {
    ErrorCallbackHelper::Call(GetParentObject(), aErrorCallback,
                              NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  nsTArray<nsString> parts;
  if (!FileSystemUtils::IsValidRelativeDOMPath(aPath, parts)) {
    ErrorCallbackHelper::Call(GetParentObject(), aErrorCallback,
                              NS_ERROR_DOM_NOT_FOUND_ERR);
    return;
  }

  ErrorResult error;
  RefPtr<Promise> promise = mDirectory->Get(aPath, error);
  if (NS_WARN_IF(error.Failed())) {
    ErrorCallbackHelper::Call(GetParentObject(), aErrorCallback,
                              error.StealNSResult());
    return;
  }

  RefPtr<GetEntryHelper> handler =
    new GetEntryHelper(GetParentObject(), Filesystem(),
                       aSuccessCallback.WasPassed()
                         ? &aSuccessCallback.Value() : nullptr,
                       aErrorCallback.WasPassed()
                         ? &aErrorCallback.Value() : nullptr,
                       aType);
  promise->AppendNativeHandler(handler);
}

void
FileSystemDirectoryEntry::RemoveRecursively(VoidCallback& aSuccessCallback,
                                            const Optional<OwningNonNull<ErrorCallback>>& aErrorCallback) const
{
  ErrorCallbackHelper::Call(GetParentObject(), aErrorCallback,
                            NS_ERROR_DOM_SECURITY_ERR);
}

} // dom namespace
} // mozilla namespace
