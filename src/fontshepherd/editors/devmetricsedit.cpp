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
#include "editors/devmetricsedit.h" // also includes tables.h
#include "editors/commondelegates.h"
#include "tables/devmetrics.h"

#include "fs_notify.h"

HdmxEdit::HdmxEdit (std::shared_ptr<FontTable> tptr, sFont* font, QWidget *parent) :
    TableEdit (parent, Qt::Window), m_font (font) {
    m_hdmx = std::dynamic_pointer_cast<HdmxTable> (tptr);

    setAttribute (Qt::WA_DeleteOnClose);
    setWindowTitle (QString ("%1 - %2").arg
	(QString::fromStdString (m_hdmx->stringName ())).arg (m_font->fontname));

    QWidget *window = new QWidget (this);
    QVBoxLayout *layout = new QVBoxLayout ();

    layout->addWidget (new QLabel (tr ("PPEM Ranges:")));
    m_ppemBox = new QLineEdit ();
    QFontMetrics fm = m_ppemBox->fontMetrics ();
    m_ppemBox->setMinimumWidth
	(fm.boundingRect ("11-13, 15-17, 19-21, 24, 27, 29, 32-33, 37, 42").width ());
    layout->addWidget (m_ppemBox);

    QHBoxLayout *buttLayout = new QHBoxLayout ();
    m_compileButton = new QPushButton (QWidget::tr ("C&ompile table"));
    connect (m_compileButton, &QPushButton::clicked, this, &HdmxEdit::save);
    m_cancelButton = new QPushButton (QWidget::tr ("&Cancel"));
    connect (m_cancelButton, &QPushButton::clicked, this, &HdmxEdit::close);

    buttLayout->addWidget (m_compileButton);
    buttLayout->addWidget (m_cancelButton);
    layout->addLayout (buttLayout);

    window->setLayout (layout);
    setCentralWidget (window);
    fillPpemBox ();

    m_valid = true;
}

bool HdmxEdit::checkUpdate (bool) {
    return true;
}

bool HdmxEdit::isModified () {
    return m_hdmx->modified ();
}

bool HdmxEdit::isValid () {
    return m_valid;
}

std::shared_ptr<FontTable> HdmxEdit::table () {
    return m_hdmx;
}

void HdmxEdit::closeEvent (QCloseEvent *event) {
    // If we are going to delete the font, ignore changes in table edits
    if (!isModified () || checkUpdate (true))
        m_hdmx->clearEditor ();
    else
        event->ignore ();
}

static int parse_ppems (std::string &edited, std::vector<uint8_t> &ppems, int &sel_start, int &sel_len) {
    size_t pos = 0;
    size_t len = edited.length ();
    int last_ppem = 0;
    bool range = false;
    while (pos < len) {
	uint8_t code = edited[pos];
	while (std::isspace (code)) {
	    pos++;
	    code = edited[pos];
	}
	if (std::isdigit (code)) {
	    int len = 0;
	    do {
		len++;
	    } while (std::isdigit (edited[pos+len]));
	    std::string str_code = edited.substr (pos, len);
	    int val = std::atoi (str_code.c_str ());
	    if (val > 0xff) {
		sel_start = pos;
		sel_len = len;
		return 2;
	    }
	    if (range && last_ppem < val) {
		for (int i=last_ppem+1; i<val; i++)
		    ppems.push_back (i);
		range = false;
	    }
	    ppems.push_back (val);
	    pos += len;
	    last_ppem = val;
	} else if (code == '-' && last_ppem > 0) {
	    range = true;
	    pos++;
	} else if (code == ',' || code == ';') {
	    pos++;
	} else {
	    sel_start = pos;
	    sel_len = 1;
	    return 1;
	}
    }
    if (ppems.empty ()) return 3;
    return 0;
}

void HdmxEdit::save () {
    std::vector<uint8_t> ppems;
    int sel_start, sel_len;
    std::string str_ppems = m_ppemBox->text ().toStdString ();
    int ret = parse_ppems (str_ppems, ppems, sel_start, sel_len);
    DeviceMetricsProvider dmp (*m_font);
    switch (ret) {
      case 0:
	close ();
	m_hdmx->clear ();
	m_hdmx->addSize (ppems[0]);
	m_hdmx->setNumGlyphs (m_font->glyph_cnt);
	for (size_t i=1; i<ppems.size (); i++)
	    m_hdmx->addSize (ppems[i]);
	if (!dmp.calculateHdmx (*m_hdmx.get (), this)) {
	    m_hdmx->packData ();
	    emit (update (m_hdmx));
	} else if (!m_hdmx->isNew ()) {
	    m_hdmx->clear ();
	    m_hdmx->unpackData (m_font);
	}
	break;
      case 1:
	m_ppemBox->setSelection (sel_start, sel_len);
        FontShepherd::postError (tr ("'hdmx' compile error"),
	    tr ("Unsupported character in PPEM list"), this);
	break;
      case 2:
	m_ppemBox->setSelection (sel_start, sel_len);
        FontShepherd::postError (tr ("'hdmx' compile error"),
	    tr ("The number is too large"), this);
	break;
      case 3:
        FontShepherd::postError (tr ("'hdmx' compile error"),
	    tr ("Please specify at least one PPEM size"), this);
	break;
    }
}

void HdmxEdit::fillPpemBox () {
    std::ostringstream s;
    std::string st;

    int start=0, end=0;
    for (auto pair: m_hdmx->records) {
	if (!start) {
	    start = pair.first;
	    end = pair.first;
	} else if (pair.first == end+1) {
	    end = pair.first;
	} else {
	    if (start == end) {
		s << start << ", ";
	    } else {
		s << start << '-' << end << ", ";
	    }
	    start = end = pair.first;
	}
    }
    if (start > 0 && start == end) {
	s << start;
    } else if (start > 0) {
	s << start << '-' << end;
    }
    st = s.str ();
    m_ppemBox->setText (QString::fromStdString (st));
}

VdmxEdit::VdmxEdit (std::shared_ptr<FontTable> tptr, sFont* font, QWidget *parent) :
    TableEdit (parent, Qt::Window), m_font (font) {
    m_vdmx = std::dynamic_pointer_cast<VdmxTable> (tptr);

    setAttribute (Qt::WA_DeleteOnClose);
    setWindowTitle (QString ("%1 - %2").arg
	(QString::fromStdString (m_vdmx->stringName ())).arg (m_font->fontname));

    QWidget *window = new QWidget (this);
    QGridLayout *layout = new QGridLayout ();

    layout->addWidget (new QLabel (tr ("Table version:")), 0, 0, 1, 1);
    m_versionBox = new QComboBox ();
    m_versionBox->addItem ("0: Symbol or ANSI encoded fonts", 0);
    m_versionBox->addItem ("1: ANSI encoding or no special subset", 1);
    layout->addWidget (m_versionBox, 0, 1, 1, 1);

    m_versionBox->setCurrentIndex
	(m_versionBox->findData (m_vdmx->version (), Qt::UserRole));
    connect (m_versionBox, static_cast<void (QComboBox::*)(int)> (&QComboBox::currentIndexChanged),
	this, &VdmxEdit::setTableVersion);

    m_ratioTab = new QTableWidget ();
    layout->addWidget (m_ratioTab, 1, 0, 6, 2);

    m_compileButton = new QPushButton (QWidget::tr ("Compile"));
    connect (m_compileButton, &QPushButton::clicked, this, &VdmxEdit::save);
    layout->addWidget (m_compileButton, 1, 2, 1, 1);

    m_addButton = new QPushButton (QWidget::tr ("Add"));
    connect (m_addButton, &QPushButton::clicked, this, &VdmxEdit::addRatio);
    layout->addWidget (m_addButton, 2, 2, 1, 1);

    m_removeButton = new QPushButton (QWidget::tr ("Remove"));
    connect (m_removeButton, &QPushButton::clicked, this, &VdmxEdit::removeRatio);
    layout->addWidget (m_removeButton, 3, 2, 1, 1);

    m_upButton = new QPushButton (QWidget::tr ("Up"));
    connect (m_upButton, &QPushButton::clicked, this, &VdmxEdit::ratioUp);
    layout->addWidget (m_upButton, 4, 2, 1, 1);

    m_downButton = new QPushButton (QWidget::tr ("Down"));
    connect (m_downButton, &QPushButton::clicked, this, &VdmxEdit::ratioDown);
    layout->addWidget (m_downButton, 5, 2, 1, 1);

    m_cancelButton = new QPushButton (QWidget::tr ("Cancel"));
    connect (m_cancelButton, &QPushButton::clicked, this, &VdmxEdit::close);
    layout->addWidget (m_cancelButton, 6, 2, 1, 1);

    window->setLayout (layout);
    setCentralWidget (window);

    fillControls ();
    setTableVersion (m_versionBox->currentIndex ());
    m_ratioTab->selectRow (0);
    m_upButton->setEnabled (false);
    m_downButton->setEnabled (m_ratioTab->rowCount () > 1);
    connect (m_ratioTab, &QTableWidget::itemSelectionChanged, this, &VdmxEdit::on_selectionChange);

    m_valid = true;
}

bool VdmxEdit::checkUpdate (bool) {
    return true;
}

bool VdmxEdit::isModified () {
    return m_vdmx->modified ();
}

bool VdmxEdit::isValid () {
    return m_valid;
}

std::shared_ptr<FontTable> VdmxEdit::table () {
    return m_vdmx;
}

void VdmxEdit::setTableVersion (int idx) {
    uint16_t newver = m_versionBox->itemData (idx, Qt::UserRole).toUInt ();
    m_compileButton->setEnabled (newver == 1);
    TrueFalseDelegate *delegate;
    switch (newver) {
      case 0:
	delegate = new TrueFalseDelegate (this, "0: Symbol", "1: ANSI");
	break;
      case 1:
	delegate = new TrueFalseDelegate (this, "0: ANSI", "1: No subset");
    }
    m_ratioTab->setItemDelegateForColumn (0, delegate);
    for (int i=0; i<m_ratioTab->rowCount (); i++) {
	auto item = m_ratioTab->item (i, 0);
	bool val = item->data (Qt::UserRole).toBool ();
	item->setText (delegate->byVal (val));
    }
}

// takes and returns the whole row
static QList<QTableWidgetItem*> takeRow (QTableWidget *tw, int row) {
    QList<QTableWidgetItem*> rowItems;
    for (int col = 0; col < tw->columnCount (); col++) {
    	rowItems << tw->takeItem (row, col);
    }
    return rowItems;
}

// sets the whole row
void setRow (QTableWidget *tw, int row, const QList<QTableWidgetItem*>& rowItems) {
    for (int col = 0; col < tw->columnCount (); col++) {
    	tw->setItem (row, col, rowItems.at (col));
    }
}

void VdmxEdit::moveRatio (int sourceRow, bool up) {
    Q_ASSERT (m_ratioTab->selectedItems ().count () > 0);
    const int destRow = (up ? sourceRow-1 : sourceRow+1);
    Q_ASSERT (destRow >= 0 && destRow < m_ratioTab->rowCount ());

    // take whole rows
    QList<QTableWidgetItem*> sourceItems = takeRow (m_ratioTab, sourceRow);
    QList<QTableWidgetItem*> destItems = takeRow (m_ratioTab, destRow);

    // set back in reverse order
    setRow (m_ratioTab, sourceRow, destItems);
    setRow (m_ratioTab, destRow, sourceItems);
}

void VdmxEdit::ratioUp () {
    QItemSelectionModel *select = m_ratioTab->selectionModel ();
    if (select->hasSelection ()) {
	const int rowidx = m_ratioTab->row (m_ratioTab->selectedItems ().at (0));
	if (rowidx > 0) {
	    moveRatio (rowidx, true);
	    m_ratioTab->selectRow (rowidx-1);
	}
    }
}

void VdmxEdit::ratioDown () {
    QItemSelectionModel *select = m_ratioTab->selectionModel ();
    if (select->hasSelection ()) {
	const int rowidx = m_ratioTab->row (m_ratioTab->selectedItems ().at (0));
	if (rowidx < (m_ratioTab->rowCount () - 1)) {
	    moveRatio (rowidx, false);
	    m_ratioTab->selectRow (rowidx+1);
	}
    }
}

void VdmxEdit::addRatio () {
    int idx = m_ratioTab->rowCount ();
    m_ratioTab->insertRow (idx);
    auto delegate = qobject_cast<TrueFalseDelegate *> (m_ratioTab->itemDelegateForColumn (0));

    auto csItem = new QTableWidgetItem ();
    csItem->setData (Qt::UserRole, true);
    csItem->setText (delegate->byVal (true));
    m_ratioTab->setItem (idx, 0, csItem);

    auto xRatItem = new QTableWidgetItem (QString::number (0));
    xRatItem->setData (Qt::UserRole, 0);
    m_ratioTab->setItem (idx, 1, xRatItem);

    auto yStartRatItem = new QTableWidgetItem (QString::number (0));
    yStartRatItem->setData (Qt::UserRole, 0);
    m_ratioTab->setItem (idx, 2, yStartRatItem);

    auto yEndRatItem = new QTableWidgetItem (QString::number (0));
    yEndRatItem->setData (Qt::UserRole, 0);
    m_ratioTab->setItem (idx, 3, yEndRatItem);

    auto yStartPelItem = new QTableWidgetItem (QString::number (8));
    yStartPelItem->setData (Qt::UserRole, 8);
    m_ratioTab->setItem (idx, 4, yStartPelItem);

    auto yEndPelItem = new QTableWidgetItem (QString::number (255));
    yEndPelItem->setData (Qt::UserRole, 255);
    m_ratioTab->setItem (idx, 5, yEndPelItem);

    m_ratioTab->selectRow (idx);
}

void VdmxEdit::removeRatio () {
    QItemSelectionModel *sel_mod = m_ratioTab->selectionModel ();
    QModelIndexList row_lst = sel_mod->selectedRows ();
    if (row_lst.size ()) {
        QModelIndex rowidx = row_lst.first ();
        m_ratioTab->removeRow (rowidx.row ());
    }
    m_removeButton->setEnabled (m_ratioTab->rowCount () > 1);
}

void VdmxEdit::on_selectionChange () {
    QItemSelectionModel *select = m_ratioTab->selectionModel ();
    if (select->hasSelection ()) {
	const int rowidx = m_ratioTab->row (m_ratioTab->selectedItems ().at (0));
	m_upButton->setEnabled (rowidx > 0);
	m_downButton->setEnabled (rowidx < (m_ratioTab->rowCount () - 1));
    }
}

void VdmxEdit::fillControls () {
    m_ratioTab->setColumnCount (6);
    QStringList labels = {
	"Charset", "X Rat", "Start Y Rat", "End Y Rat", "Max Y Pels", "Min Y Pels"
    };
    m_ratioTab->setHorizontalHeaderLabels (labels);

    m_ratioTab->setItemDelegateForColumn (1, new SpinBoxDelegate (0, 96, this));
    m_ratioTab->setItemDelegateForColumn (2, new SpinBoxDelegate (0, 96, this));
    m_ratioTab->setItemDelegateForColumn (3, new SpinBoxDelegate (0, 96, this));
    m_ratioTab->setItemDelegateForColumn (4, new SpinBoxDelegate (6, 255, this));
    m_ratioTab->setItemDelegateForColumn (5, new SpinBoxDelegate (6, 255, this));

    QFontMetrics fm = m_ratioTab->fontMetrics ();
    int fullw = fm.boundingRect ("~1: No subset~").width ();
    m_ratioTab->setColumnWidth (0, fullw);
    for (int i=1; i<6; i++) {
	int w = fm.boundingRect ("~" + labels[i] + "~").width ();
	m_ratioTab->setColumnWidth (i, w);
	fullw += w;
    }

    m_ratioTab->horizontalHeader ()->setStretchLastSection (true);
    m_ratioTab->setSelectionBehavior (QAbstractItemView::SelectRows);
    m_ratioTab->setSelectionMode (QAbstractItemView::SingleSelection);

    auto &recs = m_vdmx->records;
    m_ratioTab->setRowCount (recs.size ());
    for (size_t i=0; i<recs.size (); i++) {
	auto &rec = recs[i];

	auto csItem = new QTableWidgetItem ();
	csItem->setData (Qt::UserRole, static_cast<bool> (rec.bCharSet));
	m_ratioTab->setItem (i, 0, csItem);

	auto xRatItem = new QTableWidgetItem (QString::number (rec.xRatio));
	xRatItem->setData (Qt::UserRole, rec.xRatio);
	m_ratioTab->setItem (i, 1, xRatItem);

	auto yStartRatItem = new QTableWidgetItem (QString::number (rec.yStartRatio));
	yStartRatItem->setData (Qt::UserRole, rec.yStartRatio);
	m_ratioTab->setItem (i, 2, yStartRatItem);

	auto yEndRatItem = new QTableWidgetItem (QString::number (rec.yEndRatio));
	yEndRatItem->setData (Qt::UserRole, rec.yEndRatio);
	m_ratioTab->setItem (i, 3, yEndRatItem);

	auto yStartPelItem = new QTableWidgetItem (QString::number (rec.startsz));
	yStartPelItem->setData (Qt::UserRole, rec.startsz);
	m_ratioTab->setItem (i, 4, yStartPelItem);

	auto yEndPelItem = new QTableWidgetItem (QString::number (rec.endsz));
	yEndPelItem->setData (Qt::UserRole, rec.endsz);
	m_ratioTab->setItem (i, 5, yEndPelItem);
    }

    m_ratioTab->resize (fullw, m_ratioTab->rowHeight (0) * 5);
}

void VdmxEdit::save () {
    uint16_t nRatios = m_ratioTab->rowCount ();
    if (!nRatios) {
        FontShepherd::postError (tr ("'vdmx' compile error"),
	    tr ("There should be at least one ratio specified"), this);
	return;
    }
    close ();
    m_vdmx->m_version = 1;
    m_vdmx->clear ();
    for (size_t i=0; i<nRatios; i++) {
	auto item = m_ratioTab->item (i, 1);
	uint8_t xRatio = item->data (Qt::UserRole).toUInt ();
	item = m_ratioTab->item (i, 2);
	uint8_t yStartRatio = item->data (Qt::UserRole).toUInt ();
	item = m_ratioTab->item (i, 3);
	uint8_t yEndRatio = item->data (Qt::UserRole).toUInt ();
	item = m_ratioTab->item (i, 4);
	uint8_t startsz = item->data (Qt::UserRole).toUInt ();
	item = m_ratioTab->item (i, 5);
	uint8_t endsz = item->data (Qt::UserRole).toUInt ();

	m_vdmx->addRatio (xRatio, yStartRatio, yEndRatio);
	m_vdmx->setRatioRange (i, startsz, endsz);
    }

    DeviceMetricsProvider dmp (*m_font);
    if (!dmp.calculateVdmx (*m_vdmx.get (), this)) {
        m_vdmx->packData ();
        emit (update (m_vdmx));
    } else {
	if (!m_vdmx->isNew ()) {
	    m_vdmx->clear ();
	    m_vdmx->unpackData (m_font);
	    FontShepherd::postError (tr ("'vdmx' compile error"),
		tr ("Could not calculate 'vdmx': freetype error"), this);
	}
    }
}

void VdmxEdit::closeEvent (QCloseEvent *event) {
    // If we are going to delete the font, ignore changes in table edits
    if (!isModified () || checkUpdate (true))
        m_vdmx->clearEditor ();
    else
        event->ignore ();
}
