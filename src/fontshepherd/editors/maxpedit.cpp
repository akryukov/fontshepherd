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
#include "editors/maxpedit.h" // also includes tables.h
#include "editors/instredit.h"
#include "tables/maxp.h"
#include "tables/glyphcontainer.h"
#include "tables/instr.h"

#include "fs_notify.h"

MaxpEdit::MaxpEdit (std::shared_ptr<FontTable> tptr, sFont* font, QWidget *parent) :
    TableEdit (parent, Qt::Window), m_font (font) {
    m_maxp = std::dynamic_pointer_cast<MaxpTable> (tptr);

    setAttribute (Qt::WA_DeleteOnClose);
    setWindowTitle (QString ("%1 - %2").arg
	(QString::fromStdString (m_maxp->stringName ())).arg (m_font->fontname));

    QWidget *window = new QWidget (this);

    QVBoxLayout *cont_layout = new QVBoxLayout ();
    QHBoxLayout *version_layout = new QHBoxLayout ();

    version_layout->addWidget (new QLabel (tr ("Table version:")));
    m_versionBox = new QComboBox ();
    m_versionBox->addItem ("0.5: for fonts with PostScript outlines", 0.5);
    m_versionBox->addItem ("1.0: for TrueType fonts", 1.0);
    version_layout->addWidget (m_versionBox);
    m_versionBox->setEnabled (false);
    connect (m_versionBox, static_cast<void (QComboBox::*)(int)> (&QComboBox::currentIndexChanged),
	this, &MaxpEdit::setTableVersion);
    cont_layout->addLayout (version_layout);

    maxp_layout = new QGridLayout ();
    int vidx = 0;

    maxp_layout->addWidget (new QLabel (tr ("Number of glyphs:")), vidx, 0);
    m_numGlyphsBox = new QSpinBox ();
    m_numGlyphsBox->setMinimum (1);
    m_numGlyphsBox->setMaximum (0xFFFF);
    maxp_layout->addWidget (m_numGlyphsBox, vidx++, 1);

    maxp_layout->addWidget (new QLabel (tr ("Maximum points in a non-composite glyph:")), vidx, 0);
    m_maxPointsBox = new QSpinBox ();
    m_maxPointsBox->setMinimum (0);
    m_maxPointsBox->setMaximum (0xFFFF);
    maxp_layout->addWidget (m_maxPointsBox, vidx++, 1);

    maxp_layout->addWidget (new QLabel (tr ("Maximum contours in a non-composite glyph:")), vidx, 0);
    m_maxContoursBox = new QSpinBox ();
    m_maxContoursBox->setMinimum (0);
    m_maxContoursBox->setMaximum (0xFFFF);
    maxp_layout->addWidget (m_maxContoursBox, vidx++, 1);

    maxp_layout->addWidget (new QLabel (tr ("Maximum points in a composite glyph:")), vidx, 0);
    m_maxCompositePointsBox = new QSpinBox ();
    m_maxCompositePointsBox->setMinimum (0);
    m_maxCompositePointsBox->setMaximum (0xFFFF);
    maxp_layout->addWidget (m_maxCompositePointsBox, vidx++, 1);

    maxp_layout->addWidget (new QLabel (tr ("Maximum contours in a composite glyph:")), vidx, 0);
    m_maxCompositeContoursBox = new QSpinBox ();
    m_maxCompositeContoursBox->setMinimum (0);
    m_maxCompositeContoursBox->setMaximum (0xFFFF);
    maxp_layout->addWidget (m_maxCompositeContoursBox, vidx++, 1);

    maxp_layout->addWidget (new QLabel (tr ("Maximum number of zones:")), vidx, 0);
    m_maxZonesBox = new QSpinBox ();
    m_maxZonesBox->setMinimum (1);
    m_maxZonesBox->setMaximum (2);
    maxp_layout->addWidget (m_maxZonesBox, vidx++, 1);

    maxp_layout->addWidget (new QLabel (tr ("Maximum points used in z0:")), vidx, 0);
    m_maxTwilightBox = new QSpinBox ();
    m_maxTwilightBox->setMinimum (0);
    m_maxTwilightBox->setMaximum (0xFFFF);
    maxp_layout->addWidget (m_maxTwilightBox, vidx++, 1);

    maxp_layout->addWidget (new QLabel (tr ("Number of Storage Area locations:")), vidx, 0);
    m_maxStorageBox = new QSpinBox ();
    m_maxStorageBox->setMinimum (0);
    m_maxStorageBox->setMaximum (0xFFFF);
    maxp_layout->addWidget (m_maxStorageBox, vidx++, 1);

    maxp_layout->addWidget (new QLabel (tr ("Number of function defs:")), vidx, 0);
    m_maxFunctionDefsBox = new QSpinBox ();
    m_maxFunctionDefsBox->setMinimum (0);
    m_maxFunctionDefsBox->setMaximum (0xFFFF);
    maxp_layout->addWidget (m_maxFunctionDefsBox, vidx++, 1);

    maxp_layout->addWidget (new QLabel (tr ("Number of instruction defs:")), vidx, 0);
    m_maxInstructionDefsBox = new QSpinBox ();
    m_maxInstructionDefsBox->setMinimum (0);
    m_maxInstructionDefsBox->setMaximum (0xFFFF);
    maxp_layout->addWidget (m_maxInstructionDefsBox, vidx++, 1);

    maxp_layout->addWidget (new QLabel (tr ("Maximum stack depth:")), vidx, 0);
    m_maxStackElementsBox = new QSpinBox ();
    m_maxStackElementsBox->setMinimum (0);
    m_maxStackElementsBox->setMaximum (0xFFFF);
    maxp_layout->addWidget (m_maxStackElementsBox, vidx++, 1);

    maxp_layout->addWidget (new QLabel (tr ("Maximum glyph instructions size (in bytes):")), vidx, 0);
    m_maxSizeOfInstructionsBox = new QSpinBox ();
    m_maxSizeOfInstructionsBox->setMinimum (0);
    m_maxSizeOfInstructionsBox->setMaximum (0xFFFF);
    maxp_layout->addWidget (m_maxSizeOfInstructionsBox, vidx++, 1);

    maxp_layout->addWidget (new QLabel (tr ("Maximum number of components referenced:")), vidx, 0);
    m_maxComponentElementsBox = new QSpinBox ();
    m_maxComponentElementsBox->setMinimum (0);
    m_maxComponentElementsBox->setMaximum (0xFFFF);
    maxp_layout->addWidget (m_maxComponentElementsBox, vidx++, 1);

    maxp_layout->addWidget (new QLabel (tr ("Maximum levels of recursion:")), vidx, 0);
    m_maxComponentDepthBox = new QSpinBox ();
    m_maxComponentDepthBox->setMinimum (0);
    m_maxComponentDepthBox->setMaximum (0xFFFF);
    maxp_layout->addWidget (m_maxComponentDepthBox, vidx++, 1);

    cont_layout->addLayout (maxp_layout);

    saveButton = new QPushButton (tr ("&Compile table"));
    calcButton = new QPushButton (tr ("C&alculate"));
    closeButton = new QPushButton (tr ("C&lose"));

    QHBoxLayout *buttLayout = new QHBoxLayout ();
    buttLayout->addWidget (saveButton);
    buttLayout->addWidget (calcButton);
    buttLayout->addWidget (closeButton);
    cont_layout->addLayout (buttLayout);

    connect (saveButton, &QPushButton::clicked, this, &MaxpEdit::save);
    connect (calcButton, &QPushButton::clicked, this, &MaxpEdit::calculate);
    connect (closeButton, &QPushButton::clicked, this, &MaxpEdit::close);

    window->setLayout (cont_layout);
    setCentralWidget (window);
    fillControls (m_maxp->contents);

    m_valid = true;
}

void MaxpEdit::fillControls (maxp_data &d) {
    m_versionBox->setCurrentIndex
	(m_versionBox->findData (m_maxp->version (), Qt::UserRole));
    m_numGlyphsBox->setValue (d.numGlyphs);

    if (m_maxp->version () >= 1) {
	m_maxPointsBox->setValue (d.maxPoints);
	m_maxContoursBox->setValue (d.maxContours);
	m_maxCompositePointsBox->setValue (d.maxCompositePoints);
	m_maxCompositeContoursBox->setValue (d.maxCompositeContours);
	m_maxZonesBox->setValue (d.maxZones);
	m_maxTwilightBox->setValue (d.maxTwilightPoints);
	m_maxStorageBox->setValue (d.maxStorage);
	m_maxFunctionDefsBox->setValue (d.maxFunctionDefs);
	m_maxInstructionDefsBox->setValue (d.maxInstructionDefs);
	m_maxStackElementsBox->setValue (d.maxStackElements);
	m_maxSizeOfInstructionsBox->setValue (d.maxSizeOfInstructions);
	m_maxComponentElementsBox->setValue (d.maxComponentElements);
	m_maxComponentDepthBox->setValue (d.maxComponentDepth);
    } else {
	int nrows = maxp_layout->rowCount ();
	for (int i=1; i<nrows; i++) {
	    maxp_layout->itemAtPosition (i, 0)->widget ()->setVisible (false);
	    maxp_layout->itemAtPosition (i, 1)->widget ()->setVisible (false);
	}
    }
}

bool MaxpEdit::checkUpdate (bool) {
    return true;
}

bool MaxpEdit::isModified () {
    return m_maxp->modified ();
}

bool MaxpEdit::isValid () {
    return m_valid;
}

std::shared_ptr<FontTable> MaxpEdit::table () {
    return m_maxp;
}

void MaxpEdit::closeEvent (QCloseEvent *event) {
    // If we are going to delete the font, ignore changes in table edits
    if (!isModified () || checkUpdate (true))
        m_maxp->clearEditor ();
    else
        event->ignore ();
}

void MaxpEdit::save () {
    maxp_data &md = m_maxp->contents;

    md.version = m_versionBox->currentData (Qt::UserRole).toDouble ();

    md.numGlyphs = m_numGlyphsBox->value ();
    if (md.version >= 1) {
	md.maxPoints = m_maxPointsBox->value ();
	md.maxContours = m_maxContoursBox->value ();
	md.maxCompositePoints = m_maxCompositePointsBox->value ();
	md.maxCompositeContours = m_maxCompositeContoursBox->value ();
	md.maxZones = m_maxZonesBox->value ();
	md.maxTwilightPoints = m_maxTwilightBox->value ();
	md.maxStorage = m_maxStorageBox->value ();
	md.maxFunctionDefs = m_maxFunctionDefsBox->value ();
	md.maxInstructionDefs = m_maxInstructionDefsBox->value ();
	md.maxStackElements = m_maxStackElementsBox->value ();
	md.maxSizeOfInstructions = m_maxSizeOfInstructionsBox->value ();
	md.maxComponentElements = m_maxComponentElementsBox->value ();
	md.maxComponentDepth = m_maxComponentDepthBox->value ();
    }

    m_maxp->packData ();
    emit (update (m_maxp));
    close ();
}

void MaxpEdit::calculate () {
    GlyphContainer *glyf = dynamic_cast<GlyphContainer*> (m_font->table (CHR ('g','l','y','f')));
    GlyphContainer *cff = dynamic_cast<GlyphContainer*> (m_font->table (CHR ('C','F','F',' ')));
    GlyphContainer *cff2 = dynamic_cast<GlyphContainer*> (m_font->table (CHR ('C','F','F','2')));
    if (glyf)
	calculateTTF (glyf);
    else if (cff)
	calculateCFF (cff);
    else if (cff2)
	calculateCFF (cff2);
    else
        FontShepherd::postError (
            QCoreApplication::tr ("'maxp' table error"),
            QCoreApplication::tr ("This font has neither 'glyf' nor 'CFF' or 'CFF2' tables. "
		"Don't know how to calculate 'maxp' table contents"),
            nullptr);
}

void MaxpEdit::calculateCFF (GlyphContainer *cff) {
    m_versionBox->setCurrentIndex (m_versionBox->findData (0.5, Qt::UserRole));
    cff->fillup ();
    cff->unpackData (m_font);
    uint16_t gcnt = cff->countGlyphs ();
    m_numGlyphsBox->setValue (gcnt);
}

void MaxpEdit::calculateTTF (GlyphContainer *glyf) {
    InstrTable *fpgm = dynamic_cast<InstrTable*> (m_font->table (CHR ('f','p','g','m')));
    InstrTable *prep = dynamic_cast<InstrTable*> (m_font->table (CHR ('p','r','e','p')));
    FontTable *cvt = m_font->table (CHR ('c','v','t',' '));

    maxp_data d;

    instr_props props;
    props.maxTwilight = 0;
    props.maxStackDepth = 0;
    props.maxStorage = 0;
    props.numIdefs = 0;
    props.z0used = false;
    props.rBearingTouched = false;

    GraphicsState state;
    state.size = 24;
    state.upm = m_font->units_per_em;

    m_versionBox->setCurrentIndex (m_versionBox->findData (1.0, Qt::UserRole));
    if (fpgm) {
	fpgm->fillup ();
	std::vector<uint8_t> vdata (reinterpret_cast<uint8_t *> (fpgm->getData ()),
	    reinterpret_cast<uint8_t *> (fpgm->getData ()) + fpgm->length ());
	InstrEdit::quickExecute (vdata, state, props);
	InstrEdit::reportError (state, CHR ('f','p','g','m'), 0xFFFF);
    }
    if (cvt) {
	cvt->fillup ();
	state.cvt.reserve (cvt->dataLength ()/2);
	uint32_t pos = 0;
	while (pos < cvt->dataLength ()) {
	    int16_t cvt_val = cvt->getushort (pos);
	    pos += 2;
	    state.cvt.push_back (std::lround (static_cast<double> (cvt_val) * state.size / state.upm * 64));
	}
    }
    if (prep) {
	prep->fillup ();
        std::vector<uint8_t> vdata (reinterpret_cast<uint8_t *> (prep->getData ()),
	    reinterpret_cast<uint8_t *> (prep->getData ()) + prep->length ());
        InstrEdit::quickExecute (vdata, state, props);
	InstrEdit::reportError (state, CHR ('p','r','e','p'), 0xFFFF);
    }

    glyf->fillup ();
    glyf->unpackData (m_font);
    uint16_t gcnt = glyf->countGlyphs ();

    // probably there is no reason in attempting to execute glyph program, if there was
    // an error reading 'prep'
    if (!state.errorCode) {
	QProgressDialog progress (
	    tr ("Executing glyph programs"), tr ("Abort"), 0, gcnt, this);
	progress.setWindowModality (Qt::WindowModal);
	progress.show ();

	for (size_t i=0; i<gcnt; i++) {
	    ConicGlyph *g = glyf->glyph (m_font, i);

	    qApp->instance ()->processEvents ();
	    if (progress.wasCanceled ())
		break;
	    progress.setValue (i);

	    uint16_t pcnt, numcnt;
	    std::vector<uint16_t> refs = g->refersTo ();
	    if (refs.empty ()) {
		pcnt = g->numCompositePoints ();
		numcnt = g->numCompositeContours ();

		if ((pcnt) > d.maxPoints)
		    d.maxPoints = pcnt;
		if ((numcnt) > d.maxContours)
		    d.maxContours = numcnt;
	    } else {
		// cf. GlyphContext::resolveRefs, but here is a simpler version,
		// as only TrueType glyphs are involved
		g->provideRefGlyphs (m_font, glyf);
		if (g->checkRefs (g->gid (), gcnt) != 0) continue;
		// essentially not needed, but let it be here for consistence
		g->finalizeRefs ();

		pcnt = g->numCompositePoints ();
		numcnt = g->numCompositeContours ();
		uint16_t cd = g->componentDepth ();

		if ((pcnt) > d.maxCompositePoints)
		    d.maxCompositePoints = pcnt;
		if ((numcnt) > d.maxCompositeContours)
		    d.maxCompositeContours = numcnt;
		if (refs.size () > d.maxComponentElements)
		    d.maxComponentElements = refs.size ();
		if (cd > d.maxComponentDepth)
		    d.maxComponentDepth = cd;
	    }
	    if (g->instructions.empty ()) continue;

	    if (g->instructions.size () > d.maxSizeOfInstructions)
		d.maxSizeOfInstructions = g->instructions.size ();

	    props.rBearingPointNum = pcnt+1;
	    props.rBearingTouched = false;

	    GraphicsState gstate = state;
	    gstate.g = g;
	    for (size_t j=0; j<3; j++) {
		gstate.zp[j] = 1;
		gstate.rp[j] = 0;
	    }

	    InstrEdit::quickExecute (g->instructions, gstate, props);
	    InstrEdit::reportError (gstate, CHR ('g','l','y','f'), i);
	    if (gstate.twilightPts.size () > props.maxTwilight)
		props.maxTwilight = gstate.twilightPts.size ();
	    if (gstate.storage.size () > props.maxStorage)
		props.maxStorage = gstate.storage.size ();
	}
	progress.setValue (gcnt);
    }

    d.numGlyphs = gcnt;
    d.maxZones = props.z0used ? 2 : 1;
    d.maxTwilightPoints = props.maxTwilight;
    d.maxStorage = props.maxStorage;
    d.maxFunctionDefs = props.fdefs.size ();
    d.maxInstructionDefs = props.numIdefs;
    d.maxStackElements = props.maxStackDepth;
    fillControls (d);
}

void MaxpEdit::setTableVersion (int idx) {
    double newver = m_versionBox->itemData (idx, Qt::UserRole).toFloat ();
    bool full = newver >= 1;
    int nrows = maxp_layout->rowCount ();
    for (int i=1; i<nrows; i++) {
	maxp_layout->itemAtPosition (i, 0)->widget ()->setVisible (full);
	maxp_layout->itemAtPosition (i, 1)->widget ()->setVisible (full);
    }
    adjustSize ();
}

