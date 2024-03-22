/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_LoadInfo_h
#define mozilla_LoadInfo_h

#include "nsIContentPolicy.h"
#include "nsILoadInfo.h"
#include "nsIPrincipal.h"
#include "nsIWeakReferenceUtils.h" // for nsWeakPtr
#include "nsIURI.h"
#include "nsTArray.h"

#include "mozilla/BasePrincipal.h"

class nsINode;
class nsPIDOMWindowOuter;
class nsXMLHttpRequest;

namespace mozilla {

namespace net {
class OptionalLoadInfoArgs;
} // namespace net

namespace ipc {
// we have to forward declare that function so we can use it as a friend.
nsresult
LoadInfoArgsToLoadInfo(const mozilla::net::OptionalLoadInfoArgs& aLoadInfoArgs,
                       nsILoadInfo** outLoadInfo);
} // namespace ipc

/**
 * Class that provides an nsILoadInfo implementation.
 *
 * Note that there is no reason why this class should be MOZ_EXPORT, but
 * Thunderbird relies on some insane hacks which require this, so we'll leave it
 * as is for now, but hopefully we'll be able to remove the MOZ_EXPORT keyword
 * from this class at some point.  See bug 1149127 for the discussion.
 */
class MOZ_EXPORT LoadInfo final : public nsILoadInfo
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSILOADINFO

  // aLoadingPrincipal MUST NOT BE NULL.
  LoadInfo(nsIPrincipal* aLoadingPrincipal,
           nsIPrincipal* aTriggeringPrincipal,
           nsINode* aLoadingContext,
           nsSecurityFlags aSecurityFlags,
           nsContentPolicyType aContentPolicyType);

  // Constructor used for TYPE_DOCUMENT loads with no reasonable loadingNode.
  LoadInfo(nsPIDOMWindowOuter* aOuterWindow,
           nsIPrincipal* aLoadingPrincipal,
           nsIPrincipal* aTriggeringPrincipal,
           nsSecurityFlags aSecurityFlags);

  // create an exact copy of the loadinfo
  already_AddRefed<nsILoadInfo> Clone() const;
  // creates a copy of the loadinfo which is appropriate to use for a
  // separate request. I.e. not for a redirect or an inner channel, but
  // when a separate request is made with the same security properties.
  already_AddRefed<nsILoadInfo> CloneForNewRequest() const;

  void SetIsPreflight();
  void SetIsFromProcessingFrameAttributes();

private:
  // private constructor that is only allowed to be called from within
  // HttpChannelParent and FTPChannelParent declared as friends undeneath.
  // In e10s we can not serialize nsINode, hence we store the innerWindowID.
  // Please note that aRedirectChain uses swapElements.
  LoadInfo(nsIPrincipal* aLoadingPrincipal,
           nsIPrincipal* aTriggeringPrincipal,
           nsSecurityFlags aSecurityFlags,
           nsContentPolicyType aContentPolicyType,
           LoadTainting aTainting,
           bool aUpgradeInsecureRequests,
           bool aVerifySignedContent,
           bool aEnforceSRI,
           uint64_t aInnerWindowID,
           uint64_t aOuterWindowID,
           uint64_t aParentOuterWindowID,
           bool aEnforceSecurity,
           bool aInitialSecurityCheckDone,
           bool aIsThirdPartyRequest,
           const NeckoOriginAttributes& aOriginAttributes,
           nsTArray<nsCOMPtr<nsIPrincipal>>& aRedirectChainIncludingInternalRedirects,
           nsTArray<nsCOMPtr<nsIPrincipal>>& aRedirectChain,
           const nsTArray<nsCString>& aUnsafeHeaders,
           bool aForcePreflight,
           bool aIsPreflight);
  LoadInfo(const LoadInfo& rhs);

  friend nsresult
  mozilla::ipc::LoadInfoArgsToLoadInfo(
    const mozilla::net::OptionalLoadInfoArgs& aLoadInfoArgs,
    nsILoadInfo** outLoadInfo);

  ~LoadInfo();

  void ComputeIsThirdPartyContext(nsPIDOMWindowOuter* aOuterWindow);

  // This function is the *only* function which can change the securityflags
  // of a loadinfo. It only exists because of the XHR code. Don't call it
  // from anywhere else!
  void SetIncludeCookiesSecFlag();
  friend class ::nsXMLHttpRequest;

  // if you add a member, please also update the copy constructor
  nsCOMPtr<nsIPrincipal>           mLoadingPrincipal;
  nsCOMPtr<nsIPrincipal>           mTriggeringPrincipal;
  nsWeakPtr                        mLoadingContext;
  nsSecurityFlags                  mSecurityFlags;
  nsContentPolicyType              mInternalContentPolicyType;
  LoadTainting                     mTainting;
  bool                             mUpgradeInsecureRequests;
  bool                             mVerifySignedContent;
  bool                             mEnforceSRI;
  uint64_t                         mInnerWindowID;
  uint64_t                         mOuterWindowID;
  uint64_t                         mParentOuterWindowID;
  bool                             mEnforceSecurity;
  bool                             mInitialSecurityCheckDone;
  bool                             mIsThirdPartyContext;
  NeckoOriginAttributes            mOriginAttributes;
  nsTArray<nsCOMPtr<nsIPrincipal>> mRedirectChainIncludingInternalRedirects;
  nsTArray<nsCOMPtr<nsIPrincipal>> mRedirectChain;
  nsTArray<nsCString>              mCorsUnsafeHeaders;
  bool                             mForcePreflight;
  bool                             mIsPreflight;

  // Is true if this load was triggered by processing the attributes of the
  // browsing context container.
  // See nsILoadInfo.isFromProcessingFrameAttributes
  bool                             mIsFromProcessingFrameAttributes;
};

} // namespace mozilla

#endif // mozilla_LoadInfo_h

