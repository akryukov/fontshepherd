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

#include <assert.h>
#include <iostream>
#include <sstream>
#include <ios>

#include "sfnt.h"
#include "fs_notify.h"
#include "tables.h"
#include "tables/gdef.h"

void OpenType::readClassDefTable (char *data, uint16_t pos, std::vector<uint16_t> &glyph_list) {
    uint16_t format = FontTable::getushort (data, pos); pos+=2;

    if (format == 1) {
	uint16_t startGlyphID = FontTable::getushort (data, pos); pos+=2;
	uint16_t glyphCount = FontTable::getushort (data, pos); pos+=2;
	if (startGlyphID + glyphCount > glyph_list.size ()) {
            FontShepherd::postError (
                QCoreApplication::tr ("Error reading table"),
                QCoreApplication::tr (
		    "Glyph count exceeded: glyph %1 referred, "
		    "while only %2 glyphs are present in the font")
		    .arg (startGlyphID + glyphCount)
		    .arg (glyph_list.size ()),
                nullptr);
	    glyphCount = glyph_list.size () - startGlyphID;
	}
	for (size_t i=startGlyphID; i<startGlyphID+glyphCount; i++) {
	    glyph_list[i] = FontTable::getushort (data, pos); pos+=2;
	}
    } else if (format == 2) {
	uint16_t classRangeCount = FontTable::getushort (data, pos); pos+=2;
	for (size_t i=0; i<classRangeCount; i++) {
	    uint16_t startGlyphID = FontTable::getushort (data, pos); pos+=2;
	    uint16_t endGlyphID = FontTable::getushort (data, pos); pos+=2;
	    uint16_t glyphClass = FontTable::getushort (data, pos); pos+=2;
	    if (endGlyphID > glyph_list.size ()) {
		FontShepherd::postError (
		    QCoreApplication::tr ("Error reading Class table"),
		    QCoreApplication::tr (
			"Glyph count exceeded: glyph %1 referred, "
			"while only %2 glyphs are present in the font")
			.arg (endGlyphID)
			.arg (glyph_list.size ()),
		    nullptr);
		break;
	    }
	    for (size_t j=startGlyphID; j<=endGlyphID; j++)
		glyph_list[j] = glyphClass;
	}
    }
}

void OpenType::readCoverageTable (char *data, uint16_t pos, std::vector<uint16_t> &glyph_list) {
    uint16_t format = FontTable::getushort (data, pos); pos+=2;
    uint16_t cnt = FontTable::getushort (data, pos); pos+=2;
    glyph_list.resize (cnt, 0);

    if (format == 1) {
	for (size_t i=0; i<cnt; i++) {
	    glyph_list[i] = FontTable::getushort (data, pos); pos+=2;
	}
    } else if (format == 2) {
	uint16_t range_cnt = FontTable::getushort (data, pos); pos+=2;
	for (size_t i=0; i<range_cnt; i++) {
	    uint16_t start_gid = FontTable::getushort (data, pos); pos+=2;
	    uint16_t end_gid = FontTable::getushort (data, pos); pos+=2;
	    uint16_t start_cov = FontTable::getushort (data, pos); pos+=2;
	    for (uint16_t j=start_cov, gid=start_gid; gid<=end_gid; j++, gid++)
		glyph_list[j] = gid;
	}
    }
}

void OpenType::readDeviceTable (char *data, uint16_t pos, device_table &dtab) {
    dtab.startSize = FontTable::getushort (data, pos); pos+=2;
    dtab.endSize = FontTable::getushort (data, pos); pos+=2;
    dtab.deltaFormat = FontTable::getushort (data, pos); pos+=2;

    if (dtab.deltaFormat != DeltaFormat::VARIATION_INDEX) {
	int size_cnt = dtab.endSize - dtab.startSize + 1;
	dtab.deltaValues.resize (size_cnt);
	int div = dtab.deltaFormat == DeltaFormat::LOCAL_2_BIT_DELTAS ? 2 :
	    dtab.deltaFormat == DeltaFormat::LOCAL_4_BIT_DELTAS ? 4 : 8;
	int num_bytes = (size_cnt/div + 1) & ~1;
	int cur = 0;
	for (int i=0; i<num_bytes/2; i++) {
	    uint16_t dset = FontTable::getushort (data, pos); pos+=2;
	    for (int j=0; j<16/div && cur<size_cnt; j++, cur++) {
		uint16_t mask = ((1 << div) - 1) << j*div;
		dtab.deltaValues[cur] = (dset & mask) >> j*div;
	    }
	}
    }
}

GdefTable::GdefTable (sfntFile *fontfile, TableHeader &props) :
    FontTable (fontfile, props) {
}

void GdefTable::unpackData (sFont *fnt) {
    uint32_t pos = 0;
    this->fillup ();

    m_version = getversion (pos); pos+=4;

    glyphClassDefOffset = getushort (pos); pos +=2;
    attachListOffset = getushort (pos); pos +=2;
    ligCaretListOffset = getushort (pos); pos +=2;
    markAttachClassDefOffset = getushort (pos); pos +=2;
    if (m_version >= 1.2) {
	markGlyphSetsDefOffset = getushort (pos); pos +=2;
    }
    if (m_version >= 1.3) {
	itemVarStoreOffset = getushort (pos); pos +=2;
    }

    m_glyphClasses.resize (fnt->glyph_cnt, 0);
    m_attachClasses.resize (fnt->glyph_cnt, 0);
    if (glyphClassDefOffset)
	OpenType::readClassDefTable (data, glyphClassDefOffset, m_glyphClasses);
    if (attachListOffset)
	readAttachList ();
    if (ligCaretListOffset)
	readLigCaretList ();
    if (markAttachClassDefOffset)
	OpenType::readClassDefTable (data, markAttachClassDefOffset, m_attachClasses);
    if (markGlyphSetsDefOffset)
	readMarkGlyphSets ();
    if (itemVarStoreOffset)
	FontVariations::readVariationStore (data, itemVarStoreOffset, m_varStore);
}

void GdefTable::packData () {
}

double GdefTable::version () const {
    return m_version;
}

void GdefTable::readAttachList () {
    uint32_t pos = attachListOffset;
    uint16_t coverageOffset = getushort (pos); pos +=2;
    uint16_t glyphCount = getushort (pos); pos +=2;

    std::vector<uint16_t> glyph_list;
    OpenType::readCoverageTable (data, attachListOffset+coverageOffset, glyph_list);

    if (glyphCount != glyph_list.size ()) {
        FontShepherd::postError (
            QCoreApplication::tr ("Error reading table"),
            QCoreApplication::tr (
		"Glyph count mismatch: %1 glyphs in the coverage table, "
		"while %2 glyphs are expected")
		.arg (glyph_list.size ())
		.arg (glyphCount),
            container->parent ());
	return;
    }

    for (size_t i=0; i<glyphCount; i++) {
	std::vector<uint16_t> pt_ids;
	uint16_t pointCount = getushort (pos); pos +=2;
	pt_ids.resize (pointCount, 0);
	for (size_t j=0; i<pointCount; j++) {
	    pt_ids[j] = getushort (pos); pos +=2;
	}
	m_attachList[glyph_list[i]] = pt_ids;
    }
}

void GdefTable::readLigCaretList () {
    uint32_t pos = ligCaretListOffset;
    uint16_t coverageOffset = getushort (pos); pos +=2;
    uint16_t ligGlyphCount = getushort (pos); pos +=2;

    std::vector<uint16_t> glyph_list;
    OpenType::readCoverageTable (data, ligCaretListOffset+coverageOffset, glyph_list);

    if (ligGlyphCount != glyph_list.size ()) {
        FontShepherd::postError (
            QCoreApplication::tr ("Error reading LigCaret table"),
            QCoreApplication::tr (
		"Glyph count mismatch: %1 glyphs in the coverage table, "
		"while %2 glyphs are expected")
		.arg (glyph_list.size ())
		.arg (ligGlyphCount),
            container->parent ());
	return;
    }

    std::vector<uint16_t> off_list;
    off_list.resize (ligGlyphCount);
    for (size_t i=0; i<ligGlyphCount; i++) {
	off_list[i] = getushort (pos) + ligCaretListOffset; pos +=2;
    }

    for (size_t i=0; i<ligGlyphCount; i++) {
	pos = off_list[i];
	auto &clist = m_ligCaretList[glyph_list[i]];
	uint16_t caretCount = getushort (pos); pos +=2;
	std::vector<uint16_t> caret_off_list;
	caret_off_list.resize (ligGlyphCount);
	clist.resize (caretCount);

	for (size_t j=0; j<caretCount; j++) {
	    caret_off_list[j] = getushort (pos) + off_list[i]; pos +=2;
	}

	for (size_t j=0; j<caretCount; j++) {
	    caret_value &cval = clist[j];
	    pos = caret_off_list[j];
	    uint16_t caret_format = getushort (pos); pos +=2;
	    cval.format = caret_format;
	    switch (caret_format) {
	      case 1:
		cval.coord = getushort (pos); pos +=2;
		break;
	      case 2:
		cval.point_index = getushort (pos); pos +=2;
		break;
	      case 3:
		cval.coord = getushort (pos); pos +=2;
		cval.table_off = getushort (pos); pos +=2;
		OpenType::readDeviceTable (data, off_list[j]+cval.table_off, cval.dev_table);
	    }
	}
    }
}

void GdefTable::readMarkGlyphSets () {
    uint32_t pos = markGlyphSetsDefOffset;
    /*uint16_t format =*/ getushort (pos); pos +=2;
    uint16_t markGlyphSetCount = getushort (pos); pos +=2;
    std::vector<uint32_t> offsets;
    offsets.resize (markGlyphSetCount, 0);
    m_markGlyphSets.reserve (markGlyphSetCount);
    for (size_t i=0; i<markGlyphSetCount; i++) {
	offsets[i] = getlong (pos); pos+= 4;
    }

    for (size_t i=0; i<markGlyphSetCount; i++) {
	std::vector<uint16_t> glyph_list;
	OpenType::readCoverageTable (data, markGlyphSetsDefOffset+offsets[i], glyph_list);
	m_markGlyphSets.push_back (glyph_list);
    }
}
