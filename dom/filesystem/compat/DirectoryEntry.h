/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_DirectoryEntry_h
#define mozilla_dom_DirectoryEntry_h

#include "mozilla/dom/Entry.h"
#include "mozilla/dom/DOMFileSystemBinding.h"

namespace mozilla {
namespace dom {

class Directory;

class DirectoryEntry : public Entry
{
public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(DirectoryEntry, Entry)

  DirectoryEntry(nsIGlobalObject* aGlobalObject, Directory* aDirectory,
                 DOMFileSystem* aFileSystem);

  virtual JSObject*
  WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override;

  virtual bool
  IsDirectory() const override
  {
    return true;
  }

  virtual void
  GetName(nsAString& aName, ErrorResult& aRv) const override;

  virtual void
  GetFullPath(nsAString& aFullPath, ErrorResult& aRv) const override;

  virtual already_AddRefed<DirectoryReader>
  CreateReader() const;

  void
  GetFile(const nsAString& aPath, const FileSystemFlags& aFlag,
          const Optional<OwningNonNull<EntryCallback>>& aSuccessCallback,
          const Optional<OwningNonNull<ErrorCallback>>& aErrorCallback) const
  {
    GetInternal(aPath, aFlag, aSuccessCallback, aErrorCallback, eGetFile);
  }

  void
  GetDirectory(const nsAString& aPath, const FileSystemFlags& aFlag,
               const Optional<OwningNonNull<EntryCallback>>& aSuccessCallback,
               const Optional<OwningNonNull<ErrorCallback>>& aErrorCallback) const
  {
    GetInternal(aPath, aFlag, aSuccessCallback, aErrorCallback, eGetDirectory);
  }

  void
  RemoveRecursively(VoidCallback& aSuccessCallback,
                    const Optional<OwningNonNull<ErrorCallback>>& aErrorCallback) const;

  enum GetInternalType { eGetFile, eGetDirectory };

  virtual void
  GetInternal(const nsAString& aPath, const FileSystemFlags& aFlag,
              const Optional<OwningNonNull<EntryCallback>>& aSuccessCallback,
              const Optional<OwningNonNull<ErrorCallback>>& aErrorCallback,
              GetInternalType aType) const;

protected:
  virtual ~DirectoryEntry();

private:
  RefPtr<Directory> mDirectory;
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_DirectoryEntry_h
