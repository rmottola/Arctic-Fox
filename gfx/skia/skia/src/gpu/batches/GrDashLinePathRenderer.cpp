/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrDashLinePathRenderer.h"

#include "GrGpu.h"
#include "effects/GrDashingEffect.h"

bool GrDashLinePathRenderer::onCanDrawPath(const CanDrawPathArgs& args) const {
    SkPoint pts[2];
    if (args.fStroke->isDashed() && args.fPath->isLine(pts)) {
        return GrDashingEffect::CanDrawDashLine(pts, *args.fStroke, *args.fViewMatrix);
    }
    return false;
}

bool GrDashLinePathRenderer::onDrawPath(const DrawPathArgs& args) {
    GR_AUDIT_TRAIL_AUTO_FRAME(args.fTarget->getAuditTrail(), "GrDashLinePathRenderer::onDrawPath");
    bool msaaIsEnabled = args.fPipelineBuilder->getRenderTarget()->isUnifiedMultisampled();
    SkPoint pts[2];
    SkAssertResult(args.fPath->isLine(pts));
    SkAutoTUnref<GrDrawBatch> batch(GrDashingEffect::CreateDashLineBatch(args.fColor,
                                                                         *args.fViewMatrix,
                                                                         pts,
                                                                         args.fAntiAlias,
                                                                         msaaIsEnabled,
                                                                         *args.fStroke));
    if (!batch) {
        return false;
    }

    args.fTarget->drawBatch(*args.fPipelineBuilder, batch);
    return true;
}
