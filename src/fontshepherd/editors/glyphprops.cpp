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

#include "sfnt.h"
#include "tables.h"
#include "tables/glyphcontainer.h" // also includes splineglyph.h
#include "tables/cmap.h"
#include "tables/gdef.h"
#include "tables/cff.h"
#include "tables/mtx.h"
#include "tables/glyphnames.h"
#include "editors/unispinbox.h"
#include "editors/glyphprops.h"

#include "fs_notify.h"

static QString pack_ucodes (const std::vector<uint32_t> &ucodes) {
    QStringList ulist;
    for (uint32_t uni : ucodes)
	ulist << QString ("U+%1").arg (uni, 4, 16, QLatin1Char ('0'));

    return ulist.join (QChar (' '));
}

static std::vector<uint32_t> unpack_ucodes (const QString &packed) {
    std::vector<uint32_t> ret;
    QStringList ulist = packed.split (QChar (' '));
    QRegularExpression rx ("U\\+([0-9a-fA-F]+)");
    ret.reserve (ulist.size ());

    for (auto &suni : ulist) {
	QRegularExpressionMatch match = rx.match (suni);
	if (match.hasMatch ()) {
	    bool ok;
	    uint16_t val = match.captured (1).toInt (&ok, 16);
	    // avoid mapping to zero Unicode
	    if (val) ret.push_back (val);
	}
    }
    std::sort (ret.begin (), ret.end ());
    return ret;
}

GlyphPropsDialog::GlyphPropsDialog (sFont* fnt, int gid, GlyphNameProvider &gnp, QWidget *parent) :
    QDialog (parent), m_enc (fnt->enc), m_gid (gid), m_gnp (gnp) {
    int fcnt = 0;
    int row = 0;
    CffTable *cff = dynamic_cast<CffTable *> (fnt->table (CHR ('C','F','F',' ')));
    if (!cff)
	cff = dynamic_cast<CffTable *> (fnt->table (CHR ('C','F','F','2')));
    if (cff)
	fcnt = cff->numSubFonts ();

    GdefTable *gdef = dynamic_cast<GdefTable *> (fnt->table (CHR ('G','D','E','F')));

    setWindowTitle (tr ("Glyph Properties"));

    QVBoxLayout *layout = new QVBoxLayout ();
    QGridLayout *glay = new QGridLayout;
    layout->addLayout (glay);

    glay->addWidget (new QLabel ("Unicode"), row, 0);
    m_uniBox = new QLineEdit ();
    m_uniBox->setValidator (new QRegularExpressionValidator
	(QRegularExpression ("(U\\+[A-Fa-f0-9]{1,6})( U\\+[A-Fa-f0-9]{1,6})*"), this));
    m_uniBox->setText (pack_ucodes (m_enc->unicode (gid)));
    glay->addWidget (m_uniBox, row, 1);
    QPushButton* autoUniBtn = new QPushButton (tr ("Auto"));
    glay->addWidget (autoUniBtn, row++, 2);
    connect (m_uniBox, &QLineEdit::editingFinished, this, &GlyphPropsDialog::updateGlyphName);
    connect (autoUniBtn, &QPushButton::clicked, this, &GlyphPropsDialog::autoGlyphUni);

    QLabel *nameLabel = new QLabel ("Glyph name");
    glay->addWidget (nameLabel, row, 0);
    m_glyphNameField = new QLineEdit ();
    glay->addWidget (m_glyphNameField, row, 1);
    m_glyphNameField->setValidator (new QRegularExpressionValidator (QRegularExpression ("[A-Za-z0-9_.]*"), this));
    m_glyphNameField->setText (QString::fromStdString (m_gnp.nameByGid (static_cast<uint16_t> (gid))));
    QPushButton* autoNameBtn = new QPushButton (tr ("Auto"));
    glay->addWidget (autoNameBtn, row++, 2);
    nameLabel->setEnabled (m_gnp.fontHasGlyphNames ());
    m_glyphNameField->setEnabled (m_gnp.fontHasGlyphNames ());
    autoNameBtn->setEnabled (m_gnp.fontHasGlyphNames ());
    connect (autoNameBtn, &QPushButton::clicked, this, &GlyphPropsDialog::autoGlyphName);

    QLabel *classLabel = new QLabel ("Glyph class");
    glay->addWidget (classLabel, row, 0);
    m_glyphClassBox = new QComboBox ();
    m_glyphClassBox->addItem (tr ("No Class"), GlyphClassDef::Zero);
    m_glyphClassBox->addItem (tr ("Base Glyph"), GlyphClassDef::Base);
    m_glyphClassBox->addItem (tr ("Ligature"), GlyphClassDef::Ligature);
    m_glyphClassBox->addItem (tr ("Mark"), GlyphClassDef::Mark);
    m_glyphClassBox->addItem (tr ("Component"), GlyphClassDef::Component);
    glay->addWidget (m_glyphClassBox, row, 1);
    QPushButton* autoClassBtn = new QPushButton (tr ("Auto"));
    glay->addWidget (autoClassBtn, row++, 2);
    classLabel->setEnabled (gdef != nullptr);
    m_glyphClassBox->setEnabled (gdef != nullptr);
    autoClassBtn->setEnabled (gdef != nullptr);
    if (gdef)
	m_glyphClassBox->setCurrentIndex (m_glyphClassBox->findData (gdef->glyphClass (gid)));
    else
	m_glyphClassBox->setCurrentIndex (0);
    connect (autoClassBtn, &QPushButton::clicked, this, &GlyphPropsDialog::autoGlyphClass);

    QLabel *subLabel = new QLabel ("CFF subfont");
    glay->addWidget (subLabel, row, 0);
    m_subFontBox = new QSpinBox ();
    glay->addWidget (m_subFontBox, row++, 1);

    if (!fcnt) {
	subLabel->setVisible (false);
	m_subFontBox->setVisible (false);
    } else {
	m_subFontBox->setMaximum (fcnt-1);
    }

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

std::vector<uint32_t> GlyphPropsDialog::unicodeList () const {
    return unpack_ucodes (m_uniBox->text ());
}

std::string GlyphPropsDialog::glyphName () const {
    return m_glyphNameField->text ().toStdString ();
}

uint16_t GlyphPropsDialog::glyphClass () const {
    return m_glyphClassBox->currentData ().toUInt ();
}

uint8_t GlyphPropsDialog::subFont () const {
    return m_subFontBox->value ();
}

void GlyphPropsDialog::accept () {
    std::vector<uint32_t> ul = unicodeList ();
    for (uint32_t uni : ul) {
	uint16_t cur_gid = m_enc->gidByUnicode (uni);
	if (cur_gid && cur_gid != m_gid) {
	    FontShepherd::postError (
		QCoreApplication::tr ("Can't set Unicode mapping"),
		QCoreApplication::tr (
		"There is already a glyph mapped to U+%1.")
		    .arg (uni, uni <= 0xFFFF ? 4 : 6, 16, QLatin1Char ('0')),
		this);
	    return;
	}
    }
    QDialog::accept ();
}

void GlyphPropsDialog::updateGlyphName () {
    if (!m_glyphNameField->isEnabled ()) {
	std::vector<uint32_t> ul = unicodeList ();
	if (!ul.empty ())
	    m_glyphNameField->setText (QString::fromStdString (m_gnp.nameByUni (ul[0])));
    }
}

void GlyphPropsDialog::autoGlyphName () {
    std::vector<uint32_t> ul = unicodeList ();
    if (!ul.empty ())
        m_glyphNameField->setText (QString::fromStdString (m_gnp.nameByUni (ul[0])));
}

void GlyphPropsDialog::autoGlyphUni () {
    std::string gn = glyphName ();
    std::vector<uint32_t> ul;
    ul.push_back (m_gnp.uniByName (gn));
    m_uniBox->setText (pack_ucodes (ul));
}

void GlyphPropsDialog::autoGlyphClass () {
    std::string gn = glyphName ();
    std::vector<uint32_t> ul = unicodeList ();
    uint16_t gclass = GlyphClassDef::Zero;
    if (!ul.empty ()) {
	QChar qch = QChar (ul[0]);
	if (ul[0] >= 0xFB00 && ul[0] <= 0xFB06)
	    gclass = GlyphClassDef::Ligature;
	else if (qch.isMark ())
	    gclass = GlyphClassDef::Mark;
	else if (qch.isLetterOrNumber () || qch.isPunct ())
	    gclass = GlyphClassDef::Base;
    // Unencoded glyphs or PUA
    } else if (ul.empty () || (!ul.empty () && (
	(ul[0] >= 0xE000 && ul[0] <= 0xF8FF) ||
        (ul[0] >= 0xF0000 && ul[0] <= 0xFFFFD) ||
        (ul[0] >= 0x100000 && ul[0] <= 0x10FFFD)))) {
	if (gn.find ('_') != std::string::npos)
	    gclass = GlyphClassDef::Ligature;
    }

    m_glyphClassBox->setCurrentIndex (m_glyphClassBox->findData (gclass));
}
