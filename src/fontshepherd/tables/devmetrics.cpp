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
#include <sstream>
#include <ios>
#include <cmath>
#include <iostream>
#include <cstring>

#include "sfnt.h"
#include "tables.h"
#include "glyphcontainer.h"
#include "head.h"
#include "glyf.h"
#include "devmetrics.h"
#include "editors/devmetricsedit.h"
#include "editors/headedit.h"

#include "fs_notify.h"

VdmxTable::VdmxTable (sfntFile *fontfile, TableHeader &props) :
    FontTable (fontfile, props) {
    records = {};
    m_version = 1; // default for a new table
}

void VdmxTable::unpackData (sFont*) {
    if (is_new) return;
    uint32_t pos = 0;
    this->fillup ();

    m_version = getushort (pos); pos+=2;
    pos+=2; // numRecs; seems to be not used
    uint16_t numRatios = getushort (pos); pos+=2;
    records.resize (numRatios);
    for (size_t i=0; i<numRatios; i++) {
	auto &rat = records[i];
	rat.bCharSet = static_cast<uint8_t> (data[pos++]);
	rat.xRatio = static_cast<uint8_t> (data[pos++]);
	rat.yStartRatio = static_cast<uint8_t> (data[pos++]);
	rat.yEndRatio = static_cast<uint8_t> (data[pos++]);
    }
    for (size_t i=0; i<numRatios; i++) {
	auto &rat = records[i];
	rat.groupOff = getushort (pos); pos+=2;
    }
    for (size_t i=0; i<numRatios; i++) {
	auto &rat = records[i];
	pos = rat.groupOff;
	uint16_t numRecs = getushort (pos); pos+=2;
	rat.entries.resize (numRecs);
	rat.startsz = static_cast<uint8_t> (data[pos++]);
	rat.endsz = static_cast<uint8_t> (data[pos++]);

	for (size_t j=0; j<numRecs; j++) {
	    auto &ent = rat.entries[j];
	    ent.yPelHeight = getushort (pos); pos+=2;
	    ent.yMax = static_cast<int16_t> (getushort (pos)); pos+=2;
	    ent.yMin = static_cast<int16_t> (getushort (pos)); pos+=2;
	}
    }
}

void VdmxTable::packData () {
    std::ostringstream s;
    std::string st;

    delete[] data; data = nullptr;

    putushort (s, m_version);
    putushort (s, records.size ());
    putushort (s, records.size ());
    for (auto &rec : records) {
	s << rec.bCharSet;
	s << rec.xRatio;
	s << rec.yStartRatio;
	s << rec.yEndRatio;
    }
    for (size_t i=0; i<records.size (); i++) {
	putushort (s, 0);
    }
    for (auto &rec : records) {
	rec.groupOff = s.tellp ();
	putushort (s, rec.entries.size ());
	s << rec.startsz;
	s << rec.endsz;
	for (auto &ent : rec.entries) {
	    putushort (s, ent.yPelHeight);
	    putushort (s, static_cast<uint16_t> (ent.yMax));
	    putushort (s, static_cast<uint16_t> (ent.yMin));
	}
    }
    s.seekp (6 + records.size ()*4);
    for (auto &rec : records) {
	putushort (s, rec.groupOff);
    }

    changed = false;
    td_changed = true;
    start = 0xffffffff;

    st = s.str ();
    newlen = st.length ();
    data = new char[newlen];
    std::copy (st.begin (), st.end (), data);
}

void VdmxTable::edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller) {
    if (data == nullptr && !is_new)
        fillup ();

    if (tv == nullptr) {
	unpackData (fnt);
        VdmxEdit *edit = new VdmxEdit (tptr, fnt, caller);
        tv = edit;
        edit->show ();
    } else {
        tv->raise ();
    }
}

uint16_t VdmxTable::version () const {
    return m_version;
}

uint16_t VdmxTable::numRatios () const {
    return records.size ();
}

static int gcd3 (int a, int b, int c) {
  int maxi = 0;
  maxi = std::max (a, std::max (b, c));

  for (int i = maxi; i>1; i--) {
      if ((a%i == 0) && (b%i == 0) && (c%i == 0)) {
          return i;
      }
  }

  return 1;
}

int VdmxTable::addRatio (uint8_t x, uint8_t yStart, uint8_t yEnd) {
    records.emplace_back ();
    auto &rec = records.back ();
    int gcd = gcd3 (x, yStart, yEnd);
    x /= gcd; yStart /= gcd; yEnd /= gcd;
    rec.bCharSet = 1;
    rec.xRatio = x;
    rec.yStartRatio = yStart;
    rec.yEndRatio = yEnd;
    changed = true;
    return (records.size () - 1);
}

void VdmxTable::setRatioRange (uint16_t idx, uint8_t start_size, uint8_t end_size) {
    if (idx < records.size () && end_size >= start_size) {
	auto &rec = records[idx];
	rec.startsz = start_size;
	rec.endsz = end_size;
	rec.entries.resize (end_size - start_size + 1);
	for (size_t i=0; i<rec.entries.size (); i++) {
	    rec.entries[i].yPelHeight = i+start_size;
	}
	changed = true;
    }
}

void VdmxTable::clear () {
    m_version = 1;
    records.clear ();
    changed = true;
}

HdmxTable::HdmxTable (sfntFile *fontfile, TableHeader &props) :
    FontTable (fontfile, props) {
    records = {};
}

void HdmxTable::unpackData (sFont*) {
    if (is_new) return;
    uint32_t pos = 0;
    this->fillup ();

    m_version = getushort (pos); pos+=2;
    uint16_t numRecords = getushort (pos); pos+=2;
    uint32_t sizeDeviceRecord = getlong (pos); pos+=4;

    for (size_t i=0; i<numRecords; i++) {
	uint8_t pixelSize = data[pos]; pos++;
	records[pixelSize] = {};
	auto &rec = records.at (pixelSize);
	rec.resize (sizeDeviceRecord-2);

	/* maxWidth (not needed) */ pos++;
	for (size_t j=0; j<sizeDeviceRecord-2; j++) {
	    rec[j] = data[pos++];
	}
    }
}

void HdmxTable::packData () {
    std::ostringstream s;
    std::string st;

    delete[] data; data = nullptr;

    putushort (s, m_version);
    putushort (s, records.size ());
    uint32_t rec_size = records.begin ()->second.size () + 2;
    uint8_t pad = (-rec_size)&3;
    rec_size += pad;

    putlong (s, rec_size);
    for (auto &pair : records) {
	s << pair.first;
	// max_element returns an iterator
	uint8_t max = *std::max_element (pair.second.begin(), pair.second.end());
	s << max;
	for (size_t j=0; j<pair.second.size (); j++) {
	    s << pair.second[j];
	}
	for (size_t j=0; j<pad; j++) {
	    s << '\0';
	}
    }

    changed = false;
    td_changed = true;
    start = 0xffffffff;

    st = s.str ();
    newlen = st.length ();
    data = new char[newlen];
    std::copy (st.begin (), st.end (), data);
}

void HdmxTable::edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller) {
    if (data == nullptr && !is_new)
        fillup ();

    if (tv == nullptr) {
	unpackData (fnt);
        HdmxEdit *edit = new HdmxEdit (tptr, fnt, caller);
        tv = edit;
        edit->show ();
    } else {
        tv->raise ();
    }
}

uint16_t HdmxTable::version () const {
    return m_version;
}

uint16_t HdmxTable::numRecords () const {
    return records.size ();
}

uint16_t HdmxTable::numGlyphs () const {
    if (!records.empty ())
	return records.begin ()->second.size ();
    return 0;
}

uint8_t HdmxTable::maxWidth (uint8_t size) const {
    if (records.count (size)) {
	auto &lst = records.begin ()->second;
	return *std::max_element (lst.begin(), lst.end());
    }
    return 0;
}

uint8_t HdmxTable::maxSize () const {
    if (!records.empty ()) {
	return (records.rbegin()->first);
    }
    return 0;
}

void HdmxTable::setNumGlyphs (uint16_t size) {
    for (auto &pair : records) {
	pair.second.resize (size);
    }
    changed = true;
}

void HdmxTable::addSize (uint8_t size) {
    if (!records.count (size)) {
	records[size] = std::vector<uint8_t> (numGlyphs (), 0);
	changed = true;
    }
}

void HdmxTable::clear () {
    records.clear ();
    changed = true;
}

LtshTable::LtshTable (sfntFile *fontfile, TableHeader &props) :
    FontTable (fontfile, props) {
    m_version = 0;
    yPixels = {};
}

void LtshTable::unpackData (sFont*) {
    if (is_new) return;
    uint32_t pos = 0;
    this->fillup ();

    m_version = getushort (pos); pos+=2;
    uint16_t numGlyphs = getushort (pos); pos+=2;
    yPixels.resize (numGlyphs, 1);

    for (size_t i=0; i<numGlyphs; i++) {
	yPixels[i] = data[pos++];
    }
}

void LtshTable::packData () {
    std::ostringstream s;
    std::string st;

    delete[] data; data = nullptr;

    putushort (s, m_version);
    putushort (s, yPixels.size ());

    for (size_t i=0; i<yPixels.size (); i++)
	s << yPixels[i];

    changed = false;
    td_changed = true;
    start = 0xffffffff;

    st = s.str ();
    newlen = st.length ();
    data = new char[newlen];
    std::copy (st.begin (), st.end (), data);
}

uint16_t LtshTable::version () const {
    return m_version;
}

uint16_t LtshTable::numGlyphs () const {
    return yPixels.size ();
}

uint8_t LtshTable::yPixel (uint16_t gid) const {
    if (gid < yPixels.size ())
	return yPixels[gid];
    return 0;
}

void LtshTable::setNumGlyphs (uint16_t cnt, bool clear) {
    if (clear)
	yPixels.clear ();
    yPixels.resize (cnt, 1);
    changed = true;
}

DeviceMetricsProvider::DeviceMetricsProvider (sFont &fnt) : m_font (fnt) {
    if (ftWrapper.hasContext ()) {
	int idx = m_font.file_index;
	ftWrapper.init (m_font.container->path (idx), m_font.index);
    }
}

bool DeviceMetricsProvider::checkHead (const char *tag, QWidget *parent) {
    HeadTable *head = dynamic_cast<HeadTable*> (m_font.table (CHR ('h','e','a','d')));
    if (!head) return false;
    head->fillup ();
    head->unpackData (&m_font);
    bool instr_mod_aw = head->flags (4);
    if (!instr_mod_aw) {
	FontShepherd::postWarning (tr ("'%1' compile").arg (tag),
	    tr ("Warning: Bit 4 of 'flags' field in 'head' table is not set. I will set it for you"),
	    parent);
	head->setBitFlag (4, true);
	head->packData ();
	TableEdit *ed = head->editor ();
	if (ed) {
	    HeadEdit *he = qobject_cast<HeadEdit *> (ed);
	    if (he) he->resetData ();
	}
    }
    return true;
}

void DeviceMetricsProvider::checkGlyphCount (GlyphContainer *glyf, uint16_t gcnt) {
    HdmxTable *hdmx = dynamic_cast<HdmxTable*> (m_font.table (CHR ('h','d','m','x')));
    LtshTable *ltsh = dynamic_cast<LtshTable*> (m_font.table (CHR ('L','T','S','H')));
    bool hdmx_changed=false, ltsh_changed=false;
    uint16_t em_size = m_font.units_per_em;

    if (hdmx) {
	hdmx->fillup ();
	hdmx->unpackData (&m_font);
	uint16_t oldcnt = hdmx->numGlyphs ();
	if (oldcnt != gcnt) {
	    hdmx->setNumGlyphs (gcnt);
	    hdmx_changed = true;
	}
	// temporary solution: just assume all modified glyphs would change their
	// advance width linearly
	for (size_t i=0; i<gcnt; i++) {
	    ConicGlyph *g = glyf->glyph (&m_font, i);
	    if (g->isModified ()) {
		uint16_t aw = g->advanceWidth ();
		for (auto &rec : hdmx->records) {
		    rec.second[i] = std::lround (static_cast<float> (rec.first) / em_size * aw);
		}
		hdmx_changed = true;
	    }
	}
	if (hdmx_changed) hdmx->packData ();
    }
    if (ltsh) {
	ltsh->fillup ();
	ltsh->unpackData (&m_font);
	uint16_t oldcnt = ltsh->numGlyphs ();
	if (oldcnt != gcnt) {
	    ltsh->setNumGlyphs (gcnt, false);
	    ltsh_changed=true;
	}
	for (size_t i=0; i<gcnt; i++) {
	    ConicGlyph *g = glyf->glyph (&m_font, i);
	    if (g->isModified ()) {
		ltsh->yPixels[i] = 1;
		ltsh_changed = true;
	    }
	}
	if (ltsh_changed) ltsh->packData ();
    }
}

int DeviceMetricsProvider::calculateHdmx (HdmxTable &hdmx, QWidget *parent) {
    int ft_flags = FT_LOAD_RENDER | FT_LOAD_NO_BITMAP | FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_SVG;
    if (!checkHead ("hdmx", parent)) return 1;
    uint8_t max = hdmx.maxSize ();
    QProgressDialog progress (
	tr ("Building 'hdmx' table"), tr ("Abort"), 2, max+1, parent);
    progress.setWindowModality (Qt::WindowModal);
    progress.show ();
    int overflow_at_size = 0;
    uint16_t overflow_at_glyph = 0xffff;

    for (size_t i=2; i<=max && !overflow_at_size; i++) {
	ftWrapper.setPixelSize (i, i);
	if (hdmx.records.count (i)) {
	    auto &rec = hdmx.records.at (i);
	    for (size_t j=0; j<rec.size (); j++) {
		freetype_raster r = ftWrapper.gridFitGlyph (j, ft_flags, nullptr);
		int rounded = std::lround (r.advance/64);
		if (rounded > 255) {
		    overflow_at_size = i;
		    overflow_at_glyph = j;
		    break;
		}
		rec[j] = static_cast<uint8_t> (rounded);
	    }
	}
        qApp->instance ()->processEvents ();
        if (progress.wasCanceled ())
            return 1;
        progress.setValue (i);
    }
    progress.setValue (max+1);

    if (overflow_at_size) {
        FontShepherd::postWarning (tr ("'hdmx' compile"),
	    tr ("Couldn't generate 'hdmx' records for PPEM %1 and above: width overflow at glyph %2")
		.arg (overflow_at_size).arg (overflow_at_glyph), parent);
	auto it = hdmx.records.find (overflow_at_size);
	hdmx.records.erase (it, hdmx.records.end ());
    }
    return 0;
}

bool aw_near (uint16_t gridfitted, uint16_t linear) {
    double fudge = static_cast<double> (linear)/50.0;
    return (std::abs (linear - gridfitted) < fudge);
}

int DeviceMetricsProvider::calculateLtsh (LtshTable &ltsh, QWidget *parent) {
    int ft_flags = FT_LOAD_RENDER | FT_LOAD_NO_BITMAP | FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_SVG ;
    uint16_t em_size = m_font.units_per_em;
    if (!checkHead ("LTSH", parent)) return 1;
    GlyfTable *glyf = dynamic_cast<GlyfTable*> (m_font.table (CHR ('g','l','y','f')));
    if (!glyf) return 1;
    glyf->fillup ();
    glyf->unpackData (&m_font);

    uint16_t glyphcnt = ltsh.numGlyphs ();
    std::vector<bool> has_instrs (glyphcnt);
    std::vector<uint16_t> awidths (glyphcnt);
    std::vector<uint16_t> useMyMetrics (glyphcnt);
    for (size_t i=0; i<ltsh.numGlyphs (); i++) {
	ConicGlyph *g = glyf->glyph (&m_font, i);
	has_instrs[i] = !g->instructions.empty ();
	awidths[i] = g->advanceWidth ();
	useMyMetrics[i] = g->useMyMetricsGlyph ();
    }

    QProgressDialog progress (
	tr ("Building 'LTSH' table"), tr ("Abort"), 0, 255, parent);
    progress.setWindowModality (Qt::WindowModal);
    progress.show ();

    for (size_t j=254; j>1; j--) {
        ftWrapper.setPixelSize (j, j);
	for (size_t i=0; i<glyphcnt; i++) {
	    uint16_t aw = awidths[i];
	    if (aw > 0 && has_instrs[i] && (ltsh.yPixels[i] < j)) {
		freetype_raster r = ftWrapper.gridFitGlyph (i, ft_flags, nullptr);
		uint16_t aw_gf = std::lround (r.advance/64.0);
		uint16_t aw_lin = std::lround (static_cast<float> (j) / em_size * aw);
		if (aw_gf != aw_lin && (j<=50 || !aw_near (aw_gf, aw_lin))) {
		    ltsh.yPixels[i] = j+1;
		}
	    }
	}
        qApp->instance ()->processEvents ();
        if (progress.wasCanceled ())
            return 1;
        progress.setValue (255-j);
    }
    for (size_t i=0; i<glyphcnt; i++) {
	uint16_t msource = useMyMetrics[i];
	if (msource != 0xFFFF)
	    ltsh.yPixels[i] = ltsh.yPixels[msource];
    }
    progress.setValue (0);
    return 0;
}

int DeviceMetricsProvider::calculateVdmxLimit
    (VdmxTable &vdmx, std::vector<std::pair<int, DBounds>> &metrics, bool up, QWidget *parent) {
    int ft_flags = FT_LOAD_RENDER | FT_LOAD_NO_BITMAP | FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_SVG | FT_LOAD_LINEAR_DESIGN;

    std::sort (metrics.begin (), metrics.end (),
	[up](const std::pair<int, DBounds> &m1, const std::pair<int, DBounds> &m2) {
	    return (up ? m1.second.maxy > m2.second.maxy : m1.second.miny < m2.second.miny);
    });

    for (auto &rec : vdmx.records) {
	double x = rec.xRatio;
	double y = (rec.yStartRatio + rec.yEndRatio)/2.0;
	// for 0:0 (which means 'default') just calculate 1:1 ratio
	if (x==0 || y==0) { x=1; y=1; }
        double xrat = x/y;
        QProgressDialog progress (
	    tr ("Building 'VDMX' table for ratio %1:%2-%3")
	    .arg (rec.xRatio).arg (rec.yStartRatio).arg (rec.yEndRatio),
	    tr ("Abort"), rec.startsz, rec.endsz, parent);
	for (auto &ent : rec.entries) {
	    progress.setWindowModality (Qt::WindowModal);
	    progress.show ();
	    ftWrapper.setPixelSize (std::floor (ent.yPelHeight*xrat + .5), ent.yPelHeight);
	    int16_t maxmin = 0;
	    for (size_t i=0; i<12 && i<metrics.size (); i++) {
		freetype_raster r = ftWrapper.gridFitGlyph (metrics[i].first, ft_flags, nullptr);
		if (up && r.as > maxmin) maxmin = r.as;
		else if (!up && r.as-r.rows < maxmin) maxmin = r.as-r.rows;
	    }
	    int16_t &limit = up ? ent.yMax : ent.yMin;
	    limit = maxmin;
	    qApp->instance ()->processEvents ();
	    if (progress.wasCanceled ())
		return 1;
	    progress.setValue (ent.yPelHeight);
	}
	progress.setValue (rec.endsz);
    }
    return 0;
}

int DeviceMetricsProvider::calculateVdmx (VdmxTable &vdmx, QWidget *parent) {
    FontTable *glyf_tbl = m_font.table (CHR ('g','l','y','f'));

    if (!glyf_tbl) return 1;
    GlyfTable *glyf = dynamic_cast<GlyfTable*> (glyf_tbl);
    if (!glyf) return 2;
    glyf->fillup ();
    glyf->unpackData (&m_font);

    std::vector<std::pair<int, DBounds>> metrics;
    metrics.reserve (m_font.glyph_cnt);
    for (size_t i=0; i<m_font.glyph_cnt; i++) {
	ConicGlyph *g = glyf->glyph (&m_font, i);
	if (!g->isEmpty ())
	    metrics.push_back ({i, g->bb});
    }

    int ret;
    ret = calculateVdmxLimit (vdmx, metrics, true, parent);
    if (ret) return ret;
    ret = calculateVdmxLimit (vdmx, metrics, false, parent);
    return ret;
}
