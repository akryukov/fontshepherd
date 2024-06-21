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
#include "editors/gaspedit.h" // also includes tables.h
#include "tables/gasp.h"
#include "editors/commondelegates.h"

#include "fs_notify.h"

GaspEdit::GaspEdit (std::shared_ptr<FontTable> tptr, sFont* font, QWidget *parent) :
    TableEdit (parent, Qt::Window), m_font (font) {
    m_gasp = std::dynamic_pointer_cast<GaspTable> (tptr);

    setAttribute (Qt::WA_DeleteOnClose);
    setWindowTitle (QString ("%1 - %2").arg
	(QString::fromStdString (m_gasp->stringName ())).arg (m_font->fontname));

    QWidget *window = new QWidget (this);
    QGridLayout *layout = new QGridLayout ();

    layout->addWidget (new QLabel (tr ("Table version:")), 0, 0);
    m_versionBox = new QComboBox ();
    m_versionBox->addItem ("0: Gridfitting and Antialiasing", 0);
    m_versionBox->addItem ("1: Gridfitting and Symmetric Smoothing for ClearType", 1);
    layout->addWidget (m_versionBox, 0, 1);
    m_versionBox->setCurrentIndex
	(m_versionBox->findData (m_gasp->version (), Qt::UserRole));
    connect (m_versionBox, static_cast<void (QComboBox::*)(int)> (&QComboBox::currentIndexChanged),
	this, &GaspEdit::setTableVersion);

    layout->addWidget (new QLabel (tr ("PPEM Ranges:")), 1, 0);
    m_rangeTab = new QTableWidget ();
    layout->addWidget (m_rangeTab, 2, 0, 1, 2);

    m_okButton = new QPushButton (QWidget::tr ("OK"));
    connect (m_okButton, &QPushButton::clicked, this, &GaspEdit::save);
    m_cancelButton = new QPushButton (QWidget::tr ("&Cancel"));
    connect (m_cancelButton, &QPushButton::clicked, this, &GaspEdit::close);
    m_removeButton = new QPushButton (QWidget::tr ("&Remove entry"));
    connect (m_removeButton, &QPushButton::clicked, this, &GaspEdit::removeEntry);
    m_addButton = new QPushButton (QWidget::tr ("&Add entry"));
    connect (m_addButton, &QPushButton::clicked, this, &GaspEdit::addEntry);

    QHBoxLayout *buttLayout;
    buttLayout = new QHBoxLayout ();
    buttLayout->addWidget (m_okButton);
    buttLayout->addWidget (m_addButton);
    buttLayout->addWidget (m_removeButton);
    buttLayout->addWidget (m_cancelButton);
    layout->addLayout (buttLayout, 3, 0, 1, 2);

    window->setLayout (layout);
    setCentralWidget (window);
    fillControls ();
    setTableVersion (m_versionBox->currentIndex ());

    m_valid = true;
}

bool GaspEdit::checkUpdate (bool) {
    return true;
}

bool GaspEdit::isModified () {
    return m_gasp->modified ();
}

bool GaspEdit::isValid () {
    return m_valid;
}

std::shared_ptr<FontTable> GaspEdit::table () {
    return m_gasp;
}

void GaspEdit::setTableVersion (int idx) {
    uint16_t newver = m_versionBox->itemData (idx, Qt::UserRole).toUInt ();
    m_rangeTab->setColumnHidden (3, newver == 0);
    m_rangeTab->setColumnHidden (4, newver == 0);
}

void GaspEdit::removeEntry () {
    QItemSelectionModel *sel_mod = m_rangeTab->selectionModel ();
    QModelIndexList row_lst = sel_mod->selectedRows ();
    if (row_lst.size ()) {
        QModelIndex rowidx = row_lst.first ();
        m_rangeTab->removeRow (rowidx.row ());
    }
    m_removeButton->setEnabled (m_rangeTab->rowCount () > 0);
}

void GaspEdit::addEntry () {
    uint16_t ver = m_versionBox->itemData (m_versionBox->currentIndex (), Qt::UserRole).toUInt ();
    std::set<uint16_t> ppem_set;
    for (int i=0; i<m_rangeTab->rowCount (); i++) {
	auto item = m_rangeTab->item (i, 0);
	uint16_t ppem = item->data (Qt::UserRole).toUInt ();
	ppem_set.insert (ppem);
    }

    AddPpemDialog dlg (ver, ppem_set, this);
    switch (dlg.exec ()) {
      case QDialog::Accepted:
	break;
      case QDialog::Rejected:
	return;
    }
    int idx = 0;
    uint16_t newppem = dlg.ppem ();
    for (uint16_t val : ppem_set) {
	if (val > newppem) break;
	idx++;
    }
    m_rangeTab->insertRow (idx);
    m_rangeTab->selectRow (idx);

    auto ppem_item = new QTableWidgetItem (QString::number (newppem));
    ppem_item->setData (Qt::UserRole, newppem);
    m_rangeTab->setItem (idx, 0, ppem_item);

    bool gf_flag = dlg.gridFit ();
    addBooleanCellItem (gf_flag, 1, idx);

    bool gray_flag = dlg.doGray ();
    addBooleanCellItem (gray_flag, 2, idx);

    bool sym_gf_flag = dlg.symGridFit ();
    addBooleanCellItem (sym_gf_flag, 3, idx);

    bool sym_sm_flag = dlg.symSmooth ();
    addBooleanCellItem (sym_sm_flag, 4, idx);
}

void GaspEdit::closeEvent (QCloseEvent *event) {
    // If we are going to delete the font, ignore changes in table edits
    if (!isModified () || checkUpdate (true))
        m_gasp->clearEditor ();
    else
        event->ignore ();
}

void GaspEdit::save () {
    gasp_data &gd = m_gasp->contents;
    gd.version = m_versionBox->itemData (m_versionBox->currentIndex (), Qt::UserRole).toUInt ();
    uint16_t nranges = m_rangeTab->rowCount ();
    gd.ranges.clear ();
    gd.ranges.resize (nranges);

    for (size_t i=0; i<nranges; i++) {
	auto item = m_rangeTab->item (i, 0);
	gd.ranges[i].rangeMaxPPEM = item->data (Qt::UserRole).toUInt ();
	gd.ranges[i].rangeGaspBehavior = 0;

	bool gf_flag = m_rangeTab->item (i, 1)->data (Qt::UserRole).toBool ();
	bool gray_flag = m_rangeTab->item (i, 2)->data (Qt::UserRole).toBool ();
	if (gf_flag)
	    gd.ranges[i].rangeGaspBehavior |= static_cast<uint16_t> (GaspFlags::GRIDFIT);
	if (gray_flag)
	    gd.ranges[i].rangeGaspBehavior |= static_cast<uint16_t> (GaspFlags::DOGRAY);

	if (gd.version > 0) {
	    bool sym_gf_flag = m_rangeTab->item (i, 3)->data (Qt::UserRole).toBool ();
	    bool sym_sm_flag = m_rangeTab->item (i, 4)->data (Qt::UserRole).toBool ();
	    if (sym_gf_flag)
		gd.ranges[i].rangeGaspBehavior |= static_cast<uint16_t> (GaspFlags::SYMMETRIC_GRIDFIT);
	    if (sym_sm_flag)
		gd.ranges[i].rangeGaspBehavior |= static_cast<uint16_t> (GaspFlags::SYMMETRIC_SMOOTHING);
	}
    }

    m_gasp->packData ();
    emit (update (m_gasp));
    close ();
}

void GaspEdit::fillControls () {
    m_rangeTab->setColumnCount (5);
    QStringList labels = {"Max PPEM", "GridF", "AntiAlias",
	"Sym GridF", "Sym Smooth"};

    QFontMetrics fm = m_rangeTab->fontMetrics ();
    int fullw = 0;
    m_rangeTab->setHorizontalHeaderLabels (labels);
    for (int i=0; i<5; i++) {
	int w = fm.boundingRect ("~" + labels[i] + "~").width ();
	m_rangeTab->setColumnWidth (i, w);
	fullw += w;
    }

    m_rangeTab->horizontalHeader ()->setStretchLastSection (true);
    m_rangeTab->setSelectionBehavior (QAbstractItemView::SelectRows);
    m_rangeTab->setSelectionMode (QAbstractItemView::SingleSelection);

    size_t num = m_gasp->numRanges ();
    m_rangeTab->setRowCount (num);
    m_removeButton->setEnabled (num > 0);

    m_rangeTab->resize (fullw, m_rangeTab->rowHeight (0) * 12);
    m_rangeTab->selectRow (0);
    m_rangeTab->setItemDelegateForColumn (0, new SortedSpinBoxDelegate (this));
    for (int i=1; i<5; i++)
	m_rangeTab->setItemDelegateForColumn (i, new TrueFalseDelegate (this));

    for (size_t i=0; i<num; i++) {
	uint16_t ppem = m_gasp->maxPPEM (i);
	uint16_t behavior = m_gasp->gaspBehavior (i);

	auto ppem_item = new QTableWidgetItem (QString::number (ppem));
	ppem_item->setData (Qt::UserRole, ppem);
	m_rangeTab->setItem (i, 0, ppem_item);

	bool gf_flag = behavior & static_cast<uint16_t> (GaspFlags::GRIDFIT);
	addBooleanCellItem (gf_flag, 1, i);

	bool gray_flag = behavior & static_cast<uint16_t> (GaspFlags::DOGRAY);
	addBooleanCellItem (gray_flag, 2, i);

	bool sym_gf_flag = behavior & static_cast<uint16_t> (GaspFlags::SYMMETRIC_GRIDFIT);
	addBooleanCellItem (sym_gf_flag, 3, i);

	bool sym_sm_flag = behavior & static_cast<uint16_t> (GaspFlags::SYMMETRIC_SMOOTHING);
	addBooleanCellItem (sym_sm_flag, 4, i);
    }
}

void GaspEdit::addBooleanCellItem (bool val, int x, int y) {
    auto item = new QTableWidgetItem (val ? "true" : "false");
    item->setData (Qt::UserRole, val);
    m_rangeTab->setItem (y, x, item);
}

TrueFalseDelegate::TrueFalseDelegate (QObject *parent, QString false_str, QString true_str) :
    QStyledItemDelegate (parent), m_false (false_str), m_true (true_str) {
}

QWidget* TrueFalseDelegate::createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (option);
    Q_UNUSED (index);

    QComboBox *combo = new QComboBox (parent);
    combo->addItem (m_false, false);
    combo->addItem (m_true, true);

    return combo;
}

void TrueFalseDelegate::setEditorData (QWidget *editor, const QModelIndex &index) const {
    bool value = index.model ()->data (index, Qt::UserRole).toBool ();
    QComboBox *combo = qobject_cast<QComboBox*> (editor);
    combo->setCurrentText (value ? m_true : m_false);
}

void TrueFalseDelegate::setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
    QComboBox* combo = qobject_cast<QComboBox*> (editor);
    bool value = combo->currentData (Qt::UserRole).toBool ();
    // NB: set the UserRole after the DisplayRole
    model->setData (index, (value ? m_true : m_false), Qt::DisplayRole);
    model->setData (index, value, Qt::UserRole);
}

void TrueFalseDelegate::updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (index);
    editor->setGeometry (option.rect);
}

QString TrueFalseDelegate::byVal (bool val) const {
    return (val ? m_true : m_false);
}

SortedSpinBoxDelegate::SortedSpinBoxDelegate (QObject *parent) :
    SpinBoxDelegate (1, 0xFFFF, parent) {
}

void SortedSpinBoxDelegate::setEditorData (QWidget *editor, const QModelIndex &index) const {
    auto model = index.model ();
    uint32_t value = model->data (index, Qt::EditRole).toUInt ();
    int row = index.row ();
    int rowcnt = model->rowCount ();
    QSpinBox *box = qobject_cast<QSpinBox*> (editor);
    box->setValue (value);
    if (row > 0) {
	QModelIndex previdx = index.siblingAtRow (row-1);
	uint16_t prev = model->data (previdx, Qt::EditRole).toUInt ();
	box->setMinimum (prev+1);
    } else {
	box->setMinimum (1);
    }

    if (row < (rowcnt-1)) {
	QModelIndex nextidx = index.siblingAtRow (row+1);
	uint16_t next = model->data (nextidx, Qt::EditRole).toUInt ();
	box->setMaximum (next-1);
    } else {
	box->setMaximum (0xFFFF);
    }
}

AddPpemDialog::AddPpemDialog (uint16_t version, std::set<uint16_t> ppems, QWidget *parent) :
    QDialog (parent), m_version (version), m_ppemList (ppems) {

    setWindowTitle (tr ("Add PPEM"));

    QVBoxLayout *layout = new QVBoxLayout ();
    QGridLayout *glay = new QGridLayout ();
    layout->addLayout (glay);

    glay->addWidget (new QLabel ("Upper PPEM limit"), 0, 0);
    m_ppemBox = new QSpinBox ();
    m_ppemBox->setMinimum (1);
    m_ppemBox->setMaximum (65535);
    glay->addWidget (m_ppemBox, 0, 1);

    m_gridFitBox = new QCheckBox ();
    m_gridFitBox->setText (tr ("Use gridfitting"));
    glay->addWidget (m_gridFitBox, 1,0, 1,2);

    m_doGrayBox = new QCheckBox ();
    m_doGrayBox->setText (tr ("Use grayscale rendering"));
    glay->addWidget (m_doGrayBox, 2,0, 1,2);

    if (m_version > 0) {
	m_symGridFitBox = new QCheckBox ();
	m_symGridFitBox->setText (tr ("Use gridfitting with ClearType symmetric smoothing"));
	glay->addWidget (m_symGridFitBox, 3,0, 1,2);

	m_symSmoothBox = new QCheckBox ();
	m_symSmoothBox->setText (tr ("Use smoothing along multiple axes with ClearType®"));
	glay->addWidget (m_symSmoothBox, 4,0, 1,2);
    }

    QHBoxLayout *butt_layout = new QHBoxLayout ();
    QPushButton* okBtn = new QPushButton ("OK");
    connect (okBtn, &QPushButton::clicked, this, &AddPpemDialog::accept);
    butt_layout->addWidget( okBtn );

    QPushButton* cancelBtn = new QPushButton (tr ("Cancel"));
    connect (cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    butt_layout->addWidget( cancelBtn );
    layout->addLayout (butt_layout);

    setLayout (layout);
}

uint16_t AddPpemDialog::ppem () const {
    return m_ppemBox->value ();
}

bool AddPpemDialog::gridFit () const {
    return (m_gridFitBox->checkState () == Qt::Checked);
}

bool AddPpemDialog::doGray () const {
    return (m_doGrayBox->checkState () == Qt::Checked);
}

bool AddPpemDialog::symGridFit () const {
    return (m_version > 0 ? m_symGridFitBox->checkState () == Qt::Checked : false);
}

bool AddPpemDialog::symSmooth () const {
    return (m_version > 0 ? m_symSmoothBox->checkState () == Qt::Checked : false);
}

void AddPpemDialog::accept () {
    uint16_t ppem = m_ppemBox->value ();

    if (m_ppemList.count (ppem))
        FontShepherd::postError (
	    tr ("Can't add 'gasp' range record"),
	    tr ("There is already a range record with the PPEM specified."),
            this);
    else {
	QDialog::accept ();
    }
}
