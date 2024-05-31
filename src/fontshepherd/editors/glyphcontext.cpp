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

#include <cstdio>
#include <sstream>
#include <iostream>
#include <assert.h>
#include <stdint.h>
#include <limits>
#include <QtSvg>

#include "sfnt.h"
#include "fontview.h"
// also includes splineglyph.h
#include "tables/colr.h"
#include "glyphview.h"
#include "glyphcontext.h"

#include "fs_notify.h"
#include "fs_math.h"
#include "fs_undo.h"

GlyphContext::GlyphContext (uint16_t gid, GlyphNameProvider &gnp, std::deque<GlyphContext> &glyphs) :
    awItem (nullptr),
    m_gnp (gnp),
    m_fv_type (OutlinesType::NONE),
    m_gid (gid),
    m_palette (nullptr),
    m_glyphSet (glyphs),
    m_scene (nullptr) {

    m_fv_size = 72;
    m_name = QString::fromStdString (m_gnp.nameByGid (m_gid));
    m_pixmap = QPixmap ();
    m_tt_glyph = m_ps_glyph = m_svg_glyph = m_colr_glyph = nullptr;

    m_fvUndoGroup = std::unique_ptr<NonExclusiveUndoGroup> (new NonExclusiveUndoGroup ());
    m_gvUndoGroup = std::unique_ptr<NonExclusiveUndoGroup> (new NonExclusiveUndoGroup ());
}

GlyphContext::~GlyphContext () {
    // No need to delete underlying glyph objects, as they reside in a pool owned by FontView
    deleteScene ();
}

void GlyphContext::setGlyph (OutlinesType gtype, ConicGlyph *g) {
    if (m_gid == -1 && g)
        m_gid = g->gid ();
    switch (gtype) {
      case OutlinesType::TT:
        m_tt_glyph = g;
	break;
      case OutlinesType::PS:
        m_ps_glyph = g;
	break;
      case OutlinesType::SVG:
        m_svg_glyph = g;
	break;
      case OutlinesType::COLR:
        m_colr_glyph = g;
	break;
      default:
	;
    }
    if (g) {
	m_fvUndoGroup->addStack (g->undoStack ());
	m_gvUndoGroup->addStack (g->undoStack ());
    }
}

void GlyphContext::clearSvgGlyph () {
    if (m_svg_glyph) {
	m_fvUndoGroup->removeStack (m_svg_glyph->undoStack ());
	m_gvUndoGroup->removeStack (m_svg_glyph->undoStack ());
	m_svg_glyph = nullptr;
    }
    if (m_fv_type == OutlinesType::SVG)
	render (m_fv_type, m_fv_size);
}

bool GlyphContext::hasOutlinesType (OutlinesType gtype) {
    switch (gtype) {
      case OutlinesType::TT:
        return (m_tt_glyph != nullptr);
      case OutlinesType::PS:
        return (m_ps_glyph != nullptr);
      case OutlinesType::SVG:
        return (m_svg_glyph != nullptr);
      case OutlinesType::COLR:
        return (m_colr_glyph != nullptr);
      default:
        return false;
    }
}

void GlyphContext::switchOutlinesType (OutlinesType gtype, bool gv) {
    NonExclusiveUndoGroup *ug = gv ? m_gvUndoGroup.get () : m_fvUndoGroup.get ();
    if (!gv) m_fv_type = gtype;
    m_pixmap = QPixmap ();

    if (glyph (gtype))
	ug->setActiveStack (glyph (gtype)->undoStack ());
    else
	ug->setActiveStack (nullptr);
}

void GlyphContext::setFontViewSize (uint16_t size) {
    m_fv_size = size;
}

ConicGlyph* GlyphContext::glyph (OutlinesType gtype) {
    switch (gtype) {
      case OutlinesType::TT:
        return (m_tt_glyph);
      case OutlinesType::PS:
        return (m_ps_glyph);
      case OutlinesType::SVG:
        return (m_svg_glyph);
      case OutlinesType::COLR:
        return (m_colr_glyph);
      default:
        return (nullptr);
    }
}

int GlyphContext::gid () {
    return m_gid;
}

QString GlyphContext::name () {
    return m_name;
}

void GlyphContext::setName (const std::string &name) {
    m_name = QString::fromStdString (name);
}

void GlyphContext::providePalette (cpal_palette *palette) {
    m_palette = palette;
}

QPixmap& GlyphContext::pixmap () {
    if (m_pixmap.isNull ())
	render (m_fv_type, m_fv_size);
    return m_pixmap;
}

NonExclusiveUndoGroup* GlyphContext::undoGroup (bool gv) {
    return (gv ? m_gvUndoGroup.get () : m_fvUndoGroup.get ());
}

void GlyphContext::addCell (GlyphBox *gb) {
    m_cells.push_back (gb);
}

bool GlyphContext::resolveRefs (OutlinesType gtype) {
    static bool seac_warned = false;
    ConicGlyph *g = glyph (gtype);
    if (!g) return false;
    uint16_t cnt=0;

    if (!g->refs.empty ()) {
        for (auto &ref : g->refs) {
	    if (ref.outType == OutlinesType::NONE && hasOutlinesType (OutlinesType::COLR))
		ref.outType = hasOutlinesType (OutlinesType::TT) ?
		    OutlinesType::TT : OutlinesType::PS;
            g->provideRef (m_glyphSet[ref.GID].glyph (ref.outType), cnt++);
	}
        if (g->checkRefs (g->gid (), m_glyphSet.size ()) != 0)
            return false;
	if (gtype == OutlinesType::PS) {
	    if (!seac_warned) {
		FontShepherd::postWarning (
		    tr ("Deprecated CFF operator"),
		    tr ("This font uses SEAC-like endchar operator to build composite glyphs. "
			"This form of endchar is deprecated and should not be used in new fonts. "
			"So I will convert references to contours."),
		nullptr);
		seac_warned = true;
	    }
	    g->unlinkRefs (false);
	    g->hmUpdate (m_gnp.font ());
	    g->checkBounds (g->bb, false);
	    g->setModified (true);
	} else {
	    for (auto &ref : g->refs)
		m_glyphSet[ref.GID].addDependent (g->gid ());
	    g->finalizeRefs ();
	    g->checkBounds (g->bb, false);
	}
    }
    return true;
}

void GlyphContext::update (OutlinesType gtype) {
    uint16_t i;
    for (i=0; i<m_cells.size (); i++)
        m_cells[i]->update ();
    for (uint16_t gid: m_dependent) {
        GlyphContext &depctx = m_glyphSet[gid];
	ConicGlyph *g = depctx.glyph (gtype);
        depctx.render (gtype, m_fv_size);
        depctx.drawGlyph (g, g->gradients);
        depctx.update (gtype);
    }
}

DrawableFigure* GlyphContext::activeFigure () const {
    if (!m_scene)
	return nullptr;
    QGraphicsItem *panel = m_scene->activePanel ();
    FigureItem *ctrItem = dynamic_cast<FigureItem *> (panel);
    DrawableFigure *ret = ctrItem ? &ctrItem->svgFigure () : nullptr;
    return ret;
}

QGraphicsItem* GlyphContext::topItem () const {
    return m_topItem;
}

void GlyphContext::appendScene (GlyphScene *scene) {
    m_scene = scene;
    m_topItem = new DummyTopItem ();
    m_topItem->setFlag (QGraphicsItem::ItemHasNoContents);
    m_scene->setRootItem (m_topItem);
}

void GlyphContext::deleteScene () {
    if (m_scene)
        delete m_scene;
    m_scene = nullptr;
}

static void drawPath (DrawableFigure &fig, QPainterPath &path) {
    std::vector<ConicPointList> &conics = fig.contours;
    uint16_t i;
    Conic *sp, *first;

    path.setFillRule (Qt::WindingFill);
    for (i=0; i<conics.size (); i++) {
        ConicPointList &spl = conics[i];
        // Ignore single point paths
        if (spl.first->next && spl.first->next->to != spl.first) {
            path.moveTo (spl.first->me.x, spl.first->me.y);
            first = nullptr;
            for (sp = spl.first->next; sp && sp!=first; sp = sp->to->next) {
                if (!first) first=sp;
                if (sp->islinear)
                    path.lineTo (sp->to->me.x, sp->to->me.y);
                else if (sp->order2)
                    path.quadTo (
                        QPointF (sp->from->nextcp.x, sp->from->nextcp.y),
                        QPointF (sp->to->me.x, sp->to->me.y)
                    );
                else
                    path.cubicTo (
                        QPointF (sp->from->nextcp.x, sp->from->nextcp.y),
                        QPointF (sp->to->prevcp.x, sp->to->prevcp.y),
                        QPointF (sp->to->me.x, sp->to->me.y)
                    );
            }
        }
    }
}

static void drawPoints (DrawableFigure &fig, QGraphicsPathItem *path, bool is_ref) {
    std::vector<ConicPointList> &conics = fig.contours;

    for (auto &spls: conics) {
        ConicPoint *sp = spls.first;
        do {
            if (!sp->item) {
		auto item = new ConicPointItem (*sp, fig, path, is_ref);
		item->setSelected (sp->selected);
	    }
            sp = (sp->next) ? sp->next->to : nullptr;
        } while (sp && sp != spls.first);
    }
}

QBrush GlyphContext::figureBrush (const SvgState &state, cpal_palette *pal, std::map<std::string, Gradient> &gradients, bool fill) {
    QBrush ret = QBrush ();
    const std::string &source_id = fill ? state.fill_source_id : state.stroke_source_id;
    const bool color_set = fill ? state.fill_set : state.stroke_set;
    const uint16_t color_idx = fill ? state.fill_idx : state.stroke_idx;

    if (source_id.empty ()) {
	auto &rgba = color_set && pal && color_idx < 0xFFFF ?
	    pal->color_records[color_idx] :
	    fill ? state.fill : state.stroke;
        ret.setStyle (Qt::SolidPattern);
        ret.setColor (QColor (
            rgba.red, rgba.green, rgba.blue, rgba.alpha));
    } else if (gradients.count (source_id)) {
        QGradient qgrad;
        Gradient &grad = gradients[source_id];
        QGradientStops stops;

	// CoordinateMode actually seems to have no significant effect, as
	// we have to specify such parameters as Start/FinalStop/Center
	// in logical object coordinates anyway. How odd...
	qgrad.setCoordinateMode (QGradient::ObjectMode);
        if (grad.type == GradientType::LINEAR) {
            qgrad = QLinearGradient ();
            QLinearGradient &lg = static_cast<QLinearGradient &> (qgrad);
	    double x1, x2, y1, y2;
	    if (grad.units == GradientUnits::userSpaceOnUse) {
		x1 = grad.props["x1"] - grad.bbox.minx;
		x2 = grad.props["x2"] - grad.bbox.minx;
		y1 = grad.bbox.maxy - grad.props["y1"];
		y2 = grad.bbox.maxy - grad.props["y2"];
	    } else {
		x1 = grad.props["x1"] * (grad.bbox.maxx - grad.bbox.minx);
		x2 = grad.props["x2"] * (grad.bbox.maxx - grad.bbox.minx);
		y2 = grad.props["y1"] * (grad.bbox.miny - grad.bbox.maxy);
		y1 = grad.props["y2"] * (grad.bbox.miny - grad.bbox.maxy);
	    }
            lg.setStart (x1, y1);
            lg.setFinalStop (x2, y2);
        } else if (grad.type == GradientType::RADIAL) {
            qgrad = QRadialGradient ();
            QRadialGradient &rg = static_cast<QRadialGradient &> (qgrad);
	    double cx, cy, fx, fy;
	    if (grad.units == GradientUnits::userSpaceOnUse) {
		cx = grad.props["cx"] - grad.bbox.minx;
		cy = grad.bbox.maxy - grad.props["cy"];
	    } else {
		cx = grad.props["cx"] * (grad.bbox.maxx - grad.bbox.minx);
		cy = grad.props["cy"] * (grad.bbox.miny - grad.bbox.maxy);
	    }
            rg.setCenter (cx, cy);
	    if (grad.props.count ("fx")) {
		if (grad.units == GradientUnits::userSpaceOnUse) {
		    fx = grad.props["fx"] - grad.bbox.minx;
		    fy = grad.bbox.maxy - grad.props["fy"];
		} else {
		    fx = grad.props["fx"] * (grad.bbox.maxx - grad.bbox.minx);
		    fy = grad.props["fy"] * (grad.bbox.miny - grad.bbox.maxy);
		}
		rg.setFocalPoint (fx, fy);
	    }
            rg.setRadius (grad.props["r"]);
        }
        qgrad.setSpread (
            grad.sm == GradientExtend::EXTEND_PAD ? QGradient::PadSpread :
		grad.sm == GradientExtend::EXTEND_REFLECT ?
		QGradient::RepeatSpread : QGradient::ReflectSpread
	);
        for (auto &st : grad.stops) {
            QGradientStop qst = QPair<double, QColor> (st.offset, QColor (
                st.color.red, st.color.green, st.color.blue, st.color.alpha));
            stops.append (qst);
        }
        qgrad.setStops (stops);
        ret = QBrush (qgrad);
    }
    return ret;
}

static QPen figurePenProps (SvgState &state, int w) {
    QPen pen = QPen ();
    if (state.stroke_set) {
        pen.setColor (QColor (
            state.stroke.red, state.stroke.green, state.stroke.blue, state.stroke.alpha));
        pen.setWidth (state.stroke_width);
        if (state.linecap != lc_inherit)
            pen.setCapStyle (
                state.linecap == lc_butt ? Qt::FlatCap :
                state.linecap == lc_round ? Qt::RoundCap : Qt::SquareCap);
        if (state.linejoin != lj_inherit)
            pen.setJoinStyle (
                state.linejoin == lj_miter ? Qt::MiterJoin :
                state.linejoin == lj_round ? Qt::RoundJoin : Qt::BevelJoin);
    } else if (w!=0)
        pen.setWidth (w);
    return pen;
}

void GlyphContext::clearScene () {
    if (!m_scene)
        return;
    QList<QGraphicsItem *> ilist = m_topItem->childItems ();

    for (int i=0; i<ilist.size (); i++) {
        QGraphicsItem *item = ilist[i];
        m_scene->removeItem (item);
        delete item;
    }
}

GlyphScene* GlyphContext::scene () const {
    return m_scene;
}

void GlyphContext::drawGlyph (ConicGlyph *gref, std::map<std::string, Gradient> &gradients, RefItem *group) {
    QGraphicsPathItem *item;
    if (!m_scene)
        return;
    SvgState gstate = group ? group->ref ().svgState : SvgState ();

    // If group is specified, then the function has been called
    // recursively. Otherwise clear the scene before drawing a glyph.
    if (!group)
	clearScene ();

    for (auto &fig : gref->figures) {
	ElementType ftype = fig.elementType ();
	SvgState figstate = gstate + fig.svgState;
        if (ftype == ElementType::Circle || ftype == ElementType::Ellipse || ftype == ElementType::Rect) {
	    QAbstractGraphicsShapeItem *item;
	    if (ftype == ElementType::Rect)
		item = new FigureRectItem (fig);
	    else
		item = new FigureEllipseItem (fig);
            if (!group) {
		item->setParentItem (m_topItem);
		m_scene->setActivePanel (item);
            } else {
                group->addToGroup (item);
	    }
        } else if (/*fig.type.compare ("path") == 0*/ !fig.contours.empty ()) {
            QPainterPath path;
            drawPath (fig, path);
	    item = new FigurePathItem (path, fig);
            if (!group) {
		item->setParentItem (m_topItem);
		m_scene->setActivePanel (item);
            } else {
                group->addToGroup (item);
	    }
            QPen pen = figurePenProps (figstate, 0);
            item->setPen (pen);
        }
        if (figstate.fill_set && GlyphViewContainer::showFill ()) {
            QBrush brush = figureBrush (figstate, m_palette, gradients);
            item->setBrush (brush);
        } else if (GlyphViewContainer::showFill ())
            item->setBrush (QColor (0x80, 0x70, 0x70, 0x70));
	// Will have no effect for ellipses and rects, where there is no contours
	drawPoints (fig, item, group != nullptr);
    }
    if (!gref->figures.empty () && !group)
	updatePoints ();
    for (size_t i=0; i<gref->refs.size (); i++) {
        DrawableReference &ref = gref->refs[i];
        RefItem *g = new RefItem (ref, i, m_gnp.nameByGid (ref.GID));
        QTransform reftrans (
            ref.transform[0], ref.transform[1], ref.transform[2],
            ref.transform[3], ref.transform[4], ref.transform[5]
        );
        drawGlyph (ref.cc, gref->gradients, g);

        if (group) {
            for (QGraphicsItem *item: g->childItems ()) {
                if (!(item->flags () & QGraphicsItem::ItemHasNoContents)) {
                    item->setTransform (reftrans);
                    group->addToGroup (item);
                }
            }
            delete g;
        } else {
	    g->setParentItem (m_topItem);
            g->setTransform (reftrans);
        }
    }

    if (gref->gid () == gid ()) {
        awItem = new AdvanceWidthItem (gref->advanceWidth ());
	awItem->setParentItem (m_topItem);
    }
    if (!group)
	m_scene->notifyGlyphRedrawn ();
}

void GlyphContext::updatePoints () {
    QList<QGraphicsItem *> items = m_scene->items ();

    for (int i=0; i<items.size (); i++) {
        if (items[i]->type () == ConicPointItem::Type) {
            ConicPointItem* item = qgraphicsitem_cast<ConicPointItem*> (items[i]);
            item->prepareGeometryChange ();
        }
    }
}

void GlyphContext::colorizeFigure (QGraphicsItem *item, SvgState state) {
    ConicGlyph *scene_glyph = glyph (m_scene->outlinesType ());
    FigureItem *figItem = dynamic_cast<FigureItem *> (item);
    if (figItem) {
	DrawableFigure &fig = figItem->svgFigure ();
	SvgState newstate = state + fig.svgState;
	QAbstractGraphicsShapeItem *shape =
	    qgraphicsitem_cast<QAbstractGraphicsShapeItem  *> (item);
	QPen pen = figurePenProps (newstate, 0);
	shape->setPen (pen);
	if (newstate.fill_set && GlyphViewContainer::showFill ()) {
	    QBrush brush = figureBrush (newstate, m_palette, scene_glyph->gradients);
	    shape->setBrush (brush);
	} else if (GlyphViewContainer::showFill ())
	    shape->setBrush (QColor (0x80, 0x70, 0x70, 0x70));
	else
	    shape->setBrush (QBrush ());
    }
}

void GlyphContext::updateFill () {
    QList<QGraphicsItem *> items = m_scene->items ();
    ConicGlyph *scene_glyph = glyph (m_scene->outlinesType ());
    if (!scene_glyph) return;

    for (QGraphicsItem *item : m_topItem->childItems ()) {
	if (item->type () == RefItem::Type) {
	    for (QGraphicsItem *child : item->childItems ()) {
		if (child->isPanel ())
		    colorizeFigure (child, SvgState ());
	    }
	} else if (item->isPanel ()) {
	    colorizeFigure (item, SvgState ());
	}
    }
}

void GlyphContext::renderGlyph
    (ConicGlyph *gref, QTransform trans, SvgState &state, std::map<std::string, Gradient> &gradients, QPainter &painter) {
    for (auto &fig : gref->figures) {
	SvgState newstate = state + fig.svgState;
        QPen pen = figurePenProps (newstate, 1);
        QBrush brush = QBrush (QColor (Qt::black));

	if (newstate.fill_set)
            brush = figureBrush (newstate, m_palette, gradients);

	ElementType ftype = fig.elementType ();
        if (ftype == ElementType::Circle || ftype == ElementType::Ellipse) {
            painter.setPen (pen);
            painter.setBrush (brush);
	    double x, y;
	    trans.map (fig.props["cx"], fig.props["cy"], &x, &y);
            painter.drawEllipse (QPointF (x, y), fig.props["rx"], fig.props["ry"]);
	} else if (ftype == ElementType::Rect) {
            painter.setPen (pen);
            painter.setBrush (brush);
	    double x, y;
	    trans.map (fig.props["x"], fig.props["y"], &x, &y);
            painter.drawRect (QRectF (x, y, fig.props["width"], fig.props["height"]));
        } else if (!fig.contours.empty ()) {
            QPainterPath path;
            drawPath (fig, path);
            QPainterPath tpath = trans.map (path);
            painter.setPen (pen);
            painter.setBrush (brush);
            painter.drawPath (tpath);
        }
    }
    for (auto &ref : gref->refs) {
	SvgState newstate = state + ref.svgState;
	// May occasionally get zero GIDs in glyphs generated from the COLR table
	if (ref.GID == 0)
	    continue;
        QTransform reftrans (
            ref.transform[0], ref.transform[1], ref.transform[2],
            ref.transform[3], ref.transform[4], ref.transform[5]
        );
        assert (ref.cc);
        renderGlyph (ref.cc, reftrans*trans, newstate, gref->gradients, painter);
    }
}

void GlyphContext::renderNoGlyph (uint16_t size) {
    m_pixmap = QPixmap (size, size);
    m_pixmap.fill ();
    QPainter p (&m_pixmap);
    p.setPen (Qt::red);
    p.drawLine (0,0, size,size);
    p.drawLine (0,size, size,0);
}

void GlyphContext::render (OutlinesType gtype, uint16_t size) {
    m_fv_size = size;
    QPainter p;
    QImage canvas (size, size, QImage::Format_ARGB32_Premultiplied);
    ConicGlyph *fv_glyph = glyph (m_fv_type);

    // Return if the requested outlines type is not the one displayed in fontview
    if (!fv_glyph || gtype != m_fv_type) {
	renderNoGlyph (size);
        return;
    }

    float scale = ((float) m_fv_size)/(fv_glyph->m_ascent-fv_glyph->m_descent);

    // NB: may draw directly on a pixmap (which probably would take less time),
    // but converting from an image seems to be the only method to replace
    // pixmap data without creating a new QPixmap object. This guarantees
    // any pixmaps displayed in glyph cells are automatically updated
    canvas.fill (Qt::transparent);
    p.begin (&canvas);
    p.scale (scale, -scale);
    p.setRenderHints (QPainter::SmoothPixmapTransform | QPainter::Antialiasing);

    float xshift, yshift;
    ConicGlyph *g = fv_glyph;

    xshift = ((g->m_ascent-g->m_descent) - (g->bb.maxx-g->bb.minx))/2 - g->bb.minx;
    yshift = -g->m_ascent;
    QTransform trans = QTransform (1, 0, 0, 1, xshift, yshift);
    SvgState state;
    renderGlyph (g, trans, state, g->gradients, p);

    p.end ();
    m_pixmap.convertFromImage (canvas);
}

void GlyphContext::render (OutlinesType gtype) {
    render (gtype, m_fv_size);
}

void GlyphContext::render () {
    render (m_fv_type, m_fv_size);
}

void GlyphContext::checkSelected () {
    for (auto *item : m_scene->items ()) {
	switch (item->type ()) {
	  case OnCurvePointItem::Type: {
            ConicPointItem *baseItem = qgraphicsitem_cast<ConicPointItem *> (item->parentItem ());
            baseItem->m_point.selected = item->isSelected ();
	  } break;
	  case RefItem::Type: {
            RefItem *refItem = qgraphicsitem_cast<RefItem *> (item);
            refItem->m_ref.selected = refItem->isSelected ();
	  } break;
	  case FigurePathItem::Type:
	  case FigureEllipseItem::Type: {
            FigureItem *figItem = dynamic_cast<FigureItem *> (item);
	    auto &fig = figItem->svgFigure ();
	    fig.selected = item->isSelected ();
	  }
        }
    }
}

void GlyphContext::updateSelectedPoints () {
    for (auto *item : m_scene->items ()) {
        if (item->type () == OnCurvePointItem::Type) {
            ConicPointItem *baseItem = qgraphicsitem_cast<ConicPointItem *> (item->parentItem ());
	    if (baseItem->m_point.selected)
		item->setSelected (true);
	    else if (item->isSelected ())
		baseItem->m_point.selected = true;
        }
    }
}

void GlyphContext::selectPointContour (ConicPointItem *ptItem) {
    FigurePathItem *pathItem = qgraphicsitem_cast<FigurePathItem*> (ptItem->panel ());
    DrawableFigure &fig = pathItem->svgFigure ();
    ConicPointList *spls = fig.getPointContour (ptItem->conicPoint ());
    spls->selectAll ();
    updateSelectedPoints ();
}

bool GlyphContext::clearSelected (bool merge) {
    QList<QGraphicsItem *> sellist = m_scene->selectedItems ();
    QList<QGraphicsItem *> removable;
    OutlinesType gtype = m_scene->outlinesType ();
    ConicGlyph *scene_glyph = glyph (m_scene->outlinesType ());
    bool changed = false;
    if (!scene_glyph) return false;

    // Get rid of offcurve points: otherwise they may get deleted
    // as we delete the corresponding oncurve point, thus making
    // the pointers invalid.
    // Oh, and if entire contour is selected, don't care points
    // at all, they'll get removed with the contour anyway
    for (QGraphicsItem *item: sellist) {
	QGraphicsItem *panel = item->panel ();
	if (item->type () != OffCurvePointItem::Type &&
	    item->type () != ManipulatorItem::Type &&
	    item->type () != AdvanceWidthItem::Type &&
	    (item->type () != OnCurvePointItem::Type || !panel->isSelected ()))
	    removable << item;
    }

    for (int i=0; i<removable.size (); i++) {
	QGraphicsItem *item = removable[i];
	switch (item->type ()) {
	  case OnCurvePointItem::Type: {
            ConicPointItem *baseItem = qgraphicsitem_cast<ConicPointItem*> (item->parentItem ());
	    if (baseItem->isActive ()) {
		m_scene->removeItem (baseItem);
		delete baseItem;
		changed = true;
	    }
	  } break;
	  case RefItem::Type:
	  case FigurePathItem::Type:
	  case FigureEllipseItem::Type:
	    if (!merge) {
		m_scene->removeItem (item);
		m_scene->notifyPanelRemoved (item);
		delete item;
		changed = true;
	    }
        }
    }

    if (changed) {
	// Update path for the current SVG figure only
	QGraphicsItem *panel = m_scene->activePanel ();
	FigurePathItem *pathItem = qgraphicsitem_cast<FigurePathItem *> (panel);
	if (pathItem) {
	    DrawableFigure &curfig = pathItem->svgFigure ();
	    if (merge)
		curfig.mergeMarked ();
	    else
		curfig.clearMarked ();
	    if (curfig.contours.empty ()) {
		m_scene->notifyPanelRemoved (pathItem);
		m_scene->removeItem (pathItem);
		scene_glyph->removeFigure (curfig);
		delete pathItem;
	    } else {
		QPainterPath path;
		drawPath (curfig, path);
		pathItem->setPath (path);
	    }
	}

	// but update point numbering for all SVG figures
	uint16_t lastpt=0;
	for (auto &fig: scene_glyph->figures)
	    lastpt = fig.renumberPoints (lastpt);
	updateControlPoints ();
	updatePointNumbers ();
    }

    if (changed && !merge) {
	scene_glyph->figures.remove_if (
	    [] (DrawableFigure const &fig) { return fig.selected; }
	);
	for (auto it = scene_glyph->refs.begin (); it != scene_glyph->refs.end (); ) {
	    auto &ref = *it;
	    if (ref.selected) {
		uint16_t gid = ref.GID;
		GlyphContext &depctx = m_glyphSet[gid];
		ConicGlyph *g = depctx.glyph (gtype);
		depctx.removeDependent (scene_glyph->gid ());
		depctx.render (gtype, m_fv_size);
		depctx.drawGlyph (g, g->gradients);
		depctx.update (gtype);
		it = scene_glyph->refs.erase (it);
	    } else
		it++;
	}
    }
    return changed;
}

void GlyphContext::addDependent (uint16_t gid) {
    m_dependent.insert (gid);
}

void GlyphContext::removeDependent (uint16_t gid) {
    m_dependent.erase (gid);
}

uint16_t GlyphContext::numSelectedPoints () {
    QList<QGraphicsItem *> sellist = m_scene->selectedItems ();
    uint16_t ret=0;

    for (QGraphicsItem *item: sellist) {
        if (item->type () == OnCurvePointItem::Type)
            ret++;
    }
    return ret;
}

void GlyphContext::updatePointNumbers () {
    if (m_scene) {
	for (auto *item : m_scene->items ()) {
	    if (item->type () == ConicPointItem::Type) {
		auto *conic_item = qgraphicsitem_cast<ConicPointItem *> (item);
		conic_item->updatePointNumbers ();
	    }
	}
    }
}

void GlyphContext::updateControlPoints () {
    if (m_scene) {
	for (auto *item : m_scene->items ()) {
	    if (item->type () == ConicPointItem::Type) {
		auto *conic_item = qgraphicsitem_cast<ConicPointItem *> (item);
		conic_item->updateControlPoints ();
	    }
	}
    }
}

void GlyphContext::updateCleanupPoints () {
    std::vector<ConicPointItem *> to_delete;
    to_delete.reserve (m_scene->items ().size ());

    if (m_scene) {
	for (auto *item : m_scene->items ()) {
	    if (item->type () == ConicPointItem::Type) {
		auto *conic_item = qgraphicsitem_cast<ConicPointItem *> (item);
		// Check if the underlying point has previously been deleted
		if (!conic_item->valid ())
		    to_delete.push_back (conic_item);
		else
		    conic_item->updateControlPoints ();
	    }
	}
    }
    for (int i = to_delete.size () - 1; i>=0; i--) {
	auto *conic_item = to_delete[i];
        m_scene->removeItem (conic_item);
        delete conic_item;
    }
}

Conic* GlyphContext::pointNearSpline (QPointF &pos, double *tptr) {
    BasePoint testpt = { pos.x (), pos.y () };
    double fudge = 2;

    QGraphicsItem *panel = m_scene->activePanel ();
    FigurePathItem *ctrItem = qgraphicsitem_cast<FigurePathItem *> (panel);
    if (!ctrItem) return nullptr;
    DrawableFigure &fig = ctrItem->svgFigure ();

    for (ConicPointList &spls: fig.contours) {
        Conic *spl, *first=nullptr;
        for (spl = spls.first->next; spl && spl!=first; spl = spl->to->next) {
            if (spl->pointNear (testpt, fudge, tptr))
                return spl;
            if (!first) first=spl;
        }
    }
    return nullptr;
}

void GlyphContext::setGlyphChanged (bool val) {
    ConicGlyph *g = glyph (m_fv_type);
    if (g)
	g->setModified (val);
}

static void finalizeSpline (Conic *spl, bool do_next) {
    ConicPoint *sp1 = do_next ? spl->from : spl->to;
    ConicPoint *sp2 = do_next ? spl->to : spl->from;
    ConicPointItem *item1 = sp1->item, *item2=sp2->item;
    BasePoint cp_pos;

    cp_pos = sp1->defaultCP (do_next, spl->order2);
    item1->controlPointMoved (QPointF (cp_pos.x, cp_pos.y), do_next);
    if (!spl->order2) {
        cp_pos = sp2->defaultCP (!do_next, spl->order2);
        item2->controlPointMoved (QPointF (cp_pos.x, cp_pos.y), !do_next);
    }
    spl->refigure ();
    if (sp1->pointtype == pt_tangent && spl->islinear) {
        cp_pos = sp1->defaultCP (!do_next, spl->order2);
        item1->controlPointMoved (QPointF (cp_pos.x, cp_pos.y), !do_next);
        Conic *constrSpl = do_next ? sp1->prev : sp2->next;
        if (constrSpl) constrSpl->refigure ();
    }
}

OnCurvePointItem* GlyphContext::addPoint (QPointF &pos, enum pointtype ptype) {
    ConicPointItem *selPtItem=nullptr, *actPtItem=nullptr, *retItem;
    OnCurvePointItem *baseItem;
    ConicGlyph *scene_glyph = glyph (m_scene->outlinesType ());
    assert (scene_glyph);

    QGraphicsItem *actItem = m_scene->itemAt (pos, QTransform (1, 0, 0, -1, 0, 0));
    if (actItem && actItem->type () == OnCurvePointItem::Type)
        actPtItem = qgraphicsitem_cast<ConicPointItem *> (actItem->parentItem ());

    QList<QGraphicsItem *> sellist = m_scene->selectedItems ();
    if (sellist.size () == 1 && sellist[0]->type () == OnCurvePointItem::Type)
        selPtItem = qgraphicsitem_cast<ConicPointItem *> (sellist[0]->parentItem ());

    QGraphicsItem *panel = m_scene->activePanel ();
    FigurePathItem *pathItem = qgraphicsitem_cast<FigurePathItem *> (panel);

    if (!panel || !pathItem) {
	scene_glyph->figures.emplace_back ();
	auto &fig = scene_glyph->figures.back ();
	fig.type = "path";
	fig.order2 = (m_scene->outlinesType () == OutlinesType::TT);

        pathItem = new FigurePathItem (fig);
	pathItem->setParentItem (m_topItem);
	m_scene->setActivePanel (pathItem);
	m_scene->notifyPanelAdded (pathItem);
    }

    double t;
    Conic* actSpl = pointNearSpline (pos, &t);

    DrawableFigure &fig = pathItem->svgFigure ();
    QPainterPath path;
    ConicPoint *sp, *selPt=nullptr, *actPt=nullptr;
    ConicPointList *selSpls, *actSpls;

    bool do_next;
    Conic *spl;

    if (selPtItem) {
        selPt = &selPtItem->m_point;
        selSpls = fig.getPointContour (selPt);
    }
    if (actPtItem) {
        actPt = &actPtItem->m_point;
        actSpls = fig.getPointContour (actPt);
    }

    // Close contour by building a spline between the current point and
    // the previously selected one
    if (selPt != actPt &&
	selPt && selSpls && (!selPt->next || !selPt->prev) &&
        actPt && actSpls && (!actPt->next || !actPt->prev)) {

        retItem = actPtItem;
        if (!selPt->next) {
            do_next = true;
            if (!actPt->next && actPt->prev)
                actSpls->reverse ();
            spl = fig.splines_pool.construct (selPt, actPt, fig.order2);
        } else {
            do_next = false;
            if (!actPt->prev && actPt->next)
                actSpls->reverse ();
            spl = fig.splines_pool.construct (actPt, selPt, fig.order2);
        }
        finalizeSpline (spl, do_next);

        // Close contour
	if (selSpls == actSpls) {
            selSpls->first->isfirst = false;
            selSpls->first = selSpls->last = actPt;
            actPt->isfirst = true;
	// Merge two contours
        } else {
            if (selSpls->first == selPt) {
                selSpls->first->isfirst = false;
                selSpls->first = actSpls->first;
            } else
                selSpls->last = actSpls->last;
            fig.deleteContour (actSpls);
        }
    // Continue a countour with a new spline
    } else if (selPt && selSpls && (!selPt->next || !selPt->prev)) {
        sp = fig.points_pool.construct (pos.x (), pos.y ());
        sp->pointtype = ptype;

        if (!selPt->next) {
            spl = fig.splines_pool.construct (selPt, sp, fig.order2);
            selSpls->last = sp;
            do_next = true;
        } else {
            spl = fig.splines_pool.construct (sp, selPt, fig.order2);
            selSpls->first->isfirst = false;
            selSpls->first = sp;
            sp->isfirst = true;
            do_next = false;
        }
        retItem = new ConicPointItem (*sp, fig, pathItem, false);
        finalizeSpline (spl, do_next);
    // Insert a new point into an existing spline
    } else if (actSpl) {
        sp = fig.bisectSpline (actSpl, t);
        sp->pointtype = ptype;
        retItem = new ConicPointItem (*sp, fig, pathItem, false);
    // Just add a new point
    } else {
        sp = fig.points_pool.construct (pos.x (), pos.y ());
        sp->pointtype = ptype;
        retItem = new ConicPointItem (*sp, fig, pathItem, false);
        fig.contours.emplace_back ();
        ConicPointList &spls = fig.contours.back ();
        spls.first = spls.last = sp;
        sp->isfirst = true;
    }

    uint16_t lastpt=0;
    for (auto &figure : scene_glyph->figures)
	lastpt = figure.renumberPoints (lastpt);
    drawPath (fig, path);
    pathItem->setPath (path);
    baseItem = retItem->m_baseItem;
    updatePointNumbers ();
    return baseItem;
}

bool GlyphContext::cutSplines (const QPointF &lstart, const QPointF &lend) {
    ConicPoint a (lstart.x (), lstart.y ());
    ConicPoint b (lend.x (), lend.y ());
    Conic dummy (&a, &b, false);
    std::array<BasePoint, 9> pts;
    std::array<extended_t, 10> t1s, t2s;
    ConicGlyph *scene_glyph = glyph (m_scene->outlinesType ());
    bool fig_changed = false, foundsomething;
    std::vector<Conic *> affected;

    QGraphicsItem *panel = m_scene->activePanel ();
    FigurePathItem *pathItem = qgraphicsitem_cast<FigurePathItem *> (panel);
    if (!pathItem)
	return false;
    DrawableFigure &fig = pathItem->svgFigure ();
    QPainterPath path;

    do {
        foundsomething = false;
        for (size_t splidx=0; splidx < fig.contours.size () && !foundsomething; splidx++) {
	    ConicPointList &spls = fig.contours[splidx];
	    Conic *spl, *first=nullptr;
	    for (spl = spls.first->next; spl && spl!=first && !foundsomething; spl = spl->to->next) {
		if (dummy.intersects (spl, pts, t1s, t2s) > 0) {
		    int i;
		    // Skip points which are positioned near to the cut line and
		    // represent a start or an end point of an open contour (most probably
		    // they are there as a result of a previous spline bisection)
		    for (i=0; i<4 && t2s[i]!=-1 && (
			(t2s[i]<.001 && spl->from->prev==nullptr) ||
			(t2s[i]>.999 && spl->to->next == nullptr)); ++i);
		    if (i<4 && t1s[i]!=-1) {
			foundsomething = true;
			ConicPoint *mid1 = fig.bisectSpline (spl, t2s[i]);
			ConicPoint *mid2 = fig.points_pool.construct (*mid1);
			ConicPointList news;
			bool do_append = false;

			if (spls.first == spls.last) {
			    spls.first->isfirst = false;
			    spls.first = mid2;
			    mid2->isfirst = true;
			    spls.last = mid1;
			} else {
			    news.last = spls.last;
			    spls.last = mid1;
			    news.first = mid2;
			    mid2->isfirst = true;
			    do_append = true;
			}
			Conic *spl2 = mid1->next;
			mid1->next = nullptr;
			mid2->prev = nullptr;
			spl2->from = mid2;
			mid2->next = spl2;
			spls.ensureStart ();

			// NB: reallocating contours will make spls invalid.
			// So do this at the last stage when the reference to the current
			// contour is no longer needed.
			if (do_append)
			    fig.contours.push_back (news);

			new ConicPointItem (*mid1, fig, pathItem, false);
			new ConicPointItem (*mid2, fig, pathItem, false);
			fig_changed = true;
		    }
		}
		if (!first) first=spl;
	    }
        }
    } while (foundsomething);
    if (fig_changed) {
        drawPath (fig, path);
        pathItem->setPath (path);

	uint16_t lastpt=0;
	for (auto &figure : scene_glyph->figures)
	    lastpt = figure.renumberPoints (lastpt);
	updateControlPoints ();
	updatePointNumbers ();
    }
    return fig_changed;
}

bool GlyphContext::joinSplines (bool selected, double fudge) {
    ConicGlyph *scene_glyph = glyph (m_scene->outlinesType ());
    QGraphicsItem *panel = m_scene->activePanel ();
    FigurePathItem *pathItem = qgraphicsitem_cast<FigurePathItem *> (panel);
    if (!pathItem)
	return false;

    DrawableFigure &fig = pathItem->svgFigure ();
    bool changed = fig.join (!selected, fudge);

    if (changed) {
	updateCleanupPoints ();
	uint16_t lastpt=0;
	for (auto &figure : scene_glyph->figures)
	    lastpt = figure.renumberPoints (lastpt);
	updatePointNumbers ();
        QPainterPath path;
        drawPath (fig, path);
        pathItem->setPath (path);
    }
    return changed;
}

bool GlyphContext::unlinkSelectedRefs () {
    ConicGlyph *scene_glyph = glyph (m_scene->outlinesType ());
    int num_sel = 0;
    if (scene_glyph->refs.empty ())
	return false;

    checkSelected ();
    for (auto &ref: scene_glyph->refs)
	if (ref.selected) num_sel++;

    clearScene ();
    scene_glyph->unlinkRefs (num_sel > 0);
    for (uint16_t refgid: scene_glyph->refersTo ()) {
        GlyphContext &depctx = m_glyphSet[refgid];
        depctx.removeDependent (m_gid);
    }
    drawGlyph (scene_glyph, scene_glyph->gradients);
    return true;
}

// As advance width is stored in hmtx table rather than in the glyph container
// table, we have to change it at once for all outline types available
void GlyphContext::setAdvanceWidth (int pos) {
    if (m_tt_glyph)
	m_tt_glyph->setAdvanceWidth (pos);
    if (m_ps_glyph)
	m_ps_glyph->setAdvanceWidth (pos);
    if (m_svg_glyph)
	m_svg_glyph->setAdvanceWidth (pos);
    if (m_colr_glyph)
	m_colr_glyph->setAdvanceWidth (pos);
}

bool GlyphContext::removeFigure (int pos) {
    if (!m_scene)
        return false;
    ConicGlyph *scene_glyph = glyph (m_scene->outlinesType ());
    int idx=0;

    for (QGraphicsItem *item : m_topItem->childItems ()) {
	if (item->isPanel ()) {
	    if (idx == pos) {
		FigureItem *figItem = dynamic_cast<FigureItem*> (item);
		auto &curfig = figItem->svgFigure ();
		m_scene->notifyPanelRemoved (item);
		m_scene->removeItem (item);
		scene_glyph->removeFigure (curfig);
		delete item;
		return true;
	    }
	    idx++;
	}
    }
    return false;
}

bool GlyphContext::reorderFigures (int pos1, int pos2) {
    if (!m_scene || pos1 >= pos2)
        return false;
    ConicGlyph *scene_glyph = glyph (m_scene->outlinesType ());
    int idx=0;
    QAbstractGraphicsShapeItem *figItem1 = nullptr, *figItem2 = nullptr;

    for (QGraphicsItem *item : m_topItem->childItems ()) {
	if (item->isPanel ()) {
	    QAbstractGraphicsShapeItem *panel =
		qgraphicsitem_cast<QAbstractGraphicsShapeItem *> (item);
	    if (panel) {
		if (idx == pos1)
		    figItem1 = panel;
		else if (idx == pos2)
		    figItem2 = panel;
		idx++;
	    }
	}
	if (figItem1 && figItem2)
	    break;
    }
    if (figItem1 && figItem2) {
	figItem2->stackBefore (figItem1);
	scene_glyph->swapFigures (pos1, pos2);
	m_scene->update ();
	m_scene->notifyPanelsSwapped (pos1, pos2);
	return true;
    }
    return false;
}

void GlyphContext::addEllipse (const QRectF &rect) {
    ConicGlyph *scene_glyph = glyph (m_scene->outlinesType ());
    scene_glyph->figures.emplace_back ();
    DrawableFigure &fig = scene_glyph->figures.back ();
    auto &props = fig.props;

    props["cx"] = rect.x () + (rect.width () / 2);
    props["cy"] = rect.y () + (rect.height () / 2);
    props["rx"] = std::abs (rect.width ()/2);
    props["ry"] = std::abs (rect.height ()/2);
    if (FontShepherd::math::realNear (props["rx"], props["ry"]))
	fig.type = "circle";
    else
	fig.type = "ellipse";
    fig.order2 = false;

    if (m_scene->outlinesType () == OutlinesType::SVG) {
	QAbstractGraphicsShapeItem *item = new FigureEllipseItem (fig);
    	item->setParentItem (m_topItem);
    	m_scene->setActivePanel (item);
	m_scene->notifyPanelAdded (item);
    } else {
	scene_glyph->svgParseEllipse (fig);
	fig.type = "path";

	if (m_scene->outlinesType () == OutlinesType::TT) {
	    fig.toQuadratic (scene_glyph->upm () / 1000);
	    fig.order2 = true;
	}

	scene_glyph->mergeContours ();
	auto &front = scene_glyph->figures.front ();
	front.renumberPoints ();
	QGraphicsPathItem *pathItem = dynamic_cast<QGraphicsPathItem*> (front.item);
	if (!pathItem) {
	    pathItem = new FigurePathItem (front);
	    pathItem->setParentItem (m_topItem);
	    m_scene->setActivePanel (pathItem);
	    m_scene->notifyPanelAdded (pathItem);
	}
	QPainterPath path;
	drawPath (front, path);
	pathItem->setPath (path);
	drawPoints (front, pathItem, false);
	updatePointNumbers ();
    }
}

void GlyphContext::addRect (const QRectF &rect) {
    ConicGlyph *scene_glyph = glyph (m_scene->outlinesType ());
    scene_glyph->figures.emplace_back ();
    DrawableFigure &fig = scene_glyph->figures.back ();
    auto &props = fig.props;

    double x = rect.x ();
    double y = rect.y ();
    double w = rect.width ();
    double h = rect.height ();
    if (h < 0) {y += h; h = std::abs (h);}
    if (w < 0) {x += w; w = std::abs (w);}
    props["x"] = x;
    props["y"] = y;
    props["width"] = w;
    props["height"] = h;
    fig.type = "rect";
    fig.order2 = false;

    if (m_scene->outlinesType () == OutlinesType::SVG) {
	QAbstractGraphicsShapeItem *item = new FigureRectItem (fig);
    	item->setParentItem (m_topItem);
    	m_scene->setActivePanel (item);
	m_scene->notifyPanelAdded (item);
    } else {
	scene_glyph->svgParseRect (fig);
	fig.type = "path";

	if (m_scene->outlinesType () == OutlinesType::TT) {
	    fig.toQuadratic (scene_glyph->upm () / 1000);
	    fig.order2 = true;
	}

	scene_glyph->mergeContours ();
	auto &front = scene_glyph->figures.front ();
	QGraphicsPathItem *pathItem = dynamic_cast<QGraphicsPathItem*> (front.item);
	if (!pathItem) {
	    pathItem = new FigurePathItem (front);
	    pathItem->setParentItem (m_topItem);
	    m_scene->setActivePanel (pathItem);
	    m_scene->notifyPanelAdded (pathItem);
	}
	QPainterPath path;
	drawPath (front, path);
	pathItem->setPath (path);
	drawPoints (front, pathItem, false);
	updatePointNumbers ();
    }
}

QPainterPath DummyTopItem::shape () const {
    return QPainterPath ();
}

QRectF DummyTopItem::boundingRect () const {
    return QRectF ();
}

int DummyTopItem::type () const {
    return Type;
}

void DummyTopItem::paint (QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED (painter);
    Q_UNUSED (option);
    Q_UNUSED (widget);
}


OnCurvePointItem::OnCurvePointItem (ConicPoint &pt, DrawableFigure &fig, QGraphicsItem *parent, bool is_ref) :
    QAbstractGraphicsShapeItem (parent), m_point (pt), m_fig (fig), m_isRef (is_ref) {

    QPen pointPen (Qt::red);
    QBrush pointBrush (Qt::red);

    setFlag (QGraphicsItem::ItemIgnoresTransformations);
    if (!is_ref) {
        setFlag (QGraphicsItem::ItemIsSelectable);
        setFlag (QGraphicsItem::ItemIsMovable);
    }
    pointPen.setWidth (2);
    setPen (pointPen);
    setBrush (m_point.ttfindex >=0 ? pointBrush : QBrush ());
}

QRectF OnCurvePointItem::boundingRect () const {
    qreal penWidth = pen ().widthF () / 2.0;
    QRectF ret;
    if (!m_isRef && GlyphViewContainer::showPoints ()) {
        ret |= QRectF (
            -4 - penWidth, -4 - penWidth,
            8 + penWidth, 8 + penWidth
        );
    }
    return ret;
}

static void drawDirection (ConicPoint *sp, QPainter *painter) {
    BasePoint dir, *other;
    double len;
    int x=0, y=0, xe, ye;
    ConicPoint *test;

    if (!sp->next)
        return;

    for (test=sp; ;) {
	if (test->me.x!=sp->me.x || test->me.y!=sp->me.y) {
	    other = &test->me;
            break;
	} else if (!test->nonextcp) {
	    other = &test->nextcp;
            break;
	}
	if (!test->next)
            return;
	test = test->next->to;
	if (test==sp)
            return;
    }

    dir.x = other->x-sp->me.x;
    dir.y = sp->me.y-other->y;
    /* GWW: screen coordinates are the mirror of user coords */
    len = sqrt (dir.x*dir.x + dir.y*dir.y);
    dir.x /= len; dir.y /= len;

    x += rint (5*dir.y);
    y -= rint (5*dir.x);
    xe = x + rint (7*dir.x);
    ye = y + rint (7*dir.y);
    painter->drawLine (QLineF (x, y, xe, ye));
    painter->drawLine (QLineF (xe, ye, xe+rint(2*(dir.y-dir.x)), ye+rint(2*(-dir.y-dir.x))));
    painter->drawLine (QLineF (xe, ye, xe+rint(2*(-dir.y-dir.x)), ye+rint(2*(dir.x-dir.y))));
}

static void drawTangentPoint (QVector<QPointF> &gp, const BasePoint &unit) {
    int dir;

    dir = 0;
    gp.resize (3);
    if (unit.x!=0 || unit.y!=0) {
	qreal dx = fabs (unit.x), dy = fabs (unit.y);
	if (dx>2*dy) {
	    if (unit.x>0) dir = 0 /* right */;
	    else dir = 1 /* left */;
	} else if (dy>2*dx) {
	    if (unit.y>0) dir = 2 /* up */;
	    else dir = 3 /* down */;
	} else {
	    if (unit.y>0 && unit.x>0) dir=4;
	    else if (unit.x>0) dir=5;
	    else if (unit.y>0) dir=7;
	    else dir = 6;
	}
    }

    if (dir==1 /* left */ || dir==0 /* right */) {
	gp[0].setY (0);  gp[0].setX (dir==0 ? 4 : -4);
	gp[1].setY (-4); gp[1].setX (0);
	gp[2].setY (4);  gp[2].setX (0);
    } else if (dir==2 /* up */ || dir==3 /* down */) {
	gp[0].setX (0);  gp[0].setY (dir==2 ? -4: 4);
	gp[1].setX (-4); gp[1].setY (0);
	gp[2].setX (4);  gp[2].setY (0);
    } else {
	/* GWW: at a 45 angle, a value of 4 looks too small. I probably want 4*1.414 */
	int xdiff = unit.x > 0 ? 5:-5, ydiff = unit.y > 0 ? -5:5;
	gp[0].setX (xdiff/2); gp[0].setY (ydiff/2);
	gp[1].setX (gp[0].x ()-xdiff); gp[1].setY (gp[0].y ());
	gp[2].setX (gp[0].x ()); gp[2].setY (gp[0].y ()-ydiff);
    }
}

QPainterPath OnCurvePointItem::shape () const {
    QPainterPath path;
    switch (m_point.pointtype) {
      case pt_corner:
        path.addRect (QRectF (-4, -4, 8, 8));
      break;
      case pt_tangent:
        {
            BasePoint unit = m_point.nonextcp ? m_point.prevcp : m_point.nextcp;
            QVector<QPointF> gp;
            unit.x -= m_point.me.x; unit.y -= m_point.me.y;
            drawTangentPoint (gp, unit);
            path.addPolygon (QPolygonF (gp));
        }
      break;
      case pt_curve:
        path.addEllipse (QRectF (-4, -4, 8, 8));
      break;
    }
    return (path);
}

void OnCurvePointItem::paint (
    QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED (widget);
    static QPen selPen (QColor (0xC8, 0xC8, 0), 2);
    static QPen extrPen (QColor (0xC0, 0, 0x80), 2);
    static QBrush extrBrush (QColor (0xC0, 0, 0x80));
    static QPen firstPen (QColor (0x70, 0x70, 0));
    static QBrush firstBrush (QColor (0x70, 0x70, 0));

    if (GlyphViewContainer::showExtrema () && m_point.isExtremum ()) {
        painter->setPen (extrPen);
        painter->setBrush (m_point.ttfindex >=0 ? extrBrush : QBrush ());
    } else if (m_point.isFirst ()) {
        painter->setPen (firstPen);
        painter->setBrush (m_point.ttfindex >=0 ? firstBrush : QBrush ());
    } else {
        painter->setPen (pen ());
        painter->setBrush (m_point.ttfindex >=0 ? brush () : QBrush ());
    }

    if (!m_isRef && GlyphViewContainer::showPoints ()) {
        if (option->state & QStyle::State_Selected) {
            painter->setPen (selPen);
            painter->setBrush (Qt::NoBrush);
        }

        switch (m_point.pointtype) {
          case pt_tangent: {
                BasePoint unit = m_point.nonextcp ? m_point.prevcp : m_point.nextcp;
                QVector<QPointF> gp;
                unit.x -= m_point.me.x; unit.y -= m_point.me.y;
                drawTangentPoint (gp, unit);
                painter->drawPolygon (QPolygonF (gp));
            } break;
          case pt_corner:
            painter->drawRect (QRectF (-3, -3, 6, 6));
          break;
          case pt_curve:
            painter->drawEllipse (QRectF (-3, -3, 6, 6));
          break;
        }

	if (m_point.hintmask) {
            painter->setBrush (Qt::NoBrush);
	    painter->drawEllipse (QRectF (-6, -6, 12, 12));
	}
        if (m_point.isFirst ())
            drawDirection (&m_point, painter);
    }
}

int OnCurvePointItem::type () const {
    return Type;
}

QVariant OnCurvePointItem::itemChange (GraphicsItemChange change, const QVariant &value) {
    if (change == ItemSelectedHasChanged) {
        ConicPointItem *baseItem = qgraphicsitem_cast<ConicPointItem*> (parentItem ());
        baseItem->prepareGeometryChange ();
	ConicPoint *sp = baseItem->conicPoint ();
	sp->selected = value.toBool ();
    }
    return QGraphicsItem::itemChange (change, value);
}

void OnCurvePointItem::setPointType (enum pointtype ptype) {
    m_point.pointtype = ptype;
    update (boundingRect ());
}

OffCurvePointItem::OffCurvePointItem (
    ConicPoint &pt, DrawableFigure &fig, QGraphicsItem *parent, bool is_next, bool is_ref) :
    QAbstractGraphicsShapeItem (parent), m_point (pt), m_fig (fig), m_next (is_next), m_isRef (is_ref) {

    m_color = m_next ? QColor (0, 0x70, 0x90) : QColor (0xCC, 0, 0xCC);
    QPen ctlPen (m_color);
    ctlPen.setCapStyle (Qt::FlatCap);

    setFlag (QGraphicsItem::ItemIgnoresTransformations);
    setFlag (QGraphicsItem::ItemIsSelectable);
    setFlag (QGraphicsItem::ItemIsMovable);

    setPen (ctlPen);
    setPos (
        m_next ? m_point.nextcp.x-m_point.me.x :  m_point.prevcp.x-m_point.me.x,
        m_next ? m_point.nextcp.y-m_point.me.y :  m_point.prevcp.y-m_point.me.y
    );
}

QRectF OffCurvePointItem::boundingRect () const {
    qreal penWidth = pen ().widthF () / 2.0;
    QRectF ret = QRectF ();
    bool nocp = m_next ? m_point.nonextcp : m_point.noprevcp;

    if (!nocp && !m_isRef && GlyphViewContainer::showPoints () &&
        (GlyphViewContainer::showControlPoints () || baseSelected ())) {
        ret |= QRectF (
            -4 - penWidth, -4 - penWidth,
            8 + penWidth, 8 + penWidth
        );
    }
    return ret;
}

QPainterPath OffCurvePointItem::shape () const {
    QPainterPath path;
    path.addRect (QRectF (-4, -4, 8, 8));
    return (path);
}

void OffCurvePointItem::paint (
    QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED (widget);
    painter->setPen (pen ());
    bool nocp = m_next ? m_point.nonextcp : m_point.noprevcp;

    if (!nocp && !m_isRef && GlyphViewContainer::showPoints () &&
        (GlyphViewContainer::showControlPoints () || baseSelected ())) {
        if (option->state & QStyle::State_Selected) {
            painter->setBrush (QBrush (m_color));
            painter->drawRect (QRectF (-4, -4, 8, 8));
            painter->setBrush (Qt::NoBrush);
            painter->setPen (Qt::white);
        }

        painter->drawLine (QLineF (-4, -4, 4, 4));
        painter->drawLine (QLineF (-4, 4, 4, -4));
        painter->setPen (pen ());
    }
}

int OffCurvePointItem::type () const {
    return Type;
}

QVariant OffCurvePointItem::itemChange (GraphicsItemChange change, const QVariant &value) {
    if (change == ItemSelectedHasChanged) {
        ConicPointItem *baseItem = qgraphicsitem_cast<ConicPointItem*> (parentItem ());
        baseItem->prepareGeometryChange ();
    }
    return QGraphicsItem::itemChange (change, value);
}

bool OffCurvePointItem::baseSelected () const {
    ConicPointItem *baseItem = qgraphicsitem_cast<ConicPointItem*> (parentItem ());
    return baseItem->isConicPointSelected ();
}

bool OffCurvePointItem::isNextCP () const {
    return m_next;
}

ConicPointItem::ConicPointItem (ConicPoint &pt, DrawableFigure &fig, QGraphicsItem *parent, bool is_ref) :
    QAbstractGraphicsShapeItem (parent),
    m_valid (true), m_point (pt), m_fig (fig), m_isRef (is_ref), m_nextItem (nullptr), m_prevItem (nullptr) {

    m_point.item = this;
    setFlag (QGraphicsItem::ItemHasNoContents);
    setPos (QPointF (m_point.me.x, m_point.me.y));
    QFont num_font = QFont ();
    num_font.setStyleHint (QFont::SansSerif);
    num_font.setPointSize (8);

    makeNextCP ();
    makePrevCP ();

    m_baseItem = new OnCurvePointItem (m_point, fig, this, m_isRef);
    m_baseItem->setParentItem (this);

    // NB: point number visibility is controlled by a ConicPointItem, but an OnCurvePoint
    // item is set to its nominal parent. That is to make its shift relative to a spline
    // point independent from the viewport scale. The same technique is applied to a text
    // item responsible for displaying nextcpindex
    m_baseNumItem = new QGraphicsSimpleTextItem (QString ("%1").arg (m_point.ttfindex));
    m_baseNumItem->setBrush (QBrush (Qt::red));
    m_baseNumItem->setFont (num_font);
    m_baseNumItem->setFlag (QGraphicsItem::ItemIgnoresTransformations);
    m_baseNumItem->setPos (QPointF (0, -24));
    m_baseNumItem->setVisible (GlyphViewContainer::showPointNumbering () && m_point.ttfindex >= 0);
    m_baseNumItem->setParentItem (m_baseItem);
}

ConicPointItem::~ConicPointItem () {
    m_point.item = nullptr;
}

bool ConicPointItem::valid () const {
    return m_valid;
}

void ConicPointItem::setValid (bool val) {
    m_valid = val;
}

QRectF ConicPointItem::boundingRect () const {
    return QRectF ();
}

void ConicPointItem::paint (
    QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED (painter);
    Q_UNUSED (option);
    Q_UNUSED (widget);
}

int ConicPointItem::type () const {
    return Type;
}

void ConicPointItem::makeNextCP () {
    QFont num_font = QFont ();
    num_font.setStyleHint (QFont::SansSerif);
    num_font.setPointSize (8);

    m_nextItem = new OffCurvePointItem (m_point, m_fig, this, true, m_isRef);
    m_nextItem->setParentItem (this);
    m_nextItem->setVisible (!m_point.nonextcp);
    m_nextHandle = new QGraphicsLineItem (
        QLineF (0, 0, m_point.nextcp.x-m_point.me.x, m_point.nextcp.y-m_point.me.y),
        this);
    m_nextHandle->setPen (QPen (QColor (0, 0x70, 0x90)));
    m_nextHandle->setVisible (!m_point.nonextcp &&
	panel ()->isActive () &&
        !m_isRef && GlyphViewContainer::showPoints () && GlyphViewContainer::showControlPoints ());

    m_nextNumItem = new QGraphicsSimpleTextItem (QString ("%1").arg (m_point.nextcpindex));
    m_nextNumItem->setBrush (QBrush (Qt::red));
    m_nextNumItem->setFont (num_font);
    m_nextNumItem->setFlag (QGraphicsItem::ItemIgnoresTransformations);
    m_nextNumItem->setPos (QPointF (0, -24));
    m_nextNumItem->setVisible (!m_point.nonextcp &&
        GlyphViewContainer::showPointNumbering () && m_point.next && m_point.next->order2);
    m_nextNumItem->setParentItem (m_nextItem);
}

void ConicPointItem::makePrevCP () {
    m_prevItem = new OffCurvePointItem (m_point, m_fig, this, false, m_isRef);
    m_prevItem->setParentItem (this);
    m_prevItem->setVisible (!m_point.noprevcp);
    m_prevHandle = new QGraphicsLineItem (
        QLineF (0, 0, m_point.prevcp.x-m_point.me.x, m_point.prevcp.y-m_point.me.y),
        this);
    m_prevHandle->setPen (QPen (QColor (0xCC, 0, 0xCC)));
    m_prevHandle->setVisible (!m_point.noprevcp &&
	panel ()->isActive () &&
        !m_isRef && GlyphViewContainer::showPoints () && GlyphViewContainer::showControlPoints ());
}

void ConicPointItem::basePointMoved (QPointF newPos) {
    if (scene ()) {
        QRectF rect = scene ()->sceneRect ();
        if (!rect.contains (newPos)) {
            // Keep the item inside the scene rect
            newPos.setX (qMin (rect.right (), qMax (newPos.x (), rect.left ())));
            newPos.setY (qMin (rect.bottom (), qMax (newPos.y (), rect.top ())));
        }
    }

    setPos (newPos);
    BasePoint arg = {newPos.x (), newPos.y ()};
    m_point.moveBasePoint (arg);

    if (!m_point.noprevcp) {
        if (m_point.prev && m_point.prev->order2) {
            ConicPointItem *prevItem = m_point.prev->from->item;
            assert (prevItem);
            // If selected, then it is going to be moved separately in its turn
            if (!prevItem->isConicPointSelected ())
                prevItem->controlPointMoved (QPointF (m_point.prevcp.x, m_point.prevcp.y), true);
        }
    }
    if (!m_point.nonextcp) {
        if (m_point.next && m_point.next->order2) {
            ConicPointItem *nextItem = m_point.next->to->item;
            assert (nextItem);
            if (!nextItem->isConicPointSelected ())
                nextItem->controlPointMoved (QPointF (m_point.nextcp.x, m_point.nextcp.y), false);
        }
    }
    FigurePathItem *pathItem = qgraphicsitem_cast<FigurePathItem*> (panel ());

    QPainterPath path;
    drawPath (m_fig, path);
    pathItem->setPath (path);
}

void ConicPointItem::controlPointMoved (QPointF newPos, bool is_next) {
    QGraphicsLineItem *handle, *opp_handle;
    OffCurvePointItem *item, *opp;
    Conic *spl=nullptr, *opp_spl=nullptr;
    ConicPointItem *fw_item, *bw_item;
    BasePoint &pt = is_next ? m_point.nextcp : m_point.prevcp;
    BasePoint &opp_pt = is_next ? m_point.prevcp : m_point.nextcp;

    // Do nothing, if point is already at the desired position.
    // This prevents endless recursion between two items, representing the
    // same control point on a quadratic spline and attempting to update each other
    if (FontShepherd::math::realNear (newPos.x (), pt.x) &&
        FontShepherd::math::realNear (newPos.y (), pt.y))
        return;

    if (is_next) {
        handle = m_nextHandle; opp_handle = m_prevHandle;
        item = m_nextItem; opp = m_prevItem;
        spl = m_point.next; opp_spl = m_point.prev;
        if (spl) fw_item = spl->to->item;
        if (opp_spl) bw_item = opp_spl->from->item;
    } else {
        handle = m_prevHandle; opp_handle = m_nextHandle;
        item = m_prevItem; opp = m_nextItem;
        spl = m_point.prev; opp_spl = m_point.next;
        if (spl) fw_item = spl->from->item;
        if (opp_spl) bw_item = opp_spl->to->item;
    }

    if (scene ()) {
        QRectF rect = scene ()->sceneRect ();
        if (!rect.contains (newPos)) {
            // Keep the item inside the scene rect
            newPos.setX (qMin (rect.right (), qMax (newPos.x (), rect.left ())));
            newPos.setY (qMin (rect.bottom (), qMax (newPos.y (), rect.top ())));
        }
    }

    BasePoint arg = {newPos.x (), newPos.y ()};
    m_point.moveControlPoint (arg, is_next);
    if (m_point.meChanged ()) {
        setPos (m_point.me.x, m_point.me.y);
        opp->setPos (opp_pt.x - m_point.me.x, opp_pt.y - m_point.me.y);
        opp_handle->setLine (
            QLineF (0, 0, opp_pt.x-m_point.me.x, opp_pt.y-m_point.me.y));
    }
    if (m_point.cpChanged (!is_next)) {
        opp->setPos (opp_pt.x - m_point.me.x, opp_pt.y - m_point.me.y);
        opp_handle->setLine (
            QLineF (0, 0, opp_pt.x-m_point.me.x, opp_pt.y-m_point.me.y));

        if (opp_spl && opp_spl->order2) {
            assert (bw_item);
            bw_item->controlPointMoved (QPointF (opp_pt.x, opp_pt.y), is_next);
        }
    }

    if (spl && spl->order2) {
        assert (fw_item);
        fw_item->controlPointMoved (QPointF (pt.x, pt.y), !is_next);
    }

    // NB: the actual control point position might have been
    // modified as a result of recursion to another item representing the
    // same control point (in case of a quadratic spline) and back to this one.
    // Currently this may occur if the opposite on-curve point of the same
    // spline is tangent, so it introduces additional restrictions to the
    // control point movement. In such situation pt.x, pt.y would already
    // contain modified values
    item->setPos (pt.x-m_point.me.x, pt.y-m_point.me.y);
    item->setVisible (!m_point.noCP (is_next));
    handle->setLine (
        QLineF (0, 0, pt.x-m_point.me.x, pt.y-m_point.me.y));

    FigurePathItem *pathItem = qgraphicsitem_cast<FigurePathItem*> (panel ());
    QPainterPath path;
    drawPath (m_fig, path);
    pathItem->setPath (path);
}


bool ConicPointItem::isConicPointSelected () const {
    return (
        m_baseItem->isSelected () ||
        (m_prevItem && m_prevItem->isSelected ()) ||
        (m_nextItem && m_nextItem->isSelected ()));
}

void ConicPointItem::prepareGeometryChange () {
    m_baseItem->setVisible (m_baseItem->isActive ());
    if (!m_baseItem->isVisible ()) return;
    m_baseItem->prepareGeometryChange ();

    if (!m_point.nonextcp) {
        m_nextItem->prepareGeometryChange ();
        m_nextHandle->setVisible (!m_isRef && GlyphViewContainer::showPoints () &&
	    panel ()->isActive () &&
            (GlyphViewContainer::showControlPoints () || isConicPointSelected ()));
        m_nextNumItem->setVisible (GlyphViewContainer::showPointNumbering () &&
            m_point.next && m_point.next->order2);
    }

    if (!m_point.noprevcp) {
        m_prevItem->prepareGeometryChange ();
        m_prevHandle->setVisible (!m_isRef && GlyphViewContainer::showPoints () &&
	    panel ()->isActive () &&
            (GlyphViewContainer::showControlPoints () || isConicPointSelected ()));
    }

    m_baseNumItem->setVisible (GlyphViewContainer::showPointNumbering () &&
        m_point.ttfindex >= 0);
}

ConicPoint *ConicPointItem::conicPoint () {
    return &m_point;
}

int ConicPointItem::ttfindex () {
    return m_point.ttfindex;
}

int ConicPointItem::nextcpindex () {
    return m_point.nextcpindex;
}

void ConicPointItem::updatePointNumbers () {
    if (m_valid) {
	m_baseNumItem->setText (QString::number (m_point.ttfindex));
	m_nextNumItem->setText (QString::number (m_point.nextcpindex));
    }
}

// The underlying data has been changed, but the scene items not yet
void ConicPointItem::updateControlPoints () {
    bool cp_visible = (
	!m_isRef && (
	    (GlyphViewContainer::showPoints () && GlyphViewContainer::showControlPoints ()) ||
	    m_baseItem->isSelected ())
    );

    m_nextItem->setPos (m_point.nextcp.x-m_point.me.x, m_point.nextcp.y-m_point.me.y);
    m_nextItem->setVisible (!m_point.noCP (true) && cp_visible);
    m_nextHandle->setLine (
        QLineF (0, 0, m_nextItem->pos ().x (), m_nextItem->pos ().y ()));
    m_nextHandle->setVisible (m_nextItem->isVisible ());

    m_prevItem->setPos (m_point.prevcp.x-m_point.me.x, m_point.prevcp.y-m_point.me.y);
    m_prevItem->setVisible (!m_point.noCP (false) && cp_visible);
    m_prevHandle->setLine (
        QLineF (0, 0, m_prevItem->pos ().x (), m_prevItem->pos ().y ()));
    m_prevHandle->setVisible (m_prevItem->isVisible ());
}

FigurePathItem::FigurePathItem (const QPainterPath &path, DrawableFigure &fig) :
    QGraphicsPathItem (path), m_fig (fig) {
    this->setFlag (QGraphicsItem::ItemIsPanel);
    this->setFlag (QGraphicsItem::ItemIsSelectable);
    this->setFlag (QGraphicsItem::ItemIsMovable);
    m_fig.item = this;
}

FigurePathItem::FigurePathItem (DrawableFigure &fig) : QGraphicsPathItem (), m_fig (fig) {
    this->setFlag (QGraphicsItem::ItemIsPanel);
    this->setFlag (QGraphicsItem::ItemIsSelectable);
    this->setFlag (QGraphicsItem::ItemIsMovable);
    m_fig.item = this;
}

FigurePathItem::~FigurePathItem () {
    m_fig.item = nullptr;
}

void FigurePathItem::paint (QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    QStyleOptionGraphicsItem myoption = (*option);
    if (isActive ()) myoption.state &= ~QStyle::State_Selected;

    QGraphicsPathItem::paint (painter, &myoption, widget);
}

int FigurePathItem::type () const {
    return Type;
}

QVariant FigurePathItem::itemChange (GraphicsItemChange change, const QVariant &value) {
    if (change == ItemSelectedHasChanged && isSelected ()) {
	GlyphScene *gsc = qobject_cast<GlyphScene *> (scene ());
	gsc->setActiveFigure (this);
    }
    return QGraphicsItem::itemChange (change, value);
}

void FigurePathItem::moved (QPointF shift) {
    std::array<double, 6> trans = {1, 0, 0, 1, shift.x (), shift.y ()};
    for (auto &spls : m_fig.contours)
	spls.doTransform (trans);
    QPainterPath path;
    drawPath (m_fig, path);
    setPath (path);
}

DrawableFigure &FigurePathItem::svgFigure () const {
    return m_fig;
}

FigureEllipseItem::FigureEllipseItem (DrawableFigure &fig) : m_fig (fig) {
    setFlag (QGraphicsItem::ItemIsPanel);
    setFlag (QGraphicsItem::ItemIsSelectable);
    setFlag (QGraphicsItem::ItemIsMovable);

    fig.item = this;

    double rx = std::abs (fig.props["rx"]);
    double ry = std::abs (fig.props["ry"]);
    setPos (QPointF (fig.props["cx"], fig.props["cy"]));
    setRect (QRectF (-rx, -ry, 2*rx, 2*ry));
    QPen pen = figurePenProps (fig.svgState, 0);
    setPen (pen);

    m_manTopLeft = new ManipulatorItem (QPointF (-rx, ry), Qt::Horizontal | Qt::Vertical, this);
    m_manTopLeft->setEdge (Qt::TopEdge | Qt::LeftEdge);
    m_manTop = new ManipulatorItem (QPointF (0, ry), Qt::Vertical, this);
    m_manTop->setEdge (Qt::TopEdge);
    m_manLeft = new ManipulatorItem (QPointF (-rx, 0), Qt::Horizontal, this);
    m_manLeft->setEdge (Qt::LeftEdge);
}

FigureEllipseItem::~FigureEllipseItem () {
    m_fig.item = nullptr;
}

void FigureEllipseItem::paint (QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    QStyleOptionGraphicsItem myoption = (*option);
    if (isActive ()) myoption.state |= QStyle::State_Selected;

    // The following is based on the qt code, qt_graphicsItem_highlightSelected.
    // Currently I am not going to change the default highlight style, but let it be
    // here just for the case
#if 0
    if (isActive ()) {
	double itemPenWidth = pen ().widthF ();
	const qreal pad = itemPenWidth / 2;
	const qreal penWidth = 0; // cosmetic pen
	const QColor fgcolor = option->palette.windowText ().color ();
	const QColor bgcolor ( // ensure good contrast against fgcolor
	    fgcolor.red ()   > 127 ? 0 : 255,
	    fgcolor.green () > 127 ? 0 : 255,
	    fgcolor.blue ()  > 127 ? 0 : 255
	);
	painter->setPen (QPen (bgcolor, penWidth, Qt::SolidLine));
	painter->setBrush (Qt::NoBrush);
	painter->drawRect (boundingRect().adjusted (pad, pad, -pad, -pad));
	painter->setPen (QPen (option->palette.windowText (), 0, Qt::DashLine));
	painter->setBrush(Qt::NoBrush);
	painter->drawRect (boundingRect ().adjusted (pad, pad, -pad, -pad));
    }
#endif
    QGraphicsEllipseItem::paint (painter, &myoption, widget);
    m_manTopLeft->setVisible (isActive ());
    m_manTop->setVisible (isActive ());
    m_manLeft->setVisible (isActive ());
}

int FigureEllipseItem::type () const {
    return Type;
}

QVariant FigureEllipseItem::itemChange (GraphicsItemChange change, const QVariant &value) {
    if (change == ItemSelectedHasChanged && isSelected ()) {
	GlyphScene *gsc = qobject_cast<GlyphScene *> (scene ());
	gsc->setActiveFigure (this);
    }
    return QGraphicsItem::itemChange (change, value);
}

void FigureEllipseItem::moved (QPointF shift) {
    QPointF curPos = pos ();
    setPos (curPos + shift);
    m_fig.props["cx"] = curPos.x () + shift.x ();
    m_fig.props["cy"] = curPos.y () + shift.y ();
}

void FigureEllipseItem::manipulatorMoved (QPointF shift, ManipulatorItem *source) {
    double xshift = source->direction () & Qt::Horizontal ? shift.x () : 0;
    double yshift = source->direction () & Qt::Vertical ? shift.y () : 0;
    double rx = std::abs (m_fig.props["rx"]);
    double ry = std::abs (m_fig.props["ry"]);

    rx -= xshift;
    ry += yshift;

    m_fig.props["rx"] = rx;
    m_fig.props["ry"] = ry;
    setRect (QRectF (-rx, -ry, 2*rx, 2*ry));

    setManipulators ();
}

ManipulatorItem *FigureEllipseItem::manipulator (Qt::Edges edge) {
    if (edge & (Qt::TopEdge | Qt::LeftEdge))
	return m_manTopLeft;
    else if (edge & Qt::TopEdge)
	return m_manTop;
    else if (edge & Qt::LeftEdge)
	return m_manLeft;

    return nullptr;
}

DrawableFigure &FigureEllipseItem::svgFigure () const {
    return m_fig;
}

void FigureEllipseItem::setManipulators () {
    double rx = std::abs (m_fig.props["rx"]);
    double ry = std::abs (m_fig.props["ry"]);

    m_manTopLeft->setPos (QPointF (-rx, ry));
    m_manTop->setPos (QPointF (0, ry));
    m_manLeft->setPos (QPointF (-rx, 0));
}

FigureRectItem::FigureRectItem (DrawableFigure &fig) : m_fig (fig) {
    setFlag (QGraphicsItem::ItemIsPanel);
    setFlag (QGraphicsItem::ItemIsSelectable);
    setFlag (QGraphicsItem::ItemIsMovable);

    fig.item = this;

    //TODO: let it be here for future, as currently nothing but an identity matrix
    // can occur at this point
    auto &mat = fig.transform;
    double x = mat[0]*fig.props["x"] + mat[2]*fig.props["y"] + mat[4];
    double y = mat[1]*fig.props["x"] + mat[3]*fig.props["y"] + mat[5];

    setPos (QPointF (x, y));
    setRect (QRectF (0, 0, fig.props["width"], fig.props["height"]));
    //setTransform (QTransform (mat[0], mat[1], mat[2], mat[3], mat[4], mat[5]));
    QPen pen = figurePenProps (fig.svgState, 0);
    setPen (pen);

    // Reversed top/bottom to compensate for scene coordinate system
    m_manTopLeft = new ManipulatorItem (QPointF (0, fig.props["height"]), Qt::Horizontal | Qt::Vertical, this);
    m_manTopLeft->setEdge (Qt::BottomEdge | Qt::LeftEdge);
    m_manBotRight = new ManipulatorItem
	(QPointF (fig.props["width"], 0), Qt::Horizontal | Qt::Vertical, this);
    m_manBotRight ->setEdge (Qt::TopEdge | Qt::RightEdge);
}

FigureRectItem::~FigureRectItem () {
    m_fig.item = nullptr;
}

void FigureRectItem::paint (QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    QStyleOptionGraphicsItem myoption = (*option);
    if (isActive ()) myoption.state |= QStyle::State_Selected;

    QGraphicsRectItem::paint (painter, &myoption, widget);
    m_manTopLeft->setVisible (isActive ());
    m_manBotRight->setVisible (isActive ());
}

int FigureRectItem::type () const {
    return Type;
}

QVariant FigureRectItem::itemChange (GraphicsItemChange change, const QVariant &value) {
    if (change == ItemSelectedHasChanged && isSelected ()) {
	GlyphScene *gsc = qobject_cast<GlyphScene *> (scene ());
	gsc->setActiveFigure (this);
    }
    return QGraphicsItem::itemChange (change, value);
}

void FigureRectItem::moved (QPointF shift) {
    QPointF curPos = pos ();
    setPos (curPos + shift);
    m_fig.props["x"] = curPos.x () + shift.x ();
    m_fig.props["y"] = curPos.y () + shift.y ();
}

void FigureRectItem::manipulatorMoved (QPointF shift, ManipulatorItem *source) {
    double xshift = source->direction () & Qt::Horizontal ? shift.x () : 0;
    double yshift = source->direction () & Qt::Vertical ? shift.y () : 0;

    if (source->edge () & Qt::LeftEdge)
	m_fig.props["x"] += xshift;
    if (source->edge () & Qt::TopEdge)
	m_fig.props["y"] += yshift;

    if (source->edge () & Qt::RightEdge)
	m_fig.props["width"] += xshift;
    else if (source->edge () & Qt::LeftEdge)
	m_fig.props["width"] -= xshift;

    if (source->edge () & Qt::TopEdge)
	m_fig.props["height"] -= yshift;
    else if (source->edge () & Qt::BottomEdge)
	m_fig.props["height"] += yshift;

    setPos (QPointF (m_fig.props["x"], m_fig.props["y"]));
    setRect (QRectF (0, 0, m_fig.props["width"], m_fig.props["height"]));

    setManipulators ();
}

ManipulatorItem *FigureRectItem::manipulator (Qt::Edges edge) {
    if (edge & (Qt::BottomEdge | Qt::LeftEdge))
	return m_manTopLeft;
    else if (edge & (Qt::TopEdge | Qt::RightEdge))
	return m_manBotRight;

    return nullptr;
}

DrawableFigure &FigureRectItem::svgFigure () const {
    return m_fig;
}

void FigureRectItem::setManipulators () {
    m_manTopLeft->setPos (QPointF (0, m_fig.props["height"]));
    m_manBotRight->setPos (QPointF (m_fig.props["width"], 0));
}

ManipulatorItem::ManipulatorItem (QPointF pos, Qt::Orientations constr, QGraphicsItem *parent) :
    QAbstractGraphicsShapeItem (parent), m_direction (constr) {
    setPos (pos);

    QPen pointPen (Qt::lightGray, 2);
    QBrush pointBrush (Qt::red);

    setFlag (QGraphicsItem::ItemIgnoresTransformations);
    setFlag (QGraphicsItem::ItemIsSelectable);
    setFlag (QGraphicsItem::ItemIsMovable);

    setPen (pointPen);
    setBrush (pointBrush);

    setVisible (false);
}

QPainterPath ManipulatorItem::shape () const {
    QPainterPath path;
    path.addRect (QRectF (-4, -4, 8, 8));
    return (path);
}

QRectF ManipulatorItem::boundingRect () const {
    qreal penWidth = pen ().widthF () / 2.0;
    QRectF ret (
            -4 - penWidth, -4 - penWidth,
            8 + penWidth, 8 + penWidth
        );
    return ret;
}

void ManipulatorItem::paint (QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED (widget);
    static QPen selPen (Qt::darkGray, 2);
    static QBrush selBrush (Qt::yellow);

    painter->setPen (option->state & QStyle::State_Selected ? selPen : pen ());
    painter->setBrush (option->state & QStyle::State_Selected ? selBrush : brush ());
    painter->drawRect (QRectF (-4, -4, 8, 8));
}

int ManipulatorItem::type () const {
    return Type;
}

QVariant ManipulatorItem::itemChange (GraphicsItemChange change, const QVariant &value) {
    if (change == ItemSelectedHasChanged && isSelected ()) {
	QGraphicsScene *gsc = scene ();
	for (QGraphicsItem *item : gsc->selectedItems ()) {
	    if (item->isSelected () && item != this)
		item->setSelected (false);
	}
    }
    return QGraphicsItem::itemChange (change, value);
}

void ManipulatorItem::setDirection (Qt::Orientations flags) {
    m_direction = flags;
}

Qt::Orientations ManipulatorItem::direction () const {
    return m_direction;
}

void ManipulatorItem::setEdge (Qt::Edges flags) {
    m_edge = flags;
    switch (m_edge) {
      case Qt::TopEdge | Qt::LeftEdge:
      case Qt::BottomEdge | Qt::RightEdge:
	setCursor (QCursor (Qt::SizeFDiagCursor));
	break;
      case Qt::TopEdge | Qt::RightEdge:
      case Qt::BottomEdge | Qt::LeftEdge:
	setCursor (QCursor (Qt::SizeBDiagCursor));
	break;
      case Qt::TopEdge:
      case Qt::BottomEdge:
	setCursor (QCursor (Qt::SizeVerCursor));
	break;
      case Qt::LeftEdge:
      case Qt::RightEdge:
	setCursor (QCursor (Qt::SizeHorCursor));
	break;
    }
}

Qt::Edges ManipulatorItem::edge () const {
    return m_edge;
}

AdvanceWidthItem::AdvanceWidthItem (qreal pos, QGraphicsItem *parent) :
    QAbstractGraphicsShapeItem (parent) {

    setFlag (QGraphicsItem::ItemIgnoresTransformations);
    setFlag (QGraphicsItem::ItemIsSelectable);
    setAcceptHoverEvents (true);

    setPen (QPen (Qt::blue));
    setPos (pos, 0);
}

QRectF AdvanceWidthItem::boundingRect () const {
    return QRectF (-2, GV_MIN_Y, 4, GV_MAX_Y-GV_MIN_Y);
}

QPainterPath AdvanceWidthItem::shape () const {
    QPainterPath path;
    path.addRect (QRectF (-2, GV_MIN_Y, 4, GV_MAX_Y-GV_MIN_Y));
    return (path);
}

void AdvanceWidthItem::paint (
    QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED (widget);
    painter->setPen (pen ());

    if (option->state & QStyle::State_Selected)
        painter->setPen (Qt::green);
    else
        painter->setPen (pen ());
    painter->drawLine (QLineF (0, option->exposedRect.bottom (), 0, option->exposedRect.top ()));
}

int AdvanceWidthItem::type () const {
    return Type;
}

void AdvanceWidthItem::hoverEnterEvent (QGraphicsSceneHoverEvent *) {
    QApplication::setOverrideCursor (QCursor (Qt::SplitHCursor));
}

void AdvanceWidthItem::hoverLeaveEvent (QGraphicsSceneHoverEvent *) {
    QApplication::restoreOverrideCursor ();
}

RefItem::RefItem (DrawableReference &ref, uint16_t idx, const std::string &name, QGraphicsItem *parent) :
    QGraphicsItemGroup (parent),
    m_ref (ref),
    m_glyph (ref.cc),
    m_name (QString::fromStdString (name)),
    m_idx (idx) {
    m_ref.item = this;

    QFont name_font = QFont ();
    name_font.setStyleHint (QFont::SansSerif);
    name_font.setPointSize (8);
    BasePoint top = {0, -1e10};
    std::array<double, 6>id_trans {1, 0, 0, 1, 0, 0};
    m_glyph->findTop (&top, id_trans);
    if (top.y < -65536) top.y = 0;

    setFlag (QGraphicsItem::ItemIsSelectable);
    setFlag (QGraphicsItem::ItemIsMovable);

    // Need this dummy item:
    // 1) to make the distance between the topmost point of the glyph contour and
    //    the glyph name label independent from viewport scale ratio;
    // 2) to exclude the label from the group bounding box (as the label itself
    //    is not added to the group)
    QGraphicsPathItem *dummyItem = new QGraphicsPathItem ();
    dummyItem->setFlag (QGraphicsItem::ItemIgnoresTransformations);
    dummyItem->setFlag (QGraphicsItem::ItemHasNoContents);
    addToGroup (dummyItem);

    m_nameItem = new QGraphicsSimpleTextItem (m_name);
    m_nameItem->setFont (name_font);
    m_nameItem->setFlag (QGraphicsItem::ItemIgnoresTransformations);
    m_nameItem->setParentItem (dummyItem);

    // Don't take reference shift into account here, as label is shifted together
    // with the whole group
    int x = (int) (top.x - (m_nameItem->boundingRect ().width ())/2);
    int y = (int) top.y;
    dummyItem->setPos (QPointF (x, y));
    m_nameItem->setPos (QPointF (0, -24));
}

RefItem::~RefItem () {
    m_ref.item = nullptr;
}

void RefItem::paint (QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    if (option->state & QStyle::State_Selected)
        painter->setPen (QPen (option->palette.text (), 1.0, Qt::DashLine));
    QGraphicsItemGroup::paint (painter, option, widget);
}

int RefItem::type () const {
    return Type;
}

uint16_t RefItem::idx () const {
    return m_idx;
}

uint16_t RefItem::gid () const {
    return m_glyph->gid ();
}

const DrawableReference& RefItem::ref () const {
    return m_ref;
}

void RefItem::refMoved (QPointF shift) {
    QGraphicsItemGroup::setTransform (QTransform (1, 0, 0, 1, shift.x (), shift.y ()), true);
    m_ref.transform[4] += shift.x ();
    m_ref.transform[5] += shift.y ();
}
