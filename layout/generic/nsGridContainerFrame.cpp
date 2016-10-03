/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code is subject to the terms of the Mozilla Public License
 * version 2.0 (the "License"). You can obtain a copy of the License at
 * http://mozilla.org/MPL/2.0/. */

/* rendering object for CSS "display: grid | inline-grid" */

#include "nsGridContainerFrame.h"

#include <algorithm> // for std::stable_sort
#include <limits>
#include "mozilla/Maybe.h"
#include "mozilla/PodOperations.h" // for PodZero
#include "nsAbsoluteContainingBlock.h"
#include "nsAlgorithm.h" // for clamped()
#include "nsAutoPtr.h"
#include "nsCSSAnonBoxes.h"
#include "nsCSSFrameConstructor.h"
#include "nsDataHashtable.h"
#include "nsDisplayList.h"
#include "nsHashKeys.h"
#include "nsIFrameInlines.h"
#include "nsPresContext.h"
#include "nsRenderingContext.h"
#include "nsReadableUtils.h"
#include "nsRuleNode.h"
#include "nsStyleContext.h"

using namespace mozilla;
typedef nsAbsoluteContainingBlock::AbsPosReflowFlags AbsPosReflowFlags;
typedef nsGridContainerFrame::TrackSize TrackSize;
const uint32_t nsGridContainerFrame::kTranslatedMaxLine =
  uint32_t(nsStyleGridLine::kMaxLine - nsStyleGridLine::kMinLine);
const uint32_t nsGridContainerFrame::kAutoLine = kTranslatedMaxLine + 3457U;
typedef nsTHashtable< nsPtrHashKey<nsIFrame> > FrameHashtable;

// https://drafts.csswg.org/css-align-3/#baseline-sharing-group
enum BaselineSharingGroup
{
  // NOTE Used as an array index so must be 0 and 1.
  eFirst = 0,
  eLast = 1,
};

static void
ReparentFrame(nsIFrame* aFrame, nsContainerFrame* aOldParent,
              nsContainerFrame* aNewParent)
{
  NS_ASSERTION(aOldParent == aFrame->GetParent(),
               "Parent not consistent with expectations");

  aFrame->SetParent(aNewParent);

  // When pushing and pulling frames we need to check for whether any
  // views need to be reparented
  nsContainerFrame::ReparentFrameView(aFrame, aOldParent, aNewParent);
}

static void
ReparentFrames(nsFrameList& aFrameList, nsContainerFrame* aOldParent,
               nsContainerFrame* aNewParent)
{
  for (auto f : aFrameList) {
    ReparentFrame(f, aOldParent, aNewParent);
  }
}

static nscoord
ClampToCSSMaxBSize(nscoord aSize, const nsHTMLReflowState* aReflowState)
{
  auto maxSize = aReflowState->ComputedMaxBSize();
  if (MOZ_UNLIKELY(maxSize != NS_UNCONSTRAINEDSIZE)) {
    MOZ_ASSERT(aReflowState->ComputedMinBSize() <= maxSize);
    aSize = std::min(aSize, maxSize);
  }
  return aSize;
}

// Same as above and set aStatus INCOMPLETE if aSize wasn't clamped.
// (If we clamp aSize it means our size is less than the break point,
// i.e. we're effectively breaking in our overflow, so we should leave
// aStatus as is (it will likely be set to OVERFLOW_INCOMPLETE later)).
static nscoord
ClampToCSSMaxBSize(nscoord aSize, const nsHTMLReflowState* aReflowState,
                   nsReflowStatus* aStatus)
{
  auto maxSize = aReflowState->ComputedMaxBSize();
  if (MOZ_UNLIKELY(maxSize != NS_UNCONSTRAINEDSIZE)) {
    MOZ_ASSERT(aReflowState->ComputedMinBSize() <= maxSize);
    if (aSize < maxSize) {
      NS_FRAME_SET_INCOMPLETE(*aStatus);
    } else {
      aSize = maxSize;
    }
  } else {
    NS_FRAME_SET_INCOMPLETE(*aStatus);
  }
  return aSize;
}

enum class GridLineSide
{
  eBeforeGridGap,
  eAfterGridGap,
};

struct nsGridContainerFrame::TrackSize
{
  void Initialize(nscoord aPercentageBasis,
                  const nsStyleCoord& aMinCoord,
                  const nsStyleCoord& aMaxCoord);
  bool IsFrozen() const { return mState & eFrozen; }
#ifdef DEBUG
  void Dump() const;
#endif
  enum StateBits : uint16_t {
    eAutoMinSizing =           0x1,
    eMinContentMinSizing =     0x2,
    eMaxContentMinSizing =     0x4,
    eMinOrMaxContentMinSizing = eMinContentMinSizing | eMaxContentMinSizing,
    eIntrinsicMinSizing = eMinOrMaxContentMinSizing | eAutoMinSizing,
    eFlexMinSizing =           0x8, // not really used ATM due to a spec change
    eAutoMaxSizing =          0x10,
    eMinContentMaxSizing =    0x20,
    eMaxContentMaxSizing =    0x40,
    eAutoOrMaxContentMaxSizing = eAutoMaxSizing | eMaxContentMaxSizing,
    eIntrinsicMaxSizing = eAutoOrMaxContentMaxSizing | eMinContentMaxSizing,
    eFlexMaxSizing =          0x80,
    eFrozen =                0x100,
    eSkipGrowUnlimited1 =    0x200,
    eSkipGrowUnlimited2 =    0x400,
    eSkipGrowUnlimited = eSkipGrowUnlimited1 | eSkipGrowUnlimited2,
    eBreakBefore =           0x800,
  };

  static bool IsMinContent(const nsStyleCoord& aCoord)
  {
    return aCoord.GetUnit() == eStyleUnit_Enumerated &&
      aCoord.GetIntValue() == NS_STYLE_GRID_TRACK_BREADTH_MIN_CONTENT;
  }

  nscoord mBase;
  nscoord mLimit;
  nscoord mPosition;  // zero until we apply 'align/justify-content'
  // mBaselineSubtreeSize is the size of a baseline-aligned subtree within
  // this track.  One subtree per baseline-sharing group (per track).
  nscoord mBaselineSubtreeSize[2];
  StateBits mState;
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(TrackSize::StateBits)

namespace mozilla {
template <>
struct IsPod<nsGridContainerFrame::TrackSize> : TrueType {};
}

void
nsGridContainerFrame::TrackSize::Initialize(nscoord aPercentageBasis,
                                            const nsStyleCoord& aMinCoord,
                                            const nsStyleCoord& aMaxCoord)
{
  MOZ_ASSERT(mBase == 0 && mLimit == 0 && mState == 0,
             "track size data is expected to be initialized to zero");
  auto minSizeUnit = aMinCoord.GetUnit();
  auto maxSizeUnit = aMaxCoord.GetUnit();
  if (aPercentageBasis == NS_UNCONSTRAINEDSIZE) {
    // https://drafts.csswg.org/css-grid/#valdef-grid-template-columns-percentage
    // "If the inline or block size of the grid container is indefinite,
    //  <percentage> values relative to that size are treated as 'auto'."
    if (aMinCoord.HasPercent()) {
      minSizeUnit = eStyleUnit_Auto;
    }
    if (aMaxCoord.HasPercent()) {
      maxSizeUnit = eStyleUnit_Auto;
    }
  }
  // http://dev.w3.org/csswg/css-grid/#algo-init
  switch (minSizeUnit) {
    case eStyleUnit_FlexFraction:
    case eStyleUnit_Auto:
      mState = eAutoMinSizing;
      break;
    case eStyleUnit_Enumerated:
      mState = IsMinContent(aMinCoord) ? eMinContentMinSizing
                                       : eMaxContentMinSizing;
      break;
    default:
      mBase = nsRuleNode::ComputeCoordPercentCalc(aMinCoord, aPercentageBasis);
  }
  switch (maxSizeUnit) {
    case eStyleUnit_Auto:
      mState |= eAutoMaxSizing;
      mLimit = NS_UNCONSTRAINEDSIZE;
      break;
    case eStyleUnit_Enumerated:
      mState |= IsMinContent(aMaxCoord) ? eMinContentMaxSizing
                                        : eMaxContentMaxSizing;
      mLimit = NS_UNCONSTRAINEDSIZE;
      break;
    case eStyleUnit_FlexFraction:
      mState |= eFlexMaxSizing;
      mLimit = mBase;
      break;
    default:
      mLimit = nsRuleNode::ComputeCoordPercentCalc(aMaxCoord, aPercentageBasis);
      if (mLimit < mBase) {
        mLimit = mBase;
      }
  }

  mBaselineSubtreeSize[BaselineSharingGroup::eFirst] = nscoord(0);
  mBaselineSubtreeSize[BaselineSharingGroup::eLast] = nscoord(0);
}

/**
 * Is aFrame1 a prev-continuation of aFrame2?
 */
static bool
IsPrevContinuationOf(nsIFrame* aFrame1, nsIFrame* aFrame2)
{
  nsIFrame* prev = aFrame2;
  while ((prev = prev->GetPrevContinuation())) {
    if (prev == aFrame1) {
      return true;
    }
  }
  return false;
}

/**
 * Moves all frames from aSrc into aDest such that the resulting aDest
 * is still sorted in document content order and continuation order.
 * Precondition: both |aSrc| and |aDest| must be sorted to begin with.
 * @param aCommonAncestor a hint for nsLayoutUtils::CompareTreePosition
 */
static void
MergeSortedFrameLists(nsFrameList& aDest, nsFrameList& aSrc,
                      nsIContent* aCommonAncestor)
{
  nsIFrame* dest = aDest.FirstChild();
  for (nsIFrame* src = aSrc.FirstChild(); src; ) {
    if (!dest) {
      aDest.AppendFrames(nullptr, aSrc);
      break;
    }
    int32_t result = nsLayoutUtils::CompareTreePosition(src->GetContent(),
                                                        dest->GetContent(),
                                                        aCommonAncestor);
    if (MOZ_UNLIKELY(result == 0)) {
      // NOTE: we get here when comparing ::before/::after for the same element.
      auto srcPseudo = src->GetContent()->NodeInfo()->NameAtom();
      if (MOZ_UNLIKELY(srcPseudo == nsGkAtoms::mozgeneratedcontentbefore)) {
        auto destPseudo = dest->GetContent()->NodeInfo()->NameAtom();
        if (MOZ_LIKELY(destPseudo != nsGkAtoms::mozgeneratedcontentbefore) ||
            ::IsPrevContinuationOf(src, dest)) {
          result = -1;
        }
      } else if (MOZ_UNLIKELY(srcPseudo == nsGkAtoms::mozgeneratedcontentafter)) {
        auto destPseudo = dest->GetContent()->NodeInfo()->NameAtom();
        if (MOZ_UNLIKELY(destPseudo == nsGkAtoms::mozgeneratedcontentafter) &&
            ::IsPrevContinuationOf(src, dest)) {
          result = -1;
        }
      } else if (::IsPrevContinuationOf(src, dest)) {
        result = -1;
      }
    }
    if (result < 0) {
      // src should come before dest
      nsIFrame* next = src->GetNextSibling();
      aSrc.RemoveFrame(src);
      aDest.InsertFrame(nullptr, dest->GetPrevSibling(), src);
      src = next;
    } else {
      dest = dest->GetNextSibling();
    }
  }
  MOZ_ASSERT(aSrc.IsEmpty());
}

static void
MergeSortedFrameListsFor(nsFrameList& aDest, nsFrameList& aSrc,
                         nsContainerFrame* aParent)
{
  MergeSortedFrameLists(aDest, aSrc, aParent->GetContent());
}

class nsGridContainerFrame::GridItemCSSOrderIterator
{
public:
  enum OrderState { eUnknownOrder, eKnownOrdered, eKnownUnordered };
  enum ChildFilter { eSkipPlaceholders, eIncludeAll };
  GridItemCSSOrderIterator(nsIFrame* aGridContainer,
                           nsIFrame::ChildListID aListID,
                           ChildFilter aFilter = eSkipPlaceholders,
                           OrderState aState = eUnknownOrder)
    : mChildren(aGridContainer->GetChildList(aListID))
    , mArrayIndex(0)
    , mGridItemIndex(0)
    , mSkipPlaceholders(aFilter == eSkipPlaceholders)
#ifdef DEBUG
    , mGridContainer(aGridContainer)
    , mListID(aListID)
#endif
  {
    size_t count = 0;
    bool isOrdered = aState != eKnownUnordered;
    if (aState == eUnknownOrder) {
      auto maxOrder = std::numeric_limits<int32_t>::min();
      for (nsFrameList::Enumerator e(mChildren); !e.AtEnd(); e.Next()) {
        ++count;
        int32_t order = e.get()->StylePosition()->mOrder;
        if (order < maxOrder) {
          isOrdered = false;
          break;
        }
        maxOrder = order;
      }
    }
    if (isOrdered) {
      mEnumerator.emplace(mChildren);
    } else {
      count *= 2; // XXX somewhat arbitrary estimate for now...
      mArray.emplace(count);
      for (nsFrameList::Enumerator e(mChildren); !e.AtEnd(); e.Next()) {
        mArray->AppendElement(e.get());
      }
      // XXX replace this with nsTArray::StableSort when bug 1147091 is fixed.
      std::stable_sort(mArray->begin(), mArray->end(), IsCSSOrderLessThan);
    }

    if (mSkipPlaceholders) {
      SkipPlaceholders();
    }
  }

  nsIFrame* operator*() const
  {
    MOZ_ASSERT(!AtEnd());
    if (mEnumerator) {
      return mEnumerator->get();
    }
    return (*mArray)[mArrayIndex];
  }

  /**
   * Return the child index of the current item, placeholders not counted.
   * It's forbidden to call this method when the current frame is placeholder.
   */
  size_t GridItemIndex() const
  {
    MOZ_ASSERT(!AtEnd());
    MOZ_ASSERT((**this)->GetType() != nsGkAtoms::placeholderFrame,
               "MUST not call this when at a placeholder");
    return mGridItemIndex;
  }

  /**
   * Skip over placeholder children.
   */
  void SkipPlaceholders()
  {
    if (mEnumerator) {
      for (; !mEnumerator->AtEnd(); mEnumerator->Next()) {
        nsIFrame* child = mEnumerator->get();
        if (child->GetType() != nsGkAtoms::placeholderFrame) {
          return;
        }
      }
    } else {
      for (; mArrayIndex < mArray->Length(); ++mArrayIndex) {
        nsIFrame* child = (*mArray)[mArrayIndex];
        if (child->GetType() != nsGkAtoms::placeholderFrame) {
          return;
        }
      }
    }
  }

  bool AtEnd() const
  {
    MOZ_ASSERT(mEnumerator || mArrayIndex <= mArray->Length());
    return mEnumerator ? mEnumerator->AtEnd() : mArrayIndex >= mArray->Length();
  }

  void Next()
  {
#ifdef DEBUG
    MOZ_ASSERT(!AtEnd());
    nsFrameList list = mGridContainer->GetChildList(mListID);
    MOZ_ASSERT(list.FirstChild() == mChildren.FirstChild() &&
               list.LastChild() == mChildren.LastChild(),
               "the list of child frames must not change while iterating!");
#endif
    if (mSkipPlaceholders ||
        (**this)->GetType() != nsGkAtoms::placeholderFrame) {
      ++mGridItemIndex;
    }
    if (mEnumerator) {
      mEnumerator->Next();
    } else {
      ++mArrayIndex;
    }
    if (mSkipPlaceholders) {
      SkipPlaceholders();
    }
  }

  void Reset(ChildFilter aFilter = eSkipPlaceholders)
  {
    if (mEnumerator) {
      mEnumerator.reset();
      mEnumerator.emplace(mChildren);
    } else {
      mArrayIndex = 0;
    }
    mGridItemIndex = 0;
    mSkipPlaceholders = aFilter == eSkipPlaceholders;
    if (mSkipPlaceholders) {
      SkipPlaceholders();
    }
  }

  bool ItemsAreAlreadyInOrder() const { return mEnumerator.isSome(); }

private:
  static bool IsCSSOrderLessThan(nsIFrame* const& a, nsIFrame* const& b)
    { return a->StylePosition()->mOrder < b->StylePosition()->mOrder; }

  nsFrameList mChildren;
  // Used if child list is already in ascending 'order'.
  Maybe<nsFrameList::Enumerator> mEnumerator;
  // Used if child list is *not* in ascending 'order'.
  Maybe<nsTArray<nsIFrame*>> mArray;
  size_t mArrayIndex;
  // The index of the current grid item (placeholders excluded).
  size_t mGridItemIndex;
  // Skip placeholder children in the iteration?
  bool mSkipPlaceholders;
#ifdef DEBUG
  nsIFrame* mGridContainer;
  nsIFrame::ChildListID mListID;
#endif
};


/**
 * A LineRange can be definite or auto - when it's definite it represents
 * a consecutive set of tracks between a starting line and an ending line.
 * Before it's definite it can also represent an auto position with a span,
 * where mStart == kAutoLine and mEnd is the (non-zero positive) span.
 * For normal-flow items, the invariant mStart < mEnd holds when both
 * lines are definite.
 *
 * For abs.pos. grid items, mStart and mEnd may both be kAutoLine, meaning
 * "attach this side to the grid container containing block edge".
 * Additionally, mStart <= mEnd holds when both are definite (non-kAutoLine),
 * i.e. the invariant is slightly relaxed compared to normal flow items.
 */
struct nsGridContainerFrame::LineRange
{
 LineRange(int32_t aStart, int32_t aEnd)
   : mUntranslatedStart(aStart), mUntranslatedEnd(aEnd)
  {
#ifdef DEBUG
    if (!IsAutoAuto()) {
      if (IsAuto()) {
        MOZ_ASSERT(aEnd >= nsStyleGridLine::kMinLine &&
                   aEnd <= nsStyleGridLine::kMaxLine, "invalid span");
      } else {
        MOZ_ASSERT(aStart >= nsStyleGridLine::kMinLine &&
                   aStart <= nsStyleGridLine::kMaxLine, "invalid start line");
        MOZ_ASSERT(aEnd == int32_t(kAutoLine) ||
                   (aEnd >= nsStyleGridLine::kMinLine &&
                    aEnd <= nsStyleGridLine::kMaxLine), "invalid end line");
      }
    }
#endif
  }
  bool IsAutoAuto() const { return mStart == kAutoLine && mEnd == kAutoLine; }
  bool IsAuto() const { return mStart == kAutoLine; }
  bool IsDefinite() const { return mStart != kAutoLine; }
  uint32_t Extent() const
  {
    MOZ_ASSERT(mEnd != kAutoLine, "Extent is undefined for abs.pos. 'auto'");
    if (IsAuto()) {
      MOZ_ASSERT(mEnd >= 1 && mEnd < uint32_t(nsStyleGridLine::kMaxLine),
                 "invalid span");
      return mEnd;
    }
    return mEnd - mStart;
  }
  /**
   * Resolve this auto range to start at aStart, making it definite.
   * Precondition: this range IsAuto()
   */
  void ResolveAutoPosition(uint32_t aStart, uint32_t aExplicitGridOffset)
  {
    MOZ_ASSERT(IsAuto(), "Why call me?");
    mStart = aStart;
    mEnd += aStart;
    // Clamping to where kMaxLine is in the explicit grid, per
    // http://dev.w3.org/csswg/css-grid/#overlarge-grids :
    uint32_t translatedMax = aExplicitGridOffset + nsStyleGridLine::kMaxLine;
    if (MOZ_UNLIKELY(mStart >= translatedMax)) {
      mEnd = translatedMax;
      mStart = mEnd - 1;
    } else if (MOZ_UNLIKELY(mEnd > translatedMax)) {
      mEnd = translatedMax;
    }
  }
  /**
   * Translate the lines to account for (empty) removed tracks.  This method
   * is only for grid items and should only be called after placement.
   * aNumRemovedTracks contains a count for each line in the grid how many
   * tracks were removed between the start of the grid and that line.
   */
  void AdjustForRemovedTracks(const nsTArray<uint32_t>& aNumRemovedTracks)
  {
    MOZ_ASSERT(mStart != kAutoLine, "invalid resolved line for a grid item");
    MOZ_ASSERT(mEnd != kAutoLine, "invalid resolved line for a grid item");
    uint32_t numRemovedTracks = aNumRemovedTracks[mStart];
    MOZ_ASSERT(numRemovedTracks == aNumRemovedTracks[mEnd],
               "tracks that a grid item spans can't be removed");
    mStart -= numRemovedTracks;
    mEnd -= numRemovedTracks;
  }
  /**
   * Translate the lines to account for (empty) removed tracks.  This method
   * is only for abs.pos. children and should only be called after placement.
   * Same as for in-flow items, but we don't touch 'auto' lines here and we
   * also need to adjust areas that span into the removed tracks.
   */
  void AdjustAbsPosForRemovedTracks(const nsTArray<uint32_t>& aNumRemovedTracks)
  {
    if (mStart != nsGridContainerFrame::kAutoLine) {
      mStart -= aNumRemovedTracks[mStart];
    }
    if (mEnd != nsGridContainerFrame::kAutoLine) {
      MOZ_ASSERT(mStart == nsGridContainerFrame::kAutoLine ||
                 mEnd > mStart, "invalid line range");
      mEnd -= aNumRemovedTracks[mEnd];
    }
    if (mStart == mEnd) {
      mEnd = nsGridContainerFrame::kAutoLine;
    }
  }
  /**
   * Return the contribution of this line range for step 2 in
   * http://dev.w3.org/csswg/css-grid/#auto-placement-algo
   */
  uint32_t HypotheticalEnd() const { return mEnd; }
  /**
   * Given an array of track sizes, return the starting position and length
   * of the tracks in this line range.
   */
  void ToPositionAndLength(const nsTArray<TrackSize>& aTrackSizes,
                           nscoord* aPos, nscoord* aLength) const;
  /**
   * Given an array of track sizes, return the length of the tracks in this
   * line range.
   */
  nscoord ToLength(const nsTArray<TrackSize>& aTrackSizes) const;
  /**
   * Given an array of track sizes and a grid origin coordinate, adjust the
   * abs.pos. containing block along an axis given by aPos and aLength.
   * aPos and aLength should already be initialized to the grid container
   * containing block for this axis before calling this method.
   */
  void ToPositionAndLengthForAbsPos(const Tracks& aTracks,
                                    nscoord aGridOrigin,
                                    nscoord* aPos, nscoord* aLength) const;

  /**
   * @note We'll use the signed member while resolving definite positions
   * to line numbers (1-based), which may become negative for implicit lines
   * to the top/left of the explicit grid.  PlaceGridItems() then translates
   * the whole grid to a 0,0 origin and we'll use the unsigned member from
   * there on.
   */
  union {
    uint32_t mStart;
    int32_t mUntranslatedStart;
  };
  union {
    uint32_t mEnd;
    int32_t mUntranslatedEnd;
  };
protected:
  LineRange() {}
};

/**
 * Helper class to construct a LineRange from translated lines.
 * The ctor only accepts translated definite line numbers.
 */
struct nsGridContainerFrame::TranslatedLineRange : public LineRange
{
  TranslatedLineRange(uint32_t aStart, uint32_t aEnd)
  {
    MOZ_ASSERT(aStart < aEnd && aEnd <= kTranslatedMaxLine);
    mStart = aStart;
    mEnd = aEnd;
  }
};

/**
 * A GridArea is the area in the grid for a grid item.
 * The area is represented by two LineRanges, both of which can be auto
 * (@see LineRange) in intermediate steps while the item is being placed.
 * @see PlaceGridItems
 */
struct nsGridContainerFrame::GridArea
{
  GridArea(const LineRange& aCols, const LineRange& aRows)
    : mCols(aCols), mRows(aRows) {}
  bool IsDefinite() const { return mCols.IsDefinite() && mRows.IsDefinite(); }
  LineRange mCols;
  LineRange mRows;
};

struct nsGridContainerFrame::GridItemInfo
{
  /**
   * Item state per axis.
   */
  enum StateBits : uint8_t {
    eIsFlexing =            0x1, // does the item span a flex track?
    eFirstBaseline =        0x2, // participate in first-baseline alignment?
    // ditto last-baseline, mutually exclusive w. eFirstBaseline
    eLastBaseline =         0x4,
    eIsBaselineAligned = eFirstBaseline | eLastBaseline,
    // One of e[Self|Content]Baseline is set when eIsBaselineAligned is true
    eSelfBaseline =         0x8, // is it *-self:[last-]baseline alignment?
    // Ditto *-content:[last-]baseline. Mutually exclusive w. eSelfBaseline.
    eContentBaseline =     0x10,
    eAllBaselineBits = eIsBaselineAligned | eSelfBaseline | eContentBaseline,
  };

  explicit GridItemInfo(nsIFrame* aFrame,
                        const GridArea& aArea)
    : mFrame(aFrame)
    , mArea(aArea)
  {
    mState[eLogicalAxisBlock] = StateBits(0);
    mState[eLogicalAxisInline] = StateBits(0);
    mBaselineOffset[eLogicalAxisBlock] = nscoord(0);
    mBaselineOffset[eLogicalAxisInline] = nscoord(0);
  }

  /**
   * If the item is [align|justify]-self:[last-]baseline aligned in the given
   * axis then set aBaselineOffset to the baseline offset and return aAlign.
   * Otherwise, return a fallback alignment.
   */
  uint8_t GetSelfBaseline(uint8_t aAlign, LogicalAxis aAxis,
                          nscoord* aBaselineOffset) const
  {
    MOZ_ASSERT(aAlign == NS_STYLE_ALIGN_BASELINE ||
               aAlign == NS_STYLE_ALIGN_LAST_BASELINE);
    if (!(mState[aAxis] & eSelfBaseline)) {
      return aAlign == NS_STYLE_ALIGN_BASELINE ? NS_STYLE_ALIGN_SELF_START
                                               : NS_STYLE_ALIGN_SELF_END;
    }
    *aBaselineOffset = mBaselineOffset[aAxis];
    return aAlign;
  }

#ifdef DEBUG
  void Dump() const;
#endif

  static bool IsStartRowLessThan(const GridItemInfo* a, const GridItemInfo* b)
  {
    return a->mArea.mRows.mStart < b->mArea.mRows.mStart;
  }

  nsIFrame* const mFrame;
  GridArea mArea;
  // Offset from the margin edge to the baseline (LogicalAxis index).  It's from
  // the start edge when eFirstBaseline is set, end edge otherwise. It's mutable
  // since we update the value fairly late (just before reflowing the item).
  mutable nscoord mBaselineOffset[2];
  StateBits mState[2]; // state bits per axis (LogicalAxis index)
  static_assert(mozilla::eLogicalAxisBlock == 0, "unexpected index value");
  static_assert(mozilla::eLogicalAxisInline == 1, "unexpected index value");
};

using GridItemInfo = nsGridContainerFrame::GridItemInfo;
using ItemState = GridItemInfo::StateBits;
MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(ItemState)

#ifdef DEBUG
void
nsGridContainerFrame::GridItemInfo::Dump() const
{
  auto Dump1 = [this] (const char* aMsg, LogicalAxis aAxis) {
    auto state = mState[aAxis];
    if (!state) {
      return;
    }
    printf("%s", aMsg);
    if (state & ItemState::eIsFlexing) {
      printf("flexing ");
    }
    if (state & ItemState::eFirstBaseline) {
      printf("baseline %s-alignment ",
             (state & ItemState::eSelfBaseline) ? "self" : "content");
    }
    if (state & ItemState::eLastBaseline) {
      printf("last-baseline %s-alignment ",
             (state & ItemState::eSelfBaseline) ? "self" : "content");
    }
    if (state & ItemState::eIsBaselineAligned) {
      printf("%.2fpx", NSAppUnitsToFloatPixels(mBaselineOffset[aAxis],
                                               AppUnitsPerCSSPixel()));
    }
    printf("\n");
  };
  printf("grid-row: %d %d\n", mArea.mRows.mStart, mArea.mRows.mEnd);
  Dump1("  grid block-axis: ", eLogicalAxisBlock);
  printf("grid-column: %d %d\n", mArea.mCols.mStart, mArea.mCols.mEnd);
  Dump1("  grid inline-axis: ", eLogicalAxisInline);
}
#endif

/**
 * Utility class to find line names.  It provides an interface to lookup line
 * names with a dynamic number of repeat(auto-fill/fit) tracks taken into
 * account.
 */
class MOZ_STACK_CLASS nsGridContainerFrame::LineNameMap
{
public:
  /**
   * Create a LineNameMap.
   * @param aGridTemplate is the grid-template-rows/columns data for this axis
   * @param aNumRepeatTracks the number of actual tracks associated with
   *   a repeat(auto-fill/fit) track (zero or more), or zero if there is no
   *   specified repeat(auto-fill/fit) track
   */
  LineNameMap(const nsStyleGridTemplate& aGridTemplate,
              uint32_t                   aNumRepeatTracks)
    : mLineNameLists(aGridTemplate.mLineNameLists)
    , mRepeatAutoLineNameListBefore(aGridTemplate.mRepeatAutoLineNameListBefore)
    , mRepeatAutoLineNameListAfter(aGridTemplate.mRepeatAutoLineNameListAfter)
    , mRepeatAutoStart(aGridTemplate.HasRepeatAuto() ?
                         aGridTemplate.mRepeatAutoIndex : 0)
    , mRepeatAutoEnd(mRepeatAutoStart + aNumRepeatTracks)
    , mRepeatEndDelta(aGridTemplate.HasRepeatAuto() ?
                        int32_t(aNumRepeatTracks) - 1 :
                        0)
    , mTemplateLinesEnd(mLineNameLists.Length() + mRepeatEndDelta)
    , mHasRepeatAuto(aGridTemplate.HasRepeatAuto())
  {
    MOZ_ASSERT(mHasRepeatAuto || aNumRepeatTracks == 0);
    MOZ_ASSERT(mRepeatAutoStart <= mLineNameLists.Length());
    MOZ_ASSERT(!mHasRepeatAuto || mLineNameLists.Length() >= 2);
  }

  /**
   * Find the aNth occurrence of aName, searching forward if aNth is positive,
   * and in reverse if aNth is negative (aNth == 0 is invalid), starting from
   * aFromIndex (not inclusive), and return a 1-based line number.
   * Also take into account there is an unconditional match at aImplicitLine
   * unless it's zero.
   * Return zero if aNth occurrences can't be found.  In that case, aNth has
   * been decremented with the number of occurrences that were found (if any).
   *
   * E.g. to search for "A 2" forward from the start of the grid: aName is "A"
   * aNth is 2 and aFromIndex is zero.  To search for "A -2", aNth is -2 and
   * aFromIndex is ExplicitGridEnd + 1 (which is the line "before" the last
   * line when we're searching in reverse).  For "span A 2", aNth is 2 when
   * used on a grid-[row|column]-end property and -2 for a *-start property,
   * and aFromIndex is the line (which we should skip) on the opposite property.
   */
  uint32_t FindNamedLine(const nsString& aName, int32_t* aNth,
                         uint32_t aFromIndex, uint32_t aImplicitLine) const
  {
    MOZ_ASSERT(aNth && *aNth != 0);
    if (*aNth > 0) {
      return FindLine(aName, aNth, aFromIndex, aImplicitLine);
    }
    int32_t nth = -*aNth;
    int32_t line = RFindLine(aName, &nth, aFromIndex, aImplicitLine);
    *aNth = -nth;
    return line;
  }

private:
  /**
   * @see FindNamedLine, this function searches forward.
   */
  uint32_t FindLine(const nsString& aName, int32_t* aNth,
                    uint32_t aFromIndex, uint32_t aImplicitLine) const
  {
    MOZ_ASSERT(aNth && *aNth > 0);
    int32_t nth = *aNth;
    const uint32_t end = mTemplateLinesEnd;
    uint32_t line;
    uint32_t i = aFromIndex;
    for (; i < end; i = line) {
      line = i + 1;
      if (line == aImplicitLine || Contains(i, aName)) {
        if (--nth == 0) {
          return line;
        }
      }
    }
    if (aImplicitLine > i) {
      // aImplicitLine is after the lines we searched above so it's last.
      // (grid-template-areas has more tracks than grid-template-[rows|columns])
      if (--nth == 0) {
        return aImplicitLine;
      }
    }
    MOZ_ASSERT(nth > 0, "should have returned a valid line above already");
    *aNth = nth;
    return 0;
  }

  /**
   * @see FindNamedLine, this function searches in reverse.
   */
  uint32_t RFindLine(const nsString& aName, int32_t* aNth,
                     uint32_t aFromIndex, uint32_t aImplicitLine) const
  {
    MOZ_ASSERT(aNth && *aNth > 0);
    if (MOZ_UNLIKELY(aFromIndex == 0)) {
      return 0; // There are no named lines beyond the start of the explicit grid.
    }
    --aFromIndex; // (shift aFromIndex so we can treat it as inclusive)
    int32_t nth = *aNth;
    // The implicit line may be beyond the explicit grid so we match
    // this line first if it's within the mTemplateLinesEnd..aFromIndex range.
    const uint32_t end = mTemplateLinesEnd;
    if (aImplicitLine > end && aImplicitLine < aFromIndex) {
      if (--nth == 0) {
        return aImplicitLine;
      }
    }
    for (uint32_t i = std::min(aFromIndex, end); i; --i) {
      if (i == aImplicitLine || Contains(i - 1, aName)) {
        if (--nth == 0) {
          return i;
        }
      }
    }
    MOZ_ASSERT(nth > 0, "should have returned a valid line above already");
    *aNth = nth;
    return 0;
  }

  // Return true if aName exists at aIndex.
  bool Contains(uint32_t aIndex, const nsString& aName) const
  {
    if (!mHasRepeatAuto) {
      return mLineNameLists[aIndex].Contains(aName);
    }
    if (aIndex < mRepeatAutoEnd && aIndex >= mRepeatAutoStart &&
        mRepeatAutoLineNameListBefore.Contains(aName)) {
      return true;
    }
    if (aIndex <= mRepeatAutoEnd && aIndex > mRepeatAutoStart &&
        mRepeatAutoLineNameListAfter.Contains(aName)) {
      return true;
    }
    if (aIndex <= mRepeatAutoStart) {
      return mLineNameLists[aIndex].Contains(aName) ||
             (aIndex == mRepeatAutoEnd &&
              mLineNameLists[aIndex + 1].Contains(aName));
    }
    return aIndex >= mRepeatAutoEnd &&
           mLineNameLists[aIndex - mRepeatEndDelta].Contains(aName);
  }

  // Some style data references, for easy access.
  const nsTArray<nsTArray<nsString>>& mLineNameLists;
  const nsTArray<nsString>& mRepeatAutoLineNameListBefore;
  const nsTArray<nsString>& mRepeatAutoLineNameListAfter;
  // The index of the repeat(auto-fill/fit) track, or zero if there is none.
  const uint32_t mRepeatAutoStart;
  // The (hypothetical) index of the last such repeat() track.
  const uint32_t mRepeatAutoEnd;
  // The difference between mTemplateLinesEnd and mLineNameLists.Length().
  const int32_t mRepeatEndDelta;
  // The end of the line name lists with repeat(auto-fill/fit) tracks accounted
  // for.  It is equal to mLineNameLists.Length() when a repeat() track
  // generates one track (making mRepeatEndDelta == 0).
  const uint32_t mTemplateLinesEnd;
  // True if there is a specified repeat(auto-fill/fit) track.
  const bool mHasRepeatAuto;
};

/**
 * Encapsulates CSS track-sizing functions.
 */
struct nsGridContainerFrame::TrackSizingFunctions
{
  TrackSizingFunctions(const nsStyleGridTemplate& aGridTemplate,
                       const nsStyleCoord&        aAutoMinSizing,
                       const nsStyleCoord&        aAutoMaxSizing)
    : mMinSizingFunctions(aGridTemplate.mMinTrackSizingFunctions)
    , mMaxSizingFunctions(aGridTemplate.mMaxTrackSizingFunctions)
    , mAutoMinSizing(aAutoMinSizing)
    , mAutoMaxSizing(aAutoMaxSizing)
    , mExplicitGridOffset(0)
    , mRepeatAutoStart(aGridTemplate.HasRepeatAuto() ?
                         aGridTemplate.mRepeatAutoIndex : 0)
    , mRepeatAutoEnd(mRepeatAutoStart)
    , mRepeatEndDelta(0)
    , mHasRepeatAuto(aGridTemplate.HasRepeatAuto())
  {
    MOZ_ASSERT(mMinSizingFunctions.Length() == mMaxSizingFunctions.Length());
    MOZ_ASSERT(!mHasRepeatAuto ||
               (mMinSizingFunctions.Length() >= 1 &&
                mRepeatAutoStart < mMinSizingFunctions.Length()));
  }

  /**
   * Initialize the number of auto-fill/fit tracks to use and return that.
   * (zero if no auto-fill/fit track was specified)
   */
  uint32_t InitRepeatTracks(const nsStyleCoord& aGridGap, nscoord aMinSize,
                            nscoord aSize, nscoord aMaxSize)
  {
    uint32_t repeatTracks =
      CalculateRepeatFillCount(aGridGap, aMinSize, aSize, aMaxSize);
    SetNumRepeatTracks(repeatTracks);
    return repeatTracks;
  }

  uint32_t CalculateRepeatFillCount(const nsStyleCoord& aGridGap,
                                    nscoord aMinSize,
                                    nscoord aSize,
                                    nscoord aMaxSize) const
  {
    if (!mHasRepeatAuto) {
      return 0;
    }
    // Spec quotes are from https://drafts.csswg.org/css-grid/#repeat-notation
    const uint32_t numTracks = mMinSizingFunctions.Length();
    MOZ_ASSERT(numTracks >= 1, "expected at least the repeat() track");
    nscoord maxFill = aSize != NS_UNCONSTRAINEDSIZE ? aSize : aMaxSize;
    if (maxFill == NS_UNCONSTRAINEDSIZE && aMinSize == NS_UNCONSTRAINEDSIZE) {
      // "Otherwise, the specified track list repeats only once."
      return 1;
    }
    nscoord repeatTrackSize = 0;
    // Note that the repeat() track size is included in |sum| in this loop.
    nscoord sum = 0;
    for (uint32_t i = 0; i < numTracks; ++i) {
      // "treating each track as its max track sizing function if that is
      // definite or as its minimum track sizing function otherwise"
      // https://drafts.csswg.org/css-grid/#valdef-repeat-auto-fill
      const auto& maxCoord = mMaxSizingFunctions[i];
      const auto* coord = &maxCoord;
      if (!coord->IsCoordPercentCalcUnit()) {
        coord = &mMinSizingFunctions[i];
        if (!coord->IsCoordPercentCalcUnit()) {
          return 1;
        }
      }
      nscoord trackSize = nsRuleNode::ComputeCoordPercentCalc(*coord, aSize);
      if (i == mRepeatAutoStart) {
        // Use a minimum 1px for the repeat() track-size.
        if (trackSize < AppUnitsPerCSSPixel()) {
          trackSize = AppUnitsPerCSSPixel();
        }
        repeatTrackSize = trackSize;
      }
      sum += trackSize;
    }
    nscoord gridGap =
      std::max(nscoord(0), nsRuleNode::ComputeCoordPercentCalc(aGridGap, aSize));
    if (numTracks > 1) {
      // Add grid-gaps for all the tracks including the repeat() track.
      sum += gridGap * (numTracks - 1);
    }
    nscoord available = maxFill != NS_UNCONSTRAINEDSIZE ? maxFill : aMinSize;
    nscoord spaceToFill = available - sum;
    if (spaceToFill <= 0) {
      // "if any number of repetitions would overflow, then 1 repetition"
      return 1;
    }
    // Calculate the max number of tracks that fits without overflow.
    uint32_t numRepeatTracks = (spaceToFill / (repeatTrackSize + gridGap)) + 1;
    if (maxFill == NS_UNCONSTRAINEDSIZE) {
      // "Otherwise, if the grid container has a definite min size in
      // the relevant axis, the number of repetitions is the largest possible
      // positive integer that fulfills that minimum requirement."
      ++numRepeatTracks; // one more to ensure the grid is at least min-size
    }
    // Clamp the number of repeat tracks so that the last line <= kMaxLine.
    // (note that |numTracks| already includes one repeat() track)
    const uint32_t maxRepeatTracks = nsStyleGridLine::kMaxLine - numTracks;
    return std::min(numRepeatTracks, maxRepeatTracks);
  }

  /**
   * Compute the explicit grid end line number (in a zero-based grid).
   * @param aGridTemplateAreasEnd 'grid-template-areas' end line in this axis
   */
  uint32_t ComputeExplicitGridEnd(uint32_t aGridTemplateAreasEnd)
  {
    uint32_t end = NumExplicitTracks() + 1;
    end = std::max(end, aGridTemplateAreasEnd);
    end = std::min(end, uint32_t(nsStyleGridLine::kMaxLine));
    return end;
  }

  const nsStyleCoord& MinSizingFor(uint32_t aTrackIndex) const
  {
    if (MOZ_UNLIKELY(aTrackIndex < mExplicitGridOffset)) {
      return mAutoMinSizing;
    }
    uint32_t index = aTrackIndex - mExplicitGridOffset;
    if (index >= mRepeatAutoStart) {
      if (index < mRepeatAutoEnd) {
        return mMinSizingFunctions[mRepeatAutoStart];
      }
      index -= mRepeatEndDelta;
    }
    return index < mMinSizingFunctions.Length() ?
      mMinSizingFunctions[index] : mAutoMinSizing;
  }
  const nsStyleCoord& MaxSizingFor(uint32_t aTrackIndex) const
  {
    if (MOZ_UNLIKELY(aTrackIndex < mExplicitGridOffset)) {
      return mAutoMaxSizing;
    }
    uint32_t index = aTrackIndex - mExplicitGridOffset;
    if (index >= mRepeatAutoStart) {
      if (index < mRepeatAutoEnd) {
        return mMaxSizingFunctions[mRepeatAutoStart];
      }
      index -= mRepeatEndDelta;
    }
    return index < mMaxSizingFunctions.Length() ?
      mMaxSizingFunctions[index] : mAutoMaxSizing;
  }
  uint32_t NumExplicitTracks() const
  {
    return mMinSizingFunctions.Length() + mRepeatEndDelta;
  }
  uint32_t NumRepeatTracks() const
  {
    return mRepeatAutoEnd - mRepeatAutoStart;
  }
  void SetNumRepeatTracks(uint32_t aNumRepeatTracks)
  {
    MOZ_ASSERT(mHasRepeatAuto || aNumRepeatTracks == 0);
    mRepeatAutoEnd = mRepeatAutoStart + aNumRepeatTracks;
    mRepeatEndDelta = mHasRepeatAuto ?
                        int32_t(aNumRepeatTracks) - 1 :
                        0;
  }

  // Some style data references, for easy access.
  const nsTArray<nsStyleCoord>& mMinSizingFunctions;
  const nsTArray<nsStyleCoord>& mMaxSizingFunctions;
  const nsStyleCoord& mAutoMinSizing;
  const nsStyleCoord& mAutoMaxSizing;
  // Offset from the start of the implicit grid to the first explicit track.
  uint32_t mExplicitGridOffset;
  // The index of the repeat(auto-fill/fit) track, or zero if there is none.
  const uint32_t mRepeatAutoStart;
  // The (hypothetical) index of the last such repeat() track.
  uint32_t mRepeatAutoEnd;
  // The difference between mExplicitGridEnd and mMinSizingFunctions.Length().
  int32_t mRepeatEndDelta;
  // True if there is a specified repeat(auto-fill/fit) track.
  const bool mHasRepeatAuto;
};

/**
 * State for the tracks in one dimension.
 */
struct nsGridContainerFrame::Tracks
{
  explicit Tracks(LogicalAxis aAxis)
    : mAxis(aAxis)
  {
    mBaselineSubtreeAlign[BaselineSharingGroup::eFirst] = NS_STYLE_ALIGN_AUTO;
    mBaselineSubtreeAlign[BaselineSharingGroup::eLast] = NS_STYLE_ALIGN_AUTO;
  }

  void Initialize(const TrackSizingFunctions& aFunctions,
                  const nsStyleCoord&         aGridGap,
                  uint32_t                    aNumTracks,
                  nscoord                     aContentBoxSize);

  /**
   * Return true if aRange spans at least one track with an intrinsic sizing
   * function and does not span any tracks with a <flex> max-sizing function.
   * @param aRange the span of tracks to check
   * @param aConstraint if MIN_ISIZE, treat a <flex> min-sizing as 'min-content'
   * @param aState will be set to the union of the state bits of all the spanned
   *               tracks, unless a flex track is found - then it only contains
   *               the union of the tracks up to and including the flex track.
   */
  bool HasIntrinsicButNoFlexSizingInRange(const LineRange&      aRange,
                                          IntrinsicISizeType    aConstraint,
                                          TrackSize::StateBits* aState) const;

  // Some data we collect for aligning baseline-aligned items.
  struct ItemBaselineData
  {
    uint32_t mBaselineTrack;
    nscoord mBaseline;
    nscoord mSize;
    GridItemInfo* mGridItem;
    static bool IsBaselineTrackLessThan(const ItemBaselineData& a,
                                        const ItemBaselineData& b)
    {
      return a.mBaselineTrack < b.mBaselineTrack;
    }
  };

  /**
   * Calculate baseline offsets for the given set of items.
   * Helper for InitialzeItemBaselines.
   */
  void CalculateItemBaselines(nsTArray<ItemBaselineData>& aBaselineItems,
                              BaselineSharingGroup aBaselineGroup);

  /**
   * Initialize grid item baseline state and offsets.
   */
  void InitializeItemBaselines(GridReflowState&        aState,
                               nsTArray<GridItemInfo>& aGridItems);

  /**
   * Apply the additional alignment needed to align the baseline-aligned subtree
   * the item belongs to within its baseline track.
   */
  void AlignBaselineSubtree(const GridItemInfo& aGridItem) const;

  /**
   * Resolve Intrinsic Track Sizes.
   * http://dev.w3.org/csswg/css-grid/#algo-content
   */
  void ResolveIntrinsicSize(GridReflowState&            aState,
                            nsTArray<GridItemInfo>&     aGridItems,
                            const TrackSizingFunctions& aFunctions,
                            LineRange GridArea::*       aRange,
                            nscoord                     aPercentageBasis,
                            IntrinsicISizeType          aConstraint);

  /**
   * Helper for ResolveIntrinsicSize.  It implements step 1 "size tracks to fit
   * non-spanning items" in the spec.  Return true if the track has a <flex>
   * max-sizing function, false otherwise.
   */
  bool ResolveIntrinsicSizeStep1(GridReflowState&            aState,
                                 const TrackSizingFunctions& aFunctions,
                                 nscoord                     aPercentageBasis,
                                 IntrinsicISizeType          aConstraint,
                                 const LineRange&            aRange,
                                 const GridItemInfo&         aGridItem);
  /**
   * Collect the tracks which are growable (matching aSelector) into
   * aGrowableTracks, and return the amount of space that can be used
   * to grow those tracks.  Specifically, we return aAvailableSpace minus
   * the sum of mBase's in aPlan (clamped to 0) for the tracks in aRange,
   * or zero when there are no growable tracks.
   * @note aPlan[*].mBase represents a planned new base or limit.
   */
  static nscoord CollectGrowable(nscoord                    aAvailableSpace,
                                 const nsTArray<TrackSize>& aPlan,
                                 const LineRange&           aRange,
                                 TrackSize::StateBits       aSelector,
                                 nsTArray<uint32_t>&        aGrowableTracks)
  {
    MOZ_ASSERT(aAvailableSpace > 0, "why call me?");
    nscoord space = aAvailableSpace;
    const uint32_t start = aRange.mStart;
    const uint32_t end = aRange.mEnd;
    for (uint32_t i = start; i < end; ++i) {
      const TrackSize& sz = aPlan[i];
      space -= sz.mBase;
      if (space <= 0) {
        return 0;
      }
      if ((sz.mState & aSelector) && !sz.IsFrozen()) {
        aGrowableTracks.AppendElement(i);
      }
    }
    return aGrowableTracks.IsEmpty() ? 0 : space;
  }

  void SetupGrowthPlan(nsTArray<TrackSize>&      aPlan,
                       const nsTArray<uint32_t>& aTracks) const
  {
    for (uint32_t track : aTracks) {
      aPlan[track] = mSizes[track];
    }
  }

  void CopyPlanToBase(const nsTArray<TrackSize>& aPlan,
                      const nsTArray<uint32_t>&  aTracks)
  {
    for (uint32_t track : aTracks) {
      MOZ_ASSERT(mSizes[track].mBase <= aPlan[track].mBase);
      mSizes[track].mBase = aPlan[track].mBase;
    }
  }

  void CopyPlanToLimit(const nsTArray<TrackSize>& aPlan,
                       const nsTArray<uint32_t>&  aTracks)
  {
    for (uint32_t track : aTracks) {
      MOZ_ASSERT(mSizes[track].mLimit == NS_UNCONSTRAINEDSIZE ||
                 mSizes[track].mLimit <= aPlan[track].mBase);
      mSizes[track].mLimit = aPlan[track].mBase;
    }
  }

  /**
   * Grow the planned size for tracks in aGrowableTracks up to their limit
   * and then freeze them (all aGrowableTracks must be unfrozen on entry).
   * Subtract the space added from aAvailableSpace and return that.
   */
  nscoord GrowTracksToLimit(nscoord                   aAvailableSpace,
                            nsTArray<TrackSize>&      aPlan,
                            const nsTArray<uint32_t>& aGrowableTracks) const
  {
    MOZ_ASSERT(aAvailableSpace > 0 && aGrowableTracks.Length() > 0);
    nscoord space = aAvailableSpace;
    uint32_t numGrowable = aGrowableTracks.Length();
    while (true) {
      nscoord spacePerTrack = std::max<nscoord>(space / numGrowable, 1);
      for (uint32_t track : aGrowableTracks) {
        TrackSize& sz = aPlan[track];
        if (sz.IsFrozen()) {
          continue;
        }
        nscoord newBase = sz.mBase + spacePerTrack;
        if (newBase > sz.mLimit) {
          nscoord consumed = sz.mLimit - sz.mBase;
          if (consumed > 0) {
            space -= consumed;
            sz.mBase = sz.mLimit;
          }
          sz.mState |= TrackSize::eFrozen;
          if (--numGrowable == 0) {
            return space;
          }
        } else {
          sz.mBase = newBase;
          space -= spacePerTrack;
        }
        MOZ_ASSERT(space >= 0);
        if (space == 0) {
          return 0;
        }
      }
    }
    MOZ_ASSERT_UNREACHABLE("we don't exit the loop above except by return");
    return 0;
  }

  /**
   * Helper for GrowSelectedTracksUnlimited.  For the set of tracks (S) that
   * match aMinSizingSelector: if a track in S doesn't match aMaxSizingSelector
   * then mark it with aSkipFlag.  If all tracks in S were marked then unmark
   * them.  Return aNumGrowable minus the number of tracks marked.  It is
   * assumed that aPlan have no aSkipFlag set for tracks in aGrowableTracks
   * on entry to this method.
   */
   uint32_t MarkExcludedTracks(nsTArray<TrackSize>&      aPlan,
                               uint32_t                  aNumGrowable,
                               const nsTArray<uint32_t>& aGrowableTracks,
                               TrackSize::StateBits      aMinSizingSelector,
                               TrackSize::StateBits      aMaxSizingSelector,
                               TrackSize::StateBits      aSkipFlag) const
  {
    bool foundOneSelected = false;
    bool foundOneGrowable = false;
    uint32_t numGrowable = aNumGrowable;
    for (uint32_t track : aGrowableTracks) {
      TrackSize& sz = aPlan[track];
      const auto state = sz.mState;
      if (state & aMinSizingSelector) {
        foundOneSelected = true;
        if (state & aMaxSizingSelector) {
          foundOneGrowable = true;
          continue;
        }
        sz.mState |= aSkipFlag;
        MOZ_ASSERT(numGrowable != 0);
        --numGrowable;
      }
    }
    // 12.5 "if there are no such tracks, then all affected tracks"
    if (foundOneSelected && !foundOneGrowable) {
      for (uint32_t track : aGrowableTracks) {
        aPlan[track].mState &= ~aSkipFlag;
      }
      numGrowable = aNumGrowable;
    }
    return numGrowable;
  }

  /**
   * Increase the planned size for tracks in aGrowableTracks that match
   * aSelector (or all tracks if aSelector is zero) beyond their limit.
   * This implements the "Distribute space beyond growth limits" step in
   * https://drafts.csswg.org/css-grid/#distribute-extra-space
   */
  void GrowSelectedTracksUnlimited(nscoord                   aAvailableSpace,
                                   nsTArray<TrackSize>&      aPlan,
                                   const nsTArray<uint32_t>& aGrowableTracks,
                                   TrackSize::StateBits      aSelector) const
  {
    MOZ_ASSERT(aAvailableSpace > 0 && aGrowableTracks.Length() > 0);
    uint32_t numGrowable = aGrowableTracks.Length();
    if (aSelector) {
      DebugOnly<TrackSize::StateBits> withoutFlexMin =
        TrackSize::StateBits(aSelector & ~TrackSize::eFlexMinSizing);
      MOZ_ASSERT(withoutFlexMin == TrackSize::eIntrinsicMinSizing ||
                 withoutFlexMin == TrackSize::eMinOrMaxContentMinSizing ||
                 withoutFlexMin == TrackSize::eMaxContentMinSizing);
      // Note that eMaxContentMinSizing is always included. We do those first:
      numGrowable = MarkExcludedTracks(aPlan, numGrowable, aGrowableTracks,
                                       TrackSize::eMaxContentMinSizing,
                                       TrackSize::eMaxContentMaxSizing,
                                       TrackSize::eSkipGrowUnlimited1);
      // Now mark min-content/auto/<flex> min-sizing tracks if requested.
      auto minOrAutoSelector = aSelector & ~TrackSize::eMaxContentMinSizing;
      if (minOrAutoSelector) {
        numGrowable = MarkExcludedTracks(aPlan, numGrowable, aGrowableTracks,
                                         minOrAutoSelector,
                                         TrackSize::eIntrinsicMaxSizing,
                                         TrackSize::eSkipGrowUnlimited2);
      }
    }
    nscoord space = aAvailableSpace;
    while (true) {
      nscoord spacePerTrack = std::max<nscoord>(space / numGrowable, 1);
      for (uint32_t track : aGrowableTracks) {
        TrackSize& sz = aPlan[track];
        if (sz.mState & TrackSize::eSkipGrowUnlimited) {
          continue; // an excluded track
        }
        sz.mBase += spacePerTrack;
        space -= spacePerTrack;
        MOZ_ASSERT(space >= 0);
        if (space == 0) {
          return;
        }
      }
    }
    MOZ_ASSERT_UNREACHABLE("we don't exit the loop above except by return");
  }

  /**
   * Distribute aAvailableSpace to the planned base size for aGrowableTracks
   * up to their limits, then distribute the remaining space beyond the limits.
   */
  void DistributeToTrackBases(nscoord              aAvailableSpace,
                              nsTArray<TrackSize>& aPlan,
                              nsTArray<uint32_t>&  aGrowableTracks,
                              TrackSize::StateBits aSelector)
  {
    SetupGrowthPlan(aPlan, aGrowableTracks);
    nscoord space = GrowTracksToLimit(aAvailableSpace, aPlan, aGrowableTracks);
    if (space > 0) {
      GrowSelectedTracksUnlimited(space, aPlan, aGrowableTracks, aSelector);
    }
    CopyPlanToBase(aPlan, aGrowableTracks);
  }

  /**
   * Distribute aAvailableSpace to the planned limits for aGrowableTracks.
   */
  void DistributeToTrackLimits(nscoord              aAvailableSpace,
                               nsTArray<TrackSize>& aPlan,
                               nsTArray<uint32_t>&  aGrowableTracks)
  {
    nscoord space = GrowTracksToLimit(aAvailableSpace, aPlan, aGrowableTracks);
    if (space > 0) {
      GrowSelectedTracksUnlimited(aAvailableSpace, aPlan, aGrowableTracks,
                                  TrackSize::StateBits(0));
    }
    CopyPlanToLimit(aPlan, aGrowableTracks);
  }

  /**
   * Distribute aAvailableSize to the tracks.  This implements 12.6 at:
   * http://dev.w3.org/csswg/css-grid/#algo-grow-tracks
   */
  void DistributeFreeSpace(nscoord aAvailableSize)
  {
    const uint32_t numTracks = mSizes.Length();
    if (MOZ_UNLIKELY(numTracks == 0 || aAvailableSize <= 0)) {
      return;
    }
    if (aAvailableSize == NS_UNCONSTRAINEDSIZE) {
      for (TrackSize& sz : mSizes) {
        sz.mBase = sz.mLimit;
      }
    } else {
      // Compute free space and count growable tracks.
      nscoord space = aAvailableSize;
      uint32_t numGrowable = numTracks;
      for (const TrackSize& sz : mSizes) {
        space -= sz.mBase;
        MOZ_ASSERT(sz.mBase <= sz.mLimit);
        if (sz.mBase == sz.mLimit) {
          --numGrowable;
        }
      }
      // Distribute the free space evenly to the growable tracks. If not exactly
      // divisable the remainder is added to the leading tracks.
      while (space > 0 && numGrowable) {
        nscoord spacePerTrack =
          std::max<nscoord>(space / numGrowable, 1);
        for (uint32_t i = 0; i < numTracks && space > 0; ++i) {
          TrackSize& sz = mSizes[i];
          if (sz.mBase == sz.mLimit) {
            continue;
          }
          nscoord newBase = sz.mBase + spacePerTrack;
          if (newBase >= sz.mLimit) {
            space -= sz.mLimit - sz.mBase;
            sz.mBase = sz.mLimit;
            --numGrowable;
          } else {
            space -= spacePerTrack;
            sz.mBase = newBase;
          }
        }
      }
    }
  }

  /**
   * Implements "12.7.1. Find the Size of an 'fr'".
   * http://dev.w3.org/csswg/css-grid/#algo-find-fr-size
   * (The returned value is a 'nscoord' divided by a factor - a floating type
   * is used to avoid intermediary rounding errors.)
   */
  float FindFrUnitSize(const LineRange&            aRange,
                       const nsTArray<uint32_t>&   aFlexTracks,
                       const TrackSizingFunctions& aFunctions,
                       nscoord                     aSpaceToFill) const;

  /**
   * Implements the "find the used flex fraction" part of StretchFlexibleTracks.
   * (The returned value is a 'nscoord' divided by a factor - a floating type
   * is used to avoid intermediary rounding errors.)
   */
  float FindUsedFlexFraction(GridReflowState&            aState,
                             nsTArray<GridItemInfo>&     aGridItems,
                             const nsTArray<uint32_t>&   aFlexTracks,
                             const TrackSizingFunctions& aFunctions,
                             nscoord                     aAvailableSize) const;

  /**
   * Implements "12.7. Stretch Flexible Tracks"
   * http://dev.w3.org/csswg/css-grid/#algo-flex-tracks
   */
  void StretchFlexibleTracks(GridReflowState&            aState,
                             nsTArray<GridItemInfo>&     aGridItems,
                             const TrackSizingFunctions& aFunctions,
                             nscoord                     aAvailableSize);

  /**
   * Implements "12.3. Track Sizing Algorithm"
   * http://dev.w3.org/csswg/css-grid/#algo-track-sizing
   */
  void CalculateSizes(GridReflowState&            aState,
                      nsTArray<GridItemInfo>&     aGridItems,
                      const TrackSizingFunctions& aFunctions,
                      nscoord                     aContentSize,
                      LineRange GridArea::*       aRange,
                      IntrinsicISizeType          aConstraint);

  /**
   * Apply 'align/justify-content', whichever is relevant for this axis.
   * https://drafts.csswg.org/css-align-3/#propdef-align-content
   */
  void AlignJustifyContent(const nsHTMLReflowState& aReflowState,
                           const LogicalSize& aContainerSize);

  nscoord GridLineEdge(uint32_t aLine, GridLineSide aSide) const
  {
    if (MOZ_UNLIKELY(mSizes.IsEmpty())) {
      // https://drafts.csswg.org/css-grid/#grid-definition
      // "... the explicit grid still contains one grid line in each axis."
      MOZ_ASSERT(aLine == 0, "We should only resolve line 1 in an empty grid");
      return nscoord(0);
    }
    MOZ_ASSERT(aLine <= mSizes.Length(), "mSizes is too small");
    if (aSide == GridLineSide::eBeforeGridGap) {
      if (aLine == 0) {
        return nscoord(0);
      }
      const TrackSize& sz = mSizes[aLine - 1];
      return sz.mPosition + sz.mBase;
    }
    if (aLine == mSizes.Length()) {
      return mContentBoxSize;
    }
    return mSizes[aLine].mPosition;
  }

  nscoord SumOfGridGaps() const
  {
    auto len = mSizes.Length();
    return MOZ_LIKELY(len > 1) ? (len - 1) * mGridGap : 0;
  }

  /**
   * Break before aRow, i.e. set the eBreakBefore flag on aRow and set the grid
   * gap before aRow to zero (and shift all rows after it by the removed gap).
   */
  void BreakBeforeRow(uint32_t aRow)
  {
    MOZ_ASSERT(mAxis == eLogicalAxisBlock,
               "Should only be fragmenting in the block axis (between rows)");
    nscoord prevRowEndPos = 0;
    if (aRow != 0) {
      auto& prevSz = mSizes[aRow - 1];
      prevRowEndPos = prevSz.mPosition + prevSz.mBase;
    }
    auto& sz = mSizes[aRow];
    const nscoord gap = sz.mPosition - prevRowEndPos;
    sz.mState |= TrackSize::eBreakBefore;
    if (gap != 0) {
      for (uint32_t i = aRow, len = mSizes.Length(); i < len; ++i) {
        mSizes[i].mPosition -= gap;
      }
    }
  }

  /**
   * Set the size of aRow to aSize and adjust the position of all rows after it.
   */
  void ResizeRow(uint32_t aRow, nscoord aNewSize)
  {
    MOZ_ASSERT(mAxis == eLogicalAxisBlock,
               "Should only be fragmenting in the block axis (between rows)");
    MOZ_ASSERT(aNewSize >= 0);
    auto& sz = mSizes[aRow];
    nscoord delta = aNewSize - sz.mBase;
    NS_WARN_IF_FALSE(delta != nscoord(0), "Useless call to ResizeRow");
    sz.mBase = aNewSize;
    const uint32_t numRows = mSizes.Length();
    for (uint32_t r = aRow + 1; r < numRows; ++r) {
      mSizes[r].mPosition += delta;
    }
  }

#ifdef DEBUG
  void Dump() const
  {
    for (uint32_t i = 0, len = mSizes.Length(); i < len; ++i) {
      printf("  %d: ", i);
      mSizes[i].Dump();
      printf("\n");
    }
  }
#endif

  AutoTArray<TrackSize, 32> mSizes;
  nscoord mContentBoxSize;
  nscoord mGridGap;
  LogicalAxis mAxis;
  // Used for aligning a baseline-aligned subtree of items.  The only possible
  // values are NS_STYLE_ALIGN_{START,END,CENTER,AUTO}.  AUTO means there are
  // no baseline-aligned items in any track in that axis.
  // There is one alignment value for each BaselineSharingGroup.
  uint8_t mBaselineSubtreeAlign[2];
};

/**
 * Grid data shared by all continuations, owned by the first-in-flow.
 * The data is initialized from the first-in-flow's GridReflowState at
 * the end of its reflow.  Fragmentation will modify mRows.mSizes -
 * the mPosition to remove the row gap at the break boundary, the mState
 * by setting the eBreakBefore flag, and mBase is modified when we decide
 * to grow a row.  mOriginalRowData is setup by the first-in-flow and
 * not modified after that.  It's used for undoing the changes to mRows.
 * mCols, mGridItems, mAbsPosItems are used for initializing the grid
 * reflow state for continuations, see GridReflowState::Initialize below.
 */
struct nsGridContainerFrame::SharedGridData
{
  SharedGridData() : mCols(eLogicalAxisInline), mRows(eLogicalAxisBlock) {}
  Tracks mCols;
  Tracks mRows;
  struct RowData {
    nscoord mBase; // the original track size
    nscoord mGap;  // the original gap before a track
  };
  nsTArray<RowData> mOriginalRowData;
  nsTArray<GridItemInfo> mGridItems;
  nsTArray<GridItemInfo> mAbsPosItems;

  /**
   * Only set on the first-in-flow.  Continuations will Initialize() their
   * GridReflowState from it.
   */
  NS_DECLARE_FRAME_PROPERTY_DELETABLE(Prop, SharedGridData)
};

struct MOZ_STACK_CLASS nsGridContainerFrame::GridReflowState
{
  GridReflowState(nsGridContainerFrame*    aFrame,
                  const nsHTMLReflowState& aRS)
    : GridReflowState(aFrame, *aRS.rendContext, &aRS, aRS.mStylePosition,
                      aRS.GetWritingMode())
  {}
  GridReflowState(nsGridContainerFrame* aFrame,
                  nsRenderingContext&   aRC)
    : GridReflowState(aFrame, aRC, nullptr, aFrame->StylePosition(),
                      aFrame->GetWritingMode())
  {}

  /**
   * Initialize our track sizes and grid item info using the shared
   * state from aGridContainerFrame first-in-flow.
   */
  void InitializeForContinuation(nsGridContainerFrame* aGridContainerFrame,
                                 nscoord               aConsumedBSize)
  {
    MOZ_ASSERT(aGridContainerFrame->GetPrevInFlow(),
               "don't call this on the first-in-flow");
    MOZ_ASSERT(mGridItems.IsEmpty() && mAbsPosItems.IsEmpty(),
               "shouldn't have any item data yet");

    // Get the SharedGridData from the first-in-flow. Also calculate the number
    // of fragments before this so that we can figure out our start row below.
    uint32_t fragment = 0;
    nsIFrame* firstInFlow = aGridContainerFrame;
    for (auto pif = aGridContainerFrame->GetPrevInFlow();
         pif; pif = pif->GetPrevInFlow()) {
      ++fragment;
      firstInFlow = pif;
    }
    mSharedGridData = firstInFlow->Properties().Get(SharedGridData::Prop());
    MOZ_ASSERT(mSharedGridData, "first-in-flow must have SharedGridData");

    // Find the start row for this fragment and undo breaks after that row
    // since the breaks might be different from the last reflow.
    auto& rowSizes = mSharedGridData->mRows.mSizes;
    const uint32_t numRows = rowSizes.Length();
    mStartRow = numRows;
    for (uint32_t row = 0, breakCount = 0; row < numRows; ++row) {
      if (rowSizes[row].mState & TrackSize::eBreakBefore) {
        if (fragment == ++breakCount) {
          mStartRow = row;
          mFragBStart = rowSizes[row].mPosition;
          // Restore the original size for |row| and grid gaps / state after it.
          const auto& origRowData = mSharedGridData->mOriginalRowData;
          rowSizes[row].mBase = origRowData[row].mBase;
          nscoord prevEndPos = rowSizes[row].mPosition + rowSizes[row].mBase;
          while (++row < numRows) {
            auto& sz = rowSizes[row];
            const auto& orig = origRowData[row];
            sz.mPosition = prevEndPos + orig.mGap;
            sz.mBase = orig.mBase;
            sz.mState &= ~TrackSize::eBreakBefore;
            prevEndPos = sz.mPosition + sz.mBase;
          }
          break;
        }
      }
    }
    if (mStartRow == numRows) {
      // All of the grid's rows fit inside of previous grid-container fragments.
      mFragBStart = aConsumedBSize;
    }

    // Copy the shared track state.
    // XXX consider temporarily swapping the array elements instead and swapping
    // XXX them back after we're done reflowing, for better performance.
    // XXX (bug 1252002)
    mCols = mSharedGridData->mCols;
    mRows = mSharedGridData->mRows;

    // Copy item data from each child's first-in-flow data in mSharedGridData.
    // XXX NOTE: This is O(n^2) in the number of items. (bug 1252186)
    mIter.Reset();
    for (; !mIter.AtEnd(); mIter.Next()) {
      nsIFrame* child = *mIter;
      nsIFrame* childFirstInFlow = child->FirstInFlow();
      DebugOnly<size_t> len = mGridItems.Length();
      for (auto& itemInfo : mSharedGridData->mGridItems) {
        if (itemInfo.mFrame == childFirstInFlow) {
          auto item = mGridItems.AppendElement(GridItemInfo(child, itemInfo.mArea));
          // Copy the item's baseline data so that the item's last fragment can do
          // last-baseline alignment if necessary.
          item->mState[0] |= itemInfo.mState[0] & ItemState::eAllBaselineBits;
          item->mState[1] |= itemInfo.mState[1] & ItemState::eAllBaselineBits;
          item->mBaselineOffset[0] = itemInfo.mBaselineOffset[0];
          item->mBaselineOffset[1] = itemInfo.mBaselineOffset[1];
          break;
        }
      }
      MOZ_ASSERT(mGridItems.Length() == len + 1, "can't find GridItemInfo");
    }

    // XXX NOTE: This is O(n^2) in the number of abs.pos. items. (bug 1252186)
    nsFrameList absPosChildren(aGridContainerFrame->GetChildList(
                                 aGridContainerFrame->GetAbsoluteListID()));
    for (auto f : absPosChildren) {
      nsIFrame* childFirstInFlow = f->FirstInFlow();
      DebugOnly<size_t> len = mAbsPosItems.Length();
      for (auto& itemInfo : mSharedGridData->mAbsPosItems) {
        if (itemInfo.mFrame == childFirstInFlow) {
          mAbsPosItems.AppendElement(GridItemInfo(f, itemInfo.mArea));
          break;
        }
      }
      MOZ_ASSERT(mAbsPosItems.Length() == len + 1, "can't find GridItemInfo");
    }
  }

  /**
   * Calculate our track sizes.
   */
  void CalculateTrackSizes(const Grid&        aGrid,
                           const LogicalSize& aContentBox,
                           IntrinsicISizeType aConstraint);

  /**
   * Return the containing block for a grid item occupying aArea.
   */
  LogicalRect ContainingBlockFor(const GridArea& aArea) const;

  /**
   * Return the containing block for an abs.pos. grid item occupying aArea.
   * Any 'auto' lines in the grid area will be aligned with grid container
   * containing block on that side.
   * @param aGridOrigin the origin of the grid
   * @param aGridCB the grid container containing block (its padding area)
   */
  LogicalRect ContainingBlockForAbsPos(const GridArea&     aArea,
                                       const LogicalPoint& aGridOrigin,
                                       const LogicalRect&  aGridCB) const;

  GridItemCSSOrderIterator mIter;
  const nsStylePosition* const mGridStyle;
  Tracks mCols;
  Tracks mRows;
  TrackSizingFunctions mColFunctions;
  TrackSizingFunctions mRowFunctions;
  /**
   * Info about each (normal flow) grid item.
   */
  nsTArray<GridItemInfo> mGridItems;
  /**
   * Info about each grid-aligned abs.pos. child.
   */
  nsTArray<GridItemInfo> mAbsPosItems;

  /**
   * @note mReflowState may be null when using the 2nd ctor above. In this case
   * we'll construct a dummy parent reflow state if we need it to calculate
   * min/max-content contributions when sizing tracks.
   */
  const nsHTMLReflowState* const mReflowState;
  nsRenderingContext& mRenderingContext;
  nsGridContainerFrame* const mFrame;
  SharedGridData* mSharedGridData; // [weak] owned by mFrame's first-in-flow.
  /** Computed border+padding with mSkipSides applied. */
  LogicalMargin mBorderPadding;
  /**
   * BStart of this fragment in "grid space" (i.e. the concatenation of content
   * areas of all fragments).  Equal to mRows.mSizes[mStartRow].mPosition,
   * or, if this fragment starts after the last row, the GetConsumedBSize().
   */
  nscoord mFragBStart;
  /** The start row for this fragment. */
  uint32_t mStartRow;
  /** Our tentative ApplySkipSides bits. */
  LogicalSides mSkipSides;
  const WritingMode mWM;

private:
  GridReflowState(nsGridContainerFrame*    aFrame,
                  nsRenderingContext&      aRenderingContext,
                  const nsHTMLReflowState* aReflowState,
                  const nsStylePosition*   aGridStyle,
                  const WritingMode&       aWM)
    : mIter(aFrame, kPrincipalList)
    , mGridStyle(aGridStyle)
    , mCols(eLogicalAxisInline)
    , mRows(eLogicalAxisBlock)
    , mColFunctions(mGridStyle->mGridTemplateColumns,
                    mGridStyle->mGridAutoColumnsMin,
                    mGridStyle->mGridAutoColumnsMax)
    , mRowFunctions(mGridStyle->mGridTemplateRows,
                    mGridStyle->mGridAutoRowsMin,
                    mGridStyle->mGridAutoRowsMax)
    , mReflowState(aReflowState)
    , mRenderingContext(aRenderingContext)
    , mFrame(aFrame)
    , mSharedGridData(nullptr)
    , mBorderPadding(aWM)
    , mFragBStart(0)
    , mStartRow(0)
    , mWM(aWM)
  {
    MOZ_ASSERT(!aReflowState || aReflowState->frame == mFrame);
    if (aReflowState) {
      mBorderPadding = aReflowState->ComputedLogicalBorderPadding();
      mSkipSides = aFrame->PreReflowBlockLevelLogicalSkipSides();
      mBorderPadding.ApplySkipSides(mSkipSides);
    }
  }
};

/**
 * The Grid implements grid item placement and the state of the grid -
 * the size of the explicit/implicit grid, which cells are occupied etc.
 */
struct MOZ_STACK_CLASS nsGridContainerFrame::Grid
{
  /**
   * Place all child frames into the grid and expand the (implicit) grid as
   * needed.  The allocated GridAreas are stored in the GridAreaProperty
   * frame property on the child frame.
   * @param aComputedMinSize the container's min-size - used to determine
   *   the number of repeat(auto-fill/fit) tracks.
   * @param aComputedSize the container's size - used to determine
   *   the number of repeat(auto-fill/fit) tracks.
   * @param aComputedMaxSize the container's max-size - used to determine
   *   the number of repeat(auto-fill/fit) tracks.
   */
  void PlaceGridItems(GridReflowState& aState,
                      const LogicalSize& aComputedMinSize,
                      const LogicalSize& aComputedSize,
                      const LogicalSize& aComputedMaxSize);

  /**
   * As above but for an abs.pos. child.  Any 'auto' lines will be represented
   * by kAutoLine in the LineRange result.
   * @param aGridStart the first line in the final, but untranslated grid
   * @param aGridEnd the last line in the final, but untranslated grid
   */
  LineRange ResolveAbsPosLineRange(const nsStyleGridLine& aStart,
                                   const nsStyleGridLine& aEnd,
                                   const LineNameMap& aNameMap,
                                   uint32_t GridNamedArea::* aAreaStart,
                                   uint32_t GridNamedArea::* aAreaEnd,
                                   uint32_t aExplicitGridEnd,
                                   int32_t aGridStart,
                                   int32_t aGridEnd,
                                   const nsStylePosition* aStyle);

  /**
   * Return a GridArea for abs.pos. item with non-auto lines placed at
   * a definite line (1-based) with placement errors resolved.  One or both
   * positions may still be 'auto'.
   * @param aChild the abs.pos. grid item to place
   * @param aStyle the StylePosition() for the grid container
   */
  GridArea PlaceAbsPos(nsIFrame* aChild,
                       const LineNameMap& aColLineNameMap,
                       const LineNameMap& aRowLineNameMap,
                       const nsStylePosition* aStyle);

  /**
   * Find the first column in row aLockedRow starting at aStartCol where aArea
   * could be placed without overlapping other items.  The returned column may
   * cause aArea to overflow the current implicit grid bounds if placed there.
   */
  uint32_t FindAutoCol(uint32_t aStartCol, uint32_t aLockedRow,
                       const GridArea* aArea) const;

  /**
   * Place aArea in the first column (in row aArea->mRows.mStart) starting at
   * aStartCol without overlapping other items.  The resulting aArea may
   * overflow the current implicit grid bounds.
   * Pre-condition: aArea->mRows.IsDefinite() is true.
   * Post-condition: aArea->IsDefinite() is true.
   */
  void PlaceAutoCol(uint32_t aStartCol, GridArea* aArea) const;

  /**
   * Find the first row in column aLockedCol starting at aStartRow where aArea
   * could be placed without overlapping other items.  The returned row may
   * cause aArea to overflow the current implicit grid bounds if placed there.
   */
  uint32_t FindAutoRow(uint32_t aLockedCol, uint32_t aStartRow,
                       const GridArea* aArea) const;

  /**
   * Place aArea in the first row (in column aArea->mCols.mStart) starting at
   * aStartRow without overlapping other items. The resulting aArea may
   * overflow the current implicit grid bounds.
   * Pre-condition: aArea->mCols.IsDefinite() is true.
   * Post-condition: aArea->IsDefinite() is true.
   */
  void PlaceAutoRow(uint32_t aStartRow, GridArea* aArea) const;

  /**
   * Place aArea in the first column starting at aStartCol,aStartRow without
   * causing it to overlap other items or overflow mGridColEnd.
   * If there's no such column in aStartRow, continue in position 1,aStartRow+1.
   * Pre-condition: aArea->mCols.IsAuto() && aArea->mRows.IsAuto() is true.
   * Post-condition: aArea->IsDefinite() is true.
   */
  void PlaceAutoAutoInRowOrder(uint32_t aStartCol,
                               uint32_t aStartRow,
                               GridArea* aArea) const;

  /**
   * Place aArea in the first row starting at aStartCol,aStartRow without
   * causing it to overlap other items or overflow mGridRowEnd.
   * If there's no such row in aStartCol, continue in position aStartCol+1,1.
   * Pre-condition: aArea->mCols.IsAuto() && aArea->mRows.IsAuto() is true.
   * Post-condition: aArea->IsDefinite() is true.
   */
  void PlaceAutoAutoInColOrder(uint32_t aStartCol,
                               uint32_t aStartRow,
                               GridArea* aArea) const;

  /**
   * Return aLine if it's inside the aMin..aMax range (inclusive),
   * otherwise return kAutoLine.
   */
  static int32_t
  AutoIfOutside(int32_t aLine, int32_t aMin, int32_t aMax)
  {
    MOZ_ASSERT(aMin <= aMax);
    if (aLine < aMin || aLine > aMax) {
      return kAutoLine;
    }
    return aLine;
  }

  /**
   * Inflate the implicit grid to include aArea.
   * @param aArea may be definite or auto
   */
  void InflateGridFor(const GridArea& aArea)
  {
    mGridColEnd = std::max(mGridColEnd, aArea.mCols.HypotheticalEnd());
    mGridRowEnd = std::max(mGridRowEnd, aArea.mRows.HypotheticalEnd());
    MOZ_ASSERT(mGridColEnd <= kTranslatedMaxLine &&
               mGridRowEnd <= kTranslatedMaxLine);
  }

  enum LineRangeSide {
    eLineRangeSideStart, eLineRangeSideEnd
  };
  /**
   * Return a line number for (non-auto) aLine, per:
   * http://dev.w3.org/csswg/css-grid/#line-placement
   * @param aLine style data for the line (must be non-auto)
   * @param aNth a number of lines to find from aFromIndex, negative if the
   *             search should be in reverse order.  In the case aLine has
   *             a specified line name, it's permitted to pass in zero which
   *             will be treated as one.
   * @param aFromIndex the zero-based index to start counting from
   * @param aLineNameList the explicit named lines
   * @param aAreaStart a pointer to GridNamedArea::mColumnStart/mRowStart
   * @param aAreaEnd a pointer to GridNamedArea::mColumnEnd/mRowEnd
   * @param aExplicitGridEnd the last line in the explicit grid
   * @param aEdge indicates whether we are resolving a start or end line
   * @param aStyle the StylePosition() for the grid container
   * @return a definite line (1-based), clamped to the kMinLine..kMaxLine range
   */
  int32_t ResolveLine(const nsStyleGridLine& aLine,
                      int32_t aNth,
                      uint32_t aFromIndex,
                      const LineNameMap& aNameMap,
                      uint32_t GridNamedArea::* aAreaStart,
                      uint32_t GridNamedArea::* aAreaEnd,
                      uint32_t aExplicitGridEnd,
                      LineRangeSide aSide,
                      const nsStylePosition* aStyle);

  /**
   * Helper method for ResolveLineRange.
   * @see ResolveLineRange
   * @return a pair (start,end) of lines
   */
  typedef std::pair<int32_t, int32_t> LinePair;
  LinePair ResolveLineRangeHelper(const nsStyleGridLine& aStart,
                                  const nsStyleGridLine& aEnd,
                                  const LineNameMap& aNameMap,
                                  uint32_t GridNamedArea::* aAreaStart,
                                  uint32_t GridNamedArea::* aAreaEnd,
                                  uint32_t aExplicitGridEnd,
                                  const nsStylePosition* aStyle);

  /**
   * Return a LineRange based on the given style data. Non-auto lines
   * are resolved to a definite line number (1-based) per:
   * http://dev.w3.org/csswg/css-grid/#line-placement
   * with placement errors corrected per:
   * http://dev.w3.org/csswg/css-grid/#grid-placement-errors
   * @param aStyle the StylePosition() for the grid container
   * @param aStart style data for the start line
   * @param aEnd style data for the end line
   * @param aLineNameList the explicit named lines
   * @param aAreaStart a pointer to GridNamedArea::mColumnStart/mRowStart
   * @param aAreaEnd a pointer to GridNamedArea::mColumnEnd/mRowEnd
   * @param aExplicitGridEnd the last line in the explicit grid
   * @param aStyle the StylePosition() for the grid container
   */
  LineRange ResolveLineRange(const nsStyleGridLine& aStart,
                             const nsStyleGridLine& aEnd,
                             const LineNameMap& aNameMap,
                             uint32_t GridNamedArea::* aAreaStart,
                             uint32_t GridNamedArea::* aAreaEnd,
                             uint32_t aExplicitGridEnd,
                             const nsStylePosition* aStyle);

  /**
   * Return a GridArea with non-auto lines placed at a definite line (1-based)
   * with placement errors resolved.  One or both positions may still
   * be 'auto'.
   * @param aChild the grid item
   * @param aStyle the StylePosition() for the grid container
   */
  GridArea PlaceDefinite(nsIFrame*              aChild,
                         const LineNameMap&     aColLineNameMap,
                         const LineNameMap&     aRowLineNameMap,
                         const nsStylePosition* aStyle);

  bool HasImplicitNamedArea(const nsString& aName) const
  {
    return mAreas && mAreas->Contains(aName);
  }

  /**
   * A convenience method to lookup a name in 'grid-template-areas'.
   * @param aStyle the StylePosition() for the grid container
   * @return null if not found
   */
  static const css::GridNamedArea*
  FindNamedArea(const nsSubstring& aName, const nsStylePosition* aStyle)
  {
    if (!aStyle->mGridTemplateAreas) {
      return nullptr;
    }
    const nsTArray<css::GridNamedArea>& areas =
      aStyle->mGridTemplateAreas->mNamedAreas;
    size_t len = areas.Length();
    for (size_t i = 0; i < len; ++i) {
      const css::GridNamedArea& area = areas[i];
      if (area.mName == aName) {
        return &area;
      }
    }
    return nullptr;
  }

  // Return true if aString ends in aSuffix and has at least one character before
  // the suffix. Assign aIndex to where the suffix starts.
  static bool
  IsNameWithSuffix(const nsString& aString, const nsString& aSuffix,
                   uint32_t* aIndex)
  {
    if (StringEndsWith(aString, aSuffix)) {
      *aIndex = aString.Length() - aSuffix.Length();
      return *aIndex != 0;
    }
    return false;
  }

  static bool
  IsNameWithEndSuffix(const nsString& aString, uint32_t* aIndex)
  {
    return IsNameWithSuffix(aString, NS_LITERAL_STRING("-end"), aIndex);
  }

  static bool
  IsNameWithStartSuffix(const nsString& aString, uint32_t* aIndex)
  {
    return IsNameWithSuffix(aString, NS_LITERAL_STRING("-start"), aIndex);
  }

  /**
   * A CellMap holds state for each cell in the grid.
   * It's row major.  It's sparse in the sense that it only has enough rows to
   * cover the last row that has a grid item.  Each row only has enough entries
   * to cover columns that are occupied *on that row*, i.e. it's not a full
   * matrix covering the entire implicit grid.  An absent Cell means that it's
   * unoccupied by any grid item.
   */
  struct CellMap {
    struct Cell {
      Cell() : mIsOccupied(false) {}
      bool mIsOccupied : 1;
    };

    void Fill(const GridArea& aGridArea)
    {
      MOZ_ASSERT(aGridArea.IsDefinite());
      MOZ_ASSERT(aGridArea.mRows.mStart < aGridArea.mRows.mEnd);
      MOZ_ASSERT(aGridArea.mCols.mStart < aGridArea.mCols.mEnd);
      const auto numRows = aGridArea.mRows.mEnd;
      const auto numCols = aGridArea.mCols.mEnd;
      mCells.EnsureLengthAtLeast(numRows);
      for (auto i = aGridArea.mRows.mStart; i < numRows; ++i) {
        nsTArray<Cell>& cellsInRow = mCells[i];
        cellsInRow.EnsureLengthAtLeast(numCols);
        for (auto j = aGridArea.mCols.mStart; j < numCols; ++j) {
          cellsInRow[j].mIsOccupied = true;
        }
      }
    }

    uint32_t IsEmptyCol(uint32_t aCol) const
    {
      for (auto& row : mCells) {
        if (aCol < row.Length() && row[aCol].mIsOccupied) {
          return false;
        }
      }
      return true;
    }
    uint32_t IsEmptyRow(uint32_t aRow) const
    {
      if (aRow >= mCells.Length()) {
        return true;
      }
      for (const Cell& cell : mCells[aRow]) {
        if (cell.mIsOccupied) {
          return false;
        }
      }
      return true;
    }
#ifdef DEBUG
    void Dump() const
    {
      const size_t numRows = mCells.Length();
      for (size_t i = 0; i < numRows; ++i) {
        const nsTArray<Cell>& cellsInRow = mCells[i];
        const size_t numCols = cellsInRow.Length();
        printf("%lu:\t", (unsigned long)i + 1);
        for (size_t j = 0; j < numCols; ++j) {
          printf(cellsInRow[j].mIsOccupied ? "X " : ". ");
        }
        printf("\n");
      }
    }
#endif

    nsTArray<nsTArray<Cell>> mCells;
  };

  /**
   * State for each cell in the grid.
   */
  CellMap mCellMap;
  /**
   * @see HasImplicitNamedArea.
   */
  ImplicitNamedAreas* mAreas;
  /**
   * The last column grid line (1-based) in the explicit grid.
   * (i.e. the number of explicit columns + 1)
   */
  uint32_t mExplicitGridColEnd;
  /**
   * The last row grid line (1-based) in the explicit grid.
   * (i.e. the number of explicit rows + 1)
   */
  uint32_t mExplicitGridRowEnd;
  // Same for the implicit grid, except these become zero-based after
  // resolving definite lines.
  uint32_t mGridColEnd;
  uint32_t mGridRowEnd;

  /**
   * Offsets from the start of the implicit grid to the start of the translated
   * explicit grid.  They are zero if there are no implicit lines before 1,1.
   * e.g. "grid-column: span 3 / 1" makes mExplicitGridOffsetCol = 3 and the
   * corresponding GridArea::mCols will be 0 / 3 in the zero-based translated
   * grid.
   */
  uint32_t mExplicitGridOffsetCol;
  uint32_t mExplicitGridOffsetRow;

};

void
nsGridContainerFrame::GridReflowState::CalculateTrackSizes(
  const Grid&        aGrid,
  const LogicalSize& aContentBox,
  IntrinsicISizeType aConstraint)
{
  mCols.Initialize(mColFunctions, mGridStyle->mGridColumnGap,
                   aGrid.mGridColEnd, aContentBox.ISize(mWM));
  mRows.Initialize(mRowFunctions, mGridStyle->mGridRowGap,
                   aGrid.mGridRowEnd, aContentBox.BSize(mWM));

  mCols.CalculateSizes(*this, mGridItems, mColFunctions,
                       aContentBox.ISize(mWM), &GridArea::mCols,
                       aConstraint);

  mRows.CalculateSizes(*this, mGridItems, mRowFunctions,
                       aContentBox.BSize(mWM), &GridArea::mRows,
                       aConstraint);
}

/**
 * (XXX share this utility function with nsFlexContainerFrame at some point)
 *
 * Helper for BuildDisplayList, to implement this special-case for grid
 * items from the spec:
 *   The painting order of grid items is exactly the same as inline blocks,
 *   except that [...] 'z-index' values other than 'auto' create a stacking
 *   context even if 'position' is 'static'.
 * http://dev.w3.org/csswg/css-grid/#z-order
 */
static uint32_t
GetDisplayFlagsForGridItem(nsIFrame* aFrame)
{
  const nsStylePosition* pos = aFrame->StylePosition();
  if (pos->mZIndex.GetUnit() == eStyleUnit_Integer) {
    return nsIFrame::DISPLAY_CHILD_FORCE_STACKING_CONTEXT;
  }
  return nsIFrame::DISPLAY_CHILD_FORCE_PSEUDO_STACKING_CONTEXT;
}

static nscoord
SpaceToFill(WritingMode aWM, const LogicalSize& aSize, nscoord aMargin,
            LogicalAxis aAxis, nscoord aCBSize)
{
  nscoord size = aAxis == eLogicalAxisBlock ? aSize.BSize(aWM)
                                            : aSize.ISize(aWM);
  return aCBSize - (size + aMargin);
}

// Align an item's margin box in its aAxis inside aCBSize.
static void
AlignJustifySelf(uint8_t aAlignment, bool aOverflowSafe, LogicalAxis aAxis,
                 bool aSameSide, nscoord aBaselineAdjust, nscoord aCBSize,
                 const nsHTMLReflowState& aRS, const LogicalSize& aChildSize,
                 LogicalPoint* aPos)
{
  MOZ_ASSERT(aAlignment != NS_STYLE_ALIGN_AUTO, "unexpected 'auto' "
             "computed value for normal flow grid item");
  MOZ_ASSERT(aAlignment != NS_STYLE_ALIGN_LEFT &&
             aAlignment != NS_STYLE_ALIGN_RIGHT,
             "caller should map that to the corresponding START/END");

  // Map some alignment values to 'start' / 'end'.
  switch (aAlignment) {
    case NS_STYLE_ALIGN_SELF_START: // align/justify-self: self-start
      aAlignment = MOZ_LIKELY(aSameSide) ? NS_STYLE_ALIGN_START
                                         : NS_STYLE_ALIGN_END;
      break;
    case NS_STYLE_ALIGN_SELF_END: // align/justify-self: self-end
      aAlignment = MOZ_LIKELY(aSameSide) ? NS_STYLE_ALIGN_END
                                         : NS_STYLE_ALIGN_START;
      break;
    case NS_STYLE_ALIGN_FLEX_START: // same as 'start' for Grid
      aAlignment = NS_STYLE_ALIGN_START;
      break;
    case NS_STYLE_ALIGN_FLEX_END: // same as 'end' for Grid
      aAlignment = NS_STYLE_ALIGN_END;
      break;
  }

  // XXX try to condense this code a bit by adding the necessary convenience
  // methods? (bug 1209710)

  // Get the item's margin corresponding to the container's start/end side.
  const LogicalMargin margin = aRS.ComputedLogicalMargin();
  WritingMode wm = aRS.GetWritingMode();
  nscoord marginStart, marginEnd;
  if (aAxis == eLogicalAxisBlock) {
    if (MOZ_LIKELY(aSameSide)) {
      marginStart = margin.BStart(wm);
      marginEnd = margin.BEnd(wm);
    } else {
      marginStart = margin.BEnd(wm);
      marginEnd = margin.BStart(wm);
    }
  } else {
    if (MOZ_LIKELY(aSameSide)) {
      marginStart = margin.IStart(wm);
      marginEnd = margin.IEnd(wm);
    } else {
      marginStart = margin.IEnd(wm);
      marginEnd = margin.IStart(wm);
    }
  }

  const auto& styleMargin = aRS.mStyleMargin->mMargin;
  bool hasAutoMarginStart;
  bool hasAutoMarginEnd;
  if (aAxis == eLogicalAxisBlock) {
    hasAutoMarginStart = styleMargin.GetBStartUnit(wm) == eStyleUnit_Auto;
    hasAutoMarginEnd = styleMargin.GetBEndUnit(wm) == eStyleUnit_Auto;
  } else {
    hasAutoMarginStart = styleMargin.GetIStartUnit(wm) == eStyleUnit_Auto;
    hasAutoMarginEnd = styleMargin.GetIEndUnit(wm) == eStyleUnit_Auto;
  }

  // https://drafts.csswg.org/css-align-3/#overflow-values
  // This implements <overflow-position> = 'safe'.
  // And auto-margins: https://drafts.csswg.org/css-grid/#auto-margins
  if ((MOZ_UNLIKELY(aOverflowSafe) && aAlignment != NS_STYLE_ALIGN_START) ||
      hasAutoMarginStart || hasAutoMarginEnd) {
    nscoord space = SpaceToFill(wm, aChildSize, marginStart + marginEnd,
                                aAxis, aCBSize);
    // XXX we might want to include == 0 here as an optimization -
    // I need to see what the baseline/last-baseline code looks like first.
    if (space < 0) {
      // "Overflowing elements ignore their auto margins and overflow
      // in the end directions"
      aAlignment = NS_STYLE_ALIGN_START;
    } else if (hasAutoMarginEnd) {
      aAlignment = hasAutoMarginStart ? NS_STYLE_ALIGN_CENTER
                                      : (aSameSide ? NS_STYLE_ALIGN_START
                                                   : NS_STYLE_ALIGN_END);
    } else if (hasAutoMarginStart) {
      aAlignment = aSameSide ? NS_STYLE_ALIGN_END : NS_STYLE_ALIGN_START;
    }
  }

  // Set the position (aPos) for the requested alignment.
  nscoord offset = 0; // NOTE: this is the resulting frame offset (border box).
  switch (aAlignment) {
    case NS_STYLE_ALIGN_BASELINE:
    case NS_STYLE_ALIGN_LAST_BASELINE:
      if (MOZ_LIKELY(aSameSide == (aAlignment == NS_STYLE_ALIGN_BASELINE))) {
        offset = marginStart + aBaselineAdjust;
      } else {
        nscoord size = aAxis == eLogicalAxisBlock ? aChildSize.BSize(wm)
                                                  : aChildSize.ISize(wm);
        offset = aCBSize - (size + marginEnd) - aBaselineAdjust;
      }
      break;
    case NS_STYLE_ALIGN_STRETCH:
      MOZ_FALLTHROUGH; // ComputeSize() deals with it
    case NS_STYLE_ALIGN_START:
      offset = marginStart;
      break;
    case NS_STYLE_ALIGN_END: {
      nscoord size = aAxis == eLogicalAxisBlock ? aChildSize.BSize(wm)
                                                : aChildSize.ISize(wm);
      offset = aCBSize - (size + marginEnd);
      break;
    }
    case NS_STYLE_ALIGN_CENTER: {
      nscoord size = aAxis == eLogicalAxisBlock ? aChildSize.BSize(wm)
                                                : aChildSize.ISize(wm);
      offset = (aCBSize - size + marginStart - marginEnd) / 2;
      break;
    }
    default:
      MOZ_ASSERT_UNREACHABLE("unknown align-/justify-self value");
  }
  if (offset != 0) {
    nscoord& pos = aAxis == eLogicalAxisBlock ? aPos->B(wm) : aPos->I(wm);
    pos += MOZ_LIKELY(aSameSide) ? offset : -offset;
  }
}

static bool
SameSide(WritingMode aContainerWM, LogicalSide aContainerSide,
         WritingMode aChildWM, LogicalSide aChildSide)
{
  MOZ_ASSERT(aContainerWM.PhysicalAxis(GetAxis(aContainerSide)) ==
                 aChildWM.PhysicalAxis(GetAxis(aChildSide)));
  return aContainerWM.PhysicalSide(aContainerSide) ==
             aChildWM.PhysicalSide(aChildSide);
}

static void
AlignSelf(const nsGridContainerFrame::GridItemInfo& aGridItem,
          uint8_t aAlignSelf, const LogicalRect& aCB, const WritingMode aCBWM,
          const nsHTMLReflowState& aRS, const LogicalSize& aSize,
          LogicalPoint* aPos)
{
  auto alignSelf = aAlignSelf;
  bool overflowSafe = alignSelf & NS_STYLE_ALIGN_SAFE;
  alignSelf &= ~NS_STYLE_ALIGN_FLAG_BITS;
  // Grid's 'align-self' axis is never parallel to the container's inline axis.
  if (alignSelf == NS_STYLE_ALIGN_LEFT || alignSelf == NS_STYLE_ALIGN_RIGHT) {
    alignSelf = NS_STYLE_ALIGN_START;
  }
  if (MOZ_LIKELY(alignSelf == NS_STYLE_ALIGN_NORMAL)) {
    alignSelf = NS_STYLE_ALIGN_STRETCH;
  }
  WritingMode childWM = aRS.GetWritingMode();
  bool isOrthogonal = aCBWM.IsOrthogonalTo(childWM);
  // |sameSide| is true if the container's start side in this axis is the same
  // as the child's start side, in the child's parallel axis.
  bool sameSide = SameSide(aCBWM, eLogicalSideBStart,
                           childWM, isOrthogonal ? eLogicalSideIStart
                                                 : eLogicalSideBStart);
  nscoord baselineAdjust = 0;
  if (alignSelf == NS_STYLE_ALIGN_BASELINE ||
      alignSelf == NS_STYLE_ALIGN_LAST_BASELINE) {
    alignSelf = aGridItem.GetSelfBaseline(alignSelf, eLogicalAxisBlock,
                                          &baselineAdjust);
  }
  LogicalAxis axis = isOrthogonal ? eLogicalAxisInline : eLogicalAxisBlock;
  AlignJustifySelf(alignSelf, overflowSafe, axis, sameSide, baselineAdjust,
                   aCB.BSize(aCBWM), aRS, aSize, aPos);
}

static void
JustifySelf(const nsGridContainerFrame::GridItemInfo& aGridItem,
            uint8_t aJustifySelf, const LogicalRect& aCB, const WritingMode aCBWM,
            const nsHTMLReflowState& aRS, const LogicalSize& aSize,
            LogicalPoint* aPos)
{
  auto justifySelf = aJustifySelf;
  bool overflowSafe = justifySelf & NS_STYLE_JUSTIFY_SAFE;
  justifySelf &= ~NS_STYLE_JUSTIFY_FLAG_BITS;
  if (MOZ_LIKELY(justifySelf == NS_STYLE_ALIGN_NORMAL)) {
    justifySelf = NS_STYLE_ALIGN_STRETCH;
  }
  WritingMode childWM = aRS.GetWritingMode();
  bool isOrthogonal = aCBWM.IsOrthogonalTo(childWM);
  // |sameSide| is true if the container's start side in this axis is the same
  // as the child's start side, in the child's parallel axis.
  bool sameSide = SameSide(aCBWM, eLogicalSideIStart,
                           childWM, isOrthogonal ? eLogicalSideBStart
                                                 : eLogicalSideIStart);
  nscoord baselineAdjust = 0;
  // Grid's 'justify-self' axis is always parallel to the container's inline
  // axis, so justify-self:left|right always applies.
  switch (justifySelf) {
    case NS_STYLE_JUSTIFY_LEFT:
      justifySelf = aCBWM.IsBidiLTR() ? NS_STYLE_JUSTIFY_START
                                      : NS_STYLE_JUSTIFY_END;
      break;
    case NS_STYLE_JUSTIFY_RIGHT:
      justifySelf = aCBWM.IsBidiLTR() ? NS_STYLE_JUSTIFY_END
                                      : NS_STYLE_JUSTIFY_START;
      break;
    case NS_STYLE_JUSTIFY_BASELINE:
    case NS_STYLE_JUSTIFY_LAST_BASELINE:
      justifySelf = aGridItem.GetSelfBaseline(justifySelf, eLogicalAxisInline,
                                              &baselineAdjust);
      break;
  }

  LogicalAxis axis = isOrthogonal ? eLogicalAxisBlock : eLogicalAxisInline;
  AlignJustifySelf(justifySelf, overflowSafe, axis, sameSide, baselineAdjust,
                   aCB.ISize(aCBWM), aRS, aSize, aPos);
}

static uint16_t
GetAlignJustifyValue(uint16_t aAlignment, const WritingMode aWM,
                     const bool aIsAlign, bool* aOverflowSafe)
{
  *aOverflowSafe = aAlignment & NS_STYLE_ALIGN_SAFE;
  aAlignment &= (NS_STYLE_ALIGN_ALL_BITS & ~NS_STYLE_ALIGN_FLAG_BITS);

  // Map some alignment values to 'start' / 'end'.
  switch (aAlignment) {
    case NS_STYLE_ALIGN_LEFT:
    case NS_STYLE_ALIGN_RIGHT: {
      if (aIsAlign) {
        // Grid's 'align-content' axis is never parallel to the inline axis.
        return NS_STYLE_ALIGN_START;
      }
      bool isStart = aWM.IsBidiLTR() == (aAlignment == NS_STYLE_ALIGN_LEFT);
      return isStart ? NS_STYLE_ALIGN_START : NS_STYLE_ALIGN_END;
    }
    case NS_STYLE_ALIGN_FLEX_START: // same as 'start' for Grid
      return NS_STYLE_ALIGN_START;
    case NS_STYLE_ALIGN_FLEX_END: // same as 'end' for Grid
      return NS_STYLE_ALIGN_END;
  }
  return aAlignment;
}

static uint16_t
GetAlignJustifyFallbackIfAny(uint16_t aAlignment, const WritingMode aWM,
                             const bool aIsAlign, bool* aOverflowSafe)
{
  uint16_t fallback = aAlignment >> NS_STYLE_ALIGN_ALL_SHIFT;
  if (fallback) {
    return GetAlignJustifyValue(fallback, aWM, aIsAlign, aOverflowSafe);
  }
  // https://drafts.csswg.org/css-align-3/#fallback-alignment
  switch (aAlignment) {
    case NS_STYLE_ALIGN_STRETCH:
    case NS_STYLE_ALIGN_SPACE_BETWEEN:
      return NS_STYLE_ALIGN_START;
    case NS_STYLE_ALIGN_SPACE_AROUND:
    case NS_STYLE_ALIGN_SPACE_EVENLY:
      return NS_STYLE_ALIGN_CENTER;
  }
  return 0;
}

//----------------------------------------------------------------------

// Frame class boilerplate
// =======================

NS_QUERYFRAME_HEAD(nsGridContainerFrame)
  NS_QUERYFRAME_ENTRY(nsGridContainerFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

NS_IMPL_FRAMEARENA_HELPERS(nsGridContainerFrame)

nsContainerFrame*
NS_NewGridContainerFrame(nsIPresShell* aPresShell,
                         nsStyleContext* aContext)
{
  return new (aPresShell) nsGridContainerFrame(aContext);
}


//----------------------------------------------------------------------

// nsGridContainerFrame Method Implementations
// ===========================================

/*static*/ const nsRect&
nsGridContainerFrame::GridItemCB(nsIFrame* aChild)
{
  MOZ_ASSERT((aChild->GetStateBits() & NS_FRAME_OUT_OF_FLOW) &&
             aChild->IsAbsolutelyPositioned());
  nsRect* cb = static_cast<nsRect*>(aChild->Properties().Get(
                 GridItemContainingBlockRect()));
  MOZ_ASSERT(cb, "this method must only be called on grid items, and the grid "
                 "container should've reflowed this item by now and set up cb");
  return *cb;
}

void
nsGridContainerFrame::AddImplicitNamedAreas(
  const nsTArray<nsTArray<nsString>>& aLineNameLists)
{
  // http://dev.w3.org/csswg/css-grid/#implicit-named-areas
  // Note: recording these names for fast lookup later is just an optimization.
  const uint32_t len =
    std::min(aLineNameLists.Length(), size_t(nsStyleGridLine::kMaxLine));
  nsTHashtable<nsStringHashKey> currentStarts;
  ImplicitNamedAreas* areas = GetImplicitNamedAreas();
  for (uint32_t i = 0; i < len; ++i) {
    for (const nsString& name : aLineNameLists[i]) {
      uint32_t index;
      if (Grid::IsNameWithStartSuffix(name, &index) ||
          Grid::IsNameWithEndSuffix(name, &index)) {
        nsDependentSubstring area(name, 0, index);
        if (!areas) {
          areas = new ImplicitNamedAreas;
          Properties().Set(ImplicitNamedAreasProperty(), areas);
        }
        areas->PutEntry(area);
      }
    }
  }
}

void
nsGridContainerFrame::InitImplicitNamedAreas(const nsStylePosition* aStyle)
{
  ImplicitNamedAreas* areas = GetImplicitNamedAreas();
  if (areas) {
    // Clear it, but reuse the hashtable itself for now.  We'll remove it
    // below if it isn't needed anymore.
    areas->Clear();
  }
  AddImplicitNamedAreas(aStyle->mGridTemplateColumns.mLineNameLists);
  AddImplicitNamedAreas(aStyle->mGridTemplateRows.mLineNameLists);
  if (areas && areas->Count() == 0) {
    Properties().Delete(ImplicitNamedAreasProperty());
  }
}

int32_t
nsGridContainerFrame::Grid::ResolveLine(const nsStyleGridLine& aLine,
                                        int32_t aNth,
                                        uint32_t aFromIndex,
                                        const LineNameMap& aNameMap,
                                        uint32_t GridNamedArea::* aAreaStart,
                                        uint32_t GridNamedArea::* aAreaEnd,
                                        uint32_t aExplicitGridEnd,
                                        LineRangeSide aSide,
                                        const nsStylePosition* aStyle)
{
  MOZ_ASSERT(!aLine.IsAuto());
  int32_t line = 0;
  if (aLine.mLineName.IsEmpty()) {
    MOZ_ASSERT(aNth != 0, "css-grid 9.2: <integer> must not be zero.");
    line = int32_t(aFromIndex) + aNth;
  } else {
    if (aNth == 0) {
      // <integer> was omitted; treat it as 1.
      aNth = 1;
    }
    bool isNameOnly = !aLine.mHasSpan && aLine.mInteger == 0;
    if (isNameOnly) {
      const GridNamedArea* area = FindNamedArea(aLine.mLineName, aStyle);
      if (area || HasImplicitNamedArea(aLine.mLineName)) {
        // The given name is a named area - look for explicit lines named
        // <name>-start/-end depending on which side we're resolving.
        // http://dev.w3.org/csswg/css-grid/#grid-placement-slot
        uint32_t implicitLine = 0;
        nsAutoString lineName(aLine.mLineName);
        if (aSide == eLineRangeSideStart) {
          lineName.AppendLiteral("-start");
          implicitLine = area ? area->*aAreaStart : 0;
        } else {
          lineName.AppendLiteral("-end");
          implicitLine = area ? area->*aAreaEnd : 0;
        }
        line = aNameMap.FindNamedLine(lineName, &aNth, aFromIndex,
                                      implicitLine);
      }
    }

    if (line == 0) {
      // If mLineName ends in -start/-end, try the prefix as a named area.
      uint32_t implicitLine = 0;
      uint32_t index;
      auto GridNamedArea::* areaEdge = aAreaStart;
      bool found = IsNameWithStartSuffix(aLine.mLineName, &index);
      if (!found) {
        found = IsNameWithEndSuffix(aLine.mLineName, &index);
        areaEdge = aAreaEnd;
      }
      if (found) {
        const GridNamedArea* area =
          FindNamedArea(nsDependentSubstring(aLine.mLineName, 0, index),
                        aStyle);
        if (area) {
          implicitLine = area->*areaEdge;
        }
      }
      line = aNameMap.FindNamedLine(aLine.mLineName, &aNth, aFromIndex,
                                    implicitLine);
    }

    if (line == 0) {
      MOZ_ASSERT(aNth != 0, "we found all N named lines but 'line' is zero!");
      int32_t edgeLine;
      if (aLine.mHasSpan) {
        // http://dev.w3.org/csswg/css-grid/#grid-placement-span-int
        // 'span <custom-ident> N'
        edgeLine = aSide == eLineRangeSideStart ? 1 : aExplicitGridEnd;
      } else {
        // http://dev.w3.org/csswg/css-grid/#grid-placement-int
        // '<custom-ident> N'
        edgeLine = aNth < 0 ? 1 : aExplicitGridEnd;
      }
      // "If not enough lines with that name exist, all lines in the implicit
      // grid are assumed to have that name..."
      line = edgeLine + aNth;
    }
  }
  return clamped(line, nsStyleGridLine::kMinLine, nsStyleGridLine::kMaxLine);
}

nsGridContainerFrame::Grid::LinePair
nsGridContainerFrame::Grid::ResolveLineRangeHelper(
  const nsStyleGridLine& aStart,
  const nsStyleGridLine& aEnd,
  const LineNameMap& aNameMap,
  uint32_t GridNamedArea::* aAreaStart,
  uint32_t GridNamedArea::* aAreaEnd,
  uint32_t aExplicitGridEnd,
  const nsStylePosition* aStyle)
{
  MOZ_ASSERT(int32_t(nsGridContainerFrame::kAutoLine) > nsStyleGridLine::kMaxLine);

  if (aStart.mHasSpan) {
    if (aEnd.mHasSpan || aEnd.IsAuto()) {
      // http://dev.w3.org/csswg/css-grid/#grid-placement-errors
      if (aStart.mLineName.IsEmpty()) {
        // span <integer> / span *
        // span <integer> / auto
        return LinePair(kAutoLine, aStart.mInteger);
      }
      // span <custom-ident> / span *
      // span <custom-ident> / auto
      return LinePair(kAutoLine, 1); // XXX subgrid explicit size instead of 1?
    }

    uint32_t from = aEnd.mInteger < 0 ? aExplicitGridEnd + 1: 0;
    auto end = ResolveLine(aEnd, aEnd.mInteger, from, aNameMap, aAreaStart,
                           aAreaEnd, aExplicitGridEnd, eLineRangeSideEnd,
                           aStyle);
    int32_t span = aStart.mInteger == 0 ? 1 : aStart.mInteger;
    if (end <= 1) {
      // The end is at or before the first explicit line, thus all lines before
      // it match <custom-ident> since they're implicit.
      int32_t start = std::max(end - span, nsStyleGridLine::kMinLine);
      return LinePair(start, end);
    }
    auto start = ResolveLine(aStart, -span, end, aNameMap, aAreaStart,
                             aAreaEnd, aExplicitGridEnd, eLineRangeSideStart,
                             aStyle);
    return LinePair(start, end);
  }

  int32_t start = kAutoLine;
  if (aStart.IsAuto()) {
    if (aEnd.IsAuto()) {
      // auto / auto
      return LinePair(start, 1); // XXX subgrid explicit size instead of 1?
    }
    if (aEnd.mHasSpan) {
      if (aEnd.mLineName.IsEmpty()) {
        // auto / span <integer>
        MOZ_ASSERT(aEnd.mInteger != 0);
        return LinePair(start, aEnd.mInteger);
      }
      // http://dev.w3.org/csswg/css-grid/#grid-placement-errors
      // auto / span <custom-ident>
      return LinePair(start, 1); // XXX subgrid explicit size instead of 1?
    }
  } else {
    uint32_t from = aStart.mInteger < 0 ? aExplicitGridEnd + 1: 0;
    start = ResolveLine(aStart, aStart.mInteger, from, aNameMap,
                        aAreaStart, aAreaEnd, aExplicitGridEnd,
                        eLineRangeSideStart, aStyle);
    if (aEnd.IsAuto()) {
      // A "definite line / auto" should resolve the auto to 'span 1'.
      // The error handling in ResolveLineRange will make that happen and also
      // clamp the end line correctly if we return "start / start".
      return LinePair(start, start);
    }
  }

  uint32_t from;
  int32_t nth = aEnd.mInteger == 0 ? 1 : aEnd.mInteger;
  if (aEnd.mHasSpan) {
    if (MOZ_UNLIKELY(start < 0)) {
      if (aEnd.mLineName.IsEmpty()) {
        return LinePair(start, start + nth);
      }
      from = 0;
    } else {
      if (start >= int32_t(aExplicitGridEnd)) {
        // The start is at or after the last explicit line, thus all lines
        // after it match <custom-ident> since they're implicit.
        return LinePair(start, std::min(start + nth, nsStyleGridLine::kMaxLine));
      }
      from = start;
    }
  } else {
    from = aEnd.mInteger < 0 ? aExplicitGridEnd + 1: 0;
  }
  auto end = ResolveLine(aEnd, nth, from, aNameMap, aAreaStart,
                         aAreaEnd, aExplicitGridEnd, eLineRangeSideEnd, aStyle);
  if (start == int32_t(kAutoLine)) {
    // auto / definite line
    start = std::max(nsStyleGridLine::kMinLine, end - 1);
  }
  return LinePair(start, end);
}

nsGridContainerFrame::LineRange
nsGridContainerFrame::Grid::ResolveLineRange(
  const nsStyleGridLine& aStart,
  const nsStyleGridLine& aEnd,
  const LineNameMap& aNameMap,
  uint32_t GridNamedArea::* aAreaStart,
  uint32_t GridNamedArea::* aAreaEnd,
  uint32_t aExplicitGridEnd,
  const nsStylePosition* aStyle)
{
  LinePair r = ResolveLineRangeHelper(aStart, aEnd, aNameMap, aAreaStart,
                                      aAreaEnd, aExplicitGridEnd, aStyle);
  MOZ_ASSERT(r.second != int32_t(kAutoLine));

  if (r.first == int32_t(kAutoLine)) {
    // r.second is a span, clamp it to kMaxLine - 1 so that the returned
    // range has a HypotheticalEnd <= kMaxLine.
    // http://dev.w3.org/csswg/css-grid/#overlarge-grids
    r.second = std::min(r.second, nsStyleGridLine::kMaxLine - 1);
  } else {
    // http://dev.w3.org/csswg/css-grid/#grid-placement-errors
    if (r.first > r.second) {
      Swap(r.first, r.second);
    } else if (r.first == r.second) {
      if (MOZ_UNLIKELY(r.first == nsStyleGridLine::kMaxLine)) {
        r.first = nsStyleGridLine::kMaxLine - 1;
      }
      r.second = r.first + 1; // XXX subgrid explicit size instead of 1?
    }
  }
  return LineRange(r.first, r.second);
}

nsGridContainerFrame::GridArea
nsGridContainerFrame::Grid::PlaceDefinite(nsIFrame* aChild,
                                          const LineNameMap& aColLineNameMap,
                                          const LineNameMap& aRowLineNameMap,
                                          const nsStylePosition* aStyle)
{
  const nsStylePosition* itemStyle = aChild->StylePosition();
  return GridArea(
    ResolveLineRange(itemStyle->mGridColumnStart, itemStyle->mGridColumnEnd,
                     aColLineNameMap,
                     &GridNamedArea::mColumnStart, &GridNamedArea::mColumnEnd,
                     mExplicitGridColEnd, aStyle),
    ResolveLineRange(itemStyle->mGridRowStart, itemStyle->mGridRowEnd,
                     aRowLineNameMap,
                     &GridNamedArea::mRowStart, &GridNamedArea::mRowEnd,
                     mExplicitGridRowEnd, aStyle));
}

nsGridContainerFrame::LineRange
nsGridContainerFrame::Grid::ResolveAbsPosLineRange(
  const nsStyleGridLine& aStart,
  const nsStyleGridLine& aEnd,
  const LineNameMap& aNameMap,
  uint32_t GridNamedArea::* aAreaStart,
  uint32_t GridNamedArea::* aAreaEnd,
  uint32_t aExplicitGridEnd,
  int32_t aGridStart,
  int32_t aGridEnd,
  const nsStylePosition* aStyle)
{
  if (aStart.IsAuto()) {
    if (aEnd.IsAuto()) {
      return LineRange(kAutoLine, kAutoLine);
    }
    uint32_t from = aEnd.mInteger < 0 ? aExplicitGridEnd + 1: 0;
    int32_t end =
      ResolveLine(aEnd, aEnd.mInteger, from, aNameMap, aAreaStart,
                  aAreaEnd, aExplicitGridEnd, eLineRangeSideEnd, aStyle);
    if (aEnd.mHasSpan) {
      ++end;
    }
    // A line outside the existing grid is treated as 'auto' for abs.pos (10.1).
    end = AutoIfOutside(end, aGridStart, aGridEnd);
    return LineRange(kAutoLine, end);
  }

  if (aEnd.IsAuto()) {
    uint32_t from = aStart.mInteger < 0 ? aExplicitGridEnd + 1: 0;
    int32_t start =
      ResolveLine(aStart, aStart.mInteger, from, aNameMap, aAreaStart,
                  aAreaEnd, aExplicitGridEnd, eLineRangeSideStart, aStyle);
    if (aStart.mHasSpan) {
      start = std::max(aGridEnd - start, aGridStart);
    }
    start = AutoIfOutside(start, aGridStart, aGridEnd);
    return LineRange(start, kAutoLine);
  }

  LineRange r = ResolveLineRange(aStart, aEnd, aNameMap, aAreaStart,
                                 aAreaEnd, aExplicitGridEnd, aStyle);
  if (r.IsAuto()) {
    MOZ_ASSERT(aStart.mHasSpan && aEnd.mHasSpan, "span / span is the only case "
               "leading to IsAuto here -- we dealt with the other cases above");
    // The second span was ignored per 9.2.1.  For abs.pos., 10.1 says that this
    // case should result in "auto / auto" unlike normal flow grid items.
    return LineRange(kAutoLine, kAutoLine);
  }

  return LineRange(AutoIfOutside(r.mUntranslatedStart, aGridStart, aGridEnd),
                   AutoIfOutside(r.mUntranslatedEnd, aGridStart, aGridEnd));
}

nsGridContainerFrame::GridArea
nsGridContainerFrame::Grid::PlaceAbsPos(nsIFrame* aChild,
                                        const LineNameMap& aColLineNameMap,
                                        const LineNameMap& aRowLineNameMap,
                                        const nsStylePosition* aStyle)
{
  const nsStylePosition* itemStyle = aChild->StylePosition();
  int32_t gridColStart = 1 - mExplicitGridOffsetCol;
  int32_t gridRowStart = 1 - mExplicitGridOffsetRow;
  return GridArea(
    ResolveAbsPosLineRange(itemStyle->mGridColumnStart,
                           itemStyle->mGridColumnEnd,
                           aColLineNameMap,
                           &GridNamedArea::mColumnStart,
                           &GridNamedArea::mColumnEnd,
                           mExplicitGridColEnd, gridColStart, mGridColEnd,
                           aStyle),
    ResolveAbsPosLineRange(itemStyle->mGridRowStart,
                           itemStyle->mGridRowEnd,
                           aRowLineNameMap,
                           &GridNamedArea::mRowStart,
                           &GridNamedArea::mRowEnd,
                           mExplicitGridRowEnd, gridRowStart, mGridRowEnd,
                           aStyle));
}

uint32_t
nsGridContainerFrame::Grid::FindAutoCol(uint32_t aStartCol, uint32_t aLockedRow,
                                        const GridArea* aArea) const
{
  const uint32_t extent = aArea->mCols.Extent();
  const uint32_t iStart = aLockedRow;
  const uint32_t iEnd = iStart + aArea->mRows.Extent();
  uint32_t candidate = aStartCol;
  for (uint32_t i = iStart; i < iEnd; ) {
    if (i >= mCellMap.mCells.Length()) {
      break;
    }
    const nsTArray<CellMap::Cell>& cellsInRow = mCellMap.mCells[i];
    const uint32_t len = cellsInRow.Length();
    const uint32_t lastCandidate = candidate;
    // Find the first gap in the current row that's at least 'extent' wide.
    // ('gap' tracks how wide the current column gap is.)
    for (uint32_t j = candidate, gap = 0; j < len && gap < extent; ++j) {
      if (!cellsInRow[j].mIsOccupied) {
        ++gap;
        continue;
      }
      candidate = j + 1;
      gap = 0;
    }
    if (lastCandidate < candidate && i != iStart) {
      // Couldn't fit 'extent' tracks at 'lastCandidate' here so we must
      // restart from the beginning with the new 'candidate'.
      i = iStart;
    } else {
      ++i;
    }
  }
  return candidate;
}

void
nsGridContainerFrame::Grid::PlaceAutoCol(uint32_t aStartCol,
                                         GridArea* aArea) const
{
  MOZ_ASSERT(aArea->mRows.IsDefinite() && aArea->mCols.IsAuto());
  uint32_t col = FindAutoCol(aStartCol, aArea->mRows.mStart, aArea);
  aArea->mCols.ResolveAutoPosition(col, mExplicitGridOffsetCol);
  MOZ_ASSERT(aArea->IsDefinite());
}

uint32_t
nsGridContainerFrame::Grid::FindAutoRow(uint32_t aLockedCol, uint32_t aStartRow,
                                        const GridArea* aArea) const
{
  const uint32_t extent = aArea->mRows.Extent();
  const uint32_t jStart = aLockedCol;
  const uint32_t jEnd = jStart + aArea->mCols.Extent();
  const uint32_t iEnd = mCellMap.mCells.Length();
  uint32_t candidate = aStartRow;
  // Find the first gap in the rows that's at least 'extent' tall.
  // ('gap' tracks how tall the current row gap is.)
  for (uint32_t i = candidate, gap = 0; i < iEnd && gap < extent; ++i) {
    ++gap; // tentative, but we may reset it below if a column is occupied
    const nsTArray<CellMap::Cell>& cellsInRow = mCellMap.mCells[i];
    const uint32_t clampedJEnd = std::min<uint32_t>(jEnd, cellsInRow.Length());
    // Check if the current row is unoccupied from jStart to jEnd.
    for (uint32_t j = jStart; j < clampedJEnd; ++j) {
      if (cellsInRow[j].mIsOccupied) {
        // Couldn't fit 'extent' rows at 'candidate' here; we hit something
        // at row 'i'.  So, try the row after 'i' as our next candidate.
        candidate = i + 1;
        gap = 0;
        break;
      }
    }
  }
  return candidate;
}

void
nsGridContainerFrame::Grid::PlaceAutoRow(uint32_t aStartRow,
                                         GridArea* aArea) const
{
  MOZ_ASSERT(aArea->mCols.IsDefinite() && aArea->mRows.IsAuto());
  uint32_t row = FindAutoRow(aArea->mCols.mStart, aStartRow, aArea);
  aArea->mRows.ResolveAutoPosition(row, mExplicitGridOffsetRow);
  MOZ_ASSERT(aArea->IsDefinite());
}

void
nsGridContainerFrame::Grid::PlaceAutoAutoInRowOrder(uint32_t aStartCol,
                                                    uint32_t aStartRow,
                                                    GridArea* aArea) const
{
  MOZ_ASSERT(aArea->mCols.IsAuto() && aArea->mRows.IsAuto());
  const uint32_t colExtent = aArea->mCols.Extent();
  const uint32_t gridRowEnd = mGridRowEnd;
  const uint32_t gridColEnd = mGridColEnd;
  uint32_t col = aStartCol;
  uint32_t row = aStartRow;
  for (; row < gridRowEnd; ++row) {
    col = FindAutoCol(col, row, aArea);
    if (col + colExtent <= gridColEnd) {
      break;
    }
    col = 0;
  }
  MOZ_ASSERT(row < gridRowEnd || col == 0,
             "expected column 0 for placing in a new row");
  aArea->mCols.ResolveAutoPosition(col, mExplicitGridOffsetCol);
  aArea->mRows.ResolveAutoPosition(row, mExplicitGridOffsetRow);
  MOZ_ASSERT(aArea->IsDefinite());
}

void
nsGridContainerFrame::Grid::PlaceAutoAutoInColOrder(uint32_t aStartCol,
                                                    uint32_t aStartRow,
                                                    GridArea* aArea) const
{
  MOZ_ASSERT(aArea->mCols.IsAuto() && aArea->mRows.IsAuto());
  const uint32_t rowExtent = aArea->mRows.Extent();
  const uint32_t gridRowEnd = mGridRowEnd;
  const uint32_t gridColEnd = mGridColEnd;
  uint32_t col = aStartCol;
  uint32_t row = aStartRow;
  for (; col < gridColEnd; ++col) {
    row = FindAutoRow(col, row, aArea);
    if (row + rowExtent <= gridRowEnd) {
      break;
    }
    row = 0;
  }
  MOZ_ASSERT(col < gridColEnd || row == 0,
             "expected row 0 for placing in a new column");
  aArea->mCols.ResolveAutoPosition(col, mExplicitGridOffsetCol);
  aArea->mRows.ResolveAutoPosition(row, mExplicitGridOffsetRow);
  MOZ_ASSERT(aArea->IsDefinite());
}

void
nsGridContainerFrame::Grid::PlaceGridItems(GridReflowState& aState,
                                           const LogicalSize& aComputedMinSize,
                                           const LogicalSize& aComputedSize,
                                           const LogicalSize& aComputedMaxSize)
{
  mAreas = aState.mFrame->GetImplicitNamedAreas();
  const nsStylePosition* const gridStyle = aState.mGridStyle;
  MOZ_ASSERT(mCellMap.mCells.IsEmpty(), "unexpected entries in cell map");

  // http://dev.w3.org/csswg/css-grid/#grid-definition
  // Initialize the end lines of the Explicit Grid (mExplicitGridCol[Row]End).
  // This is determined by the larger of the number of rows/columns defined
  // by 'grid-template-areas' and the 'grid-template-rows'/'-columns', plus one.
  // Also initialize the Implicit Grid (mGridCol[Row]End) to the same values.
  // Note that this is for a grid with a 1,1 origin.  We'll change that
  // to a 0,0 based grid after placing definite lines.
  auto areas = gridStyle->mGridTemplateAreas.get();
  uint32_t numRepeatCols = aState.mColFunctions.InitRepeatTracks(
                             gridStyle->mGridColumnGap,
                             aComputedMinSize.ISize(aState.mWM),
                             aComputedSize.ISize(aState.mWM),
                             aComputedMaxSize.ISize(aState.mWM));
  mGridColEnd = mExplicitGridColEnd =
    aState.mColFunctions.ComputeExplicitGridEnd(areas ? areas->mNColumns + 1 : 1);
  LineNameMap colLineNameMap(gridStyle->mGridTemplateColumns, numRepeatCols);

  uint32_t numRepeatRows = aState.mRowFunctions.InitRepeatTracks(
                             gridStyle->mGridRowGap,
                             aComputedMinSize.BSize(aState.mWM),
                             aComputedSize.BSize(aState.mWM),
                             aComputedMaxSize.BSize(aState.mWM));
  mGridRowEnd = mExplicitGridRowEnd =
    aState.mRowFunctions.ComputeExplicitGridEnd(areas ? areas->NRows() + 1 : 1);
  LineNameMap rowLineNameMap(gridStyle->mGridTemplateRows, numRepeatRows);

  // http://dev.w3.org/csswg/css-grid/#line-placement
  // Resolve definite positions per spec chap 9.2.
  int32_t minCol = 1;
  int32_t minRow = 1;
  aState.mGridItems.ClearAndRetainStorage();
  aState.mIter.Reset();
  for (; !aState.mIter.AtEnd(); aState.mIter.Next()) {
    nsIFrame* child = *aState.mIter;
    GridItemInfo* info =
        aState.mGridItems.AppendElement(GridItemInfo(child,
                                          PlaceDefinite(child,
                                                        colLineNameMap,
                                                        rowLineNameMap,
                                                        gridStyle)));
    MOZ_ASSERT(aState.mIter.GridItemIndex() == aState.mGridItems.Length() - 1,
               "GridItemIndex() is broken");
    GridArea& area = info->mArea;
    if (area.mCols.IsDefinite()) {
      minCol = std::min(minCol, area.mCols.mUntranslatedStart);
    }
    if (area.mRows.IsDefinite()) {
      minRow = std::min(minRow, area.mRows.mUntranslatedStart);
    }
  }

  // Translate the whole grid so that the top-/left-most area is at 0,0.
  mExplicitGridOffsetCol = 1 - minCol; // minCol/Row is always <= 1, see above
  mExplicitGridOffsetRow = 1 - minRow;
  aState.mColFunctions.mExplicitGridOffset = mExplicitGridOffsetCol;
  aState.mRowFunctions.mExplicitGridOffset = mExplicitGridOffsetRow;
  const int32_t offsetToColZero = int32_t(mExplicitGridOffsetCol) - 1;
  const int32_t offsetToRowZero = int32_t(mExplicitGridOffsetRow) - 1;
  mGridColEnd += offsetToColZero;
  mGridRowEnd += offsetToRowZero;
  aState.mIter.Reset();
  for (; !aState.mIter.AtEnd(); aState.mIter.Next()) {
    GridArea& area = aState.mGridItems[aState.mIter.GridItemIndex()].mArea;
    if (area.mCols.IsDefinite()) {
      area.mCols.mStart = area.mCols.mUntranslatedStart + offsetToColZero;
      area.mCols.mEnd = area.mCols.mUntranslatedEnd + offsetToColZero;
    }
    if (area.mRows.IsDefinite()) {
      area.mRows.mStart = area.mRows.mUntranslatedStart + offsetToRowZero;
      area.mRows.mEnd = area.mRows.mUntranslatedEnd + offsetToRowZero;
    }
    if (area.IsDefinite()) {
      mCellMap.Fill(area);
      InflateGridFor(area);
    }
  }

  // http://dev.w3.org/csswg/css-grid/#auto-placement-algo
  // Step 1, place 'auto' items that have one definite position -
  // definite row (column) for grid-auto-flow:row (column).
  auto flowStyle = gridStyle->mGridAutoFlow;
  const bool isRowOrder = (flowStyle & NS_STYLE_GRID_AUTO_FLOW_ROW);
  const bool isSparse = !(flowStyle & NS_STYLE_GRID_AUTO_FLOW_DENSE);
  // We need 1 cursor per row (or column) if placement is sparse.
  {
    Maybe<nsDataHashtable<nsUint32HashKey, uint32_t>> cursors;
    if (isSparse) {
      cursors.emplace();
    }
    auto placeAutoMinorFunc = isRowOrder ? &Grid::PlaceAutoCol
                                         : &Grid::PlaceAutoRow;
    aState.mIter.Reset();
    for (; !aState.mIter.AtEnd(); aState.mIter.Next()) {
      GridArea& area = aState.mGridItems[aState.mIter.GridItemIndex()].mArea;
      LineRange& major = isRowOrder ? area.mRows : area.mCols;
      LineRange& minor = isRowOrder ? area.mCols : area.mRows;
      if (major.IsDefinite() && minor.IsAuto()) {
        // Items with 'auto' in the minor dimension only.
        uint32_t cursor = 0;
        if (isSparse) {
          cursors->Get(major.mStart, &cursor);
        }
        (this->*placeAutoMinorFunc)(cursor, &area);
        mCellMap.Fill(area);
        if (isSparse) {
          cursors->Put(major.mStart, minor.mEnd);
        }
      }
      InflateGridFor(area);  // Step 2, inflating for auto items too
    }
  }

  // XXX NOTE possible spec issue.
  // XXX It's unclear if the remaining major-dimension auto and
  // XXX auto in both dimensions should use the same cursor or not,
  // XXX https://www.w3.org/Bugs/Public/show_bug.cgi?id=16044
  // XXX seems to indicate it shouldn't.
  // XXX http://dev.w3.org/csswg/css-grid/#auto-placement-cursor
  // XXX now says it should (but didn't in earlier versions)

  // Step 3, place the remaining grid items
  uint32_t cursorMajor = 0; // for 'dense' these two cursors will stay at 0,0
  uint32_t cursorMinor = 0;
  auto placeAutoMajorFunc = isRowOrder ? &Grid::PlaceAutoRow
                                       : &Grid::PlaceAutoCol;
  aState.mIter.Reset();
  for (; !aState.mIter.AtEnd(); aState.mIter.Next()) {
    GridArea& area = aState.mGridItems[aState.mIter.GridItemIndex()].mArea;
    MOZ_ASSERT(*aState.mIter == aState.mGridItems[aState.mIter.GridItemIndex()].mFrame,
               "iterator out of sync with aState.mGridItems");
    LineRange& major = isRowOrder ? area.mRows : area.mCols;
    LineRange& minor = isRowOrder ? area.mCols : area.mRows;
    if (major.IsAuto()) {
      if (minor.IsDefinite()) {
        // Items with 'auto' in the major dimension only.
        if (isSparse) {
          if (minor.mStart < cursorMinor) {
            ++cursorMajor;
          }
          cursorMinor = minor.mStart;
        }
        (this->*placeAutoMajorFunc)(cursorMajor, &area);
        if (isSparse) {
          cursorMajor = major.mStart;
        }
      } else {
        // Items with 'auto' in both dimensions.
        if (isRowOrder) {
          PlaceAutoAutoInRowOrder(cursorMinor, cursorMajor, &area);
        } else {
          PlaceAutoAutoInColOrder(cursorMajor, cursorMinor, &area);
        }
        if (isSparse) {
          cursorMajor = major.mStart;
          cursorMinor = minor.mEnd;
#ifdef DEBUG
          uint32_t gridMajorEnd = isRowOrder ? mGridRowEnd : mGridColEnd;
          uint32_t gridMinorEnd = isRowOrder ? mGridColEnd : mGridRowEnd;
          MOZ_ASSERT(cursorMajor <= gridMajorEnd,
                     "we shouldn't need to place items further than 1 track "
                     "past the current end of the grid, in major dimension");
          MOZ_ASSERT(cursorMinor <= gridMinorEnd,
                     "we shouldn't add implicit minor tracks for auto/auto");
#endif
        }
      }
      mCellMap.Fill(area);
      InflateGridFor(area);
    }
  }

  if (aState.mFrame->IsAbsoluteContainer()) {
    // 9.4 Absolutely-positioned Grid Items
    // http://dev.w3.org/csswg/css-grid/#abspos-items
    // We only resolve definite lines here; we'll align auto positions to the
    // grid container later during reflow.
    nsFrameList children(aState.mFrame->GetChildList(
                           aState.mFrame->GetAbsoluteListID()));
    const int32_t offsetToColZero = int32_t(mExplicitGridOffsetCol) - 1;
    const int32_t offsetToRowZero = int32_t(mExplicitGridOffsetRow) - 1;
    // Untranslate the grid again temporarily while resolving abs.pos. lines.
    AutoRestore<uint32_t> save1(mGridColEnd);
    AutoRestore<uint32_t> save2(mGridRowEnd);
    mGridColEnd -= offsetToColZero;
    mGridRowEnd -= offsetToRowZero;
    aState.mAbsPosItems.ClearAndRetainStorage();
    size_t i = 0;
    for (nsFrameList::Enumerator e(children); !e.AtEnd(); e.Next(), ++i) {
      nsIFrame* child = e.get();
      GridItemInfo* info =
          aState.mAbsPosItems.AppendElement(GridItemInfo(child,
                                              PlaceAbsPos(child,
                                                          colLineNameMap,
                                                          rowLineNameMap,
                                                          gridStyle)));
      GridArea& area = info->mArea;
      if (area.mCols.mUntranslatedStart != int32_t(kAutoLine)) {
        area.mCols.mStart = area.mCols.mUntranslatedStart + offsetToColZero;
      }
      if (area.mCols.mUntranslatedEnd != int32_t(kAutoLine)) {
        area.mCols.mEnd = area.mCols.mUntranslatedEnd + offsetToColZero;
      }
      if (area.mRows.mUntranslatedStart != int32_t(kAutoLine)) {
        area.mRows.mStart = area.mRows.mUntranslatedStart + offsetToRowZero;
      }
      if (area.mRows.mUntranslatedEnd != int32_t(kAutoLine)) {
        area.mRows.mEnd = area.mRows.mUntranslatedEnd + offsetToRowZero;
      }
    }
  }

  // Count empty 'auto-fit' tracks in the repeat() range.
  // |colAdjust| will have a count for each line in the grid of how many
  // tracks were empty between the start of the grid and that line.
  Maybe<nsTArray<uint32_t>> colAdjust;
  uint32_t numEmptyCols = 0;
  if (aState.mColFunctions.mHasRepeatAuto &&
      !gridStyle->mGridTemplateColumns.mIsAutoFill &&
      aState.mColFunctions.NumRepeatTracks() > 0) {
    for (uint32_t col = aState.mColFunctions.mRepeatAutoStart,
           endRepeat = aState.mColFunctions.mRepeatAutoEnd,
           numColLines = mGridColEnd + 1;
         col < numColLines; ++col) {
      if (numEmptyCols) {
        (*colAdjust)[col] = numEmptyCols;
      }
      if (col < endRepeat && mCellMap.IsEmptyCol(col)) {
        ++numEmptyCols;
        if (colAdjust.isNothing()) {
          colAdjust.emplace(numColLines);
          colAdjust->SetLength(numColLines);
          PodZero(colAdjust->Elements(), colAdjust->Length());
        }
      }
    }
  }
  Maybe<nsTArray<uint32_t>> rowAdjust;
  uint32_t numEmptyRows = 0;
  if (aState.mRowFunctions.mHasRepeatAuto &&
      !gridStyle->mGridTemplateRows.mIsAutoFill &&
      aState.mRowFunctions.NumRepeatTracks() > 0) {
    for (uint32_t row = aState.mRowFunctions.mRepeatAutoStart,
           endRepeat = aState.mRowFunctions.mRepeatAutoEnd,
           numRowLines = mGridRowEnd + 1;
         row < numRowLines; ++row) {
      if (numEmptyRows) {
        (*rowAdjust)[row] = numEmptyRows;
      }
      if (row < endRepeat && mCellMap.IsEmptyRow(row)) {
        ++numEmptyRows;
        if (rowAdjust.isNothing()) {
          rowAdjust.emplace(numRowLines);
          rowAdjust->SetLength(numRowLines);
          PodZero(rowAdjust->Elements(), rowAdjust->Length());
        }
      }
    }
  }
  // Remove the empty 'auto-fit' tracks we found above, if any.
  if (numEmptyCols || numEmptyRows) {
    // Adjust the line numbers in the grid areas.
    for (auto& item : aState.mGridItems) {
      GridArea& area = item.mArea;
      if (numEmptyCols) {
        area.mCols.AdjustForRemovedTracks(*colAdjust);
      }
      if (numEmptyRows) {
        area.mRows.AdjustForRemovedTracks(*rowAdjust);
      }
    }
    for (auto& item : aState.mAbsPosItems) {
      GridArea& area = item.mArea;
      if (numEmptyCols) {
        area.mCols.AdjustAbsPosForRemovedTracks(*colAdjust);
      }
      if (numEmptyRows) {
        area.mRows.AdjustAbsPosForRemovedTracks(*rowAdjust);
      }
    }
    // Adjust the grid size.
    mGridColEnd -= numEmptyCols;
    mExplicitGridColEnd -= numEmptyCols;
    mGridRowEnd -= numEmptyRows;
    mExplicitGridRowEnd -= numEmptyRows;
    // Adjust the track mapping to unmap the removed tracks.
    auto finalColRepeatCount = aState.mColFunctions.NumRepeatTracks() - numEmptyCols;
    aState.mColFunctions.SetNumRepeatTracks(finalColRepeatCount);
    auto finalRowRepeatCount = aState.mRowFunctions.NumRepeatTracks() - numEmptyRows;
    aState.mRowFunctions.SetNumRepeatTracks(finalRowRepeatCount);
  }
}

void
nsGridContainerFrame::Tracks::Initialize(
  const TrackSizingFunctions& aFunctions,
  const nsStyleCoord&         aGridGap,
  uint32_t                    aNumTracks,
  nscoord                     aContentBoxSize)
{
  MOZ_ASSERT(aNumTracks >= aFunctions.mExplicitGridOffset +
                             aFunctions.NumExplicitTracks());
  mSizes.SetLength(aNumTracks);
  PodZero(mSizes.Elements(), mSizes.Length());
  for (uint32_t i = 0, len = mSizes.Length(); i < len; ++i) {
    mSizes[i].Initialize(aContentBoxSize,
                         aFunctions.MinSizingFor(i),
                         aFunctions.MaxSizingFor(i));
  }
  auto gap = nsRuleNode::ComputeCoordPercentCalc(aGridGap, aContentBoxSize);
  mGridGap = std::max(nscoord(0), gap);
  mContentBoxSize = aContentBoxSize;
}

/**
 * Reflow aChild in the given aAvailableSize.
 */
static nscoord
MeasuringReflow(nsIFrame*                aChild,
                const nsHTMLReflowState* aReflowState,
                nsRenderingContext*      aRC,
                const LogicalSize&       aAvailableSize)
{
  nsContainerFrame* parent = aChild->GetParent();
  nsPresContext* pc = aChild->PresContext();
  Maybe<nsHTMLReflowState> dummyParentState;
  const nsHTMLReflowState* rs = aReflowState;
  if (!aReflowState) {
    MOZ_ASSERT(!parent->HasAnyStateBits(NS_FRAME_IN_REFLOW));
    dummyParentState.emplace(pc, parent, aRC,
                             LogicalSize(parent->GetWritingMode(), 0,
                                         NS_UNCONSTRAINEDSIZE),
                             nsHTMLReflowState::DUMMY_PARENT_REFLOW_STATE);
    rs = dummyParentState.ptr();
  }
#ifdef DEBUG
  // This will suppress various CRAZY_SIZE warnings for this reflow.
  parent->Properties().Set(
    nsContainerFrame::DebugReflowingWithInfiniteISize(), true);
#endif
  nsHTMLReflowState childRS(pc, *rs, aChild, aAvailableSize, nullptr,
                            nsHTMLReflowState::COMPUTE_SIZE_SHRINK_WRAP |
                            nsHTMLReflowState::COMPUTE_SIZE_USE_AUTO_BSIZE);
  nsHTMLReflowMetrics childSize(childRS);
  nsReflowStatus childStatus;
  const uint32_t flags = NS_FRAME_NO_MOVE_FRAME | NS_FRAME_NO_SIZE_VIEW;
  WritingMode wm = childRS.GetWritingMode();
  parent->ReflowChild(aChild, pc, childSize, childRS, wm,
                      LogicalPoint(wm), nsSize(), flags, childStatus);
  parent->FinishReflowChild(aChild, pc, childSize, &childRS, wm,
                            LogicalPoint(wm), nsSize(), flags);
#ifdef DEBUG
    parent->Properties().Delete(nsContainerFrame::DebugReflowingWithInfiniteISize());
#endif
  return childSize.BSize(wm);
}

/**
 * Return the [min|max]-content contribution of aChild to its parent (i.e.
 * the child's margin-box) in aAxis.
 */
static nscoord
ContentContribution(const GridItemInfo&               aGridItem,
                    const nsHTMLReflowState*          aReflowState,
                    nsRenderingContext*               aRC,
                    WritingMode                       aCBWM,
                    LogicalAxis                       aAxis,
                    nsLayoutUtils::IntrinsicISizeType aConstraint,
                    uint32_t                          aFlags = 0)
{
  nsIFrame* child = aGridItem.mFrame;
  PhysicalAxis axis(aCBWM.PhysicalAxis(aAxis));
  nscoord size = nsLayoutUtils::IntrinsicForAxis(axis, aRC, child, aConstraint,
                   aFlags | nsLayoutUtils::BAIL_IF_REFLOW_NEEDED);
  if (size == NS_INTRINSIC_WIDTH_UNKNOWN) {
    // We need to reflow the child to find its BSize contribution.
    // XXX this will give mostly correct results for now (until bug 1174569).
    LogicalSize availableSize(child->GetWritingMode(), INFINITE_ISIZE_COORD, NS_UNCONSTRAINEDSIZE);
    size = ::MeasuringReflow(child, aReflowState, aRC, availableSize);
    nsIFrame::IntrinsicISizeOffsetData offsets = child->IntrinsicBSizeOffsets();
    size += offsets.hMargin;
    size = nsLayoutUtils::AddPercents(aConstraint, size, offsets.hPctMargin);
  }
  MOZ_ASSERT(aGridItem.mBaselineOffset[aAxis] >= 0,
             "baseline offset should be non-negative at this point");
  MOZ_ASSERT((aGridItem.mState[aAxis] & ItemState::eIsBaselineAligned) ||
             aGridItem.mBaselineOffset[aAxis] == nscoord(0),
             "baseline offset should be zero when not baseline-aligned");
  size += aGridItem.mBaselineOffset[aAxis];
  return std::max(size, 0);
}

static nscoord
MinContentContribution(const GridItemInfo&      aGridItem,
                       const nsHTMLReflowState* aRS,
                       nsRenderingContext*      aRC,
                       WritingMode              aCBWM,
                       LogicalAxis              aAxis)
{
  return ContentContribution(aGridItem, aRS, aRC, aCBWM, aAxis,
                             nsLayoutUtils::MIN_ISIZE);
}

static nscoord
MaxContentContribution(const GridItemInfo&      aGridItem,
                       const nsHTMLReflowState* aRS,
                       nsRenderingContext*      aRC,
                       WritingMode              aCBWM,
                       LogicalAxis              aAxis)
{
  return ContentContribution(aGridItem, aRS, aRC, aCBWM, aAxis,
                             nsLayoutUtils::PREF_ISIZE);
}

static nscoord
MinSize(const GridItemInfo&      aGridItem,
        const nsHTMLReflowState* aRS,
        nsRenderingContext*      aRC,
        WritingMode              aCBWM,
        LogicalAxis              aAxis)
{
  nsIFrame* child = aGridItem.mFrame;
  PhysicalAxis axis(aCBWM.PhysicalAxis(aAxis));
  const nsStylePosition* stylePos = child->StylePosition();
  const nsStyleCoord& style = axis == eAxisHorizontal ? stylePos->mMinWidth
                                                      : stylePos->mMinHeight;
  // https://drafts.csswg.org/css-grid/#min-size-auto
  // This calculates the min-content contribution from either a definite
  // min-width (or min-height depending on aAxis), or the "specified /
  // transferred size" for min-width:auto if overflow == visible (as min-width:0
  // otherwise), or NS_UNCONSTRAINEDSIZE for other min-width intrinsic values
  // (which results in always taking the "content size" part below).
  MOZ_ASSERT(aGridItem.mBaselineOffset[aAxis] >= 0,
             "baseline offset should be non-negative at this point");
  MOZ_ASSERT((aGridItem.mState[aAxis] & ItemState::eIsBaselineAligned) ||
             aGridItem.mBaselineOffset[aAxis] == nscoord(0),
             "baseline offset should be zero when not baseline-aligned");
  nscoord sz = aGridItem.mBaselineOffset[aAxis] +
    nsLayoutUtils::MinSizeContributionForAxis(axis, aRC, child,
                                              nsLayoutUtils::MIN_ISIZE);
  auto unit = style.GetUnit();
  if (unit == eStyleUnit_Enumerated ||
      (unit == eStyleUnit_Auto &&
       child->StyleDisplay()->mOverflowX == NS_STYLE_OVERFLOW_VISIBLE)) {
    // Now calculate the "content size" part and return whichever is smaller.
    MOZ_ASSERT(unit != eStyleUnit_Enumerated || sz == NS_UNCONSTRAINEDSIZE);
    sz = std::min(sz, ContentContribution(aGridItem, aRS, aRC, aCBWM, aAxis,
                                          nsLayoutUtils::MIN_ISIZE,
                                          nsLayoutUtils::MIN_INTRINSIC_ISIZE));
  }
  return sz;
}

void
nsGridContainerFrame::Tracks::CalculateSizes(
  GridReflowState&            aState,
  nsTArray<GridItemInfo>&     aGridItems,
  const TrackSizingFunctions& aFunctions,
  nscoord                     aContentBoxSize,
  LineRange GridArea::*       aRange,
  IntrinsicISizeType          aConstraint)
{
  nscoord percentageBasis = aContentBoxSize;
  if (percentageBasis == NS_UNCONSTRAINEDSIZE) {
    percentageBasis = 0;
  }
  InitializeItemBaselines(aState, aGridItems);
  ResolveIntrinsicSize(aState, aGridItems, aFunctions, aRange, percentageBasis,
                       aConstraint);
  if (aConstraint != nsLayoutUtils::MIN_ISIZE) {
    nscoord freeSpace = aContentBoxSize;
    if (freeSpace != NS_UNCONSTRAINEDSIZE) {
      freeSpace -= SumOfGridGaps();
    }
    DistributeFreeSpace(freeSpace);
    StretchFlexibleTracks(aState, aGridItems, aFunctions, freeSpace);
  }
}

bool
nsGridContainerFrame::Tracks::HasIntrinsicButNoFlexSizingInRange(
  const LineRange&      aRange,
  IntrinsicISizeType    aConstraint,
  TrackSize::StateBits* aState) const
{
  MOZ_ASSERT(!aRange.IsAuto(), "must have a definite range");
  const uint32_t start = aRange.mStart;
  const uint32_t end = aRange.mEnd;
  const TrackSize::StateBits selector =
    TrackSize::eIntrinsicMinSizing |
    TrackSize::eIntrinsicMaxSizing |
    (aConstraint == nsLayoutUtils::MIN_ISIZE ? TrackSize::eFlexMinSizing
                                             : TrackSize::StateBits(0));
  bool foundIntrinsic = false;
  for (uint32_t i = start; i < end; ++i) {
    TrackSize::StateBits state = mSizes[i].mState;
    *aState |= state;
    if (state & TrackSize::eFlexMaxSizing) {
      return false;
    }
    if (state & selector) {
      foundIntrinsic = true;
    }
  }
  return foundIntrinsic;
}

bool
nsGridContainerFrame::Tracks::ResolveIntrinsicSizeStep1(
  GridReflowState&            aState,
  const TrackSizingFunctions& aFunctions,
  nscoord                     aPercentageBasis,
  IntrinsicISizeType          aConstraint,
  const LineRange&            aRange,
  const GridItemInfo&         aGridItem)
{
  Maybe<nscoord> minContentContribution;
  Maybe<nscoord> maxContentContribution;
  // min sizing
  TrackSize& sz = mSizes[aRange.mStart];
  WritingMode wm = aState.mWM;
  const nsHTMLReflowState* rs = aState.mReflowState;
  nsRenderingContext* rc = &aState.mRenderingContext;
  if (sz.mState & TrackSize::eAutoMinSizing) {
    nscoord s = MinSize(aGridItem, rs, rc, wm, mAxis);
    sz.mBase = std::max(sz.mBase, s);
  } else if ((sz.mState & TrackSize::eMinContentMinSizing) ||
             (aConstraint == nsLayoutUtils::MIN_ISIZE &&
              (sz.mState & TrackSize::eFlexMinSizing))) {
    nscoord s = MinContentContribution(aGridItem, rs, rc, wm, mAxis);
    minContentContribution.emplace(s);
    sz.mBase = std::max(sz.mBase, minContentContribution.value());
  } else if (sz.mState & TrackSize::eMaxContentMinSizing) {
    nscoord s = MaxContentContribution(aGridItem, rs, rc, wm, mAxis);
    maxContentContribution.emplace(s);
    sz.mBase = std::max(sz.mBase, maxContentContribution.value());
  }
  // max sizing
  if (sz.mState & TrackSize::eMinContentMaxSizing) {
    if (minContentContribution.isNothing()) {
      nscoord s = MinContentContribution(aGridItem, rs, rc, wm, mAxis);
      minContentContribution.emplace(s);
    }
    if (sz.mLimit == NS_UNCONSTRAINEDSIZE) {
      sz.mLimit = minContentContribution.value();
    } else {
      sz.mLimit = std::max(sz.mLimit, minContentContribution.value());
    }
  } else if (sz.mState & (TrackSize::eAutoMaxSizing |
                          TrackSize::eMaxContentMaxSizing)) {
    if (maxContentContribution.isNothing()) {
      nscoord s = MaxContentContribution(aGridItem, rs, rc, wm, mAxis);
      maxContentContribution.emplace(s);
    }
    if (sz.mLimit == NS_UNCONSTRAINEDSIZE) {
      sz.mLimit = maxContentContribution.value();
    } else {
      sz.mLimit = std::max(sz.mLimit, maxContentContribution.value());
    }
  }
  if (sz.mLimit < sz.mBase) {
    sz.mLimit = sz.mBase;
  }
  return sz.mState & TrackSize::eFlexMaxSizing;
}

void
nsGridContainerFrame::Tracks::CalculateItemBaselines(
  nsTArray<ItemBaselineData>& aBaselineItems,
  BaselineSharingGroup aBaselineGroup)
{
  if (aBaselineItems.IsEmpty()) {
    return;
  }

  // Sort the collected items on their baseline track.
  std::sort(aBaselineItems.begin(), aBaselineItems.end(),
            ItemBaselineData::IsBaselineTrackLessThan);

  nscoord maxBaseline = 0;
  nscoord maxDescent = 0;
  uint32_t currentTrack = kAutoLine; // guaranteed to not match any item
  uint32_t trackStartIndex = 0;
  for (uint32_t i = 0, len = aBaselineItems.Length(); true ; ++i) {
    // Find the maximum baseline and descent in the current track.
    if (i != len) {
      const ItemBaselineData& item = aBaselineItems[i];
      if (currentTrack == item.mBaselineTrack) {
        maxBaseline = std::max(maxBaseline, item.mBaseline);
        maxDescent = std::max(maxDescent, item.mSize - item.mBaseline);
        continue;
      }
    }
    // Iterate the current track again and update the baseline offsets making
    // all items baseline-aligned within this group in this track.
    for (uint32_t j = trackStartIndex; j < i; ++j) {
      const ItemBaselineData& item = aBaselineItems[j];
      item.mGridItem->mBaselineOffset[mAxis] = maxBaseline - item.mBaseline;
      MOZ_ASSERT(item.mGridItem->mBaselineOffset[mAxis] >= 0);
    }
    // Store the size of this baseline-aligned subtree.
    if (i != 0) {
      mSizes[currentTrack].mBaselineSubtreeSize[aBaselineGroup] =
        maxBaseline + maxDescent;
    }
    if (i == len) {
      break;
    }
    // Initialize data for the next track with baseline-aligned items.
    const ItemBaselineData& item = aBaselineItems[i];
    currentTrack = item.mBaselineTrack;
    trackStartIndex = i;
    maxBaseline = item.mBaseline;
    maxDescent = item.mSize - item.mBaseline;
  }
}

void
nsGridContainerFrame::Tracks::InitializeItemBaselines(
  GridReflowState&        aState,
  nsTArray<GridItemInfo>& aGridItems)
{

  nsTArray<ItemBaselineData> firstBaselineItems;
  nsTArray<ItemBaselineData> lastBaselineItems;
  WritingMode wm = aState.mWM;
  nsStyleContext* containerSC = aState.mFrame->StyleContext();
  GridItemCSSOrderIterator& iter = aState.mIter;
  iter.Reset();
  for (; !iter.AtEnd(); iter.Next()) {
    nsIFrame* child = *iter;
    GridItemInfo& gridItem = aGridItems[iter.GridItemIndex()];
    uint32_t baselineTrack = kAutoLine;
    auto state = ItemState(0);
    auto childWM = child->GetWritingMode();
    const bool isOrthogonal = wm.IsOrthogonalTo(childWM);
    const bool isInlineAxis = mAxis == eLogicalAxisInline; // i.e. columns
    // XXX update the line below to include orthogonal grid/table boxes
    // XXX since they have baselines in both dimensions. And flexbox with
    // XXX reversed main/cross axis?
    const bool itemHasBaselineParallelToTrack = isInlineAxis == isOrthogonal;
    if (itemHasBaselineParallelToTrack) {
      // [align|justify]-self:[last-]baseline.
      auto selfAlignment = isOrthogonal ?
        child->StylePosition()->ComputedJustifySelf(containerSC) :
        child->StylePosition()->ComputedAlignSelf(containerSC);
      selfAlignment &= ~NS_STYLE_ALIGN_FLAG_BITS;
      if (selfAlignment == NS_STYLE_ALIGN_BASELINE) {
        state |= ItemState::eFirstBaseline | ItemState::eSelfBaseline;
        const GridArea& area = gridItem.mArea;
        baselineTrack = isOrthogonal ? area.mCols.mStart : area.mRows.mStart;
      } else if (selfAlignment == NS_STYLE_ALIGN_LAST_BASELINE) {
        state |= ItemState::eLastBaseline | ItemState::eSelfBaseline;
        const GridArea& area = gridItem.mArea;
        baselineTrack = (isOrthogonal ? area.mCols.mEnd : area.mRows.mEnd) - 1;
      }

      // [align|justify]-content:[last-]baseline.
      // https://drafts.csswg.org/css-align-3/#baseline-align-content
      // "[...] and its computed 'align-self' or 'justify-self' (whichever
      // affects its block axis) is 'stretch' or 'self-start' ('self-end').
      // For this purpose, the 'start', 'end', 'flex-start', and 'flex-end'
      // values of 'align-self' are treated as either 'self-start' or
      // 'self-end', whichever they end up equivalent to.
      auto alignContent = child->StylePosition()->ComputedAlignContent();
      alignContent &= ~NS_STYLE_ALIGN_FLAG_BITS;
      if (alignContent == NS_STYLE_ALIGN_BASELINE ||
          alignContent == NS_STYLE_ALIGN_LAST_BASELINE) {
        const auto selfAlignEdge = alignContent == NS_STYLE_ALIGN_BASELINE ?
          NS_STYLE_ALIGN_SELF_START : NS_STYLE_ALIGN_SELF_END;
        bool validCombo = selfAlignment == NS_STYLE_ALIGN_NORMAL ||
                          selfAlignment == NS_STYLE_ALIGN_STRETCH ||
                          selfAlignment == selfAlignEdge;
        if (!validCombo) {
          // |sameSide| is true if the container's start side in this axis is
          // the same as the child's start side, in the child's parallel axis.
          // XXX SameSide is way to complicated to use! - add a WritingMode
          // XXX method instead that does this in a better way.
          auto side = isInlineAxis ? eLogicalSideBStart : eLogicalSideIStart;
          // XXX the "isInlineAxis == isOrthogonal" is currently redundant, but
          // might be relevant if we change the itemHasBaselineParallelToTrack
          // condition above?
          auto childSide = isInlineAxis == isOrthogonal ? eLogicalSideIStart
                                                        : eLogicalSideBStart;
          bool sameSide = SameSide(wm, side, childWM, childSide);
          switch (selfAlignment) {
            case NS_STYLE_ALIGN_LEFT:
              selfAlignment = !isInlineAxis || wm.IsBidiLTR() ? NS_STYLE_ALIGN_START
                                                              : NS_STYLE_ALIGN_END;
              break;
            case NS_STYLE_ALIGN_RIGHT:
              selfAlignment = isInlineAxis && wm.IsBidiLTR() ? NS_STYLE_ALIGN_END
                                                             : NS_STYLE_ALIGN_START;
              break;
          }
          switch (selfAlignment) {
            case NS_STYLE_ALIGN_START:
            case NS_STYLE_ALIGN_FLEX_START:
              validCombo = sameSide ==
                           (alignContent == NS_STYLE_ALIGN_BASELINE);
              break;
            case NS_STYLE_ALIGN_END:
            case NS_STYLE_ALIGN_FLEX_END:
              validCombo = sameSide ==
                           (alignContent == NS_STYLE_ALIGN_LAST_BASELINE);
              break;
          }
        }
        if (validCombo) {
          const GridArea& area = gridItem.mArea;
          if (alignContent == NS_STYLE_ALIGN_BASELINE) {
            state |= ItemState::eFirstBaseline | ItemState::eContentBaseline;
            baselineTrack = isOrthogonal ? area.mCols.mStart : area.mRows.mStart;
          } else if (alignContent == NS_STYLE_ALIGN_LAST_BASELINE) {
            state |= ItemState::eLastBaseline | ItemState::eContentBaseline;
            baselineTrack = (isOrthogonal ? area.mCols.mEnd : area.mRows.mEnd) - 1;
          }
        }
      }
    }

    if (state & ItemState::eIsBaselineAligned) {
      // XXX available size issue
      LogicalSize avail(childWM, INFINITE_ISIZE_COORD, NS_UNCONSTRAINEDSIZE);
      auto* rc = &aState.mRenderingContext;
      // XXX figure out if we can avoid/merge this reflow with the main reflow.
      // XXX (after bug 1174569 is sorted out)
      ::MeasuringReflow(child, aState.mReflowState, rc, avail);
      nscoord baseline;
      if (state & ItemState::eFirstBaseline) {
        if (nsLayoutUtils::GetFirstLineBaseline(wm, child, &baseline)) {
          auto frameSize = isInlineAxis ? child->ISize(wm) : child->BSize(wm);
          auto m = child->GetLogicalUsedMargin(wm);
          baseline += isInlineAxis ? m.IStart(wm) : m.BStart(wm);
          auto alignSize = frameSize + (isInlineAxis ? m.IStartEnd(wm)
                                                     : m.BStartEnd(wm));
          firstBaselineItems.AppendElement(ItemBaselineData(
            { baselineTrack, baseline, alignSize, &gridItem }));
        } else {
          state &= ~ItemState::eAllBaselineBits;
        }
      } else {
        if (nsLayoutUtils::GetLastLineBaseline(wm, child, &baseline)) {
          auto frameSize = isInlineAxis ? child->ISize(wm) : child->BSize(wm);
          auto m = child->GetLogicalUsedMargin(wm);
          auto descent = frameSize - baseline + (isInlineAxis ? m.IEnd(wm)
                                                              : m.BEnd(wm));
          auto alignSize = frameSize + (isInlineAxis ? m.IStartEnd(wm)
                                                     : m.BStartEnd(wm));
          lastBaselineItems.AppendElement(ItemBaselineData(
            { baselineTrack, descent, alignSize, &gridItem }));
        } else {
          state &= ~ItemState::eAllBaselineBits;
        }
      }
    }
    MOZ_ASSERT((state &
                (ItemState::eFirstBaseline | ItemState::eLastBaseline)) !=
               (ItemState::eFirstBaseline | ItemState::eLastBaseline),
               "baseline and last-baseline bits are mutually exclusive");
    MOZ_ASSERT((state &
                (ItemState::eSelfBaseline | ItemState::eContentBaseline)) !=
               (ItemState::eSelfBaseline | ItemState::eContentBaseline),
               "*-self and *-content baseline bits are mutually exclusive");
    MOZ_ASSERT(!(state &
                 (ItemState::eFirstBaseline | ItemState::eLastBaseline)) ==
               !(state &
                 (ItemState::eSelfBaseline | ItemState::eContentBaseline)),
               "first/last bit requires self/content bit and vice versa");
    gridItem.mState[mAxis] = state;
    gridItem.mBaselineOffset[mAxis] = nscoord(0);
  }

  if (firstBaselineItems.IsEmpty() && lastBaselineItems.IsEmpty()) {
    return;
  }

  // TODO: CSS Align spec issue - how to align a baseline subtree in a track?
  // https://lists.w3.org/Archives/Public/www-style/2016May/0141.html
  mBaselineSubtreeAlign[BaselineSharingGroup::eFirst] = NS_STYLE_ALIGN_START;
  mBaselineSubtreeAlign[BaselineSharingGroup::eLast] = NS_STYLE_ALIGN_END;

  CalculateItemBaselines(firstBaselineItems, BaselineSharingGroup::eFirst);
  CalculateItemBaselines(lastBaselineItems, BaselineSharingGroup::eLast);
}

void
nsGridContainerFrame::Tracks::AlignBaselineSubtree(
  const GridItemInfo& aGridItem) const
{
  auto state = aGridItem.mState[mAxis];
  if (!(state & ItemState::eIsBaselineAligned)) {
    return;
  }
  const GridArea& area = aGridItem.mArea;
  int32_t baselineTrack;
  const bool isFirstBaseline = state & ItemState::eFirstBaseline;
  if (isFirstBaseline) {
    baselineTrack = mAxis == eLogicalAxisBlock ? area.mRows.mStart
                                               : area.mCols.mStart;
  } else {
    baselineTrack = (mAxis == eLogicalAxisBlock ? area.mRows.mEnd
                                                : area.mCols.mEnd) - 1;
  }
  const TrackSize& sz = mSizes[baselineTrack];
  auto baselineGroup = isFirstBaseline ? BaselineSharingGroup::eFirst
                                       : BaselineSharingGroup::eLast;
  nscoord delta = sz.mBase - sz.mBaselineSubtreeSize[baselineGroup];
  const auto subtreeAlign = mBaselineSubtreeAlign[baselineGroup];
  switch (subtreeAlign) {
    case NS_STYLE_ALIGN_START:
      if (state & ItemState::eLastBaseline) {
        aGridItem.mBaselineOffset[mAxis] += delta;
      }
      break;
    case NS_STYLE_ALIGN_END:
      if (isFirstBaseline) {
        aGridItem.mBaselineOffset[mAxis] += delta;
      }
      break;
    case NS_STYLE_ALIGN_CENTER:
      aGridItem.mBaselineOffset[mAxis] += delta / 2;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unexpected baseline subtree alignment");
  }
}

void
nsGridContainerFrame::Tracks::ResolveIntrinsicSize(
  GridReflowState&            aState,
  nsTArray<GridItemInfo>&     aGridItems,
  const TrackSizingFunctions& aFunctions,
  LineRange GridArea::*       aRange,
  nscoord                     aPercentageBasis,
  IntrinsicISizeType          aConstraint)
{
  // Some data we collect on each item for Step 2 of the algorithm below.
  struct Step2ItemData
  {
    uint32_t mSpan;
    TrackSize::StateBits mState;
    LineRange mLineRange;
    nscoord mMinSize;
    nscoord mMinContentContribution;
    nscoord mMaxContentContribution;
    nsIFrame* mFrame;
    static bool IsSpanLessThan(const Step2ItemData& a, const Step2ItemData& b)
    {
      return a.mSpan < b.mSpan;
    }
  };

  // Resolve Intrinsic Track Sizes
  // http://dev.w3.org/csswg/css-grid/#algo-content
  // We're also setting eIsFlexing on the item state here to speed up
  // FindUsedFlexFraction later.
  AutoTArray<TrackSize::StateBits, 16> stateBitsPerSpan;
  nsTArray<Step2ItemData> step2Items;
  GridItemCSSOrderIterator& iter = aState.mIter;
  nsRenderingContext* rc = &aState.mRenderingContext;
  WritingMode wm = aState.mWM;
  uint32_t maxSpan = 0; // max span of the step2Items items
  const TrackSize::StateBits flexMin =
    aConstraint == nsLayoutUtils::MIN_ISIZE ? TrackSize::eFlexMinSizing
                                            : TrackSize::StateBits(0);
  iter.Reset();
  for (; !iter.AtEnd(); iter.Next()) {
    auto& gridItem = aGridItems[iter.GridItemIndex()];
    const GridArea& area = gridItem.mArea;
    const LineRange& lineRange = area.*aRange;
    uint32_t span = lineRange.Extent();
    if (span == 1) {
      // Step 1. Size tracks to fit non-spanning items.
      if (ResolveIntrinsicSizeStep1(aState, aFunctions, aPercentageBasis,
                                    aConstraint, lineRange, gridItem)) {
        gridItem.mState[mAxis] |= ItemState::eIsFlexing;
      }
    } else {
      TrackSize::StateBits state = TrackSize::StateBits(0);
      if (HasIntrinsicButNoFlexSizingInRange(lineRange, aConstraint, &state)) {
        // Collect data for Step 2.
        maxSpan = std::max(maxSpan, span);
        if (span >= stateBitsPerSpan.Length()) {
          uint32_t len = 2 * span;
          stateBitsPerSpan.SetCapacity(len);
          for (uint32_t i = stateBitsPerSpan.Length(); i < len; ++i) {
            stateBitsPerSpan.AppendElement(TrackSize::StateBits(0));
          }
        }
        stateBitsPerSpan[span] |= state;
        nscoord minSize = 0;
        if (state & (flexMin | TrackSize::eIntrinsicMinSizing)) { // for 2.1
          minSize = MinSize(gridItem, aState.mReflowState, rc, wm, mAxis);
        }
        nscoord minContent = 0;
        if (state & (flexMin | TrackSize::eMinOrMaxContentMinSizing | // for 2.2
                     TrackSize::eIntrinsicMaxSizing)) {               // for 2.5
          minContent = MinContentContribution(gridItem, aState.mReflowState,
                                              rc, wm, mAxis);
        }
        nscoord maxContent = 0;
        if (state & (TrackSize::eMaxContentMinSizing |         // for 2.3
                     TrackSize::eAutoOrMaxContentMaxSizing)) { // for 2.6
          maxContent = MaxContentContribution(gridItem, aState.mReflowState,
                                              rc, wm, mAxis);
        }
        step2Items.AppendElement(
          Step2ItemData({span, state, lineRange, minSize,
                         minContent, maxContent, *iter}));
      } else if (state & TrackSize::eFlexMaxSizing) {
        gridItem.mState[mAxis] |= ItemState::eIsFlexing;
      }
    }
  }

  // Step 2.
  if (maxSpan) {
    // Sort the collected items on span length, shortest first.
    std::stable_sort(step2Items.begin(), step2Items.end(),
                     Step2ItemData::IsSpanLessThan);

    nsTArray<uint32_t> tracks(maxSpan);
    nsTArray<TrackSize> plan(mSizes.Length());
    plan.SetLength(mSizes.Length());
    for (uint32_t i = 0, len = step2Items.Length(); i < len; ) {
      // Start / end index for items of the same span length:
      const uint32_t spanGroupStartIndex = i;
      uint32_t spanGroupEndIndex = len;
      const uint32_t span = step2Items[i].mSpan;
      for (++i; i < len; ++i) {
        if (step2Items[i].mSpan != span) {
          spanGroupEndIndex = i;
          break;
        }
      }

      bool updatedBase = false; // Did we update any mBase in step 2.1 - 2.3?
      TrackSize::StateBits selector(flexMin | TrackSize::eIntrinsicMinSizing);
      if (stateBitsPerSpan[span] & selector) {
        // Step 2.1 MinSize to intrinsic min-sizing.
        for (i = spanGroupStartIndex; i < spanGroupEndIndex; ++i) {
          Step2ItemData& item = step2Items[i];
          if (!(item.mState & selector)) {
            continue;
          }
          nscoord space = item.mMinSize;
          if (space <= 0) {
            continue;
          }
          tracks.ClearAndRetainStorage();
          space = CollectGrowable(space, mSizes, item.mLineRange, selector,
                                  tracks);
          if (space > 0) {
            DistributeToTrackBases(space, plan, tracks, selector);
            updatedBase = true;
          }
        }
      }

      selector = flexMin | TrackSize::eMinOrMaxContentMinSizing;
      if (stateBitsPerSpan[span] & selector) {
        // Step 2.2 MinContentContribution to min-/max-content min-sizing.
        for (i = spanGroupStartIndex; i < spanGroupEndIndex; ++i) {
          Step2ItemData& item = step2Items[i];
          if (!(item.mState & selector)) {
            continue;
          }
          nscoord space = item.mMinContentContribution;
          if (space <= 0) {
            continue;
          }
          tracks.ClearAndRetainStorage();
          space = CollectGrowable(space, mSizes, item.mLineRange, selector,
                                  tracks);
          if (space > 0) {
            DistributeToTrackBases(space, plan, tracks, selector);
            updatedBase = true;
          }
        }
      }

      if (stateBitsPerSpan[span] & TrackSize::eMaxContentMinSizing) {
        // Step 2.3 MaxContentContribution to max-content min-sizing.
        for (i = spanGroupStartIndex; i < spanGroupEndIndex; ++i) {
          Step2ItemData& item = step2Items[i];
          if (!(item.mState & TrackSize::eMaxContentMinSizing)) {
            continue;
          }
          nscoord space = item.mMaxContentContribution;
          if (space <= 0) {
            continue;
          }
          tracks.ClearAndRetainStorage();
          space = CollectGrowable(space, mSizes, item.mLineRange,
                                  TrackSize::eMaxContentMinSizing,
                                  tracks);
          if (space > 0) {
            DistributeToTrackBases(space, plan, tracks,
                                   TrackSize::eMaxContentMinSizing);
            updatedBase = true;
          }
        }
      }

      if (updatedBase) {
        // Step 2.4
        for (TrackSize& sz : mSizes) {
          if (sz.mBase > sz.mLimit) {
            sz.mLimit = sz.mBase;
          }
        }
      }
      if (stateBitsPerSpan[span] & TrackSize::eIntrinsicMaxSizing) {
        plan = mSizes;
        for (TrackSize& sz : plan) {
          if (sz.mLimit == NS_UNCONSTRAINEDSIZE) {
            // use mBase as the planned limit
          } else {
            sz.mBase = sz.mLimit;
          }
        }

        // Step 2.5 MinContentContribution to intrinsic max-sizing.
        for (i = spanGroupStartIndex; i < spanGroupEndIndex; ++i) {
          Step2ItemData& item = step2Items[i];
          if (!(item.mState & TrackSize::eIntrinsicMaxSizing)) {
            continue;
          }
          nscoord space = item.mMinContentContribution;
          if (space <= 0) {
            continue;
          }
          tracks.ClearAndRetainStorage();
          space = CollectGrowable(space, plan, item.mLineRange,
                                  TrackSize::eIntrinsicMaxSizing,
                                  tracks);
          if (space > 0) {
            DistributeToTrackLimits(space, plan, tracks);
          }
        }
        for (size_t j = 0, len = mSizes.Length(); j < len; ++j) {
          TrackSize& sz = plan[j];
          sz.mState &= ~(TrackSize::eFrozen | TrackSize::eSkipGrowUnlimited);
          if (sz.mLimit != NS_UNCONSTRAINEDSIZE) {
            sz.mLimit = sz.mBase;  // collect the results from 2.5
          }
        }

        if (stateBitsPerSpan[span] & TrackSize::eAutoOrMaxContentMaxSizing) {
          // Step 2.6 MaxContentContribution to max-content max-sizing.
          for (i = spanGroupStartIndex; i < spanGroupEndIndex; ++i) {
            Step2ItemData& item = step2Items[i];
            if (!(item.mState & TrackSize::eAutoOrMaxContentMaxSizing)) {
              continue;
            }
            nscoord space = item.mMaxContentContribution;
            if (space <= 0) {
              continue;
            }
            tracks.ClearAndRetainStorage();
            space = CollectGrowable(space, plan, item.mLineRange,
                                    TrackSize::eAutoOrMaxContentMaxSizing,
                                    tracks);
            if (space > 0) {
              DistributeToTrackLimits(space, plan, tracks);
            }
          }
        }
      }
    }
  }

  // Step 3.
  for (TrackSize& sz : mSizes) {
    if (sz.mLimit == NS_UNCONSTRAINEDSIZE) {
      sz.mLimit = sz.mBase;
    }
  }
}

float
nsGridContainerFrame::Tracks::FindFrUnitSize(
  const LineRange&            aRange,
  const nsTArray<uint32_t>&   aFlexTracks,
  const TrackSizingFunctions& aFunctions,
  nscoord                     aSpaceToFill) const
{
  MOZ_ASSERT(aSpaceToFill > 0 && !aFlexTracks.IsEmpty());
  float flexFactorSum = 0.0f;
  nscoord leftOverSpace = aSpaceToFill;
  for (uint32_t i = aRange.mStart, end = aRange.mEnd; i < end; ++i) {
    const TrackSize& sz = mSizes[i];
    if (sz.mState & TrackSize::eFlexMaxSizing) {
      flexFactorSum += aFunctions.MaxSizingFor(i).GetFlexFractionValue();
    } else {
      leftOverSpace -= sz.mBase;
      if (leftOverSpace <= 0) {
        return 0.0f;
      }
    }
  }
  bool restart;
  float hypotheticalFrSize;
  nsTArray<uint32_t> flexTracks(aFlexTracks);
  uint32_t numFlexTracks = flexTracks.Length();
  do {
    restart = false;
    hypotheticalFrSize = leftOverSpace / std::max(flexFactorSum, 1.0f);
    for (uint32_t i = 0, len = flexTracks.Length(); i < len; ++i) {
      uint32_t track = flexTracks[i];
      if (track == kAutoLine) {
        continue; // Track marked as inflexible in a prev. iter of this loop.
      }
      float flexFactor = aFunctions.MaxSizingFor(track).GetFlexFractionValue();
      const nscoord base = mSizes[track].mBase;
      if (flexFactor * hypotheticalFrSize < base) {
        // 12.7.1.4: Treat this track as inflexible.
        flexTracks[i] = kAutoLine;
        flexFactorSum -= flexFactor;
        leftOverSpace -= base;
        --numFlexTracks;
        if (numFlexTracks == 0 || leftOverSpace <= 0) {
          return 0.0f;
        }
        restart = true;
        // break; XXX (bug 1176621 comment 16) measure which is more common
      }
    }
  } while (restart);
  return hypotheticalFrSize;
}

float
nsGridContainerFrame::Tracks::FindUsedFlexFraction(
  GridReflowState&            aState,
  nsTArray<GridItemInfo>&     aGridItems,
  const nsTArray<uint32_t>&   aFlexTracks,
  const TrackSizingFunctions& aFunctions,
  nscoord                     aAvailableSize) const
{
  if (aAvailableSize != NS_UNCONSTRAINEDSIZE) {
    // Use all of the grid tracks and a 'space to fill' of the available space.
    const TranslatedLineRange range(0, mSizes.Length());
    return FindFrUnitSize(range, aFlexTracks, aFunctions, aAvailableSize);
  }

  // The used flex fraction is the maximum of:
  // ... each flexible track's base size divided by its flex factor
  float fr = 0.0f;
  for (uint32_t track : aFlexTracks) {
    float flexFactor = aFunctions.MaxSizingFor(track).GetFlexFractionValue();
    if (flexFactor > 0.0f) {
      fr = std::max(fr, mSizes[track].mBase / flexFactor);
    }
  }
  WritingMode wm = aState.mWM;
  nsRenderingContext* rc = &aState.mRenderingContext;
  const nsHTMLReflowState* rs = aState.mReflowState;
  GridItemCSSOrderIterator& iter = aState.mIter;
  iter.Reset();
  // ... the result of 'finding the size of an fr' for each item that spans
  // a flex track with its max-content contribution as 'space to fill'
  for (; !iter.AtEnd(); iter.Next()) {
    const GridItemInfo& item = aGridItems[iter.GridItemIndex()];
    if (item.mState[mAxis] & ItemState::eIsFlexing) {
      nscoord spaceToFill = MaxContentContribution(item, rs, rc, wm, mAxis);
      if (spaceToFill <= 0) {
        continue;
      }
      // ... and all its spanned tracks as input.
      const LineRange& range =
        mAxis == eLogicalAxisInline ? item.mArea.mCols : item.mArea.mRows;
      nsTArray<uint32_t> itemFlexTracks;
      for (uint32_t i = range.mStart, end = range.mEnd; i < end; ++i) {
        if (mSizes[i].mState & TrackSize::eFlexMaxSizing) {
          itemFlexTracks.AppendElement(i);
        }
      }
      float itemFr =
        FindFrUnitSize(range, itemFlexTracks, aFunctions, spaceToFill);
      fr = std::max(fr, itemFr);
    }
  }
  return fr;
}

void
nsGridContainerFrame::Tracks::StretchFlexibleTracks(
  GridReflowState&            aState,
  nsTArray<GridItemInfo>&     aGridItems,
  const TrackSizingFunctions& aFunctions,
  nscoord                     aAvailableSize)
{
  if (aAvailableSize <= 0) {
    return;
  }
  nsTArray<uint32_t> flexTracks(mSizes.Length());
  for (uint32_t i = 0, len = mSizes.Length(); i < len; ++i) {
    if (mSizes[i].mState & TrackSize::eFlexMaxSizing) {
      flexTracks.AppendElement(i);
    }
  }
  if (flexTracks.IsEmpty()) {
    return;
  }
  float fr = FindUsedFlexFraction(aState, aGridItems, flexTracks,
                                  aFunctions, aAvailableSize);
  if (fr != 0.0f) {
    for (uint32_t i : flexTracks) {
      float flexFactor = aFunctions.MaxSizingFor(i).GetFlexFractionValue();
      nscoord flexLength = NSToCoordRound(flexFactor * fr);
      nscoord& base = mSizes[i].mBase;
      if (flexLength > base) {
        base = flexLength;
      }
    }
  }
}

void
nsGridContainerFrame::Tracks::AlignJustifyContent(
  const nsHTMLReflowState& aReflowState,
  const LogicalSize&     aContainerSize)
{
  if (mSizes.IsEmpty()) {
    return;
  }

  const bool isAlign = mAxis == eLogicalAxisBlock;
  auto stylePos = aReflowState.mStylePosition;
  auto valueAndFallback = isAlign ? stylePos->ComputedAlignContent() :
                                    stylePos->ComputedJustifyContent();
  WritingMode wm = aReflowState.GetWritingMode();
  bool overflowSafe;
  auto alignment = ::GetAlignJustifyValue(valueAndFallback, wm, isAlign,
                                          &overflowSafe);
  if (alignment == NS_STYLE_ALIGN_NORMAL) {
    MOZ_ASSERT(valueAndFallback == NS_STYLE_ALIGN_NORMAL,
               "*-content:normal cannot be specified with explicit fallback");
    alignment = NS_STYLE_ALIGN_STRETCH;
    valueAndFallback = alignment; // we may need a fallback for 'stretch' below
  }

  // Compute the free space and count auto-sized tracks.
  size_t numAutoTracks = 0;
  nscoord space;
  if (alignment != NS_STYLE_ALIGN_START) {
    nscoord trackSizeSum = 0;
    for (const TrackSize& sz : mSizes) {
      trackSizeSum += sz.mBase;
      if (sz.mState & TrackSize::eAutoMaxSizing) {
        ++numAutoTracks;
      }
    }
    nscoord cbSize = isAlign ? aContainerSize.BSize(wm)
                             : aContainerSize.ISize(wm);
    space = cbSize - trackSizeSum - SumOfGridGaps();
    // Use the fallback value instead when applicable.
    if (space < 0 ||
        (alignment == NS_STYLE_ALIGN_SPACE_BETWEEN && mSizes.Length() == 1)) {
      auto fallback = ::GetAlignJustifyFallbackIfAny(valueAndFallback, wm,
                                                     isAlign, &overflowSafe);
      if (fallback) {
        alignment = fallback;
      }
    }
    if (space == 0 || (space < 0 && overflowSafe)) {
      // XXX check that this makes sense also for [last-]baseline (bug 1151204).
      alignment = NS_STYLE_ALIGN_START;
    }
  }

  // Optimize the cases where we just need to set each track's position.
  nscoord pos = 0;
  bool distribute = true;
  switch (alignment) {
    case NS_STYLE_ALIGN_BASELINE:
    case NS_STYLE_ALIGN_LAST_BASELINE:
      NS_WARNING("'NYI: baseline/last-baseline' (bug 1151204)"); // XXX
      MOZ_FALLTHROUGH;
    case NS_STYLE_ALIGN_START:
      distribute = false;
      break;
    case NS_STYLE_ALIGN_END:
      pos = space;
      distribute = false;
      break;
    case NS_STYLE_ALIGN_CENTER:
      pos = space / 2;
      distribute = false;
      break;
    case NS_STYLE_ALIGN_STRETCH:
      distribute = numAutoTracks != 0;
      break;
  }
  if (!distribute) {
    for (TrackSize& sz : mSizes) {
      sz.mPosition = pos;
      pos += sz.mBase + mGridGap;
    }
    return;
  }

  // Distribute free space to/between tracks and set their position.
  MOZ_ASSERT(space > 0, "should've handled that on the fallback path above");
  nscoord between, roundingError;
  switch (alignment) {
    case NS_STYLE_ALIGN_STRETCH: {
      MOZ_ASSERT(numAutoTracks > 0, "we handled numAutoTracks == 0 above");
      nscoord spacePerTrack;
      roundingError = NSCoordDivRem(space, numAutoTracks, &spacePerTrack);
      for (TrackSize& sz : mSizes) {
        sz.mPosition = pos;
        if (!(sz.mState & TrackSize::eAutoMaxSizing)) {
          pos += sz.mBase + mGridGap;
          continue;
        }
        nscoord stretch = spacePerTrack;
        if (roundingError) {
          roundingError -= 1;
          stretch += 1;
        }
        nscoord newBase = sz.mBase + stretch;
        sz.mBase = newBase;
        pos += newBase + mGridGap;
      }
      MOZ_ASSERT(!roundingError, "we didn't distribute all rounding error?");
      return;
    }
    case NS_STYLE_ALIGN_SPACE_BETWEEN:
      MOZ_ASSERT(mSizes.Length() > 1, "should've used a fallback above");
      roundingError = NSCoordDivRem(space, mSizes.Length() - 1, &between);
      break;
    case NS_STYLE_ALIGN_SPACE_AROUND:
      roundingError = NSCoordDivRem(space, mSizes.Length(), &between);
      pos = between / 2;
      break;
    case NS_STYLE_ALIGN_SPACE_EVENLY:
      roundingError = NSCoordDivRem(space, mSizes.Length() + 1, &between);
      pos = between;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unknown align-/justify-content value");
      between = 0; // just to avoid a compiler warning
  }
  between += mGridGap;
  for (TrackSize& sz : mSizes) {
    sz.mPosition = pos;
    nscoord spacing = between;
    if (roundingError) {
      roundingError -= 1;
      spacing += 1;
    }
    pos += sz.mBase + spacing;
  }
  MOZ_ASSERT(!roundingError, "we didn't distribute all rounding error?");
}

void
nsGridContainerFrame::LineRange::ToPositionAndLength(
  const nsTArray<TrackSize>& aTrackSizes, nscoord* aPos, nscoord* aLength) const
{
  MOZ_ASSERT(mStart != kAutoLine && mEnd != kAutoLine,
             "expected a definite LineRange");
  MOZ_ASSERT(mStart < mEnd);
  nscoord startPos = aTrackSizes[mStart].mPosition;
  const TrackSize& sz = aTrackSizes[mEnd - 1];
  *aPos = startPos;
  *aLength = (sz.mPosition + sz.mBase) - startPos;
}

nscoord
nsGridContainerFrame::LineRange::ToLength(
  const nsTArray<TrackSize>& aTrackSizes) const
{
  MOZ_ASSERT(mStart != kAutoLine && mEnd != kAutoLine,
             "expected a definite LineRange");
  MOZ_ASSERT(mStart < mEnd);
  nscoord startPos = aTrackSizes[mStart].mPosition;
  const TrackSize& sz = aTrackSizes[mEnd - 1];
  return (sz.mPosition + sz.mBase) - startPos;
}

void
nsGridContainerFrame::LineRange::ToPositionAndLengthForAbsPos(
  const Tracks& aTracks, nscoord aGridOrigin,
  nscoord* aPos, nscoord* aLength) const
{
  // kAutoLine for abspos children contributes the corresponding edge
  // of the grid container's padding-box.
  if (mEnd == kAutoLine) {
    if (mStart == kAutoLine) {
      // done
    } else {
      const nscoord endPos = *aPos + *aLength;
      auto side = mStart == aTracks.mSizes.Length() ? GridLineSide::eBeforeGridGap
                                                    : GridLineSide::eAfterGridGap;
      nscoord startPos = aTracks.GridLineEdge(mStart, side);
      *aPos = aGridOrigin + startPos;
      *aLength = std::max(endPos - *aPos, 0);
    }
  } else {
    if (mStart == kAutoLine) {
      auto side = mEnd == 0 ? GridLineSide::eAfterGridGap
                            : GridLineSide::eBeforeGridGap;
      nscoord endPos = aTracks.GridLineEdge(mEnd, side);
      *aLength = std::max(aGridOrigin + endPos, 0);
    } else {
      nscoord pos;
      ToPositionAndLength(aTracks.mSizes, &pos, aLength);
      *aPos = aGridOrigin + pos;
    }
  }
}

LogicalRect
nsGridContainerFrame::GridReflowState::ContainingBlockFor(const GridArea& aArea) const
{
  nscoord i, b, iSize, bSize;
  MOZ_ASSERT(aArea.mCols.Extent() > 0, "grid items cover at least one track");
  MOZ_ASSERT(aArea.mRows.Extent() > 0, "grid items cover at least one track");
  aArea.mCols.ToPositionAndLength(mCols.mSizes, &i, &iSize);
  aArea.mRows.ToPositionAndLength(mRows.mSizes, &b, &bSize);
  return LogicalRect(mWM, i, b, iSize, bSize);
}

LogicalRect
nsGridContainerFrame::GridReflowState::ContainingBlockForAbsPos(
  const GridArea&     aArea,
  const LogicalPoint& aGridOrigin,
  const LogicalRect&  aGridCB) const
{
  nscoord i = aGridCB.IStart(mWM);
  nscoord b = aGridCB.BStart(mWM);
  nscoord iSize = aGridCB.ISize(mWM);
  nscoord bSize = aGridCB.BSize(mWM);
  aArea.mCols.ToPositionAndLengthForAbsPos(mCols, aGridOrigin.I(mWM),
                                           &i, &iSize);
  aArea.mRows.ToPositionAndLengthForAbsPos(mRows, aGridOrigin.B(mWM),
                                           &b, &bSize);
  return LogicalRect(mWM, i, b, iSize, bSize);
}

/**
 * Return a Fragmentainer object if we have a fragmentainer frame in our
 * ancestor chain of containing block (CB) reflow states.  We'll only
 * continue traversing the ancestor chain as long as the CBs have
 * the same writing-mode and have overflow:visible.
 */
Maybe<nsGridContainerFrame::Fragmentainer>
nsGridContainerFrame::GetNearestFragmentainer(const GridReflowState& aState) const
{
  Maybe<nsGridContainerFrame::Fragmentainer> data;
  WritingMode wm = aState.mWM;
  const nsHTMLReflowState* gridRS = aState.mReflowState;
  const nsHTMLReflowState* cbRS = gridRS->mCBReflowState;
  for ( ; cbRS; cbRS = cbRS->mCBReflowState) {
    nsIScrollableFrame* sf = do_QueryFrame(cbRS->frame);
    if (sf) {
      break;
    }
    if (wm.IsOrthogonalTo(cbRS->GetWritingMode())) {
      break;
    }
    nsIAtom* frameType = cbRS->frame->GetType();
    if ((frameType == nsGkAtoms::canvasFrame &&
         PresContext()->IsPaginated()) ||
        frameType == nsGkAtoms::columnSetFrame) {
      data.emplace();
      data->mIsTopOfPage = gridRS->mFlags.mIsTopOfPage;
      data->mToFragmentainerEnd = aState.mFragBStart +
        gridRS->AvailableBSize() - aState.mBorderPadding.BStart(wm);
      const auto numRows = aState.mRows.mSizes.Length();
      data->mCanBreakAtStart =
        numRows > 0 && aState.mRows.mSizes[0].mPosition > 0;
      nscoord bSize = gridRS->ComputedBSize();
      data->mIsAutoBSize = bSize == NS_AUTOHEIGHT;
      if (data->mIsAutoBSize) {
        bSize = gridRS->ComputedMinBSize();
      } else {
        bSize = NS_CSS_MINMAX(bSize,
                              gridRS->ComputedMinBSize(),
                              gridRS->ComputedMaxBSize());
      }
      nscoord gridEnd =
        aState.mRows.GridLineEdge(numRows, GridLineSide::eBeforeGridGap);
      data->mCanBreakAtEnd = bSize > gridEnd &&
                             bSize > aState.mFragBStart;
      break;
    }
  }
  return data;
}

void
nsGridContainerFrame::ReflowInFlowChild(nsIFrame*              aChild,
                                        const GridItemInfo*    aGridItemInfo,
                                        nsSize                 aContainerSize,
                                        Maybe<nscoord>         aStretchBSize,
                                        const Fragmentainer*   aFragmentainer,
                                        const GridReflowState& aState,
                                        const LogicalRect&     aContentArea,
                                        nsHTMLReflowMetrics&   aDesiredSize,
                                        nsReflowStatus&        aStatus)
{
  nsPresContext* pc = PresContext();
  nsStyleContext* containerSC = StyleContext();
  WritingMode wm = aState.mReflowState->GetWritingMode();
  LogicalMargin pad(aState.mReflowState->ComputedLogicalPadding());
  const LogicalPoint padStart(wm, pad.IStart(wm), pad.BStart(wm));
  const bool isGridItem = !!aGridItemInfo;
  MOZ_ASSERT(isGridItem == (aChild->GetType() != nsGkAtoms::placeholderFrame));
  LogicalRect cb(wm);
  WritingMode childWM = aChild->GetWritingMode();
  bool isConstrainedBSize = false;
  nscoord toFragmentainerEnd;
  const bool isOrthogonal = wm.IsOrthogonalTo(childWM);
  if (MOZ_LIKELY(isGridItem)) {
    MOZ_ASSERT(aGridItemInfo->mFrame == aChild);
    const GridArea& area = aGridItemInfo->mArea;
    MOZ_ASSERT(area.IsDefinite());
    cb = aState.ContainingBlockFor(area);
    isConstrainedBSize = aFragmentainer && !wm.IsOrthogonalTo(childWM);
    if (isConstrainedBSize) {
      cb.BStart(wm) = std::max(0, cb.BStart(wm) - aState.mFragBStart);
      toFragmentainerEnd = aFragmentainer->mToFragmentainerEnd -
        aState.mFragBStart - cb.BStart(wm);
      toFragmentainerEnd = std::max(toFragmentainerEnd, 0);
    }
    cb += aContentArea.Origin(wm);
    aState.mRows.AlignBaselineSubtree(*aGridItemInfo);
    aState.mCols.AlignBaselineSubtree(*aGridItemInfo);
    // Setup [align|justify]-content:[last-]baseline related frame properties.
    // These are added to the padding in nsCSSOffsetState::InitOffsets.
    // (a negative value signals the value is for 'last-baseline' and should be
    //  added to the (logical) end padding)
    typedef const FramePropertyDescriptor<SmallValueHolder<nscoord>>* Prop;
    auto SetProp = [aGridItemInfo, aChild] (LogicalAxis aGridAxis,
                                            Prop aProp) {
      auto state = aGridItemInfo->mState[aGridAxis];
      auto baselineAdjust = (state & ItemState::eContentBaseline) ?
             aGridItemInfo->mBaselineOffset[aGridAxis] : nscoord(0);
      if (baselineAdjust < nscoord(0)) {
        // This happens when the subtree overflows its track.
        // XXX spec issue? it's unclear how to handle this.
        baselineAdjust = nscoord(0);
      } else if (baselineAdjust > nscoord(0) &&
                 (state & ItemState::eLastBaseline)) {
        baselineAdjust = -baselineAdjust;
      }
      if (baselineAdjust != nscoord(0)) {
        aChild->Properties().Set(aProp, baselineAdjust);
      } else {
        aChild->Properties().Delete(aProp);
      }
    };
    SetProp(eLogicalAxisBlock, isOrthogonal ? IBaselinePadProperty() :
                                              BBaselinePadProperty());
    SetProp(eLogicalAxisInline, isOrthogonal ? BBaselinePadProperty() :
                                               IBaselinePadProperty());
  } else {
    cb = aContentArea;
  }

  LogicalSize reflowSize(cb.Size(wm));
  if (isConstrainedBSize) {
    reflowSize.BSize(wm) = toFragmentainerEnd;
  }
  LogicalSize childCBSize = reflowSize.ConvertTo(childWM, wm);
  if (!isConstrainedBSize) {
    childCBSize.BSize(childWM) = NS_UNCONSTRAINEDSIZE;
  }

  LogicalSize percentBasis(cb.Size(wm).ConvertTo(childWM, wm));
  nsHTMLReflowState childRS(pc, *aState.mReflowState, aChild, childCBSize,
                            &percentBasis);
  childRS.mFlags.mIsTopOfPage = aFragmentainer ? aFragmentainer->mIsTopOfPage : false;

  // If the child is stretching in its block axis, and we might be fragmenting
  // it in that axis, then setup a frame property to tell
  // nsBlockFrame::ComputeFinalSize the size.
  if (isConstrainedBSize && !wm.IsOrthogonalTo(childWM)) {
    bool stretch = false;
    if (!childRS.mStyleMargin->HasBlockAxisAuto(childWM) &&
        childRS.mStylePosition->BSize(childWM).GetUnit() == eStyleUnit_Auto) {
      auto blockAxisAlignment =
        childRS.mStylePosition->ComputedAlignSelf(StyleContext());
      if (blockAxisAlignment == NS_STYLE_ALIGN_NORMAL ||
          blockAxisAlignment == NS_STYLE_ALIGN_STRETCH) {
        stretch = true;
      }
    }
    if (stretch) {
      aChild->Properties().Set(FragStretchBSizeProperty(), *aStretchBSize);
    } else {
      aChild->Properties().Delete(FragStretchBSizeProperty());
    }
  }

  // We need the width of the child before we can correctly convert
  // the writing-mode of its origin, so we reflow at (0, 0) using a dummy
  // aContainerSize, and then pass the correct position to FinishReflowChild.
  nsHTMLReflowMetrics childSize(childRS);
  const nsSize dummyContainerSize;
  ReflowChild(aChild, pc, childSize, childRS, childWM, LogicalPoint(childWM),
              dummyContainerSize, 0, aStatus);
  LogicalPoint childPos =
    cb.Origin(wm).ConvertTo(childWM, wm,
                            aContainerSize - childSize.PhysicalSize());
  // Apply align/justify-self and reflow again if that affects the size.
  if (MOZ_LIKELY(isGridItem)) {
    LogicalSize size = childSize.Size(childWM); // from the ReflowChild()
    if (NS_FRAME_IS_COMPLETE(aStatus)) {
      auto align = childRS.mStylePosition->ComputedAlignSelf(containerSC);
      auto state = aGridItemInfo->mState[eLogicalAxisBlock];
      if (state & ItemState::eContentBaseline) {
        align = (state & ItemState::eFirstBaseline) ? NS_STYLE_ALIGN_SELF_START
                                                    : NS_STYLE_ALIGN_SELF_END;
      }
      AlignSelf(*aGridItemInfo, align, cb, wm, childRS, size, &childPos);
    }
    auto justify = childRS.mStylePosition->ComputedJustifySelf(containerSC);
    auto state = aGridItemInfo->mState[eLogicalAxisInline];
    if (state & ItemState::eContentBaseline) {
      justify = (state & ItemState::eFirstBaseline) ? NS_STYLE_JUSTIFY_SELF_START
                                                    : NS_STYLE_JUSTIFY_SELF_END;
    }
    JustifySelf(*aGridItemInfo, justify, cb, wm, childRS, size, &childPos);
  } else {
    // Put a placeholder at the padding edge, in case an ancestor is its CB.
    childPos -= padStart;
  }
  childRS.ApplyRelativePositioning(&childPos, aContainerSize);
  FinishReflowChild(aChild, pc, childSize, &childRS, childWM, childPos,
                    aContainerSize, 0);
  ConsiderChildOverflow(aDesiredSize.mOverflowAreas, aChild);
}

nscoord
nsGridContainerFrame::ReflowInFragmentainer(GridReflowState&     aState,
                                            const LogicalRect&   aContentArea,
                                            nsHTMLReflowMetrics& aDesiredSize,
                                            nsReflowStatus&      aStatus,
                                            Fragmentainer&       aFragmentainer,
                                            const nsSize&        aContainerSize)
{
  MOZ_ASSERT(aStatus == NS_FRAME_COMPLETE);
  MOZ_ASSERT(aState.mReflowState);

  // Collect our grid items and sort them in row order.  Collect placeholders
  // and put them in a separate array.
  nsTArray<const GridItemInfo*> sortedItems(aState.mGridItems.Length());
  nsTArray<nsIFrame*> placeholders(aState.mAbsPosItems.Length());
  aState.mIter.Reset(GridItemCSSOrderIterator::eIncludeAll);
  for (; !aState.mIter.AtEnd(); aState.mIter.Next()) {
    nsIFrame* child = *aState.mIter;
    if (child->GetType() != nsGkAtoms::placeholderFrame) {
      const GridItemInfo* info = &aState.mGridItems[aState.mIter.GridItemIndex()];
      sortedItems.AppendElement(info);
    } else {
      placeholders.AppendElement(child);
    }
  }
  // NOTE: no need to use stable_sort here, there are no dependencies on
  // having content order between items on the same row in the code below.
  std::sort(sortedItems.begin(), sortedItems.end(),
            GridItemInfo::IsStartRowLessThan);

  // Reflow our placeholder children; they must all be complete.
  for (auto child : placeholders) {
    nsReflowStatus childStatus;
    ReflowInFlowChild(child, nullptr, aContainerSize, Nothing(), &aFragmentainer,
                      aState, aContentArea, aDesiredSize, childStatus);
    MOZ_ASSERT(NS_FRAME_IS_COMPLETE(childStatus),
               "nsPlaceholderFrame should never need to be fragmented");
  }

  // The available size for children - we'll set this to the edge of the last
  // row in most cases below, but for now use the full size.
  nscoord childAvailableSize = aFragmentainer.mToFragmentainerEnd;
  const uint32_t startRow = aState.mStartRow;
  const uint32_t numRows = aState.mRows.mSizes.Length();
  bool isBDBClone = aState.mReflowState->mStyleBorder->mBoxDecorationBreak ==
                      NS_STYLE_BOX_DECORATION_BREAK_CLONE;
  nscoord bpBEnd = aState.mBorderPadding.BEnd(aState.mWM);

  // Set |endRow| to the first row that doesn't fit.
  uint32_t endRow = numRows;
  for (uint32_t row = startRow; row < numRows; ++row) {
    auto& sz = aState.mRows.mSizes[row];
    const nscoord bEnd = sz.mPosition + sz.mBase;
    nscoord remainingAvailableSize = childAvailableSize - bEnd;
    if (remainingAvailableSize < 0 ||
        (isBDBClone && remainingAvailableSize < bpBEnd)) {
      endRow = row;
      break;
    }
  }

  // Check for forced breaks on the items.
  const bool isTopOfPage = aFragmentainer.mIsTopOfPage;
  bool isForcedBreak = false;
  const bool avoidBreakInside = ShouldAvoidBreakInside(*aState.mReflowState);
  for (const GridItemInfo* info : sortedItems) {
    uint32_t itemStartRow = info->mArea.mRows.mStart;
    if (itemStartRow == endRow) {
      break;
    }
    auto disp = info->mFrame->StyleDisplay();
    if (disp->mBreakBefore) {
      // Propagate break-before on the first row to the container unless we're
      // already at top-of-page.
      if ((itemStartRow == 0 && !isTopOfPage) || avoidBreakInside) {
        aStatus = NS_INLINE_LINE_BREAK_BEFORE();
        return aState.mFragBStart;
      }
      if ((itemStartRow > startRow ||
           (itemStartRow == startRow && !isTopOfPage)) &&
          itemStartRow < endRow) {
        endRow = itemStartRow;
        isForcedBreak = true;
        // reset any BREAK_AFTER we found on an earlier item
        aStatus = NS_FRAME_COMPLETE;
        break;  // we're done since the items are sorted in row order
      }
    }
    uint32_t itemEndRow = info->mArea.mRows.mEnd;
    if (disp->mBreakAfter) {
      if (itemEndRow != numRows) {
        if (itemEndRow > startRow && itemEndRow < endRow) {
          endRow = itemEndRow;
          isForcedBreak = true;
          // No "break;" here since later items with break-after may have
          // a shorter span.
        }
      } else {
        // Propagate break-after on the last row to the container, we may still
        // find a break-before on this row though (and reset aStatus).
        aStatus = NS_INLINE_LINE_BREAK_AFTER(aStatus); // tentative
      }
    }
  }

  // Consume at least one row in each fragment until we have consumed them all.
  // Except for the first row if there's a break opportunity before it.
  if (startRow == endRow && startRow != numRows &&
      (startRow != 0 || !aFragmentainer.mCanBreakAtStart)) {
    ++endRow;
  }

  // Honor break-inside:avoid if we can't fit all rows.
  if (avoidBreakInside && endRow < numRows) {
    aStatus = NS_INLINE_LINE_BREAK_BEFORE();
    return aState.mFragBStart;
  }

  // Calculate the block-size including this fragment.
  nscoord bEndRow =
    aState.mRows.GridLineEdge(endRow, GridLineSide::eBeforeGridGap);
  nscoord bSize;
  if (aFragmentainer.mIsAutoBSize) {
    // We only apply min-bsize once all rows are complete (when bsize is auto).
    if (endRow < numRows) {
      bSize = bEndRow;
      auto clampedBSize = ClampToCSSMaxBSize(bSize, aState.mReflowState);
      if (MOZ_UNLIKELY(clampedBSize != bSize)) {
        // We apply max-bsize in all fragments though.
        bSize = clampedBSize;
      } else if (!isBDBClone) {
        // The max-bsize won't make this fragment COMPLETE, so the block-end
        // border will be in a later fragment.
        bpBEnd = 0;
      }
    } else {
      bSize = NS_CSS_MINMAX(bEndRow,
                            aState.mReflowState->ComputedMinBSize(),
                            aState.mReflowState->ComputedMaxBSize());
    }
  } else {
    bSize = NS_CSS_MINMAX(aState.mReflowState->ComputedBSize(),
                          aState.mReflowState->ComputedMinBSize(),
                          aState.mReflowState->ComputedMaxBSize());
  }

  // Check for overflow and set aStatus INCOMPLETE if so.
  bool overflow = bSize + bpBEnd > childAvailableSize;
  if (overflow) {
    if (avoidBreakInside) {
      aStatus = NS_INLINE_LINE_BREAK_BEFORE();
      return aState.mFragBStart;
    }
    bool breakAfterLastRow = endRow == numRows && aFragmentainer.mCanBreakAtEnd;
    if (breakAfterLastRow) {
      MOZ_ASSERT(bEndRow < bSize, "bogus aFragmentainer.mCanBreakAtEnd");
      nscoord availableSize = childAvailableSize;
      if (isBDBClone) {
        availableSize -= bpBEnd;
      }
      // Pretend we have at least 1px available size, otherwise we'll never make
      // progress in consuming our bSize.
      availableSize = std::max(availableSize,
                               aState.mFragBStart + AppUnitsPerCSSPixel());
      // Fill the fragmentainer, but not more than our desired block-size and
      // at least to the size of the last row (even if that overflows).
      nscoord newBSize = std::min(bSize, availableSize);
      newBSize = std::max(newBSize, bEndRow);
      // If it's just the border+padding that is overflowing and we have
      // box-decoration-break:clone then we are technically COMPLETE.  There's
      // no point in creating another zero-bsize fragment in this case.
      if (newBSize < bSize || !isBDBClone) {
        NS_FRAME_SET_INCOMPLETE(aStatus);
      }
      bSize = newBSize;
    } else if (bSize <= bEndRow && startRow + 1 < endRow) {
      if (endRow == numRows) {
        // We have more than one row in this fragment, so we can break before
        // the last row instead.
        --endRow;
        bEndRow = aState.mRows.GridLineEdge(endRow, GridLineSide::eBeforeGridGap);
        bSize = bEndRow;
        if (aFragmentainer.mIsAutoBSize) {
          bSize = ClampToCSSMaxBSize(bSize, aState.mReflowState);
        }
      }
      NS_FRAME_SET_INCOMPLETE(aStatus);
    } else if (endRow < numRows) {
      bSize = ClampToCSSMaxBSize(bEndRow, aState.mReflowState, &aStatus);
    } // else - no break opportunities.
  } else {
    // Even though our block-size fits we need to honor forced breaks, or if
    // a row doesn't fit in an auto-sized container (unless it's constrained
    // by a max-bsize which make us overflow-incomplete).
    if (endRow < numRows && (isForcedBreak ||
                             (aFragmentainer.mIsAutoBSize && bEndRow == bSize))) {
      bSize = ClampToCSSMaxBSize(bEndRow, aState.mReflowState, &aStatus);
    }
  }

  // If we can't fit all rows then we're at least overflow-incomplete.
  if (endRow < numRows) {
    childAvailableSize = bEndRow;
    if (NS_FRAME_IS_COMPLETE(aStatus)) {
      NS_FRAME_SET_OVERFLOW_INCOMPLETE(aStatus);
      aStatus |= NS_FRAME_REFLOW_NEXTINFLOW;
    }
  } else {
    // Children always have the full size of the rows in this fragment.
    childAvailableSize = std::max(childAvailableSize, bEndRow);
  }

  return ReflowRowsInFragmentainer(aState, aContentArea, aDesiredSize, aStatus,
                                   aFragmentainer, aContainerSize, sortedItems,
                                   startRow, endRow, bSize, childAvailableSize);
}

nscoord
nsGridContainerFrame::ReflowRowsInFragmentainer(
  GridReflowState&                     aState,
  const LogicalRect&                   aContentArea,
  nsHTMLReflowMetrics&                 aDesiredSize,
  nsReflowStatus&                      aStatus,
  Fragmentainer&                       aFragmentainer,
  const nsSize&                        aContainerSize,
  const nsTArray<const GridItemInfo*>& aSortedItems,
  uint32_t                             aStartRow,
  uint32_t                             aEndRow,
  nscoord                              aBSize,
  nscoord                              aAvailableSize)
{
  FrameHashtable pushedItems;
  FrameHashtable incompleteItems;
  FrameHashtable overflowIncompleteItems;
  bool isBDBClone = aState.mReflowState->mStyleBorder->mBoxDecorationBreak ==
                      NS_STYLE_BOX_DECORATION_BREAK_CLONE;
  bool didGrowRow = false;
  // As we walk across rows, we track whether the current row is at the top
  // of its grid-fragment, to help decide whether we can break before it. When
  // this function starts, our row is at the top of the current fragment if:
  //  - we're starting with a nonzero row (i.e. we're a continuation)
  // OR:
  //  - we're starting with the first row, & we're not allowed to break before
  //    it (which makes it effectively at the top of its grid-fragment).
  bool isRowTopOfPage = aStartRow != 0 || !aFragmentainer.mCanBreakAtStart;
  const bool isStartRowTopOfPage = isRowTopOfPage;
  // Save our full available size for later.
  const nscoord gridAvailableSize = aFragmentainer.mToFragmentainerEnd;
  // Propagate the constrained size to our children.
  aFragmentainer.mToFragmentainerEnd = aAvailableSize;
  // Reflow the items in row order up to |aEndRow| and push items after that.
  uint32_t row = 0;
  // |i| is intentionally signed, so we can set it to -1 to restart the loop.
  for (int32_t i = 0, len = aSortedItems.Length(); i < len; ++i) {
    const GridItemInfo* const info = aSortedItems[i];
    nsIFrame* child = info->mFrame;
    row = info->mArea.mRows.mStart;
    MOZ_ASSERT(child->GetPrevInFlow() ? row < aStartRow : row >= aStartRow,
               "unexpected child start row");
    if (row >= aEndRow) {
      pushedItems.PutEntry(child);
      continue;
    }

    bool rowCanGrow = false;
    nscoord maxRowSize = 0;
    if (row >= aStartRow) {
      if (row > aStartRow) {
        isRowTopOfPage = false;
      }
      // Can we grow this row?  Only consider span=1 items per spec...
      rowCanGrow = !didGrowRow && info->mArea.mRows.Extent() == 1;
      if (rowCanGrow) {
        auto& sz = aState.mRows.mSizes[row];
        // and only min-/max-content rows or flex rows in an auto-sized container
        rowCanGrow = (sz.mState & TrackSize::eMinOrMaxContentMinSizing) ||
                     ((sz.mState & TrackSize::eFlexMaxSizing) &&
                      aFragmentainer.mIsAutoBSize);
        if (rowCanGrow) {
          if (isBDBClone) {
            maxRowSize = gridAvailableSize -
                         aState.mBorderPadding.BEnd(aState.mWM);
          } else {
            maxRowSize = gridAvailableSize;
          }
          maxRowSize -= sz.mPosition;
          // ...and only if there is space for it to grow.
          rowCanGrow = maxRowSize > sz.mBase;
        }
      }
    }

    // aFragmentainer.mIsTopOfPage is propagated to the child reflow state.
    // When it's false the child can request BREAK_BEFORE.  We intentionally
    // set it to false when the row is growable (as determined in CSS Grid
    // Fragmentation) and there is a non-zero space between it and the
    // fragmentainer end (that can be used to grow it).  If the child reports
    // a forced break in this case, we grow this row to fill the fragment and
    // restart the loop.  We also restart the loop with |aEndRow = row|
    // (but without growing any row) for a BREAK_BEFORE child if it spans
    // beyond the last row in this fragment.  This is to avoid fragmenting it.
    // We only restart the loop once.
    aFragmentainer.mIsTopOfPage = isRowTopOfPage && !rowCanGrow;
    nsReflowStatus childStatus;
    // Pass along how much to stretch this fragment, in case it's needed.
    nscoord bSize =
      aState.mRows.GridLineEdge(std::min(aEndRow, info->mArea.mRows.mEnd),
                                GridLineSide::eBeforeGridGap) -
      aState.mRows.GridLineEdge(std::max(aStartRow, row),
                                GridLineSide::eAfterGridGap);
    ReflowInFlowChild(child, info, aContainerSize, Some(bSize), &aFragmentainer,
                      aState, aContentArea, aDesiredSize, childStatus);
    MOZ_ASSERT(NS_INLINE_IS_BREAK_BEFORE(childStatus) ||
               !NS_FRAME_IS_FULLY_COMPLETE(childStatus) ||
               !child->GetNextInFlow(),
               "fully-complete reflow should destroy any NIFs");

    if (NS_INLINE_IS_BREAK_BEFORE(childStatus)) {
      MOZ_ASSERT(!child->GetPrevInFlow(),
                 "continuations should never report BREAK_BEFORE status");
      MOZ_ASSERT(!aFragmentainer.mIsTopOfPage,
                 "got NS_INLINE_IS_BREAK_BEFORE at top of page");
      if (!didGrowRow) {
        if (rowCanGrow) {
          // Grow this row and restart with the next row as |aEndRow|.
          aState.mRows.ResizeRow(row, maxRowSize);
          if (aState.mSharedGridData) {
            aState.mSharedGridData->mRows.ResizeRow(row, maxRowSize);
          }
          didGrowRow = true;
          aEndRow = row + 1;  // growing this row makes the next one not fit
          i = -1;  // i == 0 after the next loop increment
          isRowTopOfPage = isStartRowTopOfPage;
          overflowIncompleteItems.Clear();
          incompleteItems.Clear();
          nscoord bEndRow =
            aState.mRows.GridLineEdge(aEndRow, GridLineSide::eBeforeGridGap);
          aFragmentainer.mToFragmentainerEnd = bEndRow;
          if (aFragmentainer.mIsAutoBSize) {
            aBSize = ClampToCSSMaxBSize(bEndRow, aState.mReflowState, &aStatus);
          } else if (NS_FRAME_IS_NOT_COMPLETE(aStatus)) {
            aBSize = NS_CSS_MINMAX(aState.mReflowState->ComputedBSize(),
                                   aState.mReflowState->ComputedMinBSize(),
                                   aState.mReflowState->ComputedMaxBSize());
            aBSize = std::min(bEndRow, aBSize);
          }
          continue;
        }

        if (!isRowTopOfPage) {
          // We can break before this row - restart with it as the new end row.
          aEndRow = row;
          aBSize = aState.mRows.GridLineEdge(aEndRow, GridLineSide::eBeforeGridGap);
          i = -1;  // i == 0 after the next loop increment
          isRowTopOfPage = isStartRowTopOfPage;
          overflowIncompleteItems.Clear();
          incompleteItems.Clear();
          NS_FRAME_SET_INCOMPLETE(aStatus);
          continue;
        }
        NS_ERROR("got BREAK_BEFORE at top-of-page");
        childStatus = NS_FRAME_COMPLETE;
      } else {
        NS_ERROR("got BREAK_BEFORE again after growing the row?");
        NS_FRAME_SET_INCOMPLETE(childStatus);
      }
    } else if (NS_INLINE_IS_BREAK_AFTER(childStatus)) {
      MOZ_ASSERT_UNREACHABLE("unexpected child reflow status");
    }

    if (NS_FRAME_IS_NOT_COMPLETE(childStatus)) {
      incompleteItems.PutEntry(child);
    } else if (!NS_FRAME_IS_FULLY_COMPLETE(childStatus)) {
      overflowIncompleteItems.PutEntry(child);
    }
  }

  // Record a break before |aEndRow|.
  if (aEndRow < aState.mRows.mSizes.Length()) {
    aState.mRows.BreakBeforeRow(aEndRow);
    if (aState.mSharedGridData) {
      aState.mSharedGridData->mRows.BreakBeforeRow(aEndRow);
    }
  }

  if (!pushedItems.IsEmpty() ||
      !incompleteItems.IsEmpty() ||
      !overflowIncompleteItems.IsEmpty()) {
    if (NS_FRAME_IS_COMPLETE(aStatus)) {
      NS_FRAME_SET_OVERFLOW_INCOMPLETE(aStatus);
      aStatus |= NS_FRAME_REFLOW_NEXTINFLOW;
    }
    // Iterate the children in normal document order and append them (or a NIF)
    // to one of the following frame lists according to their status.
    nsFrameList pushedList;
    nsFrameList incompleteList;
    nsFrameList overflowIncompleteList;
    auto* pc = PresContext();
    auto* fc = pc->PresShell()->FrameConstructor();
    for (nsIFrame* child = GetChildList(kPrincipalList).FirstChild(); child; ) {
      MOZ_ASSERT((pushedItems.Contains(child) ? 1 : 0) +
                 (incompleteItems.Contains(child) ? 1 : 0) +
                 (overflowIncompleteItems.Contains(child) ? 1 : 0) <= 1,
                 "child should only be in one of these sets");
      // Save the next-sibling so we can continue the loop if |child| is moved.
      nsIFrame* next = child->GetNextSibling();
      if (pushedItems.Contains(child)) {
        MOZ_ASSERT(child->GetParent() == this);
        StealFrame(child);
        pushedList.AppendFrame(nullptr, child);
      } else if (incompleteItems.Contains(child)) {
        nsIFrame* childNIF = child->GetNextInFlow();
        if (!childNIF) {
          childNIF = fc->CreateContinuingFrame(pc, child, this);
          incompleteList.AppendFrame(nullptr, childNIF);
        } else {
          auto parent = static_cast<nsGridContainerFrame*>(childNIF->GetParent());
          MOZ_ASSERT(parent != this || !mFrames.ContainsFrame(childNIF),
                     "child's NIF shouldn't be in the same principal list");
          // If child's existing NIF is an overflow container, convert it to an
          // actual NIF, since now |child| has non-overflow stuff to give it.
          // Or, if it's further away then our next-in-flow, then pull it up.
          if ((childNIF->GetStateBits() & NS_FRAME_IS_OVERFLOW_CONTAINER) ||
              (parent != this && parent != GetNextInFlow())) {
            parent->StealFrame(childNIF);
            childNIF->RemoveStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
            if (parent == this) {
              incompleteList.AppendFrame(nullptr, childNIF);
            } else {
              // If childNIF already lives on the next grid fragment, then we
              // don't need to reparent it, since we know it's destined to end
              // up there anyway.  Just move it to its parent's overflow list.
              if (parent == GetNextInFlow()) {
                nsFrameList toMove(childNIF, childNIF);
                parent->MergeSortedOverflow(toMove);
              } else {
                ReparentFrame(childNIF, parent, this);
                incompleteList.AppendFrame(nullptr, childNIF);
              }
            }
          }
        }
      } else if (overflowIncompleteItems.Contains(child)) {
        nsIFrame* childNIF = child->GetNextInFlow();
        if (!childNIF) {
          childNIF = fc->CreateContinuingFrame(pc, child, this);
          childNIF->AddStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
          overflowIncompleteList.AppendFrame(nullptr, childNIF);
        } else {
          DebugOnly<nsGridContainerFrame*> lastParent = this;
          auto nif = static_cast<nsGridContainerFrame*>(GetNextInFlow());
          // If child has any non-overflow-container NIFs, convert them to
          // overflow containers, since that's all |child| needs now.
          while (childNIF &&
                 !childNIF->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER)) {
            auto parent = static_cast<nsGridContainerFrame*>(childNIF->GetParent());
            parent->StealFrame(childNIF);
            childNIF->AddStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
            if (parent == this) {
              overflowIncompleteList.AppendFrame(nullptr, childNIF);
            } else {
              if (!nif || parent == nif) {
                nsFrameList toMove(childNIF, childNIF);
                parent->MergeSortedExcessOverflowContainers(toMove);
              } else {
                ReparentFrame(childNIF, parent, nif);
                nsFrameList toMove(childNIF, childNIF);
                nif->MergeSortedExcessOverflowContainers(toMove);
              }
              // We only need to reparent the first childNIF (or not at all if
              // its parent is our NIF).
              nif = nullptr;
            }
            lastParent = parent;
            childNIF = childNIF->GetNextInFlow();
          }
        }
      }
      child = next;
    }

    // Merge the results into our respective overflow child lists.
    if (!pushedList.IsEmpty()) {
      MergeSortedOverflow(pushedList);
      AddStateBits(NS_STATE_GRID_DID_PUSH_ITEMS);
    }
    if (!incompleteList.IsEmpty()) {
      MergeSortedOverflow(incompleteList);
    }
    if (!overflowIncompleteList.IsEmpty()) {
      MergeSortedExcessOverflowContainers(overflowIncompleteList);
    }
  }
  return aBSize;
}

nscoord
nsGridContainerFrame::ReflowChildren(GridReflowState&     aState,
                                     const LogicalRect&   aContentArea,
                                     nsHTMLReflowMetrics& aDesiredSize,
                                     nsReflowStatus&      aStatus)
{
  MOZ_ASSERT(aState.mReflowState);

  aStatus = NS_FRAME_COMPLETE;
  nsOverflowAreas ocBounds;
  nsReflowStatus ocStatus = NS_FRAME_COMPLETE;
  if (GetPrevInFlow()) {
    ReflowOverflowContainerChildren(PresContext(), *aState.mReflowState,
                                    ocBounds, 0, ocStatus,
                                    MergeSortedFrameListsFor);
  }

  WritingMode wm = aState.mReflowState->GetWritingMode();
  const nsSize containerSize =
    (aContentArea.Size(wm) + aState.mBorderPadding.Size(wm)).GetPhysicalSize(wm);

  nscoord bSize = aContentArea.BSize(wm);
  Maybe<Fragmentainer> fragmentainer = GetNearestFragmentainer(aState);
  if (MOZ_UNLIKELY(fragmentainer.isSome())) {
    bSize = ReflowInFragmentainer(aState, aContentArea, aDesiredSize, aStatus,
                                  *fragmentainer, containerSize);
  } else {
    aState.mIter.Reset(GridItemCSSOrderIterator::eIncludeAll);
    for (; !aState.mIter.AtEnd(); aState.mIter.Next()) {
      nsIFrame* child = *aState.mIter;
      const GridItemInfo* info = nullptr;
      if (child->GetType() != nsGkAtoms::placeholderFrame) {
        info = &aState.mGridItems[aState.mIter.GridItemIndex()];
      }
      ReflowInFlowChild(*aState.mIter, info, containerSize, Nothing(), nullptr,
                        aState, aContentArea, aDesiredSize, aStatus);
      MOZ_ASSERT(NS_FRAME_IS_COMPLETE(aStatus), "child should be complete "
                 "in unconstrained reflow");
    }
  }

  // Merge overflow container bounds and status.
  aDesiredSize.mOverflowAreas.UnionWith(ocBounds);
  NS_MergeReflowStatusInto(&aStatus, ocStatus);

  if (IsAbsoluteContainer()) {
    nsFrameList children(GetChildList(GetAbsoluteListID()));
    if (!children.IsEmpty()) {
      // 'gridOrigin' is the origin of the grid (the start of the first track),
      // with respect to the grid container's padding-box (CB).
      LogicalMargin pad(aState.mReflowState->ComputedLogicalPadding());
      const LogicalPoint gridOrigin(wm, pad.IStart(wm), pad.BStart(wm));
      const LogicalRect gridCB(wm, 0, 0,
                               aContentArea.ISize(wm) + pad.IStartEnd(wm),
                               bSize + pad.BStartEnd(wm));
      const nsSize gridCBPhysicalSize = gridCB.Size(wm).GetPhysicalSize(wm);
      size_t i = 0;
      for (nsFrameList::Enumerator e(children); !e.AtEnd(); e.Next(), ++i) {
        nsIFrame* child = e.get();
        MOZ_ASSERT(i < aState.mAbsPosItems.Length());
        MOZ_ASSERT(aState.mAbsPosItems[i].mFrame == child);
        GridArea& area = aState.mAbsPosItems[i].mArea;
        LogicalRect itemCB =
          aState.ContainingBlockForAbsPos(area, gridOrigin, gridCB);
        // nsAbsoluteContainingBlock::Reflow uses physical coordinates.
        nsRect* cb = static_cast<nsRect*>(child->Properties().Get(
                       GridItemContainingBlockRect()));
        if (!cb) {
          cb = new nsRect;
          child->Properties().Set(GridItemContainingBlockRect(), cb);
        }
        *cb = itemCB.GetPhysicalRect(wm, gridCBPhysicalSize);
      }
      // We pass a dummy rect as CB because each child has its own CB rect.
      // The eIsGridContainerCB flag tells nsAbsoluteContainingBlock::Reflow to
      // use those instead.
      nsRect dummyRect;
      AbsPosReflowFlags flags =
        AbsPosReflowFlags::eCBWidthAndHeightChanged; // XXX could be optimized
      flags |= AbsPosReflowFlags::eConstrainHeight;
      flags |= AbsPosReflowFlags::eIsGridContainerCB;
      GetAbsoluteContainingBlock()->Reflow(this, PresContext(),
                                           *aState.mReflowState,
                                           aStatus, dummyRect, flags,
                                           &aDesiredSize.mOverflowAreas);
    }
  }
  return bSize;
}

void
nsGridContainerFrame::Reflow(nsPresContext*           aPresContext,
                             nsHTMLReflowMetrics&     aDesiredSize,
                             const nsHTMLReflowState& aReflowState,
                             nsReflowStatus&          aStatus)
{
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsGridContainerFrame");
  DISPLAY_REFLOW(aPresContext, this, aReflowState, aDesiredSize, aStatus);

  if (IsFrameTreeTooDeep(aReflowState, aDesiredSize, aStatus)) {
    return;
  }

  // First we gather child frames we should include in our reflow,
  // i.e. overflowed children from our prev-in-flow, and pushed first-in-flow
  // children (that might now fit). It's important to note that these children
  // can be in arbitrary order vis-a-vis the current children in our lists.
  // E.g. grid items in the document order: A, B, C may be placed in the rows
  // 3, 2, 1.  Assume each row goes in a separate grid container fragment,
  // and we reflow the second fragment.  Now if C (in fragment 1) overflows,
  // we can't just prepend it to our mFrames like we usually do because that
  // would violate the document order invariant that other code depends on.
  // Similarly if we pull up child A (from fragment 3) we can't just append
  // that for the same reason.  Instead, we must sort these children into
  // our child lists.  (The sorting is trivial given that both lists are
  // already fully sorted individually - it's just a merge.)
  //
  // The invariants that we maintain are that each grid container child list
  // is sorted in the normal document order at all times, but that children
  // in different grid container continuations may be in arbitrary order.

  auto prevInFlow = static_cast<nsGridContainerFrame*>(GetPrevInFlow());
  // Merge overflow frames from our prev-in-flow into our principal child list.
  if (prevInFlow) {
    AutoFrameListPtr overflow(aPresContext,
                              prevInFlow->StealOverflowFrames());
    if (overflow) {
      ReparentFrames(*overflow, prevInFlow, this);
      ::MergeSortedFrameLists(mFrames, *overflow, GetContent());

      // Move trailing next-in-flows into our overflow list.
      nsFrameList continuations;
      for (nsIFrame* f = mFrames.FirstChild(); f; ) {
        nsIFrame* next = f->GetNextSibling();
        nsIFrame* pif = f->GetPrevInFlow();
        if (pif && pif->GetParent() == this) {
          mFrames.RemoveFrame(f);
          continuations.AppendFrame(nullptr, f);
        }
        f = next;
      }
      MergeSortedOverflow(continuations);

      // Move trailing OC next-in-flows into our excess overflow containers list.
      nsFrameList* overflowContainers =
        GetPropTableFrames(OverflowContainersProperty());
      if (overflowContainers) {
        nsFrameList moveToEOC;
        for (nsIFrame* f = overflowContainers->FirstChild(); f; ) {
          nsIFrame* next = f->GetNextSibling();
          nsIFrame* pif = f->GetPrevInFlow();
          if (pif && pif->GetParent() == this) {
            overflowContainers->RemoveFrame(f);
            moveToEOC.AppendFrame(nullptr, f);
          }
          f = next;
        }
        if (overflowContainers->IsEmpty()) {
          Properties().Delete(OverflowContainersProperty());
        }
        MergeSortedExcessOverflowContainers(moveToEOC);
      }
    }
  }

  // Merge our own overflow frames into our principal child list,
  // except those that are a next-in-flow for one of our items.
  DebugOnly<bool> foundOwnPushedChild = false;
  {
    nsFrameList* ourOverflow = GetOverflowFrames();
    if (ourOverflow) {
      nsFrameList items;
      for (nsIFrame* f = ourOverflow->FirstChild(); f; ) {
        nsIFrame* next = f->GetNextSibling();
        nsIFrame* pif = f->GetPrevInFlow();
        if (!pif || pif->GetParent() != this) {
          MOZ_ASSERT(f->GetParent() == this);
          ourOverflow->RemoveFrame(f);
          items.AppendFrame(nullptr, f);
          if (!pif) {
            foundOwnPushedChild = true;
          }
        }
        f = next;
      }
      ::MergeSortedFrameLists(mFrames, items, GetContent());
      if (ourOverflow->IsEmpty()) {
        DestroyOverflowList();
      }
    }
  }

  // Pull up any first-in-flow children we might have pushed.
  if (HasAnyStateBits(NS_STATE_GRID_DID_PUSH_ITEMS)) {
    RemoveStateBits(NS_STATE_GRID_DID_PUSH_ITEMS);
    nsFrameList items;
    auto nif = static_cast<nsGridContainerFrame*>(GetNextInFlow());
    auto firstNIF = nif;
    DebugOnly<bool> nifNeedPushedItem = false;
    while (nif) {
      nsFrameList nifItems;
      for (nsIFrame* nifChild = nif->GetChildList(kPrincipalList).FirstChild();
           nifChild; ) {
        nsIFrame* next = nifChild->GetNextSibling();
        if (!nifChild->GetPrevInFlow()) {
          nif->StealFrame(nifChild);
          ReparentFrame(nifChild, nif, this);
          nifItems.AppendFrame(nullptr, nifChild);
          nifNeedPushedItem = false;
        }
        nifChild = next;
      }
      ::MergeSortedFrameLists(items, nifItems, GetContent());

      if (!nif->HasAnyStateBits(NS_STATE_GRID_DID_PUSH_ITEMS)) {
        MOZ_ASSERT(!nifNeedPushedItem || mDidPushItemsBitMayLie,
                   "NS_STATE_GRID_DID_PUSH_ITEMS lied");
        break;
      }
      nifNeedPushedItem = true;

      for (nsIFrame* nifChild = nif->GetChildList(kOverflowList).FirstChild();
           nifChild; ) {
        nsIFrame* next = nifChild->GetNextSibling();
        if (!nifChild->GetPrevInFlow()) {
          nif->StealFrame(nifChild);
          ReparentFrame(nifChild, nif, this);
          nifItems.AppendFrame(nullptr, nifChild);
          nifNeedPushedItem = false;
        }
        nifChild = next;
      }
      ::MergeSortedFrameLists(items, nifItems, GetContent());

      nif->RemoveStateBits(NS_STATE_GRID_DID_PUSH_ITEMS);
      nif = static_cast<nsGridContainerFrame*>(nif->GetNextInFlow());
      MOZ_ASSERT(nif || !nifNeedPushedItem || mDidPushItemsBitMayLie,
                 "NS_STATE_GRID_DID_PUSH_ITEMS lied");
    }

    if (!items.IsEmpty()) {
      // Pull up the first next-in-flow of the pulled up items too, unless its
      // parent is our nif (to avoid leaving a hole there).
      nsFrameList childNIFs;
      nsFrameList childOCNIFs;
      for (auto child : items) {
        auto childNIF = child->GetNextInFlow();
        if (childNIF && childNIF->GetParent() != firstNIF) {
          auto parent = childNIF->GetParent();
          parent->StealFrame(childNIF);
          ReparentFrame(childNIF, parent, firstNIF);
          if ((childNIF->GetStateBits() & NS_FRAME_IS_OVERFLOW_CONTAINER)) {
            childOCNIFs.AppendFrame(nullptr, childNIF);
          } else {
            childNIFs.AppendFrame(nullptr, childNIF);
          }
        }
      }
      // Merge items' NIFs into our NIF's respective overflow child lists.
      firstNIF->MergeSortedOverflow(childNIFs);
      firstNIF->MergeSortedExcessOverflowContainers(childOCNIFs);
    }

    MOZ_ASSERT(foundOwnPushedChild || !items.IsEmpty() || mDidPushItemsBitMayLie,
               "NS_STATE_GRID_DID_PUSH_ITEMS lied");
    ::MergeSortedFrameLists(mFrames, items, GetContent());
  }

#ifdef DEBUG
  mDidPushItemsBitMayLie = false;
  SanityCheckGridItemsBeforeReflow();
#endif // DEBUG

  const nsStylePosition* stylePos = aReflowState.mStylePosition;
  if (!prevInFlow) {
    InitImplicitNamedAreas(stylePos);
  }
  GridReflowState gridReflowState(this, aReflowState);
  if (gridReflowState.mIter.ItemsAreAlreadyInOrder()) {
    AddStateBits(NS_STATE_GRID_NORMAL_FLOW_CHILDREN_IN_CSS_ORDER);
  } else {
    RemoveStateBits(NS_STATE_GRID_NORMAL_FLOW_CHILDREN_IN_CSS_ORDER);
  }
  const nscoord computedBSize = aReflowState.ComputedBSize();
  const nscoord computedISize = aReflowState.ComputedISize();
  const WritingMode& wm = gridReflowState.mWM;
  LogicalSize computedSize(wm, computedISize, computedBSize);

  nscoord consumedBSize = 0;
  if (!prevInFlow) {
    // ComputedMinSize is zero rather than NS_UNCONSTRAINEDSIZE when indefinite
    // (unfortunately) so we have to check the style data and parent reflow state
    // to determine if it's indefinite.
    LogicalSize computedMinSize(aReflowState.ComputedMinSize());
    const nsHTMLReflowState* cbState = aReflowState.mCBReflowState;
    if (!stylePos->MinISize(wm).IsCoordPercentCalcUnit() ||
        (stylePos->MinISize(wm).HasPercent() && cbState &&
         cbState->ComputedSize(wm).ISize(wm) == NS_UNCONSTRAINEDSIZE)) {
      computedMinSize.ISize(wm) = NS_UNCONSTRAINEDSIZE;
    }
    if (!stylePos->MinBSize(wm).IsCoordPercentCalcUnit() ||
        (stylePos->MinBSize(wm).HasPercent() && cbState &&
         cbState->ComputedSize(wm).BSize(wm) == NS_UNCONSTRAINEDSIZE)) {
      computedMinSize.BSize(wm) = NS_UNCONSTRAINEDSIZE;
    }
    Grid grid;
    grid.PlaceGridItems(gridReflowState, computedMinSize, computedSize,
                        aReflowState.ComputedMaxSize());

    gridReflowState.CalculateTrackSizes(grid, computedSize,
                                        nsLayoutUtils::PREF_ISIZE);

    // FIXME bug 1229180: Instead of doing this on every reflow, we should only
    // set these properties if they are needed.
    nsTArray<nscoord> colTrackSizes(gridReflowState.mCols.mSizes.Length());
    for (const TrackSize& sz : gridReflowState.mCols.mSizes) {
      colTrackSizes.AppendElement(sz.mBase);
    }
    ComputedGridTrackInfo* colInfo = new ComputedGridTrackInfo(
      gridReflowState.mColFunctions.mExplicitGridOffset,
      gridReflowState.mColFunctions.NumExplicitTracks(),
      Move(colTrackSizes));
    Properties().Set(GridColTrackInfo(), colInfo);
    
    nsTArray<nscoord> rowTrackSizes(gridReflowState.mRows.mSizes.Length());
    for (const TrackSize& sz : gridReflowState.mRows.mSizes) {
      rowTrackSizes.AppendElement(sz.mBase);
    }
    ComputedGridTrackInfo* rowInfo = new ComputedGridTrackInfo(
      gridReflowState.mRowFunctions.mExplicitGridOffset,
      gridReflowState.mRowFunctions.NumExplicitTracks(),
      Move(rowTrackSizes));
    Properties().Set(GridRowTrackInfo(), rowInfo);
  } else {
    consumedBSize = GetConsumedBSize();
    gridReflowState.InitializeForContinuation(this, consumedBSize);
  }

  nscoord bSize = 0;
  if (computedBSize == NS_AUTOHEIGHT) {
    const uint32_t numRows = gridReflowState.mRows.mSizes.Length();
    if (!prevInFlow) {
      // Note: we can't use GridLineEdge here since we haven't calculated
      // the rows' mPosition yet (happens in AlignJustifyContent below).
      for (uint32_t i = 0; i < numRows; ++i) {
        bSize += gridReflowState.mRows.mSizes[i].mBase;
      }
      bSize += gridReflowState.mRows.SumOfGridGaps();
    } else {
      bSize = gridReflowState.mRows.GridLineEdge(numRows,
                                                 GridLineSide::eAfterGridGap);
    }
    bSize = NS_CSS_MINMAX(bSize,
                          aReflowState.ComputedMinBSize(),
                          aReflowState.ComputedMaxBSize());
  } else {
    bSize = computedBSize;
  }
  bSize = std::max(bSize - consumedBSize, 0);
  auto& bp = gridReflowState.mBorderPadding;
  LogicalRect contentArea(wm, bp.IStart(wm), bp.BStart(wm),
                          computedISize, bSize);

  if (!prevInFlow) {
    // Apply 'align/justify-content' to the grid.
    gridReflowState.mCols.AlignJustifyContent(aReflowState, contentArea.Size(wm));
    gridReflowState.mRows.AlignJustifyContent(aReflowState, contentArea.Size(wm));
  }

  bSize = ReflowChildren(gridReflowState, contentArea, aDesiredSize, aStatus);
  bSize = std::max(bSize - consumedBSize, 0);

  // Skip our block-end border if we're INCOMPLETE.
  if (!NS_FRAME_IS_COMPLETE(aStatus) &&
      !gridReflowState.mSkipSides.BEnd() &&
      StyleBorder()->mBoxDecorationBreak !=
        NS_STYLE_BOX_DECORATION_BREAK_CLONE) {
    bp.BEnd(wm) = nscoord(0);
  }

  LogicalSize desiredSize(wm, computedISize + bp.IStartEnd(wm),
                              bSize         + bp.BStartEnd(wm));
  aDesiredSize.SetSize(wm, desiredSize);
  aDesiredSize.mOverflowAreas.UnionAllWith(nsRect(0, 0,
                                                  aDesiredSize.Width(),
                                                  aDesiredSize.Height()));

  // Convert INCOMPLETE -> OVERFLOW_INCOMPLETE and zero bsize if we're an OC.
  if (HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER)) {
    if (!NS_FRAME_IS_COMPLETE(aStatus)) {
      NS_FRAME_SET_OVERFLOW_INCOMPLETE(aStatus);
      aStatus |= NS_FRAME_REFLOW_NEXTINFLOW;
    }
    bSize = 0;
    desiredSize.BSize(wm) = bSize + bp.BStartEnd(wm);
    aDesiredSize.SetSize(wm, desiredSize);
  }

  if (!prevInFlow) {
    auto sharedGridData = static_cast<SharedGridData*>(
      Properties().Get(SharedGridData::Prop()));
    if (!NS_FRAME_IS_FULLY_COMPLETE(aStatus)) {
      if (!sharedGridData) {
        sharedGridData = new SharedGridData;
        Properties().Set(SharedGridData::Prop(), sharedGridData);
      }
      sharedGridData->mCols.mSizes.Clear();
      sharedGridData->mCols.mSizes.SwapElements(gridReflowState.mCols.mSizes);
      sharedGridData->mCols.mContentBoxSize = gridReflowState.mCols.mContentBoxSize;
      sharedGridData->mCols.mBaselineSubtreeAlign[0] =
        gridReflowState.mCols.mBaselineSubtreeAlign[0];
      sharedGridData->mCols.mBaselineSubtreeAlign[1] =
        gridReflowState.mCols.mBaselineSubtreeAlign[1];
      sharedGridData->mRows.mSizes.Clear();
      sharedGridData->mRows.mSizes.SwapElements(gridReflowState.mRows.mSizes);
      // Save the original row grid sizes and gaps so we can restore them later
      // in GridReflowState::Initialize for the continuations.
      auto& origRowData = sharedGridData->mOriginalRowData;
      origRowData.ClearAndRetainStorage();
      origRowData.SetCapacity(sharedGridData->mRows.mSizes.Length());
      nscoord prevTrackEnd = 0;
      for (auto& sz : sharedGridData->mRows.mSizes) {
        SharedGridData::RowData data = {sz.mBase, sz.mPosition - prevTrackEnd};
        origRowData.AppendElement(data);
        prevTrackEnd = sz.mPosition + sz.mBase;
      }
      sharedGridData->mRows.mContentBoxSize = gridReflowState.mRows.mContentBoxSize;
      sharedGridData->mRows.mBaselineSubtreeAlign[0] =
        gridReflowState.mRows.mBaselineSubtreeAlign[0];
      sharedGridData->mRows.mBaselineSubtreeAlign[1] =
        gridReflowState.mRows.mBaselineSubtreeAlign[1];
      sharedGridData->mGridItems.Clear();
      sharedGridData->mGridItems.SwapElements(gridReflowState.mGridItems);
      sharedGridData->mAbsPosItems.Clear();
      sharedGridData->mAbsPosItems.SwapElements(gridReflowState.mAbsPosItems);
    } else if (sharedGridData && !GetNextInFlow()) {
      Properties().Delete(SharedGridData::Prop());
    }
  }

  FinishAndStoreOverflow(&aDesiredSize);
  NS_FRAME_SET_TRUNCATION(aStatus, aReflowState, aDesiredSize);
}

nscoord
nsGridContainerFrame::IntrinsicISize(nsRenderingContext* aRenderingContext,
                                     IntrinsicISizeType  aConstraint)
{
  // Calculate the sum of column sizes under aConstraint.
  // http://dev.w3.org/csswg/css-grid/#intrinsic-sizes
  GridReflowState state(this, *aRenderingContext);
  InitImplicitNamedAreas(state.mGridStyle); // XXX optimize
  LogicalSize indefinite(state.mWM, NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  Grid grid;
  grid.PlaceGridItems(state, indefinite, indefinite, indefinite);  // XXX optimize
  if (grid.mGridColEnd == 0) {
    return 0;
  }
  state.mCols.Initialize(state.mColFunctions, state.mGridStyle->mGridColumnGap,
                         grid.mGridColEnd, NS_UNCONSTRAINEDSIZE);
  state.mCols.CalculateSizes(state, state.mGridItems, state.mColFunctions,
                             NS_UNCONSTRAINEDSIZE, &GridArea::mCols,
                             aConstraint);
  nscoord length = 0;
  for (const TrackSize& sz : state.mCols.mSizes) {
    length += sz.mBase;
  }
  return length + state.mCols.SumOfGridGaps();
}

nscoord
nsGridContainerFrame::GetMinISize(nsRenderingContext* aRC)
{
  DISPLAY_MIN_WIDTH(this, mCachedMinISize);
  if (mCachedMinISize == NS_INTRINSIC_WIDTH_UNKNOWN) {
    mCachedMinISize = IntrinsicISize(aRC, nsLayoutUtils::MIN_ISIZE);
  }
  return mCachedMinISize;
}

nscoord
nsGridContainerFrame::GetPrefISize(nsRenderingContext* aRC)
{
  DISPLAY_PREF_WIDTH(this, mCachedPrefISize);
  if (mCachedPrefISize == NS_INTRINSIC_WIDTH_UNKNOWN) {
    mCachedPrefISize = IntrinsicISize(aRC, nsLayoutUtils::PREF_ISIZE);
  }
  return mCachedPrefISize;
}

void
nsGridContainerFrame::MarkIntrinsicISizesDirty()
{
  mCachedMinISize = NS_INTRINSIC_WIDTH_UNKNOWN;
  mCachedPrefISize = NS_INTRINSIC_WIDTH_UNKNOWN;
  nsContainerFrame::MarkIntrinsicISizesDirty();
}

nsIAtom*
nsGridContainerFrame::GetType() const
{
  return nsGkAtoms::gridContainerFrame;
}

void
nsGridContainerFrame::BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                                       const nsRect&           aDirtyRect,
                                       const nsDisplayListSet& aLists)
{
  DisplayBorderBackgroundOutline(aBuilder, aLists);
  if (GetPrevInFlow()) {
    DisplayOverflowContainers(aBuilder, aDirtyRect, aLists);
  }

  // Our children are all grid-level boxes, which behave the same as
  // inline-blocks in painting, so their borders/backgrounds all go on
  // the BlockBorderBackgrounds list.
  typedef GridItemCSSOrderIterator::OrderState OrderState;
  OrderState order = HasAnyStateBits(NS_STATE_GRID_NORMAL_FLOW_CHILDREN_IN_CSS_ORDER)
                       ? OrderState::eKnownOrdered
                       : OrderState::eKnownUnordered;
  GridItemCSSOrderIterator iter(this, kPrincipalList,
                                GridItemCSSOrderIterator::eIncludeAll, order);
  for (; !iter.AtEnd(); iter.Next()) {
    nsIFrame* child = *iter;
    BuildDisplayListForChild(aBuilder, child, aDirtyRect, aLists,
                             ::GetDisplayFlagsForGridItem(child));
  }
}

bool
nsGridContainerFrame::DrainSelfOverflowList()
{
  // Unlike nsContainerFrame::DrainSelfOverflowList we need to merge these lists
  // so that the resulting mFrames is in document content order.
  // NOTE: nsContainerFrame::AppendFrames/InsertFrames calls this method.
  AutoFrameListPtr overflowFrames(PresContext(), StealOverflowFrames());
  if (overflowFrames) {
    ::MergeSortedFrameLists(mFrames, *overflowFrames, GetContent());
    return true;
  }
  return false;
}

void
nsGridContainerFrame::AppendFrames(ChildListID aListID, nsFrameList& aFrameList)
{
  NoteNewChildren(aListID, aFrameList);
  nsContainerFrame::AppendFrames(aListID, aFrameList);
}

void
nsGridContainerFrame::InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                                   nsFrameList& aFrameList)
{
  NoteNewChildren(aListID, aFrameList);
  nsContainerFrame::InsertFrames(aListID, aPrevFrame, aFrameList);
}

void
nsGridContainerFrame::RemoveFrame(ChildListID aListID, nsIFrame* aOldFrame)
{
#ifdef DEBUG
  ChildListIDs supportedLists =
    kAbsoluteList | kFixedList | kPrincipalList | kNoReflowPrincipalList;
  MOZ_ASSERT(supportedLists.Contains(aListID), "unexpected child list");

  // Note that kPrincipalList doesn't mean aOldFrame must be on that list.
  // It can also be on kOverflowList, in which case it might be a pushed
  // item, and if it's the only pushed item our DID_PUSH_ITEMS bit will lie.
  mDidPushItemsBitMayLie = mDidPushItemsBitMayLie ||
                           (aListID == kPrincipalList &&
                            !aOldFrame->GetPrevInFlow());
#endif

  nsContainerFrame::RemoveFrame(aListID, aOldFrame);
}

#ifdef DEBUG_FRAME_DUMP
nsresult
nsGridContainerFrame::GetFrameName(nsAString& aResult) const
{
  return MakeFrameName(NS_LITERAL_STRING("GridContainer"), aResult);
}
#endif

void
nsGridContainerFrame::NoteNewChildren(ChildListID aListID,
                                      const nsFrameList& aFrameList)
{
#ifdef DEBUG
  ChildListIDs supportedLists =
    kAbsoluteList | kFixedList | kPrincipalList | kNoReflowPrincipalList;
  MOZ_ASSERT(supportedLists.Contains(aListID), "unexpected child list");
#endif

  nsIPresShell* shell = PresContext()->PresShell();
  for (auto pif = GetPrevInFlow(); pif; pif = pif->GetPrevInFlow()) {
    if (aListID == kPrincipalList) {
      pif->AddStateBits(NS_STATE_GRID_DID_PUSH_ITEMS);
    }
    shell->FrameNeedsReflow(pif, nsIPresShell::eTreeChange, NS_FRAME_IS_DIRTY);
  }
}

void
nsGridContainerFrame::MergeSortedOverflow(nsFrameList& aList)
{
  if (aList.IsEmpty()) {
    return;
  }
  MOZ_ASSERT(!aList.FirstChild()->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER),
             "this is the wrong list to put this child frame");
  MOZ_ASSERT(aList.FirstChild()->GetParent() == this);
  nsFrameList* overflow = GetOverflowFrames();
  if (overflow) {
    ::MergeSortedFrameLists(*overflow, aList, GetContent());
  } else {
    SetOverflowFrames(aList);
  }
}

void
nsGridContainerFrame::MergeSortedExcessOverflowContainers(nsFrameList& aList)
{
  if (aList.IsEmpty()) {
    return;
  }
  MOZ_ASSERT(aList.FirstChild()->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER),
             "this is the wrong list to put this child frame");
  MOZ_ASSERT(aList.FirstChild()->GetParent() == this);
  nsFrameList* eoc = GetPropTableFrames(ExcessOverflowContainersProperty());
  if (eoc) {
    ::MergeSortedFrameLists(*eoc, aList, GetContent());
  } else {
    SetPropTableFrames(new (PresContext()->PresShell()) nsFrameList(aList),
                       ExcessOverflowContainersProperty());
  }
}

#ifdef DEBUG
void
nsGridContainerFrame::SetInitialChildList(ChildListID  aListID,
                                          nsFrameList& aChildList)
{
  ChildListIDs supportedLists = kAbsoluteList | kFixedList | kPrincipalList;
  MOZ_ASSERT(supportedLists.Contains(aListID), "unexpected child list");

  return nsContainerFrame::SetInitialChildList(aListID, aChildList);
}

void
nsGridContainerFrame::SanityCheckGridItemsBeforeReflow() const
{
  ChildListIDs absLists = kAbsoluteList | kFixedList |
    kOverflowContainersList | kExcessOverflowContainersList;
  ChildListIDs itemLists = kPrincipalList | kOverflowList;
  for (const nsIFrame* f = this; f; f = f->GetNextInFlow()) {
    MOZ_ASSERT(!f->HasAnyStateBits(NS_STATE_GRID_DID_PUSH_ITEMS),
               "At start of reflow, we should've pulled items back from all "
               "NIFs and cleared NS_STATE_GRID_DID_PUSH_ITEMS in the process");
    for (nsIFrame::ChildListIterator childLists(f);
         !childLists.IsDone(); childLists.Next()) {
      if (!itemLists.Contains(childLists.CurrentID())) {
        MOZ_ASSERT(absLists.Contains(childLists.CurrentID()),
                   "unexpected non-empty child list");
        continue;
      }
      for (auto child : childLists.CurrentList()) {
        MOZ_ASSERT(f == this || child->GetPrevInFlow(),
                   "all pushed items must be pulled up before reflow");
      }
    }
  }
  // If we have a prev-in-flow, each of its children's next-in-flow
  // should be one of our children or be null.
  const auto pif = static_cast<nsGridContainerFrame*>(GetPrevInFlow());
  if (pif) {
    const nsFrameList* oc =
      GetPropTableFrames(OverflowContainersProperty());
    const nsFrameList* eoc =
      GetPropTableFrames(ExcessOverflowContainersProperty());
    const nsFrameList* pifEOC =
      pif->GetPropTableFrames(ExcessOverflowContainersProperty());
    for (const nsIFrame* child : pif->GetChildList(kPrincipalList)) {
      const nsIFrame* childNIF = child->GetNextInFlow();
      MOZ_ASSERT(!childNIF || mFrames.ContainsFrame(childNIF) ||
                 (pifEOC && pifEOC->ContainsFrame(childNIF)) ||
                 (oc && oc->ContainsFrame(childNIF)) ||
                 (eoc && eoc->ContainsFrame(childNIF)));
    }
  }
}

void
nsGridContainerFrame::TrackSize::Dump() const
{
  printf("mPosition=%d mBase=%d mLimit=%d", mPosition, mBase, mLimit);

  printf(" min:");
  if (mState & eAutoMinSizing) {
    printf("auto ");
  } else if (mState & eMinContentMinSizing) {
    printf("min-content ");
  } else if (mState & eMaxContentMinSizing) {
    printf("max-content ");
  } else if (mState & eFlexMinSizing) {
    printf("flex ");
  }

  printf(" max:");
  if (mState & eAutoMaxSizing) {
    printf("auto ");
  } else if (mState & eMinContentMaxSizing) {
    printf("min-content ");
  } else if (mState & eMaxContentMaxSizing) {
    printf("max-content ");
  } else if (mState & eFlexMaxSizing) {
    printf("flex ");
  }

  if (mState & eFrozen) {
    printf("frozen ");
  }
  if (mState & eBreakBefore) {
    printf("break-before ");
  }
}

#endif // DEBUG
