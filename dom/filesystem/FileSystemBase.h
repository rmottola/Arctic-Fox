/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FileSystemBase_h
#define mozilla_dom_FileSystemBase_h

#include "nsAutoPtr.h"
#include "nsString.h"

namespace mozilla {
namespace dom {

class BlobImpl;
class Directory;

class FileSystemBase
{
  NS_INLINE_DECL_REFCOUNTING(FileSystemBase)
public:

  // Create file system object from its string representation.
  static already_AddRefed<FileSystemBase>
  DeserializeDOMPath(const nsAString& aString);

  FileSystemBase();

  virtual void
  Shutdown();

  // SerializeDOMPath the FileSystem to string.
  virtual void
  SerializeDOMPath(nsAString& aOutput) const = 0;

  virtual already_AddRefed<FileSystemBase>
  Clone() = 0;

  virtual nsISupports*
  GetParentObject() const;

  /*
   * Get the virtual name of the root directory. This name will be exposed to
   * the content page.
   */
  virtual void
  GetRootName(nsAString& aRetval) const = 0;

  const nsAString&
  GetLocalRootPath() const
  {
    return mLocalRootPath;
  }

  bool
  IsShutdown() const
  {
    return mShutdown;
  }

  virtual bool
  IsSafeFile(nsIFile* aFile) const;

  virtual bool
  IsSafeDirectory(Directory* aDir) const;

  bool
  GetRealPath(BlobImpl* aFile, nsIFile** aPath) const;

  /*
   * Get the permission name required to access this file system.
   */
  const nsCString&
  GetPermission() const
  {
    return mPermission;
  }

  bool
  RequiresPermissionChecks() const
  {
    return mRequiresPermissionChecks;
  }

  // CC methods
  virtual void Unlink() {}
  virtual void Traverse(nsCycleCollectionTraversalCallback &cb) {}

protected:
  virtual ~FileSystemBase();

  // The local path of the root (i.e. the OS path, with OS path separators, of
  // the OS directory that acts as the root of this OSFileSystem).
  // Only available in the parent process.
  // In the child process, we don't use it and its value should be empty.
  nsString mLocalRootPath;

  bool mShutdown;

  // The permission name required to access the file system.
  nsCString mPermission;

  bool mRequiresPermissionChecks;
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_FileSystemBase_h
