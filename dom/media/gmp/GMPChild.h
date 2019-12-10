/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPChild_h_
#define GMPChild_h_

#include "mozilla/gmp/PGMPChild.h"
#include "GMPSharedMemManager.h"
#include "GMPTimerChild.h"
#include "GMPStorageChild.h"
#include "GMPLoader.h"
#include "gmp-async-shutdown.h"
#include "gmp-entrypoints.h"
#include "prlink.h"

namespace mozilla {
namespace gmp {

class GMPChild : public PGMPChild
               , public GMPSharedMem
               , public GMPAsyncShutdownHost
{
public:
  GMPChild();
  virtual ~GMPChild();

  bool Init(const std::string& aPluginPath,
            const std::string& aVoucherPath,
            base::ProcessId aParentPid,
            MessageLoop* aIOLoop,
            IPC::Channel* aChannel);
#ifdef XP_WIN
  bool PreLoadLibraries(const std::string& aPluginPath);
#endif
  MessageLoop* GMPMessageLoop();

  // Main thread only.
  GMPTimerChild* GetGMPTimers();
  GMPStorageChild* GetGMPStorage();

  // GMPSharedMem
  virtual void CheckThread() override;

  // GMPAsyncShutdownHost
  void ShutdownComplete() override;

#if defined(XP_MACOSX) && defined(MOZ_GMP_SANDBOX)
  void StartMacSandbox();
#endif

private:

  bool PreLoadPluginVoucher(const std::string& aPluginPath);
  void PreLoadSandboxVoucher();

  bool GetLibPath(nsACString& aOutLibPath);

  virtual bool RecvSetNodeId(const nsCString& aNodeId) override;
  virtual bool RecvStartPlugin() override;

  virtual PGMPVideoDecoderChild* AllocPGMPVideoDecoderChild() override;
  virtual bool DeallocPGMPVideoDecoderChild(PGMPVideoDecoderChild* aActor) override;
  virtual bool RecvPGMPVideoDecoderConstructor(PGMPVideoDecoderChild* aActor) override;

  virtual PGMPVideoEncoderChild* AllocPGMPVideoEncoderChild() override;
  virtual bool DeallocPGMPVideoEncoderChild(PGMPVideoEncoderChild* aActor) override;
  virtual bool RecvPGMPVideoEncoderConstructor(PGMPVideoEncoderChild* aActor) override;

  virtual PGMPDecryptorChild* AllocPGMPDecryptorChild() override;
  virtual bool DeallocPGMPDecryptorChild(PGMPDecryptorChild* aActor) override;
  virtual bool RecvPGMPDecryptorConstructor(PGMPDecryptorChild* aActor) override;

  virtual PGMPAudioDecoderChild* AllocPGMPAudioDecoderChild() override;
  virtual bool DeallocPGMPAudioDecoderChild(PGMPAudioDecoderChild* aActor) override;
  virtual bool RecvPGMPAudioDecoderConstructor(PGMPAudioDecoderChild* aActor) override;

  virtual PGMPTimerChild* AllocPGMPTimerChild() override;
  virtual bool DeallocPGMPTimerChild(PGMPTimerChild* aActor) override;

  virtual PGMPStorageChild* AllocPGMPStorageChild() override;
  virtual bool DeallocPGMPStorageChild(PGMPStorageChild* aActor) override;

  virtual bool RecvCrashPluginNow() override;
  virtual bool RecvBeginAsyncShutdown() override;

  virtual void ActorDestroy(ActorDestroyReason aWhy) override;
  virtual void ProcessingError(Result aCode, const char* aReason) override;

  GMPErr GetAPI(const char* aAPIName, void* aHostAPI, void** aPluginAPI);

  GMPAsyncShutdown* mAsyncShutdown;
  nsRefPtr<GMPTimerChild> mTimerChild;
  nsRefPtr<GMPStorageChild> mStorage;

  MessageLoop* mGMPMessageLoop;
  std::string mPluginPath;
  std::string mVoucherPath;
#if defined(XP_MACOSX) && defined(MOZ_GMP_SANDBOX)
  nsCString mPluginBinaryPath;
#endif
  std::string mNodeId;
  GMPLoader* mGMPLoader;
  nsTArray<uint8_t> mPluginVoucher;
  nsTArray<uint8_t> mSandboxVoucher;
};

} // namespace gmp
} // namespace mozilla

#endif // GMPChild_h_
