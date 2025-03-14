/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DecoderDoctorDiagnostics.h"

#include "mozilla/dom/DecoderDoctorNotificationBinding.h"
#include "mozilla/Logging.h"
#include "mozilla/Preferences.h"
#include "nsContentUtils.h"
#include "nsGkAtoms.h"
#include "nsIDocument.h"
#include "nsIObserverService.h"
#include "nsIScriptError.h"
#include "nsITimer.h"
#include "nsIWeakReference.h"
#include "nsPluginHost.h"

static mozilla::LazyLogModule sDecoderDoctorLog("DecoderDoctor");
#define DD_LOG(level, arg, ...) MOZ_LOG(sDecoderDoctorLog, level, (arg, ##__VA_ARGS__))
#define DD_DEBUG(arg, ...) DD_LOG(mozilla::LogLevel::Debug, arg, ##__VA_ARGS__)
#define DD_INFO(arg, ...) DD_LOG(mozilla::LogLevel::Info, arg, ##__VA_ARGS__)
#define DD_WARN(arg, ...) DD_LOG(mozilla::LogLevel::Warning, arg, ##__VA_ARGS__)

namespace mozilla {

// Class that collects a sequence of diagnostics from the same document over a
// small period of time, in order to provide a synthesized analysis.
//
// Referenced by the document through a nsINode property, mTimer, and
// inter-task captures.
// When notified that the document is dead, or when the timer expires but
// nothing new happened, StopWatching() will remove the document property and
// timer (if present), so no more work will happen and the watcher will be
// destroyed once all references are gone.
class DecoderDoctorDocumentWatcher : public nsITimerCallback
{
public:
  static already_AddRefed<DecoderDoctorDocumentWatcher>
  RetrieveOrCreate(nsIDocument* aDocument);

  NS_DECL_ISUPPORTS
  NS_DECL_NSITIMERCALLBACK

  void AddDiagnostics(DecoderDoctorDiagnostics&& aDiagnostics,
                      const char* aCallSite);

private:
  explicit DecoderDoctorDocumentWatcher(nsIDocument* aDocument);
  virtual ~DecoderDoctorDocumentWatcher();

  // This will prevent further work from happening, watcher will deregister
  // itself from document (if requested) and cancel any timer, and soon die.
  void StopWatching(bool aRemoveProperty);

  // Remove property from document; will call DestroyPropertyCallback.
  void RemovePropertyFromDocument();
  // Callback for property destructor, will be automatically called when the
  // document (in aObject) is being destroyed.
  static void DestroyPropertyCallback(void* aObject,
                                      nsIAtom* aPropertyName,
                                      void* aPropertyValue,
                                      void* aData);

  static const uint32_t sAnalysisPeriod_ms = 1000;
  void EnsureTimerIsStarted();

  void ReportAnalysis(dom::DecoderDoctorNotificationType aNotificationType,
                      const char* aReportStringId,
                      const nsAString& aFormats);

  void SynthesizeAnalysis();

  // Raw pointer to an nsIDocument.
  // Must be non-null during construction.
  // Nulled when we want to stop watching, because either:
  // 1. The document has been destroyed (notified through
  //    DestroyPropertyCallback).
  // 2. We have not received new diagnostic information within a short time
  //    period, so we just stop watching.
  // Once nulled, no more actual work will happen, and the watcher will be
  // destroyed soon.
  nsIDocument* mDocument;

  struct Diagnostics
  {
    Diagnostics(DecoderDoctorDiagnostics&& aDiagnostics,
                const char* aCallSite)
      : mDecoderDoctorDiagnostics(Move(aDiagnostics))
      , mCallSite(aCallSite)
    {}
    Diagnostics(const Diagnostics&) = delete;
    Diagnostics(Diagnostics&& aOther)
      : mDecoderDoctorDiagnostics(Move(aOther.mDecoderDoctorDiagnostics))
      , mCallSite(Move(aOther.mCallSite))
    {}

    const DecoderDoctorDiagnostics mDecoderDoctorDiagnostics;
    const nsCString mCallSite;
  };
  typedef nsTArray<Diagnostics> DiagnosticsSequence;
  DiagnosticsSequence mDiagnosticsSequence;

  nsCOMPtr<nsITimer> mTimer; // Keep timer alive until we run.
  DiagnosticsSequence::size_type mDiagnosticsHandled = 0;
};


NS_IMPL_ISUPPORTS(DecoderDoctorDocumentWatcher, nsITimerCallback)

// static
already_AddRefed<DecoderDoctorDocumentWatcher>
DecoderDoctorDocumentWatcher::RetrieveOrCreate(nsIDocument* aDocument)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aDocument);
  RefPtr<DecoderDoctorDocumentWatcher> watcher =
    static_cast<DecoderDoctorDocumentWatcher*>(
      aDocument->GetProperty(nsGkAtoms::decoderDoctor));
  if (!watcher) {
    watcher = new DecoderDoctorDocumentWatcher(aDocument);
    if (NS_WARN_IF(NS_FAILED(
          aDocument->SetProperty(nsGkAtoms::decoderDoctor,
                                 watcher.get(),
                                 DestroyPropertyCallback,
                                 /*transfer*/ false)))) {
      DD_WARN("DecoderDoctorDocumentWatcher::RetrieveOrCreate(doc=%p) - Could not set property in document, will destroy new watcher[%p]",
              aDocument, watcher.get());
      return nullptr;
    }
    // Document owns watcher through this property.
    // Released in DestroyPropertyCallback().
    NS_ADDREF(watcher.get());
  }
  return watcher.forget();
}

DecoderDoctorDocumentWatcher::DecoderDoctorDocumentWatcher(nsIDocument* aDocument)
  : mDocument(aDocument)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mDocument);
  DD_DEBUG("DecoderDoctorDocumentWatcher[%p]::DecoderDoctorDocumentWatcher(doc=%p)",
           this, mDocument);
}

DecoderDoctorDocumentWatcher::~DecoderDoctorDocumentWatcher()
{
  MOZ_ASSERT(NS_IsMainThread());
  DD_DEBUG("DecoderDoctorDocumentWatcher[%p, doc=%p <- expect 0]::~DecoderDoctorDocumentWatcher()",
           this, mDocument);
  // mDocument should have been reset through StopWatching()!
  MOZ_ASSERT(!mDocument);
}

void
DecoderDoctorDocumentWatcher::RemovePropertyFromDocument()
{
  MOZ_ASSERT(NS_IsMainThread());
  DecoderDoctorDocumentWatcher* watcher =
    static_cast<DecoderDoctorDocumentWatcher*>(
      mDocument->GetProperty(nsGkAtoms::decoderDoctor));
  if (!watcher) {
    return;
  }
  DD_DEBUG("DecoderDoctorDocumentWatcher[%p, doc=%p]::RemovePropertyFromDocument()\n",
           watcher, watcher->mDocument);
  // This will remove the property and call our DestroyPropertyCallback.
  mDocument->DeleteProperty(nsGkAtoms::decoderDoctor);
}

// Callback for property destructors. |aObject| is the object
// the property is being removed for, |aPropertyName| is the property
// being removed, |aPropertyValue| is the value of the property, and |aData|
// is the opaque destructor data that was passed to SetProperty().
// static
void
DecoderDoctorDocumentWatcher::DestroyPropertyCallback(void* aObject,
                                                      nsIAtom* aPropertyName,
                                                      void* aPropertyValue,
                                                      void*)
{
  MOZ_ASSERT(NS_IsMainThread());
#ifdef DEBUG
  nsIDocument* document = static_cast<nsIDocument*>(aObject);
#endif
  MOZ_ASSERT(aPropertyName == nsGkAtoms::decoderDoctor);
  DecoderDoctorDocumentWatcher* watcher =
    static_cast<DecoderDoctorDocumentWatcher*>(aPropertyValue);
  MOZ_ASSERT(watcher);
  MOZ_ASSERT(watcher->mDocument == document);
  DD_DEBUG("DecoderDoctorDocumentWatcher[%p, doc=%p]::DestroyPropertyCallback()\n",
           watcher, watcher->mDocument);
  // 'false': StopWatching should not try and remove the property.
  watcher->StopWatching(false);
  NS_RELEASE(watcher);
}

void
DecoderDoctorDocumentWatcher::StopWatching(bool aRemoveProperty)
{
  MOZ_ASSERT(NS_IsMainThread());
  // StopWatching() shouldn't be called twice.
  MOZ_ASSERT(mDocument);

  if (aRemoveProperty) {
    RemovePropertyFromDocument();
  }

  // Forget document now, this will prevent more work from being started.
  mDocument = nullptr;

  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }
}

void
DecoderDoctorDocumentWatcher::EnsureTimerIsStarted()
{
  MOZ_ASSERT(NS_IsMainThread());

  if (!mTimer) {
    mTimer = do_CreateInstance(NS_TIMER_CONTRACTID);
    if (NS_WARN_IF(!mTimer)) {
      return;
    }
    if (NS_WARN_IF(NS_FAILED(
          mTimer->InitWithCallback(
            this, sAnalysisPeriod_ms, nsITimer::TYPE_ONE_SHOT)))) {
      mTimer = nullptr;
    }
  }
}

static void
DispatchNotification(nsISupports* aSubject,
                     dom::DecoderDoctorNotificationType aNotificationType,
                     const nsAString& aFormats)
{
  if (!aSubject) {
    return;
  }
  dom::DecoderDoctorNotification data;
  data.mType = aNotificationType;
  if (!aFormats.IsEmpty()) {
    data.mFormats.Construct(aFormats);
  }
  nsAutoString json;
  data.ToJSON(json);
  if (json.IsEmpty()) {
    DD_WARN("DecoderDoctorDiagnostics/DispatchEvent() - Could not create json for dispatch");
    // No point in dispatching this notification without data, the front-end
    // wouldn't know what to display.
    return;
  }
  DD_DEBUG("DecoderDoctorDiagnostics/DispatchEvent() %s", NS_ConvertUTF16toUTF8(json).get());
  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(aSubject, "decoder-doctor-notification", json.get());
  }
}

void
DecoderDoctorDocumentWatcher::ReportAnalysis(
  dom::DecoderDoctorNotificationType aNotificationType,
  const char* aReportStringId,
  const nsAString& aParams)
{
  MOZ_ASSERT(NS_IsMainThread());

  if (!mDocument) {
    return;
  }

  // 'params' will only be forwarded for non-empty strings.
  const char16_t* params[1] = { aParams.Data() };
  DD_DEBUG("DecoderDoctorDocumentWatcher[%p, doc=%p]::ReportAnalysis() ReportToConsole - aMsg='%s' params[0]='%s'",
           this, mDocument, aReportStringId,
           aParams.IsEmpty() ? "<no params>" : NS_ConvertUTF16toUTF8(params[0]).get());
  nsContentUtils::ReportToConsole(nsIScriptError::warningFlag,
                                  NS_LITERAL_CSTRING("Media"),
                                  mDocument,
                                  nsContentUtils::eDOM_PROPERTIES,
                                  aReportStringId,
                                  aParams.IsEmpty() ? nullptr : params,
                                  aParams.IsEmpty() ? 0 : 1);

  // "media.decoder-doctor.notifications-allowed" controls which notifications
  // may be dispatched to the front-end. It either contains:
  // - '*' -> Allow everything.
  // - Comma-separater list of ids -> Allow if aReportStringId (from
  //                                  dom.properties) is one of them.
  // - Nothing (missing or empty) -> Disable everything.
  nsAdoptingCString filter =
    Preferences::GetCString("media.decoder-doctor.notifications-allowed");
  filter.StripWhitespace();
  bool allowed = false;
  if (!filter || filter.IsEmpty()) {
    // Allow nothing.
  } else if (filter.EqualsLiteral("*")) {
    allowed = true;
  } else for (uint32_t start = 0; start < filter.Length(); ) {
    int32_t comma = filter.FindChar(',', start);
    uint32_t end = (comma >= 0) ? uint32_t(comma) : filter.Length();
    if (strncmp(aReportStringId, filter.Data() + start, end - start) == 0) {
      allowed = true;
      break;
    }
    // Skip comma. End of line will be caught in for 'while' clause.
    start = end + 1;
  }
  if (allowed) {
    DispatchNotification(
      mDocument->GetInnerWindow(), aNotificationType, aParams);
  }
}

enum SilverlightPresence {
  eNoSilverlight,
  eSilverlightDisabled,
  eSilverlightEnabled
};
static SilverlightPresence
CheckSilverlight()
{
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<nsPluginHost> pluginHost = nsPluginHost::GetInst();
  if (!pluginHost) {
    return eNoSilverlight;
  }
  nsTArray<nsCOMPtr<nsIInternalPluginTag>> plugins;
  pluginHost->GetPlugins(plugins, /*aIncludeDisabled*/ true);
  for (const auto& plugin : plugins) {
    for (const auto& mime : plugin->MimeTypes()) {
      if (mime.LowerCaseEqualsLiteral("application/x-silverlight")
          || mime.LowerCaseEqualsLiteral("application/x-silverlight-2")) {
        return plugin->IsEnabled() ? eSilverlightEnabled : eSilverlightDisabled;
      }
    }
  }

  return eNoSilverlight;
}

static void AppendToStringList(nsAString& list, const nsAString& item)
{
  if (!list.IsEmpty()) {
    list += NS_LITERAL_STRING(", ");
  }
  list += item;
}

void
DecoderDoctorDocumentWatcher::SynthesizeAnalysis()
{
  MOZ_ASSERT(NS_IsMainThread());

  nsAutoString playableFormats;
  nsAutoString unplayableFormats;
  // Subsets of unplayableFormats that require a specific platform decoder:
#if defined(XP_WIN)
  nsAutoString formatsRequiringWMF;
#endif
#if defined(MOZ_FFMPEG)
  nsAutoString formatsRequiringFFMpeg;
#endif
  nsAutoString supportedKeySystems;
  nsAutoString unsupportedKeySystems;
  DecoderDoctorDiagnostics::KeySystemIssue lastKeySystemIssue =
    DecoderDoctorDiagnostics::eUnset;

  for (const auto& diag : mDiagnosticsSequence) {
    switch (diag.mDecoderDoctorDiagnostics.Type()) {
      case DecoderDoctorDiagnostics::eFormatSupportCheck:
        if (diag.mDecoderDoctorDiagnostics.CanPlay()) {
          AppendToStringList(playableFormats,
                             diag.mDecoderDoctorDiagnostics.Format());
        } else {
          AppendToStringList(unplayableFormats,
                             diag.mDecoderDoctorDiagnostics.Format());
#if defined(XP_WIN)
          if (diag.mDecoderDoctorDiagnostics.DidWMFFailToLoad()) {
            AppendToStringList(formatsRequiringWMF,
                               diag.mDecoderDoctorDiagnostics.Format());
          }
#endif
#if defined(MOZ_FFMPEG)
          if (diag.mDecoderDoctorDiagnostics.DidFFmpegFailToLoad()) {
            AppendToStringList(formatsRequiringFFMpeg,
                               diag.mDecoderDoctorDiagnostics.Format());
          }
#endif
        }
        break;
      case DecoderDoctorDiagnostics::eMediaKeySystemAccessRequest:
        if (diag.mDecoderDoctorDiagnostics.IsKeySystemSupported()) {
          AppendToStringList(supportedKeySystems,
                             diag.mDecoderDoctorDiagnostics.KeySystem());
        } else {
          AppendToStringList(unsupportedKeySystems,
                             diag.mDecoderDoctorDiagnostics.KeySystem());
          DecoderDoctorDiagnostics::KeySystemIssue issue =
            diag.mDecoderDoctorDiagnostics.GetKeySystemIssue();
          if (issue != DecoderDoctorDiagnostics::eUnset) {
            lastKeySystemIssue = issue;
          }
        }
        break;
      default:
        MOZ_ASSERT(diag.mDecoderDoctorDiagnostics.Type()
                     == DecoderDoctorDiagnostics::eFormatSupportCheck
                   || diag.mDecoderDoctorDiagnostics.Type()
                        == DecoderDoctorDiagnostics::eMediaKeySystemAccessRequest);
        break;
    }
  }

  // Look at Key System issues first, as they take precedence over format checks.
  if (!unsupportedKeySystems.IsEmpty() && supportedKeySystems.IsEmpty()) {
    // No supported key systems!
    switch (lastKeySystemIssue) {
      case DecoderDoctorDiagnostics::eWidevineWithNoWMF:
        if (CheckSilverlight() != eSilverlightEnabled) {
          DD_DEBUG("DecoderDoctorDocumentWatcher[%p, doc=%p]::SynthesizeAnalysis() - unsupported key systems: %s, widevine without WMF nor Silverlight",
                   this, mDocument, NS_ConvertUTF16toUTF8(unsupportedKeySystems).get());
          ReportAnalysis(dom::DecoderDoctorNotificationType::Platform_decoder_not_found,
                         "MediaWidevineNoWMFNoSilverlight", NS_LITERAL_STRING(""));
          return;
        }
        break;
      default:
        break;
    }
  }

  // Next, check playability of requested formats.
  if (!unplayableFormats.IsEmpty()) {
    // Some requested formats cannot be played.
    if (playableFormats.IsEmpty()) {
      // No requested formats can be played. See if we can help the user, by
      // going through expected decoders from most to least desirable.
#if defined(XP_WIN)
      if (!formatsRequiringWMF.IsEmpty()) {
        DD_DEBUG("DecoderDoctorDocumentWatcher[%p, doc=%p]::SynthesizeAnalysis() - unplayable formats: %s -> Cannot play media because WMF was not found",
                 this, mDocument, NS_ConvertUTF16toUTF8(formatsRequiringWMF).get());
        ReportAnalysis(dom::DecoderDoctorNotificationType::Platform_decoder_not_found,
                       "MediaWMFNeeded", formatsRequiringWMF);
        return;
      }
#endif
#if defined(MOZ_FFMPEG)
      if (!formatsRequiringFFMpeg.IsEmpty()) {
        DD_DEBUG("DecoderDoctorDocumentWatcher[%p, doc=%p]::SynthesizeAnalysis() - unplayable formats: %s -> Cannot play media because platform decoder was not found",
                 this, mDocument, NS_ConvertUTF16toUTF8(formatsRequiringFFMpeg).get());
        ReportAnalysis(dom::DecoderDoctorNotificationType::Platform_decoder_not_found,
                       "MediaPlatformDecoderNotFound", formatsRequiringFFMpeg);
        return;
      }
#endif
      DD_WARN("DecoderDoctorDocumentWatcher[%p, doc=%p]::SynthesizeAnalysis() - Cannot play media, unplayable formats: %s",
              this, mDocument, NS_ConvertUTF16toUTF8(unplayableFormats).get());
      ReportAnalysis(dom::DecoderDoctorNotificationType::Cannot_play,
                     "MediaCannotPlayNoDecoders", unplayableFormats);
      return;
    }

    DD_INFO("DecoderDoctorDocumentWatcher[%p, doc=%p]::SynthesizeAnalysis() - Can play media, but no decoders for some requested formats: %s",
            this, mDocument, NS_ConvertUTF16toUTF8(unplayableFormats).get());
    if (Preferences::GetBool("media.decoder-doctor.verbose", false)) {
      ReportAnalysis(
        dom::DecoderDoctorNotificationType::Can_play_but_some_missing_decoders,
        "MediaNoDecoders", unplayableFormats);
    }
    return;
  }
  DD_DEBUG("DecoderDoctorDocumentWatcher[%p, doc=%p]::SynthesizeAnalysis() - Can play media, decoders available for all requested formats",
           this, mDocument);
}

void
DecoderDoctorDocumentWatcher::AddDiagnostics(DecoderDoctorDiagnostics&& aDiagnostics,
                                             const char* aCallSite)
{
  MOZ_ASSERT(NS_IsMainThread());

  if (!mDocument) {
    return;
  }

  DD_DEBUG("DecoderDoctorDocumentWatcher[%p, doc=%p]::AddDiagnostics(DecoderDoctorDiagnostics{%s}, call site '%s')",
           this, mDocument, aDiagnostics.GetDescription().Data(), aCallSite);
  mDiagnosticsSequence.AppendElement(
    Diagnostics(Move(aDiagnostics), aCallSite));
  EnsureTimerIsStarted();
}

NS_IMETHODIMP
DecoderDoctorDocumentWatcher::Notify(nsITimer* timer)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(timer == mTimer);

  // Forget timer. (Assuming timer keeps itself and us alive during this call.)
  mTimer = nullptr;

  if (!mDocument) {
    return NS_OK;
  }

  if (mDiagnosticsSequence.Length() > mDiagnosticsHandled) {
    // We have new diagnostic data.
    mDiagnosticsHandled = mDiagnosticsSequence.Length();

    SynthesizeAnalysis();

    // Restart timer, to redo analysis or stop watching this document,
    // depending on whether anything new happens.
    EnsureTimerIsStarted();
  } else {
    DD_DEBUG("DecoderDoctorDocumentWatcher[%p, doc=%p]::Notify() - No new diagnostics to analyze -> Stop watching",
             this, mDocument);
    // Stop watching this document, we don't expect more diagnostics for now.
    // If more diagnostics come in, we'll treat them as another burst, separately.
    // 'true' to remove the property from the document.
    StopWatching(true);
  }

  return NS_OK;
}


void
DecoderDoctorDiagnostics::StoreFormatDiagnostics(nsIDocument* aDocument,
                                                 const nsAString& aFormat,
                                                 bool aCanPlay,
                                                 const char* aCallSite)
{
  MOZ_ASSERT(NS_IsMainThread());
  // Make sure we only store once.
  MOZ_ASSERT(mDiagnosticsType == eUnsaved);
  mDiagnosticsType = eFormatSupportCheck;

  if (NS_WARN_IF(!aDocument)) {
    DD_WARN("DecoderDoctorDiagnostics[%p]::StoreFormatDiagnostics(nsIDocument* aDocument=nullptr, format='%s', can-play=%d, call site '%s')",
            this, NS_ConvertUTF16toUTF8(aFormat).get(), aCanPlay, aCallSite);
    return;
  }
  if (NS_WARN_IF(aFormat.IsEmpty())) {
    DD_WARN("DecoderDoctorDiagnostics[%p]::StoreFormatDiagnostics(nsIDocument* aDocument=%p, format=<empty>, can-play=%d, call site '%s')",
            this, aDocument, aCanPlay, aCallSite);
    return;
  }

  RefPtr<DecoderDoctorDocumentWatcher> watcher =
    DecoderDoctorDocumentWatcher::RetrieveOrCreate(aDocument);

  if (NS_WARN_IF(!watcher)) {
    DD_WARN("DecoderDoctorDiagnostics[%p]::StoreFormatDiagnostics(nsIDocument* aDocument=%p, format='%s', can-play=%d, call site '%s') - Could not create document watcher",
            this, aDocument, NS_ConvertUTF16toUTF8(aFormat).get(), aCanPlay, aCallSite);
    return;
  }

  mFormat = aFormat;
  mCanPlay = aCanPlay;

  // StoreDiagnostics should only be called once, after all data is available,
  // so it is safe to Move() from this object.
  watcher->AddDiagnostics(Move(*this), aCallSite);
  // Even though it's moved-from, the type should stay set
  // (Only used to ensure that we do store only once.)
  MOZ_ASSERT(mDiagnosticsType == eFormatSupportCheck);
}

void
DecoderDoctorDiagnostics::StoreMediaKeySystemAccess(nsIDocument* aDocument,
                                                    const nsAString& aKeySystem,
                                                    bool aIsSupported,
                                                    const char* aCallSite)
{
  MOZ_ASSERT(NS_IsMainThread());
  // Make sure we only store once.
  MOZ_ASSERT(mDiagnosticsType == eUnsaved);
  mDiagnosticsType = eMediaKeySystemAccessRequest;

  if (NS_WARN_IF(!aDocument)) {
    DD_WARN("DecoderDoctorDiagnostics[%p]::StoreMediaKeySystemAccess(nsIDocument* aDocument=nullptr, keysystem='%s', supported=%d, call site '%s')",
            this, NS_ConvertUTF16toUTF8(aKeySystem).get(), aIsSupported, aCallSite);
    return;
  }
  if (NS_WARN_IF(aKeySystem.IsEmpty())) {
    DD_WARN("DecoderDoctorDiagnostics[%p]::StoreMediaKeySystemAccess(nsIDocument* aDocument=%p, keysystem=<empty>, supported=%d, call site '%s')",
            this, aDocument, aIsSupported, aCallSite);
    return;
  }

  RefPtr<DecoderDoctorDocumentWatcher> watcher =
    DecoderDoctorDocumentWatcher::RetrieveOrCreate(aDocument);

  if (NS_WARN_IF(!watcher)) {
    DD_WARN("DecoderDoctorDiagnostics[%p]::StoreMediaKeySystemAccess(nsIDocument* aDocument=%p, keysystem='%s', supported=%d, call site '%s') - Could not create document watcher",
            this, NS_ConvertUTF16toUTF8(aKeySystem).get(), aIsSupported, aCallSite);
    return;
  }

  mKeySystem = aKeySystem;
  mIsKeySystemSupported = aIsSupported;

  // StoreDiagnostics should only be called once, after all data is available,
  // so it is safe to Move() from this object.
  watcher->AddDiagnostics(Move(*this), aCallSite);
  // Even though it's moved-from, the type should stay set
  // (Only used to ensure that we do store only once.)
  MOZ_ASSERT(mDiagnosticsType == eMediaKeySystemAccessRequest);
}

nsCString
DecoderDoctorDiagnostics::GetDescription() const
{
  MOZ_ASSERT(mDiagnosticsType == eFormatSupportCheck
             || mDiagnosticsType == eMediaKeySystemAccessRequest);
  nsCString s;
  switch (mDiagnosticsType) {
    case eUnsaved:
      s = "Unsaved diagnostics, cannot get accurate description";
      break;
    case eFormatSupportCheck:
      s = "format='";
      s += NS_ConvertUTF16toUTF8(mFormat).get();
      s += mCanPlay ? "', can play" : "', cannot play";
      if (mWMFFailedToLoad) {
        s += ", Windows platform decoder failed to load";
      }
      if (mFFmpegFailedToLoad) {
        s += ", Linux platform decoder failed to load";
      }
      if (mGMPPDMFailedToStartup) {
        s += ", GMP PDM failed to startup";
      } else if (!mGMP.IsEmpty()) {
        s += ", Using GMP '";
        s += mGMP;
        s += "'";
      }
      break;
    case eMediaKeySystemAccessRequest:
      s = "key system='";
      s += NS_ConvertUTF16toUTF8(mKeySystem).get();
      s += mIsKeySystemSupported ? "', supported" : "', not supported";
      switch (mKeySystemIssue) {
        case eUnset:
          break;
        case eWidevineWithNoWMF:
          s += ", Widevine with no WMF";
          break;
      }
      break;
    default:
      s = "?";
      break;
  }
  return s;
}

} // namespace mozilla
