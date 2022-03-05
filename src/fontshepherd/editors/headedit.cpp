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
#include "editors/headedit.h" // also includes tables.h
#include "tables/head.h"

#include "fs_notify.h"

QStringList HeadEdit::flagDesc = {
    QWidget::tr ("Baseline for font at y=0"),
    QWidget::tr ("Left sidebearing point at x=0"),
    QWidget::tr ("Instructions may depend on point size"),
    QWidget::tr ("Force ppem to integer values"),
    QWidget::tr ("Instructions may alter advance width"),
    QWidget::tr ("Apple: x-coord of 0 corresponds to the desired vertical baseline"),
    QWidget::tr ("(Unused)"),
    QWidget::tr ("Apple: Requires layout for correct linguistic rendering"),
    QWidget::tr ("Apple: Has one or more default metamorphosis effects"),
    QWidget::tr ("Apple: Contains strong right-to-left glyphs"),
    QWidget::tr ("Apple: Contains Indic-style rearrangement effects"),
    QWidget::tr ("Font data is \"lossless\" as a result of an optimizing transformation"),
    QWidget::tr ("Font converted (produce compatible metrics)"),
    QWidget::tr ("Optimized for ClearType"),
    QWidget::tr ("Last resort font"),
    QWidget::tr ("(Unused)"),
};

QStringList HeadEdit::macStyleDesc = {
    QWidget::tr ("Bold"),
    QWidget::tr ("Italic"),
    QWidget::tr ("Underline"),
    QWidget::tr ("Outline"),
    QWidget::tr ("Shadow"),
    QWidget::tr ("Condensed"),
    QWidget::tr ("Extended"),
    QWidget::tr ("(Reserved)"),
};

QList<QPair<QString, int>> HeadEdit::fontDirHints = {
    {"0: Fully mixed directional glyphs", 0},
    {"1: Only strongly left to right", 1},
    {"2: Like 1 but also contains neutrals", 2},
    {"-1: Only strongly right to left", -1},
    {"-2: Like -1 but also contains neutrals", -2},
};

HeadEdit::HeadEdit (FontTable* tbl, sFont* font, QWidget *parent) :
    TableEdit (parent, Qt::Window), m_font (font) {
    m_head = dynamic_cast<HeadTable *>(tbl);

    setAttribute (Qt::WA_DeleteOnClose);
    setWindowTitle (QString ("head - ").append (m_font->fontname));

    QWidget *window = new QWidget (this);
    m_tab = new QTabWidget (window);

    QWidget *ver_tab = new QWidget ();
    QWidget *flg_tab = new QWidget ();
    QWidget *mtx_tab = new QWidget ();

    QGridLayout *ver_layout = new QGridLayout ();
    ver_layout->setAlignment (Qt::AlignTop);
    ver_tab->setLayout (ver_layout);

    ver_layout->addWidget (new QLabel (tr ("Version number of the font header table")), 0, 0);
    m_versionBox = new QDoubleSpinBox ();
    ver_layout->addWidget (m_versionBox, 0, 1);

    ver_layout->addWidget (new QLabel (tr ("Font revision")), 1, 0);
    m_fontRevisionBox = new QDoubleSpinBox ();
    ver_layout->addWidget (m_fontRevisionBox, 1, 1);

    ver_layout->addWidget (new QLabel (tr ("Checksum adjustment")), 2, 0);
    m_checkSumField = new QLineEdit ();
    ver_layout->addWidget (m_checkSumField, 2, 1);

    ver_layout->addWidget (new QLabel (tr ("Magic number")), 3, 0);
    m_magicField = new QLineEdit ();
    ver_layout->addWidget (m_magicField, 3, 1);

    ver_layout->addWidget (new QLabel (tr ("Created date/time")), 4, 0);
    m_createdBox = new QDateTimeEdit ();
    ver_layout->addWidget (m_createdBox, 4, 1);

    ver_layout->addWidget (new QLabel (tr ("Modified date/time")), 5, 0);
    m_modifiedBox = new QDateTimeEdit ();
    ver_layout->addWidget (m_modifiedBox, 5, 1);

    ver_layout->addWidget (new QLabel (tr ("Smallest readable size in pixels")), 6, 0);
    m_lowestRecBox = new QSpinBox ();
    ver_layout->addWidget (m_lowestRecBox, 6, 1);

    ver_layout->addWidget (new QLabel (tr ("Font direction hint")), 7, 0);
    m_fontDirectionBox = new QComboBox ();
    ver_layout->addWidget (m_fontDirectionBox, 7, 1);

    ver_layout->addWidget (new QLabel (tr ("Offsets to glyphs in 'loca' table")), 8, 0);
    m_indexToLocFormatBox = new QComboBox ();
    ver_layout->addWidget (m_indexToLocFormatBox, 8, 1);

    ver_layout->addWidget (new QLabel (tr ("Glyph data format")), 9, 0);
    m_glyphDataFormatBox = new QSpinBox ();
    ver_layout->addWidget (m_glyphDataFormatBox, 9, 1);

    m_tab->addTab (ver_tab, QWidget::tr ("&General"));

    auto *flg_layout = new QVBoxLayout ();
    flg_layout->setAlignment (Qt::AlignTop);
    flg_tab->setLayout (flg_layout);
    m_flagList = new QListWidget ();
    flg_layout->addWidget (m_flagList);
    m_tab->addTab (flg_tab, QWidget::tr ("&Flags"));

    QGroupBox *mtx_frame = new QGroupBox ();
    QVBoxLayout *fr_layout = new QVBoxLayout ();
    mtx_tab->setLayout (fr_layout);

    QGridLayout *mtx_layout = new QGridLayout ();
    mtx_layout->setAlignment (Qt::AlignTop);
    mtx_frame->setLayout (mtx_layout);
    mtx_frame->setTitle (QWidget::tr ("Font Metrics:"));

    mtx_layout->addWidget (new QLabel (tr ("Units per Em:")), 0, 0);
    m_unitsPerEmBox = new QSpinBox ();
    mtx_layout->addWidget (m_unitsPerEmBox, 0, 1);

    QGroupBox *bb_frame = new QGroupBox ();
    bb_frame->setTitle (QWidget::tr ("Glyph Bounding Box:"));
    mtx_layout->addWidget (bb_frame, 1, 0, 1, 2);

    QGridLayout *bb_layout = new QGridLayout ();
    bb_layout->setAlignment (Qt::AlignTop);
    bb_frame->setLayout (bb_layout);

    bb_layout->addWidget (new QLabel ("X"), 0, 1);
    bb_layout->addWidget (new QLabel ("Y"), 0, 2);

    bb_layout->addWidget (new QLabel (tr ("Minimum")), 1, 0);
    m_xMinBox = new QSpinBox ();
    bb_layout->addWidget (m_xMinBox, 1, 1);
    m_yMinBox = new QSpinBox ();
    bb_layout->addWidget (m_yMinBox, 1, 2);

    bb_layout->addWidget (new QLabel (tr ("Maximum")), 2, 0);
    m_xMaxBox = new QSpinBox ();
    bb_layout->addWidget (m_xMaxBox, 2, 1);
    m_yMaxBox = new QSpinBox ();
    bb_layout->addWidget (m_yMaxBox, 2, 2);

    fr_layout->addWidget (mtx_frame);

    fr_layout->addWidget (new QLabel (tr ("Mac Style flags:")));
    m_macStyleList = new QListWidget ();
    fr_layout->addWidget (m_macStyleList);

    m_tab->addTab (mtx_tab, QWidget::tr ("&Metrics and Style"));

    QVBoxLayout *layout = new QVBoxLayout ();
    layout->addWidget (m_tab);

    saveButton = new QPushButton (tr ("&Compile table"));
    closeButton = new QPushButton (tr ("C&lose"));

    QHBoxLayout *buttLayout = new QHBoxLayout ();
    buttLayout->addWidget (saveButton);
    buttLayout->addWidget (closeButton);
    layout->addLayout (buttLayout);

    connect (saveButton, &QPushButton::clicked, this, &HeadEdit::save);
    connect (closeButton, &QPushButton::clicked, this, &HeadEdit::close);

    window->setLayout (layout);
    setCentralWidget (window);
    fillControls ();

    m_valid = true;
}

void HeadEdit::fillControls () {
    m_versionBox->setValue (m_head->version ());
    m_fontRevisionBox->setValue (m_head->fontRevision ());
    m_checkSumField->setText (QString ("0x%1").arg (m_head->checkSumAdjustment (), 0, 16));
    m_checkSumField->setEnabled (false);
    m_magicField->setText (QString ("0x%1").arg (m_head->magicNumber (), 0, 16));
    if (m_head->magicNumber () == 0x5F0F3CF5)
	m_magicField->setEnabled (false);
    m_createdBox->setDateTime (QDateTime::fromTime_t (m_head->created ()));
    m_modifiedBox->setDateTime (QDateTime::fromTime_t (m_head->modified ()));
    m_lowestRecBox->setValue (m_head->lowestRecPPEM ());

    for (int i=0; i<fontDirHints.size (); i++)
	m_fontDirectionBox->addItem (fontDirHints[i].first, fontDirHints[i].second);
    m_fontDirectionBox->setCurrentIndex
	(m_fontDirectionBox->findData (m_head->fontDirectionHint (), Qt::UserRole));
    m_indexToLocFormatBox->addItem ("0: Short offsets (Offset16)", 0);
    m_indexToLocFormatBox->addItem ("1: Long offsets (Offset32)", 1);
    m_indexToLocFormatBox->setCurrentIndex
	(m_indexToLocFormatBox->findData (m_head->indexToLocFormat (), Qt::UserRole));
    m_indexToLocFormatBox->setEnabled (false);
    m_glyphDataFormatBox->setValue (m_head->glyphDataFormat ());
    m_glyphDataFormatBox->setEnabled (false);

    for (int i=0; i<16; i++) {
	auto *item = new QListWidgetItem ();
	auto iflags = item->flags ();
	iflags |= Qt::ItemIsUserCheckable;
	item->setText (QString ("%1: %2").arg (i, 2, 10).arg (flagDesc[i]));
	item->setCheckState (m_head->flags (i) ? Qt::Checked : Qt::Unchecked);
	if (flagDesc[i] == "(Unused)")
	    iflags &= ~Qt::ItemIsEnabled;
	item->setFlags (iflags);
	m_flagList->addItem (item);
    }

    m_unitsPerEmBox->setMinimum (16);
    m_unitsPerEmBox->setMaximum (16384);
    m_unitsPerEmBox->setValue (m_head->unitsPerEm ());
    m_xMinBox->setMinimum (-32767);
    m_xMinBox->setMaximum (32767);
    m_xMinBox->setValue (m_head->xMin ());
    m_xMaxBox->setMinimum (-32767);
    m_xMaxBox->setMaximum (32767);
    m_xMaxBox->setValue (m_head->xMax ());
    m_yMinBox->setMinimum (-32767);
    m_yMinBox->setMaximum (32767);
    m_yMinBox->setValue (m_head->yMin ());
    m_yMaxBox->setMinimum (-32767);
    m_yMaxBox->setMaximum (32767);
    m_yMaxBox->setValue (m_head->yMax ());

    for (int i=0; i<7; i++) {
	auto *item = new QListWidgetItem ();
	auto iflags = item->flags ();
	iflags |= Qt::ItemIsUserCheckable;
	item->setText (QString ("%1: %2").arg (i).arg (macStyleDesc[i]));
	item->setCheckState (m_head->macStyle (i) ? Qt::Checked : Qt::Unchecked);
	if (macStyleDesc[i] == "Reserved")
	    iflags &= ~Qt::ItemIsEnabled;
	item->setFlags (iflags);
	m_macStyleList->addItem (item);
    }
}

void HeadEdit::resetData () {
    m_versionBox->setValue (m_head->version ());
    m_fontRevisionBox->setValue (m_head->fontRevision ());
    m_checkSumField->setText (QString ("0x%1").arg (m_head->checkSumAdjustment (), 0, 16));
    m_magicField->setText (QString ("0x%1").arg (m_head->magicNumber (), 0, 16));
    if (m_head->magicNumber () == 0x5F0F3CF5)
	m_magicField->setEnabled (false);
    m_createdBox->setDateTime (QDateTime::fromTime_t (m_head->created ()));
    m_modifiedBox->setDateTime (QDateTime::fromTime_t (m_head->modified ()));
    m_lowestRecBox->setValue (m_head->lowestRecPPEM ());
    m_fontDirectionBox->setCurrentIndex
	(m_fontDirectionBox->findData (m_head->fontDirectionHint (), Qt::UserRole));
    m_indexToLocFormatBox->setCurrentIndex
	(m_indexToLocFormatBox->findData (m_head->indexToLocFormat (), Qt::UserRole));
    m_glyphDataFormatBox->setValue (m_head->glyphDataFormat ());
    for (int i=0; i<16; i++) {
	auto item = m_flagList->item (i);
	item->setCheckState (m_head->macStyle (i) ? Qt::Checked : Qt::Unchecked);
    }
    m_unitsPerEmBox->setValue (m_head->unitsPerEm ());
    m_xMinBox->setValue (m_head->xMin ());
    m_xMaxBox->setValue (m_head->xMax ());
    m_yMinBox->setValue (m_head->yMin ());
    m_yMaxBox->setValue (m_head->yMax ());
    for (int i=0; i<7; i++) {
	auto item = m_macStyleList->item (i);
	item->setCheckState (m_head->macStyle (i) ? Qt::Checked : Qt::Unchecked);
    }
}

bool HeadEdit::checkUpdate (bool) {
    return true;
}

bool HeadEdit::isModified () {
    return m_head->modified ();
}

bool HeadEdit::isValid () {
    return m_valid;
}

FontTable* HeadEdit::table () {
    return m_head;
}

void HeadEdit::closeEvent (QCloseEvent *event) {
    // If we are going to delete the font, ignore changes in table edits
    if (!isModified () || checkUpdate (true))
        m_head->clearEditor ();
    else
        event->ignore ();
}

void HeadEdit::save () {
    uint32_t magic = m_magicField->text ().toUInt (nullptr, 0);
    if (magic != 0x5F0F3CF5) {
        int choice = FontShepherd::postYesNoQuestion (
	    QCoreApplication::tr ("Compiling 'head' table"),
	    QCoreApplication::tr (
	    "The Magic Number should be 0x5F0F3CF5, 0x%1 is provided. "
	    "Are you shure?").arg (magic, 0, 16),
	    this);
        if (choice == QMessageBox::No)
	    return;
    }

    head_data &hd = m_head->contents;
    hd.version = m_versionBox->value ();
    hd.fontRevision = m_fontRevisionBox->value ();
    hd.checkSumAdjustment = m_checkSumField->text ().toUInt (nullptr, 0);
    hd.magicNumber = magic;
    for (int i=0; i<16; i++) {
	auto item = m_flagList->item (i);
	hd.flags[i] = (item->checkState () == Qt::Checked);
    }
    hd.unitsPerEm = m_unitsPerEmBox->value ();
    hd.created = m_createdBox->dateTime ().toTime_t ();
    hd.modified = m_modifiedBox->dateTime ().toTime_t ();
    hd.xMin = m_xMinBox->value ();
    hd.yMin = m_yMinBox->value ();
    hd.xMax = m_xMaxBox->value ();
    hd.yMax = m_yMaxBox->value ();
    for (int i=0; i<7; i++) {
	auto item = m_macStyleList->item (i);
	hd.macStyle[i] = (item->checkState () == Qt::Checked);
    }
    hd.lowestRecPPEM = m_lowestRecBox->value ();
    hd.fontDirectionHint = m_fontDirectionBox->itemData (m_fontDirectionBox->currentIndex ()).toInt ();
    hd.indexToLocFormat = m_indexToLocFormatBox->itemData (m_indexToLocFormatBox->currentIndex ()).toInt ();
    hd.glyphDataFormat = m_glyphDataFormatBox->value ();

    m_head->packData ();
    emit (update (m_head));
    close ();
}
