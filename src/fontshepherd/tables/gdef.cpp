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

void OpenType::writeClassDefTable (QBuffer &, QDataStream &os, std::vector<uint16_t> &glyph_list) {
    std::vector<struct class_range> ranges;
    uint16_t first_idx = 0, last_idx = glyph_list.size () - 1, idx;

    while (glyph_list[first_idx] == 0 && first_idx < glyph_list.size ()) first_idx++;
    if (first_idx == glyph_list.size ())
	return;
    while (glyph_list[last_idx] == 0 && last_idx >= first_idx) last_idx--;
    ranges.push_back (class_range { first_idx, first_idx, glyph_list[first_idx] });

    for (idx = first_idx+1; idx <= last_idx; idx++) {
	struct class_range &last = ranges.back ();
	if (glyph_list[idx] == last.glyphClass && idx == last.endGlyphID + 1) {
	    last.endGlyphID++;
	} else if (glyph_list[idx] != 0) {
	    ranges.push_back ({ idx, idx, glyph_list[idx] });
	}
    }

    size_t v1_len = (last_idx - first_idx + 1) * 2 + 4;
    if (ranges.size () * 6 > v1_len) {
	os << (uint16_t) 1;
	os << first_idx;
	for (idx=first_idx; idx<=last_idx; idx++)
	    os << glyph_list[idx];
    } else {
	os << (uint16_t) 2;
	os << (uint16_t) ranges.size ();
	for (auto &rng : ranges) {
	    os << rng.startGlyphID;
	    os << rng.endGlyphID;
	    os << rng.glyphClass;
	}
    }
}

void OpenType::readCoverageTable (char *data, uint16_t pos, std::vector<uint16_t> &glyph_list) {
    uint16_t format = FontTable::getushort (data, pos); pos+=2;
    uint16_t cnt = FontTable::getushort (data, pos); pos+=2;

    if (format == 1) {
	glyph_list.resize (cnt, 0);
	for (size_t i=0; i<cnt; i++) {
	    glyph_list[i] = FontTable::getushort (data, pos); pos+=2;
	}
    } else if (format == 2) {
	for (size_t i=0; i<cnt; i++) {
	    uint16_t start_gid = FontTable::getushort (data, pos); pos+=2;
	    uint16_t end_gid = FontTable::getushort (data, pos); pos+=2;
	    uint16_t start_cov = FontTable::getushort (data, pos); pos+=2;
	    glyph_list.reserve (glyph_list.size () + end_gid - start_gid + 1);
	    for (uint16_t j=start_cov, gid=start_gid; gid<=end_gid; j++, gid++)
		glyph_list.push_back (gid);
	}
    }
}

void OpenType::writeCoverageTable (QBuffer &, QDataStream &os, std::vector<uint16_t> &glyph_list) {
    std::vector<struct class_range> ranges;
    uint16_t idx;

    if (glyph_list.empty ()) {
	// Write dummy table and return
	os << (uint16_t) 1;
	os << (uint16_t) 0;
	return;
    }

    ranges.push_back (class_range { glyph_list[0], glyph_list[0], 0 });

    for (idx = 1; idx < glyph_list.size (); idx++) {
	struct class_range &last = ranges.back ();
	if (glyph_list[idx] == last.endGlyphID + 1) {
	    last.endGlyphID++;
	} else {
	    ranges.push_back ({ glyph_list[idx], glyph_list[idx], idx });
	}
    }

    if (ranges.size () * 6 > glyph_list.size () * 2) {
	os << (uint16_t) 1;
	os << (uint16_t) glyph_list.size ();
	for (uint16_t gid : glyph_list)
	    os << gid;
    } else {
	os << (uint16_t) 2;
	os << (uint16_t) ranges.size ();
	for (auto &rng : ranges) {
	    os << rng.startGlyphID;
	    os << rng.endGlyphID;
	    os << rng.startCoverageIndex;
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
	int nbits = dtab.deltaFormat == DeltaFormat::LOCAL_2_BIT_DELTAS ? 2 :
	    dtab.deltaFormat == DeltaFormat::LOCAL_4_BIT_DELTAS ? 4 : 8;
	int nbytes = ((size_cnt*nbits+15) & ~15)/8;
	int cur = 0;
	for (int i=0; i<nbytes/2; i++) {
	    uint16_t dset = FontTable::getushort (data, pos); pos+=2;
	    for (int j=0; j<16/nbits && cur<size_cnt; j++, cur++) {
		uint16_t mask = ((1 << nbits) - 1) << j*nbits;
		dtab.deltaValues[cur] = (dset & mask) >> j*nbits;
	    }
	}
    }
}

void OpenType::writeDeviceTable (QBuffer &, QDataStream &os, device_table &dtab) {
    os << dtab.startSize;
    os << dtab.endSize;
    os << dtab.deltaFormat;

    if (dtab.deltaFormat != DeltaFormat::VARIATION_INDEX) {
	size_t nbits = dtab.deltaFormat == DeltaFormat::LOCAL_2_BIT_DELTAS ? 2 :
	    dtab.deltaFormat == DeltaFormat::LOCAL_4_BIT_DELTAS ? 4 : 8;
	size_t nbytes = ((dtab.deltaValues.size ()*nbits+15) & ~15)/8;
	size_t cur = 0;
	for (size_t i=0; i<nbytes/2; i++) {
	    uint16_t dset = 0;
	    for (size_t j=0; j<16/nbits; j++, cur++) {
		dset = dset << nbits;
		if (cur < dtab.deltaValues.size ())
		    dset |= dtab.deltaValues[cur];
	    }
	    os << dset;
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
	itemVarStoreOffset = getlong (pos); pos +=4;
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
    QByteArray ba;
    QBuffer buf (&ba);
    buf.open (QIODevice::WriteOnly);
    QDataStream os (&buf);

    delete[] data; data = nullptr;
    os << (uint16_t) 1;
    uint16_t minor = 0;
    if (!m_varStore.regions.empty () || !m_varStore.data.empty ())
	minor = 3;
    else if (!m_markGlyphSets.empty ())
	minor = 2;
    os << minor;
    for (size_t i=0; i<4; i++)
	os << (uint16_t) 0;
    if (minor > 0)
	os << (uint16_t) 0;
    if (minor > 2)
	os << (uint32_t) 0;

    bool needs_glyph_classes = false;
    bool needs_attach_classes = false;
    for (uint16_t val : m_glyphClasses) {
	if (val) {
	    needs_glyph_classes = true;
	    break;
	}
    }
    for (uint16_t val : m_attachClasses) {
	if (val) {
	    needs_attach_classes = true;
	    break;
	}
    }

    if (needs_glyph_classes) {
	glyphClassDefOffset = buf.pos ();
	OpenType::writeClassDefTable (buf, os, m_glyphClasses);
    }

    if (!m_attachList.empty ()) {
	attachListOffset = buf.pos ();
	writeAttachList (buf, os);
    }

    if (!m_ligCaretList.empty ()) {
	ligCaretListOffset = buf.pos ();
	writeLigCaretList (buf, os);
    }

    if (needs_attach_classes) {
	markAttachClassDefOffset = buf.pos ();
	OpenType::writeClassDefTable (buf, os, m_attachClasses);
    }

    if (!m_markGlyphSets.empty ()) {
	markGlyphSetsDefOffset = buf.pos ();
	writeMarkGlyphSets (buf, os);
    }

    if (!m_varStore.regions.empty () || !m_varStore.data.empty ()) {
	itemVarStoreOffset = buf.pos ();
	FontVariations::writeVariationStore (os, buf, m_varStore);
    }

    if (needs_glyph_classes) {
	buf.seek (4);
	os << glyphClassDefOffset;
    }

    if (!m_attachList.empty ()) {
	buf.seek (6);
	os << attachListOffset;
    }

    if (!m_ligCaretList.empty ()) {
	buf.seek (8);
	os << ligCaretListOffset;
    }

    if (needs_attach_classes) {
	buf.seek (10);
	os << markAttachClassDefOffset;
    }

    if (!m_markGlyphSets.empty ()) {
	buf.seek (12);
	os << markGlyphSetsDefOffset;
    }

    if (!m_varStore.regions.empty () || !m_varStore.data.empty ()) {
	buf.seek (14);
	os << itemVarStoreOffset;
    }

    buf.close ();

    changed = false;
    td_changed = true;
    start = 0xffffffff;

    newlen = ba.length ();
    data = new char[newlen];
    std::copy (ba.begin (), ba.end (), data);
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
            QCoreApplication::tr ("Error reading AttachmentList table"),
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

void GdefTable::writeAttachList (QBuffer &buf, QDataStream &os) {
    std::vector<uint16_t> gl;
    uint16_t gcnt = m_attachList.size ();
    // placeholder
    os << (uint16_t) 0;
    os << gcnt;

    gl.reserve (gcnt);
    for (auto &pair : m_attachList) {
	os << (uint16_t) pair.second.size ();
	gl.push_back (pair.first);
	for (uint16_t pt_id : pair.second)
	    os << pt_id;
    }
    uint16_t cpos = buf.pos () - attachListOffset;
    buf.seek (attachListOffset);
    os << cpos;
    buf.seek (attachListOffset + cpos);
    OpenType::writeCoverageTable (buf, os, gl);
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

void GdefTable::writeLigCaretList (QBuffer &buf, QDataStream &os) {
    std::vector<uint16_t> off_list;
    std::vector<uint16_t> glyph_list;
    uint16_t lcnt = m_ligCaretList.size ();
    off_list.resize (lcnt);
    glyph_list.resize (lcnt);

    // placeholder
    os << (uint16_t) 0;
    os << lcnt;

    // set of placeholders
    for (size_t i=0; i<lcnt; i++)
	os << (uint16_t) 0;

    size_t i = 0;
    for (auto &pair : m_ligCaretList) {
	uint16_t lc_cnt = pair.second.size ();
	std::vector<uint16_t> caret_off_list;
	caret_off_list.resize (lc_cnt);
	off_list[i] = buf.pos () - ligCaretListOffset;
	glyph_list[i] = pair.first;
	os << lc_cnt;

	// again placeholders
	for (size_t j=0; j<lc_cnt; j++)
	    os << (uint16_t) 0;
	for (size_t j=0; j<lc_cnt; j++) {
	    auto &lc_val = pair.second[j];
	    caret_off_list[j] = buf.pos () - off_list[i] - ligCaretListOffset;
	    os << lc_val.format;
	    switch (lc_val.format) {
	      case 1:
		os << lc_val.coord;
		break;
	      case 2:
		os << lc_val.point_index;
		break;
	      case 3:
		os << lc_val.coord;
		lc_val.table_off = static_cast<uint16_t> (buf.pos () - caret_off_list[j]);
		os << lc_val.table_off;
		OpenType::writeDeviceTable (buf, os, lc_val.dev_table);
		break;
	    }
	}
	uint16_t pos = buf.pos ();
	buf.seek (ligCaretListOffset + off_list[i] + 2);
	for (uint16_t off : caret_off_list)
	    os << off;
	buf.seek (pos);
	i++;
    }
    uint16_t pos = buf.pos ();
    buf.seek (ligCaretListOffset + 4);
    for (uint16_t off : off_list)
        os << off;
    buf.seek (pos);
    OpenType::writeCoverageTable (buf, os, glyph_list);
    buf.seek (ligCaretListOffset);
    os << static_cast<uint16_t> (pos - ligCaretListOffset);
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

void GdefTable::writeMarkGlyphSets (QBuffer &buf, QDataStream &os) {
    uint16_t mcnt = m_markGlyphSets.size ();
    os << (uint16_t) 1; // format
    os << mcnt;
    std::vector<uint32_t> offsets;
    offsets.resize (mcnt, 0);

    // placeholders
    for (size_t i=0; i<mcnt; i++)
	os << (uint32_t) 0;

    for (size_t i=0; i<mcnt; i++) {
	offsets[i] = buf.pos () - markGlyphSetsDefOffset;
	OpenType::writeCoverageTable (buf, os, m_markGlyphSets[i]);
    }

    buf.seek (markGlyphSetsDefOffset + 4);
    for (size_t i=0; i<mcnt; i++)
	os << offsets[i];
}

uint16_t GdefTable::glyphClass (uint16_t gid) const {
    if (gid < m_glyphClasses.size ())
	return m_glyphClasses[gid];
    return 0;
}

void GdefTable::setGlyphClass (uint16_t gid, uint16_t val) {
    if (gid >= m_glyphClasses.size ())
	m_glyphClasses.resize (gid+1, GlyphClassDef::Zero);
    m_glyphClasses[gid] = val;
    changed = true;
}
