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
#include "editors/postedit.h" // also includes tables.h
#include "tables/cmap.h"
#include "tables/glyphnames.h"
#include "tables/glyphcontainer.h"

#include "fs_notify.h"
#include "icuwrapper.h"

QList<QPair<QString, double>> PostEdit::postVersions = {
    {"1.0: Standard Mac glyph names", 1.0},
    {"2.0: Glyph names stored in the \'post\' table", 2.0},
    {"2.5: Standard Mac glyph names reordered (deprecated)", 2.5},
    {"3.0: No glyph names", 3.0},
    {"4.0: Character codes for composite fonts (deprecated)", 4.0},
};

PostEdit::PostEdit (FontTable* tbl, sFont* font, QWidget *parent) :
    TableEdit (parent, Qt::Window), m_font (font) {
    m_post = dynamic_cast<PostTable *>(tbl);
    m_regVal = std::unique_ptr<QRegularExpressionValidator> (new QRegularExpressionValidator ());
    m_regVal->setRegularExpression (QRegularExpression ("(0x[A-Fa-f0-9]+|\\d+)"));

    m_gnp = std::unique_ptr<GlyphNameProvider> (new GlyphNameProvider (*m_font));

    setAttribute (Qt::WA_DeleteOnClose);
    setWindowTitle (QString ("post - ").append (m_font->fontname));

    QWidget *window = new QWidget (this);
    m_tab = new QTabWidget (window);

    QWidget *gen_tab = new QWidget ();

    QGridLayout *gen_layout = new QGridLayout ();
    gen_layout->setAlignment (Qt::AlignTop);
    gen_tab->setLayout (gen_layout);

    gen_layout->addWidget (new QLabel (tr ("Version number of the \'post\' table")), 0, 0);
    m_versionBox = new QComboBox ();
    gen_layout->addWidget (m_versionBox, 0, 1);

    gen_layout->addWidget (new QLabel (tr ("ItalicAngle")), 1, 0);
    m_italicAngleBox = new QDoubleSpinBox ();
    gen_layout->addWidget (m_italicAngleBox, 1, 1);

    gen_layout->addWidget (new QLabel (tr ("Underline position")), 2, 0);
    m_underPosField = new QSpinBox ();
    m_underPosField->setMinimum (-32768);
    m_underPosField->setMaximum (32767);
    gen_layout->addWidget (m_underPosField, 2, 1);

    gen_layout->addWidget (new QLabel (tr ("Underline thickness")), 3, 0);
    m_underThickField = new QSpinBox ();
    m_underThickField->setMinimum (-32768);
    m_underThickField->setMaximum (32767);
    gen_layout->addWidget (m_underThickField, 3, 1);

    m_fixedPitchBox = new QCheckBox ();
    m_fixedPitchBox->setText (tr ("Font is monospaced"));
    gen_layout->addWidget (m_fixedPitchBox, 4, 0);

    gen_layout->addWidget (new QLabel (tr ("Minimum memory usage for Type 42")), 5, 0);
    m_minMem42Box = new QLineEdit ();
    m_minMem42Box->setValidator (m_regVal.get ());
    gen_layout->addWidget (m_minMem42Box, 5, 1);

    gen_layout->addWidget (new QLabel (tr ("Maximum memory usage for Type 42")), 6, 0);
    m_maxMem42Box = new QLineEdit ();
    m_maxMem42Box->setValidator (m_regVal.get ());
    gen_layout->addWidget (m_maxMem42Box, 6, 1);

    gen_layout->addWidget (new QLabel (tr ("Minimum memory usage for Type 1")), 7, 0);
    m_minMem1Box = new QLineEdit ();
    m_minMem1Box->setValidator (m_regVal.get ());
    gen_layout->addWidget (m_minMem1Box, 7, 1);

    gen_layout->addWidget (new QLabel (tr ("Maximum memory usage for Type 1")), 8, 0);
    m_maxMem1Box = new QLineEdit ();
    m_maxMem1Box->setValidator (m_regVal.get ());
    gen_layout->addWidget (m_maxMem1Box, 8, 1);

    m_gnTab = new QTableWidget (m_tab);
    m_tab->addTab (gen_tab, QWidget::tr ("&General"));
    m_tab->addTab (m_gnTab, QWidget::tr ("Glyph &names"));

    QVBoxLayout *layout = new QVBoxLayout ();
    layout->addWidget (m_tab);

    saveButton = new QPushButton (tr ("&Compile table"));
    closeButton = new QPushButton (tr ("C&lose"));

    QHBoxLayout *buttLayout = new QHBoxLayout ();
    buttLayout->addWidget (saveButton);
    buttLayout->addWidget (closeButton);
    layout->addLayout (buttLayout);

    connect (saveButton, &QPushButton::clicked, this, &PostEdit::save);
    connect (closeButton, &QPushButton::clicked, this, &PostEdit::close);

    window->setLayout (layout);
    setCentralWidget (window);

    fillControls ();

    m_valid = true;
}

PostEdit::~PostEdit () {
};

void PostEdit::fillControls () {
    for (int i=0; i<postVersions.size (); i++)
	m_versionBox->addItem (postVersions[i].first, postVersions[i].second);
    connect (m_versionBox, static_cast<void (QComboBox::*)(int)> (&QComboBox::currentIndexChanged),
	this, &PostEdit::setTableVersion);
    QStandardItemModel* model = qobject_cast<QStandardItemModel*> (m_versionBox->model ());
    QStandardItem* item25 = model->item (2);
    QStandardItem* item40 = model->item (4);
    item25->setFlags (item25->flags() & ~Qt::ItemIsEnabled);
    item40->setFlags (item40->flags() & ~Qt::ItemIsEnabled);

    m_gnTab->setColumnCount (3);
    m_gnTab->horizontalHeader ()->setSectionResizeMode (QHeaderView::Stretch);
    m_gnTab->horizontalHeader ()->setStretchLastSection (true);
    m_gnTab->setSelectionBehavior (QAbstractItemView::SelectRows);
    m_gnTab->setSelectionMode (QAbstractItemView::SingleSelection);

    resetData ();
}

void PostEdit::fillGlyphTab (QTableWidget *tab) {
    uint16_t gcnt = m_post->numberOfGlyphs ();
    tab->setRowCount (gcnt);
    CmapEnc *enc = m_gnp->encoding ();

    QString enc_title = (enc && enc->isUnicode ()) ?
	QWidget::tr ("Unicode") : QWidget::tr ("Encoded");
    m_gnTab->setHorizontalHeaderLabels (QStringList () << tr ("GID") << enc_title << tr ("Glyph name"));

    for (uint16_t i=0; i<gcnt; i++) {
        auto gid_item = new QTableWidgetItem
	    (QString ("%1 (0x%2)").arg (i).arg (i, 2, 16, QLatin1Char ('0')));
	gid_item->setFlags (gid_item->flags() & ~Qt::ItemIsEditable);
	gid_item->setData (Qt::UserRole, i);
	QString repr = enc ? enc->gidCodeRepr (i) : "<unencoded>";
	auto uni_item = new QTableWidgetItem (repr);
	uni_item->setFlags (uni_item->flags() & ~Qt::ItemIsEditable);
	if (enc && enc->isUnicode ()) {
	    auto uni = enc->unicode (i);
	    if (!uni.empty ())
		uni_item->setToolTip (QString::fromStdString (IcuWrapper::unicodeCharName (uni[0])));
	}
	auto name_item = new QTableWidgetItem
	    (QString::fromStdString (m_post->glyphName (i)));
        tab->setItem (i, 0, gid_item);
        tab->setItem (i, 1, uni_item);
        tab->setItem (i, 2, name_item);
    }
}

void PostEdit::resetData () {
    m_versionBox->setCurrentIndex
	(m_versionBox->findData (m_post->version (), Qt::UserRole));
    m_tab->setTabVisible (1, m_post->version () == 2.0);

    m_italicAngleBox->setValue (m_post->italicAngle ());
    m_underPosField->setValue (m_post->underlinePosition ());
    m_underThickField->setValue (m_post->underlineThickness ());
    m_fixedPitchBox->setChecked (m_post->isFixedPitch ());

    m_minMem42Box->setText (QString ("0x%1").arg (m_post->minMemType42 (), 0, 16));;
    m_maxMem42Box->setText (QString ("0x%1").arg (m_post->maxMemType42 (), 0, 16));
    m_minMem1Box->setText (QString ("0x%1").arg (m_post->minMemType1 (), 0, 16));
    m_maxMem1Box->setText (QString ("0x%1").arg (m_post->maxMemType1 (), 0, 16));

    m_gnTab->clearContents ();
    m_gnTab->setRowCount (0);
    fillGlyphTab (m_gnTab);
}

bool PostEdit::checkUpdate (bool) {
    return true;
}

bool PostEdit::isModified () {
    return m_post->modified ();
}

bool PostEdit::isValid () {
    return m_valid;
}

FontTable* PostEdit::table () {
    return m_post;
}

void PostEdit::closeEvent (QCloseEvent *event) {
    // If we are going to delete the font, ignore changes in table edits
    if (!isModified () || checkUpdate (true))
        m_post->clearEditor ();
    else
        event->ignore ();
}

void PostEdit::save () {
    double newver = m_versionBox->itemData (m_versionBox->currentIndex ()).toDouble ();
    m_post->setVersion (newver, m_gnp.get ());
    post_data &pd = m_post->contents;
    pd.version = newver;
    pd.italicAngle = m_italicAngleBox->value ();
    pd.underlinePosition = m_underPosField->value ();
    pd.underlineThickness = m_underThickField->value ();
    pd.isFixedPitch = m_fixedPitchBox->isChecked ();
    pd.minMemType42 = m_minMem42Box->text ().toInt ();
    pd.maxMemType42 = m_maxMem42Box->text ().toInt ();
    pd.minMemType1 = m_minMem1Box->text ().toInt ();
    pd.maxMemType1 = m_maxMem1Box->text ().toInt ();

    if (newver == 2.0) {
	for (int i=0; i<m_gnTab->rowCount (); i++) {
	    auto name_item = m_gnTab->item (i, 2);
	    const std::string &name = name_item->text ().toStdString ();
	    m_post->setGlyphName (i, name);
	}
    }

    m_post->packData ();
    emit glyphNamesChanged ();
    emit (update (m_post));
    close ();
}

void PostEdit::setTableVersion (int idx) {
    double newver = postVersions[idx].second;
    if (newver == m_post->version ())
	return;

    if (newver == 3.0) {
        int choice = FontShepherd::postYesNoQuestion (
	    QCoreApplication::tr ("Setting 'post' table version"),
	    QCoreApplication::tr (
	    "Are you sure you would like to remove glyph names "
	    "from the 'post' table?"),
	    this);
        if (choice == QMessageBox::No) {
	    m_versionBox->setCurrentIndex
		(m_versionBox->findData (m_post->version (), Qt::UserRole));
	    return;
	}
    } else if (newver == 2.0 && m_gnp->glyphNameSource () == CHR ('C','F','F',' ')) {
        int choice = FontShepherd::postYesNoQuestion (
	    QCoreApplication::tr ("Setting 'post' table version"),
	    QCoreApplication::tr (
	    "This is an OpenType-CFF font, which stores its glyph names "
	    "in the 'CFF ' table. Would you like to additionally put them to te 'post' table?"),
	    this);
        if (choice == QMessageBox::No) {
	    m_versionBox->setCurrentIndex
		(m_versionBox->findData (m_post->version (), Qt::UserRole));
	    return;
	}
    }

    m_post->setVersion (newver, m_gnp.get ());
    m_gnTab->clearContents ();
    m_gnTab->setRowCount (0);
    if (newver == 2.0)
	fillGlyphTab (m_gnTab);
    if (m_post->version () != newver) {
	m_versionBox->setCurrentIndex
	    (m_versionBox->findData (m_post->version (), Qt::UserRole));
    }
    m_tab->setTabVisible (1, m_post->version () == 2.0);
}
