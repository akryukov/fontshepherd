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
#include <QAbstractTableModel>

class GlyphContext;

class FigurePalette : public QTableView {
public:
    FigurePalette (GlyphContext &ctx, FigureModel *model, uint8_t otype, QWidget *topwin, QWidget *parent = nullptr);
    void setOutlinesType (uint8_t otype);

    static QPixmap defaultPixmap (const QSize &size);
    static QPixmap colorPixmap (const QSize &size, ConicGlyph *g, const SvgState &state, bool fill);

private slots:
    void startColorEditor (const QModelIndex &index);
    void showContextMenu (const QPoint &point);

    void removeFigure ();
    void figureUp ();
    void figureDown ();
    void unsetFillColor ();
    void unsetStrokeColor ();

private:
    void unsetColorIndeed (bool fill);
    void swapRows (int idx1, int idx2);

    GlyphContext &m_context;
    uint8_t m_outlines_type;
    QWidget *m_topWin;
};

class FigureModel : public QAbstractTableModel {
public:
    FigureModel (QGraphicsItem *figRoot, ConicGlyph *g, QWidget *parent = nullptr);

    int rowCount (const QModelIndex &parent = QModelIndex ()) const override;
    int columnCount (const QModelIndex &parent = QModelIndex ()) const override;
    QVariant data (const QModelIndex &index, int role) const override;
    bool setData (const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags (const QModelIndex &index) const override;
    QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void reset (QGraphicsItem *figRoot, ConicGlyph *g);
    void addFigure (QGraphicsItem *item, int pos);
    void removeFigure (int pos);
    void swapFigures (int pos1, int pos2);
    void setRowState (int row, SvgState &state);

private:
    QGraphicsItem *m_figRoot;
    QVector<QLatin1String> m_typeList;
    QVector<SvgState> m_stateList;
    ConicGlyph *m_glyph;
};
