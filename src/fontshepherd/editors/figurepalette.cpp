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

#include "splineglyph.h"
#include "editors/glyphcontext.h"
#include "editors/glyphview.h"
#include "editors/figurepalette.h"
#include "editors/commondelegates.h"
#include "editors/gvundo.h"
#include "fs_undo.h"

FigurePalette::FigurePalette (GlyphContext &ctx, FigureModel *model, OutlinesType otype, QWidget *topwin, QWidget *parent) :
    QTableView (parent), m_context (ctx), m_outlines_type (otype), m_topWin (topwin) {

    setModel (model);
    setItemDelegateForColumn (2, new SpinBoxDelegate (0, 100));
    setContextMenuPolicy (Qt::CustomContextMenu);

    QFontMetrics fm = fontMetrics ();
    int w0 = fm.boundingRect ("~Figure Type~").width ();
    int w1 = fm.boundingRect ("~OutlW~").width ();
    setColumnWidth (0, w0);
    for (int i=1; i<5; i++) setColumnWidth (i, w1);
    horizontalHeader ()->setStretchLastSection (true);
    setSelectionBehavior (QAbstractItemView::SelectRows);
    setSelectionMode (QAbstractItemView::SingleSelection);
    resize (w0 + w1*3, rowHeight (0) * 5);

    connect (this, &QTableView::customContextMenuRequested, this, &FigurePalette::showContextMenu);
    connect (this, &QTableView::doubleClicked, this, &FigurePalette::startColorEditor);
}

void FigurePalette::setOutlinesType (OutlinesType otype) {
    m_outlines_type = otype;
}

void FigurePalette::startColorEditor (const QModelIndex &index) {
    QAbstractItemModel *absmod = this->model ();
    FigureModel *figMod = dynamic_cast<FigureModel *> (absmod);
    if (index.column () == 1 || index.column () == 3) {
	QColor cell_color = qvariant_cast<QColor> (figMod->data (index, Qt::EditRole));
	QColorDialog cdlg (cell_color, m_topWin);
	cdlg.setOptions (QColorDialog::ShowAlphaChannel);
	if (cdlg.exec () == QDialog::Accepted) {
	    cell_color = cdlg.selectedColor ();
	    figMod->setData (index, cell_color, Qt::EditRole);
	}
    }
}

void FigurePalette::showContextMenu (const QPoint &point) {
    QModelIndex index = this->indexAt (point);
    if (index.isValid ()) {
	QMenu menu (this);

	QAction removeAction ("Remove Figure", &menu);
	QAction upAction ("Move Up", &menu);
	QAction downAction ("Move Down", &menu);
	QAction unsetFillAction ("Unset Fill Color", &menu);
	QAction unsetStrokeAction ("Unset Stroke Color", &menu);

	upAction.setEnabled (index.row () > 0);
	downAction.setEnabled (index.row () < model ()->rowCount () - 1);

	connect (&removeAction, &QAction::triggered, this, &FigurePalette::removeFigure);
	connect (&upAction, &QAction::triggered, this, &FigurePalette::figureUp);
	connect (&downAction, &QAction::triggered, this, &FigurePalette::figureDown);
	connect (&unsetFillAction, &QAction::triggered, this, &FigurePalette::unsetFillColor);
	connect (&unsetStrokeAction, &QAction::triggered, this, &FigurePalette::unsetStrokeColor);

	menu.addAction (&removeAction);
	menu.addAction (&upAction);
	menu.addAction (&downAction);
	menu.addSeparator ();
	menu.addAction (&unsetFillAction);
	menu.addAction (&unsetStrokeAction);

	menu.exec (this->viewport ()->mapToGlobal (point));
    }
}

void FigurePalette::removeFigure () {
    // NB: no direct call to FigureModel::removeFigure, as it is triggered
    // via context => scene => GlyphView interaction
    QItemSelectionModel *selMod = this->selectionModel ();
    const QModelIndexList &selRows = selMod->selectedRows ();
    if (!selRows.empty ()) {
	int row = model ()->rowCount () - (selRows[0].row () + 1);
        GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, m_outlines_type);
        ucmd->setText (tr ("Remove SVG Figure"));
        if (m_context.removeFigure (row)) {
	    m_context.render (m_outlines_type);
	    m_context.update (m_outlines_type);
	    m_context.undoGroup (true)->activeStack ()->push (ucmd);
	} else {
	    delete ucmd;
	}
    }
}

void FigurePalette::swapRows (int idx1, int idx2) {
    GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, m_outlines_type);
    ucmd->setText (tr ("Change Figure Order"));
    if (m_context.reorderFigures (idx1, idx2)) {
        m_context.render (m_outlines_type);
        m_context.update (m_outlines_type);
        m_context.undoGroup (true)->activeStack ()->push (ucmd);
    } else {
        delete ucmd;
    }
}

void FigurePalette::figureUp () {
    QItemSelectionModel *selMod = this->selectionModel ();
    const QModelIndexList &selRows = selMod->selectedRows ();
    if (!selRows.empty ()) {
	int row = model ()->rowCount () - (selRows[0].row () + 1);
	int next = row+1;
	swapRows (row, next);
    }
}

void FigurePalette::figureDown () {
    QItemSelectionModel *selMod = this->selectionModel ();
    const QModelIndexList &selRows = selMod->selectedRows ();
    if (!selRows.empty ()) {
	int row = model ()->rowCount () - (selRows[0].row () + 1);
	int prev = row-1;
	swapRows (prev, row);
    }
}

void FigurePalette::unsetColorIndeed (bool fill) {
    QItemSelectionModel *selMod = this->selectionModel ();
    const QModelIndexList &selRows = selMod->selectedRows ();
    if (!selRows.empty ()) {
	int row = model ()->rowCount () - (selRows[0].row () + 1);

	ConicGlyph *g = m_context.glyph (m_outlines_type);
	auto it = g->figures.begin ();
	std::advance (it, row);
	auto &fig = *it;
	SvgState state = fig.svgState;

	bool &set = fill ? state.fill_set : state.stroke_set;
	rgba_color &color = fill ? state.fill : state.stroke;
	set = false;
	color = rgba_color ();

	FigurePropsChangeCommand *ucmd =
	    new FigurePropsChangeCommand (m_context, m_outlines_type, state, row);
	fig.svgState = state;
	m_context.updateFill ();
	m_context.render (m_outlines_type);
	m_context.update (m_outlines_type);
	GlyphScene *gsc = m_context.scene ();
	gsc->notifyFigurePropsChanged (row);
	m_context.undoGroup (true)->activeStack ()->push (ucmd);
    }
}

void FigurePalette::unsetFillColor () {
    unsetColorIndeed (true);
}

void FigurePalette::unsetStrokeColor () {
    unsetColorIndeed (false);
}

QPixmap FigurePalette::defaultPixmap (const QSize &size) {
    QPixmap pm = QPixmap (size);
    int w = size.width ();
    int h = size.height ();
    //std::cerr << "h=" << h << " w=" << w << std::endl;
    //pm.setDevicePixelRatio (QApplication::primaryScreen()->devicePixelRatio());
    pm.fill ();
    QPainter p (&pm);
    QPen pen (Qt::black);
    pen.setWidth (4);
    p.setPen (pen);
    p.drawRect (0,0, w,h);
    pen.setWidth (2);
    p.drawLine (0,0, w,h);
    p.drawLine (0,h, w,0);
    return pm;
}

QPixmap FigurePalette::colorPixmap (const QSize &size, ConicGlyph *g, const SvgState &state, bool fill) {
    bool set = fill ? state.fill_set : state.stroke_set;
    if (!set)
        return defaultPixmap (size);

    QPixmap pm = QPixmap (size);
    int w = size.width ();
    int h = size.height ();
    pm.fill ();
    QPainter p (&pm);
    QPen pen (Qt::black);
    pen.setWidth (4);
    p.setPen (pen);
    p.setBrush (GlyphContext::figureBrush (state, nullptr, g->gradients, fill));
    p.drawRect (0,0, w,h);
    return pm;
}

FigureModel::FigureModel (QGraphicsItem *figRoot, ConicGlyph *g, QWidget *parent) :
    QAbstractTableModel (parent), m_figRoot (figRoot), m_glyph (g) {

    const auto &itemList = m_figRoot->childItems ();
    for (auto it = itemList.crbegin (); it != itemList.crend (); it++) {
	QGraphicsItem *child = *it;
	if (child->isPanel ()) {
	    FigureItem *figItem = dynamic_cast<FigureItem *> (child);
	    if (figItem) {
		m_typeList.push_back (QLatin1String (figItem->svgFigure ().type.c_str ()));
		m_stateList.push_back (figItem->svgFigure ().svgState);
	    }
	}
    }
}

int FigureModel::rowCount (const QModelIndex &parent) const {
    Q_UNUSED (parent);

    return m_stateList.size ();
}

int FigureModel::columnCount (const QModelIndex &parent) const {
    Q_UNUSED (parent);
    return 4;
}

QVariant FigureModel::data (const QModelIndex &index, int role) const {
    int colidx = index.column ();
    int row = index.row ();
    QPixmap pm;

    if (m_stateList.size () <= row)
	return QVariant ();
    const SvgState &state = m_stateList[row];
    const rgba_color &fill = state.fill;
    const rgba_color &stroke = state.stroke;

    switch (role) {
      case Qt::DisplayRole:
	switch (colidx) {
	  case 0:
	    return m_typeList[row];
	  case 2:
	    return state.stroke_width;
	}
	break;
      case Qt::EditRole:
	switch (colidx) {
	  case 1:
	    return QColor (fill.red, fill.green, fill.blue, fill.alpha);
	  case 2:
	    return state.stroke_width;
	  case 3:
	    return QColor (stroke.red, stroke.green, stroke.blue, stroke.alpha);
	}
	break;
      case Qt::UserRole:
	switch (colidx) {
	  case 0:
	    return QVariant::fromValue (state);
	  case 2:
	    return state.stroke_width;
	}
	break;
      case Qt::DecorationRole:
	switch (colidx) {
	  case 1:
	    pm = FigurePalette::colorPixmap (QSize (32, 32), m_glyph, state, true);
	    return pm;
	  case 3:
	    pm = FigurePalette::colorPixmap (QSize (32, 32), m_glyph, state, false);
	    return pm;
	}
    }
    return QVariant ();
}

bool FigureModel::setData (const QModelIndex &index, const QVariant &value, int role) {
    SvgState &state = m_stateList[index.row ()];
    if (index.isValid () && role == Qt::EditRole) {
	switch (index.column ()) {
	  case 1:
	  case 3: {
	    rgba_color &color = index.column () == 1 ? state.fill : state.stroke;
	    bool &set = index.column () == 1 ? state.fill_set : state.stroke_set;
	    if (value.userType () == QMetaType::QColor) {
		QColor new_color = qvariant_cast<QColor> (value);
		color.red = new_color.red ();
		color.green = new_color.green ();
		color.blue = new_color.blue ();
		color.alpha = new_color.alpha ();
		set = true;
	    } else {
		color.red = color.green = color.blue = color.alpha = 0;
		set = false;
	    }
	    emit dataChanged (index, index);
	  } return true;
	  case 2:
	    int w = value.toInt ();
	    state.stroke_width = w;
	    emit dataChanged (index, index);
	    return true;
	}
    }
    return false;
}

Qt::ItemFlags FigureModel::flags (const QModelIndex &index) const {
    Qt::ItemFlags ret = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column () == 2) {
	ret |= Qt::ItemIsEditable;
    }
    return ret;
}

QVariant FigureModel::headerData (int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
	switch (section) {
	  case 0:
	    return QWidget::tr ("Figure Type");
	  case 1:
	    return QWidget::tr ("FillC");
	  case 2:
	    return QWidget::tr ("OutlW");
	  case 3:
	    return QWidget::tr ("OutlC");
	}
    } else if (orientation == Qt::Vertical && role == Qt::DisplayRole) {
	return QString::number (section);
    }
    return QVariant ();
}

void FigureModel::reset (QGraphicsItem *figRoot, ConicGlyph *g) {
    beginResetModel ();
    m_figRoot = figRoot;
    m_glyph = g;
    m_stateList.clear ();
    m_typeList.clear ();
    const auto &itemList = m_figRoot->childItems ();
    for (auto it = itemList.crbegin (); it != itemList.crend (); it++) {
	QGraphicsItem *child = *it;
	if (child->isPanel ()) {
	    FigureItem *figItem = dynamic_cast<FigureItem *> (child);
	    if (figItem) {
		m_stateList.push_back (figItem->svgFigure ().svgState);
		m_typeList.push_back (QLatin1String (figItem->svgFigure ().type.c_str ()));
	    }
	}
    }
    endResetModel ();
}

void FigureModel::addFigure (QGraphicsItem *item, int pos) {
    FigureItem *figItem = dynamic_cast<FigureItem *> (item);
    beginInsertRows (QModelIndex (), pos, pos);
    if (pos <= m_typeList.size ()) {
        m_stateList.insert (m_stateList.begin () + pos, figItem->svgFigure ().svgState);
        m_typeList.insert (m_typeList.begin () + pos, QLatin1String (figItem->svgFigure ().type.c_str ()));
    } else {
        m_stateList.push_back (figItem->svgFigure ().svgState);
        m_typeList.push_back (QLatin1String (figItem->svgFigure ().type.c_str ()));
    }
    endInsertRows ();
}

void FigureModel::removeFigure (int pos) {
    if (pos >= 0 && pos < rowCount ()) {
	beginRemoveRows (QModelIndex (), pos, pos);
	m_stateList.erase (m_stateList.begin () + pos);
	m_typeList.erase (m_typeList.begin () + pos);
	endRemoveRows ();
    }
}

void FigureModel::swapFigures (int pos1, int pos2) {
    if (pos2 > pos1 && pos2 < rowCount ()) {
	beginMoveRows (QModelIndex (), pos1, pos1, QModelIndex (), pos2 + 1);
	std::swap (m_typeList[pos1], m_typeList[pos2]);
	std::swap (m_stateList[pos1], m_stateList[pos2]);
	endMoveRows ();
    }
}

void FigureModel::setRowState (int row, SvgState &state) {
    QModelIndex fill_idx = index (row, 1);
    QModelIndex stroke_w_idx = index (row, 2);
    QModelIndex stroke_idx = index (row, 3);

    m_stateList[row] = state;
    QVariant fill_c = state.fill_set ?
	QVariant (QColor (state.fill.red, state.fill.green, state.fill.blue, state.fill.alpha)) :
	QVariant (false);
    QVariant stroke_c = state.stroke_set ?
	QVariant (QColor (state.stroke.red, state.stroke.green, state.stroke.blue, state.stroke.alpha)) :
	QVariant (false);

    setData (fill_idx, fill_c, Qt::EditRole);
    setData (stroke_w_idx, state.stroke_width, Qt::EditRole);
    setData (stroke_idx, stroke_c, Qt::EditRole);
}
