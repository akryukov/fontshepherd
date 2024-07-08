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

#include "sfnt.h"
#include "tables.h"
#include "tables/glyphcontainer.h" // also includes splineglyph.h
#include "tables/maxp.h"
#include "tables/hea.h"
#include "tables/head.h"
#include "tables/os_2.h"
#include "tables/mtx.h"
#include "tables/glyf.h"
#include "tables/instr.h"

#include "tinyfont.h"

#include "fs_notify.h"

// need tables: 'head', 'hhea', 'hmtx', 'os_2', 'cvt ', 'fpgm', 'prep'
TinyFontProvider::TinyFontProvider (sFont* font, QWidget *parent) :
    m_origFont (font), m_widget (parent) {
    std::set<uint32_t> required_tags = {
	CHR ('m','a','x','p'),
	CHR ('h','e','a','d'),
	CHR ('h','h','e','a'),
	CHR ('h','m','t','x'),
	CHR ('l','o','c','a'),
	CHR ('g','l','y','f')
    };
    std::set<uint32_t> optional_tags = {
	CHR ('c','v','t',' '),
	CHR ('f','p','g','m'),
	CHR ('p','r','e','p')
    };
    for (auto tag : required_tags) {
	FontTable *tbl = font->table (tag);
	if (!tbl) {
	    m_valid = false;
	    return;
	}
    }
    m_valid = true;

    m_font.tbls.reserve (16);
    m_font.glyph_cnt = 0;
    m_font.version = 0x10000;
    for (auto tag : optional_tags) {
	FontTable *tbl = font->table (tag);
	if (tbl) {
	    tbl->fillup ();
	    m_font.tbls.emplace_back (std::make_shared<FontTable> (tbl));
	}
    }

    TableHeader props;
    HeadTable *orig_head = dynamic_cast<HeadTable *> (font->table (CHR ('h','e','a','d')));
    m_font.units_per_em = orig_head->unitsPerEm ();
    m_font.descent = orig_head->yMin ();
    m_font.ascent = orig_head->yMax ();

    props.file = nullptr;
    props.off = 0xffffffff;
    props.length = 0;
    props.checksum = 0;

    props.iname = CHR ('m','a','x','p');
    m_font.tbls.emplace_back (std::make_shared<MaxpTable>
	(dynamic_cast<MaxpTable *> (font->table (CHR ('m','a','x','p')))));
    props.iname = CHR ('h','e','a','d');
    m_font.tbls.emplace_back (std::make_shared<HeadTable>
	(dynamic_cast<HeadTable *> (font->table (CHR ('h','e','a','d')))));
    props.iname = CHR ('h','h','e','a');
    m_font.tbls.emplace_back (std::make_shared<HeaTable>
	(dynamic_cast<HeaTable *> (font->table (CHR ('h','h','e','a')))));
    props.iname = CHR ('h','m','t','x');
    m_font.tbls.emplace_back (std::make_shared<HmtxTable> (nullptr, props));
    props.iname = CHR ('l','o','c','a');
    m_font.tbls.emplace_back (std::make_shared<LocaTable> (nullptr, props));
    props.iname = CHR ('g','l','y','f');
    m_font.tbls.emplace_back (std::make_shared<GlyfTable> (nullptr, props));

    for (auto &tbl : m_font.tbls)
	tbl->setContainer (nullptr);

    GlyfTable *glyf = dynamic_cast<GlyfTable *> (m_font.table (CHR ('g','l','y','f')));
    glyf->unpackData (&m_font);
    m_origContainer = dynamic_cast<GlyfTable *> (font->table (CHR ('g','l','y','f')));
    appendOrReloadGlyph (0);
}

uint16_t TinyFontProvider::appendOrReloadGlyph (uint16_t gid) {
    GlyfTable *glyf = dynamic_cast<GlyfTable *> (m_font.table (CHR ('g','l','y','f')));
    ConicGlyph *g = m_origContainer->glyph (m_origFont, gid);
    bool appended = m_gidCorr.count (gid);

    TableHeader props;
    props.file = nullptr;
    props.off = 0xffffffff;
    props.length = 0;
    props.checksum = 0;
    props.iname = CHR ('m','a','x','p');
    MaxpTable dummy_maxp (nullptr, props);

    QByteArray gba;
    QBuffer gbuf (&gba);
    gbuf.open (QIODevice::WriteOnly);
    QDataStream os (&gbuf);

    g->toTTF (gbuf, os, &dummy_maxp);
    gbuf.close ();

    uint16_t new_gid = appended ? m_gidCorr[gid] : glyf->addGlyph (&m_font);
    ConicGlyph *ng = glyf->glyph (&m_font, new_gid);
    BoostIn buf (gba.data (), gba.size ());
    ng->fromTTF (buf, 0);
    ng->setHMetrics (g->leftSideBearing (), g->advanceWidth ());

    if (!appended) {
	m_gidCorr[gid] = new_gid;
	m_font.glyph_cnt++;
	for (auto &ref : ng->refs) {
	    uint16_t ref_gid = appendOrReloadGlyph (ref.GID);
	    ref.cc = glyf->glyph (&m_font, ref_gid);
	    ref.GID = ref_gid;
	}
    }
    return new_gid;
}

void TinyFontProvider::reloadGlyphs () {
    for (auto pair : m_gidCorr) {
	appendOrReloadGlyph (pair.first);
    }
}

void TinyFontProvider::prepare () {
    HeaTable  *hhea = dynamic_cast<HeaTable*> (m_font.table (CHR ('h','h','e','a')));
    HmtxTable *hmtx = dynamic_cast<HmtxTable*> (m_font.table (CHR ('h','m','t','x')));
    MaxpTable *maxp = dynamic_cast<MaxpTable*> (m_font.table (CHR ('m','a','x','p')));
    GlyfTable *glyf = dynamic_cast<GlyfTable *> (m_font.table (CHR ('g','l','y','f')));
    uint16_t gcnt = m_font.glyph_cnt;

    hhea->setNumOfMetrics (gcnt);
    maxp->setGlyphCount (gcnt);
    hmtx->setNumGlyphs (gcnt);

    hhea->packData ();
    glyf->packData ();
    maxp->packData ();
    hmtx->packData ();
}

void TinyFontProvider::compile () {
    prepare ();

    ba.clear ();
    QBuffer buf;
    buf.setBuffer (&ba);
    buf.open (QIODevice::ReadWrite);

    for (int j=0; j < m_font.tableCount (); j++) {
        FontTable *tab = m_font.tbls[j].get ();
        tab->newstart = 0;
        tab->newchecksum = 0;
        tab->inserted = false;
   }

    HeadTable *head = dynamic_cast<HeadTable *> (m_font.table (CHR ('h','e','a','d')));
    if (head) {
        head->updateModified ();
        head->packData ();
    }

    sfntFile::fntWrite (&buf, &m_font);

    uint32_t checksum = sfntFile::fileCheck (&buf);
    if (head) {
        checksum = 0xb1b0afba - checksum;
        buf.seek (head->newstart+2*sizeof (uint32_t));
        sfntFile::putlong (&buf, checksum);
        head->setCheckSumAdjustment (checksum);
    }
    for (int j=0; j<m_font.tableCount (); ++j) {
	FontTable *tab = m_font.tbls[j].get ();
	tab->start = tab->newstart;
	tab->len = tab->newlen;
	tab->oldchecksum = tab->newchecksum;
	tab->changed = tab->td_changed = false;
	tab->inserted = false;
	tab->infile = nullptr;
	tab->is_new = false;
    }
    buf.close ();
}

const char *TinyFontProvider::fontData () const {
    return ba.data ();
}

uint32_t TinyFontProvider::fontDataSize () const {
    return ba.size ();
}

uint16_t TinyFontProvider::gidCorr (uint16_t gid) const {
    if (m_gidCorr.count (gid))
	return m_gidCorr.at (gid);
    return 0xFFFF;
}

bool TinyFontProvider::valid () const {
    return m_valid;
}
