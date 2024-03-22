/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_OSFileSystem_h
#define mozilla_dom_OSFileSystem_h

#include "mozilla/dom/FileSystemBase.h"

namespace mozilla {
namespace dom {

class OSFileSystem final : public FileSystemBase
{
public:
  explicit OSFileSystem(const nsAString& aRootDir);

  void
  Init(nsISupports* aParent);

  // Overrides FileSystemBase

  virtual already_AddRefed<FileSystemBase>
  Clone() override;

  virtual nsISupports*
  GetParentObject() const override;

  virtual void
  GetRootName(nsAString& aRetval) const override;

  virtual bool
  IsSafeFile(nsIFile* aFile) const override;

  virtual bool
  IsSafeDirectory(Directory* aDir) const override;

  virtual void
  SerializeDOMPath(nsAString& aOutput) const override;

  // CC methods
  virtual void Unlink() override;
  virtual void Traverse(nsCycleCollectionTraversalCallback &cb) override;

private:
  virtual ~OSFileSystem() {}

   nsCOMPtr<nsISupports> mParent;
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_OSFileSystem_h
