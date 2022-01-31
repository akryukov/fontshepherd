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

#include <cstring>
#include <sstream>
#include <ios>
#include <assert.h>
#include <exception>

#include "sfnt.h"
#include "editors/cpaledit.h"
#include "editors/fontview.h"
#include "tables/colr.h"
#include "tables/name.h"

ColrTable::ColrTable (sfntFile *fontfile, TableHeader &props) :
    FontTable (fontfile, props) {
}

ColrTable::~ColrTable () {
}

void ColrTable::unpackData (sFont *) {
    uint32_t pos = 0;
    uint16_t i;
    std::vector<layer_record> layer_records;

    m_version = getushort (0);
    uint16_t numBaseGlyphRecords = getushort (2);
    uint32_t offsetBaseGlyphRecord = getlong (4);
    uint32_t offsetLayerRecord = getlong (8);
    uint16_t numLayerRecords = getushort (12);

    pos = offsetBaseGlyphRecord;
    m_baseGlyphRecords.resize (numBaseGlyphRecords);
    for (i=0; i<numBaseGlyphRecords; i++) {
        m_baseGlyphRecords[i].GID = getushort (pos); pos+=2;
        m_baseGlyphRecords[i].firstLayerIndex = getushort (pos); pos+=2;
        m_baseGlyphRecords[i].numLayers = getushort (pos); pos+=2;
    }

    pos = offsetLayerRecord;
    layer_records.resize (numLayerRecords);
    for (i=0; i<numLayerRecords; i++) {
        layer_records[i].GID = getushort (pos); pos+=2;
        layer_records[i].paletteIndex = getushort (pos); pos+=2;
    }

    for (i=0; i<numBaseGlyphRecords; i++) {
	struct base_glyph_record &bgr = m_baseGlyphRecords[i];
	bgr.layers.reserve (bgr.numLayers);
	for (uint16_t j=bgr.firstLayerIndex; j<bgr.firstLayerIndex+bgr.numLayers; j++)
	    bgr.layers.push_back (layer_records[j]);
    }
}

uint16_t ColrTable::numGlyphLayers (uint16_t gid) {
    for (uint16_t i=0; i<m_baseGlyphRecords.size (); i++) {
        if (m_baseGlyphRecords[i].GID == gid)
	    return m_baseGlyphRecords[i].numLayers;
    }
    return 0;
}

std::vector<struct layer_record> &ColrTable::glyphLayers (uint16_t gid) {
    for (uint16_t i=0; i<m_baseGlyphRecords.size (); i++) {
        if (m_baseGlyphRecords[i].GID == gid)
	    return m_baseGlyphRecords[i].layers;
    }
    throw std::exception ();
}

// NB: same function as for GlyphContainer
void ColrTable::edit (sFont* fnt, QWidget* caller) {
    // No fillup here, as it is done by fontview
    if (!tv) {
        FontView *fv = new FontView (this, fnt, caller);
        if (!fv->isValid ()) {
            fv->close ();
            return;
        }
        tv = fv;
        fv->show ();
    } else {
        tv->raise ();
    }
}

CpalTable::CpalTable (sfntFile *fontfile, TableHeader &props) :
    FontTable (fontfile, props) {
}

CpalTable::~CpalTable () {
}

void CpalTable::edit (sFont* fnt, QWidget* caller) {
    if (data == nullptr)
        fillup ();

    if (tv == nullptr) {
	unpackData (fnt);
        CpalEdit *cpaledit = new CpalEdit (this, fnt, caller);
        tv = cpaledit;
        cpaledit->show ();
    } else {
        tv->raise ();
    }
}

void CpalTable::unpackData (sFont *) {
    uint32_t pos = 0;
    uint16_t i;
    std::vector<uint16_t> firstColorIndices;
    std::vector<rgba_color> colorRecords;
    uint32_t offsetPaletteTypeArray, offsetPaletteLabelArray, offsetPaletteEntryLabelArray;

    m_paletteLabelIndices.resize (0);
    m_paletteList.resize (0);

    m_version = getushort (0);
    m_numPaletteEntries = getushort (2);
    uint16_t numPalettes = getushort (4);
    uint16_t numColorRecords = getushort (6);
    uint32_t offsetFirstColorRecord = getlong (8);

    pos = 12;
    m_paletteList.reserve (numPalettes);
    firstColorIndices.resize (numPalettes);

    for (i=0; i<numPalettes; i++) {
        firstColorIndices[i] = getushort (pos);
        pos += 2;
    }
    if (m_version > 0) {
	offsetPaletteTypeArray = getlong (pos); pos+= 4;
	offsetPaletteLabelArray = getlong (pos); pos+= 4;
	offsetPaletteEntryLabelArray = getlong (pos); pos+= 4;
    }

    pos = offsetFirstColorRecord;
    colorRecords.resize (numColorRecords);
    for (i=0; i<numColorRecords; i++) {
        colorRecords[i].blue = data[pos]; pos++;
        colorRecords[i].green = data[pos]; pos++;
        colorRecords[i].red = data[pos]; pos++;
        colorRecords[i].alpha = data[pos]; pos++;
    }

    m_paletteLabelIndices.resize (m_numPaletteEntries);
    for (i=0; i<m_numPaletteEntries; i++)
	m_paletteLabelIndices[i] = 0xFFFF;

    for (i=0; i<numPalettes; i++) {
	auto pal = std::unique_ptr<cpal_palette> (new cpal_palette ());
	pal->color_records.resize (m_numPaletteEntries);
	uint16_t color_idx=firstColorIndices[i];
	for (uint16_t j=0; j<m_numPaletteEntries; j++) {
	    pal->color_records[j] = colorRecords[color_idx];
	    color_idx++;
	}
	m_paletteList.push_back (std::move (pal));
    }

    if (m_version > 0) {
	if (offsetPaletteTypeArray) {
	    pos = offsetPaletteTypeArray;
	    for (i=0; i<numPalettes; i++) {
		m_paletteList[i]->flags = getlong (pos);
		pos+= 4;
	    }
	}
	if (offsetPaletteLabelArray) {
	    pos = offsetPaletteLabelArray;
	    for (i=0; i<numPalettes; i++) {
		m_paletteList[i]->label_idx = getushort (pos);
		pos+= 2;
	    }
	}
	if (offsetPaletteLabelArray) {
	    pos = offsetPaletteEntryLabelArray;
	    for (i=0; i<m_numPaletteEntries; i++) {
		m_paletteLabelIndices[i] = getushort (pos);
		pos+= 2;
	    }
	}
    }
}

void CpalTable::packData () {
    std::ostringstream os;
    uint32_t cr_off, type_off, plbl_off, elbl_off;

    delete[] data; data = nullptr;
    putushort (os, m_version);
    putushort (os, m_numPaletteEntries);
    putushort (os, m_paletteList.size ());
    putushort (os, m_paletteList.size ()*m_numPaletteEntries);
    putlong (os, 0); // offsetFirstColorRecord
    for (uint16_t i=0; i<m_paletteList.size (); i++)
	putushort (os, i*m_numPaletteEntries);
    if (m_version > 0) {
	putlong (os, 0); // offsetPaletteTypeArray
	putlong (os, 0); // offsetPaletteLabelArray
	putlong (os, 0); // offsetPaletteEntryLabelArray
    }
    cr_off = os.tellp ();
    os.seekp (8);
    putlong (os, cr_off);
    os.seekp (cr_off);
    for (size_t i=0; i<m_paletteList.size (); i++) {
	for (uint16_t j=0; j<m_numPaletteEntries; j++) {
	    rgba_color &rec = m_paletteList[i]->color_records[j];
	    os.put (rec.blue);
	    os.put (rec.green);
	    os.put (rec.red);
	    os.put (rec.alpha);
	}
    }
    if (m_version > 0) {
	type_off = os.tellp ();
	for (size_t i=0; i<m_paletteList.size (); i++)
	    putlong (os, m_paletteList[i]->flags.to_ulong ());
	plbl_off = os.tellp ();
	for (size_t i=0; i<m_paletteList.size (); i++)
	    putushort (os, m_paletteList[i]->label_idx);
	elbl_off = os.tellp ();
	for (size_t i=0; i<m_numPaletteEntries; i++)
	    putushort (os, m_paletteLabelIndices[i]);
	os.seekp (cr_off-12);
	putlong (os, type_off);
	putlong (os, plbl_off);
	putlong (os, elbl_off);
    }

    changed = false;
    td_changed = true;
    start = 0xffffffff;

    std::string st = os.str ();
    newlen = st.length ();
    data = new char[newlen];
    std::copy (st.begin (), st.end (), data);
}

uint16_t CpalTable::version () const {
    return m_version;
}

uint16_t CpalTable::numPalettes () const {
    return m_paletteList.size ();
}

void CpalTable::setNumPalettes (uint16_t val) {
    if (val < m_paletteList.size ())
	m_paletteList.resize (val);
    else {
	uint16_t old_cnt = m_paletteList.size ();
        for (uint16_t i = old_cnt; i<val; i++) {
	    auto pal = std::unique_ptr<cpal_palette> (new cpal_palette ());
	    pal->color_records.resize (m_numPaletteEntries);
	    m_paletteList.push_back (std::move (pal));
        }
    }
}

uint16_t CpalTable::numPaletteEntries () const {
    return m_numPaletteEntries;
}

uint16_t CpalTable::paletteNameID (uint16_t idx) const {
    if (idx < m_paletteList.size ())
	return m_paletteList[idx]->label_idx;
    return 0xFFFF;
}

uint16_t CpalTable::colorNameID (uint16_t idx) const {
    if (idx < m_numPaletteEntries)
	return m_paletteLabelIndices[idx];
    return 0xFFFF;
}

struct cpal_palette *CpalTable::palette (uint16_t idx) {
    if (idx < m_paletteList.size ())
	return m_paletteList[idx].get ();
    return nullptr;
}

QStringList CpalTable::paletteList (NameTable *name) {
    QStringList ret;
    ret.reserve (m_paletteList.size ());
    for (uint16_t i=0; i<m_paletteList.size (); i++) {
	QString user_name = "Palette";
	if (m_version > 0 && name)
	    user_name = name->bestName (m_paletteList[i]->label_idx, "Palette");
	ret.push_back (QString (
	    QApplication::tr ("%1: %2")).arg (i).arg (user_name));
    }
    return ret;
}

