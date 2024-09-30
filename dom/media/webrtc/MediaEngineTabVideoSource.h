/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsIDOMEventListener.h"
#include "MediaEngine.h"
#include "ImageContainer.h"
#include "nsITimer.h"
#include "mozilla/Monitor.h"
#include "mozilla/UniquePtr.h"
#include "nsITabSource.h"

namespace mozilla {

class MediaEngineTabVideoSource : public MediaEngineVideoSource, nsIDOMEventListener, nsITimerCallback {
  public:
    NS_DECL_THREADSAFE_ISUPPORTS
    NS_DECL_NSIDOMEVENTLISTENER
    NS_DECL_NSITIMERCALLBACK
    MediaEngineTabVideoSource();

    void Shutdown() override {};
    void GetName(nsAString_internal&) override;
    void GetUUID(nsACString_internal&) override;
    nsresult Allocate(const dom::MediaTrackConstraints &,
                      const mozilla::MediaEnginePrefs&,
                      const nsString& aDeviceId,
                      const nsACString& aOrigin) override;
    nsresult Deallocate() override;
    nsresult Start(mozilla::SourceMediaStream*, mozilla::TrackID, const mozilla::PrincipalHandle&) override;
    void SetDirectListeners(bool aHasDirectListeners) override {};
    void NotifyPull(mozilla::MediaStreamGraph*, mozilla::SourceMediaStream*, mozilla::TrackID, mozilla::StreamTime, const mozilla::PrincipalHandle& aPrincipalHandle) override;
    nsresult Stop(mozilla::SourceMediaStream*, mozilla::TrackID) override;
    nsresult Restart(const dom::MediaTrackConstraints& aConstraints,
                     const mozilla::MediaEnginePrefs& aPrefs,
                     const nsString& aDeviceId) override;
    bool IsFake() override;
    dom::MediaSourceEnum GetMediaSource() const override {
      return dom::MediaSourceEnum::Browser;
    }
    uint32_t GetBestFitnessDistance(
      const nsTArray<const dom::MediaTrackConstraintSet*>& aConstraintSets,
      const nsString& aDeviceId) override
    {
      return 0;
    }

    nsresult TakePhoto(MediaEnginePhotoCallback* aCallback) override
    {
      return NS_ERROR_NOT_IMPLEMENTED;
    }

    void Draw();

    class StartRunnable : public Runnable {
    public:
      explicit StartRunnable(MediaEngineTabVideoSource *videoSource) : mVideoSource(videoSource) {}
      NS_IMETHOD Run();
      RefPtr<MediaEngineTabVideoSource> mVideoSource;
    };

    class StopRunnable : public Runnable {
    public:
      explicit StopRunnable(MediaEngineTabVideoSource *videoSource) : mVideoSource(videoSource) {}
      NS_IMETHOD Run();
      RefPtr<MediaEngineTabVideoSource> mVideoSource;
    };

    class InitRunnable : public Runnable {
    public:
      explicit InitRunnable(MediaEngineTabVideoSource *videoSource) : mVideoSource(videoSource) {}
      NS_IMETHOD Run();
      RefPtr<MediaEngineTabVideoSource> mVideoSource;
    };

protected:
    ~MediaEngineTabVideoSource() {}

private:
    int32_t mBufWidthMax;
    int32_t mBufHeightMax;
    int64_t mWindowId;
    bool mScrollWithPage;
    int32_t mViewportOffsetX;
    int32_t mViewportOffsetY;
    int32_t mViewportWidth;
    int32_t mViewportHeight;
    int32_t mTimePerFrame;
    UniquePtr<unsigned char[]> mData;
    size_t mDataSize;
    nsCOMPtr<nsPIDOMWindowOuter> mWindow;
    // If this is set, we will run despite mWindow == nullptr.
    bool mBlackedoutWindow;
    RefPtr<layers::SourceSurfaceImage> mImage;
    nsCOMPtr<nsITimer> mTimer;
    Monitor mMonitor;
    nsCOMPtr<nsITabSource> mTabSource;
  };
}
