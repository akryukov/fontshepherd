/* Copyright (C) 2022 by Alexey Kryukov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE. */

#include "editors/glyphview.h"

// Implementation is based on the following post by KernelCoder:
// https://kernelcoder.wordpress.com/2010/08/25/how-to-insert-ruler-scale-type-widget-into-a-qabstractscrollarea-type-widget/

QDRuler::QDRuler (QDRuler::RulerType rulerType, QWidget* parent) :
    QWidget (parent),
    mRulerType(rulerType),
    mOrigin (0.),
    mRulerUnit (1.),
    mRulerZoom (1.),
    mMouseTracking (false),
    mDrawText (false)
{
    setMouseTracking (true);
    QFont txtFont ("Arial", 5);
    txtFont.setStyleHint (QFont::SansSerif);
    setFont (txtFont);
}

QSize QDRuler::minimumSizeHint () const {
    return QSize (RULER_BREADTH, RULER_BREADTH);
}

QDRuler::RulerType QDRuler::rulerType () const {
    return mRulerType;
}

qreal QDRuler::origin () const {
    return mOrigin;
}

qreal QDRuler::rulerUnit() const {
    return mRulerUnit;
}

qreal QDRuler::rulerZoom() const {
    return mRulerZoom;
}

void QDRuler::setOrigin (const qreal origin) {
    if (mOrigin != origin) {
        mOrigin = origin;
        update ();
    }
}

void QDRuler::setRulerUnit (const qreal rulerUnit) {
    if (mRulerUnit != rulerUnit) {
        mRulerUnit = rulerUnit;
        update ();
    }
}

void QDRuler::setRulerZoom (const qreal rulerZoom) {
    if (mRulerZoom != rulerZoom) {
        mRulerZoom = rulerZoom;
        update ();
    }
}


void QDRuler::setCursorPos (const QPoint cursorPos) {
    mCursorPos = this->mapFromGlobal (cursorPos);
    mCursorPos += QPoint (RULER_BREADTH, RULER_BREADTH);
    update ();
}

void QDRuler::setMouseTrack (const bool track) {
    if (mMouseTracking != track) {
        mMouseTracking = track;
        update ();
    }
}

void QDRuler::mouseMoveEvent (QMouseEvent* event) {
    mCursorPos = event->pos ();
    update ();
    QWidget::mouseMoveEvent (event);
}

void QDRuler::paintEvent (QPaintEvent*) {
    QPainter painter (this);
    painter.setRenderHints (QPainter::TextAntialiasing | QPainter::HighQualityAntialiasing);
    QPen pen (Qt::black, 0); // zero width pen is cosmetic pen
    //pen.setCosmetic (true);
    painter.setPen (pen);
    // We want to work with floating point, so we are considering
    // the rect as QRectF
    QRectF rulerRect = this->rect ();

    // at first fill the rect
    painter.fillRect (rulerRect, QColor (236, 233, 216));

    // drawing a scale of 50
    drawAScaleMeter (&painter, rulerRect, 50, (Horizontal == mRulerType ? rulerRect.height ()
          : rulerRect.width())/2);
    // drawing a scale of 100
    mDrawText = true;
    drawAScaleMeter (&painter, rulerRect, 100, 0);
    mDrawText = false;

    // drawing the current mouse position indicator
    painter.setOpacity (0.4);
    drawMousePosTick (&painter);
    painter.setOpacity (1.0);

    // drawing no man's land between the ruler & view
    QPointF starPt = Horizontal == mRulerType ? rulerRect.bottomLeft ()
        : rulerRect.topRight();
    QPointF endPt = Horizontal == mRulerType ? rulerRect.bottomRight ()
        : rulerRect.bottomRight();
    painter.setPen (QPen (Qt::black, 2));
    painter.drawLine (starPt, endPt);
}

void QDRuler::drawAScaleMeter (QPainter* painter, QRectF rulerRect, qreal scaleMeter, qreal startPosition) {
    // Flagging whether we are horizontal or vertical only to reduce
    // to cheching many times
    bool isHorzRuler = Horizontal == mRulerType;

    qreal visualScale = scaleMeter * mRulerZoom;
    qreal logicalScale = scaleMeter * mRulerUnit;

    // Ruler rectangle starting mark
    qreal rulerStartMark = isHorzRuler ? rulerRect.left () : rulerRect.bottom ();
    // Ruler rectangle ending mark
    qreal rulerEndMark = isHorzRuler ? rulerRect.right () : rulerRect.top ();

    // Condition A # If origin point is between the start & end mard,
    //we have to draw both from origin to left mark & origin to right mark.
    // Condition B # If origin point is left of the start mark, we have to draw
    // from origin to end mark.
    // Condition C # If origin point is right of the end mark, we have to draw
    // from origin to start mark.
    if (mOrigin >= rulerStartMark && mOrigin <= rulerEndMark) {
        drawFromOriginTo (painter, rulerRect, mOrigin, rulerEndMark, 0, visualScale, logicalScale, startPosition);
        drawFromOriginTo (painter, rulerRect, mOrigin, rulerStartMark, 0, -visualScale, -logicalScale, startPosition);
    } else if (mOrigin < rulerStartMark) {
        int tickNo = int ((rulerStartMark - mOrigin) / visualScale);
        drawFromOriginTo (painter, rulerRect, mOrigin + visualScale * tickNo,
            rulerEndMark, tickNo, visualScale, logicalScale, startPosition);
    } else if (mOrigin > rulerEndMark) {
        int tickNo = int ((mOrigin - rulerEndMark) / visualScale);
        drawFromOriginTo (painter, rulerRect, mOrigin - visualScale * tickNo,
            rulerStartMark, tickNo, -visualScale, -logicalScale, startPosition);
    }
}

void QDRuler::drawFromOriginTo (QPainter* painter, QRectF rulerRect, qreal startMark, qreal endMark,
    int startTickNo, qreal vstep, qreal lstep, qreal startPosition)
{
    bool isHorzRuler = Horizontal == mRulerType;
    int iterate = 0;

    for (qreal current = startMark;
        (vstep < 0 ? current >= endMark : current <= endMark); current += vstep) {
        qreal x1 = isHorzRuler ? current : rulerRect.left() + startPosition;
        qreal y1 = isHorzRuler ? rulerRect.top() + startPosition : current;
        qreal x2 = isHorzRuler ? current : rulerRect.right();
        qreal y2 = isHorzRuler ? rulerRect.bottom() : current;
        painter->drawLine (QLineF (x1, y1, x2, y2));
        if (mDrawText) {
            painter->drawText (x1 + 1, y1 + (isHorzRuler ? 12 : -2),
                QString::number (int (lstep) * startTickNo++));
            iterate++;
        }
    }
}

void QDRuler::drawMousePosTick (QPainter* painter) {
    if (mMouseTracking) {
        QPoint starPt = mCursorPos;
        QPoint endPt;
        if (Horizontal == mRulerType) {
            starPt.setY (this->rect ().top ());
            endPt.setX (starPt.x ());
            endPt.setY (this->rect ().bottom ());
        } else {
            starPt.setX (this->rect ().left ());
            endPt.setX (this->rect ().right ());
            endPt.setY (starPt.y ());
        }
        painter->drawLine (starPt, endPt);
    }
}
