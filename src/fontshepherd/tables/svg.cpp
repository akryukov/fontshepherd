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
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <iomanip>

#include "fs_notify.h"
#include "sfnt.h"
#include "editors/fontview.h" // Includes also tables.h
#include "tables/glyphcontainer.h"
#include "tables/svg.h"
#include "tables/cff.h"
#include "tables/glyf.h"
#include "tables/mtx.h"
#include "tables/maxp.h"

SvgTable::SvgTable (sfntFile *fontfile, const TableHeader &props) :
    GlyphContainer (fontfile, props), m_usable (false) {
}

SvgTable::~SvgTable () {
}

void SvgTable::unpackData (sFont *font) {
    uint32_t pos = 0;
    if (td_loaded)
        return;
    td_loaded = true;

    m_glyf = dynamic_cast<GlyfTable *> (font->table (CHR ('g','l','y','f')));
    m_cff = dynamic_cast<CffTable *> (font->table (CHR ('C','F','F',' ')));
    if (!m_glyf && !m_cff)
	m_cff = dynamic_cast<CffTable *> (font->table (CHR ('C','F','F','2')));
    if (m_glyf && !m_glyf->usable ())
    	m_glyf->unpackData (font);

    m_iEntries.reserve (font->glyph_cnt);
    m_docIdx.resize (font->glyph_cnt, -1);
    GlyphContainer::unpackData (font);
    m_version = getushort (0);
    m_offsetToSVGDocIndex = getlong (2);

    uint16_t numEntries = getushort (m_offsetToSVGDocIndex);
    pos = m_offsetToSVGDocIndex + 2;

    for (size_t i=0; i<numEntries; i++) {
        uint16_t startGlyphID = getushort (pos); pos+=2;
        uint16_t endGlyphID = getushort (pos); pos+=2;
        uint32_t svgDocOffset = getlong (pos); pos+=4;
        uint32_t svgDocLength = getlong (pos); pos+=4;

	auto it = std::find_if (m_iEntries.begin (), m_iEntries.end (),
	    [svgDocOffset, svgDocLength] (const svg_document_index_entry &e) {
		return (e.svgDocOffset == svgDocOffset && e.svgDocLength == svgDocLength);
	});
	uint16_t entry_pos = it - m_iEntries.begin ();
	if (entry_pos == m_iEntries.size ()) {
	    m_iEntries.emplace_back ();
	    auto &ie = m_iEntries.back ();
	    ie.svgDocOffset = svgDocOffset;
	    ie.svgDocLength = svgDocLength;
	}

        for (int j=startGlyphID; j<=endGlyphID; j++) {
	    if (j<font->glyph_cnt) {
		m_iEntries[entry_pos].glyphs.push_back (j);
		m_docIdx[j] = entry_pos;
	    } else {
		FontShepherd::postError (
		    tr ("Wrong glyph count"),
		    tr ("SVG table refers to a glyph with ID %1, "
			"while the font contains only %2 glyphs").arg (j).arg (font->glyph_cnt),
		    container->parent ());
	    }
	}
    }
    m_usable = true;
}

void SvgTable::cleanupDocEntries () {
    m_iEntries.erase (
        std::remove_if (
	    m_iEntries.begin (),
	    m_iEntries.end (),
	    [](svg_document_index_entry &ie){return ie.glyphs.empty ();}
	), m_iEntries.end ()
    );
    for (size_t i=0; i<m_iEntries.size (); i++) {
	auto &ie = m_iEntries[i];
	for (uint16_t gid : ie.glyphs)
	    m_docIdx[gid] = i;
    }
}

void SvgTable::dumpGradients (std::stringstream &ss, svg_document_index_entry &ie) {
    int num_defs = 0;
    for (uint16_t gid : ie.glyphs) {
	ConicGlyph *g = m_glyphs[gid];
	num_defs += g->gradients.size ();
    }

    if (num_defs) {
	std::set<std::string> unique_ids;
        ss << "  <defs>\n";
	for (uint16_t gid : ie.glyphs) {
	    ConicGlyph *g = m_glyphs[gid];
	    std::map<std::string, Gradient>::iterator it;
	    for (it = g->gradients.begin (); it != g->gradients.end (); ++it) {
		const std::string &grad_id = it->first;
		if (!unique_ids.count (grad_id)) {
		    Gradient &grad = it->second;
		    ConicGlyph::svgDumpGradient (ss, grad, grad_id);
		    unique_ids.insert (grad_id);
		}
	    }
	}
        ss << "  </defs>\n";
    }
}

void SvgTable::packData () {
    QByteArray ba;
    QBuffer buf (&ba);
    // needs read access for skipRawData (). Weird...
    buf.open (QIODevice::ReadWrite);
    QDataStream os (&buf);
    bool mtx_changed = false;

    // SVG table consists of several SVG documents, and each of those
    // documents may contain one or more glyphs. In fact, every font
    // I have seen so far has a separate document for each glyph. However,
    // if we need reference support (and I think we do), then it is obvious
    // that at least both source and target glyph should reside in the same
    // document. Of course glyphs may also share gradients and other objects.
    // So before outputting the table loop through the glyphs taking a special
    // care of references. If current glyph and reference glyph are
    // in different documents, then make them both a part of the same document.
    // The document itself is marked as changed, which means we have to
    // generate it from graphical objects instead of relying on the existing
    // binary data
    for (ConicGlyph *g : m_glyphs) {
	if (g && g->isModified ()) {
	    uint16_t gid = g->gid ();
	    m_hmtx->setaw (g->gid (), g->advanceWidth ());
	    mtx_changed |= true;
	    auto &ie = m_iEntries[m_docIdx[gid]];
	    ie.changed = true;
	    for (auto &ref : g->refs) {
		if (m_docIdx[gid] != m_docIdx[ref.GID]) {
		    auto &refie = m_iEntries[m_docIdx[ref.GID]];
		    std::vector<uint16_t> uni;
		    auto &cur = ie.glyphs;
		    auto &other = refie.glyphs;
		    std::set_union (
			cur.begin (), cur.end (),
			other.begin (), other.end (),
			std::back_inserter (uni)
		    );
		    if (gid > ref.GID) {
			refie.glyphs = uni;
			ie.glyphs.clear ();
			m_docIdx[gid] = m_docIdx[ref.GID];
			refie.changed = true;
		    } else {
			refie.glyphs.clear ();
			ie.glyphs = uni;
			m_docIdx[ref.GID] = m_docIdx[gid];
		    }
		}
	    }
	}
    }

    // Remove document entries which no longer have any glyphs associated
    // (as we have moved them into another document) and update links
    // from glyphs to SVG documents
    cleanupDocEntries ();

    // Generate lists of first/last glyphs in continuous ranged, pointing
    // to the same document. We have to do that ad hoc, as several ranges
    // may correspond to the same document
    std::vector<uint16_t> start_ids;
    std::vector<uint16_t> end_ids;
    start_ids.reserve (m_glyphs.size ());
    end_ids.reserve (m_glyphs.size ());

    for (size_t i=0; i<m_docIdx.size (); i++) {
	if (m_docIdx[i] >= 0) {
	    start_ids.push_back (i);
	    while (i+1<m_docIdx.size () && m_docIdx[i+1] == m_docIdx[i])
		i++;
	    end_ids.push_back (i);
	}
    }

    // Now output the table
    // table version
    os << (uint16_t) 0;
    // offset to SVG Document list
    m_offsetToSVGDocIndex = 10;
    os << m_offsetToSVGDocIndex;
    // reserved
    os << (uint32_t) 0;
    // num index entries
    os << (uint16_t) start_ids.size ();
    // Document index entries (place for offsets reserved)
    for (size_t i=0; i<start_ids.size (); i++) {
	os << (uint16_t) start_ids[i];
	os << (uint16_t) end_ids[i];
	os << (uint32_t) 0;
	os << (uint32_t) 0;
    }

    // Output SVG documents themselves. Only changed documents are regenerated,
    // while others are copied from the existing table data
    for (auto &ie : m_iEntries) {
	uint32_t doc_off = buf.pos ();
	if (ie.changed) {
	    ConicGlyph *g = m_glyphs[ie.glyphs.front ()];
	    std::stringstream ss;
	    std::set<uint16_t> processed_refs;
	    ss.imbue (std::locale::classic ());
	    ss << std::fixed << std::setprecision (2);
	    g->svgDumpHeader (ss, false);
	    dumpGradients (ss, ie);
	    for (uint16_t gid : ie.glyphs) {
		g = m_glyphs[gid];
		g->svgDumpGlyph (ss, processed_refs, 0);
	    }
	    ss << "</svg>\n";
	    os.writeRawData (ss.str ().c_str (), ss.str ().length ());
	} else {
	    os.writeRawData (data + m_offsetToSVGDocIndex + ie.svgDocOffset, ie.svgDocLength);
	}
	ie.svgDocOffset = doc_off - m_offsetToSVGDocIndex;
	ie.svgDocLength = buf.pos () - doc_off;
    }

    // Seek back to the SVG document index and output the correct offset
    // and length for each document
    buf.seek (m_offsetToSVGDocIndex + 2);
    for (size_t i=0; i<start_ids.size (); i++) {
	auto &ie = m_iEntries[m_docIdx[start_ids[i]]];
	os.skipRawData (4);
	os << ie.svgDocOffset;
	os << ie.svgDocLength;
    }

    buf.close ();
    changed = false;
    td_changed = true;
    start = 0xffffffff;

    newlen = ba.length ();
    delete[] data;
    data = new char[newlen];
    std::copy (ba.begin (), ba.end (), data);
}

static bool is_compressed (char *data, int off) {
    return
	((uint8_t) data[off] == 0x1F &&
	 (uint8_t) data[off+1] == 0x8B &&
	 (uint8_t) data[off+2] == 0x08);
}

std::string SvgTable::getSvgDocument (uint16_t gid) {
    if (!data || gid > m_docIdx.size () || m_docIdx[gid] < 0)
        return nullptr;

    auto &entry = m_iEntries[m_docIdx[gid]];
    if (is_compressed (data, m_offsetToSVGDocIndex + entry.svgDocOffset)) {
	std::ostringstream ss;
	boost::iostreams::array_source src
	    (data + m_offsetToSVGDocIndex + entry.svgDocOffset, entry.svgDocLength);
	boost::iostreams::filtering_istream buf;
	buf.push (boost::iostreams::gzip_decompressor ());
	buf.push (src);
	ss << buf.rdbuf ();
	return ss.str ();
    } else {
	auto ret = std::string (data + m_offsetToSVGDocIndex + entry.svgDocOffset, entry.svgDocLength);
	return ret;
    }
}

bool SvgTable::loadGlyphDocument (ConicGlyph *g, std::istream &buf, svg_document_index_entry &entry) {
    pugi::xml_parse_result result = entry.doc.load (buf);

    if (result.status != pugi::status_ok) {
	buf.seekg (0);
	std::string s (std::istreambuf_iterator<char> (buf), {});
	std::cerr << s << std::endl;
        FontShepherd::postError (
            QCoreApplication::tr ("Bad glyf data"),
            QCoreApplication::tr (
                "Could not load SVG data for glyph %1: "
                "doesn't seem to be an SVG document").arg (g->gid ()),
            nullptr);
        return false;
    }
    return g->fromSVG (entry.doc);
}

ConicGlyph* SvgTable::glyph (sFont* fnt, uint16_t gid) {
    if (!m_usable || gid >= m_glyphs.size ())
	return nullptr;
    if (m_glyphs[gid])
        return m_glyphs[gid];

    if (m_docIdx[gid] < 0)
	return nullptr;
    auto &entry = m_iEntries[m_docIdx[gid]];

    BaseMetrics gm = {fnt->units_per_em, fnt->ascent, fnt->descent};
    ConicGlyph *g = glyph_pool.construct (gid, gm);
    m_glyphs[gid] = g;

    if (m_hmtx)
        g->setHMetrics (m_hmtx->lsb (gid), m_hmtx->aw (gid));
    if (entry.loaded) {
	g->fromSVG (entry.doc);
    } else if (is_compressed (data, m_offsetToSVGDocIndex + entry.svgDocOffset)) {
	boost::iostreams::array_source src
	    (data + m_offsetToSVGDocIndex + entry.svgDocOffset, entry.svgDocLength);
	boost::iostreams::filtering_istream buf;
	buf.push (boost::iostreams::gzip_decompressor ());
	buf.push (src);
	entry.loaded = loadGlyphDocument (g, buf, entry);
    } else {
	BoostIn buf (data + m_offsetToSVGDocIndex + entry.svgDocOffset, entry.svgDocLength);
	entry.loaded = loadGlyphDocument (g, buf, entry);
    }
    return g;
}

void SvgTable::addGlyphAt (sFont* fnt, uint16_t gid) {
    BaseMetrics gm = {fnt->units_per_em, fnt->ascent, fnt->descent};
    ConicGlyph *g = glyph_pool.construct (gid, gm);
    g->setAdvanceWidth (fnt->units_per_em/3);
    g->setOutlinesType (OutlinesType::SVG);
    if (gid < m_glyphs.size ())
	m_glyphs[gid] = g;
    else {
	m_glyphs.push_back (g);
	m_docIdx.push_back (-1);
    }
    // Create a dummy document entry (no offset and length fields yet).
    // We need this, as the SVG table output algorithm assumes each glyph
    // is stored in an SVG document
    m_docIdx[gid] = m_iEntries.size ();
    m_iEntries.emplace_back ();
    auto &ie = m_iEntries.back ();
    ie.changed = true;
    ie.glyphs.push_back (gid);
}

uint16_t SvgTable::addGlyph (sFont* fnt, uint8_t) {
    uint16_t gid = m_glyphs.size ();
    addGlyphAt (fnt, gid);
    return (gid);
}

bool SvgTable::hasGlyph (uint16_t gid) const {
    return (
	m_usable && gid < m_glyphs.size () &&
	(m_glyphs[gid] || m_docIdx[gid] >= 0));
}

void SvgTable::clearGlyph (uint16_t gid) {
    if (gid < m_glyphs.size ()) {
	ConicGlyph *g = m_glyphs[gid];
	if (g) {
	    glyph_pool.free (g);
	    m_glyphs[gid] = nullptr;
	}
	auto &glist = m_iEntries[m_docIdx[gid]].glyphs;
	glist.erase (
	    std::remove_if (
		glist.begin (),
		glist.end (),
		[gid](uint16_t idx){return idx = gid;}
	    ), glist.end ()
	);
	m_docIdx[gid] = -1;
    }
}

bool SvgTable::usable () const {
    return m_usable;
}
