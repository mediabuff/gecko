/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.widget;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.drawable.Drawable;
import android.graphics.Matrix;
import android.util.AttributeSet;
import android.util.Log;
import android.widget.ImageView;

/* Special version of ImageView for thumbnails. Scales a thumbnail so that it maintains its aspect
 * ratio and so that the images width and height are the same size or greater than the view size
 */
public class ThumbnailView extends ImageView {
    private static final String LOGTAG = "GeckoThumbnailView";
    private Matrix mMatrix = null;
    private int mWidthSpec = -1;
    private int mHeightSpec = -1;

    public ThumbnailView(Context context, AttributeSet attrs) {
        super(context, attrs);
        mMatrix = new Matrix();
    }

    @Override
    public void onDraw(Canvas canvas) {
        Drawable d = getDrawable();
        if (mMatrix == null) {
            int w1 = d.getIntrinsicWidth();
            int h1 = d.getIntrinsicHeight();
            int w2 = getWidth();
            int h2 = getHeight();
    
            float scale = 1.0f;
            if (w2/h2 < w1/h1) {
                scale = (float)h2/h1;
            } else {
                scale = (float)w2/w1;
            }

            mMatrix.reset();
            mMatrix.setScale(scale, scale);
        }

        int saveCount = canvas.save();
        canvas.concat(mMatrix);
        d.draw(canvas);
        canvas.restoreToCount(saveCount);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        // OnLayout.changed isn't a reliable measure of whether or not the size of this view has changed
        // neither is onSizeChanged called often enough. Instead, we track changes in size ourselves, and
        // only invalidate this matrix if we have a new width/height spec
        if (widthMeasureSpec != mWidthSpec || heightMeasureSpec != mHeightSpec) {
            mWidthSpec = widthMeasureSpec;
            mHeightSpec = heightMeasureSpec;
            mMatrix = null;
        }
        super.onMeasure(widthMeasureSpec, heightMeasureSpec);
    }
}
