/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsMathMLmoFrame_h___
#define nsMathMLmoFrame_h___

#include "mozilla/Attributes.h"
#include "nsCOMPtr.h"
#include "nsMathMLTokenFrame.h"

//
// <mo> -- operator, fence, or separator
//

class nsMathMLmoFrame : public nsMathMLTokenFrame {
public:
  NS_DECL_FRAMEARENA_HELPERS

  friend nsIFrame* NS_NewMathMLmoFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);

  virtual eMathMLFrameType GetMathMLFrameType();

  virtual void
  SetAdditionalStyleContext(int32_t          aIndex, 
                            nsStyleContext*  aStyleContext);
  virtual nsStyleContext*
  GetAdditionalStyleContext(int32_t aIndex) const;

  virtual void BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                                const nsRect&           aDirtyRect,
                                const nsDisplayListSet& aLists) MOZ_OVERRIDE;

  NS_IMETHOD
  InheritAutomaticData(nsIFrame* aParent) MOZ_OVERRIDE;

  NS_IMETHOD
  TransmitAutomaticData() MOZ_OVERRIDE;

  NS_IMETHOD
  Reflow(nsPresContext*          aPresContext,
         nsHTMLReflowMetrics&     aDesiredSize,
         const nsHTMLReflowState& aReflowState,
         nsReflowStatus&          aStatus) MOZ_OVERRIDE;

  virtual void MarkIntrinsicWidthsDirty() MOZ_OVERRIDE;

  virtual nscoord
  GetIntrinsicWidth(nsRenderingContext *aRenderingContext) MOZ_OVERRIDE;

  NS_IMETHOD
  AttributeChanged(int32_t         aNameSpaceID,
                   nsIAtom*        aAttribute,
                   int32_t         aModType) MOZ_OVERRIDE;

  // This method is called by the parent frame to ask <mo> 
  // to stretch itself.
  NS_IMETHOD
  Stretch(nsRenderingContext& aRenderingContext,
          nsStretchDirection   aStretchDirection,
          nsBoundingMetrics&   aContainerSize,
          nsHTMLReflowMetrics& aDesiredStretchSize) MOZ_OVERRIDE;

protected:
  nsMathMLmoFrame(nsStyleContext* aContext) : nsMathMLTokenFrame(aContext) {}
  virtual ~nsMathMLmoFrame();
  
  nsMathMLChar     mMathMLChar; // Here is the MathMLChar that will deal with the operator.
  nsOperatorFlags  mFlags;
  float            mMinSize;
  float            mMaxSize;

  bool UseMathMLChar();

  // overload the base method so that we can setup our nsMathMLChar
  virtual void ProcessTextData() MOZ_OVERRIDE;

  // helper to get our 'form' and lookup in the Operator Dictionary to fetch 
  // our default data that may come from there, and to complete the setup
  // using attributes that we may have
  void
  ProcessOperatorData();

  // helper to double check thar our char should be rendered as a selected char
  bool
  IsFrameInSelection(nsIFrame* aFrame);
};

#endif /* nsMathMLmoFrame_h___ */
