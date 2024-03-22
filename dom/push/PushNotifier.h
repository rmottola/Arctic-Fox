/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_PushNotifier_h
#define mozilla_dom_PushNotifier_h

#include "nsIPushNotifier.h"

#include "nsCycleCollectionParticipant.h"
#include "nsIPrincipal.h"
#include "nsString.h"
#include "nsTArray.h"

#include "mozilla/Maybe.h"

#define PUSHNOTIFIER_CONTRACTID \
  "@mozilla.org/push/Notifier;1"

// These constants are duplicated in `PushComponents.js`.
#define OBSERVER_TOPIC_PUSH "push-message"
#define OBSERVER_TOPIC_SUBSCRIPTION_CHANGE "push-subscription-change"

namespace mozilla {
namespace dom {

/**
 * `PushNotifier` implements the `nsIPushNotifier` interface. This service
 * forwards incoming push messages to service workers running in the content
 * process, and emits XPCOM observer notifications for system subscriptions.
 *
 * The XPCOM service can only be used from the main process. Callers running
 * in the content process should use
 * `ServiceWorkerManager::SendPush{SubscriptionChange}Event` directly.
 */
class PushNotifier final : public nsIPushNotifier
{
public:
  PushNotifier();

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(PushNotifier, nsIPushNotifier)
  NS_DECL_NSIPUSHNOTIFIER

  nsresult NotifyPush(const nsACString& aScope, nsIPrincipal* aPrincipal,
                      Maybe<nsTArray<uint8_t>> aData);
  nsresult NotifyPushWorkers(const nsACString& aScope,
                             nsIPrincipal* aPrincipal,
                             Maybe<nsTArray<uint8_t>> aData);
  nsresult NotifySubscriptionChangeWorkers(const nsACString& aScope,
                                           nsIPrincipal* aPrincipal);

protected:
  virtual ~PushNotifier();

private:
  nsresult NotifyPushObservers(const nsACString& aScope,
                               Maybe<nsTArray<uint8_t>> aData);
  nsresult NotifySubscriptionChangeObservers(const nsACString& aScope);
  bool ShouldNotifyObservers(nsIPrincipal* aPrincipal);
  bool ShouldNotifyWorkers(nsIPrincipal* aPrincipal);
};

/**
 * `PushMessage` implements the `nsIPushMessage` interface, similar to
 * the `PushMessageData` WebIDL interface. Instances of this class are
 * passed as the subject of `push-message` observer notifications for
 * system subscriptions.
 */
class PushMessage final : public nsIPushMessage
{
public:
  explicit PushMessage(const nsTArray<uint8_t>& aData);

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(PushMessage,
                                           nsIPushMessage)
  NS_DECL_NSIPUSHMESSAGE

protected:
  virtual ~PushMessage();

private:
  nsresult EnsureDecodedText();

  nsTArray<uint8_t> mData;
  nsString mDecodedText;
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_PushNotifier_h
