/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _NSSSLSTATUS_H
#define _NSSSLSTATUS_H

#include "nsISSLStatus.h"
#include "nsCOMPtr.h"
#include "nsXPIDLString.h"
#include "nsIX509Cert.h"
#include "nsISerializable.h"
#include "nsIClassInfo.h"
#include "nsNSSCertificate.h" // For EVStatus

class nsSSLStatus final
  : public nsISSLStatus
  , public nsISerializable
  , public nsIClassInfo
{
protected:
  virtual ~nsSSLStatus();
public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSISSLSTATUS
  NS_DECL_NSISERIALIZABLE
  NS_DECL_NSICLASSINFO

  nsSSLStatus();

  void SetServerCert(nsNSSCertificate* aServerCert,
                     nsNSSCertificate::EVStatus aEVStatus);

  bool HasServerCert() {
    return mServerCert != nullptr;
  }

  /* public for initilization in this file */
  uint16_t mCipherSuite;
  uint16_t mProtocolVersion;

  bool mIsDomainMismatch;
  bool mIsNotValidAtThisTime;
  bool mIsUntrusted;
  bool mIsEV;

  bool mHasIsEVStatus;
  bool mHaveCipherSuiteAndProtocol;

  /* mHaveCertErrrorBits is relied on to determine whether or not a SPDY
     connection is eligible for joining in nsNSSSocketInfo::JoinConnection() */
  bool mHaveCertErrorBits;

private:
  nsCOMPtr<nsIX509Cert> mServerCert;
};

//9f1a2340-f33c-4063-bfc5-fc555c87dbc4
#define NS_SSLSTATUS_CID \
{ 0x9f1a2340, 0xf33c, 0x4063, \
  { 0xbf, 0xc5, 0xfc, 0x55, 0x5c, 0x87, 0xdb, 0xc4 } }

#endif
