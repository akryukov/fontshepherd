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
#include "editors/nameedit.h" // also includes tables.h
#include "tables/name.h"

#include "fs_notify.h"
#include "commonlists.h"
#include "editors/commondelegates.h"

// Main class, representing the name table editing window

void NameEdit::setEditWidth (QTableView *edit, int height_ratio) {
    QFontMetrics fm = edit->fontMetrics ();

    edit->setColumnWidth (0, fm.boundingRect ("ISO 10646 (deprecated)").width ());
    edit->setColumnWidth (1, fm.boundingRect ("10: Unicode UCS-4").width ());
    edit->setColumnWidth (2, fm.boundingRect ("English (USA)").width ());
    edit->setColumnWidth (3, fm.boundingRect ("Light Background Palette").width ());
    edit->setColumnWidth (4, fm.boundingRect ("Copyrigth (XXXX) My Cool Company").width ());
    edit->horizontalHeader ()->setStretchLastSection (true);
    edit->setMinimumWidth (edit->horizontalHeader ()->length());

    edit->setSelectionBehavior (QAbstractItemView::SelectRows);
    edit->setSelectionMode (QAbstractItemView::ContiguousSelection);
    edit->resize (edit->width (), edit->rowHeight (0) * height_ratio);
    edit->selectRow (0);
}

NameEdit::NameEdit (FontTable* tbl, sFont* font, QWidget *parent) :
    TableEdit (parent, Qt::Window), m_table (tbl), m_font (font) {

    setAttribute (Qt::WA_DeleteOnClose);
    setWindowTitle (QString ("name - ").append (m_font->fontname));

    m_name = dynamic_cast<NameTable *> (m_table);
    m_uGroup = std::unique_ptr<QUndoGroup> (new QUndoGroup (this));

    QWidget *window = new QWidget (this);

    m_tab = new QTabWidget (window);
    m_nametab = new QTableView (m_tab);
    m_langtab = new QTableView (m_tab);

    m_tab->addTab (m_nametab, QWidget::tr ("&Names"));
    m_nametab->setContextMenuPolicy (Qt::CustomContextMenu);
    if (m_name->version () > 0)
	m_tab->addTab (m_langtab, QWidget::tr ("&Language Tags"));
    m_langtab->setVisible (m_name->version () > 0);
    m_langtab->setContextMenuPolicy (Qt::CustomContextMenu);

    fillNameTable ();
    fillLangTable ();
    setMenuBar ();

    connect (m_tab, &QTabWidget::currentChanged, this, &NameEdit::onTabChange);
    connect (m_nametab, &QTableView::customContextMenuRequested, this, &NameEdit::customContextMenu);
    connect (m_langtab, &QTableView::customContextMenuRequested, this, &NameEdit::customContextMenu);

    m_versionBox = new QComboBox ();
    m_versionBox->addItem ("Format 0: Platform-specific language IDs", 0);
    m_versionBox->addItem ("Format 1: Custom language tags", 1);
    m_versionBox->setEditable (false);
    connect (m_versionBox, static_cast<void (QComboBox::*)(int)> (&QComboBox::currentIndexChanged),
	this, &NameEdit::switchTableVersion);
    m_versionBox->setCurrentIndex (m_name->version ());

    saveButton = new QPushButton (tr ("&Compile table"));
    removeButton = new QPushButton (tr ("&Remove name record"));
    addButton = new QPushButton (tr ("&Add name record"));
    closeButton = new QPushButton (tr ("C&lose"));

    saveButton->setEnabled (false);
    connect (saveButton, &QPushButton::clicked, this, &NameEdit::save);
    connect (addButton, &QPushButton::clicked, this, &NameEdit::addNameRecord);
    connect (removeButton, &QPushButton::clicked, this, &NameEdit::removeNameRecord);
    connect (closeButton, &QPushButton::clicked, this, &NameEdit::close);

    QVBoxLayout *layout = new QVBoxLayout ();

    QHBoxLayout *boxLayout = new QHBoxLayout ();
    boxLayout->addWidget (new QLabel ("Table format:"));
    boxLayout->addWidget (m_versionBox);
    layout->addLayout (boxLayout);

    layout->addWidget (m_tab);

    QHBoxLayout *buttLayout = new QHBoxLayout ();
    buttLayout->addWidget (saveButton);
    buttLayout->addWidget (addButton);
    buttLayout->addWidget (removeButton);
    buttLayout->addWidget (closeButton);
    layout->addLayout (buttLayout);

    window->setLayout (layout);
    setCentralWidget (window);

    m_nameStack->setActive ();
    m_valid = true;
}

NameEdit::~NameEdit () {
}

bool NameEdit::checkUpdate (bool can_cancel) {
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

bool NameEdit::isModified () {
    if (m_nameStack->isClean () && m_langStack->isClean ())
	return false;
    return true;
}

bool NameEdit::isValid () {
    return m_valid;
}

FontTable* NameEdit::table () {
    return m_table;
}

void NameEdit::closeEvent (QCloseEvent *event) {
    // If we are going to delete the font, ignore changes in table edits
    if (!isModified () || checkUpdate (true))
        m_table->clearEditor ();
    else
        event->ignore ();
}

QSize NameEdit::minimumSize () const {
    QSize size = m_nametab->size ();

    size += QSize (2, 2);
    return size;
}

QSize NameEdit::sizeHint () const {
    return minimumSize ();
}

void NameEdit::save () {
    m_name->packData ();
    m_nameStack->setClean ();
    m_langStack->setClean ();
    updateLabels ();
    emit (update (m_table));
}

void NameEdit::switchTableVersion (int index) {
    if (index == 0) {
	if (m_name->numLangTags () > 0) {
	    int choice = FontShepherd::postYesNoQuestion (
		QCoreApplication::tr ("Setting 'name' table format"),
		QCoreApplication::tr (
		    "Are you sure you want to switch to format 0?  "
		    "You will lose all custom language tags and assotiated "
		    "strings in the 'name' table. "),
		this);
	    if (choice == QMessageBox::No)
		return;
	    QAbstractItemModel *almod = m_langtab->model();
	    LangTagModel *lmod = qobject_cast<LangTagModel *> (almod);
	    lmod->clearModel ();
	    QAbstractItemModel *anmod = m_nametab->model();
	    NameRecordModel *nmod = qobject_cast<NameRecordModel *> (anmod);
	    nmod->clearCustomLangTagDependent ();
	}
	// NB: The following is possible in QT 5.2
	// m_tab->setTabVisible (1, false);
	m_tab->removeTab (1);
	m_langtab->setVisible (false);
    } else {
	// m_tab->setTabVisible (1, true);
	m_tab->removeTab (1);
	m_tab->addTab (m_langtab, QWidget::tr ("&Language Tags"));
	m_langtab->setVisible (true);
    }
}

void NameEdit::onTabChange (int index) {
    if (index == 0) {
	disconnect (addButton, &QPushButton::clicked, this, &NameEdit::addLangTag);
	disconnect (removeButton, &QPushButton::clicked, this, &NameEdit::removeLangTag);
	disconnect (addAction, &QAction::triggered, this, &NameEdit::addLangTag);
	disconnect (removeAction, &QAction::triggered, this, &NameEdit::removeLangTag);

	connect (addButton, &QPushButton::clicked, this, &NameEdit::addNameRecord);
	connect (removeButton, &QPushButton::clicked, this, &NameEdit::removeNameRecord);
	connect (addAction, &QAction::triggered, this, &NameEdit::addNameRecord);
	connect (removeAction, &QAction::triggered, this, &NameEdit::removeNameRecord);

	addButton->setText (tr ("&Add name record"));
	removeButton->setText (tr ("&Remove name record"));
	addAction->setText (tr ("&Add name record"));
	removeAction->setText (tr ("&Remove name record"));
	sortAction->setVisible (false);

	m_nameStack->setActive ();
    } else {
	disconnect (addButton, &QPushButton::clicked, this, &NameEdit::addNameRecord);
	disconnect (removeButton, &QPushButton::clicked, this, &NameEdit::removeNameRecord);
	disconnect (addAction, &QAction::triggered, this, &NameEdit::addNameRecord);
	disconnect (removeAction, &QAction::triggered, this, &NameEdit::removeNameRecord);

	connect (addButton, &QPushButton::clicked, this, &NameEdit::addLangTag);
	connect (removeButton, &QPushButton::clicked, this, &NameEdit::removeLangTag);
	connect (addAction, &QAction::triggered, this, &NameEdit::addLangTag);
	connect (removeAction, &QAction::triggered, this, &NameEdit::removeLangTag);

	addButton->setText (tr ("&Add language tag"));
	removeButton->setText (tr ("&Remove language tag"));
	addAction->setText (tr ("&Add language tag"));
	removeAction->setText (tr ("&Remove language tag"));
	sortAction->setVisible (true);

	m_langStack->setActive ();
    }
}

void NameEdit::customContextMenu (const QPoint &point) {
    QWidget *w = m_tab->currentWidget ();
    QTableView *tv = qobject_cast<QTableView*> (w);
    QModelIndex index = tv->indexAt (point);
    if (index.isValid ()) {
	QMenu menu (this);

	menu.addAction (addAction);
	menu.addAction (removeAction);
	menu.addAction (sortAction);
	menu.addSeparator ();
	menu.addAction (undoAction);
	menu.addAction (redoAction);

	menu.exec (tv->viewport ()->mapToGlobal (point));
    }
}

void NameEdit::addNameRecord () {
    name_record rec;

    AddNameDialog dlg (m_name, this);
    switch (dlg.exec ()) {
      case QDialog::Accepted:
	break;
      case QDialog::Rejected:
	return;
    }
    rec.platformID = dlg.platform ();
    rec.encodingID = dlg.encoding ();
    rec.languageID = dlg.language ();
    rec.nameID = dlg.nameType ();
    rec.name = dlg.nameText ();
    int row = dlg.rowAvailable ();

    QAbstractItemModel *absmod = m_nametab->model ();
    NameRecordModel *nmod = qobject_cast<NameRecordModel *> (absmod);
    QList<name_record> arg;
    arg << rec;

    NameRecordCommand *cmd = new NameRecordCommand (nmod, arg, row);
    cmd->setText (tr ("Add name record"));
    m_nameStack->push (cmd);
}

void NameEdit::removeNameRecord () {
    QItemSelectionModel *curidx = m_nametab->selectionModel ();
    if (curidx->hasSelection ()) {
	QModelIndex rowidx = curidx->selectedRows ().first ();
	QAbstractItemModel *absmod = m_nametab->model ();
	NameRecordModel *mod = qobject_cast<NameRecordModel *> (absmod);

	NameRecordCommand *cmd = new NameRecordCommand (mod, rowidx.row (), curidx->selectedRows ().size ());
	cmd->setText (tr ("Delete name record"));
	m_nameStack->push (cmd);
    }
}

void NameEdit::addLangTag () {
    AddLangTagDialog dlg (m_name, this);
    switch (dlg.exec ()) {
      case QDialog::Accepted:
	break;
      case QDialog::Rejected:
	return;
    }
    QString tag = dlg.langTag ();

    QAbstractItemModel *absmod = m_langtab->model ();
    LangTagModel *lmod = qobject_cast<LangTagModel *> (absmod);
    QList<QString> arg;
    arg << tag;

    QItemSelectionModel *curidx = m_langtab->selectionModel ();
    QModelIndexList sel = curidx->selectedRows ();
    int row = sel.size () > 0 ? sel.last ().row () + 1: 0;

    LangTagCommand *cmd = new LangTagCommand (lmod, arg, row);
    cmd->setText (tr ("Add language tag"));
    m_langStack->push (cmd);
}

void NameEdit::removeLangTag () {
    QItemSelectionModel *curidx = m_langtab->selectionModel ();
    if (curidx->hasSelection ()) {
	QModelIndex rowidx = curidx->selectedRows ().first ();
	QAbstractItemModel *absmod = m_langtab->model ();
	LangTagModel *lmod = qobject_cast<LangTagModel *> (absmod);

	LangTagCommand *cmd = new LangTagCommand (lmod, rowidx.row (), curidx->selectedRows ().size ());
	cmd->setText (tr ("Delete language tag"));
	m_langStack->push (cmd);
    }
}

void NameEdit::sortLangTags () {
    QItemSelectionModel *curidx = m_langtab->selectionModel ();
    QModelIndex rowidx = curidx->selectedRows ().first ();
    QAbstractItemModel *absmod = m_langtab->model ();
    LangTagModel *lmod = qobject_cast<LangTagModel *> (absmod);

    SortLangTagsCommand *cmd = new SortLangTagsCommand
	(lmod, rowidx.row (), curidx->selectedRows ().size ());
    cmd->setText (tr ("Sort language tags"));
    m_langStack->push (cmd);
}

void NameEdit::updateTableSelection (int row, int count) {
    QString cname = sender ()->metaObject ()->className ();
    QTableView *tv = (cname == "NameRecordModel") ? m_nametab : m_langtab;

    QModelIndex first_idx = tv->model ()->index (row, 0, QModelIndex ());
    tv->selectionModel ()->setCurrentIndex (first_idx,
	QItemSelectionModel::Clear | QItemSelectionModel::Rows);

    for (int i=row; i<row+count; i++) {
	QModelIndex add_idx = tv->model ()->index (i, 0, QModelIndex ());
	tv->selectionModel ()->select (add_idx,
	    QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    tv->scrollTo (first_idx);
}

void NameEdit::updateLabels () {
    QString ntitle = m_tab->tabText (0);
    bool ntitle_has_asterisk = ntitle.startsWith (QChar ('*'));
    QString ltitle = m_tab->tabText (1);
    bool ltitle_has_asterisk = ltitle.startsWith (QChar ('*'));

    if (ntitle_has_asterisk && !m_name->namesModified ()) {
        m_tab->setTabText (0, ntitle.remove (0, 1));
    } else if (!ntitle_has_asterisk && m_name->namesModified ()) {
        m_tab->setTabText (0, ntitle.prepend ('*'));
    }
    if (ltitle_has_asterisk && !m_name->langTagsModified ()) {
        m_tab->setTabText (1, ltitle.remove (0, 1));
    } else if (!ltitle_has_asterisk && m_name->langTagsModified ()) {
        m_tab->setTabText (1, ltitle.prepend ('*'));
    }
    saveButton->setEnabled (m_name->namesModified () | m_name->langTagsModified ());
}

void NameEdit::setMenuBar () {
    QMenuBar *mb = this->menuBar ();
    QMenu *fileMenu, *editMenu;

    saveAction = new QAction (tr ("&Compile"), this);
    addAction = new QAction (tr ("&Add name record"), this);
    sortAction = new QAction (tr ("&Sort language tags"), this);
    sortAction->setVisible (false);
    removeAction = new QAction (tr ("&Remove name record"), this);
    closeAction = new QAction (tr ("C&lose"), this);

    saveAction->setEnabled (false);
    connect (saveAction, &QAction::triggered, this, &NameEdit::save);
    connect (closeAction, &QAction::triggered, this, &NameEdit::close);
    connect (addAction, &QAction::triggered, this, &NameEdit::addNameRecord);
    connect (removeAction, &QAction::triggered, this, &NameEdit::removeNameRecord);
    connect (sortAction, &QAction::triggered, this, &NameEdit::sortLangTags);

    saveAction->setShortcut (QKeySequence::Save);
    closeAction->setShortcut (QKeySequence::Close);

    undoAction = m_uGroup->createUndoAction (this, tr ("&Undo"));
    redoAction = m_uGroup->createRedoAction (this, tr ("Re&do"));
    undoAction->setShortcut (QKeySequence::Undo);
    redoAction->setShortcut (QKeySequence::Redo);

    fileMenu = mb->addMenu (tr ("&File"));
    fileMenu->addAction (saveAction);
    fileMenu->addSeparator ();
    fileMenu->addAction (closeAction);

    editMenu = mb->addMenu (tr ("&Edit"));
    editMenu->addAction (addAction);
    editMenu->addAction (removeAction);
    editMenu->addSeparator ();
    editMenu->addAction (undoAction);
    editMenu->addAction (redoAction);
}

void NameEdit::fillNameTable () {
    m_nameStack = new QUndoStack (m_uGroup.get ());

    m_nameModel = std::unique_ptr<QAbstractItemModel> (new NameRecordModel (m_name));
    NameRecordModel *modptr = dynamic_cast<NameRecordModel *> (m_nameModel.get ());
    QAbstractItemDelegate *dlg = new TextDelegate (m_nameStack, m_nametab);

    connect (m_nameStack, &QUndoStack::cleanChanged, modptr, &NameRecordModel::setNamesClean);
    connect (modptr, &NameRecordModel::needsSelectionUpdate, this, &NameEdit::updateTableSelection);
    connect (modptr, &NameRecordModel::needsLabelUpdate, this, &NameEdit::updateLabels);
    m_nametab->setModel (modptr);
    m_nametab->setItemDelegateForColumn (4, dlg);

    setEditWidth (m_nametab);
}

void NameEdit::fillLangTable () {
    m_langStack = new QUndoStack (m_uGroup.get ());

    m_langModel = std::unique_ptr<QAbstractItemModel> (new LangTagModel (m_name, 0x8000));
    LangTagModel *modptr = dynamic_cast<LangTagModel *> (m_langModel.get ());
    QAbstractItemDelegate *dlg = new TextDelegate (m_langStack, m_langtab);

    connect (m_langStack, &QUndoStack::cleanChanged, modptr, &LangTagModel::setLanguagesClean);
    connect (modptr, &LangTagModel::needsSelectionUpdate, this, &NameEdit::updateTableSelection);
    connect (modptr, &LangTagModel::needsLabelUpdate, this, &NameEdit::updateLabels);
    m_langtab->setModel (modptr);
    m_langtab->setItemDelegateForColumn (0, dlg);

    m_langtab->horizontalHeader ()->setStretchLastSection (true);
    m_langtab->setSelectionBehavior (QAbstractItemView::SelectRows);
    m_langtab->setSelectionMode (QAbstractItemView::ContiguousSelection);
    if (m_langModel->rowCount (QModelIndex ()) > 0)
	m_langtab->selectRow (0);
}

// Custom models, used to display table/subtable data in a table or a tree form

NameRecordModel::NameRecordModel (NameTable *name, QWidget *parent) :
    QAbstractTableModel (parent), m_name (name)/*, m_parent (parent)*/ {
}

NameRecordModel::~NameRecordModel () {
}

int NameRecordModel::rowCount (const QModelIndex &parent) const {
    Q_UNUSED (parent);
    return m_name->numNameRecords ();
}

int NameRecordModel::columnCount (const QModelIndex &parent) const {
    Q_UNUSED (parent);
    return 5;
}

static QString first_line (const QString &instr) {
    if (instr.contains (QChar::LineFeed) || instr.contains (QChar::CarriageReturn)) {
	QStringList list = instr.split (QRegularExpression ("[\r\n]"), Qt::SkipEmptyParts);
	return list[0].append ("...");
    }
    return instr;
};

static QString custom_language_name (const QString &ltag, int code) {
    return QString ("0x%1: %2").arg (code, 4, 16).arg (ltag);
}

QVariant NameRecordModel::data (const QModelIndex &index, int role) const {
    struct name_record *rec = m_name->nameRecord (index.row ());
    QString slang = rec->languageID >= 0x8000 ?
	custom_language_name (m_name->langTagRecord (rec->languageID - 0x8000), rec->languageID) :
	QString::fromStdString (rec->strLanguage ());

    switch (role) {
      case Qt::ToolTipRole:
      case Qt::DisplayRole:
	switch (index.column ()) {
	  case 0:
	    return QString::fromStdString (rec->strPlatform ());
	  case 1:
	    return QString::fromStdString (rec->strEncoding ());
	  case 2:
	    return slang;
	  case 3:
	    return QString::fromStdString (rec->nameDescription ());
	  case 4:
	    return first_line (rec->name);
	}
	break;
      case Qt::EditRole:
	switch (index.column ()) {
	  case 0:
	    return rec->platformID;
	  case 1:
	    return rec->encodingID;
	  case 2:
	    return rec->languageID;
	  case 3:
	    return rec->nameID;
	  case 4:
	    return rec->name;
	}
    }
    return QVariant ();
}

bool NameRecordModel::setData (const QModelIndex &index, const QVariant &value, int role) {
    if (index.isValid() && index.column () == 4) {
	if (role == Qt::EditRole) {
	    QString s = value.toString ();
	    m_name->setNameString (index.row (), s);
	    emit dataChanged (index, index);
	    return true;
	}
    }
    return false;
}

Qt::ItemFlags NameRecordModel::flags (const QModelIndex &index) const {
    Qt::ItemFlags ret = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column () == 4) {
	ret |= Qt::ItemIsEditable;
    }
    return ret;
}

QVariant NameRecordModel::headerData (int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
	switch (section) {
	  case 0:
	    return QWidget::tr ("Platform");
	  case 1:
	    return QWidget::tr ("Encoding");
	  case 2:
	    return QWidget::tr ("Language");
	  case 3:
	    return QWidget::tr ("Name description");
	  case 4:
	    return QWidget::tr ("Name string");
	}
    } else if (orientation == Qt::Vertical && role == Qt::DisplayRole) {
	return QString::number (section+1);
    }
    return QVariant ();
}

bool NameRecordModel::removeRows (int row, int count, const QModelIndex &index) {
    Q_UNUSED (index);

    beginRemoveRows (QModelIndex (), row, row+count-1);
    for (int i=0; i<count; i++)
	m_name->removeNameRecord ((uint16_t) row);
    endRemoveRows ();
    emit needsSelectionUpdate (row < m_name->numNameRecords () ? row : row-1, 1);
    return true;
}

void NameRecordModel::beginResetModel () {
    QAbstractTableModel::beginResetModel ();
}

void NameRecordModel::endResetModel () {
    QAbstractTableModel::endResetModel ();
}

QModelIndex NameRecordModel::insertRows (QList<name_record> &input, int row) {
    uint16_t firstrow, lastrow;
    uint16_t count = input.size ();
    beginInsertRows (QModelIndex (), row, row+count-1);
    firstrow = lastrow = m_name->insertNameRecord (input[0]);
    for (uint16_t i=1; i<count; i++)
	lastrow = m_name->insertNameRecord (input[i]);
    endInsertRows ();
    emit needsSelectionUpdate (firstrow, count);
    return index (lastrow, 0, QModelIndex ());
}

void NameRecordModel::clearCustomLangTagDependent () {
    beginResetModel ();
    m_name->clearCustomLangTagDependent ();
    endResetModel ();
    emit needsSelectionUpdate (0, 1);
}

void NameRecordModel::setNamesClean (bool clean) {
    m_name->setNamesModified (!clean);
    emit needsLabelUpdate ();
}

LangTagModel::LangTagModel (NameTable *name, int shift, QWidget *parent) :
    QAbstractTableModel (parent), m_name (name), m_shift (shift) {
}

LangTagModel::~LangTagModel () {
}

int LangTagModel::rowCount (const QModelIndex &parent) const {
    Q_UNUSED (parent);
    return m_name->numLangTags ();
}

int LangTagModel::columnCount (const QModelIndex &parent) const {
    Q_UNUSED (parent);
    return 1;
}

QVariant LangTagModel::data (const QModelIndex &index, int role) const {
    QString name = m_name->langTagRecord (index.row ());

    switch (role) {
      case Qt::DisplayRole:
      case Qt::EditRole:
	switch (index.column ()) {
	  case 0:
	    return name;
	}
	break;
    }
    return QVariant ();
}

bool LangTagModel::setData (const QModelIndex &index, const QVariant &value, int role) {
    if (index.isValid() && index.column () == 0) {
	if (role == Qt::EditRole) {
	    m_name->setLangTag (index.row () + m_shift, value.toString ());
	    emit dataChanged (index, index);
	    return true;
	}
    }
    return false;
}

Qt::ItemFlags LangTagModel::flags (const QModelIndex &index) const {
    Qt::ItemFlags ret = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column () == 0) {
	ret |= Qt::ItemIsEditable;
    }
    return ret;
}

QVariant LangTagModel::headerData (int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
	switch (section) {
	  case 0:
	    return QWidget::tr ("Language tag");
	}
    } else if (orientation == Qt::Vertical && role == Qt::DisplayRole) {
	return QString ("0x%1").arg (section+m_shift, 0, 16);
    }
    return QVariant ();
}

bool LangTagModel::removeRows (int row, int count, const QModelIndex &index) {
    Q_UNUSED (index);

    beginRemoveRows (QModelIndex (), row, row+count-1);
    for (int i=row; i<row+count; i++)
	m_name->removeLangTag (row + m_shift);
    endRemoveRows ();
    emit needsSelectionUpdate (row < m_name->numLangTags () ? row : row-1, 1);
    return true;
}

QModelIndex LangTagModel::insertRows (QList<QString> &input, int row) {
    uint16_t firstrow, lastrow;
    uint16_t count = input.size ();
    beginInsertRows (QModelIndex (), row, row+count-1);
    firstrow = lastrow = m_name->insertLangTag (input[0], row);
    for (uint16_t i=1; i<count; i++)
	lastrow = m_name->insertLangTag (input[i], row);
    endInsertRows ();
    emit needsSelectionUpdate (firstrow, count);
    return index (lastrow, 0, QModelIndex ());
}

void LangTagModel::sortRows () {
    beginResetModel ();
    m_name->sortLangTags ();
    endResetModel ();
    emit needsSelectionUpdate (rowCount (QModelIndex ()) - 1, 1);
}

void LangTagModel::clearModel () {
    beginResetModel ();
    m_name->clearLangTags ();
    endResetModel ();
}

void LangTagModel::unSortRows (QList<QString> &order, int row, int count) {
    beginResetModel ();
    m_name->setLangTagOrder (order);
    endResetModel ();
    emit needsSelectionUpdate (row, count);
}

void LangTagModel::setLanguagesClean (bool clean) {
    m_name->setLangTagsModified (!clean);
    emit needsLabelUpdate ();
}

// Delegate classes for table cell editing

TextDelegate::TextDelegate (QUndoStack *us, QObject *parent) :
    QStyledItemDelegate (parent), m_ustack (us) {
}

QWidget* TextDelegate::createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (option);
    QString item_text = index.model ()->data (index, Qt::EditRole).toString ();
    if (item_text.contains (QChar::LineFeed) || item_text.contains (QChar::CarriageReturn)) {
	return new MultilineInputDialog (
	    tr ("Edit multiline name string"),
	    tr ("Edit multiline name string:"),
	    parent);
    } else {
	return new QLineEdit (parent);
    }
}

void TextDelegate::setEditorData (QWidget *editor, const QModelIndex &index) const {
    QString item_text = index.model ()->data (index, Qt::EditRole).toString ();
    if (editor->isWindow ()) {
	MultilineInputDialog *mdlg = qobject_cast<MultilineInputDialog *> (editor);
	mdlg->setText (item_text);
	mdlg->open ();
	// See comment to MultilineInputDialog::ensureFocus () for explanation
	mdlg->ensureFocus ();
    } else {
	QLineEdit *le = qobject_cast<QLineEdit *> (editor);
	le->setText (item_text);
    }
}

void TextDelegate::setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
    QString text;
    bool accepted = false;
    if (editor->isWindow ()) {
	MultilineInputDialog *mdlg = qobject_cast<MultilineInputDialog *> (editor);
	if (mdlg->result () == QDialog::Accepted) {
	    text = mdlg->text ();
	    accepted = true;
	}
    } else {
	QLineEdit *le = qobject_cast<QLineEdit *> (editor);
	text = le->text ();
	accepted = true;
    }
    if (accepted) {
	if (m_ustack) {
	    SetStringCommand *cmd = new SetStringCommand (model, index, text);
	    cmd->setText (tr ("Edit text"));
	    m_ustack->push (cmd);
	} else
	    model->setData (index, text, Qt::EditRole);
    }
}

void TextDelegate::updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (index);
    if (!editor->isWindow ()) editor->setGeometry (option.rect);
}

// Commands to (un)do various deletions or insertions

NameRecordCommand::NameRecordCommand (NameRecordModel *model, int row, int count) :
    m_model (model), m_row (row), m_count (count), m_remove (true) {

    for (int i=m_row; i<m_row+m_count; i++) {
	QModelIndex idx0 = m_model->index (i, 0, QModelIndex ());
	QModelIndex idx1 = m_model->index (i, 1, QModelIndex ());
	QModelIndex idx2 = m_model->index (i, 2, QModelIndex ());
	QModelIndex idx3 = m_model->index (i, 3, QModelIndex ());
	QModelIndex idx4 = m_model->index (i, 4, QModelIndex ());
	struct name_record rec = {
	    (uint16_t) m_model->data (idx0, Qt::EditRole).toUInt (),
	    (uint16_t) m_model->data (idx1, Qt::EditRole).toUInt (),
	    (uint16_t) m_model->data (idx2, Qt::EditRole).toUInt (),
	    (uint16_t) m_model->data (idx3, Qt::EditRole).toUInt (),
	    m_model->data (idx4, Qt::EditRole).toString (), 0
	};
	m_data << rec;
    }
}

NameRecordCommand::NameRecordCommand (NameRecordModel *model, const QList<name_record> &input, int row) :
    m_model (model), m_row (row), m_remove (false) {
    m_count = input.size ();
    m_data << input;
}

void NameRecordCommand::redo () {
    if (m_remove)
	m_model->removeRows (m_row, m_count, m_model->index (m_row, 1, QModelIndex ()));
    else
	m_model->insertRows (m_data, m_row);
}

void NameRecordCommand::undo () {
    if (m_remove)
	m_model->insertRows (m_data, m_row);
    else
	m_model->removeRows (m_row, m_count, m_model->index (m_row, 1, QModelIndex ()));
}

LangTagCommand::LangTagCommand (LangTagModel *model, int row, int count) :
    m_model (model), m_row (row), m_count (count), m_remove (true) {

    for (int i=m_row; i<m_row+m_count; i++) {
	QModelIndex idx0 = m_model->index (i, 0, QModelIndex ());
	QString s = m_model->data (idx0, Qt::EditRole).toString ();
	m_data << s;
    }
}

LangTagCommand::LangTagCommand (LangTagModel *model, const QList<QString> &input, int row) :
    m_model (model), m_row (row), m_remove (false) {
    m_count = input.size ();
    m_data << input;
}

void LangTagCommand::redo () {
    if (m_remove)
	m_model->removeRows (m_row, m_count, m_model->index (m_row, 0, QModelIndex ()));
    else
	m_model->insertRows (m_data, m_row);
}

void LangTagCommand::undo () {
    if (m_remove)
	m_model->insertRows (m_data, m_row);
    else
	m_model->removeRows (m_row, m_count, m_model->index (m_row, 0, QModelIndex ()));
}

SortLangTagsCommand::SortLangTagsCommand (LangTagModel *model, int row, int count) :
    m_model (model), m_row (row), m_count (count) {
    m_data.reserve (m_model->rowCount (QModelIndex ()));
    for (int i=0; i<m_model->rowCount (QModelIndex ()); i++) {
	QModelIndex idx = m_model->index (i, 0, QModelIndex ());
	QString s = m_model->data (idx, Qt::EditRole).toString ();
	m_data.push_back (s);
    }
}

void SortLangTagsCommand::redo () {
    m_model->sortRows ();
}

void SortLangTagsCommand::undo () {
    m_model->unSortRows (m_data, m_row, m_count);
}


SetStringCommand::SetStringCommand (QAbstractItemModel *model, const QModelIndex &index, QString text) :
    m_model (model), m_index (index), m_new (text) {

    m_old = m_model->data (m_index, Qt::EditRole).toString ();
}

void SetStringCommand::redo () {
    m_model->setData (m_index, m_new, Qt::EditRole);
}

void SetStringCommand::undo () {
    m_model->setData (m_index, m_old, Qt::EditRole);
}

// Custom dialogs used to add name and language tag records

AddNameDialog::AddNameDialog (NameTable *name, QWidget *parent) :
    QDialog (parent), m_name (name) {

    setWindowTitle (tr ("Add name record"));

    QVBoxLayout *layout = new QVBoxLayout ();
    QGridLayout *glay = new QGridLayout ();
    layout->addLayout (glay);

    glay->addWidget (new QLabel ("Select platform ID"), 0, 0);
    m_platformBox = new QComboBox ();
    glay->addWidget (m_platformBox, 0, 1);

    glay->addWidget (new QLabel ("Select encoding ID"), 1, 0);
    m_encodingBox = new QComboBox ();
    glay->addWidget (m_encodingBox, 1, 1);

    glay->addWidget (new QLabel ("Select language ID"), 2, 0);
    m_languageBox = new QComboBox ();
    glay->addWidget (m_languageBox, 2, 1);

    glay->addWidget (new QLabel ("Select OpenType Name ID"), 3, 0);
    m_nameTypeBox = new QComboBox ();
    glay->addWidget (m_nameTypeBox, 3, 1);

    glay->addWidget (new QLabel ("Input OpenType Name text:"), 4, 0, 1, 1);
    m_editBox = new QPlainTextEdit ();
    glay->addWidget (m_editBox, 5, 0, 3, 2);

    fillBoxes ();

    QHBoxLayout *butt_layout = new QHBoxLayout ();
    QPushButton* okBtn = new QPushButton ("OK");
    connect (okBtn, &QPushButton::clicked, this, &QDialog::accept);
    butt_layout->addWidget( okBtn );

    QPushButton* cancelBtn = new QPushButton (tr ("Cancel"));
    connect (cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    butt_layout->addWidget( cancelBtn );
    layout->addLayout (butt_layout);

    setLayout (layout);
}

int AddNameDialog::platform () const {
    QVariant ret = m_platformBox->itemData (m_platformBox->currentIndex ());
    if (ret != QVariant::Invalid)
	return ret.toInt ();
    return -1;
}

int AddNameDialog::encoding () const {
    QVariant ret = m_encodingBox->itemData (m_encodingBox->currentIndex ());
    if (ret != QVariant::Invalid)
	return ret.toInt ();
    return -1;
}

int AddNameDialog::language () const {
    QVariant ret = m_languageBox->itemData (m_languageBox->currentIndex ());
    if (ret != QVariant::Invalid)
	return ret.toInt ();
    return -1;
}

int AddNameDialog::nameType () const {
    QVariant ret = m_nameTypeBox->itemData (m_nameTypeBox->currentIndex ());
    if (ret != QVariant::Invalid)
	return ret.toInt ();
    return -1;
}

QString AddNameDialog::nameText () const {
    return m_editBox->toPlainText ();
}

int AddNameDialog::rowAvailable () const {
    return m_row;
}

void AddNameDialog::accept () {
    name_record rec;
    rec.platformID = platform ();
    rec.encodingID = encoding ();
    rec.languageID = language ();
    rec.nameID = nameType ();
    rec.name = nameText ();
    bool can_insert = m_name->checkNameRecord (rec, &m_row);

    if (can_insert)
	QDialog::accept ();
    else {
        FontShepherd::postError (
	    tr ("Can't add name record"),
	    tr ("There is already such a record in the 'name' table."),
            this);
    }
}

void AddNameDialog::fillBoxes () {
    const std::vector<FontShepherd::numbered_string> &plat_lst = FontShepherd::platforms;
    const std::vector<FontShepherd::numbered_string> &name_lst = m_name->nameList ();

    // Exclude 'custom' (ID = 4) platform, as MS spec says it should not
    // be used for strings in the 'name' table.
    for (size_t i=0; i<4; i++)
	m_platformBox->addItem (QString
	    ("%1: %2").arg (plat_lst[i].ID).arg (plat_lst[i].name.c_str ()), plat_lst[i].ID);
    m_platformBox->setEditable (false);
    QStandardItemModel *model = qobject_cast<QStandardItemModel *> (m_platformBox->model ());
    QStandardItem *item = model->item (2);
    item->setFlags (item->flags () & ~Qt::ItemIsEnabled);
    connect (m_platformBox, static_cast<void (QComboBox::*)(int)> (&QComboBox::currentIndexChanged),
	this, &AddNameDialog::setPlatformSpecific);
    m_platformBox->setCurrentIndex (3);

    for (size_t i=0; i<name_lst.size (); i++)
	m_nameTypeBox->addItem (QString
	    ("%1: %2").arg (name_lst[i].ID).arg (name_lst[i].name.c_str ()), name_lst[i].ID);
    int str_idx = m_nameTypeBox->findData (1, Qt::UserRole);
    m_nameTypeBox->setCurrentIndex (str_idx >=0 ? str_idx : 0);
}

void AddNameDialog::setPlatformSpecific (int plat) {
    const std::vector<FontShepherd::numbered_string> &lst = FontShepherd::specificList (plat);
    QStandardItemModel *model;
    QStandardItem *item;

    m_encodingBox->clear ();
    for (size_t i=0; i<lst.size (); i++)
	m_encodingBox->addItem (QString
	    ("%1: %2").arg (lst[i].ID).arg (lst[i].name.c_str ()), lst[i].ID);
    m_encodingBox->setEditable (false);
    if (plat == 3) { // Microsoft
	model = qobject_cast<QStandardItemModel *> (m_encodingBox->model ());
	for (int i=7; i<10; i++) {
	    item = model->item (i);
	    item->setFlags (item->flags () & ~Qt::ItemIsEnabled);
	}
    }
    m_languageBox->view ()->setVerticalScrollBarPolicy (Qt::ScrollBarAsNeeded);

    switch (plat) {
      case 0:
        {
	    m_encodingBox->setCurrentIndex (3);
	    m_languageBox->clear ();
	    m_languageBox->addItem ("0: Undefined", 0);
	    for (uint16_t i; i<m_name->numLangTags (); i++)
		m_languageBox->addItem (custom_language_name (m_name->langTagRecord (i), i+0x8000), i+0x8000);
	    m_languageBox->addItem ("0xFFFF: Undefined", 0xffff);
	    m_languageBox->setCurrentIndex (0);
	}
	break;
      case 1:
        {
	    m_encodingBox->setCurrentIndex (0);
	    const std::vector<FontShepherd::numbered_string> lang_lst = FontShepherd::sortedMacLanguages ();
	    m_languageBox->clear ();
	    for (size_t i=0; i<lang_lst.size (); i++)
		m_languageBox->addItem (QString::fromStdString (lang_lst[i].name), lang_lst[i].ID);
	    m_languageBox->setCurrentIndex (m_languageBox->findData (0, Qt::UserRole));
	    // No custom Language tags for Mac platform, as this platform doesn't support Unicode strings
	    // (and 'name' format 1 is an MS extension unknown to Apple anyway)
	}
	break;
      case 3:
        {
	    m_encodingBox->setCurrentIndex (1);
	    const std::vector<FontShepherd::ms_language> &lang_lst = FontShepherd::windowsLanguages;
	    m_languageBox->clear ();
	    for (size_t i=0; i<lang_lst.size (); i++)
		m_languageBox->addItem (QString ("%1 (%2)").arg
		    (lang_lst[i].language.c_str ()).arg (lang_lst[i].region.c_str ()), lang_lst[i].code);
	    for (uint16_t i; i<m_name->numLangTags (); i++)
		m_languageBox->addItem (custom_language_name (m_name->langTagRecord (i), i+0x8000), i+0x8000);
	    m_languageBox->setCurrentIndex (m_languageBox->findData (0x409, Qt::UserRole));
	}
    }
}

AddLangTagDialog::AddLangTagDialog (NameTable *name, QWidget *parent) :
    QDialog (parent), m_name (name) {

    setWindowTitle (tr ("Add name record"));

    QVBoxLayout *layout = new QVBoxLayout ();
    QGridLayout *glay = new QGridLayout;
    layout->addLayout (glay);

    glay->addWidget (new QLabel (tr ("Custom language tag:")), 0, 0);
    m_editBox = new QLineEdit ();
    glay->addWidget (m_editBox, 0, 1);

    QHBoxLayout *butt_layout = new QHBoxLayout ();
    QPushButton* okBtn = new QPushButton ("OK");
    connect (okBtn, &QPushButton::clicked, this, &QDialog::accept);
    butt_layout->addWidget( okBtn );

    QPushButton* cancelBtn = new QPushButton (tr ("Cancel"));
    connect (cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    butt_layout->addWidget( cancelBtn );
    layout->addLayout (butt_layout);

    setLayout (layout);
}

QString AddLangTagDialog::langTag () const {
    return m_editBox->text ();
}

void AddLangTagDialog::accept () {
    QString tag = langTag ();

    if (m_name->checkLangTag (tag))
	QDialog::accept ();
    else {
        FontShepherd::postError (
	    tr ("Can't add a custom language tag"),
	    tr ("There is already such a language tag in the 'name' table."),
            this);
    }
}

MultilineInputDialog::MultilineInputDialog (QString title, QString prompt, QWidget *parent) :
    QDialog (parent), m_accepted (false) {

    setWindowTitle (title);

    QVBoxLayout *layout = new QVBoxLayout ();
    QGridLayout *glay = new QGridLayout;
    layout->addLayout (glay);

    glay->addWidget (new QLabel (prompt), 0, 0);
    m_editBox = new QPlainTextEdit ();
    m_editBox->setLineWrapMode (QPlainTextEdit::NoWrap);
    glay->addWidget (m_editBox, 1, 0);

    QHBoxLayout *butt_layout = new QHBoxLayout ();
    QPushButton* okBtn = new QPushButton ("OK");
    connect (okBtn, &QPushButton::clicked, this, &QDialog::accept);
    butt_layout->addWidget( okBtn );

    QPushButton* cancelBtn = new QPushButton (tr ("Cancel"));
    connect (cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    butt_layout->addWidget( cancelBtn );
    layout->addLayout (butt_layout);

    setLayout (layout);
    m_editBox->setFocus ();
}

QSize MultilineInputDialog::sizeHint () const {
    QFontMetrics fm = m_editBox->fontMetrics ();
    int w = fm.boundingRect ("This Font Software is licensed under the SIL Open Font License, Version 1.1.").width ();
    int h = fm.lineSpacing () * 20;
    return QSize (w, h);
}

void MultilineInputDialog::setText (const QString text) {
    m_editBox->setPlainText (text);
}

QString MultilineInputDialog::text () const {
    return m_editBox->toPlainText ();
}

void MultilineInputDialog::ensureFocus () {
    /* It is perfectly legal to have a delegate editor implemented as a separate
     * window, but the delegate class checks if widget has lost its focus and,
     * if so, deletes the editor widget and calls setModelData.
     * For some reason the dialog window created as an editor widget doesn't
     * initially get the input focus and so can be deleted by the delegate
     * if user, for example, clicks at its title.
     * An immediate setFocus () call doesn't fix this, hence the trick with
     * singleShot () (see Ariya Hidayat's answer at
     * https://stackoverflow.com/questions/526761/set-qlineedit-focus-in-qt )
     * If this ever stops working, another solution is to completely redefine
     * eventFilter () for our delegate (see QAbstractItemDelegatePrivate::editorEventFilter ()
     * in qabstractitemdelegate.cpp, where the actual work is done). */
    // NB: new syntax for this call is possible beginning from QT 5.4,
    // but currently using the SLOT macro
    QTimer::singleShot (0, m_editBox, SLOT (setFocus ()));
}
