/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
// vim:cindent:ts=2:et:sw=2:
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object for CSS "::backdrop" */

#include "nsBackdropFrame.h"

using namespace mozilla;

NS_IMPL_FRAMEARENA_HELPERS(nsBackdropFrame)

/* virtual */ nsIAtom*
nsBackdropFrame::GetType() const
{
  return nsGkAtoms::backdropFrame;
}

#ifdef DEBUG_FRAME_DUMP
nsresult
nsBackdropFrame::GetFrameName(nsAString& aResult) const
{
  return MakeFrameName(NS_LITERAL_STRING("Backdrop"), aResult);
}
#endif

/* virtual */ nsStyleContext*
nsBackdropFrame::GetParentStyleContext(nsIFrame** aProviderFrame) const
{
  // Style context of backdrop pseudo-element does not inherit from
  // any element, per the Fullscreen API spec.
  *aProviderFrame = nullptr;
  return nullptr;
}

/* virtual */ void
nsBackdropFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                  const nsRect& aDirtyRect,
                                  const nsDisplayListSet& aLists)
{
  DO_GLOBAL_REFLOW_COUNT_DSP("nsBackdropFrame");
  // We want this frame to always be there even if its display value is
  // none or contents so that we can respond to style change on it. To
  // support those values, we skip painting ourselves in those cases.
  auto display = StyleDisplay()->mDisplay;
  if (display == NS_STYLE_DISPLAY_NONE ||
      display == NS_STYLE_DISPLAY_CONTENTS) {
    return;
  }

  // The WebVR specific render path in nsContainerLayerComposite
  // results in an alternating frame strobing effect when an nsBackdropFrame is
  // rendered.
  // Currently, VR content is composed of a fullscreen canvas element that
  // is expected to cover the entire viewport so a backdrop should not
  // be necessary.
  if (GetStateBits() & NS_FRAME_HAS_VR_CONTENT) {
    return;
  }

  DisplayBorderBackgroundOutline(aBuilder, aLists);
}

/* virtual */ LogicalSize
nsBackdropFrame::ComputeAutoSize(nsRenderingContext *aRenderingContext,
                                 WritingMode aWM,
                                 const LogicalSize& aCBSize,
                                 nscoord aAvailableISize,
                                 const LogicalSize& aMargin,
                                 const LogicalSize& aBorder,
                                 const LogicalSize& aPadding,
                                 bool aShrinkWrap)
{
  // Note that this frame is a child of the viewport frame.
  LogicalSize result(aWM, 0xdeadbeef, NS_UNCONSTRAINEDSIZE);
  if (aShrinkWrap) {
    result.ISize(aWM) = 0;
  } else {
    result.ISize(aWM) = aAvailableISize - aMargin.ISize(aWM) -
                        aBorder.ISize(aWM) - aPadding.ISize(aWM);
  }
  return result;
}

/* virtual */ void
nsBackdropFrame::Reflow(nsPresContext* aPresContext,
                        nsHTMLReflowMetrics& aDesiredSize,
                        const nsHTMLReflowState& aReflowState,
                        nsReflowStatus& aStatus)
{
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsBackdropFrame");
  DISPLAY_REFLOW(aPresContext, this, aReflowState, aDesiredSize, aStatus);

  // Note that this frame is a child of the viewport frame.
  WritingMode wm = aReflowState.GetWritingMode();
  LogicalMargin borderPadding = aReflowState.ComputedLogicalBorderPadding();
  nscoord isize = aReflowState.ComputedISize() + borderPadding.IStartEnd(wm);
  nscoord bsize = aReflowState.ComputedBSize() + borderPadding.BStartEnd(wm);
  aDesiredSize.SetSize(wm, LogicalSize(wm, isize, bsize));
  aStatus = NS_FRAME_COMPLETE;
}
