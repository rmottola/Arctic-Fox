/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OfflineObserver.h"
#include "nsNetUtil.h"
#include "nsIOService.h"
#include "mozilla/net/NeckoCommon.h"
#include "nsIObserverService.h"
#include "nsThreadUtils.h"
namespace mozilla {
namespace net {

NS_IMPL_ISUPPORTS(OfflineObserver, nsIObserver)

void
OfflineObserver::RegisterOfflineObserver()
{
  if (NS_IsMainThread()) {
    RegisterOfflineObserverMainThread();
  } else {
    NS_DispatchToMainThread(NewRunnableMethod(this,
                                              &OfflineObserver::RegisterOfflineObserverMainThread));
  }
}

void
OfflineObserver::RemoveOfflineObserver()
{
  if (NS_IsMainThread()) {
    RemoveOfflineObserverMainThread();
  } else {
    NS_DispatchToMainThread(NewRunnableMethod(this,
                                              &OfflineObserver::RemoveOfflineObserverMainThread));
  }
}

void
OfflineObserver::RegisterOfflineObserverMainThread()
{
  nsCOMPtr<nsIObserverService> observerService =
    mozilla::services::GetObserverService();
  if (!observerService) {
    return;
  }
  nsresult rv = observerService->AddObserver(this,
    NS_IOSERVICE_APP_OFFLINE_STATUS_TOPIC, false);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to register observer");
  }
}

void
OfflineObserver::RemoveOfflineObserverMainThread()
{
  nsCOMPtr<nsIObserverService> observerService =
    mozilla::services::GetObserverService();
  if (observerService) {
    observerService->RemoveObserver(this, NS_IOSERVICE_APP_OFFLINE_STATUS_TOPIC);
  }
}

OfflineObserver::OfflineObserver(DisconnectableParent * parent)
  : mLock("OfflineObserver")
{
  mParent = parent;
  RegisterOfflineObserver();
}

void
OfflineObserver::RemoveObserver()
{
  {
    mozilla::MutexAutoLock lock(mLock);
    mParent = nullptr;
  }
  RemoveOfflineObserver();
}

NS_IMETHODIMP
OfflineObserver::Observe(nsISupports *aSubject,
                         const char *aTopic,
                         const char16_t *aData)
{
  mozilla::MutexAutoLock lock(mLock);
  // Since the parent is supposed to call RemoveObserver in its destructor
  // we need to keep the mutex locked while calling OfflineNotification
  // to prevent mParent from going away.
  if (mParent &&
      !strcmp(aTopic, NS_IOSERVICE_APP_OFFLINE_STATUS_TOPIC)) {
    mParent->OfflineNotification(aSubject);
  }
  return NS_OK;
}

nsresult
DisconnectableParent::OfflineNotification(nsISupports *aSubject)
{
  nsCOMPtr<nsIAppOfflineInfo> info(do_QueryInterface(aSubject));
  if (!info) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  uint32_t targetAppId = NECKO_UNKNOWN_APP_ID;
  info->GetAppId(&targetAppId);

  // Obtain App ID
  uint32_t appId = GetAppId();
  if (appId != targetAppId) {
    return NS_OK;
  }

  // If the app is offline, close the socket
  if (NS_IsAppOffline(appId)) {
    OfflineDisconnect();
  }

  return NS_OK;
}

} // namespace net
} // namespace mozilla
