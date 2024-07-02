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

#include "fs_math.h"

#include "editors/fontview.h" // also includes tables.h
#include "editors/glyphview.h"
#include "splineglyph.h"
#include "editors/glyphcontext.h"
#include "editors/gvundo.h"

bool UniquePoint::operator==(const UniquePoint &rsp) const {
    return (
        FontShepherd::math::realNear (x, rsp.x) && FontShepherd::math::realNear (y, rsp.y) &&
        ttfindex == rsp.ttfindex && nextcpindex == rsp.nextcpindex &&
        is_control == rsp.is_control && (!is_control || is_next == rsp.is_next));
}

bool UniquePoint::operator!=(const UniquePoint &rsp) const {
    return !(*this == rsp);
}

UniqueFigure::unique_figure (DrawableFigure &fig) {
    type = fig.elementType ();
    props = fig.props;
    state = fig.svgState;
    transform = fig.transform;

    if (!fig.contours.empty ()) {
	for (auto &spls : fig.contours) {
	    ConicPoint *sp = spls.first;
	    do {
		UniquePoint save = {sp->me.x, sp->me.y, sp->ttfindex, sp->nextcpindex, false, false};
		onPoints.push_back (save);
		sp = sp->next ? sp->next->to : nullptr;
	    } while (sp && sp!=spls.first);
	}
    }
}

bool UniqueFigure::operator==(const UniqueFigure &rsf) const {
    return (
	type == rsf.type &&
	props == rsf.props &&
	state == rsf.state &&
	transform == rsf.transform &&
	onPoints == rsf.onPoints
    );
}

bool UniqueFigure::operator!=(const UniqueFigure &rsf) const {
    return !(*this == rsf);
}

bool UniqueRef::operator==(const UniqueRef &rsr) const {
    return (
        FontShepherd::math::realNear (x, rsr.x) && FontShepherd::math::realNear (y, rsr.y) &&
        idx == rsr.idx && gid == rsr.gid);
}

bool UniqueRef::operator!=(const UniqueRef &rsr) const {
    return !(*this == rsr);
}

bool UniqueManipulator::operator==(const UniqueManipulator &rsm) const {
    return (
        figure == rsm.figure && edge == rsm.edge
    );
}

bool UniqueManipulator::operator!=(const UniqueManipulator &rsm) const {
    return !(*this == rsm);
}

MoveCommand::MoveCommand (QPointF move, GlyphContext &gctx, OutlinesType gtype, QUndoCommand *parent) :
     QUndoCommand (parent),
     m_move (move),
     m_context (gctx),
     m_outlines_type (gtype),
     m_glyph (gctx.glyph (gtype)),
     m_undone (false) {}

void MoveCommand::appendOffCurvePoint (QPointF cp, int base_idx, int next_idx, bool is_next) {
    // can move just one control point at once
    m_offPoints.resize (0);
    UniquePoint save = {cp.x (), cp.y (), base_idx, next_idx, true, is_next};
    m_offPoints.push_back (save);
    setText (QCoreApplication::tr ("Move Control Point"));
}

void MoveCommand::appendOnCurvePoint (QPointF pt, int base_idx, int next_idx) {
    int other = m_refs.size () + m_figs.size () + m_awContainer.size ();
    UniquePoint save = {pt.x (), pt.y (), base_idx, next_idx, false, false};
    m_onPoints.push_back (save);
    if (other != 0)
        setText (QCoreApplication::tr ("Move Elements"));
    else if (m_onPoints.size () == 1)
        setText (QCoreApplication::tr ("Move Point"));
    else
        setText (QCoreApplication::tr ("Move Points"));
}

void MoveCommand::appendFigure (DrawableFigure &fig) {
    m_figs.emplace_back (fig);
    int other = m_refs.size () + m_onPoints.size () + m_awContainer.size ();
    if (other != 0)
        setText (QCoreApplication::tr ("Move Elements"));
    else if (m_figs.size () > 1)
        setText (QCoreApplication::tr ("Move Figures"));
    else
	setText (QCoreApplication::tr ("Move Figure"));
}

void MoveCommand::appendRef (QTransform trans, uint16_t idx, uint16_t gid) {
    int other = m_figs.size () + m_awContainer.size ();
    UniqueRef ur = {trans.dx (), trans.dy (), idx, gid};
    m_refs.push_back (ur);
    if (other != 0)
        setText (QCoreApplication::tr ("Move Elements"));
    else
        setText (QCoreApplication::tr ("Move Reference"));
}

void MoveCommand::appendManipulator (DrawableFigure &fig, Qt::Edges edge) {
    // can move just one control point at once
    UniqueManipulator save = {UniqueFigure (fig), edge};
    m_manipulators.push_back (save);
    setText (QCoreApplication::tr ("Modify Figure"));
}

void MoveCommand::appendAdvanceWidth (double pos) {
    int other = m_figs.size () + m_refs.size ();
    m_awContainer.push_back (pos);
    if (other != 0)
        setText (QCoreApplication::tr ("Move Elements"));
    else
        setText (QCoreApplication::tr ("Change Advance Width"));
}

void MoveCommand::undo () {
    checkOffPoint (true);
    checkManipulator (true);
    iterateFigs (true);
    iteratePoints (nullptr, m_onPoints, true);
    iterateRefs (true);
    if (!m_awContainer.empty ()) {
        double new_x = m_awContainer[0] - m_move.x ();
        m_awContainer[0] = new_x;
        m_glyph->setAdvanceWidth (round (new_x));
        if (m_context.awItem)
            m_context.awItem->setPos (QPointF (new_x, 0));
    }
    m_context.render (m_outlines_type);
    m_context.update (m_outlines_type);
    m_undone = true;
}

void MoveCommand::redo () {
    // Prevent redo from being executed right after placing the command to the undo stack.
    // That's how the Qt Undo framework works, but we don't want this
    if (!m_undone) {
        m_context.render (m_outlines_type);
        m_context.update (m_outlines_type);
        return;
    }

    checkOffPoint (true);
    checkManipulator (false);
    iterateFigs (false);
    iteratePoints (nullptr, m_onPoints, false);
    iterateRefs (false);
    if (!m_awContainer.empty ()) {
        double new_x = m_awContainer[0] + m_move.x ();
        m_awContainer[0] = new_x;
        m_glyph->setAdvanceWidth (round (new_x));
        if (m_context.awItem)
            m_context.awItem->setPos (QPointF (new_x, 0));
    }
    m_context.render (m_outlines_type);
    m_context.update (m_outlines_type);
}

bool MoveCommand::mergeWith (const QUndoCommand *cmd) {
    const MoveCommand *moveCommand = static_cast<const MoveCommand *> (cmd);
    uint16_t i;
    QPointF add = moveCommand->m_move;

    if (m_offPoints.size () != moveCommand->m_offPoints.size ())
        return false;
    else if (m_offPoints.size () == 1) {
        UniquePoint cmp = moveCommand->m_offPoints.back ();
        cmp.x -= add.x (); cmp.y -= add.y ();
        if (m_offPoints[0] == cmp) {
            m_move += add;
            m_offPoints[0].x += add.x ();
            m_offPoints[0].y += add.y ();
            return true;
        }
        return false;
    }

    if (m_manipulators.size () != moveCommand->m_manipulators.size ())
	return false;
    else if (m_manipulators.size () == 1) {
	UniqueManipulator cmp = moveCommand->m_manipulators.back ();
	UniqueFigure &fig = cmp.figure;
        if (!(cmp.edge & Qt::LeftEdge) && !(cmp.edge & Qt::RightEdge))
	    add.setX (0);
        if (!(cmp.edge & Qt::TopEdge) && !(cmp.edge & Qt::BottomEdge))
	    add.setY (0);
	if (fig.type == ElementType::Circle || fig.type == ElementType::Ellipse) {
	    fig.props["rx"] += add.x ();
	    fig.props["ry"] -= add.y ();
	    if (m_manipulators.back () == cmp) {
		m_move += add;
		m_manipulators.back ().figure.props["rx"] -= add.x ();
		m_manipulators.back ().figure.props["ry"] += add.y ();
		return true;
	    }
	    return false;
	} else if (fig.type == ElementType::Rect) {
	    if (cmp.edge & Qt::LeftEdge)
		fig.props["width"] += add.x ();
	    else if (cmp.edge & Qt::RightEdge)
		fig.props["width"] -= add.x ();

	    if (cmp.edge & Qt::TopEdge)
		fig.props["height"] += add.y ();
	    else if (cmp.edge & Qt::BottomEdge)
		fig.props["height"] -= add.y ();

	    if (cmp.edge & Qt::LeftEdge)
		fig.props["x"] -= add.x ();
	    if (cmp.edge & Qt::TopEdge)
		fig.props["y"] -= add.y ();

	    if (m_manipulators.back () == cmp) {
		m_move += add;
		auto &mfig = m_manipulators.back ().figure;

		if (cmp.edge & Qt::LeftEdge)
		    mfig.props["width"] -= add.x ();
		else if (cmp.edge & Qt::RightEdge)
		    mfig.props["width"] += add.x ();

		if (cmp.edge & Qt::TopEdge)
		    mfig.props["height"] -= add.y ();
		else if (cmp.edge & Qt::BottomEdge)
		    mfig.props["height"] += add.y ();

		if (cmp.edge & Qt::LeftEdge)
		    mfig.props["x"] += add.x ();
		if (cmp.edge & Qt::TopEdge)
		    mfig.props["y"] += add.y ();
		return true;
	    }
	    return false;
	}
    }

    if (m_onPoints.size () != moveCommand->m_onPoints.size ())
        return false;
    for (i=0; i<m_onPoints.size (); i++) {
        UniquePoint cmp = moveCommand->m_onPoints[i];
        cmp.x -= add.x (); cmp.y -= add.y ();
        if (m_onPoints[i] != cmp)
            return false;
    }

    if (m_figs.size () != moveCommand->m_figs.size ())
        return false;
    for (i=0; i<m_figs.size (); i++) {
        UniqueFigure cmp = moveCommand->m_figs[i];
	if (m_figs[i].type != cmp.type) {
	    return false;
	} else if (cmp.type == ElementType::Ellipse || cmp.type == ElementType::Circle) {
	    cmp.props["cx"] -= add.x (); cmp.props["cy"] -= add.y ();
	} else if (cmp.type == ElementType::Rect) {
	    cmp.props["x"] -= add.x (); cmp.props["y"] -= add.y ();
	} else {
	    if (m_figs[i].onPoints.size () != cmp.onPoints.size ())
		return false;
	    for (size_t j=0; j<cmp.onPoints.size (); j++) {
		UniquePoint &cmp_pt = cmp.onPoints[j];
		cmp_pt.x -= add.x (); cmp_pt.y -= add.y ();
		if (m_figs[i].onPoints[j] != cmp_pt)
		    return false;
	    }
	}
        if (m_figs[i] != cmp)
            return false;
    }

    if (m_refs.size () != moveCommand->m_refs.size ())
        return false;
    for (i=0; i<m_refs.size (); i++) {
        UniqueRef cmp = moveCommand->m_refs[i];
        cmp.x -= add.x (); cmp.y -= add.y ();
        if (m_refs[i] != cmp)
            return false;
    }
    if (m_awContainer.size () != moveCommand->m_awContainer.size ())
        return false;
    if (m_awContainer.size () == 1 && (
        m_awContainer[0] != (moveCommand->m_awContainer[0] - add.x ())))
        return false;

    for (auto &fig : m_figs) {
	if (fig.type == ElementType::Ellipse || fig.type == ElementType::Circle) {
	    fig.props["cx"] += add.x ();
	    fig.props["cy"] += add.y ();
	} else if (fig.type == ElementType::Rect) {
	    fig.props["x"] += add.x ();
	    fig.props["y"] += add.y ();
	} else {
	    for (auto &pt : fig.onPoints) {
		pt.x += add.x ();
		pt.y += add.y ();
	    }
	}
    }

    for (i=0; i<m_onPoints.size (); i++) {
        m_onPoints[i].x += add.x ();
        m_onPoints[i].y += add.y ();
    }
    for (auto &ref : m_refs) {
        ref.x += add.x ();
        ref.y += add.y ();
    }
    for (i=0; i<m_awContainer.size (); i++)
        m_awContainer[i] = m_awContainer[i] + add.x ();
    m_move += add;
    return true;
}

int MoveCommand::id () const {
    return em_move;
}

static bool samePoint (UniquePoint &u, ConicPoint *sp) {
    if (u.is_control && (u.is_next ? !sp->nonextcp : !sp->noprevcp)) {
        BasePoint &pt = u.is_next ? sp->nextcp : sp->prevcp;
        return (
            sp->ttfindex == u.ttfindex && sp->nextcpindex == u.nextcpindex &&
            FontShepherd::math::realNear (pt.x, u.x) && FontShepherd::math::realNear (pt.y, u.y));
    } else {
        return (
            sp->ttfindex == u.ttfindex && (sp->nonextcp || sp->nextcpindex == u.nextcpindex) &&
            FontShepherd::math::realNear (sp->me.x, u.x) && FontShepherd::math::realNear (sp->me.y, u.y));
    }
    return false;
}

static void movePoint (UniquePoint &u, ConicPoint *sp, QPointF vector, bool back) {
    if (back) vector *= -1;

    if (u.is_control && (u.is_next ? !sp->nonextcp : !sp->noprevcp)) {
        BasePoint &pt = u.is_next ? sp->nextcp : sp->prevcp;
        if (!sp->item) {
            BasePoint newpos = {pt.x + vector.x (), pt.y + vector.y ()};
            sp->moveControlPoint (newpos, u.is_next);
        } else {
            QPointF newPos = QPointF (pt.x + vector.x (), pt.y + vector.y ());
            sp->item->controlPointMoved (newPos, u.is_next);
        }
    } else {
        if (!sp->item) {
            BasePoint newpos = {sp->me.x + vector.x (), sp->me.y + vector.y ()};
            sp->moveBasePoint (newpos);
        } else {
            QPointF newPos = QPointF (sp->me.x + vector.x (), sp->me.y + vector.y ());
            sp->item->basePointMoved (newPos);
        }
    }
    // Store new position for the reverse operation
    u.x += vector.x (); u.y += vector.y ();
}

void MoveCommand::checkManipulator (bool undo) {
    QPointF vector = m_move;
    if (undo) vector *= -1;
    DrawableFigure *figptr = m_context.activeFigure ();
    ElementType ftype = figptr->elementType ();

    if (!figptr || m_manipulators.empty ())
    	return;
    auto &um = m_manipulators.back ();

    if (ftype == ElementType::Ellipse || ftype == ElementType::Circle) {
	FigureEllipseItem *item = dynamic_cast<FigureEllipseItem *> (figptr->item);
	ManipulatorItem *manItem = item->manipulator (um.edge);
	item->manipulatorMoved (vector, manItem);
    } else if (ftype == ElementType::Rect) {
	FigureRectItem *item = dynamic_cast<FigureRectItem *> (figptr->item);
	ManipulatorItem *manItem = item->manipulator (um.edge);
	item->manipulatorMoved (vector, manItem);
    }
}

void MoveCommand::checkOffPoint (bool undo) {
    DrawableFigure *figptr = m_context.activeFigure ();
    if (!figptr || figptr->contours.empty () || m_offPoints.empty ())
    	return;

    for (auto &spls : figptr->contours) {
        ConicPoint *sp = spls.first;
	bool found = false;
        do {
	    if (samePoint (m_offPoints[0], sp)) {
		movePoint (m_offPoints[0], sp, m_move, undo);
		found = true;
	    }
	    sp = sp->next ? sp->next->to : nullptr;
        } while (sp && sp!=spls.first && !found);
	if (found)
	    break;
    }
}

void MoveCommand::iteratePoints (DrawableFigure *figptr, std::vector<UniquePoint> &onPoints, bool undo) {
    size_t cnt=0, num=onPoints.size ();
    std::vector<bool> moved;
    moved.resize (num);
    std::fill (moved.begin (), moved.end (), 0);

    if (!figptr)
	figptr = m_context.activeFigure ();
    if (!figptr || figptr->contours.empty () || onPoints.empty ())
    	return;

    for (auto &spls : figptr->contours) {
        ConicPoint *sp = spls.first;
        do {
	    for (size_t j=0; j<num && cnt < num; j++) {
		if (!moved[j] && samePoint (onPoints[j], sp)) {
		    movePoint (onPoints[j], sp, m_move, undo);
		    moved[j] = true;
		    cnt++;
		}
	    }
	    sp = sp->next ? sp->next->to : nullptr;
        } while (sp && sp!=spls.first && cnt < num);
	if (cnt == num)
	    break;
    }
}

void MoveCommand::iterateFigs (bool undo) {
    size_t cnt=0;
    std::vector<bool> moved;
    QPointF vector = m_move;
    if (undo) vector *= -1;

    moved.resize (m_figs.size ());
    std::fill (moved.begin (), moved.end (), 0);

    for (auto it = m_glyph->figures.begin (); it != m_glyph->figures.end (); it++) {
	DrawableFigure &fig = *it;
        for (size_t j=0; j<m_figs.size () && cnt<m_figs.size (); j++) {
            UniqueFigure uf (fig);
            if (!moved[j] && uf == m_figs[j]) {
                if (fig.item) {
                    fig.item->moved (vector);
                } else {
                    fig.transform[4] += vector.x ();
                    fig.transform[5] += vector.y ();
                }
		iteratePoints (&fig, m_figs[j].onPoints, undo);
                moved[j] = true;
                cnt++;
            }
        }
    }
}

void MoveCommand::iterateRefs (bool undo) {
    uint16_t cnt=0;
    std::vector<bool> moved;
    QPointF vector = m_move;
    if (undo) vector *= -1;

    moved.resize (m_refs.size ());
    std::fill (moved.begin (), moved.end (), 0);

    for (auto it = m_glyph->refs.begin (); it != m_glyph->refs.end (); it++) {
	auto &rg = *it;
        for (size_t j=0; j<m_refs.size () && cnt<m_refs.size (); j++) {
            UniqueRef ur = {rg.transform[4], rg.transform[5], rg.item->idx (), rg.GID};
            if (!moved[j] && ur == m_refs[j]) {
                if (rg.item) {
                    rg.item->refMoved (vector);
                } else {
                    rg.transform[4] += vector.x ();
                    rg.transform[5] += vector.y ();
                }
                moved[j] = true;
                cnt++;
            }
        }
    }
}

GlyphChangeCommand::GlyphChangeCommand (GlyphContext &ctx, OutlinesType gtype, QUndoCommand *parent) :
    QUndoCommand (parent),
    m_context (ctx),
    m_outlines_type (gtype),
    m_undone (false)
{
    ConicGlyph *g = m_context.glyph (gtype);
    undo_svg = g->toSVG ();
    redo_svg = "";
}

void GlyphChangeCommand::undo () {
    ConicGlyph *g = m_context.glyph (m_outlines_type);
    if (redo_svg.compare ("") == 0)
        redo_svg = g->toSVG ();
    m_context.clearScene ();
    g->clear ();
    BoostIn buf (undo_svg.c_str (), undo_svg.size ());
    g->fromSVG (buf);
    m_context.resolveRefs (m_outlines_type);
    m_context.render (m_outlines_type);
    m_context.drawGlyph (g, g->gradients);
    m_context.update (m_outlines_type);
    m_undone = true;
}

void GlyphChangeCommand::redo () {
    // Prevent redo from being executed right after placing the command to the undo stack.
    // That's how the Qt Undo framework works, but we don't want this
    if (!m_undone)
        return;
    ConicGlyph *g = m_context.glyph (m_outlines_type);
    m_context.clearScene ();
    g->clear ();
    BoostIn buf (redo_svg.c_str (), redo_svg.size ());
    g->fromSVG (buf);
    m_context.resolveRefs (m_outlines_type);
    m_context.render (m_outlines_type);
    m_context.drawGlyph (g, g->gradients);
    m_context.update (m_outlines_type);
}

void GlyphChangeCommand::undoInvalid () {
    ConicGlyph *g = m_context.glyph (m_outlines_type);
    m_context.clearScene ();
    g->clear ();
    BoostIn buf (undo_svg.c_str (), undo_svg.size ());
    g->fromSVG (buf);
    m_context.resolveRefs (m_outlines_type);
    m_context.render (m_outlines_type);
    m_context.drawGlyph (g, g->gradients);
    m_context.update (m_outlines_type);
}

FigurePropsChangeCommand::FigurePropsChangeCommand
    (GlyphContext &ctx, OutlinesType otype, SvgState &newstate, int pos, QUndoCommand *parent) :
    QUndoCommand (parent),
    m_context (ctx),
    m_outlinesType (otype),
    m_redoState (newstate),
    m_idx (pos),
    m_undone (false)
{
    setText (QCoreApplication::tr ("Figure Properties Change"));

    ConicGlyph *g = m_context.glyph (m_outlinesType);
    auto it = g->figures.begin ();
    std::advance (it, m_idx);
    m_figptr = &(*it);
    m_undoState = m_figptr->svgState;
}

void FigurePropsChangeCommand::undo () {
    m_figptr->svgState = m_undoState;
    m_context.updateFill ();
    m_context.render (m_outlinesType);
    m_context.update (m_outlinesType);
    GlyphScene *gsc = m_context.scene ();
    gsc->notifyFigurePropsChanged (m_idx);
    m_undone = true;
}

void FigurePropsChangeCommand::redo () {
    if (!m_undone)
	return;
    m_figptr->svgState = m_redoState;
    m_context.updateFill ();
    m_context.render (m_outlinesType);
    m_context.update (m_outlinesType);
    GlyphScene *gsc = m_context.scene ();
    gsc->notifyFigurePropsChanged (m_idx);
}
