/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>

#include <fstream>

#include <prio.h>

#include "mozilla/dom/ToJSValue.h"
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Likely.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/unused.h"

#include "base/pickle.h"
#include "nsIComponentManager.h"
#include "nsIServiceManager.h"
#include "nsThreadManager.h"
#include "nsCOMArray.h"
#include "nsCOMPtr.h"
#include "nsXPCOMPrivate.h"
#include "nsIXULAppInfo.h"
#include "nsVersionComparator.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/ModuleUtils.h"
#include "nsIXPConnect.h"
#include "mozilla/Services.h"
#include "jsapi.h"
#include "jsfriendapi.h"
#include "js/GCAPI.h"
#include "nsString.h"
#include "nsITelemetry.h"
#include "nsIFile.h"
#include "nsIFileStreams.h"
#include "nsIMemoryReporter.h"
#include "nsISeekableStream.h"
#include "Telemetry.h"
#include "TelemetryCommon.h"
#include "TelemetryHistogram.h"
#include "WebrtcTelemetry.h"
#include "nsTHashtable.h"
#include "nsHashKeys.h"
#include "nsBaseHashtable.h"
#include "nsClassHashtable.h"
#include "nsXULAppAPI.h"
#include "nsReadableUtils.h"
#include "nsThreadUtils.h"
#if defined(XP_WIN)
#include "nsUnicharUtils.h"
#endif
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsJSUtils.h"
#include "nsReadableUtils.h"
#include "plstr.h"
#include "nsAppDirectoryServiceDefs.h"
#include "mozilla/BackgroundHangMonitor.h"
#include "mozilla/ThreadHangStats.h"
#include "mozilla/ProcessedStack.h"
#include "mozilla/Mutex.h"
#include "mozilla/FileUtils.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/IOInterposer.h"
#include "mozilla/PoisonIOInterposer.h"
#include "mozilla/StartupTimeline.h"
#include "mozilla/HangMonitor.h"
#if defined(MOZ_ENABLE_PROFILER_SPS)
#include "shared-libraries.h"
#endif

namespace {

using namespace mozilla;
using namespace mozilla::HangMonitor;

// The maximum number of chrome hangs stacks that we're keeping.
const size_t kMaxChromeStacksKept = 50;
// The maximum depth of a single chrome hang stack.
const size_t kMaxChromeStackDepth = 50;

// This class is conceptually a list of ProcessedStack objects, but it represents them
// more efficiently by keeping a single global list of modules.
class CombinedStacks {
public:
  CombinedStacks() : mNextIndex(0) {}
  typedef std::vector<Telemetry::ProcessedStack::Frame> Stack;
  const Telemetry::ProcessedStack::Module& GetModule(unsigned aIndex) const;
  size_t GetModuleCount() const;
  const Stack& GetStack(unsigned aIndex) const;
  size_t AddStack(const Telemetry::ProcessedStack& aStack);
  size_t GetStackCount() const;
  size_t SizeOfExcludingThis() const;
private:
  std::vector<Telemetry::ProcessedStack::Module> mModules;
  // A circular buffer to hold the stacks.
  std::vector<Stack> mStacks;
  // The index of the next buffer element to write to in mStacks.
  size_t mNextIndex;
};

static JSObject *
CreateJSStackObject(JSContext *cx, const CombinedStacks &stacks);

size_t
CombinedStacks::GetModuleCount() const {
  return mModules.size();
}

const Telemetry::ProcessedStack::Module&
CombinedStacks::GetModule(unsigned aIndex) const {
  return mModules[aIndex];
}

size_t
CombinedStacks::AddStack(const Telemetry::ProcessedStack& aStack) {
  // Advance the indices of the circular queue holding the stacks.
  size_t index = mNextIndex++ % kMaxChromeStacksKept;
  // Grow the vector up to the maximum size, if needed.
  if (mStacks.size() < kMaxChromeStacksKept) {
    mStacks.resize(mStacks.size() + 1);
  }
  // Get a reference to the location holding the new stack.
  CombinedStacks::Stack& adjustedStack = mStacks[index];
  // If we're using an old stack to hold aStack, clear it.
  adjustedStack.clear();

  size_t stackSize = aStack.GetStackSize();
  for (size_t i = 0; i < stackSize; ++i) {
    const Telemetry::ProcessedStack::Frame& frame = aStack.GetFrame(i);
    uint16_t modIndex;
    if (frame.mModIndex == std::numeric_limits<uint16_t>::max()) {
      modIndex = frame.mModIndex;
    } else {
      const Telemetry::ProcessedStack::Module& module =
        aStack.GetModule(frame.mModIndex);
      std::vector<Telemetry::ProcessedStack::Module>::iterator modIterator =
        std::find(mModules.begin(), mModules.end(), module);
      if (modIterator == mModules.end()) {
        mModules.push_back(module);
        modIndex = mModules.size() - 1;
      } else {
        modIndex = modIterator - mModules.begin();
      }
    }
    Telemetry::ProcessedStack::Frame adjustedFrame = { frame.mOffset, modIndex };
    adjustedStack.push_back(adjustedFrame);
  }
  return index;
}

const CombinedStacks::Stack&
CombinedStacks::GetStack(unsigned aIndex) const {
  return mStacks[aIndex];
}

size_t
CombinedStacks::GetStackCount() const {
  return mStacks.size();
}

size_t
CombinedStacks::SizeOfExcludingThis() const {
  // This is a crude approximation. We would like to do something like
  // aMallocSizeOf(&mModules[0]), but on linux aMallocSizeOf will call
  // malloc_usable_size which is only safe on the pointers returned by malloc.
  // While it works on current libstdc++, it is better to be safe and not assume
  // that &vec[0] points to one. We could use a custom allocator, but
  // it doesn't seem worth it.
  size_t n = 0;
  n += mModules.capacity() * sizeof(Telemetry::ProcessedStack::Module);
  n += mStacks.capacity() * sizeof(Stack);
  for (std::vector<Stack>::const_iterator i = mStacks.begin(),
         e = mStacks.end(); i != e; ++i) {
    const Stack& s = *i;
    n += s.capacity() * sizeof(Telemetry::ProcessedStack::Frame);
  }
  return n;
}

// This utility function generates a string key that is used to index the annotations
// in a hash map from |HangReports::AddHang|.
nsresult
ComputeAnnotationsKey(const HangAnnotationsPtr& aAnnotations, nsAString& aKeyOut)
{
  UniquePtr<HangAnnotations::Enumerator> annotationsEnum = aAnnotations->GetEnumerator();
  if (!annotationsEnum) {
    return NS_ERROR_FAILURE;
  }

  // Append all the attributes to the key, to uniquely identify this annotation.
  nsAutoString  key;
  nsAutoString  value;
  while (annotationsEnum->Next(key, value)) {
    aKeyOut.Append(key);
    aKeyOut.Append(value);
  }

  return NS_OK;
}

class HangReports {
public:
  /**
   * This struct encapsulates information for an individual ChromeHang annotation.
   * mHangIndex is the index of the corresponding ChromeHang.
   */
  struct AnnotationInfo {
    AnnotationInfo(uint32_t aHangIndex,
                   HangAnnotationsPtr aAnnotations)
      : mAnnotations(Move(aAnnotations))
    {
      mHangIndices.AppendElement(aHangIndex);
    }
    AnnotationInfo(AnnotationInfo&& aOther)
      : mHangIndices(aOther.mHangIndices)
      , mAnnotations(Move(aOther.mAnnotations))
    {}
    ~AnnotationInfo() {}
    AnnotationInfo& operator=(AnnotationInfo&& aOther)
    {
      mHangIndices = aOther.mHangIndices;
      mAnnotations = Move(aOther.mAnnotations);
      return *this;
    }
    // To save memory, a single AnnotationInfo can be associated to multiple chrome
    // hangs. The following array holds the index of each related chrome hang.
    nsTArray<uint32_t> mHangIndices;
    HangAnnotationsPtr mAnnotations;

  private:
    // Force move constructor
    AnnotationInfo(const AnnotationInfo& aOther) = delete;
    void operator=(const AnnotationInfo& aOther) = delete;
  };
  size_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;
  void AddHang(const Telemetry::ProcessedStack& aStack, uint32_t aDuration,
               int32_t aSystemUptime, int32_t aFirefoxUptime,
               HangAnnotationsPtr aAnnotations);
  void PruneStackReferences(const size_t aRemovedStackIndex);
  uint32_t GetDuration(unsigned aIndex) const;
  int32_t GetSystemUptime(unsigned aIndex) const;
  int32_t GetFirefoxUptime(unsigned aIndex) const;
  const nsClassHashtable<nsStringHashKey, AnnotationInfo>& GetAnnotationInfo() const;
  const CombinedStacks& GetStacks() const;
private:
  /**
   * This struct encapsulates the data for an individual ChromeHang, excluding
   * annotations.
   */
  struct HangInfo {
    // Hang duration (in seconds)
    uint32_t mDuration;
    // System uptime (in minutes) at the time of the hang
    int32_t mSystemUptime;
    // Firefox uptime (in minutes) at the time of the hang
    int32_t mFirefoxUptime;
  };
  std::vector<HangInfo> mHangInfo;
  nsClassHashtable<nsStringHashKey, AnnotationInfo> mAnnotationInfo;
  CombinedStacks mStacks;
};

void
HangReports::AddHang(const Telemetry::ProcessedStack& aStack,
                     uint32_t aDuration,
                     int32_t aSystemUptime,
                     int32_t aFirefoxUptime,
                     HangAnnotationsPtr aAnnotations) {
  // Append the new stack to the stack's circular queue.
  size_t hangIndex = mStacks.AddStack(aStack);
  // Append the hang info at the same index, in mHangInfo.
  HangInfo info = { aDuration, aSystemUptime, aFirefoxUptime };
  if (mHangInfo.size() < kMaxChromeStacksKept) {
    mHangInfo.push_back(info);
  } else {
    mHangInfo[hangIndex] = info;
    // Remove any reference to the stack overwritten in the circular queue
    // from the annotations.
    PruneStackReferences(hangIndex);
  }

  if (!aAnnotations) {
    return;
  }

  nsAutoString annotationsKey;
  // Generate a key to index aAnnotations in the hash map.
  nsresult rv = ComputeAnnotationsKey(aAnnotations, annotationsKey);
  if (NS_FAILED(rv)) {
    return;
  }

  AnnotationInfo* annotationsEntry = mAnnotationInfo.Get(annotationsKey);
  if (annotationsEntry) {
    // If the key is already in the hash map, append the index of the chrome hang
    // to its indices.
    annotationsEntry->mHangIndices.AppendElement(hangIndex);
    return;
  }

  // If the key was not found, add the annotations to the hash map.
  mAnnotationInfo.Put(annotationsKey, new AnnotationInfo(hangIndex, Move(aAnnotations)));
}

/**
 * This function removes links to discarded chrome hangs stacks and prunes unused
 * annotations.
 */
void
HangReports::PruneStackReferences(const size_t aRemovedStackIndex) {
  // We need to adjust the indices that link annotations to chrome hangs. Since we
  // removed a stack, we must remove all references to it and prune annotations
  // linked to no stacks.
  for (auto iter = mAnnotationInfo.Iter(); !iter.Done(); iter.Next()) {
    nsTArray<uint32_t>& stackIndices = iter.Data()->mHangIndices;
    size_t toRemove = stackIndices.NoIndex;
    for (size_t k = 0; k < stackIndices.Length(); k++) {
      // Is this index referencing the removed stack?
      if (stackIndices[k] == aRemovedStackIndex) {
        toRemove = k;
        break;
      }
    }

    // Remove the index referencing the old stack from the annotation.
    if (toRemove != stackIndices.NoIndex) {
      stackIndices.RemoveElementAt(toRemove);
    }

    // If this annotation no longer references any stack, drop it.
    if (!stackIndices.Length()) {
      iter.Remove();
    }
  }
}

size_t
HangReports::SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const {
  size_t n = 0;
  n += mStacks.SizeOfExcludingThis();
  // This is a crude approximation. See comment on
  // CombinedStacks::SizeOfExcludingThis.
  n += mHangInfo.capacity() * sizeof(HangInfo);
  n += mAnnotationInfo.ShallowSizeOfExcludingThis(aMallocSizeOf);
  n += mAnnotationInfo.Count() * sizeof(AnnotationInfo);
  for (auto iter = mAnnotationInfo.ConstIter(); !iter.Done(); iter.Next()) {
    n += iter.Key().SizeOfExcludingThisIfUnshared(aMallocSizeOf);
    n += iter.Data()->mAnnotations->SizeOfIncludingThis(aMallocSizeOf);
  }
  return n;
}

const CombinedStacks&
HangReports::GetStacks() const {
  return mStacks;
}

uint32_t
HangReports::GetDuration(unsigned aIndex) const {
  return mHangInfo[aIndex].mDuration;
}

int32_t
HangReports::GetSystemUptime(unsigned aIndex) const {
  return mHangInfo[aIndex].mSystemUptime;
}

int32_t
HangReports::GetFirefoxUptime(unsigned aIndex) const {
  return mHangInfo[aIndex].mFirefoxUptime;
}

const nsClassHashtable<nsStringHashKey, HangReports::AnnotationInfo>&
HangReports::GetAnnotationInfo() const {
  return mAnnotationInfo;
}

/**
 * IOInterposeObserver recording statistics of main-thread I/O during execution,
 * aimed at consumption by TelemetryImpl
 */
class TelemetryIOInterposeObserver : public IOInterposeObserver
{
  /** File-level statistics structure */
  struct FileStats {
    FileStats()
      : creates(0)
      , reads(0)
      , writes(0)
      , fsyncs(0)
      , stats(0)
      , totalTime(0)
    {}
    uint32_t  creates;      /** Number of create/open operations */
    uint32_t  reads;        /** Number of read operations */
    uint32_t  writes;       /** Number of write operations */
    uint32_t  fsyncs;       /** Number of fsync operations */
    uint32_t  stats;        /** Number of stat operations */
    double    totalTime;    /** Accumulated duration of all operations */
  };

  struct SafeDir {
    SafeDir(const nsAString& aPath, const nsAString& aSubstName)
      : mPath(aPath)
      , mSubstName(aSubstName)
    {}
    size_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const {
      return mPath.SizeOfExcludingThisIfUnshared(aMallocSizeOf) +
             mSubstName.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
    }
    nsString  mPath;        /** Path to the directory */
    nsString  mSubstName;   /** Name to substitute with */
  };

public:
  explicit TelemetryIOInterposeObserver(nsIFile* aXreDir);

  /**
   * An implementation of Observe that records statistics of all
   * file IO operations.
   */
  void Observe(Observation& aOb);

  /**
   * Reflect recorded file IO statistics into Javascript
   */
  bool ReflectIntoJS(JSContext *cx, JS::Handle<JSObject*> rootObj);

  /**
   * Adds a path for inclusion in main thread I/O report.
   * @param aPath Directory path
   * @param aSubstName Name to substitute for aPath for privacy reasons
   */
  void AddPath(const nsAString& aPath, const nsAString& aSubstName);

  /**
   * Get size of hash table with file stats
   */
  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const {
    return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
  }

  size_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const {
    size_t size = 0;
    size += mFileStats.ShallowSizeOfExcludingThis(aMallocSizeOf);
    for (auto iter = mFileStats.ConstIter(); !iter.Done(); iter.Next()) {
      size += iter.Get()->GetKey().SizeOfExcludingThisIfUnshared(aMallocSizeOf);
    }
    size += mSafeDirs.ShallowSizeOfExcludingThis(aMallocSizeOf);
    uint32_t safeDirsLen = mSafeDirs.Length();
    for (uint32_t i = 0; i < safeDirsLen; ++i) {
      size += mSafeDirs[i].SizeOfExcludingThis(aMallocSizeOf);
    }
    return size;
  }

private:
  enum Stage
  {
    STAGE_STARTUP = 0,
    STAGE_NORMAL,
    STAGE_SHUTDOWN,
    NUM_STAGES
  };
  static inline Stage NextStage(Stage aStage)
  {
    switch (aStage) {
      case STAGE_STARTUP:
        return STAGE_NORMAL;
      case STAGE_NORMAL:
        return STAGE_SHUTDOWN;
      case STAGE_SHUTDOWN:
        return STAGE_SHUTDOWN;
      default:
        return NUM_STAGES;
    }
  }

  struct FileStatsByStage
  {
    FileStats mStats[NUM_STAGES];
  };
  typedef nsBaseHashtableET<nsStringHashKey, FileStatsByStage> FileIOEntryType;

  // Statistics for each filename
  AutoHashtable<FileIOEntryType> mFileStats;
  // Container for whitelisted directories
  nsTArray<SafeDir> mSafeDirs;
  Stage             mCurStage;

  /**
   * Reflect a FileIOEntryType object to a Javascript property on obj with
   * filename as key containing array:
   * [totalTime, creates, reads, writes, fsyncs, stats]
   */
  static bool ReflectFileStats(FileIOEntryType* entry, JSContext *cx,
                               JS::Handle<JSObject*> obj);
};

TelemetryIOInterposeObserver::TelemetryIOInterposeObserver(nsIFile* aXreDir)
  : mCurStage(STAGE_STARTUP)
{
  nsAutoString xreDirPath;
  nsresult rv = aXreDir->GetPath(xreDirPath);
  if (NS_SUCCEEDED(rv)) {
    AddPath(xreDirPath, NS_LITERAL_STRING("{xre}"));
  }
}

void TelemetryIOInterposeObserver::AddPath(const nsAString& aPath,
                                           const nsAString& aSubstName)
{
  mSafeDirs.AppendElement(SafeDir(aPath, aSubstName));
}

// Threshold for reporting slow main-thread I/O (50 milliseconds).
const TimeDuration kTelemetryReportThreshold = TimeDuration::FromMilliseconds(50);

void TelemetryIOInterposeObserver::Observe(Observation& aOb)
{
  // We only report main-thread I/O
  if (!IsMainThread()) {
    return;
  }

  if (aOb.ObservedOperation() == OpNextStage) {
    mCurStage = NextStage(mCurStage);
    MOZ_ASSERT(mCurStage < NUM_STAGES);
    return;
  }

  if (aOb.Duration() < kTelemetryReportThreshold) {
    return;
  }

  // Get the filename
  const char16_t* filename = aOb.Filename();
 
  // Discard observations without filename
  if (!filename) {
    return;
  }

#if defined(XP_WIN)
  nsCaseInsensitiveStringComparator comparator;
#else
  nsDefaultStringComparator comparator;
#endif
  nsAutoString      processedName;
  nsDependentString filenameStr(filename);
  uint32_t safeDirsLen = mSafeDirs.Length();
  for (uint32_t i = 0; i < safeDirsLen; ++i) {
    if (StringBeginsWith(filenameStr, mSafeDirs[i].mPath, comparator)) {
      processedName = mSafeDirs[i].mSubstName;
      processedName += Substring(filenameStr, mSafeDirs[i].mPath.Length());
      break;
    }
  }

  if (processedName.IsEmpty()) {
    return;
  }

  // Create a new entry or retrieve the existing one
  FileIOEntryType* entry = mFileStats.PutEntry(processedName);
  if (entry) {
    FileStats& stats = entry->mData.mStats[mCurStage];
    // Update the statistics
    stats.totalTime += (double) aOb.Duration().ToMilliseconds();
    switch (aOb.ObservedOperation()) {
      case OpCreateOrOpen:
        stats.creates++;
        break;
      case OpRead:
        stats.reads++;
        break;
      case OpWrite:
        stats.writes++;
        break;
      case OpFSync:
        stats.fsyncs++;
        break;
      case OpStat:
        stats.stats++;
        break;
      default:
        break;
    }
  }
}

bool TelemetryIOInterposeObserver::ReflectFileStats(FileIOEntryType* entry,
                                                    JSContext *cx,
                                                    JS::Handle<JSObject*> obj)
{
  JS::AutoValueArray<NUM_STAGES> stages(cx);

  FileStatsByStage& statsByStage = entry->mData;
  for (int s = STAGE_STARTUP; s < NUM_STAGES; ++s) {
    FileStats& fileStats = statsByStage.mStats[s];

    if (fileStats.totalTime == 0 && fileStats.creates == 0 &&
        fileStats.reads == 0 && fileStats.writes == 0 &&
        fileStats.fsyncs == 0 && fileStats.stats == 0) {
      // Don't add an array that contains no information
      stages[s].setNull();
      continue;
    }

    // Array we want to report
    JS::AutoValueArray<6> stats(cx);
    stats[0].setNumber(fileStats.totalTime);
    stats[1].setNumber(fileStats.creates);
    stats[2].setNumber(fileStats.reads);
    stats[3].setNumber(fileStats.writes);
    stats[4].setNumber(fileStats.fsyncs);
    stats[5].setNumber(fileStats.stats);

    // Create jsStats as array of elements above
    JS::RootedObject jsStats(cx, JS_NewArrayObject(cx, stats));
    if (!jsStats) {
      continue;
    }

    stages[s].setObject(*jsStats);
  }

  JS::Rooted<JSObject*> jsEntry(cx, JS_NewArrayObject(cx, stages));
  if (!jsEntry) {
    return false;
  }

  // Add jsEntry to top-level dictionary
  const nsAString& key = entry->GetKey();
  return JS_DefineUCProperty(cx, obj, key.Data(), key.Length(),
                             jsEntry, JSPROP_ENUMERATE | JSPROP_READONLY);
}

bool TelemetryIOInterposeObserver::ReflectIntoJS(JSContext *cx,
                                                 JS::Handle<JSObject*> rootObj)
{
  return mFileStats.ReflectIntoJS(ReflectFileStats, cx, rootObj);
}

// This is not a member of TelemetryImpl because we want to record I/O during
// startup.
StaticAutoPtr<TelemetryIOInterposeObserver> sTelemetryIOObserver;

void
ClearIOReporting()
{
  if (!sTelemetryIOObserver) {
    return;
  }
  IOInterposer::Unregister(IOInterposeObserver::OpAllWithStaging,
                           sTelemetryIOObserver);
  sTelemetryIOObserver = nullptr;
}

class TelemetryImpl final
  : public nsITelemetry
  , public nsIMemoryReporter
{
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSITELEMETRY
  NS_DECL_NSIMEMORYREPORTER

public:
  void InitMemoryReporter();

  static already_AddRefed<nsITelemetry> CreateTelemetryInstance();
  static void ShutdownTelemetry();
  static void RecordSlowStatement(const nsACString &sql, const nsACString &dbName,
                                  uint32_t delay);
#if defined(MOZ_ENABLE_PROFILER_SPS)
  static void RecordChromeHang(uint32_t aDuration,
                               Telemetry::ProcessedStack &aStack,
                               int32_t aSystemUptime,
                               int32_t aFirefoxUptime,
                               HangAnnotationsPtr aAnnotations);
#endif
  static void RecordThreadHangStats(Telemetry::ThreadHangStats& aStats);
  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf);
  struct Stat {
    uint32_t hitCount;
    uint32_t totalTime;
  };
  struct StmtStats {
    struct Stat mainThread;
    struct Stat otherThreads;
  };
  typedef nsBaseHashtableET<nsCStringHashKey, StmtStats> SlowSQLEntryType;

  static void RecordIceCandidates(const uint32_t iceCandidateBitmask,
                                  const bool success,
                                  const bool loop);
private:
  TelemetryImpl();
  ~TelemetryImpl();

  static nsCString SanitizeSQL(const nsACString& sql);

  enum SanitizedState { Sanitized, Unsanitized };

  static void StoreSlowSQL(const nsACString &offender, uint32_t delay,
                           SanitizedState state);

  static bool ReflectMainThreadSQL(SlowSQLEntryType *entry, JSContext *cx,
                                   JS::Handle<JSObject*> obj);
  static bool ReflectOtherThreadsSQL(SlowSQLEntryType *entry, JSContext *cx,
                                     JS::Handle<JSObject*> obj);
  static bool ReflectSQL(const SlowSQLEntryType *entry, const Stat *stat,
                         JSContext *cx, JS::Handle<JSObject*> obj);

  bool AddSQLInfo(JSContext *cx, JS::Handle<JSObject*> rootObj, bool mainThread,
                  bool privateSQL);
  bool GetSQLStats(JSContext *cx, JS::MutableHandle<JS::Value> ret,
                   bool includePrivateSql);

  void ReadLateWritesStacks(nsIFile* aProfileDir);

  static TelemetryImpl *sTelemetry;
  AutoHashtable<SlowSQLEntryType> mPrivateSQL;
  AutoHashtable<SlowSQLEntryType> mSanitizedSQL;
  Mutex mHashMutex;
  HangReports mHangReports;
  Mutex mHangReportsMutex;
  // mThreadHangStats stores recorded, inactive thread hang stats
  Vector<Telemetry::ThreadHangStats> mThreadHangStats;
  Mutex mThreadHangStatsMutex;

  CombinedStacks mLateWritesStacks; // This is collected out of the main thread.
  bool mCachedTelemetryData;
  uint32_t mLastShutdownTime;
  uint32_t mFailedLockCount;
  nsCOMArray<nsIFetchTelemetryDataCallback> mCallbacks;
  friend class nsFetchTelemetryData;

  WebrtcTelemetry mWebrtcTelemetry;
};

TelemetryImpl*  TelemetryImpl::sTelemetry = nullptr;

MOZ_DEFINE_MALLOC_SIZE_OF(TelemetryMallocSizeOf)

NS_IMETHODIMP
TelemetryImpl::CollectReports(nsIHandleReportCallback* aHandleReport,
                              nsISupports* aData, bool aAnonymize)
{
  return MOZ_COLLECT_REPORT(
    "explicit/telemetry", KIND_HEAP, UNITS_BYTES,
    SizeOfIncludingThis(TelemetryMallocSizeOf),
    "Memory used by the telemetry system.");
}

void
InitHistogramRecordingEnabled()
{
  TelemetryHistogram::InitHistogramRecordingEnabled();
}

static uint32_t
ReadLastShutdownDuration(const char *filename) {
  FILE *f = fopen(filename, "r");
  if (!f) {
    return 0;
  }

  int shutdownTime;
  int r = fscanf(f, "%d\n", &shutdownTime);
  fclose(f);
  if (r != 1) {
    return 0;
  }

  return shutdownTime;
}

const int32_t kMaxFailedProfileLockFileSize = 10;

bool
GetFailedLockCount(nsIInputStream* inStream, uint32_t aCount,
                   unsigned int& result)
{
  nsAutoCString bufStr;
  nsresult rv;
  rv = NS_ReadInputStreamToString(inStream, bufStr, aCount);
  NS_ENSURE_SUCCESS(rv, false);
  result = bufStr.ToInteger(&rv);
  return NS_SUCCEEDED(rv) && result > 0;
}

nsresult
GetFailedProfileLockFile(nsIFile* *aFile, nsIFile* aProfileDir)
{
  NS_ENSURE_ARG_POINTER(aProfileDir);

  nsresult rv = aProfileDir->Clone(aFile);
  NS_ENSURE_SUCCESS(rv, rv);

  (*aFile)->AppendNative(NS_LITERAL_CSTRING("Telemetry.FailedProfileLocks.txt"));
  return NS_OK;
}

class nsFetchTelemetryData : public Runnable
{
public:
  nsFetchTelemetryData(const char* aShutdownTimeFilename,
                       nsIFile* aFailedProfileLockFile,
                       nsIFile* aProfileDir)
    : mShutdownTimeFilename(aShutdownTimeFilename),
      mFailedProfileLockFile(aFailedProfileLockFile),
      mTelemetry(TelemetryImpl::sTelemetry),
      mProfileDir(aProfileDir)
  {
  }

private:
  const char* mShutdownTimeFilename;
  nsCOMPtr<nsIFile> mFailedProfileLockFile;
  RefPtr<TelemetryImpl> mTelemetry;
  nsCOMPtr<nsIFile> mProfileDir;

public:
  void MainThread() {
    mTelemetry->mCachedTelemetryData = true;
    for (unsigned int i = 0, n = mTelemetry->mCallbacks.Count(); i < n; ++i) {
      mTelemetry->mCallbacks[i]->Complete();
    }
    mTelemetry->mCallbacks.Clear();
  }

  NS_IMETHOD Run() {
    LoadFailedLockCount(mTelemetry->mFailedLockCount);
    mTelemetry->mLastShutdownTime = 
      ReadLastShutdownDuration(mShutdownTimeFilename);
    mTelemetry->ReadLateWritesStacks(mProfileDir);
    nsCOMPtr<nsIRunnable> e =
      NewRunnableMethod(this, &nsFetchTelemetryData::MainThread);
    NS_ENSURE_STATE(e);
    NS_DispatchToMainThread(e);
    return NS_OK;
  }

private:
  nsresult
  LoadFailedLockCount(uint32_t& failedLockCount)
  {
    failedLockCount = 0;
    int64_t fileSize = 0;
    nsresult rv = mFailedProfileLockFile->GetFileSize(&fileSize);
    if (NS_FAILED(rv)) {
      return rv;
    }
    NS_ENSURE_TRUE(fileSize <= kMaxFailedProfileLockFileSize,
                   NS_ERROR_UNEXPECTED);
    nsCOMPtr<nsIInputStream> inStream;
    rv = NS_NewLocalFileInputStream(getter_AddRefs(inStream),
                                    mFailedProfileLockFile,
                                    PR_RDONLY);
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_TRUE(GetFailedLockCount(inStream, fileSize, failedLockCount),
                   NS_ERROR_UNEXPECTED);
    inStream->Close();

    mFailedProfileLockFile->Remove(false);
    return NS_OK;
  }
};

static TimeStamp gRecordedShutdownStartTime;
static bool gAlreadyFreedShutdownTimeFileName = false;
static char *gRecordedShutdownTimeFileName = nullptr;

static char *
GetShutdownTimeFileName()
{
  if (gAlreadyFreedShutdownTimeFileName) {
    return nullptr;
  }

  if (!gRecordedShutdownTimeFileName) {
    nsCOMPtr<nsIFile> mozFile;
    NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR, getter_AddRefs(mozFile));
    if (!mozFile)
      return nullptr;

    mozFile->AppendNative(NS_LITERAL_CSTRING("Telemetry.ShutdownTime.txt"));
    nsAutoCString nativePath;
    nsresult rv = mozFile->GetNativePath(nativePath);
    if (!NS_SUCCEEDED(rv))
      return nullptr;

    gRecordedShutdownTimeFileName = PL_strdup(nativePath.get());
  }

  return gRecordedShutdownTimeFileName;
}

NS_IMETHODIMP
TelemetryImpl::GetLastShutdownDuration(uint32_t *aResult)
{
  // The user must call AsyncFetchTelemetryData first. We return zero instead of
  // reporting a failure so that the rest of telemetry can uniformly handle
  // the read not being available yet.
  if (!mCachedTelemetryData) {
    *aResult = 0;
    return NS_OK;
  }

  *aResult = mLastShutdownTime;
  return NS_OK;
}

NS_IMETHODIMP
TelemetryImpl::GetFailedProfileLockCount(uint32_t* aResult)
{
  // The user must call AsyncFetchTelemetryData first. We return zero instead of
  // reporting a failure so that the rest of telemetry can uniformly handle
  // the read not being available yet.
  if (!mCachedTelemetryData) {
    *aResult = 0;
    return NS_OK;
  }

  *aResult = mFailedLockCount;
  return NS_OK;
}

NS_IMETHODIMP
TelemetryImpl::AsyncFetchTelemetryData(nsIFetchTelemetryDataCallback *aCallback)
{
  // We have finished reading the data already, just call the callback.
  if (mCachedTelemetryData) {
    aCallback->Complete();
    return NS_OK;
  }

  // We already have a read request running, just remember the callback.
  if (mCallbacks.Count() != 0) {
    mCallbacks.AppendObject(aCallback);
    return NS_OK;
  }

  // We make this check so that GetShutdownTimeFileName() doesn't get
  // called; calling that function without telemetry enabled violates
  // assumptions that the write-the-shutdown-timestamp machinery makes.
  if (!Telemetry::CanRecordExtended()) {
    mCachedTelemetryData = true;
    aCallback->Complete();
    return NS_OK;
  }

  // Send the read to a background thread provided by the stream transport
  // service to avoid a read in the main thread.
  nsCOMPtr<nsIEventTarget> targetThread =
    do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID);
  if (!targetThread) {
    mCachedTelemetryData = true;
    aCallback->Complete();
    return NS_OK;
  }

  // We have to get the filename from the main thread.
  const char *shutdownTimeFilename = GetShutdownTimeFileName();
  if (!shutdownTimeFilename) {
    mCachedTelemetryData = true;
    aCallback->Complete();
    return NS_OK;
  }

  nsCOMPtr<nsIFile> profileDir;
  nsresult rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                       getter_AddRefs(profileDir));
  if (NS_FAILED(rv)) {
    mCachedTelemetryData = true;
    aCallback->Complete();
    return NS_OK;
  }

  nsCOMPtr<nsIFile> failedProfileLockFile;
  rv = GetFailedProfileLockFile(getter_AddRefs(failedProfileLockFile),
                                profileDir);
  if (NS_FAILED(rv)) {
    mCachedTelemetryData = true;
    aCallback->Complete();
    return NS_OK;
  }

  mCallbacks.AppendObject(aCallback);

  nsCOMPtr<nsIRunnable> event = new nsFetchTelemetryData(shutdownTimeFilename,
                                                         failedProfileLockFile,
                                                         profileDir);

  targetThread->Dispatch(event, NS_DISPATCH_NORMAL);
  return NS_OK;
}

TelemetryImpl::TelemetryImpl()
  : mHashMutex("Telemetry::mHashMutex")
  , mHangReportsMutex("Telemetry::mHangReportsMutex")
  , mThreadHangStatsMutex("Telemetry::mThreadHangStatsMutex")
  , mCachedTelemetryData(false)
  , mLastShutdownTime(0)
  , mFailedLockCount(0)
{
  // We expect TelemetryHistogram::InitializeGlobalState() to have been
  // called before we get to this point.
  MOZ_ASSERT(TelemetryHistogram::GlobalStateHasBeenInitialized());
}

TelemetryImpl::~TelemetryImpl() {
  UnregisterWeakMemoryReporter(this);
}

void
TelemetryImpl::InitMemoryReporter() {
  RegisterWeakMemoryReporter(this);
}

NS_IMETHODIMP
TelemetryImpl::NewHistogram(const nsACString &name, const nsACString &expiration, uint32_t histogramType,
                            uint32_t min, uint32_t max, uint32_t bucketCount, JSContext *cx,
                            uint8_t optArgCount, JS::MutableHandle<JS::Value> ret)
{
  return TelemetryHistogram::NewHistogram(name, expiration, histogramType,
                                          min, max, bucketCount,
                                          cx, optArgCount, ret);
}

NS_IMETHODIMP
TelemetryImpl::NewKeyedHistogram(const nsACString &name, const nsACString &expiration, uint32_t histogramType,
                            uint32_t min, uint32_t max, uint32_t bucketCount, JSContext *cx,
                            uint8_t optArgCount, JS::MutableHandle<JS::Value> ret)
{
  return TelemetryHistogram::NewKeyedHistogram(name, expiration, histogramType,
                                               min, max, bucketCount,
                                               cx, optArgCount, ret);
}


bool
TelemetryImpl::ReflectSQL(const SlowSQLEntryType *entry,
                          const Stat *stat,
                          JSContext *cx,
                          JS::Handle<JSObject*> obj)
{
  if (stat->hitCount == 0)
    return true;

  const nsACString &sql = entry->GetKey();

  JS::Rooted<JSObject*> arrayObj(cx, JS_NewArrayObject(cx, 0));
  if (!arrayObj) {
    return false;
  }
  return (JS_DefineElement(cx, arrayObj, 0, stat->hitCount, JSPROP_ENUMERATE)
          && JS_DefineElement(cx, arrayObj, 1, stat->totalTime, JSPROP_ENUMERATE)
          && JS_DefineProperty(cx, obj, sql.BeginReading(), arrayObj,
                               JSPROP_ENUMERATE));
}

bool
TelemetryImpl::ReflectMainThreadSQL(SlowSQLEntryType *entry, JSContext *cx,
                                    JS::Handle<JSObject*> obj)
{
  return ReflectSQL(entry, &entry->mData.mainThread, cx, obj);
}

bool
TelemetryImpl::ReflectOtherThreadsSQL(SlowSQLEntryType *entry, JSContext *cx,
                                      JS::Handle<JSObject*> obj)
{
  return ReflectSQL(entry, &entry->mData.otherThreads, cx, obj);
}

bool
TelemetryImpl::AddSQLInfo(JSContext *cx, JS::Handle<JSObject*> rootObj, bool mainThread,
                          bool privateSQL)
{
  JS::Rooted<JSObject*> statsObj(cx, JS_NewPlainObject(cx));
  if (!statsObj)
    return false;

  AutoHashtable<SlowSQLEntryType> &sqlMap =
    (privateSQL ? mPrivateSQL : mSanitizedSQL);
  AutoHashtable<SlowSQLEntryType>::ReflectEntryFunc reflectFunction =
    (mainThread ? ReflectMainThreadSQL : ReflectOtherThreadsSQL);
  if (!sqlMap.ReflectIntoJS(reflectFunction, cx, statsObj)) {
    return false;
  }

  return JS_DefineProperty(cx, rootObj,
                           mainThread ? "mainThread" : "otherThreads",
                           statsObj, JSPROP_ENUMERATE);
}

NS_IMETHODIMP
TelemetryImpl::HistogramFrom(const nsACString &name, const nsACString &existing_name,
                             JSContext *cx, JS::MutableHandle<JS::Value> ret)
{
  return TelemetryHistogram::HistogramFrom(name, existing_name, cx, ret);
}

NS_IMETHODIMP
TelemetryImpl::RegisterAddonHistogram(const nsACString &id,
                                      const nsACString &name,
                                      uint32_t histogramType,
                                      uint32_t min, uint32_t max,
                                      uint32_t bucketCount,
                                      uint8_t optArgCount)
{
  return TelemetryHistogram::RegisterAddonHistogram
            (id, name, histogramType, min, max, bucketCount, optArgCount);
}

NS_IMETHODIMP
TelemetryImpl::GetAddonHistogram(const nsACString &id, const nsACString &name,
                                 JSContext *cx, JS::MutableHandle<JS::Value> ret)
{
  return TelemetryHistogram::GetAddonHistogram(id, name, cx, ret);
}

NS_IMETHODIMP
TelemetryImpl::UnregisterAddonHistograms(const nsACString &id)
{
  return TelemetryHistogram::UnregisterAddonHistograms(id);
}

NS_IMETHODIMP
TelemetryImpl::SetHistogramRecordingEnabled(const nsACString &id, bool aEnabled)
{
  return TelemetryHistogram::SetHistogramRecordingEnabled(id, aEnabled);
}

NS_IMETHODIMP
TelemetryImpl::GetHistogramSnapshots(JSContext *cx, JS::MutableHandle<JS::Value> ret)
{
  return TelemetryHistogram::CreateHistogramSnapshots(cx, ret, false, false);
}

NS_IMETHODIMP
TelemetryImpl::SnapshotSubsessionHistograms(bool clearSubsession,
                                            JSContext *cx,
                                            JS::MutableHandle<JS::Value> ret)
{
#if !defined(MOZ_WIDGET_GONK) && !defined(MOZ_WIDGET_ANDROID)
  return TelemetryHistogram::CreateHistogramSnapshots(cx, ret, true,
                                                      clearSubsession);
#else
  return NS_OK;
#endif
}

NS_IMETHODIMP
TelemetryImpl::GetAddonHistogramSnapshots(JSContext *cx, JS::MutableHandle<JS::Value> ret)
{
  return TelemetryHistogram::GetAddonHistogramSnapshots(cx, ret);
}

NS_IMETHODIMP
TelemetryImpl::GetKeyedHistogramSnapshots(JSContext *cx, JS::MutableHandle<JS::Value> ret)
{
  return TelemetryHistogram::GetKeyedHistogramSnapshots(cx, ret);
}

bool
TelemetryImpl::GetSQLStats(JSContext *cx, JS::MutableHandle<JS::Value> ret, bool includePrivateSql)
{
  JS::Rooted<JSObject*> root_obj(cx, JS_NewPlainObject(cx));
  if (!root_obj)
    return false;
  ret.setObject(*root_obj);

  MutexAutoLock hashMutex(mHashMutex);
  // Add info about slow SQL queries on the main thread
  if (!AddSQLInfo(cx, root_obj, true, includePrivateSql))
    return false;
  // Add info about slow SQL queries on other threads
  if (!AddSQLInfo(cx, root_obj, false, includePrivateSql))
    return false;

  return true;
}

NS_IMETHODIMP
TelemetryImpl::GetSlowSQL(JSContext *cx, JS::MutableHandle<JS::Value> ret)
{
  if (GetSQLStats(cx, ret, false))
    return NS_OK;
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
TelemetryImpl::GetDebugSlowSQL(JSContext *cx, JS::MutableHandle<JS::Value> ret)
{
  bool revealPrivateSql =
    Preferences::GetBool("toolkit.telemetry.debugSlowSql", false);
  if (GetSQLStats(cx, ret, revealPrivateSql))
    return NS_OK;
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
TelemetryImpl::GetWebrtcStats(JSContext *cx, JS::MutableHandle<JS::Value> ret)
{
  if (mWebrtcTelemetry.GetWebrtcStats(cx, ret))
    return NS_OK;
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
TelemetryImpl::GetMaximalNumberOfConcurrentThreads(uint32_t *ret)
{
  *ret = nsThreadManager::get()->GetHighestNumberOfThreads();
  return NS_OK;
}

NS_IMETHODIMP
TelemetryImpl::GetChromeHangs(JSContext *cx, JS::MutableHandle<JS::Value> ret)
{
  MutexAutoLock hangReportMutex(mHangReportsMutex);

  const CombinedStacks& stacks = mHangReports.GetStacks();
  JS::Rooted<JSObject*> fullReportObj(cx, CreateJSStackObject(cx, stacks));
  if (!fullReportObj) {
    return NS_ERROR_FAILURE;
  }

  ret.setObject(*fullReportObj);

  JS::Rooted<JSObject*> durationArray(cx, JS_NewArrayObject(cx, 0));
  JS::Rooted<JSObject*> systemUptimeArray(cx, JS_NewArrayObject(cx, 0));
  JS::Rooted<JSObject*> firefoxUptimeArray(cx, JS_NewArrayObject(cx, 0));
  JS::Rooted<JSObject*> annotationsArray(cx, JS_NewArrayObject(cx, 0));
  if (!durationArray || !systemUptimeArray || !firefoxUptimeArray ||
      !annotationsArray) {
    return NS_ERROR_FAILURE;
  }

  bool ok = JS_DefineProperty(cx, fullReportObj, "durations",
                              durationArray, JSPROP_ENUMERATE);
  if (!ok) {
    return NS_ERROR_FAILURE;
  }

  ok = JS_DefineProperty(cx, fullReportObj, "systemUptime",
                         systemUptimeArray, JSPROP_ENUMERATE);
  if (!ok) {
    return NS_ERROR_FAILURE;
  }

  ok = JS_DefineProperty(cx, fullReportObj, "firefoxUptime",
                         firefoxUptimeArray, JSPROP_ENUMERATE);
  if (!ok) {
    return NS_ERROR_FAILURE;
  }

  ok = JS_DefineProperty(cx, fullReportObj, "annotations", annotationsArray,
                         JSPROP_ENUMERATE);
  if (!ok) {
    return NS_ERROR_FAILURE;
  }


  const size_t length = stacks.GetStackCount();
  for (size_t i = 0; i < length; ++i) {
    if (!JS_DefineElement(cx, durationArray, i, mHangReports.GetDuration(i),
                          JSPROP_ENUMERATE)) {
      return NS_ERROR_FAILURE;
    }
    if (!JS_DefineElement(cx, systemUptimeArray, i, mHangReports.GetSystemUptime(i),
                          JSPROP_ENUMERATE)) {
      return NS_ERROR_FAILURE;
    }
    if (!JS_DefineElement(cx, firefoxUptimeArray, i, mHangReports.GetFirefoxUptime(i),
                          JSPROP_ENUMERATE)) {
      return NS_ERROR_FAILURE;
    }

    size_t annotationIndex = 0;
    const nsClassHashtable<nsStringHashKey, HangReports::AnnotationInfo>& annotationInfo =
      mHangReports.GetAnnotationInfo();

    for (auto iter = annotationInfo.ConstIter(); !iter.Done(); iter.Next()) {
      const HangReports::AnnotationInfo* info = iter.Data();

      JS::Rooted<JSObject*> keyValueArray(cx, JS_NewArrayObject(cx, 0));
      if (!keyValueArray) {
        return NS_ERROR_FAILURE;
      }

      // Create an array containing all the indices of the chrome hangs relative to this
      // annotation.
      JS::Rooted<JS::Value> indicesArray(cx);
      if (!mozilla::dom::ToJSValue(cx, info->mHangIndices, &indicesArray)) {
        return NS_ERROR_OUT_OF_MEMORY;
      }

      // We're saving the annotation as [[indices], {annotation-data}], so add the indices
      // array as the first element of that structure.
      if (!JS_DefineElement(cx, keyValueArray, 0, indicesArray, JSPROP_ENUMERATE)) {
        return NS_ERROR_FAILURE;
      }

      // Create the annotations object...
      JS::Rooted<JSObject*> jsAnnotation(cx, JS_NewPlainObject(cx));
      if (!jsAnnotation) {
        return NS_ERROR_FAILURE;
      }
      UniquePtr<HangAnnotations::Enumerator> annotationsEnum =
        info->mAnnotations->GetEnumerator();
      if (!annotationsEnum) {
        return NS_ERROR_FAILURE;
      }

      // ... fill it with key:value pairs...
      nsAutoString key;
      nsAutoString value;
      while (annotationsEnum->Next(key, value)) {
        JS::RootedValue jsValue(cx);
        jsValue.setString(JS_NewUCStringCopyN(cx, value.get(), value.Length()));
        if (!JS_DefineUCProperty(cx, jsAnnotation, key.get(), key.Length(),
                                 jsValue, JSPROP_ENUMERATE)) {
          return NS_ERROR_FAILURE;
        }
      }

      // ... and append it after the indices array.
      if (!JS_DefineElement(cx, keyValueArray, 1, jsAnnotation, JSPROP_ENUMERATE)) {
        return NS_ERROR_FAILURE;
      }
      if (!JS_DefineElement(cx, annotationsArray, annotationIndex++,
                         keyValueArray, JSPROP_ENUMERATE)) {
        return NS_ERROR_FAILURE;
      }
    }
  }

  return NS_OK;
}

static JSObject *
CreateJSStackObject(JSContext *cx, const CombinedStacks &stacks) {
  JS::Rooted<JSObject*> ret(cx, JS_NewPlainObject(cx));
  if (!ret) {
    return nullptr;
  }

  JS::Rooted<JSObject*> moduleArray(cx, JS_NewArrayObject(cx, 0));
  if (!moduleArray) {
    return nullptr;
  }
  bool ok = JS_DefineProperty(cx, ret, "memoryMap", moduleArray,
                              JSPROP_ENUMERATE);
  if (!ok) {
    return nullptr;
  }

  const size_t moduleCount = stacks.GetModuleCount();
  for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex) {
    // Current module
    const Telemetry::ProcessedStack::Module& module =
      stacks.GetModule(moduleIndex);

    JS::Rooted<JSObject*> moduleInfoArray(cx, JS_NewArrayObject(cx, 0));
    if (!moduleInfoArray) {
      return nullptr;
    }
    if (!JS_DefineElement(cx, moduleArray, moduleIndex, moduleInfoArray,
                          JSPROP_ENUMERATE)) {
      return nullptr;
    }

    unsigned index = 0;

    // Module name
    JS::Rooted<JSString*> str(cx, JS_NewStringCopyZ(cx, module.mName.c_str()));
    if (!str) {
      return nullptr;
    }
    if (!JS_DefineElement(cx, moduleInfoArray, index++, str, JSPROP_ENUMERATE)) {
      return nullptr;
    }

    // Module breakpad identifier
    JS::Rooted<JSString*> id(cx, JS_NewStringCopyZ(cx, module.mBreakpadId.c_str()));
    if (!id) {
      return nullptr;
    }
    if (!JS_DefineElement(cx, moduleInfoArray, index++, id, JSPROP_ENUMERATE)) {
      return nullptr;
    }
  }

  JS::Rooted<JSObject*> reportArray(cx, JS_NewArrayObject(cx, 0));
  if (!reportArray) {
    return nullptr;
  }
  ok = JS_DefineProperty(cx, ret, "stacks", reportArray, JSPROP_ENUMERATE);
  if (!ok) {
    return nullptr;
  }

  const size_t length = stacks.GetStackCount();
  for (size_t i = 0; i < length; ++i) {
    // Represent call stack PCs as (module index, offset) pairs.
    JS::Rooted<JSObject*> pcArray(cx, JS_NewArrayObject(cx, 0));
    if (!pcArray) {
      return nullptr;
    }

    if (!JS_DefineElement(cx, reportArray, i, pcArray, JSPROP_ENUMERATE)) {
      return nullptr;
    }

    const CombinedStacks::Stack& stack = stacks.GetStack(i);
    const uint32_t pcCount = stack.size();
    for (size_t pcIndex = 0; pcIndex < pcCount; ++pcIndex) {
      const Telemetry::ProcessedStack::Frame& frame = stack[pcIndex];
      JS::Rooted<JSObject*> framePair(cx, JS_NewArrayObject(cx, 0));
      if (!framePair) {
        return nullptr;
      }
      int modIndex = (std::numeric_limits<uint16_t>::max() == frame.mModIndex) ?
        -1 : frame.mModIndex;
      if (!JS_DefineElement(cx, framePair, 0, modIndex, JSPROP_ENUMERATE)) {
        return nullptr;
      }
      if (!JS_DefineElement(cx, framePair, 1, static_cast<double>(frame.mOffset),
                            JSPROP_ENUMERATE)) {
        return nullptr;
      }
      if (!JS_DefineElement(cx, pcArray, pcIndex, framePair, JSPROP_ENUMERATE)) {
        return nullptr;
      }
    }
  }

  return ret;
}

static bool
IsValidBreakpadId(const std::string &breakpadId) {
  if (breakpadId.size() < 33) {
    return false;
  }
  for (unsigned i = 0, n = breakpadId.size(); i < n; ++i) {
    char c = breakpadId[i];
    if ((c < '0' || c > '9') && (c < 'A' || c > 'F')) {
      return false;
    }
  }
  return true;
}

// Read a stack from the given file name. In case of any error, aStack is
// unchanged.
static void
ReadStack(const char *aFileName, Telemetry::ProcessedStack &aStack)
{
  std::ifstream file(aFileName);

  size_t numModules;
  file >> numModules;
  if (file.fail()) {
    return;
  }

  char newline = file.get();
  if (file.fail() || newline != '\n') {
    return;
  }

  Telemetry::ProcessedStack stack;
  for (size_t i = 0; i < numModules; ++i) {
    std::string breakpadId;
    file >> breakpadId;
    if (file.fail() || !IsValidBreakpadId(breakpadId)) {
      return;
    }

    char space = file.get();
    if (file.fail() || space != ' ') {
      return;
    }

    std::string moduleName;
    getline(file, moduleName);
    if (file.fail() || moduleName[0] == ' ') {
      return;
    }

    Telemetry::ProcessedStack::Module module = {
      moduleName,
      breakpadId
    };
    stack.AddModule(module);
  }

  size_t numFrames;
  file >> numFrames;
  if (file.fail()) {
    return;
  }

  newline = file.get();
  if (file.fail() || newline != '\n') {
    return;
  }

  for (size_t i = 0; i < numFrames; ++i) {
    uint16_t index;
    file >> index;
    uintptr_t offset;
    file >> std::hex >> offset >> std::dec;
    if (file.fail()) {
      return;
    }

    Telemetry::ProcessedStack::Frame frame = {
      offset,
      index
    };
    stack.AddFrame(frame);
  }

  aStack = stack;
}

static JSObject*
CreateJSTimeHistogram(JSContext* cx, const Telemetry::TimeHistogram& time)
{
  /* Create JS representation of TimeHistogram,
     in the format of Chromium-style histograms. */
  JS::RootedObject ret(cx, JS_NewPlainObject(cx));
  if (!ret) {
    return nullptr;
  }

  if (!JS_DefineProperty(cx, ret, "min", time.GetBucketMin(0),
                         JSPROP_ENUMERATE) ||
      !JS_DefineProperty(cx, ret, "max",
                         time.GetBucketMax(ArrayLength(time) - 1),
                         JSPROP_ENUMERATE) ||
      !JS_DefineProperty(cx, ret, "histogram_type",
                         nsITelemetry::HISTOGRAM_EXPONENTIAL,
                         JSPROP_ENUMERATE)) {
    return nullptr;
  }
  // TODO: calculate "sum"
  if (!JS_DefineProperty(cx, ret, "sum", 0, JSPROP_ENUMERATE)) {
    return nullptr;
  }

  JS::RootedObject ranges(
    cx, JS_NewArrayObject(cx, ArrayLength(time) + 1));
  JS::RootedObject counts(
    cx, JS_NewArrayObject(cx, ArrayLength(time) + 1));
  if (!ranges || !counts) {
    return nullptr;
  }
  /* In a Chromium-style histogram, the first bucket is an "under" bucket
     that represents all values below the histogram's range. */
  if (!JS_DefineElement(cx, ranges, 0, time.GetBucketMin(0), JSPROP_ENUMERATE) ||
      !JS_DefineElement(cx, counts, 0, 0, JSPROP_ENUMERATE)) {
    return nullptr;
  }
  for (size_t i = 0; i < ArrayLength(time); i++) {
    if (!JS_DefineElement(cx, ranges, i + 1, time.GetBucketMax(i),
                          JSPROP_ENUMERATE) ||
        !JS_DefineElement(cx, counts, i + 1, time[i], JSPROP_ENUMERATE)) {
      return nullptr;
    }
  }
  if (!JS_DefineProperty(cx, ret, "ranges", ranges, JSPROP_ENUMERATE) ||
      !JS_DefineProperty(cx, ret, "counts", counts, JSPROP_ENUMERATE)) {
    return nullptr;
  }
  return ret;
}

static JSObject*
CreateJSHangStack(JSContext* cx, const Telemetry::HangStack& stack)
{
  JS::RootedObject ret(cx, JS_NewArrayObject(cx, stack.length()));
  if (!ret) {
    return nullptr;
  }
  for (size_t i = 0; i < stack.length(); i++) {
    JS::RootedString string(cx, JS_NewStringCopyZ(cx, stack[i]));
    if (!JS_DefineElement(cx, ret, i, string, JSPROP_ENUMERATE)) {
      return nullptr;
    }
  }
  return ret;
}

static void
CreateJSHangAnnotations(JSContext* cx, const HangAnnotationsVector& annotations,
                        JS::MutableHandleObject returnedObject)
{
  JS::RootedObject annotationsArray(cx, JS_NewArrayObject(cx, 0));
  if (!annotationsArray) {
    returnedObject.set(nullptr);
    return;
  }
  // We keep track of the annotations we reported in this hash set, so we can
  // discard duplicated ones.
  nsTHashtable<nsStringHashKey> reportedAnnotations;
  size_t annotationIndex = 0;
  for (const HangAnnotationsPtr *i = annotations.begin(), *e = annotations.end();
       i != e; ++i) {
    JS::RootedObject jsAnnotation(cx, JS_NewPlainObject(cx));
    if (!jsAnnotation) {
      continue;
    }
    const HangAnnotationsPtr& curAnnotations = *i;
    // Build a key to index the current annotations in our hash set.
    nsAutoString annotationsKey;
    nsresult rv = ComputeAnnotationsKey(curAnnotations, annotationsKey);
    if (NS_FAILED(rv)) {
      continue;
    }
    // Check if the annotations are in the set. If that's the case, don't double report.
    if (reportedAnnotations.GetEntry(annotationsKey)) {
      continue;
    }
    // If not, report them.
    reportedAnnotations.PutEntry(annotationsKey);
    UniquePtr<HangAnnotations::Enumerator> annotationsEnum =
      curAnnotations->GetEnumerator();
    if (!annotationsEnum) {
      continue;
    }
    nsAutoString key;
    nsAutoString value;
    while (annotationsEnum->Next(key, value)) {
      JS::RootedValue jsValue(cx);
      jsValue.setString(JS_NewUCStringCopyN(cx, value.get(), value.Length()));
      if (!JS_DefineUCProperty(cx, jsAnnotation, key.get(), key.Length(),
                               jsValue, JSPROP_ENUMERATE)) {
        returnedObject.set(nullptr);
        return;
      }
    }
    if (!JS_SetElement(cx, annotationsArray, annotationIndex, jsAnnotation)) {
      continue;
    }
    ++annotationIndex;
  }
  // Return the array using a |MutableHandleObject| to avoid triggering a false
  // positive rooting issue in the hazard analysis build.
  returnedObject.set(annotationsArray);
}

static JSObject*
CreateJSHangHistogram(JSContext* cx, const Telemetry::HangHistogram& hang)
{
  JS::RootedObject ret(cx, JS_NewPlainObject(cx));
  if (!ret) {
    return nullptr;
  }

  JS::RootedObject stack(cx, CreateJSHangStack(cx, hang.GetStack()));
  JS::RootedObject time(cx, CreateJSTimeHistogram(cx, hang));
  auto& hangAnnotations = hang.GetAnnotations();
  JS::RootedObject annotations(cx);
  CreateJSHangAnnotations(cx, hangAnnotations, &annotations);

  if (!stack ||
      !time ||
      !annotations ||
      !JS_DefineProperty(cx, ret, "stack", stack, JSPROP_ENUMERATE) ||
      !JS_DefineProperty(cx, ret, "histogram", time, JSPROP_ENUMERATE) ||
      (!hangAnnotations.empty() && // <-- Only define annotations when nonempty
        !JS_DefineProperty(cx, ret, "annotations", annotations, JSPROP_ENUMERATE))) {
    return nullptr;
  }

  if (!hang.GetNativeStack().empty()) {
    JS::RootedObject native(cx, CreateJSHangStack(cx, hang.GetNativeStack()));
    if (!native ||
        !JS_DefineProperty(cx, ret, "nativeStack", native, JSPROP_ENUMERATE)) {
      return nullptr;
    }
  }
  return ret;
}

static JSObject*
CreateJSThreadHangStats(JSContext* cx, const Telemetry::ThreadHangStats& thread)
{
  JS::RootedObject ret(cx, JS_NewPlainObject(cx));
  if (!ret) {
    return nullptr;
  }
  JS::RootedString name(cx, JS_NewStringCopyZ(cx, thread.GetName()));
  if (!name ||
      !JS_DefineProperty(cx, ret, "name", name, JSPROP_ENUMERATE)) {
    return nullptr;
  }

  JS::RootedObject activity(cx, CreateJSTimeHistogram(cx, thread.mActivity));
  if (!activity ||
      !JS_DefineProperty(cx, ret, "activity", activity, JSPROP_ENUMERATE)) {
    return nullptr;
  }

  JS::RootedObject hangs(cx, JS_NewArrayObject(cx, 0));
  if (!hangs) {
    return nullptr;
  }
  for (size_t i = 0; i < thread.mHangs.length(); i++) {
    JS::RootedObject obj(cx, CreateJSHangHistogram(cx, thread.mHangs[i]));
    if (!JS_DefineElement(cx, hangs, i, obj, JSPROP_ENUMERATE)) {
      return nullptr;
    }
  }
  if (!JS_DefineProperty(cx, ret, "hangs", hangs, JSPROP_ENUMERATE)) {
    return nullptr;
  }

  return ret;
}

NS_IMETHODIMP
TelemetryImpl::GetThreadHangStats(JSContext* cx, JS::MutableHandle<JS::Value> ret)
{
  JS::RootedObject retObj(cx, JS_NewArrayObject(cx, 0));
  if (!retObj) {
    return NS_ERROR_FAILURE;
  }
  size_t threadIndex = 0;

  if (!BackgroundHangMonitor::IsDisabled()) {
    /* First add active threads; we need to hold |iter| (and its lock)
       throughout this method to avoid a race condition where a thread can
       be recorded twice if the thread is destroyed while this method is
       running */
    BackgroundHangMonitor::ThreadHangStatsIterator iter;
    for (Telemetry::ThreadHangStats* histogram = iter.GetNext();
         histogram; histogram = iter.GetNext()) {
      JS::RootedObject obj(cx, CreateJSThreadHangStats(cx, *histogram));
      if (!JS_DefineElement(cx, retObj, threadIndex++, obj, JSPROP_ENUMERATE)) {
        return NS_ERROR_FAILURE;
      }
    }
  }

  // Add saved threads next
  MutexAutoLock autoLock(mThreadHangStatsMutex);
  for (size_t i = 0; i < mThreadHangStats.length(); i++) {
    JS::RootedObject obj(cx,
      CreateJSThreadHangStats(cx, mThreadHangStats[i]));
    if (!JS_DefineElement(cx, retObj, threadIndex++, obj, JSPROP_ENUMERATE)) {
      return NS_ERROR_FAILURE;
    }
  }
  ret.setObject(*retObj);
  return NS_OK;
}

void
TelemetryImpl::ReadLateWritesStacks(nsIFile* aProfileDir)
{
  nsAutoCString nativePath;
  nsresult rv = aProfileDir->GetNativePath(nativePath);
  if (NS_FAILED(rv)) {
    return;
  }

  const char *name = nativePath.get();
  PRDir *dir = PR_OpenDir(name);
  if (!dir) {
    return;
  }

  PRDirEntry *ent;
  const char *prefix = "Telemetry.LateWriteFinal-";
  unsigned int prefixLen = strlen(prefix);
  while ((ent = PR_ReadDir(dir, PR_SKIP_NONE))) {
    if (strncmp(prefix, ent->name, prefixLen) != 0) {
      continue;
    }

    nsAutoCString stackNativePath = nativePath;
    stackNativePath += XPCOM_FILE_PATH_SEPARATOR;
    stackNativePath += nsDependentCString(ent->name);

    Telemetry::ProcessedStack stack;
    ReadStack(stackNativePath.get(), stack);
    if (stack.GetStackSize() != 0) {
      mLateWritesStacks.AddStack(stack);
    }
    // Delete the file so that we don't report it again on the next run.
    PR_Delete(stackNativePath.get());
  }
  PR_CloseDir(dir);
}

NS_IMETHODIMP
TelemetryImpl::GetLateWrites(JSContext *cx, JS::MutableHandle<JS::Value> ret)
{
  // The user must call AsyncReadTelemetryData first. We return an empty list
  // instead of reporting a failure so that the rest of telemetry can uniformly
  // handle the read not being available yet.

  // FIXME: we allocate the js object again and again in the getter. We should
  // figure out a way to cache it. In order to do that we have to call
  // JS_AddNamedObjectRoot. A natural place to do so is in the TelemetryImpl
  // constructor, but it is not clear how to get a JSContext in there.
  // Another option would be to call it in here when we first call
  // CreateJSStackObject, but we would still need to figure out where to call
  // JS_RemoveObjectRoot. Would it be ok to never call JS_RemoveObjectRoot
  // and just set the pointer to nullptr is the telemetry destructor?

  JSObject *report;
  if (!mCachedTelemetryData) {
    CombinedStacks empty;
    report = CreateJSStackObject(cx, empty);
  } else {
    report = CreateJSStackObject(cx, mLateWritesStacks);
  }

  if (report == nullptr) {
    return NS_ERROR_FAILURE;
  }

  ret.setObject(*report);
  return NS_OK;
}

NS_IMETHODIMP
TelemetryImpl::RegisteredHistograms(uint32_t aDataset, uint32_t *aCount,
                                    char*** aHistograms)
{
  return
    TelemetryHistogram::RegisteredHistograms(aDataset, aCount, aHistograms);
}

NS_IMETHODIMP
TelemetryImpl::RegisteredKeyedHistograms(uint32_t aDataset, uint32_t *aCount,
                                         char*** aHistograms)
{
  return
    TelemetryHistogram::RegisteredKeyedHistograms(aDataset, aCount,
                                                  aHistograms);
}

NS_IMETHODIMP
TelemetryImpl::GetHistogramById(const nsACString &name, JSContext *cx,
                                JS::MutableHandle<JS::Value> ret)
{
  return TelemetryHistogram::GetHistogramById(name, cx, ret);
}

NS_IMETHODIMP
TelemetryImpl::GetKeyedHistogramById(const nsACString &name, JSContext *cx,
                                     JS::MutableHandle<JS::Value> ret)
{
  return TelemetryHistogram::GetKeyedHistogramById(name, cx, ret);
}

/**
 * Indicates if Telemetry can record base data (FHR data). This is true if the
 * FHR data reporting service or self-support are enabled.
 *
 * In the unlikely event that adding a new base probe is needed, please check the data
 * collection wiki at https://wiki.mozilla.org/Firefox/Data_Collection and talk to the
 * Telemetry team.
 */
NS_IMETHODIMP
TelemetryImpl::GetCanRecordBase(bool *ret) {
  *ret = TelemetryHistogram::CanRecordBase();
  return NS_OK;
}

NS_IMETHODIMP
TelemetryImpl::SetCanRecordBase(bool canRecord) {
  TelemetryHistogram::SetCanRecordBase(canRecord);
  return NS_OK;
}

/**
 * Indicates if Telemetry is allowed to record extended data. Returns false if the user
 * hasn't opted into "extended Telemetry" on the Release channel, when the user has
 * explicitly opted out of Telemetry on Nightly/Aurora/Beta or if manually set to false
 * during tests.
 * If the returned value is false, gathering of extended telemetry statistics is disabled.
 */
NS_IMETHODIMP
TelemetryImpl::GetCanRecordExtended(bool *ret) {
  *ret = TelemetryHistogram::CanRecordExtended();
  return NS_OK;
}

NS_IMETHODIMP
TelemetryImpl::SetCanRecordExtended(bool canRecord) {
  TelemetryHistogram::SetCanRecordExtended(canRecord);
  return NS_OK;
}


NS_IMETHODIMP
TelemetryImpl::GetIsOfficialTelemetry(bool *ret) {
#if defined(MOZILLA_OFFICIAL) && defined(MOZ_TELEMETRY_REPORTING) && !defined(DEBUG)
  *ret = true;
#else
  *ret = false;
#endif
  return NS_OK;
}

already_AddRefed<nsITelemetry>
TelemetryImpl::CreateTelemetryInstance()
{
  MOZ_ASSERT(sTelemetry == nullptr, "CreateTelemetryInstance may only be called once, via GetService()");

  // First, initialize the TelemetryHistogram global state.
  TelemetryHistogram::InitializeGlobalState(
                        XRE_IsParentProcess() || XRE_IsContentProcess(),
                        XRE_IsParentProcess() || XRE_IsContentProcess());

  // Now, create and initialize the Telemetry global state.
  sTelemetry = new TelemetryImpl();

  // AddRef for the local reference
  NS_ADDREF(sTelemetry);
  // AddRef for the caller
  nsCOMPtr<nsITelemetry> ret = sTelemetry;

  sTelemetry->InitMemoryReporter();
  InitHistogramRecordingEnabled(); // requires sTelemetry to exist

  return ret.forget();
}

void
TelemetryImpl::ShutdownTelemetry()
{
  // No point in collecting IO beyond this point
  ClearIOReporting();
  NS_IF_RELEASE(sTelemetry);

  // Lastly, de-initialise the TelemetryHistogram global state, so as to
  // release any heap storage that would otherwise be kept alive by it.
  TelemetryHistogram::DeInitializeGlobalState();
}

void
TelemetryImpl::StoreSlowSQL(const nsACString &sql, uint32_t delay,
                            SanitizedState state)
{
  AutoHashtable<SlowSQLEntryType> *slowSQLMap = nullptr;
  if (state == Sanitized)
    slowSQLMap = &(sTelemetry->mSanitizedSQL);
  else
    slowSQLMap = &(sTelemetry->mPrivateSQL);

  MutexAutoLock hashMutex(sTelemetry->mHashMutex);

  SlowSQLEntryType *entry = slowSQLMap->GetEntry(sql);
  if (!entry) {
    entry = slowSQLMap->PutEntry(sql);
    if (MOZ_UNLIKELY(!entry))
      return;
    entry->mData.mainThread.hitCount = 0;
    entry->mData.mainThread.totalTime = 0;
    entry->mData.otherThreads.hitCount = 0;
    entry->mData.otherThreads.totalTime = 0;
  }

  if (NS_IsMainThread()) {
    entry->mData.mainThread.hitCount++;
    entry->mData.mainThread.totalTime += delay;
  } else {
    entry->mData.otherThreads.hitCount++;
    entry->mData.otherThreads.totalTime += delay;
  }
}

/**
 * This method replaces string literals in SQL strings with the word :private
 *
 * States used in this state machine:
 *
 * NORMAL:
 *  - This is the active state when not iterating over a string literal or
 *  comment
 *
 * SINGLE_QUOTE:
 *  - Defined here: http://www.sqlite.org/lang_expr.html
 *  - This state represents iterating over a string literal opened with
 *  a single quote.
 *  - A single quote within the string can be encoded by putting 2 single quotes
 *  in a row, e.g. 'This literal contains an escaped quote '''
 *  - Any double quotes found within a single-quoted literal are ignored
 *  - This state covers BLOB literals, e.g. X'ABC123'
 *  - The string literal and the enclosing quotes will be replaced with
 *  the text :private
 *
 * DOUBLE_QUOTE:
 *  - Same rules as the SINGLE_QUOTE state.
 *  - According to http://www.sqlite.org/lang_keywords.html,
 *  SQLite interprets text in double quotes as an identifier unless it's used in
 *  a context where it cannot be resolved to an identifier and a string literal
 *  is allowed. This method removes text in double-quotes for safety.
 *
 *  DASH_COMMENT:
 *  - http://www.sqlite.org/lang_comment.html
 *  - A dash comment starts with two dashes in a row,
 *  e.g. DROP TABLE foo -- a comment
 *  - Any text following two dashes in a row is interpreted as a comment until
 *  end of input or a newline character
 *  - Any quotes found within the comment are ignored and no replacements made
 *
 *  C_STYLE_COMMENT:
 *  - http://www.sqlite.org/lang_comment.html
 *  - A C-style comment starts with a forward slash and an asterisk, and ends
 *  with an asterisk and a forward slash
 *  - Any text following comment start is interpreted as a comment up to end of
 *  input or comment end
 *  - Any quotes found within the comment are ignored and no replacements made
 */
nsCString
TelemetryImpl::SanitizeSQL(const nsACString &sql) {
  nsCString output;
  int length = sql.Length();

  typedef enum {
    NORMAL,
    SINGLE_QUOTE,
    DOUBLE_QUOTE,
    DASH_COMMENT,
    C_STYLE_COMMENT,
  } State;

  State state = NORMAL;
  int fragmentStart = 0;
  for (int i = 0; i < length; i++) {
    char character = sql[i];
    char nextCharacter = (i + 1 < length) ? sql[i + 1] : '\0';

    switch (character) {
      case '\'':
      case '"':
        if (state == NORMAL) {
          state = (character == '\'') ? SINGLE_QUOTE : DOUBLE_QUOTE;
          output += nsDependentCSubstring(sql, fragmentStart, i - fragmentStart);
          output += ":private";
          fragmentStart = -1;
        } else if ((state == SINGLE_QUOTE && character == '\'') ||
                   (state == DOUBLE_QUOTE && character == '"')) {
          if (nextCharacter == character) {
            // Two consecutive quotes within a string literal are a single escaped quote
            i++;
          } else {
            state = NORMAL;
            fragmentStart = i + 1;
          }
        }
        break;
      case '-':
        if (state == NORMAL) {
          if (nextCharacter == '-') {
            state = DASH_COMMENT;
            i++;
          }
        }
        break;
      case '\n':
        if (state == DASH_COMMENT) {
          state = NORMAL;
        }
        break;
      case '/':
        if (state == NORMAL) {
          if (nextCharacter == '*') {
            state = C_STYLE_COMMENT;
            i++;
          }
        }
        break;
      case '*':
        if (state == C_STYLE_COMMENT) {
          if (nextCharacter == '/') {
            state = NORMAL;
          }
        }
        break;
      default:
        continue;
    }
  }

  if ((fragmentStart >= 0) && fragmentStart < length)
    output += nsDependentCSubstring(sql, fragmentStart, length - fragmentStart);

  return output;
}

// A whitelist mechanism to prevent Telemetry reporting on Addon & Thunderbird
// DBs.
struct TrackedDBEntry
{
  const char* mName;
  const uint32_t mNameLength;

  // This struct isn't meant to be used beyond the static arrays below.
  constexpr
  TrackedDBEntry(const char* aName, uint32_t aNameLength)
    : mName(aName)
    , mNameLength(aNameLength)
  { }

  TrackedDBEntry() = delete;
  TrackedDBEntry(TrackedDBEntry&) = delete;
};

#define TRACKEDDB_ENTRY(_name) { _name, (sizeof(_name) - 1) }

// A whitelist of database names. If the database name exactly matches one of
// these then its SQL statements will always be recorded.
static constexpr TrackedDBEntry kTrackedDBs[] = {
  // IndexedDB for about:home, see aboutHome.js
  TRACKEDDB_ENTRY("818200132aebmoouht.sqlite"),
  TRACKEDDB_ENTRY("addons.sqlite"),
  TRACKEDDB_ENTRY("content-prefs.sqlite"),
  TRACKEDDB_ENTRY("cookies.sqlite"),
  TRACKEDDB_ENTRY("downloads.sqlite"),
  TRACKEDDB_ENTRY("extensions.sqlite"),
  TRACKEDDB_ENTRY("formhistory.sqlite"),
  TRACKEDDB_ENTRY("index.sqlite"),
  TRACKEDDB_ENTRY("netpredictions.sqlite"),
  TRACKEDDB_ENTRY("permissions.sqlite"),
  TRACKEDDB_ENTRY("places.sqlite"),
  TRACKEDDB_ENTRY("reading-list.sqlite"),
  TRACKEDDB_ENTRY("search.sqlite"),
  TRACKEDDB_ENTRY("signons.sqlite"),
  TRACKEDDB_ENTRY("urlclassifier3.sqlite"),
  TRACKEDDB_ENTRY("webappsstore.sqlite")
};

// A whitelist of database name prefixes. If the database name begins with
// one of these prefixes then its SQL statements will always be recorded.
static const TrackedDBEntry kTrackedDBPrefixes[] = {
  TRACKEDDB_ENTRY("indexedDB-")
};

#undef TRACKEDDB_ENTRY

// Slow SQL statements will be automatically
// trimmed to kMaxSlowStatementLength characters.
// This limit doesn't include the ellipsis and DB name,
// that are appended at the end of the stored statement.
const uint32_t kMaxSlowStatementLength = 1000;

void
TelemetryImpl::RecordSlowStatement(const nsACString &sql,
                                   const nsACString &dbName,
                                   uint32_t delay)
{
  MOZ_ASSERT(!sql.IsEmpty());
  MOZ_ASSERT(!dbName.IsEmpty());

  if (!sTelemetry || !TelemetryHistogram::CanRecordExtended())
    return;

  bool recordStatement = false;

  for (const TrackedDBEntry& nameEntry : kTrackedDBs) {
    MOZ_ASSERT(nameEntry.mNameLength);
    const nsDependentCString name(nameEntry.mName, nameEntry.mNameLength);
    if (dbName == name) {
      recordStatement = true;
      break;
    }
  }

  if (!recordStatement) {
    for (const TrackedDBEntry& prefixEntry : kTrackedDBPrefixes) {
      MOZ_ASSERT(prefixEntry.mNameLength);
      const nsDependentCString prefix(prefixEntry.mName,
                                      prefixEntry.mNameLength);
      if (StringBeginsWith(dbName, prefix)) {
        recordStatement = true;
        break;
      }
    }
  }

  if (recordStatement) {
    nsAutoCString sanitizedSQL(SanitizeSQL(sql));
    if (sanitizedSQL.Length() > kMaxSlowStatementLength) {
      sanitizedSQL.SetLength(kMaxSlowStatementLength);
      sanitizedSQL += "...";
    }
    sanitizedSQL.AppendPrintf(" /* %s */", nsPromiseFlatCString(dbName).get());
    StoreSlowSQL(sanitizedSQL, delay, Sanitized);
  } else {
    // Report aggregate DB-level statistics for addon DBs
    nsAutoCString aggregate;
    aggregate.AppendPrintf("Untracked SQL for %s",
                           nsPromiseFlatCString(dbName).get());
    StoreSlowSQL(aggregate, delay, Sanitized);
  }

  nsAutoCString fullSQL;
  fullSQL.AppendPrintf("%s /* %s */",
                       nsPromiseFlatCString(sql).get(),
                       nsPromiseFlatCString(dbName).get());
  StoreSlowSQL(fullSQL, delay, Unsanitized);
}

void
TelemetryImpl::RecordIceCandidates(const uint32_t iceCandidateBitmask,
                                   const bool success, const bool loop)
{
  if (!sTelemetry || !TelemetryHistogram::CanRecordExtended())
    return;

  sTelemetry->mWebrtcTelemetry.RecordIceCandidateMask(iceCandidateBitmask, success, loop);
}

#if defined(MOZ_ENABLE_PROFILER_SPS)
void
TelemetryImpl::RecordChromeHang(uint32_t aDuration,
                                Telemetry::ProcessedStack &aStack,
                                int32_t aSystemUptime,
                                int32_t aFirefoxUptime,
                                HangAnnotationsPtr aAnnotations)
{
  if (!sTelemetry || !TelemetryHistogram::CanRecordExtended())
    return;

  HangAnnotationsPtr annotations;
  // We only pass aAnnotations if it is not empty.
  if (aAnnotations && !aAnnotations->IsEmpty()) {
    annotations = Move(aAnnotations);
  }

  MutexAutoLock hangReportMutex(sTelemetry->mHangReportsMutex);

  sTelemetry->mHangReports.AddHang(aStack, aDuration,
                                   aSystemUptime, aFirefoxUptime,
                                   Move(annotations));
}
#endif

void
TelemetryImpl::RecordThreadHangStats(Telemetry::ThreadHangStats& aStats)
{
  if (!sTelemetry || !TelemetryHistogram::CanRecordExtended())
    return;

  MutexAutoLock autoLock(sTelemetry->mThreadHangStatsMutex);

  // Ignore OOM.
  mozilla::Unused << sTelemetry->mThreadHangStats.append(Move(aStats));
}

NS_IMPL_ISUPPORTS(TelemetryImpl, nsITelemetry, nsIMemoryReporter)
NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR(nsITelemetry, TelemetryImpl::CreateTelemetryInstance)

#define NS_TELEMETRY_CID \
  {0xaea477f2, 0xb3a2, 0x469c, {0xaa, 0x29, 0x0a, 0x82, 0xd1, 0x32, 0xb8, 0x29}}
NS_DEFINE_NAMED_CID(NS_TELEMETRY_CID);

const Module::CIDEntry kTelemetryCIDs[] = {
  { &kNS_TELEMETRY_CID, false, nullptr, nsITelemetryConstructor },
  { nullptr }
};

const Module::ContractIDEntry kTelemetryContracts[] = {
  { "@mozilla.org/base/telemetry;1", &kNS_TELEMETRY_CID },
  { nullptr }
};

const Module kTelemetryModule = {
  Module::kVersion,
  kTelemetryCIDs,
  kTelemetryContracts,
  nullptr,
  nullptr,
  nullptr,
  TelemetryImpl::ShutdownTelemetry
};

NS_IMETHODIMP
TelemetryImpl::GetFileIOReports(JSContext *cx, JS::MutableHandleValue ret)
{
  if (sTelemetryIOObserver) {
    JS::Rooted<JSObject*> obj(cx, JS_NewPlainObject(cx));
    if (!obj) {
      return NS_ERROR_FAILURE;
    }

    if (!sTelemetryIOObserver->ReflectIntoJS(cx, obj)) {
      return NS_ERROR_FAILURE;
    }
    ret.setObject(*obj);
    return NS_OK;
  }
  ret.setNull();
  return NS_OK;
}

NS_IMETHODIMP
TelemetryImpl::MsSinceProcessStart(double* aResult)
{
  bool error;
  *aResult = (TimeStamp::NowLoRes() -
              TimeStamp::ProcessCreation(error)).ToMilliseconds();
  if (error) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  return NS_OK;
}

size_t
TelemetryImpl::SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf)
{
  size_t n = aMallocSizeOf(this);

  // Ignore the hashtables in mAddonMap; they are not significant.
  n += TelemetryHistogram::GetMapShallowSizesOfExcludingThis(aMallocSizeOf);
  n += mWebrtcTelemetry.SizeOfExcludingThis(aMallocSizeOf);
  { // Scope for mHashMutex lock
    MutexAutoLock lock(mHashMutex);
    n += mPrivateSQL.SizeOfExcludingThis(aMallocSizeOf);
    n += mSanitizedSQL.SizeOfExcludingThis(aMallocSizeOf);
  }
  { // Scope for mHangReportsMutex lock
    MutexAutoLock lock(mHangReportsMutex);
    n += mHangReports.SizeOfExcludingThis(aMallocSizeOf);
  }
  { // Scope for mThreadHangStatsMutex lock
    MutexAutoLock lock(mThreadHangStatsMutex);
    n += mThreadHangStats.sizeOfExcludingThis(aMallocSizeOf);
  }

  // It's a bit gross that we measure this other stuff that lives outside of
  // TelemetryImpl... oh well.
  if (sTelemetryIOObserver) {
    n += sTelemetryIOObserver->SizeOfIncludingThis(aMallocSizeOf);
  }

  n += TelemetryHistogram::GetHistogramSizesofIncludingThis(aMallocSizeOf);

  return n;
}

struct StackFrame
{
  uintptr_t mPC;      // The program counter at this position in the call stack.
  uint16_t mIndex;    // The number of this frame in the call stack.
  uint16_t mModIndex; // The index of module that has this program counter.
};

#ifdef MOZ_ENABLE_PROFILER_SPS
static bool CompareByPC(const StackFrame &a, const StackFrame &b)
{
  return a.mPC < b.mPC;
}

static bool CompareByIndex(const StackFrame &a, const StackFrame &b)
{
  return a.mIndex < b.mIndex;
}
#endif

} // namespace


////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// EXTERNALLY VISIBLE FUNCTIONS in no name space
// These are NOT listed in Telemetry.h

NSMODULE_DEFN(nsTelemetryModule) = &kTelemetryModule;

/**
 * The XRE_TelemetryAdd function is to be used by embedding applications
 * that can't use mozilla::Telemetry::Accumulate() directly.
 */
void
XRE_TelemetryAccumulate(int aID, uint32_t aSample)
{
  mozilla::Telemetry::Accumulate((mozilla::Telemetry::ID) aID, aSample);
}


////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// EXTERNALLY VISIBLE FUNCTIONS in mozilla::
// These are NOT listed in Telemetry.h

namespace mozilla {

void
RecordShutdownStartTimeStamp() {
#ifdef DEBUG
  // FIXME: this function should only be called once, since it should be called
  // at the earliest point we *know* we are shutting down. Unfortunately
  // this assert has been firing. Given that if we are called multiple times
  // we just keep the last timestamp, the assert is commented for now.
  static bool recorded = false;
  //  MOZ_ASSERT(!recorded);
  (void)recorded; // Silence unused-var warnings (remove when assert re-enabled)
  recorded = true;
#endif

  if (!Telemetry::CanRecordExtended())
    return;

  gRecordedShutdownStartTime = TimeStamp::Now();

  GetShutdownTimeFileName();
}

void
RecordShutdownEndTimeStamp() {
  if (!gRecordedShutdownTimeFileName || gAlreadyFreedShutdownTimeFileName)
    return;

  nsCString name(gRecordedShutdownTimeFileName);
  PL_strfree(gRecordedShutdownTimeFileName);
  gRecordedShutdownTimeFileName = nullptr;
  gAlreadyFreedShutdownTimeFileName = true;

  if (gRecordedShutdownStartTime.IsNull()) {
    // If |CanRecordExtended()| is true before |AsyncFetchTelemetryData| is called and
    // then disabled before shutdown, |RecordShutdownStartTimeStamp| will bail out and
    // we will end up with a null |gRecordedShutdownStartTime| here. This can happen
    // during tests.
    return;
  }

  nsCString tmpName = name;
  tmpName += ".tmp";
  FILE *f = fopen(tmpName.get(), "w");
  if (!f)
    return;
  // On a normal release build this should be called just before
  // calling _exit, but on a debug build or when the user forces a full
  // shutdown this is called as late as possible, so we have to
  // white list this write as write poisoning will be enabled.
  MozillaRegisterDebugFILE(f);

  TimeStamp now = TimeStamp::Now();
  MOZ_ASSERT(now >= gRecordedShutdownStartTime);
  TimeDuration diff = now - gRecordedShutdownStartTime;
  uint32_t diff2 = diff.ToMilliseconds();
  int written = fprintf(f, "%d\n", diff2);
  MozillaUnRegisterDebugFILE(f);
  int rv = fclose(f);
  if (written < 0 || rv != 0) {
    PR_Delete(tmpName.get());
    return;
  }
  PR_Delete(name.get());
  PR_Rename(tmpName.get(), name.get());
}

} // namespace mozilla


////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// EXTERNALLY VISIBLE FUNCTIONS in mozilla::Telemetry::
// These are NOT listed in Telemetry.h

namespace mozilla {
namespace Telemetry {

ProcessedStack::ProcessedStack()
{
}

size_t ProcessedStack::GetStackSize() const
{
  return mStack.size();
}

size_t ProcessedStack::GetNumModules() const
{
  return mModules.size();
}

bool ProcessedStack::Module::operator==(const Module& aOther) const {
  return  mName == aOther.mName &&
    mBreakpadId == aOther.mBreakpadId;
}

const ProcessedStack::Frame &ProcessedStack::GetFrame(unsigned aIndex) const
{
  MOZ_ASSERT(aIndex < mStack.size());
  return mStack[aIndex];
}

void ProcessedStack::AddFrame(const Frame &aFrame)
{
  mStack.push_back(aFrame);
}

const ProcessedStack::Module &ProcessedStack::GetModule(unsigned aIndex) const
{
  MOZ_ASSERT(aIndex < mModules.size());
  return mModules[aIndex];
}

void ProcessedStack::AddModule(const Module &aModule)
{
  mModules.push_back(aModule);
}

void ProcessedStack::Clear() {
  mModules.clear();
  mStack.clear();
}

ProcessedStack
GetStackAndModules(const std::vector<uintptr_t>& aPCs)
{
  std::vector<StackFrame> rawStack;
  auto stackEnd = aPCs.begin() + std::min(aPCs.size(), kMaxChromeStackDepth);
  for (auto i = aPCs.begin(); i != stackEnd; ++i) {
    uintptr_t aPC = *i;
    StackFrame Frame = {aPC, static_cast<uint16_t>(rawStack.size()),
                        std::numeric_limits<uint16_t>::max()};
    rawStack.push_back(Frame);
  }

#ifdef MOZ_ENABLE_PROFILER_SPS
  // Remove all modules not referenced by a PC on the stack
  std::sort(rawStack.begin(), rawStack.end(), CompareByPC);

  size_t moduleIndex = 0;
  size_t stackIndex = 0;
  size_t stackSize = rawStack.size();

  SharedLibraryInfo rawModules = SharedLibraryInfo::GetInfoForSelf();
  rawModules.SortByAddress();

  while (moduleIndex < rawModules.GetSize()) {
    const SharedLibrary& module = rawModules.GetEntry(moduleIndex);
    uintptr_t moduleStart = module.GetStart();
    uintptr_t moduleEnd = module.GetEnd() - 1;
    // the interval is [moduleStart, moduleEnd)

    bool moduleReferenced = false;
    for (;stackIndex < stackSize; ++stackIndex) {
      uintptr_t pc = rawStack[stackIndex].mPC;
      if (pc >= moduleEnd)
        break;

      if (pc >= moduleStart) {
        // If the current PC is within the current module, mark
        // module as used
        moduleReferenced = true;
        rawStack[stackIndex].mPC -= moduleStart;
        rawStack[stackIndex].mModIndex = moduleIndex;
      } else {
        // PC does not belong to any module. It is probably from
        // the JIT. Use a fixed mPC so that we don't get different
        // stacks on different runs.
        rawStack[stackIndex].mPC =
          std::numeric_limits<uintptr_t>::max();
      }
    }

    if (moduleReferenced) {
      ++moduleIndex;
    } else {
      // Remove module if no PCs within its address range
      rawModules.RemoveEntries(moduleIndex, moduleIndex + 1);
    }
  }

  for (;stackIndex < stackSize; ++stackIndex) {
    // These PCs are past the last module.
    rawStack[stackIndex].mPC = std::numeric_limits<uintptr_t>::max();
  }

  std::sort(rawStack.begin(), rawStack.end(), CompareByIndex);
#endif

  // Copy the information to the return value.
  ProcessedStack Ret;
  for (std::vector<StackFrame>::iterator i = rawStack.begin(),
         e = rawStack.end(); i != e; ++i) {
    const StackFrame &rawFrame = *i;
    mozilla::Telemetry::ProcessedStack::Frame frame = { rawFrame.mPC, rawFrame.mModIndex };
    Ret.AddFrame(frame);
  }

#ifdef MOZ_ENABLE_PROFILER_SPS
  for (unsigned i = 0, n = rawModules.GetSize(); i != n; ++i) {
    const SharedLibrary &info = rawModules.GetEntry(i);
    const std::string &name = info.GetName();
    std::string basename = name;
#ifdef XP_MACOSX
    // FIXME: We want to use just the basename as the libname, but the
    // current profiler addon needs the full path name, so we compute the
    // basename in here.
    size_t pos = name.rfind('/');
    if (pos != std::string::npos) {
      basename = name.substr(pos + 1);
    }
#endif
    mozilla::Telemetry::ProcessedStack::Module module = {
      basename,
      info.GetBreakpadId()
    };
    Ret.AddModule(module);
  }
#endif

  return Ret;
}

void
TimeHistogram::Add(PRIntervalTime aTime)
{
  uint32_t timeMs = PR_IntervalToMilliseconds(aTime);
  size_t index = mozilla::FloorLog2(timeMs);
  operator[](index)++;
}

const char*
HangStack::InfallibleAppendViaBuffer(const char* aText, size_t aLength)
{
  MOZ_ASSERT(this->canAppendWithoutRealloc(1));
  // Include null-terminator in length count.
  MOZ_ASSERT(mBuffer.canAppendWithoutRealloc(aLength + 1));

  const char* const entry = mBuffer.end();
  mBuffer.infallibleAppend(aText, aLength);
  mBuffer.infallibleAppend('\0'); // Explicitly append null-terminator
  this->infallibleAppend(entry);
  return entry;
}

const char*
HangStack::AppendViaBuffer(const char* aText, size_t aLength)
{
  if (!this->reserve(this->length() + 1)) {
    return nullptr;
  }

  // Keep track of the previous buffer in case we need to adjust pointers later.
  const char* const prevStart = mBuffer.begin();
  const char* const prevEnd = mBuffer.end();

  // Include null-terminator in length count.
  if (!mBuffer.reserve(mBuffer.length() + aLength + 1)) {
    return nullptr;
  }

  if (prevStart != mBuffer.begin()) {
    // The buffer has moved; we have to adjust pointers in the stack.
    for (const char** entry = this->begin(); entry != this->end(); entry++) {
      if (*entry >= prevStart && *entry < prevEnd) {
        // Move from old buffer to new buffer.
        *entry += mBuffer.begin() - prevStart;
      }
    }
  }

  return InfallibleAppendViaBuffer(aText, aLength);
}

uint32_t
HangHistogram::GetHash(const HangStack& aStack)
{
  uint32_t hash = 0;
  for (const char* const* label = aStack.begin();
       label != aStack.end(); label++) {
    /* If the string is within our buffer, we need to hash its content.
       Otherwise, the string is statically allocated, and we only need
       to hash the pointer instead of the content. */
    if (aStack.IsInBuffer(*label)) {
      hash = AddToHash(hash, HashString(*label));
    } else {
      hash = AddToHash(hash, *label);
    }
  }
  return hash;
}

bool
HangHistogram::operator==(const HangHistogram& aOther) const
{
  if (mHash != aOther.mHash) {
    return false;
  }
  if (mStack.length() != aOther.mStack.length()) {
    return false;
  }
  return mStack == aOther.mStack;
}

} // namespace Telemetry
} // namespace mozilla


////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// EXTERNALLY VISIBLE FUNCTIONS in mozilla::Telemetry::
// These are listed in Telemetry.h

namespace mozilla {
namespace Telemetry {

// The external API for controlling recording state
void
SetHistogramRecordingEnabled(ID aID, bool aEnabled)
{
  TelemetryHistogram::SetHistogramRecordingEnabled(aID, aEnabled);
}

void
Accumulate(ID aHistogram, uint32_t aSample)
{
  TelemetryHistogram::Accumulate(aHistogram, aSample);
}

void
Accumulate(ID aID, const nsCString& aKey, uint32_t aSample)
{
  TelemetryHistogram::Accumulate(aID, aKey, aSample);
}

void
Accumulate(const char* name, uint32_t sample)
{
  TelemetryHistogram::Accumulate(name, sample);
}

void
Accumulate(const char *name, const nsCString& key, uint32_t sample)
{
  TelemetryHistogram::Accumulate(name, key, sample);
}

void
AccumulateTimeDelta(ID aHistogram, TimeStamp start, TimeStamp end)
{
  Accumulate(aHistogram,
             static_cast<uint32_t>((end - start).ToMilliseconds()));
}

void
ClearHistogram(ID aId)
{
  TelemetryHistogram::ClearHistogram(aId);
}

const char*
GetHistogramName(ID id)
{
  return TelemetryHistogram::GetHistogramName(id);
}

bool
CanRecordBase()
{
  return TelemetryHistogram::CanRecordBase();
}

bool
CanRecordExtended()
{
  return TelemetryHistogram::CanRecordExtended();
}

void
RecordSlowSQLStatement(const nsACString &statement,
                       const nsACString &dbName,
                       uint32_t delay)
{
  TelemetryImpl::RecordSlowStatement(statement, dbName, delay);
}

void
RecordWebrtcIceCandidates(const uint32_t iceCandidateBitmask,
                          const bool success, const bool loop)
{
  TelemetryImpl::RecordIceCandidates(iceCandidateBitmask, success, loop);
}

void Init()
{
  // Make the service manager hold a long-lived reference to the service
  nsCOMPtr<nsITelemetry> telemetryService =
    do_GetService("@mozilla.org/base/telemetry;1");
  MOZ_ASSERT(telemetryService);
}

#if defined(MOZ_ENABLE_PROFILER_SPS)
void RecordChromeHang(uint32_t duration,
                      ProcessedStack &aStack,
                      int32_t aSystemUptime,
                      int32_t aFirefoxUptime,
                      HangAnnotationsPtr aAnnotations)
{
  TelemetryImpl::RecordChromeHang(duration, aStack,
                                  aSystemUptime, aFirefoxUptime,
                                  Move(aAnnotations));
}
#endif

void RecordThreadHangStats(ThreadHangStats& aStats)
{
  TelemetryImpl::RecordThreadHangStats(aStats);
}


void
WriteFailedProfileLock(nsIFile* aProfileDir)
{
  nsCOMPtr<nsIFile> file;
  nsresult rv = GetFailedProfileLockFile(getter_AddRefs(file), aProfileDir);
  NS_ENSURE_SUCCESS_VOID(rv);
  int64_t fileSize = 0;
  rv = file->GetFileSize(&fileSize);
  // It's expected that the file might not exist yet
  if (NS_FAILED(rv) && rv != NS_ERROR_FILE_NOT_FOUND) {
    return;
  }
  nsCOMPtr<nsIFileStream> fileStream;
  rv = NS_NewLocalFileStream(getter_AddRefs(fileStream), file,
                             PR_RDWR | PR_CREATE_FILE, 0640);
  NS_ENSURE_SUCCESS_VOID(rv);
  NS_ENSURE_TRUE_VOID(fileSize <= kMaxFailedProfileLockFileSize);
  unsigned int failedLockCount = 0;
  if (fileSize > 0) {
    nsCOMPtr<nsIInputStream> inStream = do_QueryInterface(fileStream);
    NS_ENSURE_TRUE_VOID(inStream);
    if (!GetFailedLockCount(inStream, fileSize, failedLockCount)) {
      failedLockCount = 0;
    }
  }
  ++failedLockCount;
  nsAutoCString bufStr;
  bufStr.AppendInt(static_cast<int>(failedLockCount));
  nsCOMPtr<nsISeekableStream> seekStream = do_QueryInterface(fileStream);
  NS_ENSURE_TRUE_VOID(seekStream);
  // If we read in an existing failed lock count, we need to reset the file ptr
  if (fileSize > 0) {
    rv = seekStream->Seek(nsISeekableStream::NS_SEEK_SET, 0);
    NS_ENSURE_SUCCESS_VOID(rv);
  }
  nsCOMPtr<nsIOutputStream> outStream = do_QueryInterface(fileStream);
  uint32_t bytesLeft = bufStr.Length();
  const char* bytes = bufStr.get();
  do {
    uint32_t written = 0;
    rv = outStream->Write(bytes, bytesLeft, &written);
    if (NS_FAILED(rv)) {
      break;
    }
    bytes += written;
    bytesLeft -= written;
  } while (bytesLeft > 0);
  seekStream->SetEOF();
}

void
InitIOReporting(nsIFile* aXreDir)
{
  // Never initialize twice
  if (sTelemetryIOObserver) {
    return;
  }

  sTelemetryIOObserver = new TelemetryIOInterposeObserver(aXreDir);
  IOInterposer::Register(IOInterposeObserver::OpAllWithStaging,
                         sTelemetryIOObserver);
}

void
SetProfileDir(nsIFile* aProfD)
{
  if (!sTelemetryIOObserver || !aProfD) {
    return;
  }
  nsAutoString profDirPath;
  nsresult rv = aProfD->GetPath(profDirPath);
  if (NS_FAILED(rv)) {
    return;
  }
  sTelemetryIOObserver->AddPath(profDirPath, NS_LITERAL_STRING("{profile}"));
}

void CreateStatisticsRecorder()
{
  TelemetryHistogram::CreateStatisticsRecorder();
}

void DestroyStatisticsRecorder()
{
  TelemetryHistogram::DestroyStatisticsRecorder();
}

} // namespace Telemetry
} // namespace mozilla
