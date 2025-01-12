/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_workers_xmlhttprequestupload_h__
#define mozilla_dom_workers_xmlhttprequestupload_h__

#include "nsXMLHttpRequest.h"

BEGIN_WORKERS_NAMESPACE

class XMLHttpRequestWorker;

class XMLHttpRequestUploadWorker final : public nsXHREventTarget
{
  RefPtr<XMLHttpRequestWorker> mXHR;

  explicit XMLHttpRequestUploadWorker(XMLHttpRequestWorker* aXHR);

  ~XMLHttpRequestUploadWorker();

public:
  virtual JSObject*
  WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override;

  static already_AddRefed<XMLHttpRequestUploadWorker>
  Create(XMLHttpRequestWorker* aXHR);

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(XMLHttpRequestUploadWorker, nsXHREventTarget)

  NS_DECL_ISUPPORTS_INHERITED

  nsISupports*
  GetParentObject() const
  {
    // There's only one global on a worker, so we don't need to specify.
    return nullptr;
  }

  bool
  HasListeners()
  {
    return mListenerManager && mListenerManager->HasListeners();
  }
};

END_WORKERS_NAMESPACE

#endif // mozilla_dom_workers_xmlhttprequestupload_h__
