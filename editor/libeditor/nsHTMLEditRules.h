/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHTMLEditRules_h__
#define nsHTMLEditRules_h__

#include "TypeInState.h"
#include "nsAutoPtr.h"
#include "nsCOMPtr.h"
#include "nsEditor.h"
#include "nsIEditActionListener.h"
#include "nsIEditor.h"
#include "nsIHTMLEditor.h"
#include "nsISupportsImpl.h"
#include "nsSelectionState.h"
#include "nsTArray.h"
#include "nsTextEditRules.h"
#include "nscore.h"

class nsHTMLEditor;
class nsIAtom;
class nsIDOMCharacterData;
class nsIDOMDocument;
class nsIDOMElement;
class nsIDOMNode;
class nsIEditor;
class nsINode;
class nsPlaintextEditor;
class nsRange;
class nsRulesInfo;
namespace mozilla {
namespace dom {
class Element;
class Selection;
} // namespace dom
} // namespace mozilla
struct DOMPoint;

struct StyleCache : public PropItem
{
  bool mPresent;

  StyleCache() : PropItem(), mPresent(false) {
    MOZ_COUNT_CTOR(StyleCache);
  }

  StyleCache(nsIAtom *aTag, const nsAString &aAttr, const nsAString &aValue) :
             PropItem(aTag, aAttr, aValue), mPresent(false) {
    MOZ_COUNT_CTOR(StyleCache);
  }

  ~StyleCache() {
    MOZ_COUNT_DTOR(StyleCache);
  }
};


#define SIZE_STYLE_TABLE 19

class nsHTMLEditRules : public nsTextEditRules, public nsIEditActionListener
{
public:

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(nsHTMLEditRules, nsTextEditRules)

  nsHTMLEditRules();

  // nsIEditRules methods
  NS_IMETHOD Init(nsPlaintextEditor *aEditor) override;
  NS_IMETHOD DetachEditor() override;
  NS_IMETHOD BeforeEdit(EditAction action,
                        nsIEditor::EDirection aDirection) override;
  NS_IMETHOD AfterEdit(EditAction action,
                       nsIEditor::EDirection aDirection) override;
  NS_IMETHOD WillDoAction(Selection* aSelection, nsRulesInfo* aInfo,
                          bool* aCancel, bool* aHandled) override;
  NS_IMETHOD DidDoAction(Selection* aSelection, nsRulesInfo* aInfo,
                         nsresult aResult) override;
  NS_IMETHOD DocumentModified() override;

  nsresult GetListState(bool *aMixed, bool *aOL, bool *aUL, bool *aDL);
  nsresult GetListItemState(bool *aMixed, bool *aLI, bool *aDT, bool *aDD);
  nsresult GetIndentState(bool *aCanIndent, bool *aCanOutdent);
  nsresult GetAlignment(bool *aMixed, nsIHTMLEditor::EAlignment *aAlign);
  nsresult GetParagraphState(bool *aMixed, nsAString &outFormat);
  nsresult MakeSureElemStartsOrEndsOnCR(nsIDOMNode *aNode);

  // nsIEditActionListener methods

  NS_IMETHOD WillCreateNode(const nsAString& aTag, nsIDOMNode *aParent, int32_t aPosition) override;
  NS_IMETHOD DidCreateNode(const nsAString& aTag, nsIDOMNode *aNode, nsIDOMNode *aParent, int32_t aPosition, nsresult aResult) override;
  NS_IMETHOD WillInsertNode(nsIDOMNode *aNode, nsIDOMNode *aParent, int32_t aPosition) override;
  NS_IMETHOD DidInsertNode(nsIDOMNode *aNode, nsIDOMNode *aParent, int32_t aPosition, nsresult aResult) override;
  NS_IMETHOD WillDeleteNode(nsIDOMNode *aChild) override;
  NS_IMETHOD DidDeleteNode(nsIDOMNode *aChild, nsresult aResult) override;
  NS_IMETHOD WillSplitNode(nsIDOMNode *aExistingRightNode, int32_t aOffset) override;
  NS_IMETHOD DidSplitNode(nsIDOMNode *aExistingRightNode, int32_t aOffset, nsIDOMNode *aNewLeftNode, nsresult aResult) override;
  NS_IMETHOD WillJoinNodes(nsIDOMNode *aLeftNode, nsIDOMNode *aRightNode, nsIDOMNode *aParent) override;
  NS_IMETHOD DidJoinNodes(nsIDOMNode  *aLeftNode, nsIDOMNode *aRightNode, nsIDOMNode *aParent, nsresult aResult) override;
  NS_IMETHOD WillInsertText(nsIDOMCharacterData *aTextNode, int32_t aOffset, const nsAString &aString) override;
  NS_IMETHOD DidInsertText(nsIDOMCharacterData *aTextNode, int32_t aOffset, const nsAString &aString, nsresult aResult) override;
  NS_IMETHOD WillDeleteText(nsIDOMCharacterData *aTextNode, int32_t aOffset, int32_t aLength) override;
  NS_IMETHOD DidDeleteText(nsIDOMCharacterData *aTextNode, int32_t aOffset, int32_t aLength, nsresult aResult) override;
  NS_IMETHOD WillDeleteSelection(nsISelection *aSelection) override;
  NS_IMETHOD DidDeleteSelection(nsISelection *aSelection) override;
  void DeleteNodeIfCollapsedText(nsINode& aNode);

protected:
  virtual ~nsHTMLEditRules();

  enum RulesEndpoint
  {
    kStart,
    kEnd
  };

  void InitFields();

  // nsHTMLEditRules implementation methods
  void WillInsert(Selection& aSelection, bool* aCancel);
  nsresult WillInsertText(  EditAction aAction,
                            Selection* aSelection,
                            bool            *aCancel,
                            bool            *aHandled,
                            const nsAString *inString,
                            nsAString       *outString,
                            int32_t          aMaxLength);
  nsresult WillLoadHTML(Selection* aSelection, bool* aCancel);
  nsresult WillInsertBreak(Selection& aSelection, bool* aCancel,
                           bool* aHandled);
  nsresult StandardBreakImpl(nsINode& aNode, int32_t aOffset,
                             Selection& aSelection);
  nsresult DidInsertBreak(Selection* aSelection, nsresult aResult);
  nsresult SplitMailCites(Selection* aSelection, bool* aHandled);
  nsresult WillDeleteSelection(Selection* aSelection,
                               nsIEditor::EDirection aAction,
                               nsIEditor::EStripWrappers aStripWrappers,
                               bool* aCancel, bool* aHandled);
  nsresult DidDeleteSelection(Selection* aSelection,
                              nsIEditor::EDirection aDir,
                              nsresult aResult);
  nsresult InsertBRIfNeeded(Selection* aSelection);
  ::DOMPoint GetGoodSelPointForNode(nsINode& aNode,
                                    nsIEditor::EDirection aAction);
  nsresult JoinBlocks(nsIContent& aLeftNode, nsIContent& aRightNode,
                      bool* aCanceled);
  nsresult MoveBlock(Element& aLeftBlock, Element& aRightBlock,
                     int32_t aLeftOffset, int32_t aRightOffset);
  nsresult MoveNodeSmart(nsIContent& aNode, Element& aDestElement,
                         int32_t* aOffset);
  nsresult MoveContents(Element& aElement, Element& aDestElement,
                        int32_t* aOffset);
  nsresult DeleteNonTableElements(nsINode* aNode);
  nsresult WillMakeList(Selection* aSelection,
                        const nsAString* aListType,
                        bool aEntireList,
                        const nsAString* aBulletType,
                        bool* aCancel, bool* aHandled,
                        const nsAString* aItemType = nullptr);
  nsresult WillRemoveList(Selection* aSelection, bool aOrdered, bool* aCancel,
                          bool* aHandled);
  nsresult WillIndent(Selection* aSelection, bool* aCancel, bool* aHandled);
  nsresult WillCSSIndent(Selection* aSelection, bool* aCancel, bool* aHandled);
  nsresult WillHTMLIndent(Selection* aSelection, bool* aCancel,
                          bool* aHandled);
  nsresult WillOutdent(Selection& aSelection, bool* aCancel, bool* aHandled);
  nsresult WillAlign(Selection& aSelection, const nsAString& aAlignType,
                     bool* aCancel, bool* aHandled);
  nsresult WillAbsolutePosition(Selection& aSelection, bool* aCancel,
                                bool* aHandled);
  nsresult WillRemoveAbsolutePosition(Selection* aSelection, bool* aCancel,
                                      bool* aHandled);
  nsresult WillRelativeChangeZIndex(Selection* aSelection, int32_t aChange,
                                    bool* aCancel, bool* aHandled);
  nsresult WillMakeDefListItem(Selection* aSelection,
                               const nsAString* aBlockType, bool aEntireList,
                               bool* aCancel, bool* aHandled);
  nsresult WillMakeBasicBlock(Selection& aSelection,
                              const nsAString& aBlockType,
                              bool* aCancel, bool* aHandled);
  nsresult DidMakeBasicBlock(Selection* aSelection, nsRulesInfo* aInfo,
                             nsresult aResult);
  nsresult DidAbsolutePosition();
  nsresult AlignInnerBlocks(nsINode& aNode, const nsAString* alignType);
  nsresult AlignBlockContents(nsIDOMNode *aNode, const nsAString *alignType);
  nsresult AppendInnerFormatNodes(nsTArray<OwningNonNull<nsINode>>& aArray,
                                  nsINode* aNode);
  nsresult GetFormatString(nsIDOMNode *aNode, nsAString &outFormat);
  enum class Lists { no, yes };
  enum class Tables { no, yes };
  void GetInnerContent(nsINode& aNode,
                       nsTArray<OwningNonNull<nsINode>>& aOutArrayOfNodes,
                       int32_t* aIndex, Lists aLists = Lists::yes,
                       Tables aTables = Tables::yes);
  Element* IsInListItem(nsINode* aNode);
  nsresult ReturnInHeader(Selection& aSelection, Element& aHeader,
                          nsINode& aNode, int32_t aOffset);
  nsresult ReturnInParagraph(Selection* aSelection, nsIDOMNode* aHeader,
                             nsIDOMNode* aTextNode, int32_t aOffset,
                             bool* aCancel, bool* aHandled);
  nsresult SplitParagraph(nsIDOMNode *aPara,
                          nsIContent* aBRNode,
                          Selection* aSelection,
                          nsCOMPtr<nsIDOMNode> *aSelNode,
                          int32_t *aOffset);
  nsresult ReturnInListItem(Selection& aSelection, Element& aHeader,
                            nsINode& aNode, int32_t aOffset);
  nsresult AfterEditInner(EditAction action,
                          nsIEditor::EDirection aDirection);
  nsresult RemovePartOfBlock(Element& aBlock, nsIContent& aStartChild,
                             nsIContent& aEndChild);
  void     SplitBlock(Element& aBlock,
                      nsIContent& aStartChild,
                      nsIContent& aEndChild,
                      nsIContent** aOutLeftNode = nullptr,
                      nsIContent** aOutRightNode = nullptr,
                      nsIContent** aOutMiddleNode = nullptr);
  nsresult OutdentPartOfBlock(Element& aBlock,
                              nsIContent& aStartChild,
                              nsIContent& aEndChild,
                              bool aIsBlockIndentedWithCSS,
                              nsIContent** aOutLeftNode,
                              nsIContent** aOutRightNode);

  nsresult ConvertListType(Element* aList, Element** aOutList,
                           nsIAtom* aListType, nsIAtom* aItemType);

  nsresult CreateStyleForInsertText(Selection& aSelection, nsIDocument& aDoc);
  enum class MozBRCounts { yes, no };
  nsresult IsEmptyBlock(Element& aNode, bool* aOutIsEmptyBlock,
                        MozBRCounts aMozBRCounts = MozBRCounts::yes);
  nsresult CheckForEmptyBlock(nsINode* aStartNode, Element* aBodyNode,
                              Selection* aSelection,
                              nsIEditor::EDirection aAction, bool* aHandled);
  enum class BRLocation { beforeBlock, blockEnd };
  Element* CheckForInvisibleBR(Element& aBlock, BRLocation aWhere,
                               int32_t aOffset = 0);
  nsresult ExpandSelectionForDeletion(Selection& aSelection);
  bool IsFirstNode(nsIDOMNode *aNode);
  bool IsLastNode(nsIDOMNode *aNode);
  nsresult NormalizeSelection(Selection* aSelection);
  void GetPromotedPoint(RulesEndpoint aWhere, nsIDOMNode* aNode,
                        int32_t aOffset, EditAction actionID,
                        nsCOMPtr<nsIDOMNode>* outNode, int32_t* outOffset);
  void GetPromotedRanges(Selection& aSelection,
                         nsTArray<RefPtr<nsRange>>& outArrayOfRanges,
                         EditAction inOperationType);
  void PromoteRange(nsRange& aRange, EditAction inOperationType);
  enum class TouchContent { no, yes };
  nsresult GetNodesForOperation(nsTArray<RefPtr<nsRange>>& aArrayOfRanges,
                                nsTArray<OwningNonNull<nsINode>>& aOutArrayOfNodes,
                                EditAction aOperationType,
                                TouchContent aTouchContent = TouchContent::yes);
  void GetChildNodesForOperation(nsINode& aNode,
      nsTArray<OwningNonNull<nsINode>>& outArrayOfNodes);
  nsresult GetNodesFromPoint(::DOMPoint aPoint,
                             EditAction aOperation,
                             nsTArray<OwningNonNull<nsINode>>& outArrayOfNodes,
                             TouchContent aTouchContent);
  nsresult GetNodesFromSelection(Selection& aSelection,
                                 EditAction aOperation,
                                 nsTArray<OwningNonNull<nsINode>>& outArrayOfNodes,
                                 TouchContent aTouchContent = TouchContent::yes);
  enum class EntireList { no, yes };
  nsresult GetListActionNodes(nsTArray<OwningNonNull<nsINode>>& aOutArrayOfNodes,
                              EntireList aEntireList,
                              TouchContent aTouchContent = TouchContent::yes);
  void GetDefinitionListItemTypes(Element* aElement, bool* aDT, bool* aDD);
  nsresult GetParagraphFormatNodes(
      nsTArray<OwningNonNull<nsINode>>& outArrayOfNodes,
      TouchContent aTouchContent = TouchContent::yes);
  void LookInsideDivBQandList(nsTArray<OwningNonNull<nsINode>>& aNodeArray);
  nsresult BustUpInlinesAtRangeEndpoints(nsRangeStore &inRange);
  nsresult BustUpInlinesAtBRs(nsIContent& aNode,
                              nsTArray<OwningNonNull<nsINode>>& aOutArrayOfNodes);
  nsIContent* GetHighestInlineParent(nsINode& aNode);
  void MakeTransitionList(nsTArray<OwningNonNull<nsINode>>& aNodeArray,
                          nsTArray<bool>& aTransitionArray);
  nsresult RemoveBlockStyle(nsTArray<OwningNonNull<nsINode>>& aNodeArray);
  nsresult ApplyBlockStyle(nsTArray<OwningNonNull<nsINode>>& aNodeArray,
                           nsIAtom& aBlockTag);
  nsresult MakeBlockquote(nsTArray<OwningNonNull<nsINode>>& aNodeArray);
  nsresult SplitAsNeeded(nsIAtom& aTag, OwningNonNull<nsINode>& inOutParent,
                         int32_t& inOutOffset);
  nsresult SplitAsNeeded(nsIAtom& aTag, nsCOMPtr<nsINode>& inOutParent,
                         int32_t& inOutOffset);
  nsresult AddTerminatingBR(nsIDOMNode *aBlock);
  ::DOMPoint JoinNodesSmart(nsIContent& aNodeLeft, nsIContent& aNodeRight);
  Element* GetTopEnclosingMailCite(nsINode& aNode);
  nsresult PopListItem(nsIDOMNode *aListItem, bool *aOutOfList);
  nsresult RemoveListStructure(Element& aList);
  nsresult CacheInlineStyles(nsIDOMNode *aNode);
  nsresult ReapplyCachedStyles();
  void ClearCachedStyles();
  void AdjustSpecialBreaks();
  nsresult AdjustWhitespace(Selection* aSelection);
  nsresult PinSelectionToNewBlock(Selection* aSelection);
  void CheckInterlinePosition(Selection& aSelection);
  nsresult AdjustSelection(Selection* aSelection,
                           nsIEditor::EDirection aAction);
  nsresult FindNearSelectableNode(nsIDOMNode *aSelNode,
                                  int32_t aSelOffset,
                                  nsIEditor::EDirection &aDirection,
                                  nsCOMPtr<nsIDOMNode> *outSelectableNode);
  /**
   * Returns true if aNode1 or aNode2 or both is the descendant of some type of
   * table element, but their nearest table element ancestors differ.  "Table
   * element" here includes not just <table> but also <td>, <tbody>, <tr>, etc.
   * The nodes count as being their own descendants for this purpose, so a
   * table element is its own nearest table element ancestor.
   */
  bool     InDifferentTableElements(nsIDOMNode* aNode1, nsIDOMNode* aNode2);
  bool     InDifferentTableElements(nsINode* aNode1, nsINode* aNode2);
  nsresult RemoveEmptyNodes();
  nsresult SelectionEndpointInNode(nsINode *aNode, bool *aResult);
  nsresult UpdateDocChangeRange(nsRange* aRange);
  nsresult ConfirmSelectionInBody();
  nsresult InsertMozBRIfNeeded(nsINode& aNode);
  bool     IsEmptyInline(nsINode& aNode);
  bool     ListIsEmptyLine(nsTArray<OwningNonNull<nsINode>>& arrayOfNodes);
  nsresult RemoveAlignment(nsIDOMNode * aNode, const nsAString & aAlignType, bool aChildrenOnly);
  nsresult MakeSureElemStartsOrEndsOnCR(nsIDOMNode *aNode, bool aStarts);
  enum class ContentsOnly { no, yes };
  nsresult AlignBlock(Element& aElement,
                      const nsAString& aAlignType, ContentsOnly aContentsOnly);
  enum class Change { minus, plus };
  nsresult ChangeIndentation(Element& aElement, Change aChange);
  void DocumentModifiedWorker();

// data members
protected:
  nsHTMLEditor           *mHTMLEditor;
  RefPtr<nsRange>       mDocChangeRange;
  bool                    mListenerEnabled;
  bool                    mReturnInEmptyLIKillsList;
  bool                    mDidDeleteSelection;
  bool                    mDidRangedDelete;
  bool                    mRestoreContentEditableCount;
  RefPtr<nsRange>       mUtilRange;
  uint32_t                mJoinOffset;  // need to remember an int across willJoin/didJoin...
  nsCOMPtr<Element>       mNewBlock;
  RefPtr<nsRangeStore>  mRangeItem;
  StyleCache              mCachedStyles[SIZE_STYLE_TABLE];
};

#endif //nsHTMLEditRules_h__

