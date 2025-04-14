/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrPath.h"

namespace {
// Verb count limit for generating path key from content of a volatile path.
// The value should accomodate at least simple rects and rrects.
static const int kSimpleVolatilePathVerbLimit = 10;

inline static bool compute_key_for_line_path(const SkPath& path, const GrStrokeInfo& stroke,
                                             GrUniqueKey* key) {
    SkPoint pts[2];
    if (!path.isLine(pts)) {
        return false;
    }
    static_assert((sizeof(pts) % sizeof(uint32_t)) == 0 && sizeof(pts) > sizeof(uint32_t),
                  "pts_needs_padding");

    const int kBaseData32Cnt = 1 + sizeof(pts) / sizeof(uint32_t);
    int strokeDataCnt = stroke.computeUniqueKeyFragmentData32Cnt();
    static const GrUniqueKey::Domain kOvalPathDomain = GrUniqueKey::GenerateDomain();
    GrUniqueKey::Builder builder(key, kOvalPathDomain, kBaseData32Cnt + strokeDataCnt);
    builder[0] = path.getFillType();
    memcpy(&builder[1], &pts, sizeof(pts));
    if (strokeDataCnt > 0) {
        stroke.asUniqueKeyFragment(&builder[kBaseData32Cnt]);
    }
    return true;
}

inline static bool compute_key_for_oval_path(const SkPath& path, const GrStrokeInfo& stroke,
                                             GrUniqueKey* key) {
    SkRect rect;
    // Point order is significant when dashing, so we cannot devolve to a rect key.
    if (stroke.isDashed() || !path.isOval(&rect)) {
        return false;
    }
    static_assert((sizeof(rect) % sizeof(uint32_t)) == 0 && sizeof(rect) > sizeof(uint32_t),
                  "rect_needs_padding");

    const int kBaseData32Cnt = 1 + sizeof(rect) / sizeof(uint32_t);
    int strokeDataCnt = stroke.computeUniqueKeyFragmentData32Cnt();
    static const GrUniqueKey::Domain kOvalPathDomain = GrUniqueKey::GenerateDomain();
    GrUniqueKey::Builder builder(key, kOvalPathDomain, kBaseData32Cnt + strokeDataCnt);
    builder[0] = path.getFillType();
    memcpy(&builder[1], &rect, sizeof(rect));
    if (strokeDataCnt > 0) {
        stroke.asUniqueKeyFragment(&builder[kBaseData32Cnt]);
    }
    return true;
}

// Encodes the full path data to the unique key for very small, volatile paths. This is typically
// hit when clipping stencils the clip stack. Intention is that this handles rects too, since
// SkPath::isRect seems to do non-trivial amount of work.
inline static bool compute_key_for_simple_path(const SkPath& path, const GrStrokeInfo& stroke,
                                               GrUniqueKey* key) {
    if (!path.isVolatile()) {
        return false;
    }
    // The check below should take care of negative values casted positive.
    const int verbCnt = path.countVerbs();
    if (verbCnt > kSimpleVolatilePathVerbLimit) {
        return false;
    }

    // If somebody goes wild with the constant, it might cause an overflow.
    static_assert(kSimpleVolatilePathVerbLimit <= 100,
                  "big_simple_volatile_path_verb_limit_may_cause_overflow");

    const int pointCnt = path.countPoints();
    if (pointCnt < 0) {
        SkASSERT(false);
        return false;
    }
    SkSTArray<16, SkScalar, true> conicWeights(16);
    if ((path.getSegmentMasks() & SkPath::kConic_SegmentMask) != 0) {
        SkPath::RawIter iter(path);
        SkPath::Verb verb;
        SkPoint points[4];
        while ((verb = iter.next(points)) != SkPath::kDone_Verb) {
            if (verb == SkPath::kConic_Verb) {
                conicWeights.push_back(iter.conicWeight());
            }
        }
    }

    const int conicWeightCnt = conicWeights.count();

    // Construct counts that align as uint32_t counts.
#define ARRAY_DATA32_COUNT(array_type, count) \
    static_cast<int>((((count) * sizeof(array_type) + sizeof(uint32_t) - 1) / sizeof(uint32_t)))

    const int verbData32Cnt = ARRAY_DATA32_COUNT(uint8_t, verbCnt);
    const int pointData32Cnt = ARRAY_DATA32_COUNT(SkPoint, pointCnt);
    const int conicWeightData32Cnt = ARRAY_DATA32_COUNT(SkScalar, conicWeightCnt);

#undef ARRAY_DATA32_COUNT

    // The unique key data is a "message" with following fragments:
    // 0) domain, key length, uint32_t for fill type and uint32_t for verbCnt
    //   (fragment 0, fixed size)
    // 1) verb, point data and conic weights (varying size)
    // 2) stroke data (varying size)

    const int baseData32Cnt = 2 + verbData32Cnt + pointData32Cnt + conicWeightData32Cnt;
    const int strokeDataCnt = stroke.computeUniqueKeyFragmentData32Cnt();
    static const GrUniqueKey::Domain kSimpleVolatilePathDomain = GrUniqueKey::GenerateDomain();
    GrUniqueKey::Builder builder(key, kSimpleVolatilePathDomain, baseData32Cnt + strokeDataCnt);
    int i = 0;
    builder[i++] = path.getFillType();

    // Serialize the verbCnt to make the whole message unambiguous.
    // We serialize two variable length fragments to the message:
    // * verbs, point data and conic weights (fragment 1)
    // * stroke data (fragment 2)
    // "Proof:"
    // Verb count establishes unambiguous verb data.
    // Verbs encode also point data size and conic weight size.
    // Thus the fragment 1 is unambiguous.
    // Unambiguous fragment 1 establishes unambiguous fragment 2, since the length of the message
    // has been established.

    builder[i++] = SkToU32(verbCnt); // The path limit is compile-asserted above, so the cast is ok.

    // Fill the last uint32_t with 0 first, since the last uint8_ts of the uint32_t may be
    // uninitialized. This does not produce ambiguous verb data, since we have serialized the exact
    // verb count.
    if (verbData32Cnt != static_cast<int>((verbCnt * sizeof(uint8_t) / sizeof(uint32_t)))) {
        builder[i + verbData32Cnt - 1] = 0;
    }
    path.getVerbs(reinterpret_cast<uint8_t*>(&builder[i]), verbCnt);
    i += verbData32Cnt;

    static_assert(((sizeof(SkPoint) % sizeof(uint32_t)) == 0) && sizeof(SkPoint) > sizeof(uint32_t),
                  "skpoint_array_needs_padding");

    // Here we assume getPoints does a memcpy, so that we do not need to worry about the alignment.
    path.getPoints(reinterpret_cast<SkPoint*>(&builder[i]), pointCnt);
    i += pointData32Cnt;

    if (conicWeightCnt > 0) {
        if (conicWeightData32Cnt != static_cast<int>(
                (conicWeightCnt * sizeof(SkScalar) / sizeof(uint32_t)))) {
            builder[i + conicWeightData32Cnt - 1] = 0;
        }
        memcpy(&builder[i], conicWeights.begin(), conicWeightCnt * sizeof(SkScalar));
        SkDEBUGCODE(i += conicWeightData32Cnt);
    }
    SkASSERT(i == baseData32Cnt);
    if (strokeDataCnt > 0) {
        stroke.asUniqueKeyFragment(&builder[baseData32Cnt]);
    }
    return true;
}

inline static void compute_key_for_general_path(const SkPath& path, const GrStrokeInfo& stroke,
                                                GrUniqueKey* key) {
    const int kBaseData32Cnt = 2;
    int strokeDataCnt = stroke.computeUniqueKeyFragmentData32Cnt();
    static const GrUniqueKey::Domain kGeneralPathDomain = GrUniqueKey::GenerateDomain();
    GrUniqueKey::Builder builder(key, kGeneralPathDomain, kBaseData32Cnt + strokeDataCnt);
    builder[0] = path.getGenerationID();
    builder[1] = path.getFillType();
    if (strokeDataCnt > 0) {
        stroke.asUniqueKeyFragment(&builder[kBaseData32Cnt]);
    }
}

}

void GrPath::ComputeKey(const SkPath& path, const GrStrokeInfo& stroke, GrUniqueKey* key,
                        bool* outIsVolatile) {
    if (compute_key_for_line_path(path, stroke, key)) {
        *outIsVolatile = false;
        return;
    }

    if (compute_key_for_oval_path(path, stroke, key)) {
        *outIsVolatile = false;
        return;
    }

    if (compute_key_for_simple_path(path, stroke, key)) {
        *outIsVolatile = false;
        return;
    }

    compute_key_for_general_path(path, stroke, key);
    *outIsVolatile = path.isVolatile();
}

#ifdef SK_DEBUG
bool GrPath::isEqualTo(const SkPath& path, const GrStrokeInfo& stroke) const {
    if (!fStroke.hasEqualEffect(stroke)) {
        return false;
    }

    // We treat same-rect ovals as identical - but only when not dashing.
    SkRect ovalBounds;
    if (!fStroke.isDashed() && fSkPath.isOval(&ovalBounds)) {
        SkRect otherOvalBounds;
        return path.isOval(&otherOvalBounds) && ovalBounds == otherOvalBounds;
    }

    return fSkPath == path;
}
#endif
