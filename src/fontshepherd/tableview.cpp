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

#include <iostream>
#include "exceptions.h"
#include "fontshepherd.h"
#include "tableview.h"
#include "sfnt.h"
#include "tables.h"
#include "tables/devmetrics.h"

#include "fs_notify.h"

TableViewContainer::TableViewContainer (QString &path, QWidget* parent_w) :
    QTabWidget (parent_w) {
    m_has_font = false;
    curTab = 0;
    m_uGroup = std::unique_ptr<QUndoGroup> (new QUndoGroup (this));
    QUndoGroup *ugptr = dynamic_cast<QUndoGroup *> (m_uGroup.get ());

    try {
	QString check_path = checkPath (path);
        fontFile = std::unique_ptr<sfntFile> (new sfntFile (check_path, this));
        m_has_font = true;
    } catch (FileNotFoundException &e) {
        FontShepherd::postError (tr ("Error"),
            tr ("Could not open %1.").arg (e.fileName ()), parent_w);
    } catch (FileDamagedException &e) {
        FontShepherd::postError (tr ("Error"),
            tr ("Could not read data from %1. The file is damaged.").arg (e.fileName ()), parent_w);
    } catch (FileLoadCanceledException &e) {
        FontShepherd::postError (tr ("Error"),
            tr ("Could not load %1: loading canceled by user.").arg (e.fileName ()), parent_w);
    }
    if (!m_has_font)
        return;
    parent_w->setWindowTitle (fontFile->name ());

    FontShepherdMain *fsptr = qobject_cast <FontShepherdMain *> (parent_w);
    for (int i=0; i<fontFile->fontCount (); i++) {
        sFont* fnt = fontFile->font (i);
	QUndoStack *us = new QUndoStack (ugptr);
        TableView* tbl_matrix = new TableView (fnt, i, us, this);
	m_uStackMap.insert (tbl_matrix, us);
	connect (us, &QUndoStack::cleanChanged, this, [this, i](bool val) {
	    this->setFontModified (i, !val);
	});
	connect (tbl_matrix, &TableView::rowSelected, fsptr, &FontShepherdMain::enableEditActions);
        addTab (tbl_matrix, fnt->fontname);
    }
    connect (this, &TableViewContainer::fileModified, fsptr, &FontShepherdMain::setModified);
}

TableViewContainer::~TableViewContainer () {
    qDebug() << "delete container";
}

bool TableViewContainer::loadFont (QString &path) {
    QUndoGroup *ugptr = dynamic_cast<QUndoGroup *> (m_uGroup.get ());
    FontShepherdMain *fsptr = qobject_cast <FontShepherdMain *> (window ());

    try {
        fontFile->addToCollection (path);
    } catch (FileNotFoundException &e) {
        FontShepherd::postError (tr ("Error"),
            tr ("Could not open %1.").arg (e.fileName ()), fsptr);
	return false;
    } catch (FileDamagedException &e) {
        FontShepherd::postError (tr ("Error"),
            tr ("Could not read data from %1. The file is damaged.").arg (e.fileName ()), fsptr);
	return false;
    } catch (FileLoadCanceledException &e) {
        FontShepherd::postError (tr ("Error"),
            tr ("Could not load %1: loading canceled by user.").arg (e.fileName ()), fsptr);
	return false;
    } catch (FileDuplicateException &e) {
        FontShepherd::postError (tr ("Error"),
            tr ("Could not load %1: can't import the same file twice.").arg (e.fileName ()), fsptr);
	return false;
    }

    int startpos = this->count ();
    for (int i=startpos; i<fontFile->fontCount (); i++) {
        sFont* fnt = fontFile->font (i);
	QUndoStack *us = new QUndoStack (ugptr);
        TableView* tbl_matrix = new TableView (fnt, i, us, this);
	m_uStackMap.insert (tbl_matrix, us);
	connect (us, &QUndoStack::cleanChanged, this, [this, i](bool val) {
	    this->setFontModified (i, !val);
	});
	connect (tbl_matrix, &TableView::rowSelected, fsptr, &FontShepherdMain::enableEditActions);
        addTab (tbl_matrix, fnt->fontname);
    }
    return true;
}

bool TableViewContainer::hasFont () {
    return m_has_font;
}

sfntFile *TableViewContainer::font () {
    return m_has_font ? fontFile.get () : nullptr;
}

QString TableViewContainer::checkPath (QString &path) {
    QString ret = path;
    if (path.isEmpty ())
        ret = QFileDialog::getOpenFileName(this, tr ("Open Font"), "",
            tr ("OpenType Font Files (*.ttf *.otf *.ttc)"));

    if (!ret.isEmpty ())
        return ret;
    else
	throw FileNotFoundException (path.toStdString ());
}

void TableViewContainer::saveFont (bool overwrite, bool ttc) {
    QString newpath = QString ();
    int fidx = currentIndex ();
    int imin = ttc ? fidx : 0;
    int imax = ttc ? fidx+1 : fontFile->fontCount ();

    try {
        if (!overwrite || !fontFile->hasSource (fidx, ttc)) {
            newpath = QFileDialog::getSaveFileName (this, tr ("Save Font"), "",
                tr ("OpenType Font Files (*.ttf *.TTF *.otf *.OTF *.ttc *.TTC)"));

            if (!newpath.isEmpty ())
                fontFile->save (newpath, ttc, fidx);
	    else
		return;
        } else {
            fontFile->save (fontFile->path (fidx), ttc, fidx);
        }
    } catch (CantBackupException &e) {
        QMessageBox::critical (this, tr ("Error"),
            tr ("Could not save %1: failed to backup.").arg (e.fileName ()));
	return;
    } catch (CantRestoreException &e) {
        QMessageBox::critical (this, tr ("Error"),
            tr ("Could not save %1: failed to restore from backup.").arg (e.fileName ()));
	return;
    } catch (FileDamagedException &e) {
        QMessageBox::critical (this, tr ("Error"),
            tr ("Could not read data from %1. The file is damaged.").arg (e.fileName ()));
	return;
    } catch (FileAccessException &e) {
        QMessageBox::critical (this, tr ("Error"),
            tr ("Can't write to %1.").arg (e.fileName ()));
	return;
    }

    emit fileModified (false);
    for (auto w: m_uStackMap.keys ())
	m_uStackMap[w]->setClean ();
    // NB: the following is connected to QUndoStack::cleanChanged, but
    // setClean () doesn't cause that signal to be fired
    for (int i=imin; i<imax; i++)
	setFontModified (i, false);
    QWidget *w = currentWidget ();
    if (w) {
	QTableView *tv = qobject_cast<QTableView *> (w);
	tv->viewport ()->update ();
    }
}

QAction *TableViewContainer::undoAction (QObject *parent, const QString &prefix) {
    return m_uGroup->createUndoAction (parent, prefix);
}

QAction *TableViewContainer::redoAction (QObject *parent, const QString &prefix) {
    return m_uGroup->createRedoAction (parent, prefix);
}

void TableViewContainer::setFontModified (int font_idx, const bool val) {
    if (font_idx < this->count ()) {
	QString title = tabText (font_idx);
	bool has_asterisk = title.startsWith (QChar ('*'));

	// Tables may have been edited. These changes aren't handled by the TableView undo stack
	bool coll_changed = val, fnt_changed = val;
	for (int i=0; i<fontFile->fontCount ();i++) {
	    sFont *fnt = fontFile->font (i);
	    for (int j=0; j<fnt->tableCount (); j++) {
		coll_changed |= fnt->tbls[j]->modified ();
		if (i==font_idx) fnt_changed |= fnt->tbls[j]->modified ();
	    }
	}

	if (has_asterisk && !fnt_changed)
	    setTabText (font_idx, title.remove (0, 1));
	else if (!has_asterisk && fnt_changed)
	    setTabText (font_idx, title.prepend ('*'));
	emit fileModified (coll_changed);
    }
}

TableViewModel::TableViewModel (sFont *fnt, int idx, QWidget *parent) :
    QAbstractTableModel (parent), m_font (fnt), m_index (idx), m_parent (parent) {
}

int TableViewModel::rowCount (const QModelIndex &parent) const {
    Q_UNUSED (parent);
    return m_font->tableCount ();
}

int TableViewModel::columnCount (const QModelIndex &parent) const {
    Q_UNUSED (parent);
    return 3;
}

QVariant TableViewModel::data (const QModelIndex &index, int role) const {
    FontTable* tbl = m_font->tbls[index.row ()].get ();
    int start = tbl->td_changed ? -1 : tbl->start;
    int length = tbl->td_changed ? tbl->newlen : tbl->len;
    switch (role) {
      case Qt::DisplayRole:
	switch (index.column ()) {
	  case 0:
	    return QString::fromStdString (tbl->stringName ());
	  case 1:
	    return QString::number (start);
	  case 2:
	    return QString::number (length);
	}
	break;
      case Qt::FontRole:
	if (tbl->td_changed) {
	    QFont bf;
            bf.setBold (true);
            return bf;
	}
	break;
      case Qt::ForegroundRole:
	if (m_font->container->tableRefCount (m_font->tbls[index.row ()].get ()) > 1)
	    return QColor (Qt::green);
	break;
      case Qt::TextAlignmentRole:
	switch (index.column ()) {
	  case 0:
	    return QVariant (Qt::AlignLeft | Qt::AlignVCenter);
	  case 1:
	    return QVariant (Qt::AlignRight | Qt::AlignVCenter);
	  case 2:
	    return QVariant (Qt::AlignRight | Qt::AlignVCenter);
	}
    }
    return QVariant ();
}

bool TableViewModel::setData (const QModelIndex &index, const QVariant &value, int role) {
    Q_UNUSED (index);
    Q_UNUSED (value);
    Q_UNUSED (role);
    return false;
}

Qt::ItemFlags TableViewModel::flags (const QModelIndex &index) const {
    Q_UNUSED (index);
    return (Qt::ItemIsEnabled | Qt::ItemIsSelectable);
}

QVariant TableViewModel::headerData (int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
	switch (section) {
	  case 0:
	    return QWidget::tr ("Table");
	  case 1:
	    return QWidget::tr ("Offset");
	  case 2:
	    return QWidget::tr ("Length");
	}
    } else if (orientation == Qt::Vertical && role == Qt::DisplayRole) {
	return QString::number (section+1);
    }
    return QVariant ();
}

bool TableViewModel::removeRows (int row, int count, const QModelIndex &index) {
    Q_UNUSED (index);
    Q_ASSERT (count==1);

    beginRemoveRows (QModelIndex (), row, row);
    m_font->tbls[row].reset ();
    m_font->tbls.erase (m_font->tbls.begin () + row);
    endRemoveRows ();
    return true;
}

bool TableViewModel::insertTable (int row, std::shared_ptr<FontTable> tptr) {
    tptr->container = m_font->container;
    beginInsertRows (QModelIndex (), row, row);
    m_font->tbls.insert (m_font->tbls.begin () + row, tptr);
    endInsertRows ();
    emit needsSelectionUpdate (row);
    return true;
}

bool TableViewModel::pasteTable (int row, FontTable *tbl) {
    Q_ASSERT (row < m_font->tableCount ());
    tbl->container = m_font->container;
    m_font->tbls[row].reset (tbl);
    emit dataChanged (index (row, 0, QModelIndex ()), index (row, 2, QModelIndex ()));
    emit needsSelectionUpdate (row);
    return true;
}

void TableViewModel::updateViews (shared_ptr<FontTable> tptr) {
    int row = 0;
    if (tptr->compiled ()) {
	if (tptr->isNew ()) {
	    row = rowCount (QModelIndex ());
	    insertTable (row, tptr);
	} else {
	    for (int i=0; i<m_font->tableCount (); i++) {
		if (m_font->tbls[i] == tptr) {
		    row = i;
		    break;
		}
	    }
	}
	emit dataChanged (index (row, 0, QModelIndex ()), index (row, 2, QModelIndex ()));
    }
}

TableView::TableView (sFont* fnt, int idx, QUndoStack *us, QWidget* parent) :
    QTableView (parent), m_font (fnt), m_index (idx), m_ustack (us), m_parent (parent) {

    verticalHeader ()->setVisible (false);
    setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    m_model = std::unique_ptr<TableViewModel> (new TableViewModel (fnt, idx, parent));
    TableViewModel *modptr = dynamic_cast<TableViewModel *> (m_model.get ());
    setModel (modptr);
    connect (modptr, &TableViewModel::needsSelectionUpdate, this, &TableView::updateSelection);

    int hwidth = horizontalHeader ()->length();
    int fwidth = frameWidth () * 2;
    setMinimumWidth (hwidth + fwidth + 16);
    setMinimumHeight (rowHeight (0) * 16);
    horizontalHeader ()->setStretchLastSection (true);

    setEditTriggers (QAbstractItemView::NoEditTriggers);

    setSelectionBehavior (QAbstractItemView::SelectRows);
    setSelectionMode (QAbstractItemView::SingleSelection);

    connect (this, &QTableView::doubleClicked, this, &TableView::doubleClickHandler);
}

TableView::~TableView () {
}

void TableView::selectionChanged (const QItemSelection  &selected, const QItemSelection  &deselected) {
    QTableView::selectionChanged (selected, deselected);
    int idxnew = selected.length () > 0 ? selected.at (0).top () : -1;
    int idxold = deselected.length () > 0 ? deselected.at (0).top () : -1;

    if (idxnew >= 0 && idxnew != idxold)
        emit rowSelected (m_index, idxnew);
    else if (idxnew < 0)
        emit rowSelected (m_index, -1);
}

void TableView::clear () {
    int cur;
    FontTable* cur_table = nullptr;

    cur = getSelectionIndex ();
    if (cur >= 0) cur_table = m_font->tbls[cur].get ();

    if (cur_table==nullptr)
        return;

    TableViewModel *modptr = dynamic_cast<TableViewModel *> (m_model.get ());
    AddOrRemoveTableCommand *cmd = new AddOrRemoveTableCommand (modptr, m_font, cur);
    cmd->setText (tr ("Insert table"));
    m_ustack->push (cmd);
}

void TableView::cut () {
    int cur;
    FontTable* cur_table = nullptr;

    cur = getSelectionIndex ();
    if (cur >= 0) cur_table = m_font->tbls[cur].get ();

    if (cur_table == nullptr)
        return;
    if (cur_table->editor () && cur_table->editor ()->isModified ())
        cur_table->editor ()->checkUpdate (false);
    if (!cur_table->loaded ())
        cur_table->fillup ();

    QClipboard *clipboard = QApplication::clipboard ();
    QMimeData *md = new QMimeData;
    md->setData ("fontshepherd/x-fonttable", cur_table->serialize ());
    clipboard->setMimeData (md);

    TableViewModel *modptr = dynamic_cast<TableViewModel *> (m_model.get ());
    AddOrRemoveTableCommand *cmd = new AddOrRemoveTableCommand (modptr, m_font, cur);
    cmd->setText (tr ("Cut table"));
    m_ustack->push (cmd);
}

void TableView::copy () {
    int cur;
    FontTable* cur_table = nullptr;

    cur = getSelectionIndex ();
    if (cur >= 0) cur_table = m_font->tbls[cur].get ();

    if (cur_table == nullptr)
        return;
    if (cur_table->editor () && cur_table->editor ()->isModified ())
        cur_table->editor ()->checkUpdate (false);
    if (!cur_table->loaded ())
        cur_table->fillup ();

    QClipboard *clipboard = QApplication::clipboard ();
    QMimeData *md = new QMimeData;
    md->setData ("fontshepherd/x-fonttable", cur_table->serialize ());
    clipboard->setMimeData (md);
}

void TableView::paste () {
    FontTable *paste_table = nullptr, *cur_table = nullptr;

    QClipboard *clipboard = QApplication::clipboard ();
    const QMimeData *md = clipboard->mimeData ();
    if (!md->hasFormat ("fontshepherd/x-fonttable"))
        return;

    paste_table = new FontTable (md->data ("fontshepherd/x-fonttable"));
    int row = getSelectionIndex ();
    if (row >= 0) cur_table = m_font->tbls[row].get ();

    if (cur_table!=nullptr) {
	if (cur_table->iName () != paste_table->iName ()) {
            QMessageBox::StandardButton ask;
            ask = QMessageBox::question (this,
                tr ("Table name mismatch"),
                tr ("You are attempting to replace the selected "
                    "table with one of a different type.\n"
                    "Is that really what you want to do?"),
                QMessageBox::Yes|QMessageBox::No);
            if (ask == QMessageBox::No)
                return;
        }
    } else {
        for (int i=0; i<m_font->tableCount (); ++i ) {
            if (m_font->tbls[i]->iName () == paste_table->iName ()) {
                m_font->tbls[i].reset (cur_table);
                row = i;
                break;
            }
        }
    }

    TableViewModel *modptr = dynamic_cast<TableViewModel *> (m_model.get ());
    if (cur_table==nullptr) {
	AddOrRemoveTableCommand *cmd = new AddOrRemoveTableCommand (modptr, m_font, paste_table, m_font->tableCount ());
	cmd->setText (tr ("Insert table"));
	m_ustack->push (cmd);
    } else {
	PasteTableCommand *cmd = new PasteTableCommand (modptr, m_font, paste_table, row);
	cmd->setText (tr ("Replace table"));
	m_ustack->push (cmd);
    }
}

void TableView::unselect () {
    clearSelection ();
    setCurrentIndex (QModelIndex ());
}

void TableView::editTable (std::shared_ptr<FontTable> tptr, bool hex) {
    if (hex)
	tptr->hexEdit (m_font, tptr, this);
    else
	tptr->edit (m_font, tptr, this);
    TableViewContainer *contptr = qobject_cast<TableViewContainer *> (m_parent);
    if (tptr->editor ()) {
	TableViewModel *modptr = dynamic_cast<TableViewModel *> (m_model.get ());
        connect (tptr->editor (), &TableEdit::update, modptr, &TableViewModel::updateViews);
        connect (tptr->editor (), &TableEdit::update, contptr, [=] () {
	    contptr->setFontModified (m_index, true);
	});
    }
}

void TableView::editTable (int row, bool hex) {
    Q_ASSERT (row>=0 && row < m_font->tableCount ());
    std::shared_ptr<FontTable> curTable = m_font->tbls[row];
    if (curTable == nullptr)
        return;
    editTable (curTable, hex);
}

void TableView::edit () {
    int row = getSelectionIndex ();
    editTable (row, false);
}

void TableView::hexEdit () {
    int row = getSelectionIndex ();
    editTable (row, true);
}

void TableView::genHdmxTable () {
    std::shared_ptr<HdmxTable> hdmx =
	std::dynamic_pointer_cast<HdmxTable> (m_font->sharedTable (CHR ('h','d','m','x')));
    if (!hdmx) {
	std::vector<uint8_t> hdmx_sizes {
	    11, 12, 13, 15, 16, 17, 19, 20, 21, 24, 27, 29,
	    32, 33, 37, 42, 46, 50, 54, 58, 67, 75, 83, 92, 100
	};
	TableHeader props;
	props.file = nullptr;
	props.iname = CHR ('h','d','m','x');
	props.off = 0xffffffff;
	props.length = 0;
	props.checksum = 0;
	hdmx = std::make_shared<HdmxTable> (m_font->container, props);

	hdmx->addSize (hdmx_sizes[0]);
	hdmx->setNumGlyphs (m_font->glyph_cnt);
	for (size_t i=1; i<hdmx_sizes.size (); i++) {
	    hdmx->addSize (hdmx_sizes[i]);
	}
    }
    // sequence:: editTable calls hdmx->edit and connects TableEdit::update to updateViews;
    // if user canceled, the table is restored in HdmxEdit::save;
    // updateViews inserts the table into the model or deletes it, if necessary
    editTable (hdmx, false);
}

void TableView::genLtshTable () {
    std::shared_ptr<LtshTable> ltsh =
	dynamic_pointer_cast<LtshTable> (m_font->sharedTable (CHR ('L','T','S','H')));
    if (!ltsh) {
	TableHeader props;
	props.file = nullptr;
	props.iname = CHR ('L','T','S','H');
	props.off = 0xffffffff;
	props.length = 0;
	props.checksum = 0;
	ltsh = std::make_shared<LtshTable> (m_font->container, props);
    }
    ltsh->setNumGlyphs (m_font->glyph_cnt, true);

    DeviceMetricsProvider dmp (*m_font);
    if (!dmp.calculateLtsh (*ltsh, this)) {
	ltsh->packData ();
	TableViewModel *modptr = dynamic_cast<TableViewModel *> (m_model.get ());
	modptr->updateViews (ltsh);
    } else {
        // unpackData already includes a check for is_new
	ltsh->unpackData (nullptr);
    }
}

void TableView::genVdmxTable () {
    std::shared_ptr<VdmxTable> vdmx =
	std::dynamic_pointer_cast<VdmxTable> (m_font->sharedTable (CHR ('V','D','M','X')));
    if (!vdmx) {
	TableHeader props;
	props.file = nullptr;
	props.iname = CHR ('V','D','M','X');
	props.off = 0xffffffff;
	props.length = 0;
	props.checksum = 0;
	vdmx = std::make_shared<VdmxTable> (m_font->container, props);

	vdmx->addRatio (1, 1, 1);
	vdmx->setRatioRange (0, 8, 255);
	vdmx->addRatio (2, 1, 1);
	vdmx->setRatioRange (1, 8, 255);
    }
    // sequence:: editTable calls hdmx->edit and connects TableEdit::update to updateViews;
    // if user canceled, the table is restored in HdmxEdit::save;
    // updateViews inserts the table into the model or deletes it, if necessary
    editTable (vdmx, false);
}

void TableView::doubleClickHandler (const QModelIndex &index) {
    int row = index.row ();
    editTable (row, false);
}

void TableView::updateSelection (int row) {
    selectRow (row);
}

QUndoStack *TableView::undoStack () {
    return m_ustack;
}

int TableView::getSelectionIndex () {
    return (selectionModel ()->hasSelection ()) ?
        selectionModel ()->selectedRows ().at (0).row () : -1;
}

AddOrRemoveTableCommand::AddOrRemoveTableCommand (TableViewModel *model, sFont *fnt, int row) :
    m_model (model), m_font (fnt), m_row (row), m_remove (true) {

    // Remove table: row number should be less than the number of tables in the font
    Q_ASSERT (m_row >= 0 && m_row < m_font->tableCount ());
    m_table = m_font->tbls[m_row]->serialize ();
}

AddOrRemoveTableCommand::AddOrRemoveTableCommand (TableViewModel *model, sFont *fnt, FontTable *tbl, int row) :
    m_model (model), m_font (fnt), m_row (row), m_remove (false) {

    // Insert table: row number may be equal to the number of tables in the font
    // (in case we are appending a table)
    Q_ASSERT (m_row >= 0 && m_row <= m_font->tableCount ());
    m_table = tbl->serialize ();
}

void AddOrRemoveTableCommand::redo () {
    if (m_remove)
	m_model->removeRow (m_row, QModelIndex ());
    else {
	std::shared_ptr<FontTable> tptr = std::make_shared<FontTable> (m_table);
	tptr->setModified (true);
	tptr->setContainer (m_font->container);
	m_model->insertTable (m_row, tptr);
    }
}

void AddOrRemoveTableCommand::undo () {
    if (m_remove) {
	std::shared_ptr<FontTable> tptr = std::make_shared<FontTable> (m_table);
	m_model->insertTable (m_row, tptr);
    } else
	m_model->removeRow (m_row, QModelIndex ());
}

PasteTableCommand::PasteTableCommand (TableViewModel *model, sFont *fnt, FontTable *table, int row) :
    m_model (model), m_font (fnt), m_row (row) {

    m_new = table->serialize ();
    // Replace table: row number should be less than the number of tables in the font
    Q_ASSERT (row >= 0 && row < m_font->tableCount ());
    m_old = m_font->tbls[m_row]->serialize ();
}

void PasteTableCommand::redo () {
    FontTable *tbl = new FontTable (m_new);
    tbl->setModified (true);
    tbl->setContainer (m_font->container);
    m_model->pasteTable (m_row, tbl);
}

void PasteTableCommand::undo () {
    FontTable *tbl = new FontTable (m_old);
    m_model->pasteTable (m_row, tbl);
}
