/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_CreateFileTask_h
#define mozilla_dom_CreateFileTask_h

#include "mozilla/dom/FileSystemTaskBase.h"
#include "nsAutoPtr.h"
#include "mozilla/ErrorResult.h"

class nsIInputStream;

namespace mozilla {
namespace dom {

class Blob;
class BlobImpl;
class Promise;

class CreateFileTask final : public FileSystemTaskBase
{
public:
  static already_AddRefed<CreateFileTask>
  Create(FileSystemBase* aFileSystem,
         nsIFile* aFile,
         Blob* aBlobData,
         InfallibleTArray<uint8_t>& aArrayData,
         bool replace,
         ErrorResult& aRv);

  static already_AddRefed<CreateFileTask>
  Create(FileSystemBase* aFileSystem,
         const FileSystemCreateFileParams& aParam,
         FileSystemRequestParent* aParent,
         ErrorResult& aRv);

  virtual
  ~CreateFileTask();

  already_AddRefed<Promise>
  GetPromise();

  virtual void
  GetPermissionAccessType(nsCString& aAccess) const override;

protected:
  virtual FileSystemParams
  GetRequestParams(const nsString& aSerializedDOMPath,
                   ErrorResult& aRv) const override;

  virtual FileSystemResponseValue
  GetSuccessRequestResult(ErrorResult& aRv) const override;

  virtual void
  SetSuccessRequestResult(const FileSystemResponseValue& aValue,
                          ErrorResult& aRv) override;

  virtual nsresult
  Work() override;

  virtual void
  HandlerCallback() override;

private:
  CreateFileTask(FileSystemBase* aFileSystem,
                 nsIFile* aFile,
                 bool aReplace);

  CreateFileTask(FileSystemBase* aFileSystem,
                 const FileSystemCreateFileParams& aParam,
                 FileSystemRequestParent* aParent);

  void
  GetOutputBufferSize() const;

  static uint32_t sOutputBufferSize;
  RefPtr<Promise> mPromise;
  nsCOMPtr<nsIFile> mTargetPath;

  // Not thread-safe and should be released on main thread.
  RefPtr<Blob> mBlobData;

  nsCOMPtr<nsIInputStream> mBlobStream;
  InfallibleTArray<uint8_t> mArrayData;
  bool mReplace;

  // This cannot be a File because this object is created on a different
  // thread and File is not thread-safe. Let's use the BlobImpl instead.
  RefPtr<BlobImpl> mTargetBlobImpl;
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_CreateFileTask_h
