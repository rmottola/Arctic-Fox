/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_XMLHttpRequestMainThread_h
#define mozilla_dom_XMLHttpRequestMainThread_h

#include <bitset>
#include "nsAutoPtr.h"
#include "nsIXMLHttpRequest.h"
#include "nsISupportsUtils.h"
#include "nsIURI.h"
#include "nsIHttpChannel.h"
#include "nsIDocument.h"
#include "nsIStreamListener.h"
#include "nsWeakReference.h"
#include "nsIChannelEventSink.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsIInterfaceRequestor.h"
#include "nsIHttpHeaderVisitor.h"
#include "nsIProgressEventSink.h"
#include "nsJSUtils.h"
#include "nsTArray.h"
#include "nsITimer.h"
#include "nsIPrincipal.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsISizeOfEventTarget.h"
#include "nsIXPConnect.h"
#include "nsIInputStream.h"
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/NotNull.h"
#include "mozilla/dom/TypedArray.h"
#include "mozilla/dom/XMLHttpRequest.h"
#include "mozilla/dom/XMLHttpRequestBinding.h"
#include "mozilla/dom/XMLHttpRequestEventTarget.h"

#ifdef Status
/* Xlib headers insist on this for some reason... Nuke it because
   it'll override our member name */
#undef Status
#endif

class nsIJARChannel;
class nsILoadGroup;
class nsIUnicodeDecoder;
class nsIJSID;

namespace mozilla {
namespace dom {

class Blob;
class BlobSet;
class FormData;
class XMLHttpRequestUpload;

// A helper for building up an ArrayBuffer object's data
// before creating the ArrayBuffer itself.  Will do doubling
// based reallocation, up to an optional maximum growth given.
//
// When all the data has been appended, call getArrayBuffer,
// passing in the JSContext* for which the ArrayBuffer object
// is to be created.  This also implicitly resets the builder,
// or it can be reset explicitly at any point by calling reset().
class ArrayBufferBuilder
{
  uint8_t* mDataPtr;
  uint32_t mCapacity;
  uint32_t mLength;
  void* mMapPtr;
public:
  ArrayBufferBuilder();
  ~ArrayBufferBuilder();

  void reset();

  // Will truncate if aNewCap is < length().
  bool setCapacity(uint32_t aNewCap);

  // Append aDataLen bytes from data to the current buffer.  If we
  // need to grow the buffer, grow by doubling the size up to a
  // maximum of aMaxGrowth (if given).  If aDataLen is greater than
  // what the new capacity would end up as, then grow by aDataLen.
  //
  // The data parameter must not overlap with anything beyond the
  // builder's current valid contents [0..length)
  bool append(const uint8_t* aNewData, uint32_t aDataLen,
              uint32_t aMaxGrowth = 0);

  uint32_t length()   { return mLength; }
  uint32_t capacity() { return mCapacity; }

  JSObject* getArrayBuffer(JSContext* aCx);

  // Memory mapping to starting position of file(aFile) in the zip
  // package(aJarFile).
  //
  // The file in the zip package has to be uncompressed and the starting
  // position of the file must be aligned according to array buffer settings
  // in JS engine.
  nsresult mapToFileInPackage(const nsCString& aFile, nsIFile* aJarFile);

protected:
  static bool areOverlappingRegions(const uint8_t* aStart1, uint32_t aLength1,
                                    const uint8_t* aStart2, uint32_t aLength2);
};

class nsXMLHttpRequestXPCOMifier;

// Make sure that any non-DOM interfaces added here are also added to
// nsXMLHttpRequestXPCOMifier.
class XMLHttpRequestMainThread final : public XMLHttpRequest,
                                       public nsIXMLHttpRequest,
                                       public nsIJSXMLHttpRequest,
                                       public nsIStreamListener,
                                       public nsIChannelEventSink,
                                       public nsIProgressEventSink,
                                       public nsIInterfaceRequestor,
                                       public nsSupportsWeakReference,
                                       public nsITimerCallback,
                                       public nsISizeOfEventTarget
{
  friend class nsXHRParseEndListener;
  friend class nsXMLHttpRequestXPCOMifier;

public:
  enum class ProgressEventType : uint8_t {
    loadstart,
    progress,
    error,
    abort,
    timeout,
    load,
    loadend,
    ENUM_MAX
  };

  XMLHttpRequestMainThread();

  void Construct(nsIPrincipal* aPrincipal,
                 nsIGlobalObject* aGlobalObject,
                 nsIURI* aBaseURI = nullptr,
                 nsILoadGroup* aLoadGroup = nullptr)
  {
    MOZ_ASSERT(aPrincipal);
    MOZ_ASSERT_IF(nsCOMPtr<nsPIDOMWindowInner> win = do_QueryInterface(
      aGlobalObject), win->IsInnerWindow());
    mPrincipal = aPrincipal;
    BindToOwner(aGlobalObject);
    mBaseURI = aBaseURI;
    mLoadGroup = aLoadGroup;
  }

  void InitParameters(bool aAnon, bool aSystem);

  void SetParameters(bool aAnon, bool aSystem)
  {
    mIsAnon = aAnon || aSystem;
    mIsSystem = aSystem;
  }

  NS_DECL_ISUPPORTS_INHERITED

  // nsIXMLHttpRequest
  NS_DECL_NSIXMLHTTPREQUEST

  NS_FORWARD_NSIXMLHTTPREQUESTEVENTTARGET(XMLHttpRequestEventTarget::)

  // nsIStreamListener
  NS_DECL_NSISTREAMLISTENER

  // nsIRequestObserver
  NS_DECL_NSIREQUESTOBSERVER

  // nsIChannelEventSink
  NS_DECL_NSICHANNELEVENTSINK

  // nsIProgressEventSink
  NS_DECL_NSIPROGRESSEVENTSINK

  // nsIInterfaceRequestor
  NS_DECL_NSIINTERFACEREQUESTOR

  // nsITimerCallback
  NS_DECL_NSITIMERCALLBACK

  // nsISizeOfEventTarget
  virtual size_t
    SizeOfEventTargetIncludingThis(MallocSizeOf aMallocSizeOf) const override;

  NS_REALLY_FORWARD_NSIDOMEVENTTARGET(XMLHttpRequestEventTarget)

  // states
  virtual uint16_t ReadyState() const override;

  // request
  virtual void
  Open(const nsACString& aMethod, const nsAString& aUrl,
       ErrorResult& aRv) override
  {
    Open(aMethod, aUrl, true,
         Optional<nsAString>(),
         Optional<nsAString>(),
         aRv);
  }

  virtual void
  Open(const nsACString& aMethod, const nsAString& aUrl, bool aAsync,
       const Optional<nsAString>& aUser,
       const Optional<nsAString>& aPassword,
       ErrorResult& aRv) override
  {
    aRv = Open(aMethod, NS_ConvertUTF16toUTF8(aUrl),
               aAsync, aUser, aPassword);
  }

  virtual void
  SetRequestHeader(const nsACString& aHeader, const nsACString& aValue,
                   ErrorResult& aRv) override
  {
    aRv = SetRequestHeader(aHeader, aValue);
  }

  virtual uint32_t
  Timeout() const override
  {
    return mTimeoutMilliseconds;
  }

  virtual void
  SetTimeout(uint32_t aTimeout, ErrorResult& aRv) override;

  virtual bool WithCredentials() const override;

  virtual void
  SetWithCredentials(bool aWithCredentials, ErrorResult& aRv) override;

  virtual XMLHttpRequestUpload*
  GetUpload(ErrorResult& aRv) override;

private:
  virtual ~XMLHttpRequestMainThread();

  class RequestBody
  {
  public:
    RequestBody() : mType(eUninitialized)
    {
    }
    explicit RequestBody(const ArrayBuffer* aArrayBuffer) : mType(eArrayBuffer)
    {
      mValue.mArrayBuffer = aArrayBuffer;
    }
    explicit RequestBody(const ArrayBufferView* aArrayBufferView) : mType(eArrayBufferView)
    {
      mValue.mArrayBufferView = aArrayBufferView;
    }
    explicit RequestBody(Blob& aBlob) : mType(eBlob)
    {
      mValue.mBlob = &aBlob;
    }
    explicit RequestBody(nsIDocument* aDocument) : mType(eDocument)
    {
      mValue.mDocument = aDocument;
    }
    explicit RequestBody(const nsAString& aString) : mType(eDOMString)
    {
      mValue.mString = &aString;
    }
    explicit RequestBody(FormData& aFormData) : mType(eFormData)
    {
      mValue.mFormData = &aFormData;
    }
    explicit RequestBody(nsIInputStream* aStream) : mType(eInputStream)
    {
      mValue.mStream = aStream;
    }

    enum Type {
      eUninitialized,
      eArrayBuffer,
      eArrayBufferView,
      eBlob,
      eDocument,
      eDOMString,
      eFormData,
      eInputStream
    };
    union Value {
      const ArrayBuffer* mArrayBuffer;
      const ArrayBufferView* mArrayBufferView;
      Blob* mBlob;
      nsIDocument* mDocument;
      const nsAString* mString;
      FormData* mFormData;
      nsIInputStream* mStream;
    };

    Type GetType() const
    {
      MOZ_ASSERT(mType != eUninitialized);
      return mType;
    }
    Value GetValue() const
    {
      MOZ_ASSERT(mType != eUninitialized);
      return mValue;
    }

  private:
    Type mType;
    Value mValue;
  };

  static nsresult GetRequestBody(nsIVariant* aVariant,
                                 const Nullable<RequestBody>& aBody,
                                 nsIInputStream** aResult,
                                 uint64_t* aContentLength,
                                 nsACString& aContentType,
                                 nsACString& aCharset);

  nsresult Send(nsIVariant* aVariant, const Nullable<RequestBody>& aBody);
  nsresult Send(const Nullable<RequestBody>& aBody)
  {
    return Send(nullptr, aBody);
  }
  nsresult Send(const RequestBody& aBody)
  {
    return Send(Nullable<RequestBody>(aBody));
  }

  bool IsCrossSiteCORSRequest() const;
  bool IsDeniedCrossSiteCORSRequest();

  // Tell our channel what network interface ID we were told to use.
  // If it's an HTTP channel and we were told to use a non-default
  // interface ID.
  void PopulateNetworkInterfaceId();

public:
  virtual void
  Send(JSContext* /*aCx*/, ErrorResult& aRv) override
  {
    aRv = Send(Nullable<RequestBody>());
  }

  virtual void
  Send(JSContext* /*aCx*/, const ArrayBuffer& aArrayBuffer,
       ErrorResult& aRv) override
  {
    aRv = Send(RequestBody(&aArrayBuffer));
  }

  virtual void
  Send(JSContext* /*aCx*/, const ArrayBufferView& aArrayBufferView,
       ErrorResult& aRv) override
  {
    aRv = Send(RequestBody(&aArrayBufferView));
  }

  virtual void
  Send(JSContext* /*aCx*/, Blob& aBlob, ErrorResult& aRv) override
  {
    aRv = Send(RequestBody(aBlob));
  }

  virtual void
  Send(JSContext* /*aCx*/, nsIDocument& aDoc, ErrorResult& aRv) override
  {
    aRv = Send(RequestBody(&aDoc));
  }

  virtual void
  Send(JSContext* aCx, const nsAString& aString, ErrorResult& aRv) override
  {
    if (DOMStringIsNull(aString)) {
      Send(aCx, aRv);
    }
    else {
      aRv = Send(RequestBody(aString));
    }
  }

  virtual void
  Send(JSContext* /*aCx*/, FormData& aFormData, ErrorResult& aRv) override
  {
    aRv = Send(RequestBody(aFormData));
  }

  virtual void
  Send(JSContext* aCx, nsIInputStream* aStream, ErrorResult& aRv) override
  {
    NS_ASSERTION(aStream, "Null should go to string version");
    nsCOMPtr<nsIXPConnectWrappedJS> wjs = do_QueryInterface(aStream);
    if (wjs) {
      JSObject* data = wjs->GetJSObject();
      if (!data) {
        aRv.Throw(NS_ERROR_DOM_TYPE_ERR);
        return;
      }
      JS::Rooted<JS::Value> dataAsValue(aCx, JS::ObjectValue(*data));
      nsAutoString dataAsString;
      if (ConvertJSValueToString(aCx, dataAsValue, eNull,
                                 eNull, dataAsString)) {
        Send(aCx, dataAsString, aRv);
      } else {
        aRv.Throw(NS_ERROR_FAILURE);
      }
      return;
    }
    aRv = Send(RequestBody(aStream));
  }

  void
  Abort() {
    ErrorResult rv;
    Abort(rv);
    MOZ_ASSERT(!rv.Failed());
  }

  virtual void
  Abort(ErrorResult& aRv) override;

  // response
  virtual void
  GetResponseURL(nsAString& aUrl) override;

  virtual uint32_t
  GetStatus(ErrorResult& aRv) override;

  virtual void
  GetStatusText(nsACString& aStatusText, ErrorResult& aRv) override;

  virtual void
  GetResponseHeader(const nsACString& aHeader, nsACString& aResult,
                    ErrorResult& aRv) override;

  void
  GetResponseHeader(const nsAString& aHeader, nsAString& aResult,
                    ErrorResult& aRv)
  {
    nsAutoCString result;
    GetResponseHeader(NS_ConvertUTF16toUTF8(aHeader), result, aRv);
    if (result.IsVoid()) {
      aResult.SetIsVoid(true);
    }
    else {
      // The result value should be inflated:
      CopyASCIItoUTF16(result, aResult);
    }
  }

  virtual void
  GetAllResponseHeaders(nsACString& aResponseHeaders,
                        ErrorResult& aRv) override;

  bool IsSafeHeader(const nsACString& aHeaderName,
                    NotNull<nsIHttpChannel*> aHttpChannel) const;

  virtual void
  OverrideMimeType(const nsAString& aMimeType, ErrorResult& aRv) override;

  virtual XMLHttpRequestResponseType
  ResponseType() const override
  {
    return XMLHttpRequestResponseType(mResponseType);
  }

  virtual void
  SetResponseType(XMLHttpRequestResponseType aType,
                  ErrorResult& aRv) override;

  virtual void
  GetResponse(JSContext* aCx, JS::MutableHandle<JS::Value> aResponse,
              ErrorResult& aRv) override;

  virtual void
  GetResponseText(nsAString& aResponseText, ErrorResult& aRv) override;

  virtual nsIDocument*
  GetResponseXML(ErrorResult& aRv) override;

  virtual bool
  MozBackgroundRequest() const override;

  virtual void
  SetMozBackgroundRequest(bool aMozBackgroundRequest, ErrorResult& aRv) override;

  virtual bool
  MozAnon() const override;

  virtual bool
  MozSystem() const override;

  virtual nsIChannel*
  GetChannel() const override
  {
    return mChannel;
  }

  virtual void
  GetNetworkInterfaceId(nsACString& aId) const override
  {
    aId = mNetworkInterfaceId;
  }

  virtual void
  SetNetworkInterfaceId(const nsACString& aId) override
  {
    mNetworkInterfaceId = aId;
  }

  // We need a GetInterface callable from JS for chrome JS
  virtual void
  GetInterface(JSContext* aCx, nsIJSID* aIID,
               JS::MutableHandle<JS::Value> aRetval,
               ErrorResult& aRv) override;

  // This fires a trusted readystatechange event, which is not cancelable and
  // doesn't bubble.
  nsresult FireReadystatechangeEvent();
  void DispatchProgressEvent(DOMEventTargetHelper* aTarget,
                             const ProgressEventType aType,
                             bool aLengthComputable,
                             int64_t aLoaded, int64_t aTotal);

  // Dispatch the "progress" event on the XHR or XHR.upload object if we've
  // received data since the last "progress" event. Also dispatches
  // "uploadprogress" as needed.
  void MaybeDispatchProgressEvents(bool aFinalProgress);

  // This is called by the factory constructor.
  nsresult Init();

  nsresult init(nsIPrincipal* principal,
                nsIScriptContext* scriptContext,
                nsPIDOMWindowInner* globalObject,
                nsIURI* baseURI);

  void SetRequestObserver(nsIRequestObserver* aObserver);

  NS_DECL_CYCLE_COLLECTION_SKIPPABLE_SCRIPT_HOLDER_CLASS_INHERITED(XMLHttpRequestMainThread,
                                                                   XMLHttpRequest)
  bool AllowUploadProgress();
  void RootJSResultObjects();

  virtual void DisconnectFromOwner() override;

  static void SetDontWarnAboutSyncXHR(bool aVal)
  {
    sDontWarnAboutSyncXHR = aVal;
  }
  static bool DontWarnAboutSyncXHR()
  {
    return sDontWarnAboutSyncXHR;
  }
protected:
  // XHR states are meant to mirror the XHR2 spec:
  //   https://xhr.spec.whatwg.org/#states
  enum class State : uint8_t {
    unsent,           // object has been constructed.
    opened,           // open() has been successfully invoked.
    headers_received, // redirects followed and response headers received.
    loading,          // response body is being received.
    done,             // data transfer concluded, whether success or error.
  };

  nsresult DetectCharset();
  nsresult AppendToResponseText(const char * aBuffer, uint32_t aBufferLen);
  static NS_METHOD StreamReaderFunc(nsIInputStream* in,
                void* closure,
                const char* fromRawSegment,
                uint32_t toOffset,
                uint32_t count,
                uint32_t *writeCount);
  nsresult CreateResponseParsedJSON(JSContext* aCx);
  void CreatePartialBlob(ErrorResult& aRv);
  bool CreateDOMBlob(nsIRequest *request);
  // Change the state of the object with this. The broadcast argument
  // determines if the onreadystatechange listener should be called.
  nsresult ChangeState(State aState, bool aBroadcast = true);
  already_AddRefed<nsILoadGroup> GetLoadGroup() const;
  nsIURI *GetBaseURI();

  already_AddRefed<nsIHttpChannel> GetCurrentHttpChannel();
  already_AddRefed<nsIJARChannel> GetCurrentJARChannel();

  bool IsSystemXHR() const;

  void ChangeStateToDone();

  void StartProgressEventTimer();

  nsresult OnRedirectVerifyCallback(nsresult result);

  nsresult Open(const nsACString& method, const nsACString& url, bool async,
                const Optional<nsAString>& user,
                const Optional<nsAString>& password);

  already_AddRefed<nsXMLHttpRequestXPCOMifier> EnsureXPCOMifier();

  nsCOMPtr<nsISupports> mContext;
  nsCOMPtr<nsIPrincipal> mPrincipal;
  nsCOMPtr<nsIChannel> mChannel;
  nsCOMPtr<nsIDocument> mResponseXML;
  nsTArray<nsCString> mCORSUnsafeHeaders;

  nsCOMPtr<nsIStreamListener> mXMLParserStreamListener;

  // used to implement getAllResponseHeaders()
  class nsHeaderVisitor : public nsIHttpHeaderVisitor
  {
  public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIHTTPHEADERVISITOR
    nsHeaderVisitor(const XMLHttpRequestMainThread& aXMLHttpRequest,
                    NotNull<nsIHttpChannel*> aHttpChannel)
      : mXHR(aXMLHttpRequest), mHttpChannel(aHttpChannel) {}
    const nsACString &Headers() { return mHeaders; }
  private:
    virtual ~nsHeaderVisitor() {}

    nsCString mHeaders;
    const XMLHttpRequestMainThread& mXHR;
    NotNull<nsCOMPtr<nsIHttpChannel>> mHttpChannel;
  };

  // The bytes of our response body. Only used for DEFAULT, ARRAYBUFFER and
  // BLOB responseTypes
  nsCString mResponseBody;

  // The text version of our response body. This is incrementally decoded into
  // as we receive network data. However for the DEFAULT responseType we
  // lazily decode into this from mResponseBody only when .responseText is
  // accessed.
  // Only used for DEFAULT and TEXT responseTypes.
  nsString mResponseText;

  // For DEFAULT responseType we use this to keep track of how far we've
  // lazily decoded from mResponseBody to mResponseText
  uint32_t mResponseBodyDecodedPos;

  // Decoder used for decoding into mResponseText
  // Only used for DEFAULT, TEXT and JSON responseTypes.
  // In cases where we've only received half a surrogate, the decoder itself
  // carries the state to remember this. Next time we receive more data we
  // simply feed the new data into the decoder which will handle the second
  // part of the surrogate.
  nsCOMPtr<nsIUnicodeDecoder> mDecoder;

  nsCString mResponseCharset;

  void MatchCharsetAndDecoderToResponseDocument();

  XMLHttpRequestResponseType mResponseType;

  // It is either a cached blob-response from the last call to GetResponse,
  // but is also explicitly set in OnStopRequest.
  RefPtr<Blob> mResponseBlob;
  // Non-null only when we are able to get a os-file representation of the
  // response, i.e. when loading from a file.
  RefPtr<Blob> mDOMBlob;
  // We stream data to mBlobSet when response type is "blob" or "moz-blob"
  // and mDOMBlob is null.
  nsAutoPtr<BlobSet> mBlobSet;

  nsString mOverrideMimeType;

  /**
   * The notification callbacks the channel had when Send() was
   * called.  We want to forward things here as needed.
   */
  nsCOMPtr<nsIInterfaceRequestor> mNotificationCallbacks;
  /**
   * Sink interfaces that we implement that mNotificationCallbacks may
   * want to also be notified for.  These are inited lazily if we're
   * asked for the relevant interface.
   */
  nsCOMPtr<nsIChannelEventSink> mChannelEventSink;
  nsCOMPtr<nsIProgressEventSink> mProgressEventSink;

  nsIRequestObserver* mRequestObserver;

  nsCOMPtr<nsIURI> mBaseURI;
  nsCOMPtr<nsILoadGroup> mLoadGroup;

  State mState;

  bool mFlagSynchronous;
  bool mFlagAborted;
  bool mFlagParseBody;
  bool mFlagSyncLooping;
  bool mFlagBackgroundRequest;
  bool mFlagHadUploadListenersOnSend;
  bool mFlagACwithCredentials;
  bool mFlagTimedOut;
  bool mFlagDeleted;

  // The XHR2 spec's send() flag. Set when the XHR begins uploading, until it
  // finishes downloading (or an error/abort has occurred during either phase).
  // Used to guard against the user trying to alter headers/etc when it's too
  // late, and ensure the XHR only handles one in-flight request at once.
  bool mFlagSend;

  RefPtr<XMLHttpRequestUpload> mUpload;
  int64_t mUploadTransferred;
  int64_t mUploadTotal;
  bool mUploadLengthComputable;
  bool mUploadComplete;
  bool mProgressSinceLastProgressEvent;

  // Timeout support
  PRTime mRequestSentTime;
  uint32_t mTimeoutMilliseconds;
  nsCOMPtr<nsITimer> mTimeoutTimer;
  void StartTimeoutTimer();
  void HandleTimeoutCallback();

  bool mErrorLoad;
  bool mErrorParsingXML;
  bool mWaitingForOnStopRequest;
  bool mProgressTimerIsActive;
  bool mIsHtml;
  bool mWarnAboutMultipartHtml;
  bool mWarnAboutSyncHtml;
  bool mLoadLengthComputable;
  int64_t mLoadTotal; // 0 if not known.
  // Amount of script-exposed (i.e. after undoing gzip compresion) data
  // received.
  uint64_t mDataAvailable;
  // Number of HTTP message body bytes received so far. This quantity is
  // in the same units as Content-Length and mLoadTotal, and hence counts
  // compressed bytes when the channel has gzip Content-Encoding. If the
  // channel does not have Content-Encoding, this will be the same as
  // mDataReceived except between the OnProgress that changes mLoadTransferred
  // and the corresponding OnDataAvailable (which changes mDataReceived).
  // Ordering of OnProgress and OnDataAvailable is undefined.
  int64_t mLoadTransferred;
  nsCOMPtr<nsITimer> mProgressNotifier;
  void HandleProgressTimerCallback();

  bool mIsSystem;
  bool mIsAnon;

  // A platform-specific identifer to represent the network interface
  // that this request is associated with.
  nsCString mNetworkInterfaceId;

  /**
   * Close the XMLHttpRequest's channels and dispatch appropriate progress
   * events.
   *
   * @param aType The progress event type.
   */
  void CloseRequestWithError(const ProgressEventType aType);

  bool mFirstStartRequestSeen;
  bool mInLoadProgressEvent;

  nsCOMPtr<nsIAsyncVerifyRedirectCallback> mRedirectCallback;
  nsCOMPtr<nsIChannel> mNewRedirectChannel;

  JS::Heap<JS::Value> mResultJSON;

  ArrayBufferBuilder mArrayBufferBuilder;
  JS::Heap<JSObject*> mResultArrayBuffer;
  bool mIsMappedArrayBuffer;

  void ResetResponse();

  bool ShouldBlockAuthPrompt();

  struct RequestHeader
  {
    nsCString header;
    nsCString value;
  };
  nsTArray<RequestHeader> mModifiedRequestHeaders;

  nsTHashtable<nsCStringHashKey> mAlreadySetHeaders;

  // Helper object to manage our XPCOM scriptability bits
  nsXMLHttpRequestXPCOMifier* mXPCOMifier;

  static bool sDontWarnAboutSyncXHR;
};

class MOZ_STACK_CLASS AutoDontWarnAboutSyncXHR
{
public:
  AutoDontWarnAboutSyncXHR() : mOldVal(XMLHttpRequestMainThread::DontWarnAboutSyncXHR())
  {
    XMLHttpRequestMainThread::SetDontWarnAboutSyncXHR(true);
  }

  ~AutoDontWarnAboutSyncXHR()
  {
    XMLHttpRequestMainThread::SetDontWarnAboutSyncXHR(mOldVal);
  }

private:
  bool mOldVal;
};

// A shim class designed to expose the non-DOM interfaces of
// XMLHttpRequest via XPCOM stuff.
class nsXMLHttpRequestXPCOMifier final : public nsIStreamListener,
                                         public nsIChannelEventSink,
                                         public nsIAsyncVerifyRedirectCallback,
                                         public nsIProgressEventSink,
                                         public nsIInterfaceRequestor,
                                         public nsITimerCallback
{
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(nsXMLHttpRequestXPCOMifier,
                                           nsIStreamListener)

  explicit nsXMLHttpRequestXPCOMifier(XMLHttpRequestMainThread* aXHR) :
    mXHR(aXHR)
  {
  }

private:
  ~nsXMLHttpRequestXPCOMifier() {
    if (mXHR) {
      mXHR->mXPCOMifier = nullptr;
    }
  }

public:
  NS_FORWARD_NSISTREAMLISTENER(mXHR->)
  NS_FORWARD_NSIREQUESTOBSERVER(mXHR->)
  NS_FORWARD_NSICHANNELEVENTSINK(mXHR->)
  NS_FORWARD_NSIASYNCVERIFYREDIRECTCALLBACK(mXHR->)
  NS_FORWARD_NSIPROGRESSEVENTSINK(mXHR->)
  NS_FORWARD_NSITIMERCALLBACK(mXHR->)

  NS_DECL_NSIINTERFACEREQUESTOR

private:
  RefPtr<XMLHttpRequestMainThread> mXHR;
};

class nsXHRParseEndListener : public nsIDOMEventListener
{
public:
  NS_DECL_ISUPPORTS
  NS_IMETHOD HandleEvent(nsIDOMEvent *event) override
  {
    nsCOMPtr<nsIXMLHttpRequest> xhr = do_QueryReferent(mXHR);
    if (xhr) {
      static_cast<XMLHttpRequestMainThread*>(xhr.get())->ChangeStateToDone();
    }
    mXHR = nullptr;
    return NS_OK;
  }
  explicit nsXHRParseEndListener(nsIXMLHttpRequest* aXHR)
    : mXHR(do_GetWeakReference(aXHR)) {}
private:
  virtual ~nsXHRParseEndListener() {}

  nsWeakPtr mXHR;
};

} // dom namespace
} // mozilla namespace

#endif // mozilla_dom_XMLHttpRequestMainThread_h
