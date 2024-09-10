/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/RTCCertificate.h"

#include <cmath>
#include "cert.h"
#include "jsapi.h"
#include "mozilla/dom/CryptoKey.h"
#include "mozilla/dom/RTCCertificateBinding.h"
#include "mozilla/dom/WebCryptoCommon.h"
#include "mozilla/dom/WebCryptoTask.h"
#include "mozilla/Snprintf.h"

#include <cstdio>

namespace mozilla {
namespace dom {

#define RTCCERTIFICATE_SC_VERSION 0x00000001

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(RTCCertificate, mGlobal)
NS_IMPL_CYCLE_COLLECTING_ADDREF(RTCCertificate)
NS_IMPL_CYCLE_COLLECTING_RELEASE(RTCCertificate)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(RTCCertificate)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

// Note: explicit casts necessary to avoid
//       warning C4307: '*' : integral constant overflow
#define ONE_DAY PRTime(PR_USEC_PER_SEC) * PRTime(60) /*sec*/ \
  * PRTime(60) /*min*/ * PRTime(24) /*hours*/
#define EXPIRATION_DEFAULT ONE_DAY * PRTime(30)
#define EXPIRATION_SLACK ONE_DAY
#define EXPIRATION_MAX ONE_DAY * PRTime(365) /*year*/

const size_t RTCCertificateCommonNameLength = 16;
const size_t RTCCertificateMinRsaSize = 1024;

class GenerateRTCCertificateTask : public GenerateAsymmetricKeyTask
{
public:
  GenerateRTCCertificateTask(nsIGlobalObject* aGlobal, JSContext* aCx,
                             const ObjectOrString& aAlgorithm,
                             const Sequence<nsString>& aKeyUsages)
      : GenerateAsymmetricKeyTask(aGlobal, aCx, aAlgorithm, true, aKeyUsages),
        mExpires(0),
        mAuthType(ssl_kea_null),
        mCertificate(nullptr),
        mSignatureAlg(SEC_OID_UNKNOWN)
  {
    // Expiry is 30 days after by default.
    // This is a sort of arbitrary range designed to be valid
    // now with some slack in case the other side expects
    // some before expiry.
    //

    mExpires = EXPIRATION_DEFAULT;
    if (!aAlgorithm.IsObject()) {
      return;
    }

    // Load the "expires" attribute from the algorithm dictionary.  This is
    // (currently) non-standard; it exists to support testing of certificate
    // expiration, since one month is too long to wait for a test to run.
    JS::Rooted<JS::Value> exp(aCx, JS::UndefinedValue());
    JS::Rooted<JSObject*> jsval(aCx, aAlgorithm.GetAsObject());
    bool ok = JS_GetProperty(aCx, jsval, "expires", &exp);
    int64_t expval;
    if (ok) {
      ok = JS::ToInt64(aCx, exp, &expval);
    }
    if (ok && expval > 0) {
      mExpires = std::min(expval, EXPIRATION_MAX);
    }
  }

private:
  PRTime mExpires;
  SSLKEAType mAuthType;
  ScopedCERTCertificate mCertificate;
  SECOidTag mSignatureAlg;

  static CERTName* GenerateRandomName(PK11SlotInfo* aSlot)
  {
    uint8_t randomName[RTCCertificateCommonNameLength];
    SECStatus rv = PK11_GenerateRandomOnSlot(aSlot, randomName,
                                             sizeof(randomName));
    if (rv != SECSuccess) {
      return nullptr;
    }

    char buf[sizeof(randomName) * 2 + 4];
    PL_strncpy(buf, "CN=", 3);
    for (size_t i = 0; i < sizeof(randomName); ++i) {
      snprintf(&buf[i * 2 + 3], 2, "%.2x", randomName[i]);
    }
    buf[sizeof(buf) - 1] = '\0';

    return CERT_AsciiToName(buf);
  }

  nsresult GenerateCertificate()
  {
    ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
    MOZ_ASSERT(slot.get());

    ScopedCERTName subjectName(GenerateRandomName(slot.get()));
    if (!subjectName) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    ScopedSECKEYPublicKey publicKey(mKeyPair->mPublicKey.get()->GetPublicKey());
    ScopedCERTSubjectPublicKeyInfo spki(
        SECKEY_CreateSubjectPublicKeyInfo(publicKey));
    if (!spki) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    ScopedCERTCertificateRequest certreq(
        CERT_CreateCertificateRequest(subjectName, spki, nullptr));
    if (!certreq) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    PRTime now = PR_Now();
    PRTime notBefore = now - EXPIRATION_SLACK;
    mExpires += now;

    ScopedCERTValidity validity(CERT_CreateValidity(notBefore, mExpires));
    if (!validity) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    unsigned long serial;
    // Note: This serial in principle could collide, but it's unlikely, and we
    // don't expect anyone to be validating certificates anyway.
    SECStatus rv =
        PK11_GenerateRandomOnSlot(slot,
                                  reinterpret_cast<unsigned char *>(&serial),
                                  sizeof(serial));
    if (rv != SECSuccess) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    CERTCertificate* cert = CERT_CreateCertificate(serial, subjectName,
                                                   validity, certreq);
    if (!cert) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }
    mCertificate.reset(cert);
    return NS_OK;
  }

  nsresult SignCertificate()
  {
    MOZ_ASSERT(mSignatureAlg != SEC_OID_UNKNOWN);
    PLArenaPool *arena = mCertificate->arena;

    SECStatus rv = SECOID_SetAlgorithmID(arena, &mCertificate->signature,
                                         mSignatureAlg, nullptr);
    if (rv != SECSuccess) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    // Set version to X509v3.
    *(mCertificate->version.data) = SEC_CERTIFICATE_VERSION_3;
    mCertificate->version.len = 1;

    SECItem innerDER = { siBuffer, nullptr, 0 };
    if (!SEC_ASN1EncodeItem(arena, &innerDER, mCertificate,
                            SEC_ASN1_GET(CERT_CertificateTemplate))) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    SECItem *signedCert = PORT_ArenaZNew(arena, SECItem);
    if (!signedCert) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    ScopedSECKEYPrivateKey privateKey(mKeyPair->mPrivateKey.get()->GetPrivateKey());
    rv = SEC_DerSignData(arena, signedCert, innerDER.data, innerDER.len,
                         privateKey, mSignatureAlg);
    if (rv != SECSuccess) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }
    mCertificate->derCert = *signedCert;
    return NS_OK;
  }

  nsresult BeforeCrypto() override
  {
    if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_RSASSA_PKCS1)) {
      // Double check that size is OK.
      auto sz = static_cast<size_t>(mRsaParams.keySizeInBits);
      if (sz < RTCCertificateMinRsaSize) {
        return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      }

      KeyAlgorithmProxy& alg = mKeyPair->mPublicKey.get()->Algorithm();
      if (alg.mType != KeyAlgorithmProxy::RSA ||
          !alg.mRsa.mHash.mName.EqualsLiteral(WEBCRYPTO_ALG_SHA256)) {
        return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      }

      mSignatureAlg = SEC_OID_PKCS1_SHA256_WITH_RSA_ENCRYPTION;
      mAuthType = ssl_kea_rsa;

    } else if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_ECDSA)) {
      // We only support good curves in WebCrypto.
      // If that ever changes, check that a good one was chosen.

      mSignatureAlg = SEC_OID_ANSIX962_ECDSA_SHA256_SIGNATURE;
      mAuthType = ssl_kea_ecdh;
    } else {
      return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
    }
    return NS_OK;
  }

  nsresult DoCrypto() override
  {
    nsresult rv = GenerateAsymmetricKeyTask::DoCrypto();
    NS_ENSURE_SUCCESS(rv, rv);

    rv = GenerateCertificate();
    NS_ENSURE_SUCCESS(rv, rv);

    rv = SignCertificate();
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  virtual void Resolve() override
  {
    // Make copies of the private key and certificate, otherwise, when this
    // object is deleted, the structures they reference will be deleted too.
    SECKEYPrivateKey* key = mKeyPair->mPrivateKey.get()->GetPrivateKey();
    CERTCertificate* cert = CERT_DupCertificate(mCertificate);
    RefPtr<RTCCertificate> result =
        new RTCCertificate(mResultPromise->GetParentObject(),
                           key, cert, mAuthType, mExpires);
    mResultPromise->MaybeResolve(result);
  }
};

already_AddRefed<Promise>
RTCCertificate::GenerateCertificate(
    const GlobalObject& aGlobal, const ObjectOrString& aKeygenAlgorithm,
    ErrorResult& aRv, JSCompartment* aCompartment)
{
  nsIGlobalObject* global = xpc::NativeGlobal(aGlobal.Get());
  RefPtr<Promise> p = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }
  Sequence<nsString> usages;
  if (!usages.AppendElement(NS_LITERAL_STRING("sign"), fallible)) {
    return nullptr;
  }
  RefPtr<WebCryptoTask> task =
      new GenerateRTCCertificateTask(global, aGlobal.Context(),
                                     aKeygenAlgorithm, usages);
  task->DispatchWithPromise(p);
  return p.forget();
}

RTCCertificate::RTCCertificate(nsIGlobalObject* aGlobal)
    : mGlobal(aGlobal),
      mPrivateKey(nullptr),
      mCertificate(nullptr),
      mAuthType(ssl_kea_null),
      mExpires(0)
{
}

RTCCertificate::RTCCertificate(nsIGlobalObject* aGlobal,
                               SECKEYPrivateKey* aPrivateKey,
                               CERTCertificate* aCertificate,
                               SSLKEAType aAuthType,
                               PRTime aExpires)
    : mGlobal(aGlobal),
      mPrivateKey(aPrivateKey),
      mCertificate(aCertificate),
      mAuthType(aAuthType),
      mExpires(aExpires)
{
}

RTCCertificate::~RTCCertificate()
{
  nsNSSShutDownPreventionLock locker;
  if (isAlreadyShutDown()) {
    return;
  }
  destructorSafeDestroyNSSReference();
  shutdown(calledFromObject);
}

// This creates some interesting lifecycle consequences, since the DtlsIdentity
// holds NSS objects, but does not implement nsNSSShutDownObject.

// Unfortunately, the code that uses DtlsIdentity cannot always use that lock
// due to external linkage requirements.  Therefore, the lock is held on this
// object instead.  Consequently, the DtlsIdentity that this method returns must
// have a lifetime that is strictly shorter than the RTCCertificate.
//
// RTCPeerConnection provides this guarantee by holding a strong reference to
// the RTCCertificate.  It will cleanup any DtlsIdentity instances that it
// creates before the RTCCertificate reference is released.
RefPtr<DtlsIdentity>
RTCCertificate::CreateDtlsIdentity() const
{
  nsNSSShutDownPreventionLock locker;
  if (isAlreadyShutDown() || !mPrivateKey || !mCertificate) {
    return nullptr;
  }
  SECKEYPrivateKey* key = SECKEY_CopyPrivateKey(mPrivateKey);
  CERTCertificate* cert = CERT_DupCertificate(mCertificate);
  RefPtr<DtlsIdentity> id = new DtlsIdentity(key, cert, mAuthType);
  return id;
}

JSObject*
RTCCertificate::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return RTCCertificateBinding::Wrap(aCx, this, aGivenProto);
}

void
RTCCertificate::virtualDestroyNSSReference()
{
  destructorSafeDestroyNSSReference();
}

void
RTCCertificate::destructorSafeDestroyNSSReference()
{
  mPrivateKey.dispose();
  mCertificate.dispose();
}

bool
RTCCertificate::WritePrivateKey(JSStructuredCloneWriter* aWriter,
                                const nsNSSShutDownPreventionLock& aLockProof) const
{
  JsonWebKey jwk;
  nsresult rv = CryptoKey::PrivateKeyToJwk(mPrivateKey, jwk, aLockProof);
  if (NS_FAILED(rv)) {
    return false;
  }
  nsString json;
  if (!jwk.ToJSON(json)) {
    return false;
  }
  return WriteString(aWriter, json);
}

bool
RTCCertificate::WriteCertificate(JSStructuredCloneWriter* aWriter,
                                 const nsNSSShutDownPreventionLock& /*proof*/) const
{
  ScopedCERTCertificateList certs(CERT_CertListFromCert(mCertificate.get()));
  if (!certs || certs->len <= 0) {
    return false;
  }
  if (!JS_WriteUint32Pair(aWriter, certs->certs[0].len, 0)) {
    return false;
  }
  return JS_WriteBytes(aWriter, certs->certs[0].data, certs->certs[0].len);
}

bool
RTCCertificate::WriteStructuredClone(JSStructuredCloneWriter* aWriter) const
{
  nsNSSShutDownPreventionLock locker;
  if (isAlreadyShutDown() || !mPrivateKey || !mCertificate) {
    return false;
  }

  return JS_WriteUint32Pair(aWriter, RTCCERTIFICATE_SC_VERSION, mAuthType) &&
      JS_WriteUint32Pair(aWriter, (mExpires >> 32) & 0xffffffff,
                         mExpires & 0xffffffff) &&
      WritePrivateKey(aWriter, locker) &&
      WriteCertificate(aWriter, locker);
}

bool
RTCCertificate::ReadPrivateKey(JSStructuredCloneReader* aReader,
                               const nsNSSShutDownPreventionLock& aLockProof)
{
  nsString json;
  if (!ReadString(aReader, json)) {
    return false;
  }
  JsonWebKey jwk;
  if (!jwk.Init(json)) {
    return false;
  }
  mPrivateKey = CryptoKey::PrivateKeyFromJwk(jwk, aLockProof);
  return !!mPrivateKey;
}

bool
RTCCertificate::ReadCertificate(JSStructuredCloneReader* aReader,
                                const nsNSSShutDownPreventionLock& /*proof*/)
{
  CryptoBuffer cert;
  if (!ReadBuffer(aReader, cert) || cert.Length() == 0) {
    return false;
  }

  SECItem der = { siBuffer, cert.Elements(),
                  static_cast<unsigned int>(cert.Length()) };
  mCertificate = CERT_NewTempCertificate(CERT_GetDefaultCertDB(),
                                         &der, nullptr, true, true);
  return !!mCertificate;
}

bool
RTCCertificate::ReadStructuredClone(JSStructuredCloneReader* aReader)
{
  nsNSSShutDownPreventionLock locker;
  if (isAlreadyShutDown()) {
    return false;
  }

  uint32_t version, authType;
  if (!JS_ReadUint32Pair(aReader, &version, &authType) ||
      version != RTCCERTIFICATE_SC_VERSION) {
    return false;
  }
  mAuthType = static_cast<SSLKEAType>(authType);

  uint32_t high, low;
  if (!JS_ReadUint32Pair(aReader, &high, &low)) {
    return false;
  }
  mExpires = static_cast<PRTime>(high) << 32 | low;

  return ReadPrivateKey(aReader, locker) &&
      ReadCertificate(aReader, locker);
}

} // namespace dom
} // namespace mozilla
