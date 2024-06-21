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
#include <unicode/uchar.h>

#include "sfnt.h"
#include "editors/cmapedit.h" // also includes tables.h
#include "tables/cmap.h"
#include "tables/glyphnames.h"
#include "editors/unispinbox.h"

#include "fs_notify.h"
#include "commonlists.h"
#include "icuwrapper.h"

// Main class, representing the cmap table editing window

CmapEdit::CmapEdit (std::shared_ptr<FontTable> tptr, sFont *fnt, QWidget *parent) :
    TableEdit (parent, Qt::Window), m_font (fnt) {
    uint16_t i;

    setAttribute (Qt::WA_DeleteOnClose);
    setWindowTitle (QString("cmap - ").append (m_font->fontname));

    m_cmap = std::dynamic_pointer_cast<CmapTable> (tptr);
    m_gnp = std::unique_ptr<GlyphNameProvider> (new GlyphNameProvider (*m_font));
    m_uGroup = std::unique_ptr<QUndoGroup> (new QUndoGroup (this));

    QWidget *window = new QWidget (this);

    m_maptab = new QTabWidget (window);
    m_tabtab = new QTableView (m_maptab);
    m_enctab = new QTabWidget (m_maptab);
    m_model = std::unique_ptr<GidListModel> (new GidListModel (m_font, false, m_enctab));
    m_model8 = std::unique_ptr<GidListModel> (new GidListModel (m_font, true, m_enctab));

    m_maptab->addTab (m_tabtab, QWidget::tr ("&Tables"));
    m_maptab->addTab (m_enctab, QWidget::tr ("&Encoding Subtables"));
    m_tabtab->setContextMenuPolicy (Qt::CustomContextMenu);

    setMenuBar ();
    connect (m_maptab, &QTabWidget::currentChanged, this, &CmapEdit::onTabChange);
    connect (m_enctab->tabBar (), &QTabBar::tabMoved, this, &CmapEdit::changeSubTableOrder);
    connect (m_enctab, &QTabWidget::currentChanged, this, &CmapEdit::onEncTabChange);
    connect (m_tabtab, &QTableView::customContextMenuRequested, this, &CmapEdit::onTablesContextMenu);

    saveButton = new QPushButton (QWidget::tr ("&Compile table"));
    removeButton = new QPushButton (QWidget::tr ("&Remove record"));
    addButton = new QPushButton (QWidget::tr ("&Add record"));
    closeButton = new QPushButton (QWidget::tr ("C&lose"));

    saveButton->setEnabled (false);
    connect (saveButton, &QPushButton::clicked, this, &CmapEdit::save);
    connect (addButton, &QPushButton::clicked, this, &CmapEdit::addEncodingRecord);
    connect (removeButton, &QPushButton::clicked, this, &CmapEdit::removeEncodingRecord);
    connect (closeButton, &QPushButton::clicked, this, &CmapEdit::close);

    QVBoxLayout *layout;
    layout = new QVBoxLayout ();
    layout->addWidget (m_maptab);

    QHBoxLayout *buttLayout;
    buttLayout = new QHBoxLayout ();
    buttLayout->addWidget (saveButton);
    buttLayout->addWidget (addButton);
    buttLayout->addWidget (removeButton);
    buttLayout->addWidget (closeButton);
    layout->addLayout (buttLayout);

    window->setLayout (layout);
    setCentralWidget (window);

    fillTables ();
    m_enctab->setTabsClosable (true);
    m_enctab->setMovable (true);
    connect (m_enctab, &QTabWidget::tabCloseRequested, this, &CmapEdit::removeSubTable);
    for (i=0; i<m_cmap->numSubTables (); i++) {
	CmapEnc *cur_enc = m_cmap->getSubTable (i);
	fillSubTable (cur_enc);
    }

    setTablesModified (m_cmap->tablesModified ());
    setSubTablesModified (m_cmap->subTablesModified ());

    m_uStackMap[m_tabtab]->setActive ();
    m_valid = true;
}

CmapEdit::~CmapEdit () {
}

bool CmapEdit::checkUpdate (bool can_cancel) {
    if (isModified ()) {
        QMessageBox::StandardButton ask;
        ask = QMessageBox::question (this,
            tr ("Unsaved Changes"),
            tr ("This table has been modified. "
                "Would you like to export the changes back into the font?"),
            can_cancel ?  (QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel) :
                          (QMessageBox::Yes|QMessageBox::No));
        if (ask == QMessageBox::Cancel) {
            return false;
        } else if (ask == QMessageBox::Yes) {
            save ();
        }
    }
    return true;
}

bool CmapEdit::isModified () {
    return m_cmap->tablesModified () || m_cmap->subTablesModified ();
}

bool CmapEdit::isValid () {
    return m_valid;
}

std::shared_ptr<FontTable> CmapEdit::table () {
    return m_cmap;
}

void CmapEdit::closeEvent (QCloseEvent *event) {
    // If we are going to delete the font, ignore changes in table edits
    if (!isModified () || checkUpdate (true)) {
        m_cmap->clearEditor ();
    } else {
        event->ignore ();
    }
}

QSize CmapEdit::minimumSize () const {
    QSize size = m_tabtab->size ();

    size += QSize (2, 2);
    return size;
}

QSize CmapEdit::sizeHint () const {
    return minimumSize ();
}

void CmapEdit::save () {
    uint16_t i, j, cnt=0;

    for (i=0; i<m_cmap->numSubTables (); i++) {
	CmapEnc *sub = m_cmap->getSubTable (i);
	for (j=0; j<m_cmap->numTables (); j++) {
	    if (m_cmap->getTable (j)->subtable () == sub) {
		cnt++;
		break;
	    }
	}
    }
    if (cnt < m_cmap->numSubTables ()) {
        FontShepherd::postError (
	    QCoreApplication::tr ("Can't compile table"),
	    QCoreApplication::tr (
	    "There are one or more subtables not linked to encoding records. "
	    "Please link or delete them first."),
	    this);
	return;
    }

    m_cmap->packData ();
    setTablesModified (false);
    setSubTablesModified (false);

    for (auto w: m_uStackMap.keys ())
	m_uStackMap[w]->setClean ();
    updateSubTableLabels ();
    emit (update (m_cmap));
}

void CmapEdit::removeEncodingRecord () {
    QItemSelectionModel *curidx = m_tabtab->selectionModel ();
    QModelIndex rowidx = curidx->selectedRows ().first ();
    QAbstractItemModel *absmod = m_tabtab->model ();
    CmapTableModel *cmod = qobject_cast<CmapTableModel *> (absmod);

    QUndoStack *us = m_uStackMap[m_tabtab];
    TableRecordCommand *cmd = new TableRecordCommand (cmod, rowidx.row ());
    cmd->setText (tr ("Remove encoding record"));
    us->push (cmd);
}

void CmapEdit::addEncodingRecord () {
    uint16_t platform, specific, subtable;

    AddTableDialog dlg (m_cmap.get (), this);
    switch (dlg.exec ()) {
      case QDialog::Accepted:
	break;
      case QDialog::Rejected:
	return;
    }
    platform = dlg.platform ();
    specific = dlg.specific ();
    subtable = dlg.subtable ();

    QAbstractItemModel *absmod = m_tabtab->model ();
    CmapTableModel *cmod = qobject_cast<CmapTableModel *> (absmod);
    QList<table_record> arg;
    struct table_record rec = {platform, specific, subtable};
    arg << rec;

    QUndoStack *us = m_uStackMap[m_tabtab];
    TableRecordCommand *cmd = new TableRecordCommand (cmod, arg);
    cmd->setText (tr ("Add encoding record"));
    us->push (cmd);
}

void CmapEdit::removeSubTable (int idx) {
    uint16_t i;
    CmapEnc *enc = m_cmap->getSubTable ((uint16_t) idx);

    if (enc->isLocked ()) {
        FontShepherd::postError (
	    QCoreApplication::tr ("Can't delete cmap subtable"),
	    QCoreApplication::tr (
	    "This cmap subtable is used by another table editor. "
	    "Please close other table editors before attempting to delete it."),
	    this);
	return;
    }

    if (m_cmap->numSubTables () == 1) {
        FontShepherd::postError (
	    QCoreApplication::tr ("Can't delete cmap subtable"),
	    QCoreApplication::tr (
	    "This is the only remaining cmap subtable in the font. "
	    "Please create more before deleting this one."),
	    this);
	return;
    }

    for (i=0; i<m_cmap->numTables (); i++) {
	CmapEncTable *tab = m_cmap->getTable (i);
	if (tab->subtable () == enc) {
	    FontShepherd::postError (
		QCoreApplication::tr ("Can't delete cmap subtable"),
		QCoreApplication::tr (
		"Can't delete a cmap subtable linked to an encoding record. "
		"Please unlink it (or delete the encoding record) first."),
		this);
	    return;
	}
    }

    if (enc->isUnicode () && enc->numBits () == 32) {
	for (i=0; i<m_cmap->numSubTables (); i++) {
	    CmapEnc *test = m_cmap->getSubTable (i);
	    if (test != enc && test->isUnicode () && test->numBits () == 32)
		break;
	}
	if (i==m_cmap->numSubTables ()) {
	    int choice = FontShepherd::postYesNoQuestion (
		QCoreApplication::tr ("Deleting cmap subtable"),
		QCoreApplication::tr (
		"Are you sure you want to delete the only currently available "
		"32-bit Unicode subtable from this font? "
		"This operation cannot be undone!"),
		this);
	    if (choice == QMessageBox::No)
		return;
	}
    } else if (enc->isUnicode ()) {
	for (i=0; i<m_cmap->numSubTables (); i++) {
	    CmapEnc *test = m_cmap->getSubTable (i);
	    if (test != enc && test->isUnicode ())
		break;
	}
	if (i==m_cmap->numSubTables ()) {
	    int choice = FontShepherd::postYesNoQuestion (
		QCoreApplication::tr ("Deleting cmap subtable"),
		QCoreApplication::tr (
		"Are you sure you want to delete the only currently available "
		"Unicode subtable from this font? "
		"This operation cannot be undone!"),
		this);
	    if (choice == QMessageBox::No)
		return;
	}
    } else {
        int choice = FontShepherd::postYesNoQuestion (
	    QCoreApplication::tr ("Deleting cmap subtable"),
	    QCoreApplication::tr (
	    "Are you sure you want to delete the selected subtable? "
	    "This operation cannot be undone!"),
	    this);
        if (choice == QMessageBox::No)
	    return;
    }
    m_enctab->removeTab (idx);
    m_model_storage.erase (m_model_storage.begin () + idx);
    m_cmap->removeSubTable (idx, m_font);
    setSubTablesModified (true);
    updateSubTableLabels ();
}

void CmapEdit::removeSelectedSubTable () {
    int idx = m_enctab->currentIndex ();
    removeSubTable (idx);
}

void CmapEdit::addSubTable () {
    QString encoding;
    GlyphNameProvider *gnp=nullptr;
    CmapEnc *newenc;
    std::map<std::string, int> args;

    AddSubTableDialog dlg (m_cmap.get (), m_gnp->fontHasGlyphNames (), this);
    switch (dlg.exec ()) {
      case QDialog::Accepted:
	break;
      case QDialog::Rejected:
	return;
    }
    args["format"] = dlg.format ();
    args["language"] = dlg.language ();
    args["source"] = dlg.source ();
    args["minimum"] = dlg.minCode ();
    args["maximum"] = dlg.maxCode ();
    encoding = dlg.encoding ();
    if (args["source"] == m_cmap->numSubTables ())
	gnp = m_gnp.get ();

    newenc = m_cmap->addSubTable (args, encoding.toStdString (), gnp);
    fillSubTable (newenc);
    QString title = m_enctab->tabText (m_enctab->count () - 1);
    m_enctab->setTabText (m_enctab->count () - 1, title.prepend ('*'));
    m_enctab->setCurrentIndex (m_enctab->count () - 1);
    setSubTablesModified (true);
}

void CmapEdit::removeSubTableMapping () {
    QWidget *w = m_enctab->currentWidget ();
    if (w) {
	QTableView *tv = qobject_cast<QTableView*> (w);
	QItemSelectionModel *curidx = tv->selectionModel ();
	QModelIndex rowidx = curidx->selectedRows ().first ();
	QAbstractItemModel *absmod = tv->model ();
	EncSubModel *cmod = qobject_cast<EncSubModel *> (absmod);
	QUndoStack *us = m_uStackMap[tv];

	MappingCommand *cmd = new MappingCommand (cmod, rowidx.row (), curidx->selectedRows ().size ());
	cmd->setText (tr ("Delete Mapping"));
	us->push (cmd);
	setSubTablesModified (true);
    }
}

void CmapEdit::addSubTableMapping () {
    int idx = m_enctab->currentIndex ();
    QWidget *w = m_enctab->currentWidget ();
    CmapEnc *enc = m_cmap->getSubTable (idx);
    uint32_t code;
    uint16_t gid;
    int pos;

    AddMappingDialog dlg (enc, m_model.get (), this);
    switch (dlg.exec ()) {
      case QDialog::Accepted:
	break;
      case QDialog::Rejected:
	return;
    }
    code = dlg.code ();
    gid = dlg.gid ();
    pos = enc->codeAvailable (code);
    if (w && pos >= 0) {
	QTableView *tv = qobject_cast<QTableView*> (w);
	QAbstractItemModel *amod = tv->model ();
	EncSubModel *cmod = qobject_cast<EncSubModel *> (amod);
	QList<struct enc_mapping> arg;
	struct enc_mapping m {code, gid};
	arg << m;
	QUndoStack *us = m_uStackMap[tv];

	MappingCommand *cmd = new MappingCommand (cmod, arg, pos);
	cmd->setText (tr ("Add Mapping"));
	us->push (cmd);
	setSubTablesModified (true);
    }
}

void CmapEdit::removeSubTableRange () {
    QWidget *w = m_enctab->currentWidget ();
    if (w) {
	QTableView *tv = qobject_cast<QTableView*> (w);
	QItemSelectionModel *curidx = tv->selectionModel ();
	QModelIndex rowidx = curidx->selectedRows ().first ();
	QAbstractItemModel *amod = tv->model ();
	Enc13SubModel *cmod = qobject_cast<Enc13SubModel *> (amod);
	QUndoStack *us = m_uStackMap[tv];

	RangeCommand *cmd = new RangeCommand (cmod, rowidx.row (), curidx->selectedRows ().size ());
	cmd->setText (tr ("Delete Range"));
	us->push (cmd);
	setSubTablesModified (true);
    }
}

void CmapEdit::addSubTableRange () {
    int idx = m_enctab->currentIndex ();
    QWidget *w = m_enctab->currentWidget ();
    CmapEnc *enc = m_cmap->getSubTable (idx);
    int pos;
    enc_range rng;

    pos = enc->firstAvailableRange (&rng.first_enc, &rng.length);
    if (pos < 0) {
        FontShepherd::postError (
	    QCoreApplication::tr ("Can't add mapping range"),
	    QCoreApplication::tr (
		"All unicode values are already mapped to glyphs."),
	    this);
        return;
    }

    AddRangeDialog dlg (enc, rng, m_model.get (), this);
    switch (dlg.exec ()) {
      case QDialog::Accepted:
	break;
      case QDialog::Rejected:
	return;
    }
    rng.first_enc = dlg.firstCode ();
    rng.length = dlg.lastCode () - rng.first_enc + 1;
    rng.first_gid = dlg.gid ();
    pos = enc->rangeAvailable (rng.first_enc, rng.length);
    if (w && pos >= 0) {
	QTableView *tv = qobject_cast<QTableView*> (w);
	QAbstractItemModel *amod = tv->model ();
	Enc13SubModel *cmod = qobject_cast<Enc13SubModel *> (amod);
	QList<struct enc_range> arg;
	arg << rng;
	QUndoStack *us = m_uStackMap[tv];

	RangeCommand *cmd = new RangeCommand (cmod, arg, pos);
	cmd->setText (tr ("Add Range"));
	us->push (cmd);
	setSubTablesModified (true);
    }
}

void CmapEdit::removeVariationSequence () {
    QWidget *w = m_enctab->currentWidget ();
    int idx = m_enctab->currentIndex ();
    CmapEnc *enc = m_cmap->getSubTable (idx);
    if (enc->format () != 14) {
        FontShepherd::postError (
	    QCoreApplication::tr ("Can't access variation selectors"),
	    QCoreApplication::tr (
		"Oops! Something is wrong. Expected subtable format 14, got %1.").arg (enc->format ()),
	    this);
        return;
    }
    if (w) {
	QTreeView *tv = qobject_cast<QTreeView *> (w);
	QAbstractItemModel *amod = tv->model ();
	VarSelectorModel *vmod = qobject_cast<VarSelectorModel *> (amod);
	QModelIndex item_idx = tv->currentIndex ();
	QUndoStack *us = m_uStackMap[tv];

	VariationCommand *cmd = new VariationCommand (vmod, item_idx.parent (), item_idx.row (), 1);
	cmd->setText (deleteVarSequenceAction->text ());
	us->push (cmd);
	setSubTablesModified (true);
    }
}

void CmapEdit::addVariationSequence () {
    int idx = m_enctab->currentIndex ();
    QWidget *w = m_enctab->currentWidget ();
    CmapEnc *enc = m_cmap->getSubTable (idx);
    if (enc->format () != 14) {
        FontShepherd::postError (
	    QCoreApplication::tr ("Can't access variation selectors"),
	    QCoreApplication::tr (
		"Oops! Something is wrong. Expected subtable format 14, got %1.").arg (enc->format ()),
	    this);
        return;
    }

    if (w) {
	QTreeView *tv = qobject_cast<QTreeView *> (w);
	QAbstractItemModel *model = tv->model ();
	VarSelectorModel *vmod = qobject_cast<VarSelectorModel *> (model);
	QModelIndex item_idx = tv->currentIndex ();
	QUndoStack *us = m_uStackMap[tv];

	AddVariationDialog dlg (enc, m_model.get (), this);
	dlg.init (item_idx);

	switch (dlg.exec ()) {
	  case QDialog::Accepted:
	    break;
	  case QDialog::Rejected:
	    return;
	}

	QList<struct uni_variation> arg;
	struct uni_variation var;
	var.selector = dlg.selector ();
	var.is_dflt = dlg.isDefault ();
	var.unicode = dlg.code ();
	var.gid = dlg.gid ();
	arg << var;

	VariationCommand *cmd = new VariationCommand (vmod, arg);
	cmd->setText (tr ("Add Unicode Variation Sequence"));
	us->push (cmd);
	setSubTablesModified (true);
    }
}

void CmapEdit::onTabChange (int index) {
    QUndoStack *us;
    switch (index) {
      case 0:
	addButton->setText (QWidget::tr ("&Add record"));
	disconnect (addButton, &QPushButton::clicked, this, &CmapEdit::addSubTable);
	connect (addButton, &QPushButton::clicked, this, &CmapEdit::addEncodingRecord);
	removeButton->setText (QWidget::tr ("&Remove record"));
	disconnect (removeButton, &QPushButton::clicked, this, &CmapEdit::removeSelectedSubTable);
	connect (removeButton, &QPushButton::clicked, this, &CmapEdit::removeEncodingRecord);

	addAction->setText (QWidget::tr ("&Add encoding record"));
	disconnect (addAction, &QAction::triggered, this, &CmapEdit::addSubTable);
	connect (addAction, &QAction::triggered, this, &CmapEdit::addEncodingRecord);
	removeAction->setText (QWidget::tr ("&Remove encoding record"));
	disconnect (removeAction, &QAction::triggered, this, &CmapEdit::removeSelectedSubTable);
	connect (removeAction, &QAction::triggered, this, &CmapEdit::removeEncodingRecord);

        addMappingAction->setVisible (false);
        deleteMappingAction->setVisible (false);
        addRangeAction->setVisible (false);
        deleteRangeAction->setVisible (false);

	us = m_uStackMap[m_tabtab];
	us->setActive ();
	break;
      case 1:
	addButton->setText (QWidget::tr ("&Add Subtable"));
	disconnect (addButton, &QPushButton::clicked, this, &CmapEdit::addEncodingRecord);
	connect (addButton, &QPushButton::clicked, this, &CmapEdit::addSubTable);
	removeButton->setText (QWidget::tr ("&Remove Subtable"));
	disconnect (removeButton, &QPushButton::clicked, this, &CmapEdit::removeEncodingRecord);
	connect (removeButton, &QPushButton::clicked, this, &CmapEdit::removeSelectedSubTable);

	addAction->setText (QWidget::tr ("&Add subtable"));
	disconnect (addAction, &QAction::triggered, this, &CmapEdit::addEncodingRecord);
	connect (addAction, &QAction::triggered, this, &CmapEdit::addSubTable);
	removeAction->setText (QWidget::tr ("&Remove subtable"));
	disconnect (removeAction, &QAction::triggered, this, &CmapEdit::removeEncodingRecord);
	connect (removeAction, &QAction::triggered, this, &CmapEdit::removeSelectedSubTable);

	// to show/hide proper actions
	onEncTabChange (m_enctab->currentIndex ());
	break;
    }
}

void CmapEdit::onEncTabChange (int index) {
    CmapEnc *cur = m_cmap->getSubTable (index);
    QWidget *w = m_enctab->currentWidget ();
    QUndoStack *us = m_uStackMap[w];

    us->setActive ();

    deleteMappingAction->setEnabled (cur->format () > 0);
    addMappingAction->setEnabled (cur->format () > 0);
}

void CmapEdit::setTablesModified (bool val) {
    QString title = m_maptab->tabText (0);
    bool has_asterisk = title.startsWith (QChar ('*'));

    if (has_asterisk && !val) {
        m_maptab->setTabText (0, title.remove (0, 1));
    } else if (!has_asterisk && val) {
        m_maptab->setTabText (0, title.prepend ('*'));
    }
    saveButton->setEnabled (this->isModified ());
}

void CmapEdit::setTablesClean (bool clean) {
    m_cmap->setTablesModified (!clean);
    setTablesModified (!clean);
}

void CmapEdit::setSubTablesModified (bool val) {
    QString title = m_maptab->tabText (1);
    bool has_asterisk = title.startsWith (QChar ('*'));

    if (has_asterisk && !val) {
        m_maptab->setTabText (1, title.remove (0, 1));
    } else if (!has_asterisk && val) {
        m_maptab->setTabText (1, title.prepend ('*'));
    }
    m_cmap->setSubTablesModified (val);
    saveButton->setEnabled (this->isModified ());
}

void CmapEdit::updateSubTableLabel (int index) {
    CmapEnc *enc = m_cmap->getSubTable (index);
    m_enctab->setTabText (index, QString::fromStdString (enc->stringName ()));
    if (enc->isModified ())
        m_enctab->setTabText (index, m_enctab->tabText (index).prepend ('*'));
}

void CmapEdit::updateSubTableLabels () {
    int i;
    for (i=0; i<m_enctab->count (); i++)
	updateSubTableLabel (i);
}

void CmapEdit::changeSubTableOrder (int from, int to) {
    m_cmap->reorderSubTables (from, to);
    std::iter_swap (m_model_storage.begin() + from, m_model_storage.begin() + to);
    updateSubTableLabels ();
}

void CmapEdit::updateTableSelection (int row) {
    m_tabtab->selectRow (row);
}

void CmapEdit::updateMappingSelection (uint16_t tab_idx, int row, int count) {
    uint16_t i;
    QWidget *w = m_enctab->widget (tab_idx);
    if (w) {
	QTableView *tv = qobject_cast<QTableView*> (w);
	QModelIndex add_idx = tv->model ()->index (row, 0, QModelIndex ());
	tv->selectionModel ()->clearSelection ();
	tv->scrollTo (add_idx);

	QItemSelection selectedItems = tv->selectionModel ()->selection ();
	for (i=row; i<row+count; i++) {
	    tv->selectRow (i);
	    selectedItems.merge (tv->selectionModel ()->selection (),
		QItemSelectionModel::Select | QItemSelectionModel::Rows);
	}
	tv->selectionModel ()->select (selectedItems, QItemSelectionModel::Select);
    }
}

void CmapEdit::updateVariationSelection (uint16_t tab_idx, int row, int count, QModelIndex parent) {
    uint16_t i;
    QWidget *w = m_enctab->widget (tab_idx);
    if (w) {
	QTreeView *tv = qobject_cast<QTreeView*> (w);
	tv->selectionModel ()->clearSelection ();
	QModelIndex add_idx = tv->model ()->index (row, 0, parent);
	if (parent.isValid ()) {
	    // Supposed to be no more than two iterations
	    QModelIndex curpar = parent;
	    while (curpar.isValid ()) {
		tv->setExpanded (curpar, true);
		curpar = curpar.parent ();
	    }
	}
	tv->scrollTo (add_idx);

	QItemSelection selectedItems = tv->selectionModel ()->selection ();
	for (i=row; i<row+count; i++) {
	    add_idx = tv->model ()->index (i, 0, parent);
	    tv->selectionModel ()->select (add_idx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
	    tv->setCurrentIndex (add_idx);
	    selectedItems.merge (tv->selectionModel ()->selection (),
		QItemSelectionModel::Select | QItemSelectionModel::Rows);
	}
    }
}

// Private methods

void CmapEdit::showEditMenu () {
    int idx = m_maptab->currentIndex ();

    addMappingAction->setVisible (false);
    deleteMappingAction->setVisible (false);
    addRangeAction->setVisible (false);
    deleteRangeAction->setVisible (false);
    addVarSequenceAction->setVisible (false);
    deleteVarSequenceAction->setVisible (false);

    if (idx == 1) {
	QWidget *w = m_enctab->currentWidget ();
	if (w) {
	    QString wtype = w->metaObject ()->className ();
	    if (wtype == "QTableView") {
		QTableView *tv = qobject_cast<QTableView*> (w);
		QAbstractItemModel *cmod = tv->model ();
		QString mtype = cmod->metaObject ()->className ();
		if (mtype == "EncSubModel") {
		    addMappingAction->setVisible (true);
		    deleteMappingAction->setVisible (true);
		} else if (mtype == "Enc13SubModel") {
		    addRangeAction->setVisible (true);
		    deleteRangeAction->setVisible (true);
		}
	    } else if (wtype == "QTreeView") {
		QTreeView *tv = qobject_cast<QTreeView*> (w);
	        QModelIndex index = tv->currentIndex ();
		if (index.isValid ()) {
		    addVarSequenceAction->setVisible (true);
		    deleteVarSequenceAction->setVisible (true);

		    VarSelectorModel::VarSelectorItem* item =
			static_cast<VarSelectorModel::VarSelectorItem*> (index.internalPointer ());
		    setEditMenuTexts (item);
		}
	    }
	}
    }
}

void CmapEdit::onTablesContextMenu (const QPoint &point) {
    QModelIndex index = m_tabtab->indexAt (point);
    if (index.isValid ()) {
	QMenu menu (this);

	menu.addAction (addAction);
	menu.addAction (removeAction);
	menu.addSeparator ();
	menu.addAction (undoAction);
	menu.addAction (redoAction);

	menu.exec (m_tabtab->viewport ()->mapToGlobal (point));
    }
}

void CmapEdit::onMappingsContextMenu (const QPoint &point) {
    QWidget *w = m_enctab->currentWidget ();
    QTableView *tv = qobject_cast<QTableView*> (w);
    QModelIndex index = tv->indexAt (point);
    if (index.isValid ()) {
	QMenu menu (this);

	menu.addAction (addMappingAction);
	menu.addAction (deleteMappingAction);
	menu.addSeparator ();
	menu.addAction (undoAction);
	menu.addAction (redoAction);

	menu.exec (tv->viewport ()->mapToGlobal (point));
    }
}

void CmapEdit::onRangesContextMenu (const QPoint &point) {
    QWidget *w = m_enctab->currentWidget ();
    QTableView *tv = qobject_cast<QTableView*> (w);
    QModelIndex index = tv->indexAt (point);
    if (index.isValid ()) {
	QMenu menu (this);

	menu.addAction (addRangeAction);
	menu.addAction (deleteRangeAction);
	menu.addSeparator ();
	menu.addAction (undoAction);
	menu.addAction (redoAction);

	menu.exec (tv->viewport ()->mapToGlobal (point));
    }
}

void CmapEdit::onVarSelectorsContextMenu (const QPoint &point) {
    QWidget *w = m_enctab->currentWidget ();
    QTreeView *tv = qobject_cast<QTreeView*> (w);
    QModelIndex index = tv->indexAt (point);
    if (index.isValid ()) {
	VarSelectorModel::VarSelectorItem* item =
	    static_cast<VarSelectorModel::VarSelectorItem*> (index.internalPointer ());
	setEditMenuTexts (item);

        addVarSequenceAction->setVisible (true);
        deleteVarSequenceAction->setVisible (true);

	QMenu menu (this);

	menu.addAction (addVarSequenceAction);
	menu.addAction (deleteVarSequenceAction);
	menu.addSeparator ();
	menu.addAction (undoAction);
	menu.addAction (redoAction);

	menu.exec (tv->viewport ()->mapToGlobal (point));
    }
}

static bool existing_encoding (CmapTable *cmap, uint16_t platform, uint16_t specific,
    uint16_t language, QWidget *parent) {
    uint16_t i;

    for (i=0; i < cmap->numTables (); i++) {
	CmapEncTable *tab = cmap->getTable (i);
	if (tab->platform () == platform && tab->specific () == specific &&
	    tab->subtable () && tab->subtable ()->language () == language) {
            FontShepherd::postError (
		QCoreApplication::tr ("Existing encoding record"),
                QCoreApplication::tr (
		    "There is already an encoding record with the same "
		    "platform ID, specific ID and subtable laguage."),
                parent);
	    return true;
	}
    }
    return false;
}

void CmapEdit::setMenuBar () {
    QMenuBar *mb = this->menuBar ();
    QMenu *fileMenu, *editMenu;

    saveAction = new QAction (tr ("&Compile"), this);
    addAction = new QAction (tr ("&Add encoding record"), this);
    removeAction = new QAction (tr ("&Remove encoding record"), this);
    closeAction = new QAction (tr ("C&lose"), this);

    saveAction->setEnabled (false);
    connect (saveAction, &QAction::triggered, this, &CmapEdit::save);
    connect (closeAction, &QAction::triggered, this, &CmapEdit::close);
    connect (addAction, &QAction::triggered, this, &CmapEdit::addEncodingRecord);
    connect (removeAction, &QAction::triggered, this, &CmapEdit::removeEncodingRecord);

    saveAction->setShortcut (QKeySequence::Save);
    closeAction->setShortcut (QKeySequence::Close);

    undoAction = m_uGroup->createUndoAction (this, tr ("&Undo"));
    redoAction = m_uGroup->createRedoAction (this, tr ("Re&do"));
    undoAction->setShortcut (QKeySequence::Undo);
    redoAction->setShortcut (QKeySequence::Redo);

    addMappingAction = new QAction (tr ("&Add mapping"), this);
    deleteMappingAction = new QAction (tr ("&Delete mapping"), this);
    connect (addMappingAction, &QAction::triggered, this, &CmapEdit::addSubTableMapping);
    connect (deleteMappingAction, &QAction::triggered, this, &CmapEdit::removeSubTableMapping);

    addRangeAction = new QAction (tr ("&Add range"), this);
    deleteRangeAction = new QAction (tr ("&Delete range"), this);
    connect (addRangeAction, &QAction::triggered, this, &CmapEdit::addSubTableRange);
    connect (deleteRangeAction, &QAction::triggered, this, &CmapEdit::removeSubTableRange);

    addVarSequenceAction = new QAction (tr ("&Add Unicode Variation Sequence"), this);
    deleteVarSequenceAction = new QAction (tr ("&Delete Unicode Variation Sequence"), this);
    connect (addVarSequenceAction, &QAction::triggered, this, &CmapEdit::addVariationSequence);
    connect (deleteVarSequenceAction, &QAction::triggered, this, &CmapEdit::removeVariationSequence);

    fileMenu = mb->addMenu (tr ("&File"));
    fileMenu->addAction (saveAction);
    fileMenu->addSeparator ();
    fileMenu->addAction (closeAction);

    editMenu = mb->addMenu (tr ("&Edit"));
    editMenu->addAction (addAction);
    editMenu->addAction (removeAction);
    editMenu->addAction (addMappingAction);
    editMenu->addAction (deleteMappingAction);
    editMenu->addAction (addRangeAction);
    editMenu->addAction (deleteRangeAction);
    editMenu->addAction (addVarSequenceAction);
    editMenu->addAction (deleteVarSequenceAction);
    editMenu->addSeparator ();
    editMenu->addAction (undoAction);
    editMenu->addAction (redoAction);
    connect (editMenu, &QMenu::aboutToShow, this, &CmapEdit::showEditMenu);
}

void CmapEdit::fillTables () {
    QUndoStack *us = new QUndoStack (m_uGroup.get ());
    m_uStackMap.insert (m_tabtab, us);

    QAbstractItemDelegate *dlg = new SubtableSelectorDelegate (m_cmap.get (), us, m_tabtab);
    std::unique_ptr<QAbstractItemModel> model (new CmapTableModel (m_cmap.get (), m_tabtab));
    m_model_storage.push_back (std::move (model));
    CmapTableModel *modptr = qobject_cast<CmapTableModel *> (m_model_storage.back ().get ());

    connect (us, &QUndoStack::cleanChanged, this, &CmapEdit::setTablesClean);
    connect (modptr, &CmapTableModel::needsSelectionUpdate, this, &CmapEdit::updateTableSelection);
    m_tabtab->setModel (modptr);
    m_tabtab->setItemDelegateForColumn (2, dlg);

    QFontMetrics fm = m_tabtab->fontMetrics ();
    m_tabtab->setColumnWidth (0, fm.boundingRect ("~~1: Macintosh~~").width ());
    m_tabtab->setColumnWidth (1, fm.boundingRect ("~~2: ISO 10646 1993 semantics~~").width ());
    m_tabtab->setColumnWidth (2, fm.boundingRect ("~~00: language 00, format 00~~").width ());
    m_tabtab->horizontalHeader ()->setStretchLastSection (true);
    // Add some amount to the calculated value, as otherwise the viewport isn't
    // extended to the full table width (seems to be a QT bug)
    m_tabtab->setMinimumWidth (m_tabtab->horizontalHeader ()->length() + 24);

    m_tabtab->setSelectionBehavior (QAbstractItemView::SelectRows);
    m_tabtab->setSelectionMode (QAbstractItemView::SingleSelection);
    m_tabtab->resize (m_tabtab->width (), m_tabtab->rowHeight (0) * 10);
    m_tabtab->selectRow (0);
}

void CmapEdit::showStandard (QTabWidget *tab, CmapEnc *sub, GidListModel *lmodel) {
    QStringList headers;
    QTableView *enc_view = new QTableView (tab);
    QUndoStack *us = new QUndoStack (m_uGroup.get ());

    m_uStackMap.insert (enc_view, us);

    tab->addTab (enc_view, QString::fromStdString (sub->stringName ()));
    QAbstractItemDelegate *dlg = new ComboDelegate (lmodel, us, enc_view);
    enc_view->setItemDelegateForColumn (1, dlg);

    std::unique_ptr<QAbstractItemModel> tmod (new EncSubModel (sub, lmodel, this));
    m_model_storage.push_back (std::move (tmod));
    EncSubModel *modptr = qobject_cast<EncSubModel *> (m_model_storage.back ().get ());
    connect (us, &QUndoStack::cleanChanged, modptr, &EncSubModel::setSubTableModified);
    connect (modptr, &EncSubModel::needsLabelUpdate, this, &CmapEdit::updateSubTableLabel);
    connect (modptr, &EncSubModel::needsSelectionUpdate, this, &CmapEdit::updateMappingSelection);
    enc_view->setModel (modptr);

    enc_view->horizontalHeader ()->setSectionResizeMode (QHeaderView::Stretch);
    enc_view->setSelectionBehavior (QAbstractItemView::SelectRows);
    enc_view->setSelectionMode (QAbstractItemView::ContiguousSelection);
    enc_view->selectRow (0);

    enc_view->setContextMenuPolicy (Qt::CustomContextMenu);
    connect (enc_view, &QTableView::customContextMenuRequested, this, &CmapEdit::onMappingsContextMenu);
}

void CmapEdit::showRanges13 (QTabWidget *tab, CmapEnc *sub) {
    QStringList headers;
    QTableView *enc_view = new QTableView (tab);
    QUndoStack *us = new QUndoStack (m_uGroup.get ());

    m_uStackMap.insert (enc_view, us);

    tab->addTab (enc_view, QString::fromStdString (sub->stringName ()));
    QAbstractItemDelegate *dlg = new UnicodeDelegate (us, enc_view);
    enc_view->setItemDelegateForColumn (1, dlg);
    enc_view->setItemDelegateForColumn (2, dlg);

    std::unique_ptr<QAbstractItemModel> tmod (new Enc13SubModel (sub, m_gnp.get (), this));
    m_model_storage.push_back (std::move (tmod));
    Enc13SubModel *modptr = qobject_cast<Enc13SubModel *> (m_model_storage.back ().get ());
    connect (us, &QUndoStack::cleanChanged, modptr, &Enc13SubModel::setSubTableModified);
    connect (modptr, &Enc13SubModel::needsLabelUpdate, this, &CmapEdit::updateSubTableLabel);
    connect (modptr, &Enc13SubModel::needsSelectionUpdate, this, &CmapEdit::updateMappingSelection);
    enc_view->setModel (modptr);

    enc_view->horizontalHeader ()->setSectionResizeMode (QHeaderView::Stretch);
    enc_view->setSelectionBehavior (QAbstractItemView::SelectRows);
    enc_view->setSelectionMode (QAbstractItemView::ContiguousSelection);
    enc_view->selectRow (0);

    enc_view->setContextMenuPolicy (Qt::CustomContextMenu);
    connect (enc_view, &QTableView::customContextMenuRequested, this, &CmapEdit::onRangesContextMenu);
}

void CmapEdit::showVariations (QTabWidget *tab, CmapEnc *sub, GidListModel *model) {
    QStringList headers;
    QTreeView *enc_sub_tree = new QTreeView (tab);
    QUndoStack *us = new QUndoStack (m_uGroup.get ());

    m_uStackMap.insert (enc_sub_tree, us);

    tab->addTab (enc_sub_tree, QString::fromStdString (sub->stringName ()));
    enc_sub_tree->header ()->setSectionResizeMode (QHeaderView::Stretch);
    enc_sub_tree->setHeaderHidden (true);
    enc_sub_tree->setSelectionBehavior (QAbstractItemView::SelectRows);
    //enc_sub_tree->setSelectionMode (QAbstractItemView::ContiguousSelection);

    QAbstractItemDelegate *dlg = new ComboDelegate (model, us, enc_sub_tree);
    enc_sub_tree->setItemDelegateForColumn (1, dlg);

    std::unique_ptr<QAbstractItemModel> tmod (new VarSelectorModel (sub, model, this));
    m_model_storage.push_back (std::move (tmod));
    VarSelectorModel *modptr = qobject_cast<VarSelectorModel *> (m_model_storage.back ().get ());
    connect (us, &QUndoStack::cleanChanged, modptr, &VarSelectorModel::setSubTableModified);
    connect (modptr, &VarSelectorModel::needsLabelUpdate, this, &CmapEdit::updateSubTableLabel);
    connect (modptr, &VarSelectorModel::needsSelectionUpdate, this, &CmapEdit::updateVariationSelection);
    enc_sub_tree->setModel (modptr);

    enc_sub_tree->setContextMenuPolicy (Qt::CustomContextMenu);
    connect (enc_sub_tree, &QTreeWidget::customContextMenuRequested, this, &CmapEdit::onVarSelectorsContextMenu);
}

void CmapEdit::fillSubTable (CmapEnc *cur_enc) {
    switch (cur_enc->format ()) {
      case 0:
	showStandard (m_enctab, cur_enc, m_model8.get ());
        break;
      case 2:
      case 4:
      case 6:
      case 10:
      case 12:
	showStandard (m_enctab, cur_enc, m_model.get ());
        break;
      case 13:
	showRanges13 (m_enctab, cur_enc);
        break;
      case 14:
	showVariations (m_enctab, cur_enc, m_model.get ());
	break;
    }
}

void CmapEdit::setEditMenuTexts (VarSelectorModel::VarSelectorItem* item) {
    switch (item->type ()) {
      case em_varSelector:
        deleteVarSequenceAction->setText ("&Delete Unicode Variation Selector record");
        break;
      case em_uvsDefaultGroup:
        deleteVarSequenceAction->setText ("&Delete default UVS list");
        break;
      case em_uvsNonDefaultGroup:
        deleteVarSequenceAction->setText ("&Delete non-default UVS list");
        break;
      case em_uvsDefaultRecord:
        deleteVarSequenceAction->setText ("&Delete default Unicode Variation Sequence");
        break;
      case em_uvsNonDefaultRecord:
        deleteVarSequenceAction->setText ("&Delete non-default Unicode Variation Sequence");
        break;
    }
}

// Custom dialogs used to add various types of tables, subtables and mappings

static bool valid_format (uint16_t platform, uint16_t specific, CmapEnc *sub, QWidget *parent) {
    switch (sub->format ()) {
      case 0:
	break;
      case 2:
	if (CmapEncTable::isCJK (platform, specific) || (platform == plt_mac && specific == 0))
	    ;
	else {
            FontShepherd::postError (
		QCoreApplication::tr ("Incorrect subtable format"),
                QCoreApplication::tr (
		    "Subtable Format 2 is for CJK encodings only. "
		    "It is not compatible with Unicode or 8 bit codepages."),
                parent);
	    return false;
	}
	break;
      case 4:
      case 6:
	if ((platform == plt_unicode && specific >= 4) ||
	    (platform == plt_ms && specific >= 10)) {
            FontShepherd::postError (
		QCoreApplication::tr ("Incorrect subtable format"),
                QCoreApplication::tr (
		    "This platform ID and specific ID pair assumes "
		    "a 32-bit Unicode encoding, while format %1 subtable "
		    "can be used only for 16-bit Unicode characters.").arg (sub->format ()),
                parent);
	    return false;
	}
	break;
      case 8:
	break;
      case 10:
      case 12:
	if ((platform == plt_unicode && (specific == 4 || specific == 6)) ||
	    (platform == plt_ms && specific >= 10))
	    ;
	else {
            FontShepherd::postError (
		QCoreApplication::tr ("Incorrect subtable format"),
                QCoreApplication::tr (
		    "You need an appropriate platform ID and specific ID "
		    "pair (for example, platform 3 (Microsoft) and specific 10 "
		    "(Unicode UCS-4)) to map this subtable, as subtable "
		    "format %1 is intended for 32-bit Unicode only.").arg (sub->format ()),
                parent);
	    return false;
	}
	break;
      case 13:
	break;
      case 14:
	if (platform != plt_unicode || specific != 5) {
            FontShepherd::postError (
		QCoreApplication::tr ("Incorrect subtable format"),
                QCoreApplication::tr (
		    "Subtable format 14 (Unicode Variation Sequences) "
		    "can only be used with platform ID 0 (Unicode) "
		    "and encoding ID 5."),
                parent);
	    return false;
	}
    }
    return true;
}

AddTableDialog::AddTableDialog (CmapTable *cmap, QWidget *parent) :
    QDialog (parent), m_cmap (cmap) {

    setWindowTitle (tr ("Add encoding record"));

    QVBoxLayout *layout = new QVBoxLayout ();
    QGridLayout *glay = new QGridLayout;
    layout->addLayout (glay);

    glay->addWidget (new QLabel ("Select platform ID"), 0, 0);
    m_platformBox = new QComboBox ();
    glay->addWidget (m_platformBox, 0, 1);

    glay->addWidget (new QLabel ("Select specific ID"), 1, 0);
    m_specificBox = new QComboBox ();
    glay->addWidget (m_specificBox, 1, 1);

    glay->addWidget (new QLabel ("Select subtable"), 2, 0);
    m_subtableBox = new QComboBox ();
    glay->addWidget (m_subtableBox, 2, 1);

    fillBoxes ();

    QHBoxLayout *butt_layout = new QHBoxLayout ();
    QPushButton* okBtn = new QPushButton ("OK");
    connect (okBtn, &QPushButton::clicked, this, &QDialog::accept);
    butt_layout->addWidget (okBtn);

    QPushButton* cancelBtn = new QPushButton (tr ("Cancel"));
    connect (cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    butt_layout->addWidget( cancelBtn );
    layout->addLayout (butt_layout);

    setLayout (layout);
}

int AddTableDialog::platform () const {
    QVariant ret = m_platformBox->itemData (m_platformBox->currentIndex ());
    if (ret != QVariant::Invalid)
	return ret.toInt ();
    return -1;
}

int AddTableDialog::specific () const {
    QVariant ret = m_specificBox->itemData (m_specificBox->currentIndex ());
    if (ret != QVariant::Invalid)
	return ret.toInt ();
    return -1;
}

int AddTableDialog::subtable () const {
    QVariant ret = m_subtableBox->itemData (m_subtableBox->currentIndex ());
    if (ret != QVariant::Invalid)
	return ret.toInt ();
    return -1;
}


void AddTableDialog::accept () {
    uint16_t platform = (uint16_t) this->platform ();
    uint16_t specific = (uint16_t) this->specific ();
    uint16_t enc_idx = (uint16_t) this->subtable ();
    CmapEnc *enc = m_cmap->getSubTable (enc_idx);

    if (!existing_encoding (m_cmap, platform, specific, enc->language (), this) &&
	valid_format (platform, specific, enc, this))
	QDialog::accept ();
}

void AddTableDialog::fillBoxes () {
    uint16_t i;
    QStandardItemModel *model;
    QStandardItem *item;
    const std::vector<FontShepherd::numbered_string> &plat_lst = FontShepherd::platforms;

    for (i=0; i<plat_lst.size (); i++)
	m_platformBox->addItem (QString
	    ("%1: %2").arg (plat_lst[i].ID).arg (plat_lst[i].name.c_str ()), plat_lst[i].ID);
    m_platformBox->setEditable (false);
    model = qobject_cast<QStandardItemModel *> (m_platformBox->model ());
    item = model->item (2);
    item->setFlags (item->flags () & ~Qt::ItemIsEnabled);
    m_platformBox->setCurrentIndex (3);
    connect (m_platformBox, static_cast<void (QComboBox::*)(int)> (&QComboBox::currentIndexChanged),
	this, &AddTableDialog::setSpecificList);

    setSpecificList (3);
    m_specificBox->setCurrentIndex (1);

    for (i=0; i<m_cmap->numSubTables (); i++)
	m_subtableBox->addItem (QString::fromStdString (m_cmap->getSubTable (i)->stringName ()), i);
}

void AddTableDialog::setSpecificList (int plat) {
    const std::vector<FontShepherd::numbered_string> &lst = FontShepherd::specificList (plat);
    QStandardItemModel *model;
    QStandardItem *item;
    uint16_t i;

    m_specificBox->clear ();
    for (i=0; i<lst.size (); i++)
	m_specificBox->addItem (QString
	    ("%1: %2").arg (lst[i].ID).arg (lst[i].name.c_str ()), lst[i].ID);
    m_specificBox->setEditable (false);
    if (plat == 3) { // Microsoft
	model = qobject_cast<QStandardItemModel *> (m_specificBox->model ());
	for (i=7; i<10; i++) {
	    item = model->item (i);
	    item->setFlags (item->flags () & ~Qt::ItemIsEnabled);
	}
    }
    m_specificBox->setCurrentIndex (0);
}

QList<QPair<QString, int>> AddSubTableDialog::formatList = {
    {"Format 0: Byte encoding table", 0},
    {"Format 2: High-byte mapping through table", 2},
    {"Format 4: Segment mapping to delta values", 4},
    {"Format 6: Trimmed table mapping", 6},
    {"Format 10: Trimmed array", 10},
    {"Format 12: Segmented coverage", 12},
    {"Format 13: Many-to-one range mappings", 13},
    {"Format 14: Unicode Variation Sequences", 14}
};

QList<QPair<QString, QString>> AddSubTableDialog::euList = {
    {"Mac OS Roman", "MACINTOSH"},
    {"Mac OS Cyrillic", "MACCYRILLIC"},
    {"Mac OS Ukrainian", "MACUKRAINIAN"},
    {"Mac OS Central European", "MAC-CENTRALEUROPE"},
    {"Windows-1250 (Central European)", "WINDOWS-1250"},
    {"Windows-1251 (Cyrillic)", "WINDOWS-1251"},
    {"Windows-1252 (Western)", "WINDOWS-1252"},
    {"Windows-1253 (Greek)", "WINDOWS-1253"},
    {"Windows-1254 (Turkish)", "WINDOWS-1254"},
    {"Windows-1255 (Hebrew)", "WINDOWS-1255"},
    {"Windows-1256 (Arabic)", "WINDOWS-1256"},
    {"Windows-1257 (Baltic)", "WINDOWS-1257"},
    {"Windows-1258 (Vietnamese)", "WINDOWS-1258"},
    {"Windows Symbol", "SYMBOL"}
};

QList<QPair<QString, QString>> AddSubTableDialog::cjkList = {
    {"Big5", "Big5"},
    {"Big5-HKSCS", "BIG5-HKSCS"},
    {"EUC-KR", "EUC-KR"},
    {"Johab", "JOHAB"},
    {"GB18030", "GB18030"},
    {"Shift-JIS", "SHIFT-JIS"},
    {"Shift-JIS x 2012", "SHIFT_JISX2012"}
};

AddSubTableDialog::AddSubTableDialog (CmapTable *cmap, bool has_glyph_names, QWidget *parent) :
    QDialog (parent), m_cmap (cmap) {
    setWindowTitle (tr ("Add cmap subtable"));

    m_default_enc = 0;

    QVBoxLayout *layout = new QVBoxLayout ();
    QGridLayout *glay = new QGridLayout;
    layout->addLayout (glay);

    glay->addWidget (new QLabel ("Subtable format"), 0, 0);
    glay->addWidget (new QLabel ("Language"), 1, 0);
    glay->addWidget (new QLabel ("Encoding"), 2, 0);
    glay->addWidget (new QLabel ("Minimum code"), 3, 0);
    glay->addWidget (new QLabel ("Maximum code"), 4, 0);
    glay->addWidget (new QLabel ("Take mappings from"), 5, 0);

    m_formatBox = new QComboBox ();
    glay->addWidget (m_formatBox, 0, 1);
    for (int i=0; i<formatList.count (); i++)
	m_formatBox->addItem (formatList[i].first, formatList[i].second);
    m_formatBox->setCurrentIndex (2);
    connect (m_formatBox, static_cast<void (QComboBox::*)(int)> (&QComboBox::currentIndexChanged),
	this, &AddSubTableDialog::fillControls);

    m_languageBox = new QComboBox ();
    for (size_t i=0; i<FontShepherd::macLanguages.size (); i++) {
	const FontShepherd::numbered_string *lang = &FontShepherd::macLanguages[i];
	QString lang_str = QString ("%1: %2").arg (lang->ID).arg (lang->name.c_str ());
	m_languageBox->addItem (lang_str, lang->ID);
    }
    m_languageBox->setCurrentIndex (0);
    glay->addWidget (m_languageBox, 1, 1);

    m_encodingBox = new QComboBox ();
    glay->addWidget (m_encodingBox, 2, 1);
    m_minBox = new UniSpinBox ();
    glay->addWidget (m_minBox, 3, 1);
    m_minBox->setMaximum (0xffffff);
    m_maxBox = new UniSpinBox ();
    glay->addWidget (m_maxBox, 4, 1);
    m_maxBox->setMaximum (0xffffff);
    m_sourceBox = new QComboBox ();
    glay->addWidget (m_sourceBox, 5, 1);

    connect (m_encodingBox, static_cast<void (QComboBox::*)(int)> (&QComboBox::currentIndexChanged),
	this, &AddSubTableDialog::setEncoding);

    for (int i=0; i<m_cmap->numSubTables (); i++) {
	CmapEnc *sub = m_cmap->getSubTable (i);
	m_sourceBox->addItem (QString::fromStdString (sub->stringName ()), i);
	if (sub->isCurrent ()) m_default_enc = i;
    }
    if (has_glyph_names)
	m_sourceBox->addItem ("Glyph names", m_sourceBox->count ());
    m_sourceBox->addItem ("(No source)", -1);

    QHBoxLayout *butt_layout = new QHBoxLayout ();
    QPushButton* okBtn = new QPushButton ("OK");
    connect (okBtn, &QPushButton::clicked, this, &QDialog::accept);
    butt_layout->addWidget( okBtn );

    QPushButton* cancelBtn = new QPushButton (tr ("Cancel"));
    connect (cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    butt_layout->addWidget( cancelBtn );
    layout->addLayout (butt_layout);

    setLayout (layout);
    fillControls (2);
}

int AddSubTableDialog::format () const {
    QVariant ret = m_formatBox->itemData (m_formatBox->currentIndex ());
    if (ret != QVariant::Invalid)
	return ret.toInt ();
    return -1;
}

int AddSubTableDialog::language () const {
    QVariant ret = m_languageBox->itemData (m_languageBox->currentIndex ());
    if (ret != QVariant::Invalid)
	return ret.toInt ();
    return -1;
}

QString AddSubTableDialog::encoding () const {
    QVariant ret = m_encodingBox->itemData (m_encodingBox->currentIndex ());
    if (ret != QVariant::Invalid)
	return ret.toString ();
    return "";
}

int AddSubTableDialog::source () const {
    QVariant ret = m_sourceBox->itemData (m_sourceBox->currentIndex ());
    if (ret != QVariant::Invalid)
	return ret.toInt ();
    return -1;
}

int AddSubTableDialog::minCode () const {
    QVariant ret = m_minBox->value ();
    if (ret != QVariant::Invalid)
	return ret.toInt ();
    return 0;
}

int AddSubTableDialog::maxCode () const {
    QVariant ret = m_maxBox->value ();
    if (ret != QVariant::Invalid)
	return ret.toInt ();
    return 0;
}

void AddSubTableDialog::fillControls (int idx) {
    int i;
    int fmt = m_formatBox->itemData (idx).toInt ();
    m_encodingBox->clear ();
    m_encodingBox->setEnabled (true);
    m_sourceBox->setEnabled (true);
    m_sourceBox->setCurrentIndex (m_default_enc);

    switch (fmt) {
      case 0:
	for (i=0; i<euList.count (); i++)
	    m_encodingBox->addItem (euList[i].first, euList[i].second);
	m_encodingBox->addItem ("(Custom 8 bit)", "CUSTOM");
	m_encodingBox->setCurrentIndex (0);
	m_maxBox->setValue (0xff);
	m_minBox->setEnabled (false);
	m_maxBox->setEnabled (false);
	break;
      case 2:
	for (i=0; i<cjkList.count (); i++)
	    m_encodingBox->addItem (cjkList[i].first, cjkList[i].second);
	m_encodingBox->setCurrentIndex (0);
	m_maxBox->setValue (0xffff);
	m_minBox->setEnabled (false);
	m_maxBox->setEnabled (false);
	break;
      case 4:
	m_encodingBox->addItem ("Unicode 16 bit", "Unicode");
	for (i=0; i<cjkList.count (); i++)
	    m_encodingBox->addItem (cjkList[i].first, cjkList[i].second);
	m_encodingBox->setCurrentIndex (0);
	m_maxBox->setValue (0xffff);
	m_minBox->setEnabled (false);
	m_maxBox->setEnabled (false);
	break;
      case 6:
	m_encodingBox->addItem ("Unicode 16 bit", "Unicode");
	for (i=0; i<euList.count (); i++)
	    m_encodingBox->addItem (euList[i].first, euList[i].second);
	m_encodingBox->addItem ("(Custom 8 bit)", "CUSTOM");
	m_encodingBox->setCurrentIndex (0);
	break;
      case 10:
	m_encodingBox->addItem ("Unicode 32 bit", "Unicode");
	m_encodingBox->setCurrentIndex (0);
	m_encodingBox->setEnabled (false);
	m_maxBox->setValue (0xff);
	m_minBox->setEnabled (true);
	m_maxBox->setEnabled (true);
	break;
      case 12:
	m_encodingBox->addItem ("Unicode 32 bit", "Unicode");
	m_encodingBox->setCurrentIndex (0);
	m_encodingBox->setEnabled (false);
	m_maxBox->setValue (0xffffff);
	m_minBox->setEnabled (false);
	m_maxBox->setEnabled (false);
	break;
      case 13:
      case 14:
	m_encodingBox->addItem ("Unicode 32 bit", "Unicode");
	m_encodingBox->setCurrentIndex (0);
	m_encodingBox->setEnabled (false);
	m_languageBox->setEnabled (false);
	int match = m_sourceBox->findText ("(No source)");
	if (match >= 0)
	    m_sourceBox->setCurrentIndex (match);
	m_sourceBox->setEnabled (false);
	m_maxBox->setValue (0xffffff);
	m_minBox->setEnabled (false);
	m_maxBox->setEnabled (false);
	break;
    }
}

void AddSubTableDialog::setEncoding (int val) {
    QString senc = m_encodingBox->itemText (val);
    m_minBox->setValue (0x0);

    if (senc == "(Custom 8 bit)") {
	int match = m_sourceBox->findText ("(No source)");
	if (match >= 0)
	    m_sourceBox->setCurrentIndex (match);
	m_sourceBox->setEnabled (false);
    } else {
	m_sourceBox->setEnabled (true);
	m_sourceBox->setCurrentIndex (m_default_enc);
    }
    if (format () == 6) {
	if (senc == "Windows Symbol") {
	    m_minBox->setValue (0xf000);
	    m_maxBox->setValue (0xf0ff);
	    m_minBox->setEnabled (false);
	    m_maxBox->setEnabled (false);
	} else {
	    m_maxBox->setValue (0xff);
	    m_minBox->setEnabled (false);
	    m_maxBox->setEnabled (false);
	}
    }
}

AddMappingDialog::AddMappingDialog (CmapEnc *enc, GidListModel *model, QWidget *parent) :
    QDialog (parent), m_enc (enc), m_model (model) {

    setWindowTitle (tr ("Add code to GID mapping"));

    QVBoxLayout *layout = new QVBoxLayout ();
    QGridLayout *glay = new QGridLayout;
    layout->addLayout (glay);

    glay->addWidget (new QLabel (enc->isUnicode () ? "Unicode" : "Encoding"), 0, 0);
    m_codeBox = new UniSpinBox ();
    m_codeBox->setMaximum (m_enc->numBits () == 32 ? 0xffffff : 0xffff);
    m_codeBox->setValue (m_enc->firstAvailableCode ());
    glay->addWidget (m_codeBox, 0, 1);

    glay->addWidget (new QLabel ("GID"), 1, 0);
    m_gidBox = new QComboBox ();
    m_gidBox->setModel (m_model);
    m_gidBox->view ()->setVerticalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    m_gidBox->setCurrentIndex (0);
    glay->addWidget (m_gidBox, 1, 1);

    QHBoxLayout *butt_layout = new QHBoxLayout ();
    QPushButton* okBtn = new QPushButton ("OK");
    connect (okBtn, &QPushButton::clicked, this, &QDialog::accept);
    butt_layout->addWidget (okBtn);

    QPushButton* cancelBtn = new QPushButton (tr ("Cancel"));
    connect (cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    butt_layout->addWidget (cancelBtn);
    layout->addLayout (butt_layout);

    setLayout (layout);
}

uint32_t AddMappingDialog::code () const {
    return (uint32_t) m_codeBox->value ();
}

uint16_t AddMappingDialog::gid () const {
    return (uint16_t) m_gidBox->currentIndex ();
}

void AddMappingDialog::accept () {
    int pos = m_enc->codeAvailable (code ());
    if (pos >= 0)
	QDialog::accept ();
    else {
        FontShepherd::postError (
    	QCoreApplication::tr ("Can't add glyph mapping"),
            QCoreApplication::tr (
    	    "There is already such a code in the given subtable."),
            this);
    }
}

AddRangeDialog::AddRangeDialog (CmapEnc *enc, struct enc_range rng, GidListModel *model, QWidget *parent) :
    QDialog (parent), m_enc (enc), m_model (model) {

    setWindowTitle (tr ("Add range to GID mapping"));

    QVBoxLayout *layout = new QVBoxLayout ();
    QGridLayout *glay = new QGridLayout;
    layout->addLayout (glay);

    glay->addWidget (new QLabel ("Minimum Unicode"), 0, 0);
    m_firstBox = new UniSpinBox ();
    m_firstBox->setMaximum (0xffffff);
    m_firstBox->setValue (rng.first_enc);
    glay->addWidget (m_firstBox, 0, 1);

    glay->addWidget (new QLabel ("Maximum Unicode"), 1, 0);
    m_lastBox = new UniSpinBox ();
    m_lastBox->setMaximum (0xffffff);
    m_lastBox->setValue (rng.first_enc + rng.length - 1);
    glay->addWidget (m_lastBox, 1, 1);

    glay->addWidget (new QLabel ("GID"), 2, 0);
    m_gidBox = new QComboBox ();
    m_gidBox->setModel (m_model);
    m_gidBox->view ()->setVerticalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    m_gidBox->setCurrentIndex (0);
    glay->addWidget (m_gidBox, 2, 1);

    QHBoxLayout *butt_layout = new QHBoxLayout ();
    QPushButton* okBtn = new QPushButton ("OK");
    connect (okBtn, &QPushButton::clicked, this, &QDialog::accept);
    butt_layout->addWidget (okBtn);

    QPushButton* cancelBtn = new QPushButton (tr ("Cancel"));
    connect (cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    butt_layout->addWidget (cancelBtn);
    layout->addLayout (butt_layout);

    setLayout (layout);
}

uint32_t AddRangeDialog::firstCode () const {
    return (uint32_t) m_firstBox->value ();
}

uint32_t AddRangeDialog::lastCode () const {
    return (uint32_t) m_lastBox->value ();
}

uint16_t AddRangeDialog::gid () const {
    return (uint16_t) m_gidBox->currentIndex ();
}

void AddRangeDialog::accept () {
    int pos = m_enc->rangeAvailable (firstCode (), lastCode () - firstCode () + 1);
    if (pos >= 0)
	QDialog::accept ();
    else {
        FontShepherd::postError (
    	QCoreApplication::tr ("Can't add range mapping"),
            QCoreApplication::tr (
    	    "This range intersects with already defined ranges."),
            this);
    }
}

AddVariationDialog::AddVariationDialog (CmapEnc *enc, GidListModel *model, QWidget *parent) :
    QDialog (parent), m_enc (enc), m_model (model) {

    setWindowTitle (tr ("Add Unicode Variation Sequence"));

    QVBoxLayout *layout = new QVBoxLayout ();
    QGridLayout *glay = new QGridLayout;
    layout->addLayout (glay);

    glay->addWidget (new QLabel ("Variation selector"), 0,0, 1,1);
    m_vsBox = new VarSelectorBox ();
    glay->addWidget (m_vsBox, 0,1, 1,1);

    m_defaultBox = new QCheckBox ();
    m_defaultBox->setText (tr ("Default sequence"));
    glay->addWidget (m_defaultBox, 1,0, 1,2);
    connect (m_defaultBox, &QCheckBox::stateChanged, this, &AddVariationDialog::setDefault);

    glay->addWidget (new QLabel ("Unicode"), 2,0, 1,1);
    glay->addWidget (new QLabel ("GID"), 3,0, 1,1);

    m_codeBox = new UniSpinBox ();
    m_codeBox->setMaximum (0xffffff);
    glay->addWidget (m_codeBox, 2,1, 1,1);
    m_gidBox = new QComboBox ();
    m_gidBox->setModel (m_model);
    m_gidBox->view ()->setVerticalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    m_gidBox->setCurrentIndex (0);
    glay->addWidget (m_gidBox, 3,1, 1,1);

    QHBoxLayout *butt_layout = new QHBoxLayout ();
    QPushButton* okBtn = new QPushButton ("OK");
    connect (okBtn, &QPushButton::clicked, this, &QDialog::accept);
    butt_layout->addWidget (okBtn);

    QPushButton* cancelBtn = new QPushButton (tr ("Cancel"));
    connect (cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    butt_layout->addWidget (cancelBtn);
    layout->addLayout (butt_layout);

    setLayout (layout);
}

void AddVariationDialog::init (QModelIndex &index) {
    VarSelectorModel::VarSelectorItem* item = nullptr;
    if (index.isValid ())
	item = static_cast<VarSelectorModel::VarSelectorItem*> (index.internalPointer ());
    if (item) {
	uint32_t selector;
	bool dflt = false;
	switch (item->type ()) {
	  case em_varSelector:
	  case em_uvsDefaultGroup:
	    selector = item->unicode ();
	    dflt = true;
	    break;
	  case em_uvsNonDefaultGroup:
	    selector = item->unicode ();
	    break;
	  case em_uvsDefaultRecord:
	    dflt = true;
	    /* fall through */
	  case em_uvsNonDefaultRecord:
	    selector = item->parent ()->unicode ();
	}
	m_vsBox->setValue (selector);
	m_defaultBox->setChecked (dflt);
    }
}

uint32_t AddVariationDialog::selector () const {
    return (uint32_t) m_vsBox->value ();
}

bool AddVariationDialog::isDefault () const {
    return m_defaultBox->isChecked ();
}

uint32_t AddVariationDialog::code () const {
    return (uint32_t) m_codeBox->value ();
}

uint16_t AddVariationDialog::gid () const {
    return (uint16_t) m_gidBox->currentIndex ();
}

void AddVariationDialog::accept () {
    uint16_t i;
    uint32_t j;
    bool found = false;
    for (i=0; i<m_enc->count (); i++) {
	struct var_selector_record *vsr = m_enc->getVarSelectorRecord (i);
	if (vsr->selector == this->selector ()) {
	    if (this->isDefault ()) {
		for (j=0; j<vsr->default_vars.size (); j++) {
		    if (vsr->default_vars[j] == this->code ()) {
			found = true;
			break;
		    }
		}
	    } else {
		for (j=0; j<vsr->non_default_vars.size (); j++) {
		    if (vsr->non_default_vars[j].code == this->code ()) {
			found = true;
			break;
		    }
		}
	    }
	}
	if (found) break;
    }

    if (!found)
	QDialog::accept ();
    else {
        FontShepherd::postError (
    	QCoreApplication::tr ("Can't add Unicode Variation Sequence"),
            QCoreApplication::tr (
    	    "This Unicode Variation Sequence is already defined."),
            this);
    }
}

void AddVariationDialog::setDefault (int state) {
    m_gidBox->setEnabled (state == Qt::Unchecked);
}

// Custom models, used to display table/subtable data in a table or a tree form

CmapTableModel::CmapTableModel (CmapTable *cmap, QWidget *parent) :
    QAbstractTableModel (parent), m_cmap (cmap), m_parent (parent) {
}

CmapTableModel::~CmapTableModel () {
}

int CmapTableModel::rowCount (const QModelIndex &parent) const {
    Q_UNUSED (parent);
    return m_cmap->numTables ();
}

int CmapTableModel::columnCount (const QModelIndex &parent) const {
    Q_UNUSED (parent);
    return 3;
}

QVariant CmapTableModel::data (const QModelIndex &index, int role) const {
    struct CmapEncTable *tab = m_cmap->getTable (index.row ());

    switch (role) {
      case Qt::ToolTipRole:
      case Qt::DisplayRole:
	switch (index.column ()) {
	  case 0:
	    return QString::fromStdString (tab->strPlatform ());
	  case 1:
	    return QString::fromStdString (tab->strSpecific ());
	  case 2:
	    return QString::fromStdString (tab->subtable ()->stringName ());
	}
	break;
      case Qt::EditRole:
	switch (index.column ()) {
	  case 0:
	    return tab->platform ();
	  case 1:
	    return tab->specific ();
	  case 2:
	    return tab->subtable ()->index ();
	}
    }
    return QVariant ();
}

bool CmapTableModel::setData (const QModelIndex &index, const QVariant &value, int role) {
    CmapEncTable *tab = m_cmap->getTable (index.row ());

    if (index.isValid() && index.column () == 2) {
	if (role == Qt::EditRole) {
	    uint16_t sub_idx = value.toUInt ();
	    CmapEnc* sub = m_cmap->getSubTable (sub_idx);
	    if (valid_format (tab->platform (), tab->specific (), sub, m_parent)) {
		tab->setSubTable (sub);
		emit dataChanged (index, index);
		return true;
	    }
	}
    }
    return false;
}

Qt::ItemFlags CmapTableModel::flags (const QModelIndex &index) const {
    Qt::ItemFlags ret = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column () == 2) {
	ret |= Qt::ItemIsEditable;
    }
    return ret;
}

QVariant CmapTableModel::headerData (int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
	switch (section) {
	  case 0:
	    return QWidget::tr ("Platform");
	  case 1:
	    return QWidget::tr ("Encoding");
	  case 2:
	    return QWidget::tr ("Subtable Index");
	}
    } else if (orientation == Qt::Vertical && role == Qt::DisplayRole) {
	return QString::number (section+1);
    }
    return QVariant ();
}

bool CmapTableModel::removeRows (int row, int count, const QModelIndex &index) {
    Q_UNUSED (index);
    Q_ASSERT (count == 1);

    if (m_cmap->numTables () == 1) {
        FontShepherd::postError (
    	QCoreApplication::tr ("Can't delete the last table"),
            QCoreApplication::tr (
    	    "Can't delete the last cmap encoding table in the list."
	    "Create more before deleting this one."),
            m_parent);
	return false;
    } else if (row >= m_cmap->numTables ()) {
        FontShepherd::postError (
    	QCoreApplication::tr ("Wrong index"),
            QCoreApplication::tr (
    	    "Wrong index of the row to be deleted."
	    "This should be a bug in the application."),
            m_parent);
	return false;
    }
    beginRemoveRows (QModelIndex (), row, row);
    m_cmap->removeTable ((uint16_t) row);
    endRemoveRows ();
    emit needsSelectionUpdate (row < m_cmap->numTables () ? row : row-1);
    return true;
}

QModelIndex CmapTableModel::insertRows (QList<table_record> &input) {
    Q_ASSERT (input.size () == 1);
    uint16_t row;
    CmapEnc *enc = m_cmap->getSubTable (input[0].subtable);
    beginResetModel ();
    row = m_cmap->addTable (input[0].platform, input[0].specific, enc);
    endResetModel ();
    emit needsSelectionUpdate (row);
    return index (row, 0, QModelIndex ());
}

GidListModel::GidListModel (sFont* fnt, bool is_8bit, QObject *parent) :
    QAbstractListModel (parent), m_font (fnt), m_8bit_limit (is_8bit) {

    bool has_glyph_names;
    int i;
    int len = m_8bit_limit ? 256 : m_font->glyph_cnt;

    m_gnp = std::unique_ptr<GlyphNameProvider> (new GlyphNameProvider (*m_font));
    has_glyph_names = m_gnp->fontHasGlyphNames ();
    for (i=0; i<len; i++) {
	QString gid_str;
	if (has_glyph_names)
	    gid_str = QString ("#%1: %2").arg (i).arg (QString::fromStdString (m_gnp->nameByGid (i)));
	else
	    gid_str = QString ("#%1").arg (i);
	m_data << gid_str;
    }
}

GidListModel::~GidListModel () {}

QVariant GidListModel::data (const QModelIndex & index, int role) const {
    if (role == Qt::DisplayRole)
	return QVariant (m_data[index.row ()]);
    return QVariant (index.row ());
}

int GidListModel::rowCount (const QModelIndex & parent) const {
    Q_UNUSED (parent);
    return (m_8bit_limit ? 256 : m_font->glyph_cnt);
}

QString GidListModel::getGidStr (const uint32_t gid) const {
    if (gid < (uint32_t) rowCount ())
	return m_data[gid];
    return QString ("<wrong GID %1>").arg (gid);
}

SubtableSelectorDelegate::SubtableSelectorDelegate (CmapTable *cmap, QUndoStack *us, QObject *parent) :
    QStyledItemDelegate (parent), m_cmap (cmap), m_ustack (us) {
}

QWidget* SubtableSelectorDelegate::createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (option);
    Q_UNUSED (index);
    QComboBox* combo = new QComboBox (parent);
    QListView *view = (QListView *) combo->view ();
    QStringList enc_list;
    uint16_t i;

    for (i=0; i<m_cmap->numSubTables (); i++)
	enc_list << QString::fromStdString (m_cmap->getSubTable (i)->stringName ());
    combo->addItems (enc_list);
    combo->setEditable (false);
    view->setUniformItemSizes (true);
    view->setLayoutMode (QListView::Batched);
    return combo;
}

void SubtableSelectorDelegate::setEditorData (QWidget *editor, const QModelIndex &index) const {
    QString value = index.model ()->data (index, Qt::DisplayRole).toString ();
    QComboBox* combo = qobject_cast<QComboBox*> (editor);
    int idx = combo->findText (value);
    combo->setCurrentIndex (idx);
}

void SubtableSelectorDelegate::setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
    QComboBox* comboBox = qobject_cast<QComboBox*> (editor);
    uint16_t value = comboBox->currentIndex ();
    ChangeCellCommand *cmd = new ChangeCellCommand (model, index, value);
    cmd->setText (tr ("Set encoding subtable"));
    m_ustack->push (cmd);
}

void SubtableSelectorDelegate::updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (index);
    editor->setGeometry (option.rect);
}

ComboDelegate::ComboDelegate (GidListModel *model, QUndoStack *us, QObject *parent) :
    QStyledItemDelegate (parent), m_model (model), m_ustack (us) {
}

QWidget* ComboDelegate::createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (option);
    Q_UNUSED (index);
    QComboBox* combo = new QComboBox (parent);
    QListView *view = (QListView *) combo->view ();
    view->setUniformItemSizes (true);
    view->setLayoutMode (QListView::Batched);
    combo->setEditable (false);
    combo->setModel (m_model);
    return combo;
}

void ComboDelegate::setEditorData (QWidget *editor, const QModelIndex &index) const {
    QString value = index.model ()->data (index, Qt::DisplayRole).toString ();
    QComboBox* combo = qobject_cast<QComboBox*> (editor);
    combo->view ()->setVerticalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    int idx = combo->findText (value);
    combo->setCurrentIndex (idx);
    combo->view ()->scrollTo (combo->model ()->index (idx, 0));
}

void ComboDelegate::setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
    QComboBox* comboBox = qobject_cast<QComboBox*> (editor);
    uint16_t value = comboBox->currentIndex ();
    ChangeCellCommand *cmd = new ChangeCellCommand (model, index, value);
    cmd->setText (tr ("Change Mapping"));
    m_ustack->push (cmd);
}

void ComboDelegate::updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (index);
    editor->setGeometry (option.rect);
}

UnicodeDelegate::UnicodeDelegate (QUndoStack *us, QObject *parent) :
    QStyledItemDelegate (parent), m_ustack (us) {
}

QWidget* UnicodeDelegate::createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (option);
    UniSpinBox *box = new UniSpinBox (parent);
    uint32_t min, max;
    QModelIndex previdx, pprevidx, nextidx;

    box->setFrame (false);

    if (index.column () == 1) {
	if (index.row () > 0) {
	    previdx  = index.model ()->index (index.row () - 1, 2);
	    min = index.model ()->data  (previdx, Qt::EditRole).toUInt () + 1;
	} else {
	    min = 0;
	}
	nextidx = index.model ()->index (index.row (), 2);
	max = index.model ()->data (nextidx, Qt::EditRole).toUInt ();
    } else {
	if (index.row () < index.model ()->rowCount (index) - 1) {
	    nextidx = index.model ()->index (index.row () + 1, 1);
	    max = index.model ()->data (nextidx, Qt::EditRole).toUInt () - 1;
	} else {
	    max = 0xFFFFFF;
	}
	previdx  = index.model ()->index (index.row (), 1);
	min = index.model ()->data  (previdx, Qt::EditRole).toUInt ();
    }
    box->setMinimum (min);
    box->setMaximum (max);

    return box;
}

void UnicodeDelegate::setEditorData (QWidget *editor, const QModelIndex &index) const {
    uint32_t value = index.model ()->data (index, Qt::EditRole).toUInt ();
    UniSpinBox *box = qobject_cast<UniSpinBox*> (editor);
    box->setValue (value);
}

void UnicodeDelegate::setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
    UniSpinBox* box = qobject_cast<UniSpinBox*> (editor);
    box->interpretText ();
    uint32_t value = box->value ();
    ChangeCellCommand *cmd = new ChangeCellCommand (model, index, value);
    cmd->setText (tr ("Change Range"));
    m_ustack->push (cmd);
}

void UnicodeDelegate::updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (index);
    editor->setGeometry (option.rect);
}

EncSubModel::EncSubModel (CmapEnc *enc, GidListModel *lmodel, QWidget *parent) :
    QAbstractTableModel (parent), m_enc (enc), m_listmodel (lmodel), m_parent (parent) {
}

int EncSubModel::rowCount (const QModelIndex &parent) const {
    Q_UNUSED (parent);
    return (m_enc->count ());
}

int EncSubModel::columnCount (const QModelIndex &parent) const {
    Q_UNUSED (parent);
    return 2;
}

QVariant EncSubModel::data (const QModelIndex &index, int role) const {
    QString ret_str;
    uint32_t ret_code = m_enc->encByPos (index.row ());
    uint16_t ret_gid  = m_enc->gidByPos (index.row ());

    switch (role) {
      case Qt::EditRole:
	switch (index.column ()) {
	  case 0:
	    return ret_code;
	  break;
	  case 1:
	    return ret_gid;
	}
	break;
      case Qt::DisplayRole:
	switch (index.column ()) {
	  case 0:
	    return m_enc->codeRepr (ret_code);
	  break;
	  case 1:
	    ret_str = m_listmodel->getGidStr (ret_gid);
	    return ret_str;
	}
	break;
      case Qt::ToolTipRole:
	if (index.column () == 0 && m_enc->isUnicode ())
	    return QString::fromStdString (IcuWrapper::unicodeCharName (ret_code));
    }
    return QVariant ();
}

bool EncSubModel::setData (const QModelIndex &index, const QVariant &value, int role) {
    if (index.isValid() && index.column () == 1) {
	if (role == Qt::EditRole) {
	    uint16_t gid = value.toUInt ();
	    m_enc->setGidByPos (index.row (), gid);
	    emit dataChanged (index, index);
	    emit needsSelectionUpdate (m_enc->index (), index.row (), 1);
	    return true;
	}
    }
    return false;
}

Qt::ItemFlags EncSubModel::flags (const QModelIndex &index) const {
    Qt::ItemFlags ret = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column () == 1) {
	ret |= Qt::ItemIsEditable;
    }
    return ret;
}

QVariant EncSubModel::headerData (int section, Qt::Orientation orientation, int role) const {
    const char *enc_title = m_enc->isUnicode () ? "Unicode" : "Encoding";
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
	switch (section) {
	  case 0:
	    return enc_title;
	  case 1:
	    return "GID";
	}
    } else if (orientation == Qt::Vertical && role == Qt::DisplayRole) {
	return QString::number (section+1);
    }
    return QVariant ();
}

bool EncSubModel::removeRows (int row, int count, const QModelIndex &index) {
    Q_UNUSED (index);
    uint16_t i;

    if (m_enc->format () == 6 || m_enc->format () == 10) {
	if (row == 0 || (uint32_t) (row + count) == m_enc->count ())
	    ;
	else {
	    FontShepherd::postError (
		QCoreApplication::tr ("Can't delete glyph mappings"),
		QCoreApplication::tr (
		"Can't delete mappings from the middle of a trimmed "
		"mapping array (cmap subtable format 6 or 10)."),
		m_parent);
	    return false;
	}
    }

    beginRemoveRows (QModelIndex (), row, row+count-1);
    for (i=0; i<count; i++) {
	QModelIndex row_idx = this->index (row, 0, QModelIndex ());
	uint32_t code = data (row_idx, Qt::EditRole).toUInt ();
	m_enc->deleteMapping (code);
    }
    endRemoveRows ();
    emit needsSelectionUpdate (m_enc->index (), row, 1);
    return true;
}

QModelIndex EncSubModel::insertRows (const QList<struct enc_mapping> &input, int row) {
    uint16_t i;
    uint16_t count = input.size ();
    beginInsertRows (QModelIndex (), row, row+count-1);
    for (i=0; i<count; i++) {
	uint32_t code = input[i].code;
	uint16_t gid  = input[i].gid;
	m_enc->insertMapping (code, gid);
    }
    endInsertRows ();
    emit needsSelectionUpdate (m_enc->index (), row, input.size ());
    return index (row, 0, QModelIndex ());
}

void EncSubModel::setSubTableModified (bool clean) {
    m_enc->setModified (!clean);
    emit (needsLabelUpdate (m_enc->index ()));
}

Enc13SubModel::Enc13SubModel (CmapEnc *enc, GlyphNameProvider *gnp, QObject *parent) :
    QAbstractTableModel (parent), m_enc (enc), m_gnp (gnp) {
}

int Enc13SubModel::rowCount (const QModelIndex &parent) const {
    Q_UNUSED (parent);
    return (m_enc->numRanges ());
}

int Enc13SubModel::columnCount (const QModelIndex &parent) const {
    Q_UNUSED (parent);
    return 3;
}

QVariant Enc13SubModel::data (const QModelIndex &index, int role) const {
    QString ret_str;
    struct enc_range *er = m_enc->getRange (index.row ());
    uint32_t uni;

    switch (role) {
      case Qt::DisplayRole:
	if (index.column () == 0) {
	    std::string name = m_gnp->nameByGid (er->first_gid);
	    return QString ("#%1: %2").arg (er->first_gid).arg (QString::fromStdString (name));
	} else {
	    uni = (index.column () == 1 ? er->first_enc : er->first_enc + er->length - 1);
    	    QString uni_str = QString ("U+%1").arg
    		(uni, uni <= 0xFFFF ? 4 : 6, 16, QLatin1Char ('0')).toUpper ();
	    return uni_str;
	}
	break;
      case Qt::EditRole:
	switch (index.column ()) {
	  case 0:
	    return er->first_gid;
	  case 1:
	    return er->first_enc;
	  case 2:
	    return er->first_enc + er->length - 1;
	}
	break;
      case Qt::ToolTipRole:
	if (index.column () > 0) {
	    uni = (index.column () == 1 ? er->first_enc : er->first_enc + er->length - 1);
	    return QString::fromStdString (IcuWrapper::unicodeCharName (uni));
	}
    }
    return QVariant ();
}

bool Enc13SubModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    uint32_t shift;
    if (index.isValid()) {
	if (role == Qt::EditRole) {
	    struct enc_range *er = m_enc->getRange (index.row ());
	    m_enc->setModified (true);
	    switch (index.column ()) {
	      case 1:
		shift = er->first_enc - value.toUInt ();
		er->first_enc = value.toUInt ();
		er->length += shift;
		emit dataChanged (index, index);
		emit needsSelectionUpdate (m_enc->index (), index.row (), 1);
		return true;
	      case 2:
		er->length = value.toUInt () - er->first_enc + 1;
		emit dataChanged (index, index);
		emit needsSelectionUpdate (m_enc->index (), index.row (), 1);
		return true;
	    }
	}
    }
    return false;
}

Qt::ItemFlags Enc13SubModel::flags (const QModelIndex &index) const {
    Qt::ItemFlags ret = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column () > 0) {
	ret |= Qt::ItemIsEditable;
    }
    return ret;
}

QVariant Enc13SubModel::headerData (int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
	switch (section) {
	  case 0:
	    return "GID";
	  case 1:
	    return "First Unicode";
	  case 2:
	    return "Last Unicode";
	}
    } else if (orientation == Qt::Vertical && role == Qt::DisplayRole) {
	return QString::number (section+1);
    }
    return QVariant ();
}

bool Enc13SubModel::removeRows (int row, int count, const QModelIndex &index) {
    Q_UNUSED (index);
    int i;

    beginRemoveRows (QModelIndex (), row, row+count-1);
    for (i=row; i<row+count; i++)
	m_enc->deleteRange (i);
    endRemoveRows ();
    emit needsSelectionUpdate (m_enc->index (), row, 1);
    return true;
}

QModelIndex Enc13SubModel::insertRows (QList<struct enc_range> &input, int row) {
    uint16_t i;
    int count = input.size ();

    beginInsertRows (QModelIndex (), row, row+count-1);
    for (i=0; i<count; i++) {
	struct enc_range rng = input[i];
	m_enc->insertRange (rng.first_enc, rng.first_gid, rng.length);
    }
    endInsertRows ();
    emit needsSelectionUpdate (m_enc->index (), row, input.size ());
    return index (row, 0, QModelIndex ());
}

void Enc13SubModel::setSubTableModified (bool clean) {
    m_enc->setModified (!clean);
    emit (needsLabelUpdate (m_enc->index ()));
}

VarSelectorModel::VarSelectorItem::VarSelectorItem (VarSelectorItem *parent) :
    m_parent (parent) {
}

VarSelectorModel::VarSelectorItem *VarSelectorModel::VarSelectorItem::getChild (int) {
    return nullptr;
}

bool VarSelectorModel::VarSelectorItem::removeChildren (int, int) {
    return false;
}

void VarSelectorModel::VarSelectorItem::appendChild (uint32_t, int, bool) {
}

VarSelectorModel::VarSelectorItem *VarSelectorModel::VarSelectorItem::parent () const {
    return m_parent;
}

bool VarSelectorModel::VarSelectorItem::setData (int, const QVariant &, int) {
    return false;
}

Qt::ItemFlags VarSelectorModel::VarSelectorItem::flags (int) const {
    return (Qt::ItemIsEnabled | Qt::ItemIsSelectable);
}

int VarSelectorModel::VarSelectorItem::rowCount () const {
    return m_children.size ();
}

int VarSelectorModel::VarSelectorItem::columnCount () const {
    return 1;
}

VarSelectorModel::VarSelectorRoot::VarSelectorRoot
    (CmapEnc *enc, struct var_selector_record *vsr, GidListModel *lmodel, VarSelectorItem *parent) :
    VarSelectorModel::VarSelectorItem (parent), m_enc (enc), m_vsr (vsr), m_glyph_desc_provider (lmodel) {

    m_children.reserve (2);

    if (m_vsr->default_offset) {
	std::unique_ptr<VarSelectorItem> group (new UvsItemGroup (nullptr, em_uvsDefaultGroup, this));
	m_children.push_back (std::move (group));
    }

    if (m_vsr->non_default_offset) {
	std::unique_ptr<VarSelectorItem> group (new UvsItemGroup (lmodel, em_uvsNonDefaultGroup, this));
	m_children.push_back (std::move (group));
    }
}

enum vsItemType VarSelectorModel::VarSelectorRoot::type () const {
    return em_varSelector;
}

VarSelectorModel::VarSelectorItem *VarSelectorModel::VarSelectorRoot::getChild (int idx) {
    if (idx < (int) m_children.size ())
	return m_children[idx].get ();
    return nullptr;
}

bool VarSelectorModel::VarSelectorRoot::removeChildren (int row, int count) {
    if (row >=0 && count > 0 && row+count <= (int) m_children.size ()) {
	m_children.erase (m_children.begin ()+row, m_children.begin ()+row+count);
	return true;
    }
    return false;
}

void VarSelectorModel::VarSelectorRoot::appendChild (uint32_t, int, bool is_dflt) {
    if (is_dflt) {
	std::unique_ptr<VarSelectorItem> group (new UvsItemGroup (nullptr, em_uvsDefaultGroup, this));
	m_children.insert (m_children.begin (), std::move (group));
    } else {
	std::unique_ptr<VarSelectorItem> group (new UvsItemGroup (m_glyph_desc_provider, em_uvsNonDefaultGroup, this));
	m_children.push_back (std::move (group));
    }
}

QVariant VarSelectorModel::VarSelectorRoot::data (int column, int role) const {
    uint32_t uni = m_vsr->selector;
    if (column == 0) {
	switch (role) {
	  case Qt::DisplayRole:
	    return QString ("U+%1").arg
		(uni, uni <= 0xFFFF ? 4 : 6, 16, QLatin1Char ('0')).toUpper ();
	  case Qt::EditRole:
	    return uni;
	}
    }
    return QVariant ();
}

struct var_selector_record *VarSelectorModel::VarSelectorRoot::vsRecord () const {
    return m_vsr;
}

uint32_t VarSelectorModel::VarSelectorRoot::unicode () const {
    return m_vsr->selector;
}

int VarSelectorModel::VarSelectorRoot::findRow () const {
    uint16_t i;
    for (i=0; i<m_enc->count (); i++) {
        var_selector_record *vsr = m_enc->getVarSelectorRecord (i);
        if (vsr->selector == m_vsr->selector)
    	return i;
    }
    return -1;
}

void VarSelectorModel::VarSelectorRoot::update (uint16_t i) {
    m_vsr = m_enc->getVarSelectorRecord (i);
}

VarSelectorModel::UvsItemGroup::UvsItemGroup (
    GidListModel *lmodel, enum vsItemType type, VarSelectorItem *parent) :
    VarSelectorModel::VarSelectorItem (parent), m_glyph_desc_provider (lmodel), m_type (type) {
    uint32_t i;

    var_selector_record *vsr = this->vsRecord ();
    if (m_type == em_uvsDefaultGroup) {
	m_children.reserve (vsr->default_vars.size ());
	for (i=0; i<vsr->default_vars.size (); i++) {
	    std::unique_ptr<VarSelectorItem> item
		(new UvsItem (lmodel, vsr->default_vars[i], em_uvsDefaultRecord, this));
	    m_children.push_back (std::move (item));
	}
    } else if (m_type == em_uvsNonDefaultGroup) {
	m_children.reserve (vsr->non_default_vars.size ());
	for (i=0; i<vsr->non_default_vars.size (); i++) {
	    std::unique_ptr<VarSelectorItem> item
		(new UvsItem (lmodel, vsr->non_default_vars[i].code, em_uvsNonDefaultRecord, this));
	    m_children.push_back (std::move (item));
	}
    }
}

enum vsItemType VarSelectorModel::UvsItemGroup::type () const {
    return m_type;
}

VarSelectorModel::VarSelectorItem *VarSelectorModel::UvsItemGroup::getChild (int idx) {
    if (idx < (int) m_children.size ())
	return m_children[idx].get ();
    return nullptr;
}

bool VarSelectorModel::UvsItemGroup::removeChildren (int row, int count) {
    if (row >=0 && count > 0 && row+count <= (int) m_children.size ()) {
	m_children.erase (m_children.begin ()+row, m_children.begin ()+row+count);
	return true;
    }
    return false;
}

void VarSelectorModel::UvsItemGroup::appendChild (uint32_t code, int row, bool is_dflt) {
    std::unique_ptr<VarSelectorItem> item (new UvsItem (m_glyph_desc_provider, code,
	is_dflt ? em_uvsDefaultRecord : em_uvsNonDefaultRecord, this));
    m_children.insert (m_children.begin () + row, std::move (item));
}

QVariant VarSelectorModel::UvsItemGroup::data (int column, int role) const {
    struct var_selector_record *vsr = this->vsRecord ();
    uint32_t cnt = (m_type == em_uvsDefaultGroup) ?
	vsr->default_vars.size () : vsr->non_default_vars.size ();
    if (column == 0) {
	switch (role) {
	  case Qt::DisplayRole:
	    return QString ((m_type == em_uvsDefaultGroup) ?
		"Default UVS: %1 records" : "Non-Default UVS: %1 records").arg (cnt);
	  case Qt::EditRole:
	    return m_type;
	}
    }
    return QVariant ();
}

int VarSelectorModel::UvsItemGroup::columnCount () const {
    return (m_type == em_uvsDefaultGroup) ? 1 : 2;
}

struct var_selector_record *VarSelectorModel::UvsItemGroup::vsRecord () const {
    return parent ()->vsRecord ();
}

uint32_t VarSelectorModel::UvsItemGroup::unicode () const {
    return vsRecord ()->selector;
}

int VarSelectorModel::UvsItemGroup::findRow () const {
    struct var_selector_record *vsr = this->vsRecord ();
    if (vsr->default_offset && m_type == em_uvsNonDefaultGroup)
	return 1;
    else if (!vsr->default_offset || m_type == em_uvsDefaultGroup)
	return 0;
    return -1;
}

VarSelectorModel::UvsItem::UvsItem
    (GidListModel *lmodel, uint32_t uni, enum vsItemType type, VarSelectorItem *parent) :
    VarSelectorModel::VarSelectorItem (parent), m_glyph_desc_provider (lmodel), m_unicode (uni), m_type (type) {
}

enum vsItemType VarSelectorModel::UvsItem::type () const {
    return m_type;
}

QVariant VarSelectorModel::UvsItem::data (int column, int role) const {
    QString ret_str, gid_str;
    uint16_t i, gid = 0;

    struct var_selector_record *vsr = this->vsRecord ();
    if (m_type == em_uvsNonDefaultRecord) {
	for (i=0; i<vsr->non_default_vars.size (); i++) {
	    if (vsr->non_default_vars[i].code == m_unicode) {
		gid = vsr->non_default_vars[i].gid;
		break;
	    }
	}
    }

    switch (role) {
      case Qt::DisplayRole:
	switch (column) {
	  case 0:
	    ret_str = QString ("U+%1").arg
		(m_unicode, m_unicode <= 0xFFFF ? 4 : 6, 16, QLatin1Char ('0')).toUpper ();
	    return ret_str;
	  case 1:
	    if (m_type == em_uvsNonDefaultRecord) {
		ret_str = m_glyph_desc_provider->getGidStr (gid);
		return ret_str;
	    }
	    return QVariant ();
	}
	break;
      case Qt::EditRole:
	switch (column) {
	  case 0:
	    return m_unicode;
	  case 1:
	    return gid;
	}
	break;
      case Qt::ToolTipRole:
	if (column == 0)
	    return QString::fromStdString (IcuWrapper::unicodeCharName (m_unicode));
    }
    return QVariant ();
}

bool VarSelectorModel::UvsItem::setData (int column, const QVariant &value, int role) {
    uint32_t i;
    struct var_selector_record *vsr = this->vsRecord ();
    if (m_type == em_uvsNonDefaultRecord && column == 1) {
	if (role == Qt::EditRole) {
	    uint16_t gid = value.toUInt ();
	    for (i=0; i<vsr->non_default_vars.size (); i++) {
		if (vsr->non_default_vars[i].code == m_unicode) {
		    vsr->non_default_vars[i].gid = gid;
		    return true;
		}
	    }
	}
    }
    return false;
}

Qt::ItemFlags VarSelectorModel::UvsItem::flags (int column) const {
    Qt::ItemFlags ret = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (column == 1) {
	ret |= Qt::ItemIsEditable;
    }
    return ret;
}

int VarSelectorModel::UvsItem::columnCount () const {
    return (m_type == em_uvsDefaultRecord) ? 1 : 2;
}

struct var_selector_record *VarSelectorModel::UvsItem::vsRecord () const {
    return parent ()->vsRecord ();
}

uint32_t VarSelectorModel::UvsItem::unicode () const {
    return m_unicode;
}

int VarSelectorModel::UvsItem::findRow () const {
    uint32_t i;
    struct var_selector_record *vsr = this->vsRecord ();
    if (m_type == em_uvsDefaultRecord) {
	for (i=0; i<vsr->default_vars.size (); i++) {
	    if (m_unicode == vsr->default_vars[i])
		return i;
	}
    } else {
	for (i=0; i<vsr->non_default_vars.size (); i++) {
	    if (m_unicode == vsr->non_default_vars[i].code)
		return i;
	}
    }
    return -1;
}

VarSelectorModel::VarSelectorModel (CmapEnc *enc, GidListModel *lmodel, QObject *parent) :
    QAbstractItemModel (parent), m_enc (enc), m_lmodel (lmodel) {
    uint16_t i;
    m_root.reserve (m_enc->count ());

    for (i=0; i<m_enc->count (); i++) {
	var_selector_record *vsr = m_enc->getVarSelectorRecord (i);
	std::unique_ptr<VarSelectorRoot> root (new VarSelectorRoot (m_enc, vsr, m_lmodel, nullptr));
	m_root.push_back (std::move (root));
    }
}

QModelIndex VarSelectorModel::index (int row, int column, const QModelIndex &parent) const {
    if (!hasIndex (row, column, parent))
    	return QModelIndex ();

    if (!parent.isValid ())
    	return createIndex (row, column, m_root[row].get ());

    VarSelectorItem* parent_item = static_cast<VarSelectorItem*> (parent.internalPointer ());
    return createIndex (row, column, parent_item->getChild (row));
}

QModelIndex VarSelectorModel::parent (const QModelIndex &child) const {
    if (!child.isValid ()) {
    	return QModelIndex ();
    }
    VarSelectorItem* child_item = static_cast<VarSelectorItem*> (child.internalPointer ());
    VarSelectorItem* parent_item = child_item->parent ();
    if (parent_item)
    	return createIndex (parent_item->findRow (), 0, parent_item);
    else
	return QModelIndex ();
}

int VarSelectorModel::rowCount (const QModelIndex &parent) const {
    if (!parent.isValid ())
	return m_enc->count ();
    else {
	VarSelectorItem* item = static_cast<VarSelectorItem*> (parent.internalPointer ());
	return item->rowCount ();
    }
    return 0;
}

int VarSelectorModel::columnCount (const QModelIndex &parent) const {
    if (parent.isValid ()) {
	VarSelectorItem* item = static_cast<VarSelectorItem*> (parent.internalPointer ());
	return item->columnCount ();
    }
    return 2;
}

QVariant VarSelectorModel::data (const QModelIndex &index, int role) const {
    QString ret_str;

    if (index.isValid ()) {
	VarSelectorItem* item = static_cast<VarSelectorItem*> (index.internalPointer ());
	return item->data (index.column (), role);
    }
    return QVariant ();
}

bool VarSelectorModel::setData (const QModelIndex &index, const QVariant &value, int role) {
    if (index.isValid ()) {
	VarSelectorItem* item = static_cast<VarSelectorItem*> (index.internalPointer ());
	if (item->setData (index.column (), value, role)) {
	    emit dataChanged (index, index);
	    emit needsSelectionUpdate (m_enc->index (), index.row (), 1, index.parent ());
	    return true;
	}
    }
    return false;
}

Qt::ItemFlags VarSelectorModel::flags (const QModelIndex &index) const {
    if (index.isValid ()) {
	VarSelectorItem* item = static_cast<VarSelectorItem*> (index.internalPointer ());
	return item->flags (index.column ());
    }
    return (Qt::ItemIsEnabled | Qt::ItemIsSelectable);
}

bool VarSelectorModel::removeRows (int row, int count, const QModelIndex &parent_idx) {
    uint16_t i;
    VarSelectorItem *parent_item = nullptr;
    if (parent_idx.isValid ())
	parent_item = static_cast<VarSelectorItem*> (parent_idx.internalPointer ());
    Q_ASSERT (row >= 0 && count >= 1);

    beginRemoveRows (parent_idx, row, row+count-1);
    VarSelectorItem *first = (parent_idx.isValid ()) ?
        parent_item->getChild (row) : m_root[row].get ();
    struct var_selector_record *vsr = first->vsRecord ();

    switch (first->type ()) {
      case em_varSelector:
	for (i=row; i<row+count; i++) {
	    VarSelectorItem *item = m_root[row].get ();
	    vsr = item->vsRecord ();
	    m_enc->deleteVarSelectorRecord (vsr->selector);
	}
        removeRootItems (row, count);
        break;
      case em_uvsDefaultGroup:
	Q_ASSERT (count == 1);
        parent_item->removeChildren (row);
        vsr->default_offset = 0;
        vsr->default_vars.clear ();
        break;
      case em_uvsNonDefaultGroup:
	Q_ASSERT (count == 1);
        parent_item->removeChildren (row);
        vsr->non_default_offset = 0;
        vsr->non_default_vars.clear ();
        break;
      case em_uvsDefaultRecord:
        parent_item->removeChildren (row, count);
        vsr->default_vars.erase (vsr->default_vars.begin ()+row, vsr->default_vars.begin ()+row+count);
        break;
      case em_uvsNonDefaultRecord:
        parent_item->removeChildren (row, count);
        vsr->non_default_vars.erase (vsr->non_default_vars.begin ()+row, vsr->non_default_vars.begin ()+row+count);
    }
    endRemoveRows ();
    if (parent_item && parent_item->rowCount () == 0)
	removeRow (parent_idx.row (), parent_idx.parent ());
    return true;
}

QModelIndex VarSelectorModel::insertRows (QList<struct uni_variation> &input, int type) {
    int i, cnt=0;
    uint32_t prev_selector = 0;
    QModelIndex ret;

    /* Determine the count of top level items to be inserted or restored after deletion.
     * If it's a variation selector record to be restored, then there is just a single
     * item, although it may have many children */
    switch (type) {
      case em_varSelector:
	for (i=0; i<input.count (); i++) {
	    if (input[i].selector != prev_selector) {
		prev_selector = input[i].selector;
		cnt++;
	    }
	}
	break;
      case em_uvsDefaultGroup:
      case em_uvsNonDefaultGroup:
	cnt = 1;
	break;
      case em_uvsDefaultRecord:
      case em_uvsNonDefaultRecord:
	cnt = input.count ();
    }

    ret = insertRow (input[0].selector, input[0].is_dflt, input[0].unicode, input[0].gid);
    for (i=1; i<input.count (); i++)
	insertRow (input[i].selector, input[i].is_dflt, input[i].unicode, input[i].gid);
    if (type == em_uvsDefaultGroup || type == em_uvsNonDefaultGroup)
	ret = parent (ret);
    else if (type == em_varSelector)
	ret = parent (ret.parent ());
    emit needsSelectionUpdate (m_enc->index (), ret.row (), cnt, ret.parent ());
    return ret;
}

QModelIndex VarSelectorModel::insertRow (uint32_t selector, bool is_dflt, uint32_t code, uint16_t gid) {
    uint16_t i, row;

    for (i=0; i<m_enc->count (); i++) {
	var_selector_record *vsr = m_enc->getVarSelectorRecord (i);
	if (vsr->selector == selector)
	    break;
    }
    row = i;

    var_selector_record *vsr = m_enc->addVariationSequence (selector, is_dflt, code, gid);
    if (!vsr)
	return QModelIndex ();
    m_enc->setModified (true);
    if (row<m_root.size ()) {
	VarSelectorItem *root = m_root[row].get ();
	QModelIndex root_idx = index (row, 0, QModelIndex ());
	VarSelectorItem *group = nullptr;
	for (i=0; i<root->rowCount (); i++) {
	    VarSelectorItem *tg = root->getChild (i);
	    if ((tg->type () == em_uvsDefaultGroup && is_dflt) ||
		(tg->type () == em_uvsNonDefaultGroup && !is_dflt)) {
		group = tg;
		row = i;
		break;
	    }
	}
	if (!group) {
	    row = is_dflt ? 0 : 1;
	    beginInsertRows (root_idx, row, row);
	    root->appendChild (code, row, is_dflt);
	    endInsertRows ();
	    QModelIndex group_idx = index (row, 0, root_idx);
	    return index (0, 0, group_idx);
	} else {
	    QModelIndex group_idx = index (row, 0, root_idx);
	    for (i=0; i<group->rowCount (); i++) {
		VarSelectorItem *seq = group->getChild (i);
		if (seq->unicode () > code)
		    break;
	    }
	    row = i;
	    beginInsertRows (group_idx, row, row);
	    group->appendChild (code, row, is_dflt);
	    endInsertRows ();
	    return index (row, 0, group_idx);
	}
    } else {
	std::unique_ptr<VarSelectorRoot> root (new VarSelectorRoot (m_enc, vsr, m_lmodel, nullptr));
	for (i=0; i<m_root.size (); i++) {
	    if (m_root[i]->unicode () > selector)
		break;
	}
	row = i;
	beginInsertRows (QModelIndex (), row, row);
	m_root.insert (m_root.begin () + row, std::move (root));
	endInsertRows ();
	QModelIndex root_idx = index (row, 0, QModelIndex ());
        QModelIndex group_idx = index (0, 0, root_idx);
        return index (0, 0, group_idx);
    }
}

void VarSelectorModel::setSubTableModified (bool clean) {
    m_enc->setModified (!clean);
    emit (needsLabelUpdate (m_enc->index ()));
}

bool VarSelectorModel::removeRootItems (int row, int count) {
    uint16_t i;
    if (row >= 0 && count > 0 && row+count <= (int) m_root.size ()) {
	m_root.erase (m_root.begin ()+row, m_root.begin ()+row+count);
        for (i=0; i<m_root.size (); i++)
	    m_root[i]->update (i);
	return true;
    }
    return false;
}

// Commands to (un)do various deletions or insertions

TableRecordCommand::TableRecordCommand (CmapTableModel *model, int row) :
    m_model (model), m_row (row), m_remove (true) {

    QModelIndex idx0 = m_model->index (m_row, 0, QModelIndex ());
    QModelIndex idx1 = m_model->index (m_row, 1, QModelIndex ());
    QModelIndex idx2 = m_model->index (m_row, 2, QModelIndex ());
    struct table_record rec = {
        (uint16_t) m_model->data (idx0, Qt::EditRole).toUInt (),
        (uint16_t) m_model->data (idx1, Qt::EditRole).toUInt (),
        (uint16_t) m_model->data (idx2, Qt::EditRole).toUInt ()
    };
    m_data << rec;
}

TableRecordCommand::TableRecordCommand
    (CmapTableModel *model, const QList<table_record> &input) :
    m_model (model), m_remove (false) {

    Q_ASSERT (input.size () == 1);
    m_data << input;
}

void TableRecordCommand::redo () {
    if (m_remove)
	m_model->removeRows (m_row, 1, m_model->index (m_row, 1, QModelIndex ()));
    else
	m_model->insertRows (m_data);
}

void TableRecordCommand::undo () {
    if (m_remove)
	m_model->insertRows (m_data);
    else
	m_model->removeRows (m_row, 1, m_model->index (m_row, 1, QModelIndex ()));
}

MappingCommand::MappingCommand (EncSubModel *model, int row, int count) :
    m_model (model), m_row (row), m_count (count), m_remove (true) {

    int i;

    for (i=row; i<row+count; i++) {
	QModelIndex idx0 = m_model->index (i, 0, QModelIndex ());
	QModelIndex idx1 = m_model->index (i, 1, QModelIndex ());
	struct enc_mapping m {
	    m_model->data (idx0, Qt::EditRole).toUInt (),
	    (uint16_t) m_model->data (idx1, Qt::EditRole).toUInt ()
	};
	m_data << m;
    }
}

MappingCommand::MappingCommand (EncSubModel *model, const QList<struct enc_mapping> &input, int row) :
    m_model (model), m_row (row), m_remove (false) {

    m_count = input.size ();
    m_data << input;
}

void MappingCommand::redo () {
    if (m_remove)
	m_model->removeRows (m_row, m_count, m_model->index (m_row, m_count, QModelIndex ()));
    else
	m_model->insertRows (m_data, m_row);
}

void MappingCommand::undo () {
    if (m_remove)
	m_model->insertRows (m_data, m_row);
    else
	m_model->removeRows (m_row, m_count, m_model->index (m_row, m_count, QModelIndex ()));
}

RangeCommand::RangeCommand (Enc13SubModel *model, int row, int count) :
    m_model (model), m_row (row), m_count (count), m_remove (true) {

    int i;

    for (i=m_row; i<m_row+m_count; i++) {
	QModelIndex idx0 = m_model->index (i, 0, QModelIndex ());
	QModelIndex idx1 = m_model->index (i, 1, QModelIndex ());
	QModelIndex idx2 = m_model->index (i, 2, QModelIndex ());
	struct enc_range m;
	m.first_enc = m_model->data (idx1, Qt::EditRole).toUInt ();
	m.length = m_model->data (idx2, Qt::EditRole).toUInt () - m.first_enc + 1;
	m.first_gid = (uint16_t) m_model->data (idx0, Qt::EditRole).toUInt ();
	m_data << m;
    }
}

RangeCommand::RangeCommand (Enc13SubModel *model, const QList<struct enc_range> &input, int row) :
    m_model (model), m_row (row), m_remove (false) {

    m_count = input.size ();
    m_data << input;
}

void RangeCommand::redo () {
    if (m_remove)
	m_model->removeRows (m_row, m_count, m_model->index (m_row, m_count, QModelIndex ()));
    else
	m_model->insertRows (m_data, m_row);
}

void RangeCommand::undo () {
    if (m_remove)
	m_model->insertRows (m_data, m_row);
    else
	m_model->removeRows (m_row, m_count, m_model->index (m_row, m_count, QModelIndex ()));
}

VariationCommand::VariationCommand (VarSelectorModel *model, QModelIndex parent, int row, int count) :
    m_model (model), m_parent (parent), m_row (row), m_count (count), m_remove (true) {

    int i, j;
    uint32_t selector;
    if (!parent.isValid ()) {
	m_type = em_varSelector;
	for (i=0; i<m_count; i++) {
	    QModelIndex vsr_idx = m_model->index (m_row+i, 0, parent);
	    selector = m_model->data (vsr_idx, Qt::EditRole).toUInt ();
	    for (j=0; j<m_model->rowCount (vsr_idx); j++) {
		QModelIndex group_idx = m_model->index (j, 0, vsr_idx);
		readSequences (group_idx, selector, 0, m_model->rowCount (group_idx));
	    }
	}
    } else {
	QModelIndex cur_idx = m_model->index (row, 0, parent);
	if (m_model->hasChildren (cur_idx)) {
	    Q_ASSERT (m_count == 1);
	    selector = m_model->data (parent, Qt::EditRole).toUInt ();
	    m_type = m_model->data (cur_idx, Qt::EditRole).toInt ();
	    readSequences (cur_idx, selector, 0, m_model->rowCount (cur_idx));
	} else {
	    QModelIndex vsr_idx = m_model->parent (parent);
	    selector = m_model->data (vsr_idx, Qt::EditRole).toUInt ();
	    int group_type = m_model->data (parent, Qt::EditRole).toInt ();
	    m_type = (group_type == em_uvsDefaultGroup) ? em_uvsDefaultRecord : em_uvsNonDefaultRecord;
	    readSequences (parent, selector, m_row, m_count);
	}
    }
}

VariationCommand::VariationCommand (VarSelectorModel *model, const QList<struct uni_variation> &input) :
    m_model (model), m_remove (false) {

    Q_ASSERT (input.size () > 0);
    m_count = input.size ();
    m_data << input;
    m_type = input[0].is_dflt ? em_uvsDefaultRecord : em_uvsNonDefaultRecord;
}

void VariationCommand::redo () {
    if (m_remove)
	m_model->removeRows (m_row, m_count, m_parent);
    else {
	QModelIndex idx = m_model->insertRows (m_data, m_type);
	m_parent = idx.parent ();
	m_row = idx.row ();
    }
}

void VariationCommand::undo () {
    if (m_remove)
	m_model->insertRows (m_data, m_type);
    else
	m_model->removeRows (m_row, m_count, m_parent);
}

void VariationCommand::readSequences (QModelIndex &group_idx, uint32_t selector, int row, int count) {
    int i;
    int group_type = m_model->data (group_idx, Qt::EditRole).toInt ();
    if (group_type == em_uvsDefaultGroup) {
        for (i=row; i<row+count; i++) {
	    struct uni_variation uvs = {};
	    uvs.selector = selector;
	    uvs.is_dflt = true;
	    QModelIndex uni_idx = m_model->index (i, 0, group_idx);
	    uvs.unicode = m_model->data (uni_idx, Qt::EditRole).toUInt ();
	    m_data << uvs;
        }
    } else {
        for (i=row; i<row+count; i++) {
	    struct uni_variation uvs = {};
	    uvs.selector = selector;
	    uvs.is_dflt = false;
	    QModelIndex uni_idx = m_model->index (i, 0, group_idx);
	    QModelIndex gid_idx = m_model->index (i, 1, group_idx);
	    uvs.unicode = m_model->data (uni_idx, Qt::EditRole).toUInt ();
	    uvs.gid = m_model->data (gid_idx, Qt::EditRole).toUInt ();
	    m_data << uvs;
        }
    }
}

ChangeCellCommand::ChangeCellCommand (QAbstractItemModel *model, const QModelIndex &index, uint32_t new_val) :
    m_model (model), m_index (index), m_new (new_val) {

    m_old = m_model->data (m_index, Qt::EditRole).toUInt ();
}

void ChangeCellCommand::redo () {
    m_model->setData (m_index, m_new, Qt::EditRole);
}

void ChangeCellCommand::undo () {
    m_model->setData (m_index, m_old, Qt::EditRole);
}
