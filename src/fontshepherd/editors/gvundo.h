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

#include <QtWidgets>

enum cmd_type {
    em_move, em_merge, em_delete, em_join
};

// Can't just preserve a pointer/reference to a moved point, as it may further
// be deleted. Hopefully the combination of coordinates, point index and the
// index of the next control point (for the case this point is interpolated)
// gives us a unique description sufficient to find the desired object
typedef struct unique_point {
    double x, y;
    int ttfindex, nextcpindex;
    bool is_control, is_next;

    bool operator==(const struct unique_point &rsp) const;
    bool operator!=(const struct unique_point &rsp) const;
} UniquePoint;

typedef struct unique_figure {
    FigureType type;
    std::map<std::string, double> props;
    SvgState state;
    std::array<double, 6> transform;
    std::vector<UniquePoint> onPoints;

    unique_figure (DrawableFigure &fig);

    bool operator==(const struct unique_figure &rsf) const;
    bool operator!=(const struct unique_figure &rsf) const;
} UniqueFigure;

typedef struct unique_manipulator {
    UniqueFigure figure;
    Qt::Edges edge;

    bool operator==(const struct unique_manipulator &rsm) const;
    bool operator!=(const struct unique_manipulator &rsm) const;
} UniqueManipulator;

typedef struct unique_ref {
    double x, y;
    uint16_t idx, gid;

    bool operator==(const struct unique_ref &rsr) const;
    bool operator!=(const struct unique_ref &rsr) const;
} UniqueRef;

class DrawableFigure;
class AdvanceWidthItem;
class ConicGlyph;

class MoveCommand : public QUndoCommand {

public:
    MoveCommand (QPointF move, GlyphContext &gctx, uint8_t gtype, QUndoCommand *parent=nullptr);

    void appendOffCurvePoint (QPointF cp, int base_idx, int next_idx, bool is_next);
    void appendOnCurvePoint (QPointF pt, int base_idx, int next_idx);
    void appendFigure (DrawableFigure &fig);
    void appendManipulator (DrawableFigure &fig, Qt::Edges edge);
    void appendRef (QTransform trans, uint16_t idx, uint16_t gid);
    void appendAdvanceWidth (double pos);

    void undo ();
    void redo ();
    int id () const;
    bool mergeWith (const QUndoCommand *cmd);

private:
    void checkOffPoint (bool undo);
    void checkManipulator (bool undo);
    void iteratePoints (DrawableFigure *figptr, std::vector<UniquePoint> &onPoints, bool undo);
    void iterateFigs (bool undo);
    void iterateRefs (bool undo);

    QPointF m_move;
    GlyphContext &m_context;
    uint8_t m_outlines_type;
    ConicGlyph *m_glyph;
    bool m_undone;

    std::vector<UniquePoint> m_offPoints;
    std::vector<UniquePoint> m_onPoints;
    std::vector<UniqueFigure> m_figs;
    std::vector<UniqueRef> m_refs;
    std::vector<UniqueManipulator> m_manipulators;
    std::vector<double> m_awContainer;
};

class FigurePropsChangeCommand : public QUndoCommand {

public:
    FigurePropsChangeCommand (GlyphContext &ctx, uint8_t otype, SvgState &newstate, int pos, QUndoCommand *parent=nullptr);

    void undo ();
    void redo ();

private:
    GlyphContext &m_context;
    uint8_t m_outlinesType;
    DrawableFigure *m_figptr;
    SvgState m_undoState, m_redoState;
    int m_idx;
    bool m_undone;
};

/* Generic undo command: glyph state is just serialized and then
   restored from SVG */
class GlyphChangeCommand : public QUndoCommand {

public:
    GlyphChangeCommand (GlyphContext &ctx, uint8_t gtype, QUndoCommand *parent=nullptr);

    void undo ();
    void redo ();
    // Return to the previous state without attempting to preserve the current one.
    // Useful if the current state is known invalid
    void undoInvalid ();

private:
    GlyphContext &m_context;
    uint8_t m_outlines_type;
    bool m_undone;
    std::string undo_svg, redo_svg;
};
