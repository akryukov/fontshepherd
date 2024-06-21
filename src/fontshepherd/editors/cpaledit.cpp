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

#include "sfnt.h"
#include "editors/cpaledit.h" // also includes tables.h
#include "tables/colr.h"
#include "tables/glyphnames.h"
#include "tables/name.h"
#include "editors/nameedit.h"

#include "fs_notify.h"
#include "commonlists.h"
#include "commondelegates.h"

// Main class, representing the cpal table editing window

CpalEdit::CpalEdit (std::shared_ptr<FontTable> tptr, sFont *fnt, QWidget *parent) :
    TableEdit (parent, Qt::Window), m_font (fnt) {

    setAttribute (Qt::WA_DeleteOnClose);
    setWindowTitle (QString("CPAL - ").append (m_font->fontname));

    m_cpal = std::dynamic_pointer_cast<CpalTable> (tptr);
    m_name = std::dynamic_pointer_cast<NameTable> (m_font->sharedTable (CHR ('n','a','m','e')));

    std::vector<FontShepherd::numbered_string> name_lst;
    name_lst.reserve (m_cpal->numPaletteEntries ());
    for (size_t i=0; i<m_cpal->numPaletteEntries (); i++) {
	if (m_cpal->m_paletteLabelIndices[i] != 0xFFFF) {
	    name_lst.push_back (FontShepherd::numbered_string
		{m_cpal->m_paletteLabelIndices[i], tr ("Palette entry name").toStdString ()});
	}
    }

    m_nameProxy = std::unique_ptr<NameProxy> (new NameProxy (m_name.get ()));
    m_nameProxy->update (name_lst);

    m_entryNames.reserve (m_cpal->numPaletteEntries ());
    for (uint16_t i=0; i<m_cpal->numPaletteEntries (); i++)
	m_entryNames.push_back (entryLabel (i));

    QWidget *window = new QWidget ();
    QVBoxLayout *layout = new QVBoxLayout ();

    m_tab = new QTabWidget ();
    layout->addWidget (m_tab);
    m_cpalTab = new QWidget ();
    m_tab->addTab (m_cpalTab, QWidget::tr ("&General"));
    m_tab->setCurrentWidget (m_cpalTab);

    QGridLayout *cpal_lay = new QGridLayout ();
    m_cpalTab->setLayout (cpal_lay);

    cpal_lay->addWidget (new QLabel (tr ("CPAL table version")), 0, 0);
    m_cpalVersionBox = new QSpinBox ();
    cpal_lay->addWidget (m_cpalVersionBox, 0, 1);
    connect (m_cpalVersionBox, static_cast<void (QSpinBox::*)(int)> (&QSpinBox::valueChanged),
	this, &CpalEdit::setTableVersion);

    cpal_lay->addWidget (new QLabel (tr ("Number of palettes")), 1, 0);
    m_numPalettesBox = new QSpinBox ();
    cpal_lay->addWidget (m_numPalettesBox, 1, 1);
    connect (m_numPalettesBox, static_cast<void (QSpinBox::*)(int)> (&QSpinBox::valueChanged),
	this, &CpalEdit::setPalettesNumber);

    cpal_lay->addWidget (new QLabel (tr ("Number of palette entries")), 2, 0);
    m_numEntriesBox = new QSpinBox ();
    cpal_lay->addWidget (m_numEntriesBox, 2, 1);
    connect (m_numEntriesBox, static_cast<void (QSpinBox::*)(int)> (&QSpinBox::valueChanged),
	this, &CpalEdit::setEntriesNumber);

    m_entryIdList = new QTableView ();
    cpal_lay->addWidget (m_entryIdList, 0, 2, 3, 1);

    cpal_lay->addWidget (new QLabel (tr ("Palette entry names:")), 4, 0, 1, 3);
    m_entryNameView = new QTableView ();
    cpal_lay->addWidget (m_entryNameView, 5, 0, 1, 3);

    m_palContainer = new QTabWidget ();
    m_tab->addTab (m_palContainer, QWidget::tr ("CPAL p&alettes"));
    for (uint16_t i=0; i<m_cpal->numPalettes (); i++) {
	auto ptab = new PaletteTab (m_cpal->palette (i), m_name.get (), i, m_entryNames);
	ptab->setTableVersion (m_cpal->version ());
	connect (ptab, &PaletteTab::needsLabelUpdate, this, &CpalEdit::updatePaletteLabel);
	connect (ptab, &PaletteTab::tableModified, this, [=] (bool val) {m_cpal->setModified (val);});
	m_palContainer->addTab (ptab, ptab->label ());
    }

    saveButton = new QPushButton (QWidget::tr ("&Compile table"));
    removeButton = new QPushButton (QWidget::tr ("&Remove record"));
    addButton = new QPushButton (QWidget::tr ("&Add record"));
    closeButton = new QPushButton (QWidget::tr ("C&lose"));

    connect (saveButton, &QPushButton::clicked, this, &CpalEdit::save);
    connect (closeButton, &QPushButton::clicked, this, &CpalEdit::close);
    connect (addButton, &QPushButton::clicked, this, &CpalEdit::addNameRecord);
    connect (removeButton, &QPushButton::clicked, this, &CpalEdit::removeSelectedNameRecord);

    auto buttLayout = new QHBoxLayout ();
    buttLayout->addWidget (saveButton);
    buttLayout->addWidget (addButton);
    buttLayout->addWidget (removeButton);
    buttLayout->addWidget (closeButton);
    layout->addLayout (buttLayout);

    fillControls ();

    window->setLayout (layout);
    setCentralWidget (window);
}

CpalEdit::~CpalEdit () {
}

QString CpalEdit::entryLabel (int idx) {
    uint16_t name_idx = m_cpal->m_paletteLabelIndices[idx];
    QString name = m_nameProxy->bestName (name_idx, tr ("Palette entry"));
    return QString ("%1: %2").arg (idx).arg (name);
}

void CpalEdit::fillControls () {
    // Block signals here, as setting table version otherwise would cause
    // setTableVersion () to be executed. However, this is useless right now,
    // because a) setTableVersion () requires palette tabs to be already
    // available, which is not the case here, and b) it doesn't get triggered
    // anyway, if the table version is 0, which is equal to the default
    // value of the spin box.
    m_cpalVersionBox->blockSignals (true);
    m_cpalVersionBox->setMaximum (1);
    m_cpalVersionBox->setValue (m_cpal->version ());
    m_cpalVersionBox->blockSignals (false);

    m_numPalettesBox->setMaximum (0xFFFF);
    m_numPalettesBox->setValue (m_cpal->numPalettes ());

    m_numEntriesBox->setMaximum (0xFFFF);
    m_numEntriesBox->setValue (m_cpal->numPaletteEntries ());

    m_entryIdList->setSelectionBehavior (QAbstractItemView::SelectRows);
    m_entryIdList->setSelectionMode (QAbstractItemView::SingleSelection);
    m_entry_id_model = std::unique_ptr<ListEntryIdModel>
	(new ListEntryIdModel (m_cpal->m_paletteLabelIndices, this));
    connect (m_entry_id_model.get (), &QAbstractItemModel::dataChanged,	this, &CpalEdit::updateEntryList);
    m_entryIdList->setModel (m_entry_id_model.get ());
    m_entryIdList->horizontalHeader ()->setStretchLastSection (true);

    QAbstractItemDelegate *dlg = new SpinBoxDelegate (0x100, 0xFFFF);
    m_entryIdList->setItemDelegateForColumn (0, dlg);

    auto *proxyptr = dynamic_cast<NameProxy *> (m_nameProxy.get ());
    m_nameModel = std::unique_ptr<NameRecordModel> (new NameRecordModel (proxyptr));
    NameRecordModel *modptr = dynamic_cast<NameRecordModel *> (m_nameModel.get ());
    m_entryNameView->setModel (modptr);
    NameEdit::setEditWidth (m_entryNameView, 6);
    connect (m_entryNameView->selectionModel (), &QItemSelectionModel::selectionChanged,
	this, &CpalEdit::checkNameSelection);
    checkNameSelection (m_entryNameView->selectionModel ()->selection (), QItemSelection ());

    connect (m_tab, &QTabWidget::currentChanged, this, &CpalEdit::onTabChange);
    connect (m_palContainer, &QTabWidget::currentChanged, this, &CpalEdit::onPaletteChange);

    setTableVersion (m_cpal->version ());
}

bool CpalEdit::checkUpdate (bool) {
    return true;
}

bool CpalEdit::isModified () {
    return m_cpal->modified ();
}

bool CpalEdit::isValid () {
    return m_valid;
}

std::shared_ptr<FontTable> CpalEdit::table () {
    return m_cpal;
}

void CpalEdit::closeEvent (QCloseEvent *event) {
    // If we are going to delete the font, ignore changes in table edits
    if (!isModified () || checkUpdate (true)) {
        m_cpal->clearEditor ();
    } else {
        event->ignore ();
    }
}

QSize CpalEdit::minimumSize () const {
    QSize size = m_tab->size ();

    size += QSize (2, 2);
    return size;
}

QSize CpalEdit::sizeHint () const {
    return minimumSize ();
}

void CpalEdit::save () {
    if (m_cpalVersionBox->value () > 0 && m_name->editor ()->isModified ()) {
        int choice = FontShepherd::postYesNoQuestion (
	    tr ("Compile font tables"),
	    tr (
		"You have unsaved changes in the 'name' table. "
		"If you compile the 'cpal' table now, 'name' will also be overwritten. "
		"Do you really want to overwrite it?"),
	    this);
        if (choice == QMessageBox::No)
	    return;
    }

    if (m_cpalVersionBox->value () > 0) {
	for (int i=0; i<m_palContainer->count (); i++) {
	    QWidget *w = m_palContainer->widget (i);
	    PaletteTab *ptab = qobject_cast<PaletteTab *> (w);
	    ptab->flush ();
	}
	m_nameProxy->flush ();
	m_name->packData ();
	emit (update (m_name));
    }

    m_cpal->packData ();
    emit (update (m_cpal));
}

void CpalEdit::setTableVersion (int version) {
    if (version != m_cpal->version ()) {
	m_cpal->setModified (true);
	m_cpal->m_version = version;
    }
    m_entryIdList->setEnabled (version > 0);
    m_entryNameView->setEnabled (version > 0);
    for (int i=0; i<m_palContainer->count (); i++) {
	auto pal_w = qobject_cast<PaletteTab *> (m_palContainer->widget (i));
	pal_w->setTableVersion (version);
    }
    addButton->setEnabled (version > 0);
    if (version > 0) {
	switch (m_tab->currentIndex ()) {
	  case 0:
	    {
		checkNameSelection (m_entryNameView->selectionModel ()->selection (), QItemSelection ());
		break;
	    }
	  case 1:
	    {
		QWidget *w = m_palContainer->currentWidget ();
		PaletteTab *ptab = qobject_cast<PaletteTab *> (w);
		removeButton->setEnabled (ptab->checkNameSelection ());
	    }
	}
    } else {
	removeButton->setEnabled (false);
    }
}

void CpalEdit::setPalettesNumber (int value) {
    if (value < m_cpal->numPalettes ()) {
        int choice = FontShepherd::postYesNoQuestion (
	    tr ("Decrease number of palettes"),
	    tr (
		"Are you sure you want to delete %1 "
		"color %2 from this font? "
		"This operation cannot be undone!")
		.arg (m_cpal->numPalettes () - value)
		.arg (value - m_cpal->numPalettes () == 1 ? tr ("palette") : tr ("palettes")),
	    this);
        if (choice == QMessageBox::No) {
	    m_numPalettesBox->blockSignals (true);
	    m_numPalettesBox->setValue (m_cpal->numPalettes ());
	    m_numPalettesBox->blockSignals (false);
	    return;
	} else {
	    for (uint16_t i=m_cpal->numPalettes ()-1; i>= value; i--)
		m_palContainer->removeTab (i);
	    m_cpal->setNumPalettes (value);
	}
	m_cpal->setModified (true);
    } else if (value > m_cpal->numPalettes ()) {
        int choice = FontShepherd::postYesNoQuestion (
	    tr ("Increase number of palettes"),
	    tr (
		"Would you like to add %1 new %2 to this font, "
		"filling them with default values?")
		.arg (value - m_cpal->numPalettes ())
		.arg (value - m_cpal->numPalettes () == 1 ? tr ("palette") : tr ("palettes")),
	    this);
        if (choice == QMessageBox::No) {
	    m_numPalettesBox->blockSignals (true);
	    m_numPalettesBox->setValue (m_cpal->numPalettes ());
	    m_numPalettesBox->blockSignals (false);
	    return;
	} else {
	    uint16_t old_cnt = m_cpal->m_paletteList.size ();
	    m_cpal->setNumPalettes (value);
	    for (uint16_t i = old_cnt; i<value; i++) {
		auto ptab = new PaletteTab (m_cpal->palette (i), m_name.get (), i, m_entryNames);
		ptab->setTableVersion (m_cpal->version ());
		connect (ptab, &PaletteTab::needsLabelUpdate, this, &CpalEdit::updatePaletteLabel);
		m_palContainer->addTab (ptab, ptab->label ());
	    }
	}
	m_cpal->setModified (true);
    }
}

void CpalEdit::setEntriesNumber (int value) {
    if (value == m_cpal->numPaletteEntries ())
	return;
    else if (value < m_cpal->numPaletteEntries ()) {
        int choice = FontShepherd::postYesNoQuestion (
	    tr ("Decrease number of palette entries"),
	    tr (
		"Would you like to decrease the number of palette entries? "
		"This will remove %1 %2 from each palette.")
		.arg ((int) m_cpal->numPaletteEntries () - value)
		.arg ((int) m_cpal->numPaletteEntries () - value > 1 ? "colors" : "color"),
	    this);
        if (choice == QMessageBox::No) {
	    m_numEntriesBox->blockSignals (true);
	    m_numEntriesBox->setValue (m_cpal->numPaletteEntries ());
	    m_numEntriesBox->blockSignals (false);
	    return;
	}
	m_entry_id_model->truncate (value);
	m_entryNames.resize (value);
    } else {
	m_entry_id_model->expand (value);
	m_entryNames.reserve (value);
	for (uint16_t i = m_cpal->m_numPaletteEntries; i<value; i++)
	    m_entryNames.push_back (entryLabel (i));
    }
    updateNameModel ();
    for (uint16_t i = 0; i<m_cpal->numPalettes (); i++) {
        auto pal_w = qobject_cast<PaletteTab *> (m_palContainer->widget (i));
        pal_w->setColorCount (value);
    }
    m_cpal->m_numPaletteEntries = value;
    m_cpal->setModified (true);
}

void CpalEdit::updatePaletteLabel (int pal_idx) {
    Q_ASSERT (pal_idx < m_palContainer->count ());
    auto ptab = qobject_cast<PaletteTab *> (m_palContainer->widget (pal_idx));
    m_palContainer->setTabText (pal_idx, ptab->label ());
}

void CpalEdit::updateNameModel () {
    std::vector<FontShepherd::numbered_string> name_lst;
    name_lst.reserve (m_cpal->numPaletteEntries ());
    for (size_t i=0; i<m_cpal->numPaletteEntries (); i++) {
	if (m_cpal->m_paletteLabelIndices[i] != 0xFFFF) {
	    name_lst.push_back (FontShepherd::numbered_string
		{m_cpal->m_paletteLabelIndices[i], tr ("Palette entry name").toStdString ()});
	}
    }

    m_nameModel->beginResetModel ();
    m_nameProxy->update (name_lst);
    m_nameModel->endResetModel ();
    checkNameSelection (m_entryNameView->selectionModel ()->selection (), QItemSelection ());
}

void CpalEdit::updateEntryList
    (const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles) {
    Q_UNUSED (bottomRight);
    Q_UNUSED (roles);
    Q_ASSERT (topLeft.row () < m_entryNames.size ());
    updateNameModel ();
    QString new_val = entryLabel (topLeft.row ());
    m_entryNames[topLeft.row ()] = new_val;
}

void CpalEdit::onTabChange (int index) {
    QWidget *w = m_palContainer->currentWidget ();
    PaletteTab *ptab = qobject_cast<PaletteTab *> (w);

    switch (index) {
      case 0:
	disconnect (addButton, &QPushButton::clicked, ptab, &PaletteTab::addNameRecord);
	disconnect (removeButton, &QPushButton::clicked, ptab, &PaletteTab::removeSelectedNameRecord);
	disconnect (ptab, &PaletteTab::fwdNameSelectionChanged,
	    this, &CpalEdit::checkNameSelection);
	connect (addButton, &QPushButton::clicked, this, &CpalEdit::addNameRecord);
	connect (removeButton, &QPushButton::clicked, this, &CpalEdit::removeSelectedNameRecord);
	connect (m_entryNameView->selectionModel (), &QItemSelectionModel::selectionChanged,
	    this, &CpalEdit::checkNameSelection);

	removeButton->setEnabled (m_entryNameView->selectionModel ()->hasSelection ());
	break;
      case 1:
	connect (addButton, &QPushButton::clicked, ptab, &PaletteTab::addNameRecord);
	connect (removeButton, &QPushButton::clicked, ptab, &PaletteTab::removeSelectedNameRecord);
	connect (ptab, &PaletteTab::fwdNameSelectionChanged, this, &CpalEdit::checkNameSelection);
	disconnect (addButton, &QPushButton::clicked, this, &CpalEdit::addNameRecord);
	disconnect (removeButton, &QPushButton::clicked, this, &CpalEdit::removeSelectedNameRecord);
	disconnect (m_entryNameView->selectionModel (), &QItemSelectionModel::selectionChanged,
	    this, &CpalEdit::checkNameSelection);

	removeButton->setEnabled (ptab->checkNameSelection ());
    }
}

void CpalEdit::onPaletteChange (int index) {
    QWidget *w;
    PaletteTab *ptab;

    for (int i=0; i<m_palContainer->count (); i++) {
	w = m_palContainer->widget (i);
	ptab = qobject_cast<PaletteTab *> (w);
	disconnect (addButton, &QPushButton::clicked, ptab, &PaletteTab::addNameRecord);
	disconnect (removeButton, &QPushButton::clicked, ptab, &PaletteTab::removeSelectedNameRecord);
	disconnect (ptab, &PaletteTab::fwdNameSelectionChanged, this, &CpalEdit::checkNameSelection);
    }
    w = m_palContainer->widget (index);
    ptab = qobject_cast<PaletteTab *> (w);
    connect (addButton, &QPushButton::clicked, ptab, &PaletteTab::addNameRecord);
    connect (removeButton, &QPushButton::clicked, ptab, &PaletteTab::removeSelectedNameRecord);
    connect (ptab, &PaletteTab::fwdNameSelectionChanged, this, &CpalEdit::checkNameSelection);
    removeButton->setEnabled (ptab->checkNameSelection ());
}

void CpalEdit::checkNameSelection (const QItemSelection &newSelection, const QItemSelection &oldSelection) {
    Q_UNUSED (oldSelection);
    removeButton->setEnabled (!newSelection.empty ());
}

void CpalEdit::addNameRecord () {
    name_record rec;

    if (m_nameProxy->nameList ().size () == 0) {
        FontShepherd::postError (
	    tr ("Can't add palette entry name"),
	    tr ("Can't add palette entry name, as no name IDs are currently defined."),
            this);
	return;
    }

    AddNameDialog dlg (m_nameProxy.get (), this);
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

    QAbstractItemModel *absmod = m_entryNameView->model ();
    NameRecordModel *nmod = qobject_cast<NameRecordModel *> (absmod);
    QList<name_record> arg;
    arg << rec;
    nmod->insertRows (arg, row);
}

void CpalEdit::removeSelectedNameRecord () {
    QItemSelectionModel *sel_mod = m_entryNameView->selectionModel ();
    if (sel_mod->hasSelection ()) {
	int row = sel_mod->selectedRows ().first ().row ();
	int count = sel_mod->selectedRows ().size ();
	QAbstractItemModel *absmod = m_entryNameView->model ();
	absmod->removeRows (row, count, QModelIndex ());
    }
}

PaletteTab::PaletteTab (cpal_palette *pal, NameTable *name,
    int idx, QVector<QString> &en_list, QTabWidget *parent) :
    QWidget (parent), m_pal (pal), m_idx (idx), m_entryNames (en_list) {

    m_nameProxy = std::unique_ptr<NameProxy> (new NameProxy (name));
    if (m_pal->label_idx != 0xFFFF)
	m_nameProxy->update (std::vector<FontShepherd::numbered_string>
	    { { m_pal->label_idx, tr ("Palette name").toStdString () } });
    else
	m_nameProxy->update (std::vector<FontShepherd::numbered_string> ());

    QGridLayout *glay = new QGridLayout ();
    setLayout (glay);

    glay->addWidget (new QLabel (tr ("Palette name ID:")), 0, 0);
    m_nameIdBox = new QSpinBox ();
    glay->addWidget (m_nameIdBox, 0, 1);

    glay->addWidget (new QLabel (tr ("Palette properties:")), 1, 0, 1, 2);
    m_flagList = new QListWidget ();
    glay->addWidget (m_flagList, 2, 0, 1, 2);

    glay->addWidget (new QLabel (tr ("Palette colors:")), 3, 0, 1, 2);
    m_colorList = new QTableView ();
    glay->addWidget (m_colorList, 4, 0, 1, 2);
    connect (m_colorList, &QTableView::doubleClicked, this, &PaletteTab::startColorEditor);

    glay->addWidget (new QLabel (tr ("Palette names:")), 5, 0, 1, 2);
    m_nameView = new QTableView ();
    glay->addWidget (m_nameView, 6, 0, 1, 2);

    fillControls ();
}

PaletteTab::~PaletteTab () {
}

QString PaletteTab::label () {
    QString name = m_nameProxy->bestName (m_pal->label_idx, tr ("Unnamed palette"));
    return QString ("%1: %2").arg (m_idx).arg (name);
}

void PaletteTab::fillControls () {
    m_nameIdBox->setMinimum (0x100);
    m_nameIdBox->setMaximum (0xFFFF);
    m_nameIdBox->setValue (m_pal->label_idx);
    connect (m_nameIdBox, static_cast<void (QSpinBox::*)(int)> (&QSpinBox::valueChanged),
	this, &PaletteTab::onNameIdChange);

    auto *item = new QListWidgetItem (tr ("Usable with light background"));
    auto iflags = item->flags ();
    iflags |= Qt::ItemIsUserCheckable;
    item->setFlags (iflags);
    item->setCheckState (m_pal->flags[0] ? Qt::Checked : Qt::Unchecked);
    m_flagList->addItem (item);

    item = new QListWidgetItem (tr ("Usable with dark background"));
    item->setFlags (iflags);
    item->setCheckState (m_pal->flags[1] ? Qt::Checked : Qt::Unchecked);
    m_flagList->addItem (item);

    m_colorList->setSelectionBehavior (QAbstractItemView::SelectRows);
    m_colorList->setSelectionMode (QAbstractItemView::SingleSelection);
    m_color_model = std::unique_ptr<ColorModel> (new ColorModel (m_pal, m_entryNames, this));
    m_colorList->setModel (m_color_model.get ());
    m_colorList->horizontalHeader ()->setStretchLastSection (true);

    auto *proxyptr = dynamic_cast<NameProxy *> (m_nameProxy.get ());
    m_nameModel = std::unique_ptr<NameRecordModel> (new NameRecordModel (proxyptr));
    NameRecordModel *modptr = dynamic_cast<NameRecordModel *> (m_nameModel.get ());
    m_nameView->setModel (modptr);
    NameEdit::setEditWidth (m_nameView, 6);

    connect (m_nameView->selectionModel (), &QItemSelectionModel::selectionChanged,
	this, &PaletteTab::fwdNameSelectionChanged);
}

void PaletteTab::setTableVersion (int version) {
    m_nameIdBox->setEnabled (version > 0);
    m_flagList->setEnabled (version > 0);
    m_nameView->setEnabled (version > 0);
}

void PaletteTab::setColorCount (int count) {
    int num_rows = m_color_model->rowCount (QModelIndex ());
    if (count > num_rows) {
	m_color_model->expand (count);
	emit tableModified (true);
    } else if (count < num_rows) {
	m_color_model->truncate (count);
	emit tableModified (true);
    }
}

void PaletteTab::startColorEditor (const QModelIndex &index) {
    if (index.column () == 0) {
	QColor cell_color = qvariant_cast<QColor> (m_color_model->data (index, Qt::EditRole));
	QColorDialog cdlg (cell_color);
	cdlg.setOptions (QColorDialog::ShowAlphaChannel);
	if (cdlg.exec () == QDialog::Accepted) {
	    cell_color = cdlg.selectedColor ();
	    m_color_model->setData (index, cell_color, Qt::EditRole);
	    emit tableModified (true);
	}
    }
}

void PaletteTab::onNameIdChange (int val) {
    m_pal->label_idx = val;
    m_nameModel->beginResetModel ();
    if (m_pal->label_idx != 0xFFFF)
	m_nameProxy->update (std::vector<FontShepherd::numbered_string>
	    { { m_pal->label_idx, tr ("Palette name").toStdString () } });
    else
	m_nameProxy->update (std::vector<FontShepherd::numbered_string> ());
    m_nameModel->endResetModel ();
    emit needsLabelUpdate (m_idx);
    emit fwdNameSelectionChanged (m_nameView->selectionModel ()->selection (), QItemSelection ());
    emit tableModified (true);
}

void PaletteTab::addNameRecord () {
    name_record rec;

    if (m_nameProxy->nameList ().size () == 0) {
        FontShepherd::postError (
	    tr ("Can't add palette name"),
	    tr ("There is no name ID set for this palette. "
		"Please set it before adding a name."),
            this);
	return;
    }

    AddNameDialog dlg (m_nameProxy.get (), this);
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

    QAbstractItemModel *absmod = m_nameView->model ();
    NameRecordModel *nmod = qobject_cast<NameRecordModel *> (absmod);
    QList<name_record> arg;
    arg << rec;
    nmod->insertRows (arg, row);
}

void PaletteTab::removeSelectedNameRecord () {
    QItemSelectionModel *sel_mod = m_nameView->selectionModel ();
    if (sel_mod->hasSelection ()) {
	int row = sel_mod->selectedRows ().first ().row ();
	int count = sel_mod->selectedRows ().size ();
	QAbstractItemModel *absmod = m_nameView->model ();
	absmod->removeRows (row, count, QModelIndex ());
    }
}

bool PaletteTab::checkNameSelection () {
    return m_nameView->selectionModel ()->hasSelection ();
}

void PaletteTab::flush () {
    for (int i=0; i<m_flagList->count (); i++) {
	auto item = m_flagList->item (i);
	m_pal->flags[i] = (item->checkState () == Qt::Checked);
    }
    m_nameProxy->flush ();
}

ColorModel::ColorModel (cpal_palette *pal, QVector<QString> &en_list, QWidget *parent) :
    QAbstractTableModel (parent), m_pal (pal), m_entryNames (en_list), m_parent (parent) {
}

ColorModel::~ColorModel () {
}

int ColorModel::rowCount (const QModelIndex &parent) const {
    Q_UNUSED (parent);
    return m_pal->color_records.size ();
}

int ColorModel::columnCount (const QModelIndex &parent) const {
    Q_UNUSED (parent);
    return 2;
}

QVariant ColorModel::data (const QModelIndex &index, int role) const {
    rgba_color &color = m_pal->color_records[index.row ()];
    QColor bg_color = QColor (color.red, color.green, color.blue, color.alpha);

    switch (role) {
      case Qt::EditRole:
	switch (index.column ()) {
	  case 0:
	    return bg_color;
	}
	break;
      case Qt::BackgroundRole:
	switch (index.column ()) {
	  case 0:
	    return QBrush (bg_color);
	}
	break;
      case Qt::DisplayRole:
	switch (index.column ()) {
	  case 1:
	    return m_entryNames[index.row ()];
	}
	break;
    }
    return QVariant ();
}

bool ColorModel::setData (const QModelIndex &index, const QVariant &value, int role) {
    if (index.isValid() && index.column () == 0) {
	rgba_color &color = m_pal->color_records[index.row ()];
	if (role == Qt::EditRole) {
	    QColor new_color = qvariant_cast<QColor> (value);
	    color.red = new_color.red ();
	    color.green = new_color.green ();
	    color.blue = new_color.blue ();
	    color.alpha = new_color.alpha ();
	    emit dataChanged (index, index);
	    return true;
	}
    }
    return false;
}

Qt::ItemFlags ColorModel::flags (const QModelIndex &index) const {
    Q_UNUSED (index);
    Qt::ItemFlags ret = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    return ret;
}

QVariant ColorModel::headerData (int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
	switch (section) {
	  case 0:
	    return QWidget::tr ("Color");
	  case 1:
	    return QWidget::tr ("Palette entry name");
	}
    } else if (orientation == Qt::Vertical && role == Qt::DisplayRole) {
	return QString::number (section);
    }
    return QVariant ();
}

void ColorModel::truncate (int new_count) {
    int count = m_pal->color_records.size ();
    beginRemoveRows (QModelIndex (), new_count, count-1);
    m_pal->color_records.resize (new_count);
    endRemoveRows ();
}

void ColorModel::expand (int new_count) {
    int count = m_pal->color_records.size ();
    beginInsertRows (QModelIndex (), count, new_count-1);
    for (int i=count; i<new_count; i++)
	m_pal->color_records.push_back (rgba_color ());
    endInsertRows ();
}

ListEntryIdModel::ListEntryIdModel (std::vector<uint16_t> &idxList, QWidget *parent) :
    QAbstractTableModel (parent), m_idxList (idxList), m_parent (parent) {
}

ListEntryIdModel::~ListEntryIdModel () {
}

int ListEntryIdModel::rowCount (const QModelIndex &parent) const {
    Q_UNUSED (parent);
    return m_idxList.size ();
}

int ListEntryIdModel::columnCount (const QModelIndex &parent) const {
    Q_UNUSED (parent);
    return 1;
}

QVariant ListEntryIdModel::data (const QModelIndex &index, int role) const {
    uint16_t entry_id = m_idxList[index.row ()];

    switch (role) {
      case Qt::DisplayRole:
	switch (index.column ()) {
	  case 0:
	    return entry_id == 0xFFFF ?
		QString ("No name ID: 0xFFFF") : QString::number (entry_id);
	}
	break;
      case Qt::EditRole:
	switch (index.column ()) {
	  case 0:
	    return entry_id;
	}
	break;
    }
    return QVariant ();
}

bool ListEntryIdModel::setData (const QModelIndex &index, const QVariant &value, int role) {
    if (index.isValid() && index.column () == 0) {
	if (role == Qt::EditRole) {
	    uint16_t entry_id = value.toUInt ();
	    if (entry_id > 0xFF) {
		m_idxList[index.row ()] = entry_id;
		emit dataChanged (index, index);
		return true;
	    }
	}
    }
    return false;
}

Qt::ItemFlags ListEntryIdModel::flags (const QModelIndex &index) const {
    Qt::ItemFlags ret = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column () == 0) {
	ret |= Qt::ItemIsEditable;
    }
    return ret;
}

QVariant ListEntryIdModel::headerData (int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
	switch (section) {
	  case 0:
	    return QWidget::tr ("Entry Name ID");
	}
    } else if (orientation == Qt::Vertical && role == Qt::DisplayRole) {
	return QString::number (section);
    }
    return QVariant ();
}

void ListEntryIdModel::truncate (int new_count) {
    int count = m_idxList.size ();
    beginRemoveRows (QModelIndex (), new_count, count-1);
    m_idxList.resize (new_count);
    endRemoveRows ();
}

void ListEntryIdModel::expand (int new_count) {
    int count = m_idxList.size ();
    beginInsertRows (QModelIndex (), count, new_count-1);
    for (int i=count; i<new_count; i++)
	m_idxList.push_back (0xFFFF);
    endInsertRows ();
}

SpinBoxDelegate::SpinBoxDelegate (int min, int max, QObject *parent) :
    QStyledItemDelegate (parent), m_min (min), m_max (max) {
}

QWidget* SpinBoxDelegate::createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (option);
    Q_UNUSED (index);
    QSpinBox *box = new QSpinBox (parent);

    box->setFrame (false);
    box->setMinimum (m_min);
    box->setMaximum (m_max);

    return box;
}

void SpinBoxDelegate::setEditorData (QWidget *editor, const QModelIndex &index) const {
    uint32_t value = index.model ()->data (index, Qt::EditRole).toUInt ();
    QSpinBox *box = qobject_cast<QSpinBox*> (editor);
    box->setValue (value);
}

void SpinBoxDelegate::setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
    QSpinBox* box = qobject_cast<QSpinBox*> (editor);
    uint32_t value = box->value ();
    model->setData (index, value, Qt::EditRole);
}

void SpinBoxDelegate::updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (index);
    editor->setGeometry (option.rect);
}
