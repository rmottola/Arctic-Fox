/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ServiceWorkerManager.h"

#include "nsIConsoleService.h"
#include "nsIDOMEventTarget.h"
#include "nsIDocument.h"
#include "nsIScriptSecurityManager.h"
#include "nsIStreamLoader.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIHttpHeaderVisitor.h"
#include "nsINetworkInterceptController.h"
#include "nsIMutableArray.h"
#include "nsIScriptError.h"
#include "nsISimpleEnumerator.h"
#include "nsITimer.h"
#include "nsIUploadChannel2.h"
#include "nsPIDOMWindow.h"
#include "nsScriptLoader.h"
#include "nsServiceManagerUtils.h"
#include "nsDebug.h"
#include "nsISupportsPrimitives.h"

#include "jsapi.h"

#include "mozilla/BasePrincipal.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/ErrorNames.h"
#include "mozilla/LoadContext.h"
#include "mozilla/Telemetry.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/DOMError.h"
#include "mozilla/dom/ErrorEvent.h"
#include "mozilla/dom/Headers.h"
#include "mozilla/dom/InternalHeaders.h"
#include "mozilla/dom/Navigator.h"
#include "mozilla/dom/NotificationEvent.h"
#include "mozilla/dom/PromiseNativeHandler.h"
#include "mozilla/dom/Request.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "mozilla/unused.h"
#include "mozilla/EnumSet.h"

#include "nsContentPolicyUtils.h"
#include "nsContentSecurityManager.h"
#include "nsContentUtils.h"
#include "nsGlobalWindow.h"
#include "nsNetUtil.h"
#include "nsIURL.h"
#include "nsProxyRelease.h"
#include "nsQueryObject.h"
#include "nsTArray.h"

#include "RuntimeService.h"
#include "ServiceWorker.h"
#include "ServiceWorkerClient.h"
#include "ServiceWorkerContainer.h"
#include "ServiceWorkerManagerChild.h"
#include "ServiceWorkerPrivate.h"
#include "ServiceWorkerRegistrar.h"
#include "ServiceWorkerRegistration.h"
#include "ServiceWorkerScriptCache.h"
#include "ServiceWorkerEvents.h"
#include "SharedWorker.h"
#include "WorkerInlines.h"
#include "WorkerPrivate.h"
#include "WorkerRunnable.h"
#include "WorkerScope.h"

#ifndef MOZ_SIMPLEPUSH
#include "mozilla/dom/TypedArray.h"
#endif

#ifdef PostMessage
#undef PostMessage
#endif

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::ipc;

BEGIN_WORKERS_NAMESPACE

#define PURGE_DOMAIN_DATA "browser:purge-domain-data"
#define PURGE_SESSION_HISTORY "browser:purge-session-history"
#define CLEAR_ORIGIN_DATA "clear-origin-data"

static_assert(nsIHttpChannelInternal::CORS_MODE_SAME_ORIGIN == static_cast<uint32_t>(RequestMode::Same_origin),
              "RequestMode enumeration value should match Necko CORS mode value.");
static_assert(nsIHttpChannelInternal::CORS_MODE_NO_CORS == static_cast<uint32_t>(RequestMode::No_cors),
              "RequestMode enumeration value should match Necko CORS mode value.");
static_assert(nsIHttpChannelInternal::CORS_MODE_CORS == static_cast<uint32_t>(RequestMode::Cors),
              "RequestMode enumeration value should match Necko CORS mode value.");
static_assert(nsIHttpChannelInternal::CORS_MODE_NAVIGATE == static_cast<uint32_t>(RequestMode::Navigate),
              "RequestMode enumeration value should match Necko CORS mode value.");

static_assert(nsIHttpChannelInternal::REDIRECT_MODE_FOLLOW == static_cast<uint32_t>(RequestRedirect::Follow),
              "RequestRedirect enumeration value should make Necko Redirect mode value.");
static_assert(nsIHttpChannelInternal::REDIRECT_MODE_ERROR == static_cast<uint32_t>(RequestRedirect::Error),
              "RequestRedirect enumeration value should make Necko Redirect mode value.");
static_assert(nsIHttpChannelInternal::REDIRECT_MODE_MANUAL == static_cast<uint32_t>(RequestRedirect::Manual),
              "RequestRedirect enumeration value should make Necko Redirect mode value.");
static_assert(3 == static_cast<uint32_t>(RequestRedirect::EndGuard_),
              "RequestRedirect enumeration value should make Necko Redirect mode value.");

static_assert(nsIHttpChannelInternal::FETCH_CACHE_MODE_DEFAULT == static_cast<uint32_t>(RequestCache::Default),
             "RequestCache enumeration value should match Necko Cache mode value.");
static_assert(nsIHttpChannelInternal::FETCH_CACHE_MODE_NO_STORE == static_cast<uint32_t>(RequestCache::No_store),
             "RequestCache enumeration value should match Necko Cache mode value.");
static_assert(nsIHttpChannelInternal::FETCH_CACHE_MODE_RELOAD == static_cast<uint32_t>(RequestCache::Reload),
             "RequestCache enumeration value should match Necko Cache mode value.");
static_assert(nsIHttpChannelInternal::FETCH_CACHE_MODE_NO_CACHE == static_cast<uint32_t>(RequestCache::No_cache),
             "RequestCache enumeration value should match Necko Cache mode value.");
static_assert(nsIHttpChannelInternal::FETCH_CACHE_MODE_FORCE_CACHE == static_cast<uint32_t>(RequestCache::Force_cache),
             "RequestCache enumeration value should match Necko Cache mode value.");
static_assert(5 == static_cast<uint32_t>(RequestCache::EndGuard_),
             "RequestCache enumeration value should match Necko Cache mode value.");

static StaticRefPtr<ServiceWorkerManager> gInstance;

struct ServiceWorkerManager::RegistrationDataPerPrincipal final
{
  // Ordered list of scopes for glob matching.
  // Each entry is an absolute URL representing the scope.
  // Each value of the hash table is an array of an absolute URLs representing
  // the scopes.
  //
  // An array is used for now since the number of controlled scopes per
  // domain is expected to be relatively low. If that assumption was proved
  // wrong this should be replaced with a better structure to avoid the
  // memmoves associated with inserting stuff in the middle of the array.
  nsTArray<nsCString> mOrderedScopes;

  // Scope to registration.
  // The scope should be a fully qualified valid URL.
  nsRefPtrHashtable<nsCStringHashKey, ServiceWorkerRegistrationInfo> mInfos;

  // Maps scopes to job queues.
  nsClassHashtable<nsCStringHashKey, ServiceWorkerJobQueue> mJobQueues;

  // Map scopes to scheduled update timers.
  nsInterfaceHashtable<nsCStringHashKey, nsITimer> mUpdateTimers;
};

struct ServiceWorkerManager::PendingOperation final
{
  nsCOMPtr<nsIRunnable> mRunnable;

  ServiceWorkerJobQueue* mQueue;
  RefPtr<ServiceWorkerJob> mJob;

  ServiceWorkerRegistrationData mRegistration;
};

class ServiceWorkerJob : public nsISupports
{
  friend class ServiceWorkerJobQueue;

public:
  NS_DECL_ISUPPORTS

  enum Type
  {
    RegisterJob,
    UpdateJob,
    InstallJob,
    UnregisterJob
  };

  virtual void Start() = 0;

  bool
  IsRegisterOrInstallJob() const
  {
    return mJobType == RegisterJob || mJobType == UpdateJob ||
      mJobType == InstallJob;
  }

protected:
  // The queue keeps the jobs alive, so they can hold a rawptr back to the
  // queue.
  ServiceWorkerJobQueue* mQueue;

  Type mJobType;

  explicit ServiceWorkerJob(ServiceWorkerJobQueue* aQueue, Type aJobType)
    : mQueue(aQueue)
    , mJobType(aJobType)
  {}

  virtual ~ServiceWorkerJob()
  {}

  void
  Done(nsresult aStatus);
};

class ServiceWorkerJobQueue final
{
  friend class ServiceWorkerJob;

  struct QueueData final
  {
    QueueData()
      : mPopping(false)
    { }

    ~QueueData()
    {
      if (!mJobs.IsEmpty()) {
        NS_WARNING("Pending/running jobs still around on shutdown!");
      }
    }

    nsTArray<RefPtr<ServiceWorkerJob>> mJobs;
    bool mPopping;
  };

  const nsCString mOriginAttributesSuffix;
  QueueData mRegistrationJobQueue;
  QueueData mInstallationJobQueue;

public:
  explicit ServiceWorkerJobQueue(const nsACString& aScopeKey)
    : mOriginAttributesSuffix(aScopeKey)
  {}

  ~ServiceWorkerJobQueue()
  { }

  void
  Append(ServiceWorkerJob* aJob)
  {
    MOZ_ASSERT(aJob);
    QueueData& queue = GetQueue(aJob->mJobType);
    MOZ_ASSERT(!queue.mJobs.Contains(aJob));

    bool wasEmpty = queue.mJobs.IsEmpty();
    queue.mJobs.AppendElement(aJob);
    if (wasEmpty) {
      aJob->Start();
    }
  }

  void
  CancelJobs();

private:
  void
  CancelJobs(QueueData& aQueue);

  // Internal helper function used to assign jobs to the correct queue.
  QueueData&
  GetQueue(ServiceWorkerJob::Type aType)
  {
    switch (aType) {
    case ServiceWorkerJob::Type::RegisterJob:
    case ServiceWorkerJob::Type::UpdateJob:
    case ServiceWorkerJob::Type::UnregisterJob:
      return mRegistrationJobQueue;
    case ServiceWorkerJob::Type::InstallJob:
      return mInstallationJobQueue;
    default:
      MOZ_CRASH("Invalid job queue type.");
      return mRegistrationJobQueue;
    }
  }

  bool
  IsEmpty()
  {
    return mRegistrationJobQueue.mJobs.IsEmpty() &&
      mInstallationJobQueue.mJobs.IsEmpty();
  }

  void
  Pop(QueueData& aQueue)
  {
    MOZ_ASSERT(!aQueue.mPopping,
               "Pop() called recursively, did you write a job which calls Done() synchronously from Start()?");

    AutoRestore<bool> savePopping(aQueue.mPopping);
    aQueue.mPopping = true;
    MOZ_ASSERT(!aQueue.mJobs.IsEmpty());
    aQueue.mJobs.RemoveElementAt(0);
    if (!aQueue.mJobs.IsEmpty()) {
      aQueue.mJobs[0]->Start();
    } else if (IsEmpty()) {
      RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
      MOZ_ASSERT(swm);
      swm->MaybeRemoveRegistrationInfo(mOriginAttributesSuffix);
    }
  }

  void
  Done(ServiceWorkerJob* aJob)
  {
    MOZ_ASSERT(aJob);
    QueueData& queue = GetQueue(aJob->mJobType);
    MOZ_ASSERT(!queue.mJobs.IsEmpty());
    MOZ_ASSERT(queue.mJobs[0] == aJob);
    Pop(queue);
  }
};

namespace {

nsresult
PopulateRegistrationData(nsIPrincipal* aPrincipal,
                         const ServiceWorkerRegistrationInfo* aRegistration,
                         ServiceWorkerRegistrationData& aData)
{
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aRegistration);

  if (NS_WARN_IF(!BasePrincipal::Cast(aPrincipal)->IsCodebasePrincipal())) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv = PrincipalToPrincipalInfo(aPrincipal, &aData.principal());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  aData.scope() = aRegistration->mScope;

  RefPtr<ServiceWorkerInfo> newest = aRegistration->Newest();
  if (NS_WARN_IF(!newest)) {
    return NS_ERROR_FAILURE;
  }

  aData.scriptSpec() = newest->ScriptSpec();

  if (aRegistration->mActiveWorker) {
    aData.currentWorkerURL() = aRegistration->mActiveWorker->ScriptSpec();
    aData.activeCacheName() = aRegistration->mActiveWorker->CacheName();
  }

  if (aRegistration->mWaitingWorker) {
    aData.waitingCacheName() = aRegistration->mWaitingWorker->CacheName();
  }

  return NS_OK;
}

class TeardownRunnable final : public nsRunnable
{
public:
  explicit TeardownRunnable(ServiceWorkerManagerChild* aActor)
    : mActor(aActor)
  {
    MOZ_ASSERT(mActor);
  }

  NS_IMETHODIMP Run() override
  {
    MOZ_ASSERT(mActor);
    mActor->SendShutdown();
    return NS_OK;
  }

private:
  ~TeardownRunnable() {}

  RefPtr<ServiceWorkerManagerChild> mActor;
};

} // namespace

NS_IMPL_ISUPPORTS0(ServiceWorkerJob)

void
ServiceWorkerJob::Done(nsresult aStatus)
{
  if (NS_WARN_IF(NS_FAILED(aStatus))) {
#ifdef DEBUG
    nsAutoCString errorName;
    GetErrorName(aStatus, errorName);
#endif
    NS_WARNING(nsPrintfCString("ServiceWorkerJob failed with error: %s",
                               errorName.get()).get());
  }

  if (mQueue) {
    mQueue->Done(this);
  }
}

void
ServiceWorkerRegistrationInfo::Clear()
{
  if (mInstallingWorker) {
    mInstallingWorker->UpdateState(ServiceWorkerState::Redundant);
    mInstallingWorker->WorkerPrivate()->NoteDeadServiceWorkerInfo();
    mInstallingWorker = nullptr;
    // FIXME(nsm): Abort any inflight requests from installing worker.
  }

  if (mWaitingWorker) {
    mWaitingWorker->UpdateState(ServiceWorkerState::Redundant);

    nsresult rv = serviceWorkerScriptCache::PurgeCache(mPrincipal,
                                                       mWaitingWorker->CacheName());
    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to purge the waiting cache.");
    }

    mWaitingWorker->WorkerPrivate()->NoteDeadServiceWorkerInfo();
    mWaitingWorker = nullptr;
  }

  if (mActiveWorker) {
    mActiveWorker->UpdateState(ServiceWorkerState::Redundant);

    nsresult rv = serviceWorkerScriptCache::PurgeCache(mPrincipal,
                                                       mActiveWorker->CacheName());
    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to purge the active cache.");
    }

    mActiveWorker->WorkerPrivate()->NoteDeadServiceWorkerInfo();
    mActiveWorker = nullptr;
  }

  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  MOZ_ASSERT(swm);
  swm->InvalidateServiceWorkerRegistrationWorker(this,
                                                 WhichServiceWorker::INSTALLING_WORKER |
                                                 WhichServiceWorker::WAITING_WORKER |
                                                 WhichServiceWorker::ACTIVE_WORKER);
}

ServiceWorkerRegistrationInfo::ServiceWorkerRegistrationInfo(const nsACString& aScope,
                                                             nsIPrincipal* aPrincipal)
  : mControlledDocumentsCounter(0)
  , mUpdateState(NoUpdate)
  , mLastUpdateCheckTime(0)
  , mScope(aScope)
  , mPrincipal(aPrincipal)
  , mUpdating(false)
  , mPendingUninstall(false)
{}

ServiceWorkerRegistrationInfo::~ServiceWorkerRegistrationInfo()
{
  if (IsControllingDocuments()) {
    NS_WARNING("ServiceWorkerRegistrationInfo is still controlling documents. This can be a bug or a leak in ServiceWorker API or in any other API that takes the document alive.");
  }
}

NS_IMPL_ISUPPORTS(ServiceWorkerRegistrationInfo, nsIServiceWorkerRegistrationInfo)

NS_IMETHODIMP
ServiceWorkerRegistrationInfo::GetPrincipal(nsIPrincipal** aPrincipal)
{
  AssertIsOnMainThread();
  NS_ADDREF(*aPrincipal = mPrincipal);
  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerRegistrationInfo::GetScope(nsAString& aScope)
{
  AssertIsOnMainThread();
  CopyUTF8toUTF16(mScope, aScope);
  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerRegistrationInfo::GetScriptSpec(nsAString& aScriptSpec)
{
  AssertIsOnMainThread();
  RefPtr<ServiceWorkerInfo> newest = Newest();
  if (newest) {
    CopyUTF8toUTF16(newest->ScriptSpec(), aScriptSpec);
  }
  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerRegistrationInfo::GetInstallingWorker(nsIServiceWorkerInfo **aResult)
{
  AssertIsOnMainThread();
  nsCOMPtr<nsIServiceWorkerInfo> info = do_QueryInterface(mInstallingWorker);
  info.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerRegistrationInfo::GetWaitingWorker(nsIServiceWorkerInfo **aResult)
{
  AssertIsOnMainThread();
  nsCOMPtr<nsIServiceWorkerInfo> info = do_QueryInterface(mWaitingWorker);
  info.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerRegistrationInfo::GetActiveWorker(nsIServiceWorkerInfo **aResult)
{
  AssertIsOnMainThread();
  nsCOMPtr<nsIServiceWorkerInfo> info = do_QueryInterface(mActiveWorker);
  info.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerRegistrationInfo::GetWorkerByID(uint64_t aID, nsIServiceWorkerInfo **aResult)
{
  AssertIsOnMainThread();
  MOZ_ASSERT(aResult);

  RefPtr<ServiceWorkerInfo> info = GetServiceWorkerInfoById(aID);
  // It is ok to return null for a missing service worker info.
  info.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerRegistrationInfo::AddListener(
                            nsIServiceWorkerRegistrationInfoListener *aListener)
{
  AssertIsOnMainThread();

  if (!aListener || mListeners.Contains(aListener)) {
    return NS_ERROR_INVALID_ARG;
  }

  mListeners.AppendElement(aListener);

  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerRegistrationInfo::RemoveListener(
                            nsIServiceWorkerRegistrationInfoListener *aListener)
{
  AssertIsOnMainThread();

  if (!aListener || !mListeners.Contains(aListener)) {
    return NS_ERROR_INVALID_ARG;
  }

  mListeners.RemoveElement(aListener);

  return NS_OK;
}

already_AddRefed<ServiceWorkerInfo>
ServiceWorkerRegistrationInfo::GetServiceWorkerInfoById(uint64_t aId)
{
  RefPtr<ServiceWorkerInfo> serviceWorker;
  if (mInstallingWorker && mInstallingWorker->ID() == aId) {
    serviceWorker = mInstallingWorker;
  } else if (mWaitingWorker && mWaitingWorker->ID() == aId) {
    serviceWorker = mWaitingWorker;
  } else if (mActiveWorker && mActiveWorker->ID() == aId) {
    serviceWorker = mActiveWorker;
  }

  return serviceWorker.forget();
}

//////////////////////////
// ServiceWorkerManager //
//////////////////////////

NS_IMPL_ADDREF(ServiceWorkerManager)
NS_IMPL_RELEASE(ServiceWorkerManager)

NS_INTERFACE_MAP_BEGIN(ServiceWorkerManager)
  NS_INTERFACE_MAP_ENTRY(nsIServiceWorkerManager)
  NS_INTERFACE_MAP_ENTRY(nsIIPCBackgroundChildCreateCallback)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIServiceWorkerManager)
NS_INTERFACE_MAP_END

ServiceWorkerManager::ServiceWorkerManager()
  : mActor(nullptr)
  , mShuttingDown(false)
{
  // Register this component to PBackground.
  MOZ_ALWAYS_TRUE(BackgroundChild::GetOrCreateForCurrentThread(this));
}

ServiceWorkerManager::~ServiceWorkerManager()
{
  // The map will assert if it is not empty when destroyed.
  mRegistrationInfos.Clear();
  MOZ_ASSERT(!mActor);
}

void
ServiceWorkerManager::Init()
{
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    DebugOnly<nsresult> rv;
    rv = obs->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false /* ownsWeak */);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }

  if (XRE_IsParentProcess()) {
    RefPtr<ServiceWorkerRegistrar> swr = ServiceWorkerRegistrar::Get();
    MOZ_ASSERT(swr);

    nsTArray<ServiceWorkerRegistrationData> data;
    swr->GetRegistrations(data);
    LoadRegistrations(data);

    if (obs) {
      DebugOnly<nsresult> rv;
      rv = obs->AddObserver(this, PURGE_SESSION_HISTORY, false /* ownsWeak */);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
      rv = obs->AddObserver(this, PURGE_DOMAIN_DATA, false /* ownsWeak */);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
      rv = obs->AddObserver(this, CLEAR_ORIGIN_DATA, false /* ownsWeak */);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    }
  }
}

class ContinueLifecycleTask : public nsISupports
{
  NS_DECL_ISUPPORTS

protected:
  virtual ~ContinueLifecycleTask()
  {}

public:
  virtual void ContinueAfterWorkerEvent(bool aSuccess) = 0;
};

NS_IMPL_ISUPPORTS0(ContinueLifecycleTask);

class ServiceWorkerInstallJob;

class ContinueInstallTask final : public ContinueLifecycleTask
{
  RefPtr<ServiceWorkerInstallJob> mJob;

public:
  explicit ContinueInstallTask(ServiceWorkerInstallJob* aJob)
    : mJob(aJob)
  {}

  void ContinueAfterWorkerEvent(bool aSuccess) override;
};

class ContinueActivateTask final : public ContinueLifecycleTask
{
  RefPtr<ServiceWorkerRegistrationInfo> mRegistration;

public:
  explicit ContinueActivateTask(ServiceWorkerRegistrationInfo* aReg)
    : mRegistration(aReg)
  {}

  void
  ContinueAfterWorkerEvent(bool aSuccess) override;
};

class ContinueLifecycleRunnable final : public LifeCycleEventCallback
{
  nsMainThreadPtrHandle<ContinueLifecycleTask> mTask;
  bool mSuccess;

public:
  explicit ContinueLifecycleRunnable(const nsMainThreadPtrHandle<ContinueLifecycleTask>& aTask)
    : mTask(aTask)
    , mSuccess(false)
  {
    AssertIsOnMainThread();
  }

  void
  SetResult(bool aResult) override
  {
    mSuccess = aResult;
  }

  NS_IMETHOD
  Run() override
  {
    AssertIsOnMainThread();
    mTask->ContinueAfterWorkerEvent(mSuccess);
    return NS_OK;
  }
};

class ServiceWorkerResolveWindowPromiseOnUpdateCallback final : public ServiceWorkerUpdateFinishCallback
{
  RefPtr<nsPIDOMWindowInner> mWindow;
  // The promise "returned" by the call to Update up to
  // navigator.serviceWorker.register().
  RefPtr<Promise> mPromise;

  ~ServiceWorkerResolveWindowPromiseOnUpdateCallback()
  {}

public:
  ServiceWorkerResolveWindowPromiseOnUpdateCallback(nsPIDOMWindowInner* aWindow,
                                                    Promise* aPromise)
    : mWindow(aWindow)
    , mPromise(aPromise)
  {}

  void
  UpdateSucceeded(ServiceWorkerRegistrationInfo* aInfo) override
  {
    RefPtr<ServiceWorkerRegistrationMainThread> swr =
      mWindow->GetServiceWorkerRegistration(NS_ConvertUTF8toUTF16(aInfo->mScope));
    mPromise->MaybeResolve(swr);
  }

  void
  UpdateFailed(ErrorResult& aStatus) override
  {
    mPromise->MaybeReject(aStatus);
  }
};

class ContinueUpdateRunnable final : public LifeCycleEventCallback
{
  nsMainThreadPtrHandle<nsISupports> mJob;
  bool mScriptEvaluationResult;
public:
  explicit ContinueUpdateRunnable(const nsMainThreadPtrHandle<nsISupports> aJob)
    : mJob(aJob)
    , mScriptEvaluationResult(false)
  {
    AssertIsOnMainThread();
  }

  void
  SetResult(bool aResult)
  {
    mScriptEvaluationResult = aResult;
  }

  NS_IMETHOD Run();
};

namespace {

/**
 * The spec mandates slightly different behaviors for computing the scope
 * prefix string in case a Service-Worker-Allowed header is specified versus
 * when it's not available.
 *
 * With the header:
 *   "Set maxScopeString to "/" concatenated with the strings in maxScope's
 *    path (including empty strings), separated from each other by "/"."
 * Without the header:
 *   "Set maxScopeString to "/" concatenated with the strings, except the last
 *    string that denotes the script's file name, in registration's registering
 *    script url's path (including empty strings), separated from each other by
 *    "/"."
 *
 * In simpler terms, if the header is not present, we should only use the
 * "directory" part of the pathname, and otherwise the entire pathname should be
 * used.  ScopeStringPrefixMode allows the caller to specify the desired
 * behavior.
 */
enum ScopeStringPrefixMode {
  eUseDirectory,
  eUsePath
};

nsresult
GetRequiredScopeStringPrefix(nsIURI* aScriptURI, nsACString& aPrefix,
                             ScopeStringPrefixMode aPrefixMode)
{
  nsresult rv = aScriptURI->GetPrePath(aPrefix);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (aPrefixMode == eUseDirectory) {
    nsCOMPtr<nsIURL> scriptURL(do_QueryInterface(aScriptURI));
    if (NS_WARN_IF(!scriptURL)) {
      return NS_ERROR_FAILURE;
    }

    nsAutoCString dir;
    rv = scriptURL->GetDirectory(dir);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    aPrefix.Append(dir);
  } else if (aPrefixMode == eUsePath) {
    nsAutoCString path;
    rv = aScriptURI->GetPath(path);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    aPrefix.Append(path);
  } else {
    MOZ_ASSERT_UNREACHABLE("Invalid value for aPrefixMode");
  }
  return NS_OK;
}

class PropagateSoftUpdateRunnable final : public nsRunnable
{
public:
  PropagateSoftUpdateRunnable(const PrincipalOriginAttributes& aOriginAttributes,
                              const nsAString& aScope)
    : mOriginAttributes(aOriginAttributes)
    , mScope(aScope)
  {}

  NS_IMETHOD Run() override
  {
    AssertIsOnMainThread();

    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    MOZ_ASSERT(swm);

    swm->PropagateSoftUpdate(mOriginAttributes, mScope);
    return NS_OK;
  }

private:
  ~PropagateSoftUpdateRunnable()
  {}

  const PrincipalOriginAttributes mOriginAttributes;
  const nsString mScope;
};

class PropagateUnregisterRunnable final : public nsRunnable
{
public:
  PropagateUnregisterRunnable(nsIPrincipal* aPrincipal,
                              nsIServiceWorkerUnregisterCallback* aCallback,
                              const nsAString& aScope)
    : mPrincipal(aPrincipal)
    , mCallback(aCallback)
    , mScope(aScope)
  {
    MOZ_ASSERT(aPrincipal);
  }

  NS_IMETHOD Run() override
  {
    AssertIsOnMainThread();

    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    MOZ_ASSERT(swm);

    nsresult rv = swm->PropagateUnregister(mPrincipal, mCallback, mScope);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    return NS_OK;
  }

private:
  ~PropagateUnregisterRunnable()
  {}

  nsCOMPtr<nsIPrincipal> mPrincipal;
  nsCOMPtr<nsIServiceWorkerUnregisterCallback> mCallback;
  const nsString mScope;
};

class RemoveRunnable final : public nsRunnable
{
public:
  explicit RemoveRunnable(const nsACString& aHost)
  {}

  NS_IMETHOD Run() override
  {
    AssertIsOnMainThread();

    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    MOZ_ASSERT(swm);

    swm->Remove(mHost);
    return NS_OK;
  }

private:
  ~RemoveRunnable()
  {}

  const nsCString mHost;
};

class PropagateRemoveRunnable final : public nsRunnable
{
public:
  explicit PropagateRemoveRunnable(const nsACString& aHost)
  {}

  NS_IMETHOD Run() override
  {
    AssertIsOnMainThread();

    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    MOZ_ASSERT(swm);

    swm->PropagateRemove(mHost);
    return NS_OK;
  }

private:
  ~PropagateRemoveRunnable()
  {}

  const nsCString mHost;
};

class PropagateRemoveAllRunnable final : public nsRunnable
{
public:
  PropagateRemoveAllRunnable()
  {}

  NS_IMETHOD Run() override
  {
    AssertIsOnMainThread();

    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    MOZ_ASSERT(swm);

    swm->PropagateRemoveAll();
    return NS_OK;
  }

private:
  ~PropagateRemoveAllRunnable()
  {}
};

} // namespace

class ServiceWorkerJobBase : public ServiceWorkerJob
{
public:
  ServiceWorkerJobBase(ServiceWorkerJobQueue* aQueue,
                       ServiceWorkerJob::Type aJobType,
                       nsIPrincipal* aPrincipal,
                       const nsACString& aScope,
                       const nsACString& aScriptSpec,
                       ServiceWorkerUpdateFinishCallback* aCallback,
                       ServiceWorkerInfo* aServiceWorkerInfo)
    : ServiceWorkerJob(aQueue, aJobType)
    , mPrincipal(aPrincipal)
    , mScope(aScope)
    , mScriptSpec(aScriptSpec)
    , mCallback(aCallback)
    , mUpdateAndInstallInfo(aServiceWorkerInfo)
    , mCanceled(false)
  {
    AssertIsOnMainThread();
    MOZ_ASSERT(aPrincipal);
  }

  void
  Cancel()
  {
    mQueue = nullptr;
    mCanceled = true;
  }

protected:
  nsCOMPtr<nsIPrincipal> mPrincipal;
  const nsCString mScope;
  const nsCString mScriptSpec;
  RefPtr<ServiceWorkerUpdateFinishCallback> mCallback;
  RefPtr<ServiceWorkerRegistrationInfo> mRegistration;
  RefPtr<ServiceWorkerInfo> mUpdateAndInstallInfo;
  bool mCanceled;

  ~ServiceWorkerJobBase()
  { }

  // Ensure that mRegistration is set for the job.  Also, if mRegistration was
  // already set, ensure that a new registration object has not replaced it in
  // the ServiceWorkerManager.  This can happen when jobs race such that the
  // registration is cleared and recreated while an update job is executing.
  nsresult
  EnsureAndVerifyRegistration()
  {
    AssertIsOnMainThread();

    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    if (NS_WARN_IF(!swm)) {
      mRegistration = nullptr;
      return NS_ERROR_NOT_AVAILABLE;
    }

    RefPtr<ServiceWorkerRegistrationInfo> registration =
      swm->GetRegistration(mPrincipal, mScope);

    if (NS_WARN_IF(!registration)) {
      mRegistration = nullptr;
      return NS_ERROR_NOT_AVAILABLE;
    }

    if (NS_WARN_IF(mRegistration && registration != mRegistration)) {
      mRegistration = nullptr;
      return NS_ERROR_NOT_AVAILABLE;
    }

    mRegistration = registration.forget();
    return NS_OK;
  }

  void
  Succeed()
  {
    AssertIsOnMainThread();
    // We don't have a callback for soft updates.
    if (mCallback) {
      mCallback->UpdateSucceeded(mRegistration);
      mCallback = nullptr;
    }
  }

  // This MUST only be called when the job is still performing actions related
  // to registration or update. After the spec resolves the update promise, use
  // Done() with the failure code instead.
  // Callers MUST hold a strong ref before calling this!
  void
  FailWithErrorResult(ErrorResult& aRv)
  {
    AssertIsOnMainThread();

    // With cancellation support, we may only be running with one reference
    // from another object like a stream loader or something.
    RefPtr<ServiceWorkerJob> kungFuDeathGrip = this;

    // Save off the plain error code to pass to Done() where its logged to
    // stderr as a warning.
    nsresult origStatus = static_cast<nsresult>(aRv.ErrorCodeAsInt());

    // Ensure that we only surface SecurityErr or TypeErr to script.
    if (aRv.Failed() && !aRv.ErrorCodeIs(NS_ERROR_DOM_SECURITY_ERR) &&
                        !aRv.ErrorCodeIs(NS_ERROR_DOM_TYPE_ERR)) {

      // Remove the old error code so we can replace it with a TypeError.
      aRv.SuppressException();

      NS_ConvertUTF8toUTF16 scriptSpec(mScriptSpec);
      NS_ConvertUTF8toUTF16 scope(mScope);

      // Throw the type error with a generic error message.
      aRv.ThrowTypeError<MSG_SW_INSTALL_ERROR>(scriptSpec, scope);
    }

    if (mCallback) {
      mCallback->UpdateFailed(aRv);
      mCallback = nullptr;
    }
    // In case the callback does not consume the exception
    aRv.SuppressException();

    mUpdateAndInstallInfo = nullptr;

    if (!mRegistration) {
      Done(origStatus);
      return;
    }

    if (mRegistration->mInstallingWorker) {
      nsresult rv = serviceWorkerScriptCache::PurgeCache(mPrincipal,
                                                         mRegistration->mInstallingWorker->CacheName());
      if (NS_FAILED(rv)) {
        NS_WARNING("Failed to purge the installing worker cache.");
      }
    }

    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    swm->MaybeRemoveRegistration(mRegistration);
    // Ensures that the job can't do anything useful from this point on.
    mRegistration = nullptr;
    Done(origStatus);
  }

  void
  Fail(nsresult aRv)
  {
    ErrorResult rv(aRv);
    FailWithErrorResult(rv);
  }
};

class ServiceWorkerInstallJob final : public ServiceWorkerJobBase
{
  friend class ContinueInstallTask;

public:
  enum InstallType {
    UpdateSameScript,
    OverwriteScript
  };

  ServiceWorkerInstallJob(ServiceWorkerJobQueue* aQueue,
                          nsIPrincipal* aPrincipal,
                          const nsACString& aScope,
                          const nsACString& aScriptSpec,
                          ServiceWorkerUpdateFinishCallback* aCallback,
                          ServiceWorkerInfo* aServiceWorkerInfo,
                          InstallType aType)
    : ServiceWorkerJobBase(aQueue, Type::InstallJob, aPrincipal, aScope,
                           aScriptSpec, aCallback, aServiceWorkerInfo)
    , mType(aType)
  {
  }

  void
  Start()
  {
    AssertIsOnMainThread();
    nsCOMPtr<nsIRunnable> r =
      NS_NewRunnableMethod(this, &ServiceWorkerInstallJob::Install);
    MOZ_ALWAYS_TRUE(NS_SUCCEEDED(NS_DispatchToMainThread(r)));
  }

  void
  Install()
  {
    RefPtr<ServiceWorkerJob> kungFuDeathGrip = this;

    if (mCanceled) {
      return Fail(NS_ERROR_DOM_ABORT_ERR);
    }

    nsresult rv = EnsureAndVerifyRegistration();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Fail(NS_ERROR_DOM_ABORT_ERR);
    }

    // If we are trying to install an update for an existing script, then
    // make sure we don't overwrite a recent script change or resurrect a
    // dead registration.
    if (mType == UpdateSameScript) {
      RefPtr<ServiceWorkerInfo> newest = mRegistration->Newest();
      if (!newest || !mScriptSpec.Equals(newest->ScriptSpec())) {
        return Fail(NS_ERROR_DOM_ABORT_ERR);
      }
    }

    // Begin [[Install]] atomic step 3.
    if (mRegistration->mInstallingWorker) {
      mRegistration->mInstallingWorker->UpdateState(ServiceWorkerState::Redundant);
      mRegistration->mInstallingWorker->WorkerPrivate()->TerminateWorker();
    }

    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    swm->InvalidateServiceWorkerRegistrationWorker(mRegistration,
                                                   WhichServiceWorker::INSTALLING_WORKER);

    mRegistration->mInstallingWorker = mUpdateAndInstallInfo.forget();
    mRegistration->mInstallingWorker->UpdateState(ServiceWorkerState::Installing);
    mRegistration->NotifyListenersOnChange();

    Succeed();

    // The job should NOT call fail from this point on.

    // Step 8 "Queue a task..." for updatefound.
    nsCOMPtr<nsIRunnable> upr =
      NS_NewRunnableMethodWithArg<ServiceWorkerRegistrationInfo*>(
        swm,
        &ServiceWorkerManager::FireUpdateFoundOnServiceWorkerRegistrations,
        mRegistration);

    NS_DispatchToMainThread(upr);

    // Call ContinueAfterInstallEvent(false) on main thread if the SW
    // script fails to load.
    nsCOMPtr<nsIRunnable> failRunnable = NS_NewRunnableMethodWithArgs<bool>
      (this, &ServiceWorkerInstallJob::ContinueAfterInstallEvent, false);

    nsMainThreadPtrHandle<ContinueLifecycleTask> installTask(
      new nsMainThreadPtrHolder<ContinueLifecycleTask>(new ContinueInstallTask(this)));
    RefPtr<LifeCycleEventCallback> callback = new ContinueLifecycleRunnable(installTask);

    // This triggers Step 4.7 "Queue a task to run the following substeps..."
    // which sends the install event to the worker.
    ServiceWorkerPrivate* workerPrivate =
      mRegistration->mInstallingWorker->WorkerPrivate();
    rv = workerPrivate->SendLifeCycleEvent(NS_LITERAL_STRING("install"),
                                           callback, failRunnable);

    if (NS_WARN_IF(NS_FAILED(rv))) {
      ContinueAfterInstallEvent(false /* aSuccess */);
    }
  }

  void
  ContinueAfterInstallEvent(bool aInstallEventSuccess)
  {
    if (mCanceled) {
      return Done(NS_ERROR_DOM_ABORT_ERR);
    }

    nsresult rv = EnsureAndVerifyRegistration();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Fail(NS_ERROR_DOM_ABORT_ERR);
    }

    if (!mRegistration->mInstallingWorker) {
      NS_WARNING("mInstallingWorker was null.");
      return Done(NS_ERROR_DOM_ABORT_ERR);
    }

    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();

    // "If installFailed is true"
    if (NS_WARN_IF(!aInstallEventSuccess)) {
      mRegistration->mInstallingWorker->UpdateState(ServiceWorkerState::Redundant);
      mRegistration->mInstallingWorker = nullptr;
      swm->InvalidateServiceWorkerRegistrationWorker(mRegistration,
                                                     WhichServiceWorker::INSTALLING_WORKER);
      swm->MaybeRemoveRegistration(mRegistration);
      return Done(NS_ERROR_DOM_ABORT_ERR);
    }

    // "If registration's waiting worker is not null"
    if (mRegistration->mWaitingWorker) {
      mRegistration->mWaitingWorker->WorkerPrivate()->TerminateWorker();
      mRegistration->mWaitingWorker->UpdateState(ServiceWorkerState::Redundant);

      nsresult rv =
        serviceWorkerScriptCache::PurgeCache(mRegistration->mPrincipal,
                                             mRegistration->mWaitingWorker->CacheName());
      if (NS_FAILED(rv)) {
        NS_WARNING("Failed to purge the old waiting cache.");
      }
    }

    mRegistration->mWaitingWorker = mRegistration->mInstallingWorker.forget();
    mRegistration->mWaitingWorker->UpdateState(ServiceWorkerState::Installed);
    mRegistration->NotifyListenersOnChange();
    swm->StoreRegistration(mPrincipal, mRegistration);
    swm->InvalidateServiceWorkerRegistrationWorker(mRegistration,
                                                   WhichServiceWorker::INSTALLING_WORKER | WhichServiceWorker::WAITING_WORKER);

    Done(NS_OK);
    // Activate() is invoked out of band of atomic.
    mRegistration->TryToActivateAsync();
  }

private:
  const InstallType mType;
};

class ServiceWorkerRegisterJob final : public ServiceWorkerJobBase,
                                       public serviceWorkerScriptCache::CompareCallback
{
  friend class ContinueUpdateRunnable;

  nsCOMPtr<nsILoadGroup> mLoadGroup;

  ~ServiceWorkerRegisterJob()
  { }

public:
  NS_DECL_ISUPPORTS_INHERITED

  // [[Register]]
  ServiceWorkerRegisterJob(ServiceWorkerJobQueue* aQueue,
                           nsIPrincipal* aPrincipal,
                           const nsACString& aScope,
                           const nsACString& aScriptSpec,
                           ServiceWorkerUpdateFinishCallback* aCallback,
                           nsILoadGroup* aLoadGroup)
    : ServiceWorkerJobBase(aQueue, Type::RegisterJob, aPrincipal, aScope,
                           aScriptSpec, aCallback, nullptr)
    , mLoadGroup(aLoadGroup)
  {
    AssertIsOnMainThread();
    MOZ_ASSERT(mLoadGroup);
    MOZ_ASSERT(aCallback);
  }

  // [[Update]]
  ServiceWorkerRegisterJob(ServiceWorkerJobQueue* aQueue,
                           nsIPrincipal* aPrincipal,
                           const nsACString& aScope,
                           const nsACString& aScriptSpec,
                           ServiceWorkerUpdateFinishCallback* aCallback)
    : ServiceWorkerJobBase(aQueue, Type::UpdateJob, aPrincipal, aScope,
                           aScriptSpec, aCallback, nullptr)
  {
    AssertIsOnMainThread();
  }

  void
  Start() override
  {
    AssertIsOnMainThread();
    MOZ_ASSERT(!mCanceled);

    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    if (!swm->HasBackgroundActor()) {
      nsCOMPtr<nsIRunnable> runnable =
        NS_NewRunnableMethod(this, &ServiceWorkerRegisterJob::Start);
      swm->AppendPendingOperation(runnable);
      return;
    }

    if (mJobType == RegisterJob) {
      MOZ_ASSERT(!mRegistration);
      mRegistration = swm->GetRegistration(mPrincipal, mScope);

      if (mRegistration) {
        mRegistration->mPendingUninstall = false;
        RefPtr<ServiceWorkerInfo> newest = mRegistration->Newest();
        if (newest && mScriptSpec.Equals(newest->ScriptSpec())) {
          Succeed();

          // Done() must always be called async from Start()
          nsCOMPtr<nsIRunnable> runnable =
            NS_NewRunnableMethodWithArg<nsresult>(
              this,
              &ServiceWorkerRegisterJob::Done,
              NS_OK);
          MOZ_ALWAYS_TRUE(NS_SUCCEEDED(NS_DispatchToCurrentThread(runnable)));

          return;
        }
      } else {
        mRegistration = swm->CreateNewRegistration(mScope, mPrincipal);
      }
    } else {
      MOZ_ASSERT(mJobType == UpdateJob);

      nsresult rv = EnsureAndVerifyRegistration();
      if (NS_WARN_IF(NS_FAILED(rv))) {
        // Do nothing here, but since mRegistration is nullptr we will
        // trigger the async Fail() call below.
        MOZ_ASSERT(!mRegistration);
      }

      // If a different script spec has been registered between when this update
      // was scheduled and it running now, then simply abort.
      RefPtr<ServiceWorkerInfo> newest = mRegistration ? mRegistration->Newest()
                                                       : nullptr;
      if (!mRegistration ||
          (newest && !mScriptSpec.Equals(newest->ScriptSpec()))) {

        // Done() must always be called async from Start()
        nsCOMPtr<nsIRunnable> runnable =
          NS_NewRunnableMethodWithArg<nsresult>(
            this,
            &ServiceWorkerRegisterJob::Fail,
            NS_ERROR_DOM_ABORT_ERR);
          MOZ_ALWAYS_TRUE(NS_SUCCEEDED(NS_DispatchToCurrentThread(runnable)));

        return;
      }
    }

    Update();
  }

  void
  ComparisonResult(nsresult aStatus, bool aInCacheAndEqual,
                   const nsAString& aNewCacheName,
                   const nsACString& aMaxScope) override
  {
    RefPtr<ServiceWorkerRegisterJob> kungFuDeathGrip = this;

    if (NS_WARN_IF(mCanceled)) {
      Fail(NS_ERROR_DOM_ABORT_ERR);
      return;
    }

    if (NS_WARN_IF(NS_FAILED(aStatus))) {
      Fail(aStatus);
      return;
    }

    nsresult rv = EnsureAndVerifyRegistration();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Fail(NS_ERROR_DOM_ABORT_ERR);
    }

    if (aInCacheAndEqual) {
      Succeed();
      Done(NS_OK);
      return;
    }

    AssertIsOnMainThread();
    Telemetry::Accumulate(Telemetry::SERVICE_WORKER_UPDATED, 1);

    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();

    nsCOMPtr<nsIURI> scriptURI;
    rv = NS_NewURI(getter_AddRefs(scriptURI), mScriptSpec);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      Fail(NS_ERROR_DOM_SECURITY_ERR);
      return;
    }
    nsCOMPtr<nsIURI> maxScopeURI;
    if (!aMaxScope.IsEmpty()) {
      rv = NS_NewURI(getter_AddRefs(maxScopeURI), aMaxScope,
                     nullptr, scriptURI);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        Fail(NS_ERROR_DOM_SECURITY_ERR);
        return;
      }
    }

    nsAutoCString defaultAllowedPrefix;
    rv = GetRequiredScopeStringPrefix(scriptURI, defaultAllowedPrefix,
                                      eUseDirectory);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      Fail(NS_ERROR_DOM_SECURITY_ERR);
      return;
    }
    nsAutoCString maxPrefix(defaultAllowedPrefix);
    if (maxScopeURI) {
      rv = GetRequiredScopeStringPrefix(maxScopeURI, maxPrefix, eUsePath);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        Fail(NS_ERROR_DOM_SECURITY_ERR);
        return;
      }
    }

    if (!StringBeginsWith(mRegistration->mScope, maxPrefix)) {
      NS_WARNING("By default a service worker's scope is restricted to at or below it's script's location.");
      Fail(NS_ERROR_DOM_SECURITY_ERR);
      return;
    }

    nsAutoCString scopeKey;
    rv = swm->PrincipalToScopeKey(mRegistration->mPrincipal, scopeKey);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Fail(NS_ERROR_FAILURE);
    }

    ServiceWorkerManager::RegistrationDataPerPrincipal* data;
    if (!swm->mRegistrationInfos.Get(scopeKey, &data)) {
      return Fail(NS_ERROR_FAILURE);
    }

    MOZ_ASSERT(!mUpdateAndInstallInfo);
    mUpdateAndInstallInfo =
      new ServiceWorkerInfo(mRegistration, mScriptSpec, aNewCacheName);

    RefPtr<ServiceWorkerJob> upcasted = this;
    nsMainThreadPtrHandle<nsISupports> handle(
        new nsMainThreadPtrHolder<nsISupports>(upcasted));
    RefPtr<LifeCycleEventCallback> callback = new ContinueUpdateRunnable(handle);

    ServiceWorkerPrivate* workerPrivate =
      mUpdateAndInstallInfo->WorkerPrivate();
    rv = workerPrivate->CheckScriptEvaluation(callback);

    if (NS_WARN_IF(NS_FAILED(rv))) {
      Fail(NS_ERROR_DOM_ABORT_ERR);
    }
  }

private:
  // This will perform steps 27 and 28 from [[Update]]
  // Remove the job from the registration queue and invoke [[Install]]
  void
  ContinueInstall(bool aScriptEvaluationResult)
  {
    AssertIsOnMainThread();

    nsresult rv = EnsureAndVerifyRegistration();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Fail(NS_ERROR_DOM_ABORT_ERR);
    }

    mRegistration->mUpdating = false;

    RefPtr<ServiceWorkerRegisterJob> kungFuDeathGrip = this;
    if (mCanceled) {
      return Fail(NS_ERROR_DOM_ABORT_ERR);
    }

    if (NS_WARN_IF(!aScriptEvaluationResult)) {
      ErrorResult error;

      NS_ConvertUTF8toUTF16 scriptSpec(mScriptSpec);
      NS_ConvertUTF8toUTF16 scope(mRegistration->mScope);
      error.ThrowTypeError<MSG_SW_SCRIPT_THREW>(scriptSpec, scope);
      return FailWithErrorResult(error);
    }

    // For updates we want to make sure our install job does not end up
    // changing the script for the registration.  Since a registration
    // script change can be queued in an install job, we can not
    // conclusively verify that the update install should proceed here.
    // Instead, we have to pass a flag into our install job indicating
    // if a script change is allowed or not.  This can then be used to
    // check the current script after all previous install jobs have been
    // flushed.
    ServiceWorkerInstallJob::InstallType installType =
      mJobType == UpdateJob ? ServiceWorkerInstallJob::UpdateSameScript
                            : ServiceWorkerInstallJob::OverwriteScript;

    RefPtr<ServiceWorkerInstallJob> job =
      new ServiceWorkerInstallJob(mQueue, mPrincipal, mScope, mScriptSpec,
                                  mCallback, mUpdateAndInstallInfo,
                                  installType);
    mQueue->Append(job);
    Done(NS_OK);
  }

  void
  Update()
  {
    AssertIsOnMainThread();

    // Since Update() is called synchronously from Start(), we can assert this.
    MOZ_ASSERT(!mCanceled);
    MOZ_ASSERT(mRegistration);
    nsCOMPtr<nsIRunnable> r =
      NS_NewRunnableMethod(this, &ServiceWorkerRegisterJob::ContinueUpdate);
    NS_DispatchToMainThread(r);

    mRegistration->mUpdating = true;
  }

  // Aspects of (actually the whole algorithm) of [[Update]] after
  // "Run the following steps in parallel."
  void
  ContinueUpdate()
  {
    AssertIsOnMainThread();
    RefPtr<ServiceWorkerRegisterJob> kungFuDeathGrip = this;

    if (mCanceled) {
      return Fail(NS_ERROR_DOM_ABORT_ERR);
    }

    nsresult rv = EnsureAndVerifyRegistration();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Fail(NS_ERROR_DOM_ABORT_ERR);
    }

    if (mRegistration->mInstallingWorker) {
      mRegistration->mInstallingWorker->UpdateState(ServiceWorkerState::Redundant);
      mRegistration->mInstallingWorker->WorkerPrivate()->TerminateWorker();
      mRegistration->mInstallingWorker = nullptr;
    }

    RefPtr<ServiceWorkerInfo> workerInfo = mRegistration->Newest();
    nsAutoString cacheName;

    // 9.2.20 If newestWorker is not null, and newestWorker's script url is
    // equal to registration's registering script url and response is a
    // byte-for-byte match with the script resource of newestWorker...
    if (workerInfo && workerInfo->ScriptSpec().Equals(mScriptSpec)) {
      cacheName = workerInfo->CacheName();
    }

    rv = serviceWorkerScriptCache::Compare(mRegistration, mPrincipal, cacheName,
                                           NS_ConvertUTF8toUTF16(mScriptSpec),
                                           this, mLoadGroup);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Fail(rv);
    }
  }

  void
  Done(nsresult aStatus)
  {
    AssertIsOnMainThread();

    if (mRegistration) {
      mRegistration->mUpdating = false;
    }

    ServiceWorkerJob::Done(aStatus);
  }
};

NS_IMPL_ISUPPORTS_INHERITED0(ServiceWorkerRegisterJob, ServiceWorkerJob);

void
ServiceWorkerJobQueue::CancelJobs()
{
  // The order doesn't matter. Cancel() just sets a flag on these jobs.
  CancelJobs(mRegistrationJobQueue);
  CancelJobs(mInstallationJobQueue);
}

void
ServiceWorkerJobQueue::CancelJobs(QueueData& aQueue)
{
  if (aQueue.mJobs.IsEmpty()) {
    return;
  }

  // We have to treat the first job specially. It is the running job and needs
  // to be notified correctly.
  RefPtr<ServiceWorkerJob> runningJob = aQueue.mJobs[0];
  // We can just let an Unregister job run to completion.
  if (runningJob->IsRegisterOrInstallJob()) {
    ServiceWorkerJobBase* job = static_cast<ServiceWorkerJobBase*>(runningJob.get());
    job->Cancel();
  }

  // Get rid of everything. Non-main thread objects may still be holding a ref
  // to the running register job. Since we called Cancel() on it, the job's
  // main thread functions will just exit.
  aQueue.mJobs.Clear();
}

NS_IMETHODIMP
ContinueUpdateRunnable::Run()
{
  AssertIsOnMainThread();
  RefPtr<ServiceWorkerJob> job = static_cast<ServiceWorkerJob*>(mJob.get());
  RefPtr<ServiceWorkerRegisterJob> upjob = static_cast<ServiceWorkerRegisterJob*>(job.get());
  upjob->ContinueInstall(mScriptEvaluationResult);
  return NS_OK;
}

void
ContinueInstallTask::ContinueAfterWorkerEvent(bool aSuccess)
{
  // This does not start the job immediately if there are other jobs in the
  // queue, which captures the "atomic" behaviour we want.
  mJob->ContinueAfterInstallEvent(aSuccess);
}

// This function implements parts of the step 3 of the following algorithm:
// https://w3c.github.io/webappsec/specs/powerfulfeatures/#settings-secure
static bool
IsFromAuthenticatedOrigin(nsIDocument* aDoc)
{
  MOZ_ASSERT(aDoc);
  nsCOMPtr<nsIDocument> doc(aDoc);
  nsCOMPtr<nsIContentSecurityManager> csm = do_GetService(NS_CONTENTSECURITYMANAGER_CONTRACTID);
  if (NS_WARN_IF(!csm)) {
    return false;
  }

  while (doc && !nsContentUtils::IsChromeDoc(doc)) {
    bool trustworthyURI = false;

    // The origin of the document may be different from the document URI
    // itself.  Check the principal, not the document URI itself.
    nsCOMPtr<nsIPrincipal> documentPrincipal = doc->NodePrincipal();

    // The check for IsChromeDoc() above should mean we never see a system
    // principal inside the loop.
    MOZ_ASSERT(!nsContentUtils::IsSystemPrincipal(documentPrincipal));

    // Pass the principal as a URI to the security manager
    nsCOMPtr<nsIURI> uri;
    documentPrincipal->GetURI(getter_AddRefs(uri));
    if (NS_WARN_IF(!uri)) {
      return false;
    }

    csm->IsURIPotentiallyTrustworthy(uri, &trustworthyURI);
    if (!trustworthyURI) {
      return false;
    }

    doc = doc->GetParentDocument();
  }
  return true;
}

// If we return an error code here, the ServiceWorkerContainer will
// automatically reject the Promise.
NS_IMETHODIMP
ServiceWorkerManager::Register(mozIDOMWindow* aWindow,
                               nsIURI* aScopeURI,
                               nsIURI* aScriptURI,
                               nsISupports** aPromise)
{
  AssertIsOnMainThread();

  if (NS_WARN_IF(!aWindow)) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  auto* window = nsPIDOMWindowInner::From(aWindow);
  nsCOMPtr<nsIDocument> doc = window->GetExtantDoc();
  if (!doc) {
    return NS_ERROR_FAILURE;
  }

  // Don't allow service workers to register when the *document* is chrome.
  if (NS_WARN_IF(nsContentUtils::IsSystemPrincipal(doc->NodePrincipal()))) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  nsCOMPtr<nsPIDOMWindowOuter> outerWindow = window->GetOuterWindow();
  bool serviceWorkersTestingEnabled =
    outerWindow->GetServiceWorkersTestingEnabled();

  bool authenticatedOrigin;
  if (Preferences::GetBool("dom.serviceWorkers.testing.enabled") ||
      serviceWorkersTestingEnabled) {
    authenticatedOrigin = true;
  } else {
    authenticatedOrigin = IsFromAuthenticatedOrigin(doc);
  }

  if (!authenticatedOrigin) {
    NS_WARNING("ServiceWorker registration from insecure websites is not allowed.");
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  // Data URLs are not allowed.
  nsCOMPtr<nsIPrincipal> documentPrincipal = doc->NodePrincipal();

  nsresult rv = documentPrincipal->CheckMayLoad(aScriptURI, true /* report */,
                                                false /* allowIfInheritsPrincipal */);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  // Check content policy.
  int16_t decision = nsIContentPolicy::ACCEPT;
  rv = NS_CheckContentLoadPolicy(nsIContentPolicy::TYPE_INTERNAL_SERVICE_WORKER,
                                 aScriptURI,
                                 documentPrincipal,
                                 doc,
                                 EmptyCString(),
                                 nullptr,
                                 &decision);
  NS_ENSURE_SUCCESS(rv, rv);
  if (NS_WARN_IF(decision != nsIContentPolicy::ACCEPT)) {
    return NS_ERROR_CONTENT_BLOCKED;
  }


  rv = documentPrincipal->CheckMayLoad(aScopeURI, true /* report */,
                                       false /* allowIfInheritsPrinciple */);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  // The IsURIPotentiallyTrustworthy() check allows file:// and possibly other
  // URI schemes.  We need to explicitly only allows http and https schemes.
  // Note, we just use the aScriptURI here for the check since its already
  // been verified as same origin with the document principal.  This also
  // is a good block against accidentally allowing blob: script URIs which
  // might inherit the origin.
  bool isHttp = false;
  bool isHttps = false;
  aScriptURI->SchemeIs("http", &isHttp);
  aScriptURI->SchemeIs("https", &isHttps);
  if (NS_WARN_IF(!isHttp && !isHttps)) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  nsCString cleanedScope;
  rv = aScopeURI->GetSpecIgnoringRef(cleanedScope);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_ERROR_FAILURE;
  }

  nsAutoCString spec;
  rv = aScriptURI->GetSpecIgnoringRef(spec);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsCOMPtr<nsIGlobalObject> sgo = do_QueryInterface(window);
  ErrorResult result;
  RefPtr<Promise> promise = Promise::Create(sgo, result);
  if (result.Failed()) {
    return result.StealNSResult();
  }

  nsAutoCString originSuffix;
  rv = PrincipalToScopeKey(documentPrincipal, originSuffix);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  AddRegisteringDocument(cleanedScope, doc);

  ServiceWorkerJobQueue* queue = GetOrCreateJobQueue(originSuffix, cleanedScope);
  MOZ_ASSERT(queue);

  RefPtr<ServiceWorkerResolveWindowPromiseOnUpdateCallback> cb =
    new ServiceWorkerResolveWindowPromiseOnUpdateCallback(window, promise);

  nsCOMPtr<nsILoadGroup> docLoadGroup = doc->GetDocumentLoadGroup();
  RefPtr<WorkerLoadInfo::InterfaceRequestor> ir =
    new WorkerLoadInfo::InterfaceRequestor(documentPrincipal, docLoadGroup);
  ir->MaybeAddTabChild(docLoadGroup);

  // Create a load group that is separate from, yet related to, the document's load group.
  // This allows checks for interfaces like nsILoadContext to yield the values used by the
  // the document, yet will not cancel the update job if the document's load group is cancelled.
  nsCOMPtr<nsILoadGroup> loadGroup = do_CreateInstance(NS_LOADGROUP_CONTRACTID);
  rv = loadGroup->SetNotificationCallbacks(ir);
  MOZ_ALWAYS_TRUE(NS_SUCCEEDED(rv));

  RefPtr<ServiceWorkerRegisterJob> job =
    new ServiceWorkerRegisterJob(queue, documentPrincipal, cleanedScope, spec,
                                 cb, loadGroup);
  queue->Append(job);

  AssertIsOnMainThread();
  Telemetry::Accumulate(Telemetry::SERVICE_WORKER_REGISTRATIONS, 1);

  promise.forget(aPromise);
  return NS_OK;
}

void
ServiceWorkerManager::AppendPendingOperation(ServiceWorkerJobQueue* aQueue,
                                             ServiceWorkerJob* aJob)
{
  MOZ_ASSERT(!mActor);
  MOZ_ASSERT(aQueue);
  MOZ_ASSERT(aJob);

  if (!mShuttingDown) {
    PendingOperation* opt = mPendingOperations.AppendElement();
    opt->mQueue = aQueue;
    opt->mJob = aJob;
  }
}

void
ServiceWorkerManager::AppendPendingOperation(nsIRunnable* aRunnable)
{
  MOZ_ASSERT(!mActor);
  MOZ_ASSERT(aRunnable);

  if (!mShuttingDown) {
    PendingOperation* opt = mPendingOperations.AppendElement();
    opt->mRunnable = aRunnable;
  }
}

void
ServiceWorkerRegistrationInfo::TryToActivateAsync()
{
  nsCOMPtr<nsIRunnable> r =
  NS_NewRunnableMethod(this,
                       &ServiceWorkerRegistrationInfo::TryToActivate);
  MOZ_ALWAYS_TRUE(NS_SUCCEEDED(NS_DispatchToMainThread(r)));
}

/*
 * TryToActivate should not be called directly, use TryToACtivateAsync instead.
 */
void
ServiceWorkerRegistrationInfo::TryToActivate()
{
  if (!IsControllingDocuments() ||
      // Waiting worker will be removed if the registration is removed
      (mWaitingWorker && mWaitingWorker->SkipWaitingFlag())) {
    Activate();
  }
}

void
ContinueActivateTask::ContinueAfterWorkerEvent(bool aSuccess)
{
  mRegistration->FinishActivate(aSuccess);
}

void
ServiceWorkerRegistrationInfo::PurgeActiveWorker()
{
  RefPtr<ServiceWorkerInfo> exitingWorker = mActiveWorker.forget();
  if (!exitingWorker)
    return;

  // FIXME(jaoo): Bug 1170543 - Wait for exitingWorker to finish and terminate it.
  exitingWorker->UpdateState(ServiceWorkerState::Redundant);
  nsresult rv = serviceWorkerScriptCache::PurgeCache(mPrincipal,
                                                     exitingWorker->CacheName());
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to purge the activating cache.");
  }
  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  swm->InvalidateServiceWorkerRegistrationWorker(this, WhichServiceWorker::ACTIVE_WORKER);
}

void
ServiceWorkerRegistrationInfo::Activate()
{
  RefPtr<ServiceWorkerInfo> activatingWorker = mWaitingWorker;
  if (!activatingWorker) {
    return;
  }

  PurgeActiveWorker();

  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  swm->InvalidateServiceWorkerRegistrationWorker(this, WhichServiceWorker::WAITING_WORKER);

  mActiveWorker = activatingWorker.forget();
  mWaitingWorker = nullptr;
  mActiveWorker->UpdateState(ServiceWorkerState::Activating);
  NotifyListenersOnChange();

  // FIXME(nsm): Unlink appcache if there is one.

  swm->CheckPendingReadyPromises();

  // "Queue a task to fire a simple event named controllerchange..."
  nsCOMPtr<nsIRunnable> controllerChangeRunnable =
    NS_NewRunnableMethodWithArg<ServiceWorkerRegistrationInfo*>(swm,
                                                                &ServiceWorkerManager::FireControllerChange,
                                                                this);
  NS_DispatchToMainThread(controllerChangeRunnable);

  nsCOMPtr<nsIRunnable> failRunnable =
    NS_NewRunnableMethodWithArg<bool>(this,
                                      &ServiceWorkerRegistrationInfo::FinishActivate,
                                      false /* success */);

  nsMainThreadPtrHandle<ContinueLifecycleTask> continueActivateTask(
    new nsMainThreadPtrHolder<ContinueLifecycleTask>(new ContinueActivateTask(this)));
  RefPtr<LifeCycleEventCallback> callback =
    new ContinueLifecycleRunnable(continueActivateTask);

  ServiceWorkerPrivate* workerPrivate = mActiveWorker->WorkerPrivate();
  nsresult rv = workerPrivate->SendLifeCycleEvent(NS_LITERAL_STRING("activate"),
                                                  callback, failRunnable);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_ALWAYS_TRUE(NS_SUCCEEDED(NS_DispatchToMainThread(failRunnable)));
    return;
  }
}

/*
 * Implements the async aspects of the getRegistrations algorithm.
 */
class GetRegistrationsRunnable final : public nsRunnable
{
  nsCOMPtr<nsPIDOMWindowInner> mWindow;
  RefPtr<Promise> mPromise;
public:
  GetRegistrationsRunnable(nsPIDOMWindowInner* aWindow, Promise* aPromise)
    : mWindow(aWindow), mPromise(aPromise)
  {}

  NS_IMETHODIMP
  Run()
  {
    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();

    nsIDocument* doc = mWindow->GetExtantDoc();
    if (!doc) {
      mPromise->MaybeReject(NS_ERROR_UNEXPECTED);
      return NS_OK;
    }

    nsCOMPtr<nsIURI> docURI = doc->GetDocumentURI();
    if (!docURI) {
      mPromise->MaybeReject(NS_ERROR_UNEXPECTED);
      return NS_OK;
    }

    nsCOMPtr<nsIPrincipal> principal = doc->NodePrincipal();
    if (!principal) {
      mPromise->MaybeReject(NS_ERROR_UNEXPECTED);
      return NS_OK;
    }

    nsTArray<RefPtr<ServiceWorkerRegistrationMainThread>> array;

    if (NS_WARN_IF(!BasePrincipal::Cast(principal)->IsCodebasePrincipal())) {
      return NS_OK;
    }

    nsAutoCString scopeKey;
    nsresult rv = swm->PrincipalToScopeKey(principal, scopeKey);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    ServiceWorkerManager::RegistrationDataPerPrincipal* data;
    if (!swm->mRegistrationInfos.Get(scopeKey, &data)) {
      mPromise->MaybeResolve(array);
      return NS_OK;
    }

    for (uint32_t i = 0; i < data->mOrderedScopes.Length(); ++i) {
      NS_ConvertUTF8toUTF16 scope(data->mOrderedScopes[i]);

      nsCOMPtr<nsIURI> scopeURI;
      nsresult rv = NS_NewURI(getter_AddRefs(scopeURI), scope, nullptr, nullptr);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        mPromise->MaybeReject(rv);
        break;
      }

      rv = principal->CheckMayLoad(scopeURI, true /* report */,
                                   false /* allowIfInheritsPrincipal */);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        continue;
      }

      RefPtr<ServiceWorkerRegistrationMainThread> swr =
        mWindow->GetServiceWorkerRegistration(scope);

      array.AppendElement(swr);
    }

    mPromise->MaybeResolve(array);
    return NS_OK;
  }
};

// If we return an error code here, the ServiceWorkerContainer will
// automatically reject the Promise.
NS_IMETHODIMP
ServiceWorkerManager::GetRegistrations(mozIDOMWindow* aWindow,
                                       nsISupports** aPromise)
{
  AssertIsOnMainThread();

  if (NS_WARN_IF(!aWindow)) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  auto* window = nsPIDOMWindowInner::From(aWindow);
  nsCOMPtr<nsIDocument> doc = window->GetExtantDoc();
  if (NS_WARN_IF(!doc)) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  // Don't allow service workers to register when the *document* is chrome for
  // now.
  MOZ_ASSERT(!nsContentUtils::IsSystemPrincipal(doc->NodePrincipal()));

  nsCOMPtr<nsIGlobalObject> sgo = do_QueryInterface(window);
  ErrorResult result;
  RefPtr<Promise> promise = Promise::Create(sgo, result);
  if (result.Failed()) {
    return result.StealNSResult();
  }

  nsCOMPtr<nsIRunnable> runnable =
    new GetRegistrationsRunnable(window, promise);
  promise.forget(aPromise);
  return NS_DispatchToCurrentThread(runnable);
}

/*
 * Implements the async aspects of the getRegistration algorithm.
 */
class GetRegistrationRunnable final : public nsRunnable
{
  nsCOMPtr<nsPIDOMWindowInner> mWindow;
  RefPtr<Promise> mPromise;
  nsString mDocumentURL;

public:
  GetRegistrationRunnable(nsPIDOMWindowInner* aWindow, Promise* aPromise,
                          const nsAString& aDocumentURL)
    : mWindow(aWindow), mPromise(aPromise), mDocumentURL(aDocumentURL)
  {}

  NS_IMETHODIMP
  Run()
  {
    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();

    nsIDocument* doc = mWindow->GetExtantDoc();
    if (!doc) {
      mPromise->MaybeReject(NS_ERROR_UNEXPECTED);
      return NS_OK;
    }

    nsCOMPtr<nsIURI> docURI = doc->GetDocumentURI();
    if (!docURI) {
      mPromise->MaybeReject(NS_ERROR_UNEXPECTED);
      return NS_OK;
    }

    nsCOMPtr<nsIURI> uri;
    nsresult rv = NS_NewURI(getter_AddRefs(uri), mDocumentURL, nullptr, docURI);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      mPromise->MaybeReject(rv);
      return NS_OK;
    }

    nsCOMPtr<nsIPrincipal> principal = doc->NodePrincipal();
    if (!principal) {
      mPromise->MaybeReject(NS_ERROR_UNEXPECTED);
      return NS_OK;
    }

    rv = principal->CheckMayLoad(uri, true /* report */,
                                 false /* allowIfInheritsPrinciple */);
    if (NS_FAILED(rv)) {
      mPromise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
      return NS_OK;
    }

    RefPtr<ServiceWorkerRegistrationInfo> registration =
      swm->GetServiceWorkerRegistrationInfo(principal, uri);

    if (!registration) {
      mPromise->MaybeResolve(JS::UndefinedHandleValue);
      return NS_OK;
    }

    NS_ConvertUTF8toUTF16 scope(registration->mScope);
    RefPtr<ServiceWorkerRegistrationMainThread> swr =
      mWindow->GetServiceWorkerRegistration(scope);
    mPromise->MaybeResolve(swr);

    return NS_OK;
  }
};

// If we return an error code here, the ServiceWorkerContainer will
// automatically reject the Promise.
NS_IMETHODIMP
ServiceWorkerManager::GetRegistration(mozIDOMWindow* aWindow,
                                      const nsAString& aDocumentURL,
                                      nsISupports** aPromise)
{
  AssertIsOnMainThread();

  if (NS_WARN_IF(!aWindow)) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  auto* window = nsPIDOMWindowInner::From(aWindow);
  nsCOMPtr<nsIDocument> doc = window->GetExtantDoc();
  if (NS_WARN_IF(!doc)) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  // Don't allow service workers to register when the *document* is chrome for
  // now.
  MOZ_ASSERT(!nsContentUtils::IsSystemPrincipal(doc->NodePrincipal()));

  nsCOMPtr<nsIGlobalObject> sgo = do_QueryInterface(window);
  ErrorResult result;
  RefPtr<Promise> promise = Promise::Create(sgo, result);
  if (result.Failed()) {
    return result.StealNSResult();
  }

  nsCOMPtr<nsIRunnable> runnable =
    new GetRegistrationRunnable(window, promise, aDocumentURL);
  promise.forget(aPromise);
  return NS_DispatchToCurrentThread(runnable);
}

class GetReadyPromiseRunnable final : public nsRunnable
{
  nsCOMPtr<nsPIDOMWindowInner> mWindow;
  RefPtr<Promise> mPromise;

public:
  GetReadyPromiseRunnable(nsPIDOMWindowInner* aWindow, Promise* aPromise)
    : mWindow(aWindow), mPromise(aPromise)
  {}

  NS_IMETHODIMP
  Run()
  {
    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();

    nsIDocument* doc = mWindow->GetExtantDoc();
    if (!doc) {
      mPromise->MaybeReject(NS_ERROR_UNEXPECTED);
      return NS_OK;
    }

    nsCOMPtr<nsIURI> docURI = doc->GetDocumentURI();
    if (!docURI) {
      mPromise->MaybeReject(NS_ERROR_UNEXPECTED);
      return NS_OK;
    }

    if (!swm->CheckReadyPromise(mWindow, docURI, mPromise)) {
      swm->StorePendingReadyPromise(mWindow, docURI, mPromise);
    }

    return NS_OK;
  }
};

NS_IMETHODIMP
ServiceWorkerManager::SendPushEvent(const nsACString& aOriginAttributes,
                                    const nsACString& aScope,
                                    uint32_t aDataLength,
                                    uint8_t* aDataBytes,
                                    uint8_t optional_argc)
{
  if (optional_argc == 2) {
    nsTArray<uint8_t> data;
    if (!data.InsertElementsAt(0, aDataBytes, aDataLength, fallible)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    return SendPushEvent(aOriginAttributes, aScope, Some(data));
  }
  MOZ_ASSERT(optional_argc == 0);
  return SendPushEvent(aOriginAttributes, aScope, Nothing());
}

nsresult
ServiceWorkerManager::SendPushEvent(const nsACString& aOriginAttributes,
                                    const nsACString& aScope,
                                    Maybe<nsTArray<uint8_t>> aData)
{
#ifdef MOZ_SIMPLEPUSH
  return NS_ERROR_NOT_AVAILABLE;
#else
  PrincipalOriginAttributes attrs;
  if (!attrs.PopulateFromSuffix(aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  ServiceWorkerInfo* serviceWorker = GetActiveWorkerInfoForScope(attrs, aScope);
  if (NS_WARN_IF(!serviceWorker)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<ServiceWorkerRegistrationInfo> registration =
    GetRegistration(serviceWorker->GetPrincipal(), aScope);

  return serviceWorker->WorkerPrivate()->SendPushEvent(aData, registration);
#endif // MOZ_SIMPLEPUSH
}

NS_IMETHODIMP
ServiceWorkerManager::SendPushSubscriptionChangeEvent(const nsACString& aOriginAttributes,
                                                      const nsACString& aScope)
{
#ifdef MOZ_SIMPLEPUSH
  return NS_ERROR_NOT_AVAILABLE;
#else
  PrincipalOriginAttributes attrs;
  if (!attrs.PopulateFromSuffix(aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  ServiceWorkerInfo* info = GetActiveWorkerInfoForScope(attrs, aScope);
  if (!info) {
    return NS_ERROR_FAILURE;
  }
  return info->WorkerPrivate()->SendPushSubscriptionChangeEvent();
#endif
}

NS_IMETHODIMP
ServiceWorkerManager::SendNotificationClickEvent(const nsACString& aOriginSuffix,
                                                 const nsACString& aScope,
                                                 const nsAString& aID,
                                                 const nsAString& aTitle,
                                                 const nsAString& aDir,
                                                 const nsAString& aLang,
                                                 const nsAString& aBody,
                                                 const nsAString& aTag,
                                                 const nsAString& aIcon,
                                                 const nsAString& aData,
                                                 const nsAString& aBehavior)
{
  PrincipalOriginAttributes attrs;
  if (!attrs.PopulateFromSuffix(aOriginSuffix)) {
    return NS_ERROR_INVALID_ARG;
  }

  ServiceWorkerInfo* info = GetActiveWorkerInfoForScope(attrs, aScope);
  if (!info) {
    return NS_ERROR_FAILURE;
  }

  ServiceWorkerPrivate* workerPrivate = info->WorkerPrivate();
  return workerPrivate->SendNotificationClickEvent(aID, aTitle, aDir,
                                                   aLang, aBody, aTag,
                                                   aIcon, aData, aBehavior,
                                                   NS_ConvertUTF8toUTF16(aScope));
}

NS_IMETHODIMP
ServiceWorkerManager::GetReadyPromise(mozIDOMWindow* aWindow,
                                      nsISupports** aPromise)
{
  AssertIsOnMainThread();

  if (NS_WARN_IF(!aWindow)) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  auto* window = nsPIDOMWindowInner::From(aWindow);

  nsCOMPtr<nsIDocument> doc = window->GetExtantDoc();
  if (NS_WARN_IF(!doc)) {
    return NS_ERROR_FAILURE;
  }

  // Don't allow service workers to register when the *document* is chrome for
  // now.
  MOZ_ASSERT(!nsContentUtils::IsSystemPrincipal(doc->NodePrincipal()));

  MOZ_ASSERT(!mPendingReadyPromises.Contains(window));

  nsCOMPtr<nsIGlobalObject> sgo = do_QueryInterface(window);
  ErrorResult result;
  RefPtr<Promise> promise = Promise::Create(sgo, result);
  if (result.Failed()) {
    return result.StealNSResult();
  }

  nsCOMPtr<nsIRunnable> runnable =
    new GetReadyPromiseRunnable(window, promise);
  promise.forget(aPromise);
  return NS_DispatchToCurrentThread(runnable);
}

NS_IMETHODIMP
ServiceWorkerManager::RemoveReadyPromise(mozIDOMWindow* aWindow)
{
  AssertIsOnMainThread();
  MOZ_ASSERT(aWindow);

  if (!aWindow) {
    return NS_ERROR_FAILURE;
  }

  mPendingReadyPromises.Remove(aWindow);
  return NS_OK;
}

void
ServiceWorkerManager::StorePendingReadyPromise(nsPIDOMWindowInner* aWindow,
                                               nsIURI* aURI,
                                               Promise* aPromise)
{
  PendingReadyPromise* data;

  // We should not have 2 pending promises for the same window.
  MOZ_ASSERT(!mPendingReadyPromises.Get(aWindow, &data));

  data = new PendingReadyPromise(aURI, aPromise);
  mPendingReadyPromises.Put(aWindow, data);
}

void
ServiceWorkerManager::CheckPendingReadyPromises()
{
  for (auto iter = mPendingReadyPromises.Iter(); !iter.Done(); iter.Next()) {
    nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(iter.Key());
    MOZ_ASSERT(window);

    nsAutoPtr<PendingReadyPromise>& pendingReadyPromise = iter.Data();
    if (CheckReadyPromise(window, pendingReadyPromise->mURI,
                          pendingReadyPromise->mPromise)) {
      iter.Remove();
    }
  }
}

bool
ServiceWorkerManager::CheckReadyPromise(nsPIDOMWindowInner* aWindow,
                                        nsIURI* aURI, Promise* aPromise)
{
  MOZ_ASSERT(aWindow);
  MOZ_ASSERT(aURI);

  nsCOMPtr<nsIDocument> doc = aWindow->GetExtantDoc();
  MOZ_ASSERT(doc);

  nsCOMPtr<nsIPrincipal> principal = doc->NodePrincipal();
  MOZ_ASSERT(principal);

  RefPtr<ServiceWorkerRegistrationInfo> registration =
    GetServiceWorkerRegistrationInfo(principal, aURI);

  if (registration && registration->mActiveWorker) {
    NS_ConvertUTF8toUTF16 scope(registration->mScope);
    RefPtr<ServiceWorkerRegistrationMainThread> swr =
      aWindow->GetServiceWorkerRegistration(scope);
    aPromise->MaybeResolve(swr);
    return true;
  }

  return false;
}

ServiceWorkerInfo*
ServiceWorkerManager::GetActiveWorkerInfoForScope(const PrincipalOriginAttributes& aOriginAttributes,
                                                  const nsACString& aScope)
{
  AssertIsOnMainThread();

  nsCOMPtr<nsIURI> scopeURI;
  nsresult rv = NS_NewURI(getter_AddRefs(scopeURI), aScope, nullptr, nullptr);
  if (NS_FAILED(rv)) {
    return nullptr;
  }
  RefPtr<ServiceWorkerRegistrationInfo> registration =
    GetServiceWorkerRegistrationInfo(aOriginAttributes, scopeURI);
  if (!registration) {
    return nullptr;
  }

  return registration->mActiveWorker;
}

ServiceWorkerInfo*
ServiceWorkerManager::GetActiveWorkerInfoForDocument(nsIDocument* aDocument)
{
  AssertIsOnMainThread();

  RefPtr<ServiceWorkerRegistrationInfo> registration;
  GetDocumentRegistration(aDocument, getter_AddRefs(registration));

  if (!registration) {
    return nullptr;
  }

  return registration->mActiveWorker;
}

class ServiceWorkerUnregisterJob final : public ServiceWorkerJob
{
  RefPtr<ServiceWorkerRegistrationInfo> mRegistration;
  const nsCString mScope;
  nsCOMPtr<nsIServiceWorkerUnregisterCallback> mCallback;
  nsCOMPtr<nsIPrincipal> mPrincipal;

  ~ServiceWorkerUnregisterJob()
  {}

public:
  ServiceWorkerUnregisterJob(ServiceWorkerJobQueue* aQueue,
                             const nsACString& aScope,
                             nsIServiceWorkerUnregisterCallback* aCallback,
                             nsIPrincipal* aPrincipal)
    : ServiceWorkerJob(aQueue, Type::UnregisterJob)
    , mScope(aScope)
    , mCallback(aCallback)
    , mPrincipal(aPrincipal)
  {
    AssertIsOnMainThread();
  }

  void
  Start() override
  {
    AssertIsOnMainThread();
    nsCOMPtr<nsIRunnable> r =
      NS_NewRunnableMethod(this, &ServiceWorkerUnregisterJob::UnregisterAndDone);
    MOZ_ALWAYS_TRUE(NS_SUCCEEDED(NS_DispatchToMainThread(r)));
  }

private:
  // You probably want UnregisterAndDone().
  nsresult
  Unregister()
  {
    AssertIsOnMainThread();

    PrincipalInfo principalInfo;
    if (NS_WARN_IF(NS_FAILED(PrincipalToPrincipalInfo(mPrincipal,
                                                      &principalInfo)))) {
      return mCallback ? mCallback->UnregisterSucceeded(false) : NS_OK;
    }

    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();

    // Could it be that we are shutting down.
    if (swm->mActor) {
      swm->mActor->SendUnregister(principalInfo, NS_ConvertUTF8toUTF16(mScope));
    }

    nsAutoCString scopeKey;
    nsresult rv = swm->PrincipalToScopeKey(mPrincipal, scopeKey);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return mCallback ? mCallback->UnregisterSucceeded(false) : NS_OK;
    }

    // "Let registration be the result of running [[Get Registration]]
    // algorithm passing scope as the argument."
    ServiceWorkerManager::RegistrationDataPerPrincipal* data;
    if (!swm->mRegistrationInfos.Get(scopeKey, &data)) {
      // "If registration is null, then, resolve promise with false."
      return mCallback ? mCallback->UnregisterSucceeded(false) : NS_OK;
    }

    RefPtr<ServiceWorkerRegistrationInfo> registration;
    if (!data->mInfos.Get(mScope, getter_AddRefs(registration))) {
      // "If registration is null, then, resolve promise with false."
      return mCallback ? mCallback->UnregisterSucceeded(false) : NS_OK;
    }

    MOZ_ASSERT(registration);

    // "Set registration's uninstalling flag."
    registration->mPendingUninstall = true;
    // "Resolve promise with true"
    rv = mCallback ? mCallback->UnregisterSucceeded(true) : NS_OK;
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    // "If no service worker client is using registration..."
    if (!registration->IsControllingDocuments()) {
      // "If registration's uninstalling flag is set.."
      if (!registration->mPendingUninstall) {
        return NS_OK;
      }

      // "Invoke [[Clear Registration]]..."
      registration->Clear();
      swm->RemoveRegistration(registration);
    }

    return NS_OK;
  }

  // The unregister job is done irrespective of success or failure of any sort.
  void
  UnregisterAndDone()
  {
    nsresult rv = Unregister();
    Unused << NS_WARN_IF(NS_FAILED(rv));
    Done(rv);
  }
};

NS_IMETHODIMP
ServiceWorkerManager::Unregister(nsIPrincipal* aPrincipal,
                                 nsIServiceWorkerUnregisterCallback* aCallback,
                                 const nsAString& aScope)
{
  AssertIsOnMainThread();

  if (!aPrincipal) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv;

// This is not accessible by content, and callers should always ensure scope is
// a correct URI, so this is wrapped in DEBUG
#ifdef DEBUG
  nsCOMPtr<nsIURI> scopeURI;
  rv = NS_NewURI(getter_AddRefs(scopeURI), aScope, nullptr, nullptr);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }
#endif

  nsAutoCString originSuffix;
  rv = PrincipalToScopeKey(aPrincipal, originSuffix);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  NS_ConvertUTF16toUTF8 scope(aScope);
  ServiceWorkerJobQueue* queue = GetOrCreateJobQueue(originSuffix, scope);
  MOZ_ASSERT(queue);

  RefPtr<ServiceWorkerUnregisterJob> job =
    new ServiceWorkerUnregisterJob(queue, scope, aCallback, aPrincipal);

  if (mActor) {
    queue->Append(job);
    return NS_OK;
  }

  AppendPendingOperation(queue, job);
  return NS_OK;
}

ServiceWorkerJobQueue*
ServiceWorkerManager::GetOrCreateJobQueue(const nsACString& aKey,
                                          const nsACString& aScope)
{
  ServiceWorkerManager::RegistrationDataPerPrincipal* data;
  if (!mRegistrationInfos.Get(aKey, &data)) {
    data = new RegistrationDataPerPrincipal();
    mRegistrationInfos.Put(aKey, data);
  }

  ServiceWorkerJobQueue* queue;
  if (!data->mJobQueues.Get(aScope, &queue)) {
    queue = new ServiceWorkerJobQueue(aKey);
    data->mJobQueues.Put(aScope, queue);
  }

  return queue;
}

/* static */
already_AddRefed<ServiceWorkerManager>
ServiceWorkerManager::GetInstance()
{
  // Note: We don't simply check gInstance for null-ness here, since otherwise
  // this can resurrect the ServiceWorkerManager pretty late during shutdown.
  static bool firstTime = true;
  if (firstTime) {
    firstTime = false;

    AssertIsOnMainThread();

    gInstance = new ServiceWorkerManager();
    gInstance->Init();
    ClearOnShutdown(&gInstance);
  }
  RefPtr<ServiceWorkerManager> copy = gInstance.get();
  return copy.forget();
}

void
ServiceWorkerManager::FinishFetch(ServiceWorkerRegistrationInfo* aRegistration)
{
}

void
ServiceWorkerManager::ReportToAllClients(const nsCString& aScope,
                                         const nsString& aMessage,
                                         const nsString& aFilename,
                                         const nsString& aLine,
                                         uint32_t aLineNumber,
                                         uint32_t aColumnNumber,
                                         uint32_t aFlags)
{
  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aFilename);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  AutoTArray<uint64_t, 16> windows;

  // Report errors to every controlled document.
  for (auto iter = mControlledDocuments.Iter(); !iter.Done(); iter.Next()) {
    ServiceWorkerRegistrationInfo* reg = iter.UserData();
    MOZ_ASSERT(reg);
    if (!reg->mScope.Equals(aScope)) {
      continue;
    }

    nsCOMPtr<nsIDocument> doc = do_QueryInterface(iter.Key());
    if (!doc || !doc->IsCurrentActiveDocument() || !doc->GetWindow()) {
      continue;
    }

    windows.AppendElement(doc->InnerWindowID());

    nsContentUtils::ReportToConsoleNonLocalized(aMessage,
                                                aFlags,
                                                NS_LITERAL_CSTRING("Service Workers"),
                                                doc,
                                                uri,
                                                aLine,
                                                aLineNumber,
                                                aColumnNumber);
  }

  // Report to any documents that have called .register() for this scope.  They
  // may not be controlled, but will still want to see error reports.
  WeakDocumentList* regList = mRegisteringDocuments.Get(aScope);
  if (regList) {
    for (int32_t i = regList->Length() - 1; i >= 0; --i) {
      nsCOMPtr<nsIDocument> doc = do_QueryReferent(regList->ElementAt(i));
      if (!doc) {
        regList->RemoveElementAt(i);
        continue;
      }

      if (!doc->IsCurrentActiveDocument()) {
        continue;
      }

      uint64_t innerWindowId = doc->InnerWindowID();
      if (windows.Contains(innerWindowId)) {
        continue;
      }

      windows.AppendElement(innerWindowId);

      nsContentUtils::ReportToConsoleNonLocalized(aMessage,
                                                  aFlags,
                                                  NS_LITERAL_CSTRING("Service Workers"),
                                                  doc,
                                                  uri,
                                                  aLine,
                                                  aLineNumber,
                                                  aColumnNumber);
    }

    if (regList->IsEmpty()) {
      regList = nullptr;
      nsAutoPtr<WeakDocumentList> doomed;
      mRegisteringDocuments.RemoveAndForget(aScope, doomed);
    }
  }

  InterceptionList* intList = mNavigationInterceptions.Get(aScope);
  if (intList) {
    nsIConsoleService* consoleService = nullptr;
    for (uint32_t i = 0; i < intList->Length(); ++i) {
      nsCOMPtr<nsIInterceptedChannel> channel = intList->ElementAt(i);

      nsCOMPtr<nsIChannel> inner;
      rv = channel->GetChannel(getter_AddRefs(inner));
      if (NS_WARN_IF(NS_FAILED(rv))) {
        continue;
      }

      uint64_t innerWindowId = nsContentUtils::GetInnerWindowID(inner);
      if (innerWindowId == 0 || windows.Contains(innerWindowId)) {
        continue;
      }

      windows.AppendElement(innerWindowId);

      // Unfortunately the nsContentUtils helpers don't provide a convenient
      // way to log to a window ID without a document.  Use console service
      // directly.
      nsCOMPtr<nsIScriptError> errorObject =
        do_CreateInstance(NS_SCRIPTERROR_CONTRACTID, &rv);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return;
      }

      rv = errorObject->InitWithWindowID(aMessage,
                                         aFilename,
                                         aLine,
                                         aLineNumber,
                                         aColumnNumber,
                                         aFlags,
                                         NS_LITERAL_CSTRING("Service Workers"),
                                         innerWindowId);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return;
      }

      if (!consoleService) {
        rv = CallGetService(NS_CONSOLESERVICE_CONTRACTID, &consoleService);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return;
        }
      }

      consoleService->LogMessage(errorObject);
    }
  }

  // If there are no documents to report to, at least report something to the
  // browser console.
  if (windows.IsEmpty()) {
    nsContentUtils::ReportToConsoleNonLocalized(aMessage,
                                                aFlags,
                                                NS_LITERAL_CSTRING("Service Workers"),
                                                nullptr,  // document
                                                uri,
                                                aLine,
                                                aLineNumber,
                                                aColumnNumber);
    return;
  }
}

void
ServiceWorkerManager::HandleError(JSContext* aCx,
                                  nsIPrincipal* aPrincipal,
                                  const nsCString& aScope,
                                  const nsString& aWorkerURL,
                                  const nsString& aMessage,
                                  const nsString& aFilename,
                                  const nsString& aLine,
                                  uint32_t aLineNumber,
                                  uint32_t aColumnNumber,
                                  uint32_t aFlags,
                                  JSExnType aExnType)
{
  AssertIsOnMainThread();
  MOZ_ASSERT(aPrincipal);

  nsAutoCString scopeKey;
  nsresult rv = PrincipalToScopeKey(aPrincipal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  ServiceWorkerManager::RegistrationDataPerPrincipal* data;
  if (NS_WARN_IF(!mRegistrationInfos.Get(scopeKey, &data))) {
    return;
  }

  // Always report any uncaught exceptions or errors to the console of
  // each client.
  ReportToAllClients(aScope, aMessage, aFilename, aLine, aLineNumber,
                     aColumnNumber, aFlags);
}

void
ServiceWorkerRegistrationInfo::FinishActivate(bool aSuccess)
{
  if (mPendingUninstall || !mActiveWorker ||
      mActiveWorker->State() != ServiceWorkerState::Activating) {
    return;
  }

  // Activation never fails, so aSuccess is ignored.
  mActiveWorker->UpdateState(ServiceWorkerState::Activated);
  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  swm->StoreRegistration(mPrincipal, this);
}

void
ServiceWorkerRegistrationInfo::RefreshLastUpdateCheckTime()
{
  AssertIsOnMainThread();
  mLastUpdateCheckTime = PR_IntervalNow() / PR_MSEC_PER_SEC;
}

bool
ServiceWorkerRegistrationInfo::IsLastUpdateCheckTimeOverOneDay() const
{
  AssertIsOnMainThread();

  // For testing.
  if (Preferences::GetBool("dom.serviceWorkers.testUpdateOverOneDay")) {
    return true;
  }

  const uint64_t kSecondsPerDay = 86400;
  const uint64_t now = PR_IntervalNow() / PR_MSEC_PER_SEC;

  if ((mLastUpdateCheckTime != 0) &&
      (now - mLastUpdateCheckTime > kSecondsPerDay)) {
    return true;
  }
  return false;
}

void
ServiceWorkerRegistrationInfo::NotifyListenersOnChange()
{
  nsTArray<nsCOMPtr<nsIServiceWorkerRegistrationInfoListener>> listeners(mListeners);
  for (size_t index = 0; index < listeners.Length(); ++index) {
    listeners[index]->OnChange();
  }
}

void
ServiceWorkerRegistrationInfo::MaybeScheduleTimeCheckAndUpdate()
{
  AssertIsOnMainThread();

  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  if (!swm) {
    // shutting down, do nothing
    return;
  }

  if (mUpdateState == NoUpdate) {
    mUpdateState = NeedTimeCheckAndUpdate;
  }

  swm->ScheduleUpdateTimer(mPrincipal, mScope);
}

void
ServiceWorkerRegistrationInfo::MaybeScheduleUpdate()
{
  AssertIsOnMainThread();

  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  if (!swm) {
    // shutting down, do nothing
    return;
  }

  mUpdateState = NeedUpdate;

  swm->ScheduleUpdateTimer(mPrincipal, mScope);
}

bool
ServiceWorkerRegistrationInfo::CheckAndClearIfUpdateNeeded()
{
  AssertIsOnMainThread();

  bool result = mUpdateState == NeedUpdate ||
               (mUpdateState == NeedTimeCheckAndUpdate &&
                IsLastUpdateCheckTimeOverOneDay());

  mUpdateState = NoUpdate;

  return result;
}

void
ServiceWorkerManager::LoadRegistration(
                             const ServiceWorkerRegistrationData& aRegistration)
{
  AssertIsOnMainThread();

  nsCOMPtr<nsIPrincipal> principal =
    PrincipalInfoToPrincipal(aRegistration.principal());
  if (!principal) {
    return;
  }

  RefPtr<ServiceWorkerRegistrationInfo> registration =
    GetRegistration(principal, aRegistration.scope());
  if (!registration) {
    registration = CreateNewRegistration(aRegistration.scope(), principal);
  } else {
    RefPtr<ServiceWorkerInfo> newest = registration->Newest();
    if (newest && newest->ScriptSpec() == aRegistration.scriptSpec() &&
        !!registration->mActiveWorker == aRegistration.currentWorkerURL().IsEmpty()) {
      // No needs for updates.
      return;
    }
  }

  const nsCString& currentWorkerURL = aRegistration.currentWorkerURL();
  if (!currentWorkerURL.IsEmpty()) {
    registration->mActiveWorker =
      new ServiceWorkerInfo(registration, currentWorkerURL,
                            aRegistration.activeCacheName());
    registration->mActiveWorker->SetActivateStateUncheckedWithoutEvent(ServiceWorkerState::Activated);
  }
}

void
ServiceWorkerManager::LoadRegistrations(
                  const nsTArray<ServiceWorkerRegistrationData>& aRegistrations)
{
  AssertIsOnMainThread();

  for (uint32_t i = 0, len = aRegistrations.Length(); i < len; ++i) {
    LoadRegistration(aRegistrations[i]);
  }
}

void
ServiceWorkerManager::ActorFailed()
{
  MOZ_CRASH("Failed to create a PBackgroundChild actor!");
}

void
ServiceWorkerManager::ActorCreated(mozilla::ipc::PBackgroundChild* aActor)
{
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(!mActor);

  PServiceWorkerManagerChild* actor =
    aActor->SendPServiceWorkerManagerConstructor();

  mActor = static_cast<ServiceWorkerManagerChild*>(actor);

  // Flush the pending requests.
  for (uint32_t i = 0, len = mPendingOperations.Length(); i < len; ++i) {
    MOZ_ASSERT(mPendingOperations[i].mRunnable ||
               (mPendingOperations[i].mJob && mPendingOperations[i].mQueue));

    if (mPendingOperations[i].mRunnable) {
      nsresult rv = NS_DispatchToCurrentThread(mPendingOperations[i].mRunnable);
      if (NS_FAILED(rv)) {
        NS_WARNING("Failed to dispatch a runnable.");
        return;
      }
    } else {
      mPendingOperations[i].mQueue->Append(mPendingOperations[i].mJob);
    }
  }

  mPendingOperations.Clear();
}

void
ServiceWorkerManager::StoreRegistration(
                                   nsIPrincipal* aPrincipal,
                                   ServiceWorkerRegistrationInfo* aRegistration)
{
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aRegistration);

  if (mShuttingDown) {
    return;
  }

  MOZ_ASSERT(mActor);

  ServiceWorkerRegistrationData data;
  nsresult rv = PopulateRegistrationData(aPrincipal, aRegistration, data);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  PrincipalInfo principalInfo;
  if (NS_WARN_IF(NS_FAILED(PrincipalToPrincipalInfo(aPrincipal,
                                                    &principalInfo)))) {
    return;
  }

  mActor->SendRegister(data);
}

already_AddRefed<ServiceWorkerRegistrationInfo>
ServiceWorkerManager::GetServiceWorkerRegistrationInfo(nsPIDOMWindowInner* aWindow)
{
  MOZ_ASSERT(aWindow);
  nsCOMPtr<nsIDocument> document = aWindow->GetExtantDoc();
  return GetServiceWorkerRegistrationInfo(document);
}

already_AddRefed<ServiceWorkerRegistrationInfo>
ServiceWorkerManager::GetServiceWorkerRegistrationInfo(nsIDocument* aDoc)
{
  MOZ_ASSERT(aDoc);
  nsCOMPtr<nsIURI> documentURI = aDoc->GetDocumentURI();
  nsCOMPtr<nsIPrincipal> principal = aDoc->NodePrincipal();
  return GetServiceWorkerRegistrationInfo(principal, documentURI);
}

already_AddRefed<ServiceWorkerRegistrationInfo>
ServiceWorkerManager::GetServiceWorkerRegistrationInfo(nsIPrincipal* aPrincipal,
                                                       nsIURI* aURI)
{
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aURI);

  //XXXnsm Temporary fix until Bug 1171432 is fixed.
  if (NS_WARN_IF(BasePrincipal::Cast(aPrincipal)->AppId() == nsIScriptSecurityManager::UNKNOWN_APP_ID)) {
    return nullptr;
  }

  nsAutoCString originAttributesSuffix;
  nsresult rv = PrincipalToScopeKey(aPrincipal, originAttributesSuffix);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  return GetServiceWorkerRegistrationInfo(originAttributesSuffix, aURI);
}

already_AddRefed<ServiceWorkerRegistrationInfo>
ServiceWorkerManager::GetServiceWorkerRegistrationInfo(const PrincipalOriginAttributes& aOriginAttributes,
                                                       nsIURI* aURI)
{
  MOZ_ASSERT(aURI);

  nsAutoCString originAttributesSuffix;
  aOriginAttributes.CreateSuffix(originAttributesSuffix);
  return GetServiceWorkerRegistrationInfo(originAttributesSuffix, aURI);
}

already_AddRefed<ServiceWorkerRegistrationInfo>
ServiceWorkerManager::GetServiceWorkerRegistrationInfo(const nsACString& aScopeKey,
                                                       nsIURI* aURI)
{
  MOZ_ASSERT(aURI);

  nsAutoCString spec;
  nsresult rv = aURI->GetSpec(spec);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  nsAutoCString scope;
  RegistrationDataPerPrincipal* data;
  if (!FindScopeForPath(aScopeKey, spec, &data, scope)) {
    return nullptr;
  }

  MOZ_ASSERT(data);

  RefPtr<ServiceWorkerRegistrationInfo> registration;
  data->mInfos.Get(scope, getter_AddRefs(registration));
  // ordered scopes and registrations better be in sync.
  MOZ_ASSERT(registration);

#ifdef DEBUG
  nsAutoCString originSuffix;
  rv = registration->mPrincipal->GetOriginSuffix(originSuffix);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  MOZ_ASSERT(originSuffix.Equals(aScopeKey));
#endif

  if (registration->mPendingUninstall) {
    return nullptr;
  }
  return registration.forget();
}

/* static */ nsresult
ServiceWorkerManager::PrincipalToScopeKey(nsIPrincipal* aPrincipal,
                                          nsACString& aKey)
{
  MOZ_ASSERT(aPrincipal);

  if (!BasePrincipal::Cast(aPrincipal)->IsCodebasePrincipal()) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv = aPrincipal->GetOriginSuffix(aKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

/* static */ void
ServiceWorkerManager::AddScopeAndRegistration(const nsACString& aScope,
                                              ServiceWorkerRegistrationInfo* aInfo)
{
  MOZ_ASSERT(aInfo);
  MOZ_ASSERT(aInfo->mPrincipal);

  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  MOZ_ASSERT(swm);

  nsAutoCString scopeKey;
  nsresult rv = swm->PrincipalToScopeKey(aInfo->mPrincipal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  RegistrationDataPerPrincipal* data;
  if (!swm->mRegistrationInfos.Get(scopeKey, &data)) {
    data = new RegistrationDataPerPrincipal();
    swm->mRegistrationInfos.Put(scopeKey, data);
  }

  for (uint32_t i = 0; i < data->mOrderedScopes.Length(); ++i) {
    const nsCString& current = data->mOrderedScopes[i];

    // Perfect match!
    if (aScope.Equals(current)) {
      data->mInfos.Put(aScope, aInfo);
      swm->NotifyListenersOnRegister(aInfo);
      return;
    }

    // Sort by length, with longest match first.
    // /foo/bar should be before /foo/
    // Similarly /foo/b is between the two.
    if (StringBeginsWith(aScope, current)) {
      data->mOrderedScopes.InsertElementAt(i, aScope);
      data->mInfos.Put(aScope, aInfo);
      swm->NotifyListenersOnRegister(aInfo);
      return;
    }
  }

  data->mOrderedScopes.AppendElement(aScope);
  data->mInfos.Put(aScope, aInfo);
  swm->NotifyListenersOnRegister(aInfo);
}

/* static */ bool
ServiceWorkerManager::FindScopeForPath(const nsACString& aScopeKey,
                                       const nsACString& aPath,
                                       RegistrationDataPerPrincipal** aData,
                                       nsACString& aMatch)
{
  MOZ_ASSERT(aData);

  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  MOZ_ASSERT(swm);

  if (!swm->mRegistrationInfos.Get(aScopeKey, aData)) {
    return false;
  }

  for (uint32_t i = 0; i < (*aData)->mOrderedScopes.Length(); ++i) {
    const nsCString& current = (*aData)->mOrderedScopes[i];
    if (StringBeginsWith(aPath, current)) {
      aMatch = current;
      return true;
    }
  }

  return false;
}

/* static */ bool
ServiceWorkerManager::HasScope(nsIPrincipal* aPrincipal,
                               const nsACString& aScope)
{
  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  MOZ_ASSERT(swm);

  nsAutoCString scopeKey;
  nsresult rv = PrincipalToScopeKey(aPrincipal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  RegistrationDataPerPrincipal* data;
  if (!swm->mRegistrationInfos.Get(scopeKey, &data)) {
    return false;
  }

  return data->mOrderedScopes.Contains(aScope);
}

/* static */ void
ServiceWorkerManager::RemoveScopeAndRegistration(ServiceWorkerRegistrationInfo* aRegistration)
{
  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  MOZ_ASSERT(swm);

  nsAutoCString scopeKey;
  nsresult rv = swm->PrincipalToScopeKey(aRegistration->mPrincipal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  RegistrationDataPerPrincipal* data;
  if (!swm->mRegistrationInfos.Get(scopeKey, &data)) {
    return;
  }

  nsCOMPtr<nsITimer> timer = data->mUpdateTimers.Get(aRegistration->mScope);
  if (timer) {
    timer->Cancel();
    data->mUpdateTimers.Remove(aRegistration->mScope);
  }

  // The registration should generally only be removed if there are no controlled
  // documents, but mControlledDocuments can contain references to potentially
  // controlled docs.  This happens when the service worker is not active yet.
  // We must purge these references since we are evicting the registration.
  for (auto iter = swm->mControlledDocuments.Iter(); !iter.Done(); iter.Next()) {
    ServiceWorkerRegistrationInfo* reg = iter.UserData();
    MOZ_ASSERT(reg);
    if (reg->mScope.Equals(aRegistration->mScope)) {
      iter.Remove();
    }
  }

  RefPtr<ServiceWorkerRegistrationInfo> info;
  data->mInfos.Get(aRegistration->mScope, getter_AddRefs(info));

  data->mInfos.Remove(aRegistration->mScope);
  data->mOrderedScopes.RemoveElement(aRegistration->mScope);
  swm->NotifyListenersOnUnregister(info);

  swm->MaybeRemoveRegistrationInfo(scopeKey);
  swm->NotifyServiceWorkerRegistrationRemoved(aRegistration);
}

void
ServiceWorkerManager::MaybeRemoveRegistrationInfo(const nsACString& aScopeKey)
{
  RegistrationDataPerPrincipal* data;
  if (!mRegistrationInfos.Get(aScopeKey, &data)) {
    return;
  }

  if (data->mOrderedScopes.IsEmpty() && data->mJobQueues.Count() == 0) {
    mRegistrationInfos.Remove(aScopeKey);
  }
}

void
ServiceWorkerManager::MaybeStartControlling(nsIDocument* aDoc,
                                            const nsAString& aDocumentId)
{
  AssertIsOnMainThread();

  // We keep a set of documents that service workers may choose to start
  // controlling using claim().
  MOZ_ASSERT(!mAllDocuments.Contains(aDoc));
  mAllDocuments.PutEntry(aDoc);

  RefPtr<ServiceWorkerRegistrationInfo> registration =
    GetServiceWorkerRegistrationInfo(aDoc);
  if (registration) {
    MOZ_ASSERT(!mControlledDocuments.Contains(aDoc));
    StartControllingADocument(registration, aDoc, aDocumentId);
  }
}

void
ServiceWorkerManager::MaybeStopControlling(nsIDocument* aDoc)
{
  MOZ_ASSERT(aDoc);
  RefPtr<ServiceWorkerRegistrationInfo> registration;
  mControlledDocuments.Remove(aDoc, getter_AddRefs(registration));
  // A document which was uncontrolled does not maintain that state itself, so
  // it will always call MaybeStopControlling() even if there isn't an
  // associated registration. So this check is required.
  if (registration) {
    StopControllingADocument(registration);
  }

  mAllDocuments.RemoveEntry(aDoc);
}

void
ServiceWorkerManager::MaybeCheckNavigationUpdate(nsIDocument* aDoc)
{
  AssertIsOnMainThread();
  MOZ_ASSERT(aDoc);
  // We perform these success path navigation update steps when the
  // document tells us its more or less done loading.  This avoids
  // slowing down page load and also lets pages consistently get
  // updatefound events when they fire.
  //
  // 9.8.20 If respondWithEntered is false, then:
  // 9.8.22 Else: (respondWith was entered and succeeded)
  //    If request is a non-subresource request, then: Invoke Soft Update
  //    algorithm.
  RefPtr<ServiceWorkerRegistrationInfo> registration;
  mControlledDocuments.Get(aDoc, getter_AddRefs(registration));
  if (registration) {
    registration->MaybeScheduleUpdate();
  }
}

void
ServiceWorkerManager::StartControllingADocument(ServiceWorkerRegistrationInfo* aRegistration,
                                                nsIDocument* aDoc,
                                                const nsAString& aDocumentId)
{
  MOZ_ASSERT(aRegistration);
  MOZ_ASSERT(aDoc);

  aRegistration->StartControllingADocument();
  mControlledDocuments.Put(aDoc, aRegistration);
  if (!aDocumentId.IsEmpty()) {
    aDoc->SetId(aDocumentId);
  }
  Telemetry::Accumulate(Telemetry::SERVICE_WORKER_CONTROLLED_DOCUMENTS, 1);
}

void
ServiceWorkerManager::StopControllingADocument(ServiceWorkerRegistrationInfo* aRegistration)
{
  aRegistration->StopControllingADocument();
  if (!aRegistration->IsControllingDocuments()) {
    if (aRegistration->mPendingUninstall) {
      aRegistration->Clear();
      RemoveRegistration(aRegistration);
    } else {
      // If the registration has an active worker that is running
      // this might be a good time to stop it.
      if (aRegistration->mActiveWorker) {
        ServiceWorkerPrivate* serviceWorkerPrivate =
          aRegistration->mActiveWorker->WorkerPrivate();
        serviceWorkerPrivate->NoteStoppedControllingDocuments();
      }
      aRegistration->TryToActivateAsync();
    }
  }
}

NS_IMETHODIMP
ServiceWorkerManager::GetScopeForUrl(nsIPrincipal* aPrincipal,
                                     const nsAString& aUrl, nsAString& aScope)
{
  MOZ_ASSERT(aPrincipal);

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aUrl, nullptr, nullptr);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<ServiceWorkerRegistrationInfo> r =
    GetServiceWorkerRegistrationInfo(aPrincipal, uri);
  if (!r) {
      return NS_ERROR_FAILURE;
  }

  aScope = NS_ConvertUTF8toUTF16(r->mScope);
  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerManager::AddRegistrationEventListener(const nsAString& aScope,
                                                   ServiceWorkerRegistrationListener* aListener)
{
  AssertIsOnMainThread();
  MOZ_ASSERT(aListener);
#ifdef DEBUG
  // Ensure a registration is only listening for it's own scope.
  nsAutoString regScope;
  aListener->GetScope(regScope);
  MOZ_ASSERT(!regScope.IsEmpty());
  MOZ_ASSERT(aScope.Equals(regScope));
#endif

  MOZ_ASSERT(!mServiceWorkerRegistrationListeners.Contains(aListener));
  mServiceWorkerRegistrationListeners.AppendElement(aListener);
  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerManager::RemoveRegistrationEventListener(const nsAString& aScope,
                                                      ServiceWorkerRegistrationListener* aListener)
{
  AssertIsOnMainThread();
  MOZ_ASSERT(aListener);
#ifdef DEBUG
  // Ensure a registration is unregistering for it's own scope.
  nsAutoString regScope;
  aListener->GetScope(regScope);
  MOZ_ASSERT(!regScope.IsEmpty());
  MOZ_ASSERT(aScope.Equals(regScope));
#endif

  MOZ_ASSERT(mServiceWorkerRegistrationListeners.Contains(aListener));
  mServiceWorkerRegistrationListeners.RemoveElement(aListener);
  return NS_OK;
}

void
ServiceWorkerManager::FireUpdateFoundOnServiceWorkerRegistrations(
  ServiceWorkerRegistrationInfo* aRegistration)
{
  AssertIsOnMainThread();

  nsTObserverArray<ServiceWorkerRegistrationListener*>::ForwardIterator it(mServiceWorkerRegistrationListeners);
  while (it.HasMore()) {
    RefPtr<ServiceWorkerRegistrationListener> target = it.GetNext();
    nsAutoString regScope;
    target->GetScope(regScope);
    MOZ_ASSERT(!regScope.IsEmpty());

    NS_ConvertUTF16toUTF8 utf8Scope(regScope);
    if (utf8Scope.Equals(aRegistration->mScope)) {
      target->UpdateFound();
    }
  }
}

/*
 * This is used for installing, waiting and active.
 */
nsresult
ServiceWorkerManager::GetServiceWorkerForScope(nsPIDOMWindowInner* aWindow,
                                               const nsAString& aScope,
                                               WhichServiceWorker aWhichWorker,
                                               nsISupports** aServiceWorker)
{
  AssertIsOnMainThread();

  if (NS_WARN_IF(!aWindow)) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  nsCOMPtr<nsIDocument> doc = aWindow->GetExtantDoc();
  MOZ_ASSERT(doc);

  ///////////////////////////////////////////
  // Security check
  nsAutoCString scope = NS_ConvertUTF16toUTF8(aScope);
  nsCOMPtr<nsIURI> scopeURI;
  // We pass nullptr as the base URI since scopes obtained from
  // ServiceWorkerRegistrations MUST be fully qualified URIs.
  nsresult rv = NS_NewURI(getter_AddRefs(scopeURI), scope, nullptr, nullptr);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  nsCOMPtr<nsIPrincipal> documentPrincipal = doc->NodePrincipal();
  rv = documentPrincipal->CheckMayLoad(scopeURI, true /* report */,
                                       false /* allowIfInheritsPrinciple */);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }
  ////////////////////////////////////////////

  RefPtr<ServiceWorkerRegistrationInfo> registration =
    GetRegistration(documentPrincipal, scope);
  if (NS_WARN_IF(!registration)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<ServiceWorkerInfo> info;
  if (aWhichWorker == WhichServiceWorker::INSTALLING_WORKER) {
    info = registration->mInstallingWorker;
  } else if (aWhichWorker == WhichServiceWorker::WAITING_WORKER) {
    info = registration->mWaitingWorker;
  } else if (aWhichWorker == WhichServiceWorker::ACTIVE_WORKER) {
    info = registration->mActiveWorker;
  } else {
    MOZ_CRASH("Invalid worker type");
  }

  if (NS_WARN_IF(!info)) {
    return NS_ERROR_DOM_NOT_FOUND_ERR;
  }

  RefPtr<ServiceWorker> serviceWorker = info->GetOrCreateInstance(aWindow);

  serviceWorker->SetState(info->State());
  serviceWorker.forget(aServiceWorker);
  return NS_OK;
}

namespace {

class ContinueDispatchFetchEventRunnable : public nsRunnable
{
  RefPtr<ServiceWorkerPrivate> mServiceWorkerPrivate;
  nsCOMPtr<nsIInterceptedChannel> mChannel;
  nsCOMPtr<nsILoadGroup> mLoadGroup;
  nsString mDocumentId;
  bool mIsReload;
public:
  ContinueDispatchFetchEventRunnable(ServiceWorkerPrivate* aServiceWorkerPrivate,
                                     nsIInterceptedChannel* aChannel,
                                     nsILoadGroup* aLoadGroup,
                                     const nsAString& aDocumentId,
                                     bool aIsReload)
    : mServiceWorkerPrivate(aServiceWorkerPrivate)
    , mChannel(aChannel)
    , mLoadGroup(aLoadGroup)
    , mDocumentId(aDocumentId)
    , mIsReload(aIsReload)
  {
    MOZ_ASSERT(aServiceWorkerPrivate);
    MOZ_ASSERT(aChannel);
  }

  void
  HandleError()
  {
    AssertIsOnMainThread();
    NS_WARNING("Unexpected error while dispatching fetch event!");
    DebugOnly<nsresult> rv = mChannel->ResetInterception();
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "Failed to resume intercepted network request");
  }

  NS_IMETHOD
  Run() override
  {
    AssertIsOnMainThread();

    nsCOMPtr<nsIChannel> channel;
    nsresult rv = mChannel->GetChannel(getter_AddRefs(channel));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      HandleError();
      return NS_OK;
    }

    // The channel might have encountered an unexpected error while ensuring
    // the upload stream is cloneable.  Check here and reset the interception
    // if that happens.
    nsresult status;
    rv = channel->GetStatus(&status);
    if (NS_WARN_IF(NS_FAILED(rv) || NS_FAILED(status))) {
      HandleError();
      return NS_OK;
    }

    rv = mServiceWorkerPrivate->SendFetchEvent(mChannel, mLoadGroup,
                                               mDocumentId, mIsReload);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      HandleError();
    }

    return NS_OK;
  }
};

} // anonymous namespace

void
ServiceWorkerManager::DispatchFetchEvent(const PrincipalOriginAttributes& aOriginAttributes,
                                         nsIDocument* aDoc,
                                         const nsAString& aDocumentIdForTopLevelNavigation,
                                         nsIInterceptedChannel* aChannel,
                                         bool aIsReload,
                                         bool aIsSubresourceLoad,
                                         ErrorResult& aRv)
{
  MOZ_ASSERT(aChannel);
  AssertIsOnMainThread();

  RefPtr<ServiceWorkerInfo> serviceWorker;
  nsCOMPtr<nsILoadGroup> loadGroup;
  nsAutoString documentId;

  if (aIsSubresourceLoad) {
    MOZ_ASSERT(aDoc);
    serviceWorker = GetActiveWorkerInfoForDocument(aDoc);
    loadGroup = aDoc->GetDocumentLoadGroup();
    nsresult rv = aDoc->GetOrCreateId(documentId);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return;
    }
  } else {
    nsCOMPtr<nsIChannel> internalChannel;
    aRv = aChannel->GetChannel(getter_AddRefs(internalChannel));
    if (NS_WARN_IF(aRv.Failed())) {
      return;
    }

    internalChannel->GetLoadGroup(getter_AddRefs(loadGroup));

    // TODO: Use aDocumentIdForTopLevelNavigation for potentialClientId, pending
    // the spec change.

    nsCOMPtr<nsIURI> uri;
    aRv = aChannel->GetSecureUpgradedChannelURI(getter_AddRefs(uri));
    if (NS_WARN_IF(aRv.Failed())) {
      return;
    }

    RefPtr<ServiceWorkerRegistrationInfo> registration =
      GetServiceWorkerRegistrationInfo(aOriginAttributes, uri);
    if (!registration) {
      NS_WARNING("No registration found when dispatching the fetch event");
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }

    // This should only happen if IsAvailable() returned true.
    MOZ_ASSERT(registration->mActiveWorker);
    serviceWorker = registration->mActiveWorker;

    AddNavigationInterception(serviceWorker->Scope(), aChannel);
  }

  if (NS_WARN_IF(aRv.Failed()) || !serviceWorker) {
    return;
  }

  nsCOMPtr<nsIRunnable> continueRunnable =
    new ContinueDispatchFetchEventRunnable(serviceWorker->WorkerPrivate(),
                                           aChannel, loadGroup,
                                           documentId, aIsReload);

  nsCOMPtr<nsIChannel> innerChannel;
  aRv = aChannel->GetChannel(getter_AddRefs(innerChannel));
  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }

  nsCOMPtr<nsIUploadChannel2> uploadChannel = do_QueryInterface(innerChannel);

  // If there is no upload stream, then continue immediately
  if (!uploadChannel) {
    MOZ_ALWAYS_TRUE(NS_SUCCEEDED(continueRunnable->Run()));
    return;
  }
  // Otherwise, ensure the upload stream can be cloned directly.  This may
  // require some async copying, so provide a callback.
  aRv = uploadChannel->EnsureUploadStreamIsCloneable(continueRunnable);
}

bool
ServiceWorkerManager::IsAvailable(const PrincipalOriginAttributes& aOriginAttributes,
                                  nsIURI* aURI)
{
  MOZ_ASSERT(aURI);

  RefPtr<ServiceWorkerRegistrationInfo> registration =
    GetServiceWorkerRegistrationInfo(aOriginAttributes, aURI);
  return registration && registration->mActiveWorker;
}

bool
ServiceWorkerManager::IsControlled(nsIDocument* aDoc, ErrorResult& aRv)
{
  MOZ_ASSERT(aDoc);

  RefPtr<ServiceWorkerRegistrationInfo> registration;
  nsresult rv = GetDocumentRegistration(aDoc, getter_AddRefs(registration));
  if (NS_WARN_IF(NS_FAILED(rv) && rv != NS_ERROR_NOT_AVAILABLE)) {
    // It's OK to ignore the case where we don't have a registration.
    aRv.Throw(rv);
    return false;
  }

  MOZ_ASSERT_IF(!!registration, !nsContentUtils::IsInPrivateBrowsing(aDoc));
  return !!registration;
}

nsresult
ServiceWorkerManager::GetDocumentRegistration(nsIDocument* aDoc,
                                              ServiceWorkerRegistrationInfo** aRegistrationInfo)
{
  RefPtr<ServiceWorkerRegistrationInfo> registration;
  if (!mControlledDocuments.Get(aDoc, getter_AddRefs(registration))) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // If the document is controlled, the current worker MUST be non-null.
  if (!registration->mActiveWorker) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  registration.forget(aRegistrationInfo);
  return NS_OK;
}

/*
 * The .controller is for the registration associated with the document when
 * the document was loaded.
 */
NS_IMETHODIMP
ServiceWorkerManager::GetDocumentController(nsPIDOMWindowInner* aWindow,
                                            nsISupports** aServiceWorker)
{
  MOZ_ASSERT(aWindow);
  nsCOMPtr<nsIDocument> doc = aWindow->GetExtantDoc();
  if (!doc) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  RefPtr<ServiceWorkerRegistrationInfo> registration;
  nsresult rv = GetDocumentRegistration(doc, getter_AddRefs(registration));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  MOZ_ASSERT(registration->mActiveWorker);
  RefPtr<ServiceWorker> serviceWorker =
    registration->mActiveWorker->GetOrCreateInstance(aWindow);

  serviceWorker.forget(aServiceWorker);
  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerManager::GetInstalling(nsPIDOMWindowInner* aWindow,
                                    const nsAString& aScope,
                                    nsISupports** aServiceWorker)
{
  return GetServiceWorkerForScope(aWindow, aScope,
                                  WhichServiceWorker::INSTALLING_WORKER,
                                  aServiceWorker);
}

NS_IMETHODIMP
ServiceWorkerManager::GetWaiting(nsPIDOMWindowInner* aWindow,
                                 const nsAString& aScope,
                                 nsISupports** aServiceWorker)
{
  return GetServiceWorkerForScope(aWindow, aScope,
                                  WhichServiceWorker::WAITING_WORKER,
                                  aServiceWorker);
}

NS_IMETHODIMP
ServiceWorkerManager::GetActive(nsPIDOMWindowInner* aWindow,
                                const nsAString& aScope,
                                nsISupports** aServiceWorker)
{
  return GetServiceWorkerForScope(aWindow, aScope,
                                  WhichServiceWorker::ACTIVE_WORKER,
                                  aServiceWorker);
}

void
ServiceWorkerManager::InvalidateServiceWorkerRegistrationWorker(ServiceWorkerRegistrationInfo* aRegistration,
                                                                WhichServiceWorker aWhichOnes)
{
  AssertIsOnMainThread();
  nsTObserverArray<ServiceWorkerRegistrationListener*>::ForwardIterator it(mServiceWorkerRegistrationListeners);
  while (it.HasMore()) {
    RefPtr<ServiceWorkerRegistrationListener> target = it.GetNext();
    nsAutoString regScope;
    target->GetScope(regScope);
    MOZ_ASSERT(!regScope.IsEmpty());

    NS_ConvertUTF16toUTF8 utf8Scope(regScope);

    if (utf8Scope.Equals(aRegistration->mScope)) {
      target->InvalidateWorkers(aWhichOnes);
    }
  }
}

void
ServiceWorkerManager::NotifyServiceWorkerRegistrationRemoved(ServiceWorkerRegistrationInfo* aRegistration)
{
  AssertIsOnMainThread();
  nsTObserverArray<ServiceWorkerRegistrationListener*>::ForwardIterator it(mServiceWorkerRegistrationListeners);
  while (it.HasMore()) {
    RefPtr<ServiceWorkerRegistrationListener> target = it.GetNext();
    nsAutoString regScope;
    target->GetScope(regScope);
    MOZ_ASSERT(!regScope.IsEmpty());

    NS_ConvertUTF16toUTF8 utf8Scope(regScope);

    if (utf8Scope.Equals(aRegistration->mScope)) {
      target->RegistrationRemoved();
    }
  }
}

void
ServiceWorkerManager::SoftUpdate(const PrincipalOriginAttributes& aOriginAttributes,
                                 const nsACString& aScope)
{
  AssertIsOnMainThread();

  nsCOMPtr<nsIURI> scopeURI;
  nsresult rv = NS_NewURI(getter_AddRefs(scopeURI), aScope);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  nsCOMPtr<nsIPrincipal> principal =
    BasePrincipal::CreateCodebasePrincipal(scopeURI, aOriginAttributes);
  if (NS_WARN_IF(!principal)) {
    return;
  }

  nsAutoCString scopeKey;
  aOriginAttributes.CreateSuffix(scopeKey);

  RefPtr<ServiceWorkerRegistrationInfo> registration =
    GetRegistration(scopeKey, aScope);
  if (NS_WARN_IF(!registration)) {
    return;
  }

  // "If registration's uninstalling flag is set, abort these steps."
  if (registration->mPendingUninstall) {
    return;
  }

  // "If registration's installing worker is not null, abort these steps."
  if (registration->mInstallingWorker) {
    return;
  }

  // "Let newestWorker be the result of running Get Newest Worker algorithm
  // passing registration as its argument.
  // If newestWorker is null, abort these steps."
  RefPtr<ServiceWorkerInfo> newest = registration->Newest();
  if (!newest) {
    return;
  }

  // "If the registration queue for registration is empty, invoke Update algorithm,
  // or its equivalent, with client, registration as its argument."
  // TODO(catalinb): We don't implement the force bypass cache flag.
  // See: https://github.com/slightlyoff/ServiceWorker/issues/759
  if (!registration->mUpdating) {
    ServiceWorkerJobQueue* queue = GetOrCreateJobQueue(scopeKey, aScope);
    MOZ_ASSERT(queue);

    RefPtr<ServiceWorkerRegisterJob> job =
      new ServiceWorkerRegisterJob(queue, principal, registration->mScope,
                                   newest->ScriptSpec(), nullptr);
    queue->Append(job);
  }
}

void
ServiceWorkerManager::Update(nsIPrincipal* aPrincipal,
                             const nsACString& aScope,
                             ServiceWorkerUpdateFinishCallback* aCallback)
{
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aCallback);

  nsAutoCString scopeKey;
  nsresult rv = PrincipalToScopeKey(aPrincipal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  RefPtr<ServiceWorkerRegistrationInfo> registration =
    GetRegistration(scopeKey, aScope);
  if (NS_WARN_IF(!registration)) {
    return;
  }

  // "If registration's uninstalling flag is set, abort these steps."
  if (registration->mPendingUninstall) {
    return;
  }

  // "Let newestWorker be the result of running Get Newest Worker algorithm
  // passing registration as its argument.
  // If newestWorker is null, return a promise rejected with "InvalidStateError"
  RefPtr<ServiceWorkerInfo> newest = registration->Newest();
  if (!newest) {
    ErrorResult error(NS_ERROR_DOM_INVALID_STATE_ERR);
    aCallback->UpdateFailed(error);

    // In case the callback does not consume the exception
    error.SuppressException();

    return;
  }

  ServiceWorkerJobQueue* queue =
    GetOrCreateJobQueue(scopeKey, aScope);
  MOZ_ASSERT(queue);

  // "Invoke Update algorithm, or its equivalent, with client, registration as
  // its argument."
  RefPtr<ServiceWorkerRegisterJob> job =
    new ServiceWorkerRegisterJob(queue, aPrincipal, registration->mScope,
                                 newest->ScriptSpec(), aCallback);
  queue->Append(job);
}

namespace {

static void
FireControllerChangeOnDocument(nsIDocument* aDocument)
{
  AssertIsOnMainThread();
  MOZ_ASSERT(aDocument);

  nsCOMPtr<nsPIDOMWindowInner> w = aDocument->GetInnerWindow();
  if (!w) {
    NS_WARNING("Failed to dispatch controllerchange event");
    return;
  }

  auto* window = nsGlobalWindow::Cast(w.get());
  ErrorResult result;
  dom::Navigator* navigator = window->GetNavigator(result);
  if (NS_WARN_IF(result.Failed())) {
    result.SuppressException();
    return;
  }

  RefPtr<ServiceWorkerContainer> container = navigator->ServiceWorker();
  container->ControllerChanged(result);
  if (result.Failed()) {
    NS_WARNING("Failed to dispatch controllerchange event");
  }
}

} // anonymous namespace

UniquePtr<ServiceWorkerClientInfo>
ServiceWorkerManager::GetClient(nsIPrincipal* aPrincipal,
                                const nsAString& aClientId,
                                ErrorResult& aRv)
{
  UniquePtr<ServiceWorkerClientInfo> clientInfo;
  nsCOMPtr<nsISupportsInterfacePointer> ifptr =
    do_CreateInstance(NS_SUPPORTS_INTERFACE_POINTER_CONTRACTID);
  if (NS_WARN_IF(!ifptr)) {
    return clientInfo;
  }
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (NS_WARN_IF(!obs)) {
    return clientInfo;
  }

  nsresult rv = obs->NotifyObservers(ifptr, "service-worker-get-client",
                                     PromiseFlatString(aClientId).get());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return clientInfo;
  }

  nsCOMPtr<nsISupports> ptr;
  ifptr->GetData(getter_AddRefs(ptr));
  nsCOMPtr<nsIDocument> doc = do_QueryInterface(ptr);
  if (NS_WARN_IF(!doc)) {
    return clientInfo;
  }

  bool equals = false;
  aPrincipal->Equals(doc->NodePrincipal(), &equals);
  if (!equals) {
    return clientInfo;
  }

  if (!IsFromAuthenticatedOrigin(doc)) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return clientInfo;
  }

  clientInfo.reset(new ServiceWorkerClientInfo(doc));
  return clientInfo;
}

void
ServiceWorkerManager::GetAllClients(nsIPrincipal* aPrincipal,
                                    const nsCString& aScope,
                                    bool aIncludeUncontrolled,
                                    nsTArray<ServiceWorkerClientInfo>& aDocuments)
{
  MOZ_ASSERT(aPrincipal);

  RefPtr<ServiceWorkerRegistrationInfo> registration =
    GetRegistration(aPrincipal, aScope);

  if (!registration) {
    // The registration was removed, leave the array empty.
    return;
  }

  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (NS_WARN_IF(!obs)) {
    return;
  }

  nsCOMPtr<nsISimpleEnumerator> enumerator;
  nsresult rv = obs->EnumerateObservers("service-worker-get-client",
                                        getter_AddRefs(enumerator));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  auto ProcessDocument = [&aDocuments](nsIPrincipal* aPrincipal, nsIDocument* aDoc) {
    if (!aDoc || !aDoc->GetWindow()) {
      return;
    }

    bool equals = false;
    aPrincipal->Equals(aDoc->NodePrincipal(), &equals);
    if (!equals) {
      return;
    }

    if (!Preferences::GetBool("dom.serviceWorkers.testing.enabled") &&
        !IsFromAuthenticatedOrigin(aDoc)) {
      return;
    }

    ServiceWorkerClientInfo clientInfo(aDoc);
    aDocuments.AppendElement(aDoc);
  };

  // Since it's not simple to check whether a document is in
  // mControlledDocuments, we take different code paths depending on whether we
  // need to look at all documents.  The common parts of the two loops are
  // factored out into the ProcessDocument lambda.
  if (aIncludeUncontrolled) {
    bool loop = true;
    while (NS_SUCCEEDED(enumerator->HasMoreElements(&loop)) && loop) {
      nsCOMPtr<nsISupports> ptr;
      rv = enumerator->GetNext(getter_AddRefs(ptr));
      if (NS_WARN_IF(NS_FAILED(rv))) {
        continue;
      }

      nsCOMPtr<nsIDocument> doc = do_QueryInterface(ptr);
      ProcessDocument(aPrincipal, doc);
    }
  } else {
    for (auto iter = mControlledDocuments.Iter(); !iter.Done(); iter.Next()) {
      ServiceWorkerRegistrationInfo* thisRegistration = iter.UserData();
      MOZ_ASSERT(thisRegistration);
      if (!registration->mScope.Equals(thisRegistration->mScope)) {
        continue;
      }

      nsCOMPtr<nsIDocument> doc = do_QueryInterface(iter.Key());
      ProcessDocument(aPrincipal, doc);
    }
  }
}

void
ServiceWorkerManager::MaybeClaimClient(nsIDocument* aDocument,
                                       ServiceWorkerRegistrationInfo* aWorkerRegistration)
{
  MOZ_ASSERT(aWorkerRegistration);
  MOZ_ASSERT(aWorkerRegistration->mActiveWorker);

  // Same origin check
  if (!aWorkerRegistration->mPrincipal->Equals(aDocument->NodePrincipal())) {
    return;
  }

  // The registration that should be controlling the client
  RefPtr<ServiceWorkerRegistrationInfo> matchingRegistration =
    GetServiceWorkerRegistrationInfo(aDocument);

  // The registration currently controlling the client
  RefPtr<ServiceWorkerRegistrationInfo> controllingRegistration;
  GetDocumentRegistration(aDocument, getter_AddRefs(controllingRegistration));

  if (aWorkerRegistration != matchingRegistration ||
        aWorkerRegistration == controllingRegistration) {
    return;
  }

  if (controllingRegistration) {
    StopControllingADocument(controllingRegistration);
  }

  StartControllingADocument(aWorkerRegistration, aDocument, NS_LITERAL_STRING(""));
  FireControllerChangeOnDocument(aDocument);
}

nsresult
ServiceWorkerManager::ClaimClients(nsIPrincipal* aPrincipal,
                                   const nsCString& aScope, uint64_t aId)
{
  RefPtr<ServiceWorkerRegistrationInfo> registration =
    GetRegistration(aPrincipal, aScope);

  if (!registration || !registration->mActiveWorker ||
      !(registration->mActiveWorker->ID() == aId)) {
    // The worker is not active.
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  for (auto iter = mAllDocuments.Iter(); !iter.Done(); iter.Next()) {
    nsCOMPtr<nsIDocument> document = do_QueryInterface(iter.Get()->GetKey());
    swm->MaybeClaimClient(document, registration);
  }

  return NS_OK;
}

nsresult
ServiceWorkerManager::SetSkipWaitingFlag(nsIPrincipal* aPrincipal,
                                         const nsCString& aScope,
                                         uint64_t aServiceWorkerID)
{
  RefPtr<ServiceWorkerRegistrationInfo> registration =
    GetRegistration(aPrincipal, aScope);
  if (NS_WARN_IF(!registration)) {
    return NS_ERROR_FAILURE;
  }

  if (registration->mInstallingWorker &&
      (registration->mInstallingWorker->ID() == aServiceWorkerID)) {
    registration->mInstallingWorker->SetSkipWaitingFlag();
  } else if (registration->mWaitingWorker &&
             (registration->mWaitingWorker->ID() == aServiceWorkerID)) {
    registration->mWaitingWorker->SetSkipWaitingFlag();
    if (registration->mWaitingWorker->State() == ServiceWorkerState::Installed) {
      registration->TryToActivateAsync();
    }
  } else {
    NS_WARNING("Failed to set skipWaiting flag, no matching worker.");
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

void
ServiceWorkerManager::FireControllerChange(ServiceWorkerRegistrationInfo* aRegistration)
{
  AssertIsOnMainThread();
  for (auto iter = mControlledDocuments.Iter(); !iter.Done(); iter.Next()) {
    if (iter.UserData() != aRegistration) {
      continue;
    }

    nsCOMPtr<nsIDocument> doc = do_QueryInterface(iter.Key());
    if (NS_WARN_IF(!doc)) {
      continue;
    }

    FireControllerChangeOnDocument(doc);
  }
}

already_AddRefed<ServiceWorkerRegistrationInfo>
ServiceWorkerManager::GetRegistration(nsIPrincipal* aPrincipal,
                                      const nsACString& aScope) const
{
  MOZ_ASSERT(aPrincipal);

  nsAutoCString scopeKey;
  nsresult rv = PrincipalToScopeKey(aPrincipal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  return GetRegistration(scopeKey, aScope);
}

NS_IMETHODIMP
ServiceWorkerManager::GetRegistrationByPrincipal(nsIPrincipal* aPrincipal,
                                                 const nsAString& aScope,
                                                 nsIServiceWorkerRegistrationInfo** aInfo)
{
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aInfo);

  nsCOMPtr<nsIURI> scopeURI;
  nsresult rv = NS_NewURI(getter_AddRefs(scopeURI), aScope, nullptr, nullptr);
  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<ServiceWorkerRegistrationInfo> info =
    GetServiceWorkerRegistrationInfo(aPrincipal, scopeURI);
  if (!info) {
    return NS_ERROR_FAILURE;
  }
  info.forget(aInfo);

  return NS_OK;
}

already_AddRefed<ServiceWorkerRegistrationInfo>
ServiceWorkerManager::GetRegistration(const nsACString& aScopeKey,
                                      const nsACString& aScope) const
{
  RefPtr<ServiceWorkerRegistrationInfo> reg;

  RegistrationDataPerPrincipal* data;
  if (!mRegistrationInfos.Get(aScopeKey, &data)) {
    return reg.forget();
  }

  data->mInfos.Get(aScope, getter_AddRefs(reg));
  return reg.forget();
}

ServiceWorkerRegistrationInfo*
ServiceWorkerManager::CreateNewRegistration(const nsCString& aScope,
                                            nsIPrincipal* aPrincipal)
{
#ifdef DEBUG
  AssertIsOnMainThread();
  nsCOMPtr<nsIURI> scopeURI;
  nsresult rv = NS_NewURI(getter_AddRefs(scopeURI), aScope, nullptr, nullptr);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  RefPtr<ServiceWorkerRegistrationInfo> tmp =
    GetRegistration(aPrincipal, aScope);
  MOZ_ASSERT(!tmp);
#endif

  ServiceWorkerRegistrationInfo* registration = new ServiceWorkerRegistrationInfo(aScope, aPrincipal);
  // From now on ownership of registration is with
  // mServiceWorkerRegistrationInfos.
  AddScopeAndRegistration(aScope, registration);
  return registration;
}

void
ServiceWorkerManager::MaybeRemoveRegistration(ServiceWorkerRegistrationInfo* aRegistration)
{
  MOZ_ASSERT(aRegistration);
  RefPtr<ServiceWorkerInfo> newest = aRegistration->Newest();
  if (!newest && HasScope(aRegistration->mPrincipal, aRegistration->mScope)) {
    aRegistration->Clear();
    RemoveRegistration(aRegistration);
  }
}

void
ServiceWorkerManager::RemoveRegistrationInternal(ServiceWorkerRegistrationInfo* aRegistration)
{
  MOZ_ASSERT(aRegistration);
  MOZ_ASSERT(!aRegistration->IsControllingDocuments());

  if (mShuttingDown) {
    return;
  }

  // All callers should be either from a job in which case the actor is
  // available, or from MaybeStopControlling(), in which case, this will only be
  // called if a valid registration is found. If a valid registration exists,
  // it means the actor is available since the original map of registrations is
  // populated by it, and any new registrations wait until the actor is
  // available before proceeding (See ServiceWorkerRegisterJob::Start).
  MOZ_ASSERT(mActor);

  PrincipalInfo principalInfo;
  if (NS_WARN_IF(NS_FAILED(PrincipalToPrincipalInfo(aRegistration->mPrincipal,
                                                    &principalInfo)))) {
    //XXXnsm I can't think of any other reason a stored principal would fail to
    //convert.
    NS_WARNING("Unable to unregister serviceworker due to possible OOM");
    return;
  }

  mActor->SendUnregister(principalInfo, NS_ConvertUTF8toUTF16(aRegistration->mScope));
}

void
ServiceWorkerManager::RemoveRegistration(ServiceWorkerRegistrationInfo* aRegistration)
{
  RemoveRegistrationInternal(aRegistration);
  MOZ_ASSERT(HasScope(aRegistration->mPrincipal, aRegistration->mScope));
  RemoveScopeAndRegistration(aRegistration);
}

namespace {
/**
 * See browser/components/sessionstore/Utils.jsm function hasRootDomain().
 *
 * Returns true if the |url| passed in is part of the given root |domain|.
 * For example, if |url| is "www.mozilla.org", and we pass in |domain| as
 * "mozilla.org", this will return true. It would return false the other way
 * around.
 */
bool
HasRootDomain(nsIURI* aURI, const nsACString& aDomain)
{
  AssertIsOnMainThread();
  MOZ_ASSERT(aURI);

  nsAutoCString host;
  nsresult rv = aURI->GetHost(host);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  nsACString::const_iterator start, end;
  host.BeginReading(start);
  host.EndReading(end);
  if (!FindInReadable(aDomain, start, end)) {
    return false;
  }

  if (host.Equals(aDomain)) {
    return true;
  }

  // Beginning of the string matches, can't look at the previous char.
  if (start.get() == host.BeginReading()) {
    // Equals failed so this is fine.
    return false;
  }

  char prevChar = *(--start);
  return prevChar == '.';
}

} // namespace

NS_IMETHODIMP
ServiceWorkerManager::GetAllRegistrations(nsIArray** aResult)
{
  AssertIsOnMainThread();

  nsCOMPtr<nsIMutableArray> array(do_CreateInstance(NS_ARRAY_CONTRACTID));
  if (!array) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (auto it1 = mRegistrationInfos.Iter(); !it1.Done(); it1.Next()) {
    for (auto it2 = it1.UserData()->mInfos.Iter(); !it2.Done(); it2.Next()) {
      ServiceWorkerRegistrationInfo* reg = it2.UserData();
      MOZ_ASSERT(reg);

      if (reg->mPendingUninstall) {
        continue;
      }

      array->AppendElement(reg, false);
    }
  }

  array.forget(aResult);
  return NS_OK;
}

// MUST ONLY BE CALLED FROM Remove(), RemoveAll() and RemoveAllRegistrations()!
void
ServiceWorkerManager::ForceUnregister(RegistrationDataPerPrincipal* aRegistrationData,
                                      ServiceWorkerRegistrationInfo* aRegistration)
{
  MOZ_ASSERT(aRegistrationData);
  MOZ_ASSERT(aRegistration);

  ServiceWorkerJobQueue* queue;
  aRegistrationData->mJobQueues.Get(aRegistration->mScope, &queue);
  if (queue) {
    queue->CancelJobs();
  }

  nsCOMPtr<nsITimer> timer =
    aRegistrationData->mUpdateTimers.Get(aRegistration->mScope);
  if (timer) {
    timer->Cancel();
    aRegistrationData->mUpdateTimers.Remove(aRegistration->mScope);
  }

  // Since Unregister is async, it is ok to call it in an enumeration.
  Unregister(aRegistration->mPrincipal, nullptr, NS_ConvertUTF8toUTF16(aRegistration->mScope));
}

NS_IMETHODIMP
ServiceWorkerManager::RemoveAndPropagate(const nsACString& aHost)
{
  Remove(aHost);
  PropagateRemove(aHost);
  return NS_OK;
}

void
ServiceWorkerManager::Remove(const nsACString& aHost)
{
  AssertIsOnMainThread();

  // We need to postpone this operation in case we don't have an actor because
  // this is needed by the ForceUnregister.
  if (!mActor) {
    RefPtr<nsIRunnable> runnable = new RemoveRunnable(aHost);
    AppendPendingOperation(runnable);
    return;
  }

  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  for (auto it1 = mRegistrationInfos.Iter(); !it1.Done(); it1.Next()) {
    ServiceWorkerManager::RegistrationDataPerPrincipal* data = it1.UserData();
    for (auto it2 = data->mInfos.Iter(); !it2.Done(); it2.Next()) {
      ServiceWorkerRegistrationInfo* reg = it2.UserData();
      nsCOMPtr<nsIURI> scopeURI;
      nsresult rv = NS_NewURI(getter_AddRefs(scopeURI), it2.Key(),
                              nullptr, nullptr);
      // This way subdomains are also cleared.
      if (NS_SUCCEEDED(rv) && HasRootDomain(scopeURI, aHost)) {
        swm->ForceUnregister(data, reg);
      }
    }
  }
}

void
ServiceWorkerManager::PropagateRemove(const nsACString& aHost)
{
  AssertIsOnMainThread();

  if (!mActor) {
    RefPtr<nsIRunnable> runnable = new PropagateRemoveRunnable(aHost);
    AppendPendingOperation(runnable);
    return;
  }

  mActor->SendPropagateRemove(nsCString(aHost));
}

void
ServiceWorkerManager::RemoveAll()
{
  AssertIsOnMainThread();

  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  for (auto it1 = mRegistrationInfos.Iter(); !it1.Done(); it1.Next()) {
    ServiceWorkerManager::RegistrationDataPerPrincipal* data = it1.UserData();
    for (auto it2 = data->mInfos.Iter(); !it2.Done(); it2.Next()) {
      ServiceWorkerRegistrationInfo* reg = it2.UserData();
      swm->ForceUnregister(data, reg);
    }
  }
}

void
ServiceWorkerManager::PropagateRemoveAll()
{
  AssertIsOnMainThread();
  MOZ_ASSERT(XRE_IsParentProcess());

  if (!mActor) {
    RefPtr<nsIRunnable> runnable = new PropagateRemoveAllRunnable();
    AppendPendingOperation(runnable);
    return;
  }

  mActor->SendPropagateRemoveAll();
}

void
ServiceWorkerManager::RemoveAllRegistrations(OriginAttributesPattern* aPattern)
{
  AssertIsOnMainThread();

  MOZ_ASSERT(aPattern);

  for (auto it1 = mRegistrationInfos.Iter(); !it1.Done(); it1.Next()) {
    ServiceWorkerManager::RegistrationDataPerPrincipal* data = it1.UserData();

    // We can use iteration because ForceUnregister (and Unregister) are
    // async. Otherwise doing some R/W operations on an hashtable during
    // iteration will crash.
    for (auto it2 = data->mInfos.Iter(); !it2.Done(); it2.Next()) {
      ServiceWorkerRegistrationInfo* reg = it2.UserData();

      MOZ_ASSERT(reg);
      MOZ_ASSERT(reg->mPrincipal);

      bool matches =
        aPattern->Matches(BasePrincipal::Cast(reg->mPrincipal)->OriginAttributesRef());
      if (!matches) {
        continue;
      }

      RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
      swm->ForceUnregister(data, reg);
    }
  }
}

NS_IMETHODIMP
ServiceWorkerManager::AddListener(nsIServiceWorkerManagerListener* aListener)
{
  AssertIsOnMainThread();

  if (!aListener || mListeners.Contains(aListener)) {
    return NS_ERROR_INVALID_ARG;
  }

  mListeners.AppendElement(aListener);

  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerManager::RemoveListener(nsIServiceWorkerManagerListener* aListener)
{
  AssertIsOnMainThread();

  if (!aListener || !mListeners.Contains(aListener)) {
    return NS_ERROR_INVALID_ARG;
  }

  mListeners.RemoveElement(aListener);

  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerManager::ShouldReportToWindow(mozIDOMWindowProxy* aWindow,
                                           const nsACString& aScope,
                                           bool* aResult)
{
  AssertIsOnMainThread();
  MOZ_ASSERT(aResult);

  *aResult = false;

  // Get the inner window ID to compare to our document windows below.
  nsCOMPtr<nsPIDOMWindowOuter> targetWin = nsPIDOMWindowOuter::From(aWindow);
  if (NS_WARN_IF(!targetWin)) {
    return NS_OK;
  }

  targetWin = targetWin->GetScriptableTop();
  uint64_t winId = targetWin->WindowID();

  // Check our weak registering document references first.  This way we clear
  // out as many dead weak references as possible when this method is called.
  WeakDocumentList* list = mRegisteringDocuments.Get(aScope);
  if (list) {
    for (int32_t i = list->Length() - 1; i >= 0; --i) {
      nsCOMPtr<nsIDocument> doc = do_QueryReferent(list->ElementAt(i));
      if (!doc) {
        list->RemoveElementAt(i);
        continue;
      }

      if (!doc->IsCurrentActiveDocument()) {
        continue;
      }

      nsCOMPtr<nsPIDOMWindowOuter> win = doc->GetWindow();
      if (!win) {
        continue;
      }

      win = win->GetScriptableTop();

      // Match.  We should report to this window.
      if (win && winId == win->WindowID()) {
        *aResult = true;
        return NS_OK;
      }
    }

    if (list->IsEmpty()) {
      list = nullptr;
      nsAutoPtr<WeakDocumentList> doomed;
      mRegisteringDocuments.RemoveAndForget(aScope, doomed);
    }
  }

  // Examine any windows performing a navigation that we are currently
  // intercepting.
  InterceptionList* intList = mNavigationInterceptions.Get(aScope);
  if (intList) {
    for (uint32_t i = 0; i < intList->Length(); ++i) {
      nsCOMPtr<nsIInterceptedChannel> channel = intList->ElementAt(i);

      nsCOMPtr<nsIChannel> inner;
      nsresult rv = channel->GetChannel(getter_AddRefs(inner));
      if (NS_WARN_IF(NS_FAILED(rv))) {
        continue;
      }

      uint64_t id = nsContentUtils::GetInnerWindowID(inner);
      if (id == 0) {
        continue;
      }

      nsCOMPtr<nsPIDOMWindowInner> win = nsGlobalWindow::GetInnerWindowWithId(id)->AsInner();
      if (!win) {
        continue;
      }

      nsCOMPtr<nsPIDOMWindowOuter> outer = win->GetScriptableTop();

      // Match.  We should report to this window.
      if (outer && winId == outer->WindowID()) {
        *aResult = true;
        return NS_OK;
      }
    }
  }

  // Next examine controlled documents to see if the windows match.
  for (auto iter = mControlledDocuments.Iter(); !iter.Done(); iter.Next()) {
    ServiceWorkerRegistrationInfo* reg = iter.UserData();
    MOZ_ASSERT(reg);
    if (!reg->mScope.Equals(aScope)) {
      continue;
    }

    nsCOMPtr<nsIDocument> doc = do_QueryInterface(iter.Key());
    if (!doc || !doc->IsCurrentActiveDocument()) {
      continue;
    }

    nsCOMPtr<nsPIDOMWindowOuter> win = doc->GetWindow();
    if (!win) {
      continue;
    }

    win = win->GetScriptableTop();

    // Match.  We should report to this window.
    if (win && winId == win->WindowID()) {
      *aResult = true;
      return NS_OK;
    }
  }

  // No match.  We should not report to this window.
  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerManager::Observe(nsISupports* aSubject,
                              const char* aTopic,
                              const char16_t* aData)
{
  if (strcmp(aTopic, PURGE_SESSION_HISTORY) == 0) {
    MOZ_ASSERT(XRE_IsParentProcess());
    RemoveAll();
    PropagateRemoveAll();
    return NS_OK;
  }

  if (strcmp(aTopic, PURGE_DOMAIN_DATA) == 0) {
    MOZ_ASSERT(XRE_IsParentProcess());
    nsAutoString domain(aData);
    RemoveAndPropagate(NS_ConvertUTF16toUTF8(domain));
    return NS_OK;
  }

  if (strcmp(aTopic, CLEAR_ORIGIN_DATA) == 0) {
    MOZ_ASSERT(XRE_IsParentProcess());
    OriginAttributesPattern pattern;
    MOZ_ALWAYS_TRUE(pattern.Init(nsAutoString(aData)));

    RemoveAllRegistrations(&pattern);
    return NS_OK;
  }

  if (strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID) == 0) {
    mShuttingDown = true;

    for (auto it1 = mRegistrationInfos.Iter(); !it1.Done(); it1.Next()) {
      for (auto it2 = it1.UserData()->mUpdateTimers.Iter(); !it2.Done(); it2.Next()) {
        nsCOMPtr<nsITimer> timer = it2.UserData();
        timer->Cancel();
      }
      it1.UserData()->mUpdateTimers.Clear();
    }

    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    if (obs) {
      obs->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID);

      if (XRE_IsParentProcess()) {
        obs->RemoveObserver(this, PURGE_SESSION_HISTORY);
        obs->RemoveObserver(this, PURGE_DOMAIN_DATA);
        obs->RemoveObserver(this, CLEAR_ORIGIN_DATA);
      }
    }

    if (mActor) {
      mActor->ManagerShuttingDown();

      RefPtr<TeardownRunnable> runnable = new TeardownRunnable(mActor);
      nsresult rv = NS_DispatchToMainThread(runnable);
      Unused << NS_WARN_IF(NS_FAILED(rv));
      mActor = nullptr;
    }
    return NS_OK;
  }

  MOZ_CRASH("Received message we aren't supposed to be registered for!");
  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerManager::PropagateSoftUpdate(JS::Handle<JS::Value> aOriginAttributes,
                                          const nsAString& aScope,
                                          JSContext* aCx)
{
  AssertIsOnMainThread();

  PrincipalOriginAttributes attrs;
  if (!aOriginAttributes.isObject() || !attrs.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  PropagateSoftUpdate(attrs, aScope);
  return NS_OK;
}

void
ServiceWorkerManager::PropagateSoftUpdate(const PrincipalOriginAttributes& aOriginAttributes,
                                          const nsAString& aScope)
{
  AssertIsOnMainThread();

  if (!mActor) {
    RefPtr<nsIRunnable> runnable =
      new PropagateSoftUpdateRunnable(aOriginAttributes, aScope);
    AppendPendingOperation(runnable);
    return;
  }

  mActor->SendPropagateSoftUpdate(aOriginAttributes, nsString(aScope));
}

NS_IMETHODIMP
ServiceWorkerManager::PropagateUnregister(nsIPrincipal* aPrincipal,
                                          nsIServiceWorkerUnregisterCallback* aCallback,
                                          const nsAString& aScope)
{
  AssertIsOnMainThread();
  MOZ_ASSERT(aPrincipal);

  if (!mActor) {
    RefPtr<nsIRunnable> runnable =
      new PropagateUnregisterRunnable(aPrincipal, aCallback, aScope);
    AppendPendingOperation(runnable);
    return NS_OK;
  }

  PrincipalInfo principalInfo;
  if (NS_WARN_IF(NS_FAILED(PrincipalToPrincipalInfo(aPrincipal,
                                                    &principalInfo)))) {
    return NS_ERROR_FAILURE;
  }

  mActor->SendPropagateUnregister(principalInfo, nsString(aScope));

  nsresult rv = Unregister(aPrincipal, aCallback, aScope);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

void
ServiceWorkerManager::NotifyListenersOnRegister(
                                        nsIServiceWorkerRegistrationInfo* aInfo)
{
  nsTArray<nsCOMPtr<nsIServiceWorkerManagerListener>> listeners(mListeners);
  for (size_t index = 0; index < listeners.Length(); ++index) {
    listeners[index]->OnRegister(aInfo);
  }
}

void
ServiceWorkerManager::NotifyListenersOnUnregister(
                                        nsIServiceWorkerRegistrationInfo* aInfo)
{
  nsTArray<nsCOMPtr<nsIServiceWorkerManagerListener>> listeners(mListeners);
  for (size_t index = 0; index < listeners.Length(); ++index) {
    listeners[index]->OnUnregister(aInfo);
  }
}

void
ServiceWorkerManager::AddRegisteringDocument(const nsACString& aScope,
                                             nsIDocument* aDoc)
{
  AssertIsOnMainThread();
  MOZ_ASSERT(!aScope.IsEmpty());
  MOZ_ASSERT(aDoc);

  WeakDocumentList* list = mRegisteringDocuments.LookupOrAdd(aScope);
  MOZ_ASSERT(list);

  for (int32_t i = list->Length() - 1; i >= 0; --i) {
    nsCOMPtr<nsIDocument> existing = do_QueryReferent(list->ElementAt(i));
    if (!existing) {
      list->RemoveElementAt(i);
      continue;
    }
    if (existing == aDoc) {
      return;
    }
  }

  list->AppendElement(do_GetWeakReference(aDoc));
}

class ServiceWorkerManager::InterceptionReleaseHandle final : public nsISupports
{
  const nsCString mScope;

  // Weak reference to channel is safe, because the channel holds a
  // reference to this object.  Also, the pointer is only used for
  // comparison purposes.
  nsIInterceptedChannel* mChannel;

  ~InterceptionReleaseHandle()
  {
    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    swm->RemoveNavigationInterception(mScope, mChannel);
  }

public:
  InterceptionReleaseHandle(const nsACString& aScope,
                            nsIInterceptedChannel* aChannel)
    : mScope(aScope)
    , mChannel(aChannel)
  {
    AssertIsOnMainThread();
    MOZ_ASSERT(!aScope.IsEmpty());
    MOZ_ASSERT(mChannel);
  }

  NS_DECL_ISUPPORTS
};

NS_IMPL_ISUPPORTS0(ServiceWorkerManager::InterceptionReleaseHandle);

void
ServiceWorkerManager::AddNavigationInterception(const nsACString& aScope,
                                                nsIInterceptedChannel* aChannel)
{
  AssertIsOnMainThread();
  MOZ_ASSERT(!aScope.IsEmpty());
  MOZ_ASSERT(aChannel);

  InterceptionList* list =
    mNavigationInterceptions.LookupOrAdd(aScope);
  MOZ_ASSERT(list);
  MOZ_ASSERT(!list->Contains(aChannel));

  nsCOMPtr<nsISupports> releaseHandle =
    new InterceptionReleaseHandle(aScope, aChannel);
  aChannel->SetReleaseHandle(releaseHandle);

  list->AppendElement(aChannel);
}

void
ServiceWorkerManager::RemoveNavigationInterception(const nsACString& aScope,
                                                   nsIInterceptedChannel* aChannel)
{
  AssertIsOnMainThread();
  MOZ_ASSERT(aChannel);
  InterceptionList* list =
    mNavigationInterceptions.Get(aScope);
  if (list) {
    MOZ_ALWAYS_TRUE(list->RemoveElement(aChannel));
    MOZ_ASSERT(!list->Contains(aChannel));
    if (list->IsEmpty()) {
      list = nullptr;
      nsAutoPtr<InterceptionList> doomed;
      mNavigationInterceptions.RemoveAndForget(aScope, doomed);
    }
  }
}

class UpdateTimerCallback final : public nsITimerCallback
{
  nsCOMPtr<nsIPrincipal> mPrincipal;
  const nsCString mScope;

  ~UpdateTimerCallback()
  {
  }

public:
  UpdateTimerCallback(nsIPrincipal* aPrincipal, const nsACString& aScope)
    : mPrincipal(aPrincipal)
    , mScope(aScope)
  {
    AssertIsOnMainThread();
    MOZ_ASSERT(mPrincipal);
    MOZ_ASSERT(!mScope.IsEmpty());
  }

  NS_IMETHOD
  Notify(nsITimer* aTimer) override
  {
    AssertIsOnMainThread();

    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    if (!swm) {
      // shutting down, do nothing
      return NS_OK;
    }

    swm->UpdateTimerFired(mPrincipal, mScope);
    return NS_OK;
  }

  NS_DECL_ISUPPORTS
};

NS_IMPL_ISUPPORTS(UpdateTimerCallback, nsITimerCallback)

void
ServiceWorkerManager::ScheduleUpdateTimer(nsIPrincipal* aPrincipal,
                                          const nsACString& aScope)
{
  AssertIsOnMainThread();
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(!aScope.IsEmpty());

  if (mShuttingDown) {
    return;
  }

  nsAutoCString scopeKey;
  nsresult rv = PrincipalToScopeKey(aPrincipal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  RegistrationDataPerPrincipal* data;
  if (!mRegistrationInfos.Get(scopeKey, &data)) {
    return;
  }

  nsCOMPtr<nsITimer> timer = data->mUpdateTimers.Get(aScope);
  if (timer) {
    timer->Cancel();
    data->mUpdateTimers.Remove(aScope);
  } else {
    timer = do_CreateInstance("@mozilla.org/timer;1", &rv);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return;
    }
  }

  nsCOMPtr<nsITimerCallback> callback = new UpdateTimerCallback(aPrincipal,
                                                                aScope);

  const uint32_t UPDATE_DELAY_MS = 1000;

  rv = timer->InitWithCallback(callback, UPDATE_DELAY_MS,
                               nsITimer::TYPE_ONE_SHOT);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  data->mUpdateTimers.Put(aScope, timer);
}

void
ServiceWorkerManager::UpdateTimerFired(nsIPrincipal* aPrincipal,
                                       const nsACString& aScope)
{
  AssertIsOnMainThread();
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(!aScope.IsEmpty());

  if (mShuttingDown) {
    return;
  }

  // First cleanup the timer.
  nsAutoCString scopeKey;
  nsresult rv = PrincipalToScopeKey(aPrincipal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  RegistrationDataPerPrincipal* data;
  if (!mRegistrationInfos.Get(scopeKey, &data)) {
    return;
  }

  nsCOMPtr<nsITimer> timer = data->mUpdateTimers.Get(aScope);
  if (timer) {
    timer->Cancel();
    data->mUpdateTimers.Remove(aScope);
  }

  RefPtr<ServiceWorkerRegistrationInfo> registration;
  data->mInfos.Get(aScope, getter_AddRefs(registration));
  if (!registration) {
    return;
  }

  if (!registration->CheckAndClearIfUpdateNeeded()) {
    return;
  }

  PrincipalOriginAttributes attrs =
    BasePrincipal::Cast(aPrincipal)->OriginAttributesRef();

  // Then trigger an update to fire asynchronously now.
  PropagateSoftUpdate(attrs, NS_ConvertUTF8toUTF16(aScope));
}

NS_IMPL_ISUPPORTS(ServiceWorkerInfo, nsIServiceWorkerInfo)

NS_IMETHODIMP
ServiceWorkerInfo::GetScriptSpec(nsAString& aScriptSpec)
{
  AssertIsOnMainThread();
  CopyUTF8toUTF16(mScriptSpec, aScriptSpec);
  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerInfo::GetCacheName(nsAString& aCacheName)
{
  AssertIsOnMainThread();
  aCacheName = mCacheName;
  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerInfo::GetDebugger(nsIWorkerDebugger** aResult)
{
  if (NS_WARN_IF(!aResult)) {
    return NS_ERROR_FAILURE;
  }

  return mServiceWorkerPrivate->GetDebugger(aResult);
}

NS_IMETHODIMP
ServiceWorkerInfo::AttachDebugger()
{
  return mServiceWorkerPrivate->AttachDebugger();
}

NS_IMETHODIMP
ServiceWorkerInfo::DetachDebugger()
{
  return mServiceWorkerPrivate->DetachDebugger();
}

void
ServiceWorkerInfo::AppendWorker(ServiceWorker* aWorker)
{
  MOZ_ASSERT(aWorker);
#ifdef DEBUG
  nsAutoString workerURL;
  aWorker->GetScriptURL(workerURL);
  MOZ_ASSERT(workerURL.Equals(NS_ConvertUTF8toUTF16(mScriptSpec)));
#endif
  MOZ_ASSERT(!mInstances.Contains(aWorker));

  mInstances.AppendElement(aWorker);
  aWorker->SetState(State());
}

void
ServiceWorkerInfo::RemoveWorker(ServiceWorker* aWorker)
{
  MOZ_ASSERT(aWorker);
#ifdef DEBUG
  nsAutoString workerURL;
  aWorker->GetScriptURL(workerURL);
  MOZ_ASSERT(workerURL.Equals(NS_ConvertUTF8toUTF16(mScriptSpec)));
#endif
  MOZ_ASSERT(mInstances.Contains(aWorker));

  mInstances.RemoveElement(aWorker);
}

namespace {

class ChangeStateUpdater final : public nsRunnable
{
public:
  ChangeStateUpdater(const nsTArray<ServiceWorker*>& aInstances,
                     ServiceWorkerState aState)
    : mState(aState)
  {
    for (size_t i = 0; i < aInstances.Length(); ++i) {
      mInstances.AppendElement(aInstances[i]);
    }
  }

  NS_IMETHODIMP Run()
  {
    // We need to update the state of all instances atomically before notifying
    // them to make sure that the observed state for all instances inside
    // statechange event handlers is correct.
    for (size_t i = 0; i < mInstances.Length(); ++i) {
      mInstances[i]->SetState(mState);
    }
    for (size_t i = 0; i < mInstances.Length(); ++i) {
      mInstances[i]->DispatchStateChange(mState);
    }

    return NS_OK;
  }

private:
  AutoTArray<RefPtr<ServiceWorker>, 1> mInstances;
  ServiceWorkerState mState;
};

}

void
ServiceWorkerInfo::UpdateState(ServiceWorkerState aState)
{
  AssertIsOnMainThread();
#ifdef DEBUG
  // Any state can directly transition to redundant, but everything else is
  // ordered.
  if (aState != ServiceWorkerState::Redundant) {
    MOZ_ASSERT_IF(mState == ServiceWorkerState::EndGuard_, aState == ServiceWorkerState::Installing);
    MOZ_ASSERT_IF(mState == ServiceWorkerState::Installing, aState == ServiceWorkerState::Installed);
    MOZ_ASSERT_IF(mState == ServiceWorkerState::Installed, aState == ServiceWorkerState::Activating);
    MOZ_ASSERT_IF(mState == ServiceWorkerState::Activating, aState == ServiceWorkerState::Activated);
  }
  // Activated can only go to redundant.
  MOZ_ASSERT_IF(mState == ServiceWorkerState::Activated, aState == ServiceWorkerState::Redundant);
#endif
  // Flush any pending functional events to the worker when it transitions to the
  // activated state.
  // TODO: Do we care that these events will race with the propagation of the
  //       state change?
  if (aState == ServiceWorkerState::Activated && mState != aState) {
    mServiceWorkerPrivate->Activated();
  }
  mState = aState;
  nsCOMPtr<nsIRunnable> r = new ChangeStateUpdater(mInstances, mState);
  MOZ_ALWAYS_TRUE(NS_SUCCEEDED(NS_DispatchToMainThread(r.forget())));
}

ServiceWorkerInfo::ServiceWorkerInfo(ServiceWorkerRegistrationInfo* aReg,
                                     const nsACString& aScriptSpec,
                                     const nsAString& aCacheName)
  : mRegistration(aReg)
  , mScriptSpec(aScriptSpec)
  , mCacheName(aCacheName)
  , mState(ServiceWorkerState::EndGuard_)
  , mServiceWorkerID(GetNextID())
  , mServiceWorkerPrivate(new ServiceWorkerPrivate(this))
  , mSkipWaitingFlag(false)
{
  MOZ_ASSERT(mRegistration);
  MOZ_ASSERT(!aCacheName.IsEmpty());
}

ServiceWorkerInfo::~ServiceWorkerInfo()
{
  MOZ_ASSERT(mServiceWorkerPrivate);
  mServiceWorkerPrivate->NoteDeadServiceWorkerInfo();
}

static uint64_t gServiceWorkerInfoCurrentID = 0;

uint64_t
ServiceWorkerInfo::GetNextID() const
{
  return ++gServiceWorkerInfoCurrentID;
}

already_AddRefed<ServiceWorker>
ServiceWorkerInfo::GetOrCreateInstance(nsPIDOMWindowInner* aWindow)
{
  AssertIsOnMainThread();
  MOZ_ASSERT(aWindow);

  RefPtr<ServiceWorker> ref;

  for (uint32_t i = 0; i < mInstances.Length(); ++i) {
    MOZ_ASSERT(mInstances[i]);
    if (mInstances[i]->GetOwner() == aWindow) {
      ref = mInstances[i];
      break;
    }
  }

  if (!ref) {
    ref = new ServiceWorker(aWindow, this);
  }

  return ref.forget();
}

END_WORKERS_NAMESPACE
