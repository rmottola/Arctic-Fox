/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsNSSCallbacks_h
#define nsNSSCallbacks_h

#include "mozilla/Attributes.h"
#include "mozilla/CondVar.h"
#include "mozilla/Mutex.h"
#include "nsAutoPtr.h"
#include "nsCOMPtr.h"
#include "nsIStreamLoader.h"
#include "nspr.h"
#include "nsString.h"
#include "pk11func.h"
#include "pkix/pkixtypes.h"

#include "ocspt.h" // Must be included after pk11func.h.

class nsILoadGroup;

char*
PK11PasswordPrompt(PK11SlotInfo *slot, PRBool retry, void* arg);

void HandshakeCallback(PRFileDesc *fd, void *client_data);
SECStatus CanFalseStartCallback(PRFileDesc* fd, void* client_data,
                                PRBool *canFalseStart);

class nsHTTPListener final : public nsIStreamLoaderObserver
{
private:
  // For XPCOM implementations that are not a base class for some other
  // class, it is good practice to make the destructor non-virtual and
  // private.  Then the only way to delete the object is via Release.
#ifdef _MSC_VER
  // C4265: Class has virtual members but destructor is not virtual
  __pragma(warning(disable:4265))
#endif
  ~nsHTTPListener();

public:
  nsHTTPListener();

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSISTREAMLOADEROBSERVER

  nsCOMPtr<nsIStreamLoader> mLoader;

  nsresult mResultCode;

  bool mHttpRequestSucceeded;
  uint16_t mHttpResponseCode;
  nsCString mHttpResponseContentType;

  const uint8_t* mResultData; // allocated in loader, but owned by listener
  uint32_t mResultLen;

  mozilla::Mutex mLock;
  mozilla::CondVar mCondition;
  volatile bool mWaitFlag;

  bool mResponsibleForDoneSignal;
  void send_done_signal();

  // no nsCOMPtr. When I use it, I get assertions about
  //   loadgroup not being thread safe.
  // So, let's use a raw pointer and ensure we only create and destroy
  // it on the network thread ourselves.
  nsILoadGroup *mLoadGroup;
  PRThread *mLoadGroupOwnerThread;
  void FreeLoadGroup(bool aCancelLoad);
};

class nsNSSHttpServerSession
{
public:
  typedef mozilla::pkix::Result Result;

  nsCString mHost;
  uint16_t mPort;

  static Result createSessionFcn(const char* host,
                                 uint16_t portnum,
                                 SEC_HTTP_SERVER_SESSION* pSession);
};

class nsNSSHttpRequestSession
{
protected:
  mozilla::ThreadSafeAutoRefCnt mRefCount;

public:
  typedef mozilla::pkix::Result Result;

  static Result createFcn(SEC_HTTP_SERVER_SESSION session,
                          const char* httpProtocolVariant,
                          const char* pathAndQueryString,
                          const char* httpRequestMethod,
                          const PRIntervalTime timeout,
                          SEC_HTTP_REQUEST_SESSION* pRequest);

  Result setPostDataFcn(const char* httpData,
                        const uint32_t httpDataLen,
                        const char* httpContentType);

  Result trySendAndReceiveFcn(PRPollDesc** pPollDesc,
                              uint16_t* httpResponseCode,
                              const char** httpResponseContentType,
                              const char** httpResponseHeaders,
                              const char** httpResponseData,
                              uint32_t* httpResponseDataLen);

  void AddRef();
  void Release();

  nsCString mURL;
  nsCString mRequestMethod;

  bool mHasPostData;
  nsCString mPostData;
  nsCString mPostContentType;

  PRIntervalTime mTimeoutInterval;

  RefPtr<nsHTTPListener> mListener;

protected:
  nsNSSHttpRequestSession();
  ~nsNSSHttpRequestSession();

  Result internal_send_receive_attempt(bool& retryableError,
                                       PRPollDesc** pPollDesc,
                                       uint16_t* httpResponseCode,
                                       const char** httpResponseContentType,
                                       const char** httpResponseHeaders,
                                       const char** httpResponseData,
                                       uint32_t* httpResponseDataLen);
};

class nsNSSHttpInterface
{
public:
  typedef mozilla::pkix::Result Result;

  static Result createSessionFcn(const char* host,
                                 uint16_t portnum,
                                 SEC_HTTP_SERVER_SESSION* pSession)
  {
    return nsNSSHttpServerSession::createSessionFcn(host, portnum, pSession);
  }

  static Result createFcn(SEC_HTTP_SERVER_SESSION session,
                          const char* httpProtocolVariant,
                          const char* pathAndQueryString,
                          const char* httpRequestMethod,
                          const PRIntervalTime timeout,
                          SEC_HTTP_REQUEST_SESSION* pRequest)
  {
    return nsNSSHttpRequestSession::createFcn(session, httpProtocolVariant,
                                              pathAndQueryString,
                                              httpRequestMethod, timeout,
                                              pRequest);
  }

  static Result setPostDataFcn(SEC_HTTP_REQUEST_SESSION request,
                               const char* httpData,
                               const uint32_t httpDataLen,
                               const char* httpContentType)
  {
    return static_cast<nsNSSHttpRequestSession*>(request)
      ->setPostDataFcn(httpData, httpDataLen, httpContentType);
  }

  static Result trySendAndReceiveFcn(SEC_HTTP_REQUEST_SESSION request,
                                     PRPollDesc** pPollDesc,
                                     uint16_t* httpResponseCode,
                                     const char** httpResponseContentType,
                                     const char** httpResponseHeaders,
                                     const char** httpResponseData,
                                     uint32_t* httpResponseDataLen)
  {
    return static_cast<nsNSSHttpRequestSession*>(request)
      ->trySendAndReceiveFcn(pPollDesc, httpResponseCode,
                             httpResponseContentType, httpResponseHeaders,
                             httpResponseData, httpResponseDataLen);
  }
};

#endif // nsNSSCallbacks_h
