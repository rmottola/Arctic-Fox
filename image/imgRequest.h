/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_imgRequest_h
#define mozilla_image_imgRequest_h

#include "nsIChannelEventSink.h"
#include "nsIInterfaceRequestor.h"
#include "nsIStreamListener.h"
#include "nsIThreadRetargetableStreamListener.h"
#include "nsIPrincipal.h"

#include "nsAutoPtr.h"
#include "nsCOMPtr.h"
#include "nsProxyRelease.h"
#include "nsStringGlue.h"
#include "nsError.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "mozilla/Atomics.h"
#include "mozilla/net/ReferrerPolicy.h"

class imgCacheValidator;
class imgLoader;
class imgRequestProxy;
class imgCacheEntry;
class imgMemoryReporter;
class imgRequestNotifyRunnable;
class nsIApplicationCache;
class nsIProperties;
class nsIRequest;
class nsITimedChannel;
class nsIURI;

namespace mozilla {
namespace image {
class Image;
class ImageURL;
class ProgressTracker;
} // namespace image
} // namespace mozilla

class imgRequest final : public nsIStreamListener,
                             public nsIThreadRetargetableStreamListener,
                             public nsIChannelEventSink,
                             public nsIInterfaceRequestor,
                             public nsIAsyncVerifyRedirectCallback
{
  virtual ~imgRequest();

public:
  typedef mozilla::image::Image Image;
  typedef mozilla::image::ImageURL ImageURL;
  typedef mozilla::image::ProgressTracker ProgressTracker;
  typedef mozilla::net::ReferrerPolicy ReferrerPolicy;

  explicit imgRequest(imgLoader* aLoader);

  NS_DECL_THREADSAFE_ISUPPORTS

  nsresult Init(nsIURI *aURI,
                nsIURI *aCurrentURI,
                nsIRequest *aRequest,
                nsIChannel *aChannel,
                imgCacheEntry *aCacheEntry,
                nsISupports* aCX,
                nsIPrincipal* aLoadingPrincipal,
                int32_t aCORSMode,
                ReferrerPolicy aReferrerPolicy);

  void ClearLoader();

  // Callers must call imgRequestProxy::Notify later.
  void AddProxy(imgRequestProxy *proxy);

  nsresult RemoveProxy(imgRequestProxy *proxy, nsresult aStatus);

  // Cancel, but also ensure that all work done in Init() is undone. Call this
  // only when the channel has failed to open, and so calling Cancel() on it
  // won't be sufficient.
  void CancelAndAbort(nsresult aStatus);

  // Called or dispatched by cancel for main thread only execution.
  void ContinueCancel(nsresult aStatus);

  // Called or dispatched by EvictFromCache for main thread only execution.
  void ContinueEvict();

  // Request that we start decoding the image as soon as data becomes available.
  void RequestDecode() { mDecodeRequested = true; }

  inline uint64_t InnerWindowID() const {
    return mInnerWindowId;
  }

  // Set the cache validation information (expiry time, whether we must
  // validate, etc) on the cache entry based on the request information.
  // If this function is called multiple times, the information set earliest
  // wins.
  static void SetCacheValidation(imgCacheEntry* aEntry, nsIRequest* aRequest);

  // Check if application cache of the original load is different from
  // application cache of the new load.  Also lack of application cache
  // on one of the loads is considered a change of a loading cache since
  // HTTP cache may contain a different data then app cache.
  bool CacheChanged(nsIRequest* aNewRequest);

  bool GetMultipart() const { return mIsMultiPartChannel; }

  // The CORS mode for which we loaded this image.
  int32_t GetCORSMode() const { return mCORSMode; }

  // The Referrer Policy in effect when loading this image.
  ReferrerPolicy GetReferrerPolicy() const { return mReferrerPolicy; }

  // The principal for the document that loaded this image. Used when trying to
  // validate a CORS image load.
  already_AddRefed<nsIPrincipal> GetLoadingPrincipal() const
  {
    nsCOMPtr<nsIPrincipal> principal = mLoadingPrincipal;
    return principal.forget();
  }

  // Return the ProgressTracker associated with this imgRequest. It may live
  // in |mProgressTracker| or in |mImage.mProgressTracker|, depending on whether
  // mImage has been instantiated yet.
  already_AddRefed<ProgressTracker> GetProgressTracker();

  // Get the current principal of the image. No AddRefing.
  inline nsIPrincipal* GetPrincipal() const { return mPrincipal.get(); }

  // Resize the cache entry to 0 if it exists
  void ResetCacheEntry();

  // OK to use on any thread.
  nsresult GetURI(ImageURL **aURI);
  nsresult GetCurrentURI(nsIURI **aURI);

  nsresult GetImageErrorCode(void);

private:
  friend class imgCacheEntry;
  friend class imgRequestProxy;
  friend class imgLoader;
  friend class imgCacheValidator;
  friend class imgCacheExpirationTracker;
  friend class imgRequestNotifyRunnable;
  friend class mozilla::image::ProgressTracker;

  inline void SetLoadId(void *aLoadId) {
    mLoadId = aLoadId;
  }
  void Cancel(nsresult aStatus);
  void EvictFromCache();
  void RemoveFromCache();

  nsresult GetSecurityInfo(nsISupports **aSecurityInfo);

  imgCacheValidator* GetValidator() const { return mValidator; }

  inline const char *GetMimeType() const {
    return mContentType.get();
  }
  inline nsIProperties *Properties() {
    return mProperties;
  }

  // Reset the cache entry after we've dropped our reference to it. Used by the
  // imgLoader when our cache entry is re-requested after we've dropped our
  // reference to it.
  void SetCacheEntry(imgCacheEntry *entry);

  // Returns whether we've got a reference to the cache entry.
  bool HasCacheEntry() const;

  // Update the cache entry size based on the image container.
  void UpdateCacheEntrySize();

  // Return the priority of the underlying network request, or return
  // PRIORITY_NORMAL if it doesn't support nsISupportsPriority.
  int32_t Priority() const;

  // Adjust the priority of the underlying network request by the given delta
  // on behalf of the given proxy.
  void AdjustPriority(imgRequestProxy *aProxy, int32_t aDelta);

  // Return whether we've seen some data at this point
  bool HasTransferredData() const { return mGotData; }

  // Set whether this request is stored in the cache. If it isn't, regardless
  // of whether this request has a non-null mCacheEntry, this imgRequest won't
  // try to update or modify the image cache.
  void SetIsInCache(bool cacheable);

  bool IsBlockingOnload() const;
  void SetBlockingOnload(bool block) const;

  bool HasConsumers();

public:
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSITHREADRETARGETABLESTREAMLISTENER
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSICHANNELEVENTSINK
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSIASYNCVERIFYREDIRECTCALLBACK

  // Sets properties for this image; will dispatch to main thread if needed.
  void SetProperties(nsIChannel* aChan);

private:
  friend class imgMemoryReporter;

  // Weak reference to parent loader; this request cannot outlive its owner.
  imgLoader* mLoader;
  nsCOMPtr<nsIRequest> mRequest;
  // The original URI we were loaded with. This is the same as the URI we are
  // keyed on in the cache. We store a string here to avoid off main thread
  // refcounting issues with nsStandardURL.
  nsRefPtr<ImageURL> mURI;
  // The URI of the resource we ended up loading after all redirects, etc.
  nsCOMPtr<nsIURI> mCurrentURI;
  // The principal of the document which loaded this image. Used when validating for CORS.
  nsCOMPtr<nsIPrincipal> mLoadingPrincipal;
  // The principal of this image.
  nsCOMPtr<nsIPrincipal> mPrincipal;
  // Progress tracker -- transferred to mImage, when it gets instantiated.
  nsRefPtr<ProgressTracker> mProgressTracker;
  nsRefPtr<Image> mImage;
  nsCOMPtr<nsIProperties> mProperties;
  nsCOMPtr<nsISupports> mSecurityInfo;
  nsCOMPtr<nsIChannel> mChannel;
  nsCOMPtr<nsIInterfaceRequestor> mPrevChannelSink;
  nsCOMPtr<nsIApplicationCache> mApplicationCache;

  nsCOMPtr<nsITimedChannel> mTimedChannel;

  nsCString mContentType;

  nsRefPtr<imgCacheEntry> mCacheEntry; /* we hold on to this to this so long as we have observers */

  void *mLoadId;

  imgCacheValidator *mValidator;
  nsCOMPtr<nsIAsyncVerifyRedirectCallback> mRedirectCallback;
  nsCOMPtr<nsIChannel> mNewRedirectChannel;

  // The ID of the inner window origin, used for error reporting.
  uint64_t mInnerWindowId;

  // The CORS mode (defined in imgIRequest) this image was loaded with. By
  // default, imgIRequest::CORS_NONE.
  int32_t mCORSMode;

  // The Referrer Policy (defined in ReferrerPolicy.h) used for this image.
  ReferrerPolicy mReferrerPolicy;

  nsresult mImageErrorCode;

  mozilla::Atomic<bool> mDecodeRequested;

  bool mIsMultiPartChannel : 1;
  bool mGotData : 1;
  bool mIsInCache : 1;
  bool mBlockingOnload : 1;
  bool mNewPartPending : 1;
};

#endif // mozilla_image_imgRequest_h
