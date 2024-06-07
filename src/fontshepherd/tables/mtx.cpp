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

#include <sstream>
#include <ios>
#include <iostream>

#include "sfnt.h"
#include "tables.h"
#include "mtx.h"
#include "hea.h"

HmtxTable::HmtxTable (sfntFile *fontfile, TableHeader &props) :
    FontTable (fontfile, props),
    m_hhea (nullptr),
    m_loaded (false) {
}

HmtxTable::~HmtxTable () {
}

void HmtxTable::unpackData (sFont *font) {
    uint16_t i;
    uint32_t pos = 0;

    m_hhea = dynamic_cast<HeaTable *> (font->table (CHR ('h','h','e','a')));

    if (!m_hhea)
        return;
    m_hhea->fillup ();
    m_hhea->unpackData (font);

    m_lbearings.resize (font->glyph_cnt);
    m_widths.resize (font->glyph_cnt);

    for (i=0; i<m_hhea->numOfMetrics (); i++) {
        m_widths[i] = getushort (pos); pos+=2;
	uint16_t tmp = getushort (pos); pos+=2;
        m_lbearings[i] = static_cast<int16_t> (tmp);
    }
    int lastw = m_widths[i-1];

    for (i=m_hhea->numOfMetrics (); i<font->glyph_cnt; i++) {
        m_widths[i] = lastw;
	uint16_t tmp = getushort (pos); pos+=2;
        m_lbearings[i] = static_cast<int16_t> (tmp);
    }
    m_loaded = true;
}

void HmtxTable::packData () {
    std::ostringstream s;
    std::string st;
    uint16_t i;

    delete[] data; data = nullptr;

    int numhm = m_widths.size ();
    while (numhm > 1 && m_widths[numhm-1] == m_widths[numhm-2])
	numhm--;

    if (numhm != m_hhea->numOfMetrics ()) {
	m_hhea->setNumOfMetrics (numhm);
	m_hhea->packData ();
    }

    for (i=0; i<numhm; i++) {
	putushort (s, m_widths[i]);
	putushort (s, m_lbearings[i]);
    }
    for (; i<m_lbearings.size(); i++)
	putushort (s, m_lbearings[i]);

    changed = false;
    td_changed = true;
    start = 0xffffffff;

    st = s.str ();
    newlen = st.length ();
    data = new char[newlen];
    std::copy (st.begin (), st.end (), data);
}

int HmtxTable::lsb (uint16_t gid) const {
    if (gid < m_lbearings.size ())
        return (m_lbearings[gid]);
    return 0;
}

uint16_t HmtxTable::aw (uint16_t gid) const {
    if (gid < m_widths.size ())
        return (m_widths[gid]);
    return 0;
}

void HmtxTable::setNumGlyphs (uint16_t cnt) {
    if (cnt != m_lbearings.size ()) {
        m_lbearings.resize (cnt);
	m_widths.resize (cnt);
	changed = true;
    }
}

void HmtxTable::setlsb (uint16_t gid, int lsb) {
    if (gid < m_lbearings.size ()) {
        m_lbearings[gid] = lsb;
	changed = true;
    }
}

void HmtxTable::setaw (uint16_t gid, uint16_t aw) {
    if (gid < m_widths.size ()) {
        m_widths[gid] = aw;
	changed = true;
    }
}
