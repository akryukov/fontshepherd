/* Copyright (C) 2000-2012 by George Williams
 * Copyright (C) 2022 by Alexey Kryukov
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

#include "sfnt.h"
#include "editors/fontview.h" // Includes also tables.h
#include "tables/glyphcontainer.h"
#include "tables/glyf.h"
#include "tables/head.h"
#include "tables/maxp.h"
#include "tables/mtx.h"

GlyfTable::GlyfTable (sfntFile *fontfile, TableHeader &props) :
    GlyphContainer (fontfile, props) {
}

GlyfTable::~GlyfTable () {
}

void GlyfTable::unpackData (sFont *font) {
    if (td_loaded)
        return;
    GlyphContainer::unpackData (font);

    m_loca = std::dynamic_pointer_cast<LocaTable> (font->sharedTable (CHR ('l','o','c','a')));
    if (!m_loca)
        return;

    m_loca->fillup ();
    m_loca->unpackData (font);
    td_loaded = true;
}

void GlyfTable::packData () {
    QByteArray ba;
    QBuffer buf (&ba);
    buf.open (QIODevice::WriteOnly);
    QDataStream os (&buf);
    uint16_t gid = 0;

    delete[] data; data = nullptr;

    m_loca->setGlyphCount (m_glyphs.size ());
    m_loca->setGlyphOffset (gid++, 0);
    for (auto *g : m_glyphs) {
	uint32_t off = g->toTTF (buf, os, m_maxp);
	m_loca->setGlyphOffset (gid++, off);
	m_hmtx->setaw (g->gid (), g->advanceWidth ());
	m_hmtx->setlsb (g->gid (), g->leftSideBearing ());
    }
    buf.close ();

    changed = false;
    td_changed = true;
    start = 0xffffffff;

    newlen = ba.length ();
    data = new char[newlen];
    std::copy (ba.begin (), ba.end (), data);
    m_loca->packData ();
}

ConicGlyph* GlyfTable::glyph (sFont* fnt, uint16_t gid) {
    if (!m_loca || gid >= m_glyphs.size ())
        return nullptr;
    if (m_glyphs[gid])
        return m_glyphs[gid];

    uint32_t off = m_loca->getGlyphOffset (gid);
    uint32_t noff = m_loca->getGlyphOffset (gid+1);
    if (off == 0xFFFFFFFF || noff == 0xFFFFFFFF)
        return nullptr;

    BaseMetrics gm = {fnt->units_per_em, fnt->ascent, fnt->descent};
    ConicGlyph *g = glyph_pool.construct (gid, gm);
    m_glyphs[gid] = g;
    if (m_hmtx)
        g->setHMetrics (m_hmtx->lsb (gid), m_hmtx->aw (gid));

    BoostIn buf (data+off, noff-off);
    g->fromTTF (buf, off);
    return g;
}

uint16_t GlyfTable::addGlyph (sFont* fnt, uint8_t) {
    BaseMetrics gm = {fnt->units_per_em, fnt->ascent, fnt->descent};
    uint16_t gid = m_glyphs.size ();
    ConicGlyph *g = glyph_pool.construct (gid, gm);
    g->setAdvanceWidth (fnt->units_per_em/3);
    g->setOutlinesType (OutlinesType::TT);
    m_glyphs.push_back (g);
    return (gid);
}

bool GlyfTable::usable () const {
    return td_loaded;
}

LocaTable::LocaTable (sfntFile *fontfile, TableHeader &props) :
    FontTable (fontfile, props) {
}

void LocaTable::unpackData (sFont *font) {
    int i, shift;
    uint32_t pos = 0;

    m_head = dynamic_cast<HeadTable *> (font->table (CHR ('h','e','a','d')));
    if (!m_head)
        return;
    m_head->fillup ();
    m_head->unpackData (font);
    bool is_long = m_head->indexToLocFormat ();
    shift = is_long ? 4 : 2;
    offsets.reserve (font->glyph_cnt+1);

    for (i=0; i<font->glyph_cnt+1; i++) {
        if ((pos + shift) > len) {
            QMessageBox::critical (0, QMessageBox::tr ("Error"),
                QMessageBox::tr ("Broken loca table: got %1 glyph offsets, expected %2.")
                .arg (i).arg (font->glyph_cnt+1));
            break;
        }
        uint32_t off = is_long ? getlong (pos) : getushort (pos) * 2;
        offsets.push_back (off);
        pos += shift;
    }
}

void LocaTable::packData () {
    std::ostringstream s;
    std::string st;
    bool is_long = (offsets.back ()/2 > 0xffff);

    delete[] data; data = nullptr;

    changed = false;
    td_changed = true;
    start = 0xffffffff;

    for (size_t i=0; i<offsets.size (); i++) {
	if (is_long)
	    putlong (s, offsets[i]);
	else
	    putushort (s, offsets[i]/2);
    }
    if (is_long != m_head->indexToLocFormat ()) {
	m_head->setIndexToLocFormat (is_long);
	if (m_head->editor ())
	    m_head->editor ()->resetData ();
    }

    st = s.str ();
    newlen = st.length ();
    data = new char[newlen];
    std::copy (st.begin (), st.end (), data);
}

uint32_t LocaTable::getGlyphOffset (uint16_t gid) const {
    if (gid >= offsets.size ())
        return 0xFFFFFFFF;
    return offsets[gid];
}

void LocaTable::setGlyphOffset (uint16_t gid, uint32_t off) {
    if (gid < offsets.size ())
	offsets[gid] = off;
}

void LocaTable::setGlyphCount (uint16_t cnt) {
    if (cnt != offsets.size ()-1)
	offsets.resize (cnt+1);
}
