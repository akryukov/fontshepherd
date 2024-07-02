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

#include <stdint.h>
#include <set>
#include <deque>
#include <QtWidgets>

#ifndef _FS_ENUM_POINTTYPE_COLOR_DEFINED
#define _FS_ENUM_POINTTYPE_COLOR_DEFINED
enum pointtype { pt_curve, pt_corner, pt_tangent };
#endif

#ifndef _FONSHEPHERD_GLYPHCONTEXT_H
#define _FONSHEPHERD_GLYPHCONTEXT_H

class GlyphBox;
class GlyphNameProvider;

class GlyphScene;
struct svg_state;
typedef svg_state SvgState;
struct cpal_palette;
struct Gradient;
class DrawableFigure;
class AdvanceWidthItem;
class RefItem;
class Conic;
class OnCurvePointItem;
class NonExclusiveUndoGroup;

enum class OutlinesType;

namespace GlyphGraphicItems {
    enum GlyphGraphicItems {
	DummyTop,
	FigurePath,
	FigureEllipse,
	FigureRect,
	Ref,
	ConicPoint,
	OffCurvePoint,
	OnCurvePoint,
	Manipulator,
	AdvanceWidth,
    };
};

class GlyphContext {
    Q_DECLARE_TR_FUNCTIONS (GlyphContext)

public:
    GlyphContext (uint16_t gid, GlyphNameProvider &gnp, std::deque<GlyphContext> &glyphs);
    GlyphContext (GlyphContext && other) = default;
    ~GlyphContext ();

    void setGlyph (OutlinesType gtype, ConicGlyph *g);
    void clearSvgGlyph ();
    bool hasOutlinesType (OutlinesType gtype);
    void switchOutlinesType (OutlinesType gtype, bool gv=false);
    void setFontViewSize (uint16_t size);
    ConicGlyph* glyph (OutlinesType gtype);
    int gid ();
    QString name ();
    void setName (const std::string &name);
    void providePalette (cpal_palette *palette);

    void render (OutlinesType gtype, uint16_t size);
    void render (OutlinesType gtype);
    void render ();
    void renderNoGlyph (uint16_t size);

    QPixmap& pixmap ();
    NonExclusiveUndoGroup* undoGroup (bool gv=false);
    void addCell (GlyphBox *gb);
    bool resolveRefs (OutlinesType gtype);
    void update (OutlinesType gtype);
    DrawableFigure* activeFigure () const;
    QGraphicsItem* topItem () const;

    void appendScene (GlyphScene *scene);
    void deleteScene ();
    void clearScene ();
    GlyphScene *scene () const;

    void checkSelected ();
    void updateSelectedPoints ();
    bool clearSelected (bool merge);
    void selectPointContour (ConicPointItem *ptItem);
    bool joinSplines (bool selected=false, double fudge=0.0);
    bool unlinkSelectedRefs ();
    void setAdvanceWidth (int pos);

    bool removeFigure (int pos);
    bool reorderFigures (int pos1, int pos2);
    void addEllipse (const QRectF &rect);
    void addRect (const QRectF &rect);

    void drawGlyph (ConicGlyph *gref, std::map<std::string, Gradient> &gradients, RefItem *group=nullptr);
    void updatePoints ();
    void updateFill ();

    uint16_t numSelectedPoints ();
    OnCurvePointItem* addPoint (QPointF &pos, enum pointtype ptype);
    bool cutSplines (const QPointF &lstart, const QPointF &lend);
    Conic* pointNearSpline (QPointF &pos, double *tptr);

    void setGlyphChanged (bool val);

    AdvanceWidthItem *awItem;

    void addDependent (uint16_t gid);
    void removeDependent (uint16_t gid);

    static QBrush figureBrush (const SvgState &state, cpal_palette *pal, std::map<std::string, Gradient> &gradients, bool fill=true);

private:
    void renderGlyph (ConicGlyph *gref, QTransform trans, SvgState &state, std::map<std::string, Gradient> &gradients, QPainter &painter);
    void updatePointNumbers ();
    void updateControlPoints ();
    void updateCleanupPoints ();
    void colorizeFigure (QGraphicsItem *item, SvgState state);

    ConicGlyph *m_tt_glyph, *m_ps_glyph, *m_svg_glyph, *m_colr_glyph;
    GlyphNameProvider &m_gnp;
    OutlinesType m_fv_type;
    uint16_t m_fv_size;
    int m_gid;
    cpal_palette *m_palette;
    std::deque<GlyphContext> &m_glyphSet;
    QString m_name;
    QPixmap m_pixmap;
    std::unique_ptr<NonExclusiveUndoGroup> m_fvUndoGroup, m_gvUndoGroup;
    std::vector<GlyphBox *> m_cells;
    std::set<uint16_t> m_dependent;
    GlyphScene *m_scene;
    QGraphicsItem *m_topItem;
};

class DummyTopItem : public QAbstractGraphicsShapeItem {
    QPainterPath shape () const;
    QRectF boundingRect () const;
    void paint (QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget = 0);
    enum { Type = UserType + GlyphGraphicItems::DummyTop };
    int type () const override;
};
Q_DECLARE_METATYPE (DummyTopItem *);

class ConicPoint;

class OffCurvePointItem : public QAbstractGraphicsShapeItem {
    friend class ConicPointItem;

public:
    OffCurvePointItem (ConicPoint &pt, DrawableFigure &fig, QGraphicsItem *parent, bool is_next, bool is_ref);
    QPainterPath shape () const;
    QRectF boundingRect () const;
    void paint (QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget = 0);
    enum { Type = UserType + GlyphGraphicItems::OffCurvePoint };
    int type () const;
    QVariant itemChange (GraphicsItemChange change, const QVariant &value);

    bool isNextCP () const;

private:
    bool baseSelected () const;

    ConicPoint &m_point;
    DrawableFigure &m_fig;
    bool m_next;
    bool m_isRef;
    QColor m_color;
};

class OnCurvePointItem : public QAbstractGraphicsShapeItem {
    friend class ConicPointItem;

public:
    OnCurvePointItem (ConicPoint &pt, DrawableFigure &fig, QGraphicsItem *parent, bool is_ref);
    QPainterPath shape () const;
    QRectF boundingRect () const;
    void paint (QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget = 0);
    enum { Type = UserType + GlyphGraphicItems::OnCurvePoint };
    int type () const;
    QVariant itemChange (GraphicsItemChange change, const QVariant &value);

    void setPointType (enum pointtype ptype);

private:
    ConicPoint &m_point;
    DrawableFigure &m_fig;
    bool m_isRef;
};

// A dummy item, which draws nothing by itself, but owns items responsible
// for displaying an oncurve point and its control points. Need this basically
// because I can't attach control points to the item used to visualize the base
// point with its `ItemIgnoresTransformations' coordinate system (this would
// distort everything).
// The calculations needed to adjust point positions are also done here
class ConicPointItem : public QAbstractGraphicsShapeItem {
    friend class GlyphContext;

public:
    ConicPointItem (ConicPoint &pt, DrawableFigure &fig, QGraphicsItem *parent, RefItem *ref);
    ~ConicPointItem ();

    QRectF boundingRect () const;
    void paint (QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget = 0);
    enum { Type = UserType + GlyphGraphicItems::ConicPoint };
    int type () const;

    void makeNextCP (int pcnt_shift);
    void makePrevCP ();

    void basePointMoved (QPointF newpos);
    void controlPointMoved (QPointF newpos, bool is_next);
    bool isConicPointSelected () const;
    void prepareGeometryChange ();

    ConicPoint *conicPoint ();
    int ttfindex ();
    int nextcpindex ();
    void updatePointNumbers ();
    void updateControlPoints ();

    bool valid () const;
    void setValid (bool val);

private:
    bool m_valid;
    ConicPoint &m_point;
    DrawableFigure &m_fig;
    bool m_isRef;
    OnCurvePointItem *m_baseItem;
    OffCurvePointItem *m_nextItem, *m_prevItem;
    QGraphicsLineItem *m_prevHandle, *m_nextHandle;
    QGraphicsSimpleTextItem *m_baseNumItem, *m_nextNumItem;
};

class FigureItem {
public:
    virtual DrawableFigure &svgFigure () const = 0;
    virtual void moved (QPointF shift) = 0;
};

class ManipulatorItem : public QAbstractGraphicsShapeItem {
    friend class FigureEllipseItem;

public:
    ManipulatorItem (QPointF pos, Qt::Orientations constr, QGraphicsItem *parent=nullptr);
    QPainterPath shape () const;
    QRectF boundingRect () const;
    void paint (QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget = 0);
    enum { Type = UserType + GlyphGraphicItems::Manipulator };
    int type () const;
    QVariant itemChange (GraphicsItemChange change, const QVariant &value);

    void setDirection (Qt::Orientations flags);
    Qt::Orientations direction () const;
    void setEdge (Qt::Edges flags);
    Qt::Edges edge () const;

private:
    Qt::Orientations m_direction;
    Qt::Edges m_edge;
};

class FigurePathItem : public QGraphicsPathItem, public FigureItem {
    friend class GlyphContext;
public:

    FigurePathItem (const QPainterPath &path, DrawableFigure &fig);
    FigurePathItem (DrawableFigure &fig);
    ~FigurePathItem ();

    void paint (QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget = 0);
    enum { Type = UserType + GlyphGraphicItems::FigurePath };
    int type () const override;
    QVariant itemChange (GraphicsItemChange change, const QVariant &value);

    void moved (QPointF shift);
    DrawableFigure &svgFigure () const;

private:
    DrawableFigure &m_fig;
};

class FigureEllipseItem : public QGraphicsEllipseItem, public FigureItem {

public:
    FigureEllipseItem (DrawableFigure &fig);
    ~FigureEllipseItem ();

    void paint (QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget = 0);
    enum { Type = UserType + GlyphGraphicItems::FigureEllipse };
    int type () const override;
    QVariant itemChange (GraphicsItemChange change, const QVariant &value);

    void moved (QPointF shift);
    void manipulatorMoved (QPointF shift, ManipulatorItem *source);
    ManipulatorItem *manipulator (Qt::Edges edge);

    DrawableFigure &svgFigure () const;

private:
    void setManipulators ();

    DrawableFigure &m_fig;
    ManipulatorItem *m_manTopLeft, *m_manLeft, *m_manTop;
};

class FigureRectItem : public QGraphicsRectItem, public FigureItem {

public:
    FigureRectItem (DrawableFigure &fig);
    ~FigureRectItem ();

    void paint (QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget = 0);
    enum { Type = UserType + GlyphGraphicItems::FigureRect };
    int type () const override;
    QVariant itemChange (GraphicsItemChange change, const QVariant &value);

    void moved (QPointF shift);
    void manipulatorMoved (QPointF shift, ManipulatorItem *source);
    ManipulatorItem *manipulator (Qt::Edges edge);

    DrawableFigure &svgFigure () const;

private:
    void setManipulators ();

    DrawableFigure &m_fig;
    ManipulatorItem *m_manTopLeft, *m_manBotRight;
};

class AdvanceWidthItem : public QAbstractGraphicsShapeItem {

public:
    AdvanceWidthItem (qreal pos, QGraphicsItem *parent=nullptr);
    QPainterPath shape () const;
    QRectF boundingRect () const;
    void paint (QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget = 0);
    enum { Type = UserType + GlyphGraphicItems::AdvanceWidth };
    int type () const;

    void hoverEnterEvent (QGraphicsSceneHoverEvent *event);
    void hoverLeaveEvent (QGraphicsSceneHoverEvent *event);
};

class DrawableReference;

class RefItem : public QGraphicsItemGroup {
    friend class GlyphContext;

public:
    RefItem (DrawableReference &ref, const std::string &name, QGraphicsItem *parent=nullptr);
    ~RefItem ();
    void paint (QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
    enum { Type = UserType + GlyphGraphicItems::Ref };
    int type () const;

    const QString &name () const;
    uint16_t idx () const;
    uint16_t gid () const;
    const DrawableReference &ref () const;

    void refMoved (QPointF shift);

    void setFirstPointNumber (int val);
    int firstPointNumber ();

private:
    DrawableReference &m_ref;
    ConicGlyph *m_glyph;
    QString m_name;
    // Used to have an index corresponding to the reference number in
    // the list, but let it be a (unique) random code now
    uint16_t m_idx;
    QGraphicsSimpleTextItem *m_nameItem;
};
#endif
