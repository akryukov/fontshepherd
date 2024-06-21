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

#include <cstring>
#include <sstream>
#include <iostream>
#include <ios>
#include <assert.h>
#include <iconv.h>

#include "sfnt.h"
#include "editors/cmapedit.h"
#include "tables/cmap.h"
#include "tables/glyphnames.h"
#include "fs_notify.h"
#include "exceptions.h"
#include "commonlists.h"

static int rcomp_mappings_by_code  (const struct enc_mapping m1, const struct enc_mapping m2) {
    return (m1.code < m2.code);
}

#if 0
static int rcomp_by_code  (const struct enc_range r1, const struct enc_range r2) {
    return (r1.first_enc < r2.first_enc);
}
#endif

CmapTable::CmapTable (sfntFile *fontfile, TableHeader &props) :
    FontTable (fontfile, props),
    m_tables_changed (false),
    m_subtables_changed (false) {
}

CmapTable::~CmapTable () {
    if (tv) {
        tv->close ();
        tv = nullptr;
    }
}

void CmapTable::unpackData (sFont *font) {
    uint32_t cur, i, j, k;
    uint16_t slen;
    uint16_t segCount;
    uint16_t index;
    CmapEnc *enc;
    uint32_t fpos;

    if (td_loaded)
        return;
    this->fillup ();

    m_version = this->getushort (0);
    uint16_t tab_cnt = this->getushort (2);
    fpos = 4;
    for (i=0; i<tab_cnt; ++i) {
	uint16_t platform = this->getushort (fpos); fpos += 2;
	uint16_t specific = this->getushort (fpos); fpos += 2;
	uint32_t offset = this->getlong (fpos); fpos += 4;
	CmapEncTable *ctab = new CmapEncTable (platform, specific, offset);
        cmap_tables.push_back (std::unique_ptr<CmapEncTable> (ctab));
    }

    /* read in each encoding table (presuming we understand it) */
    for (cur=0; cur<tab_cnt; cur++) {
        CmapEncTable *ctabptr = cmap_tables[cur].get ();
	for (i=0; i<cmap_subtables.size () && !ctabptr->subtable (); i++) {
	    CmapEnc *tenc = cmap_subtables[i].get ();
	    if (ctabptr->offset () == tenc->offset ())
		ctabptr->setSubTable (tenc);
	}
	if (ctabptr->subtable ()) continue;

	enc = new CmapEnc (ctabptr->platform (), ctabptr->specific (), this);
	cmap_subtables.push_back (std::unique_ptr<CmapEnc> (enc));
	ctabptr->setSubTable (enc);

	fpos = ctabptr->offset ();
	enc->setOffset (ctabptr->offset ());
	enc->setFormat (this->getushort (fpos)); fpos += 2;
        if (enc->format () >=8) {
            // formats 8.0, 10.0, 12.0 and 13.0 all start from a 32-bit fixed,
            // but the decimal portion is currently always 0
            if (enc->format () <= 13) fpos += 2;
            enc->setLength (this->getlong (fpos)); fpos += 4;
	    if (enc->format () != 14) {
		enc->setLanguage (this->getlong (fpos)); fpos += 4;
	    }
        } else {
            enc->setLength (this->getushort (fpos)); fpos += 2;
            enc->setLanguage (this->getushort (fpos)); fpos += 2;
        }

	switch (enc->format ()) {
          case 0:
            {
                for (i=0; i<256; ++i)
                    enc->addMapping (i, (uint8_t) this->data[fpos++]);
            }
            break;

          case 2:
	    {
                uint16_t table[256];
                unsigned int max_sub_head_key = 0, cnt;
                int last;
                std::vector<struct subhead> subheads;

                for (i=0; i<256; ++i) {
                    table[i] = this->getushort (fpos)/8; fpos+=2;	/* Sub-header keys */
                    if (table[i]>max_sub_head_key)
                        max_sub_head_key = table[i];	/* The entry is a byte pointer, I want a pointer in units of struct subheader */
                }
                subheads.resize (max_sub_head_key+1);
                for (i=0; i<=max_sub_head_key; ++i) {
                    subheads[i].first = this->getushort (fpos); fpos+=2;
                    subheads[i].cnt = this->getushort (fpos); fpos+=2;
                    subheads[i].delta = this->getushort(fpos); fpos+=2;
                    subheads[i].rangeoff =  (this->getushort (fpos)-
					    (max_sub_head_key-i)*sizeof (struct subhead)-2);
                    fpos += 2;
                }
                cnt = (enc->length () - (fpos - ctabptr->offset ()));
                /* GWW: The count is the number of glyph indexes to read. it is the */
                /*  length of the entire subtable minus that bit we've read so far */

                last = -1;
                for (i=0; i<256; ++i) {
                    if (table[i]==0) {
                        /* GWW: Special case, single byte encoding entry, look it up in */
                        /*  subhead */
                        /* In the one example I've got of this encoding (wcl-02.ttf) the chars */
                        /* 0xfd, 0xfe, 0xff are said to exist but there is no mapping */
                        /* for them. */
                        if (last!=-1)
                            index = 0;	/* the subhead says there are 256 entries, but in fact there are only 193, so attempting to find these guys should give an error */
                        else if (i<subheads[0].first || i>=subheads[0].first+subheads[0].cnt ||
                                subheads[0].rangeoff+(i-subheads[0].first)>=cnt)
                            index = 0;
                        else if ((index = getushort (fpos + subheads[0].rangeoff+(i-subheads[0].first)*2))!= 0)
                            index = (uint32_t) (index+subheads[0].delta);
                        /* I assume the single byte codes are just ascii or latin1*/
                        if (index!=0 && index<font->glyph_cnt) {
                            enc->addMapping (i, index, 1);
                        }
                    } else {
                        int k = table[i];
                        for (j=0; j<subheads[k].cnt; ++j) {
                            if (subheads[k].rangeoff+j>=cnt)
                                index = 0;
                            else if ((index = getushort (fpos + subheads[k].rangeoff+j*2))!= 0)
                                index = (uint16_t) (index+subheads[k].delta);
                            if (index!=0 && index<font->glyph_cnt) {
                                enc->addMapping ((i<<8)|(j+subheads[k].first), index, 1);
                            }
                        }
                        if (last==-1) last = i;
                    }
                }
            }
            break;

          case 4:
	    {
		std::vector<uint16_t> glyphs;
		std::vector<struct enc_range4> ranges;

		segCount = this->getushort (fpos)/2; fpos+=2;
                /* searchRange = */ this->getushort (fpos); fpos+=2;
                /* entrySelector = */ this->getushort (fpos); fpos+=2;
                /* rangeShift = */ this->getushort (fpos); fpos+=2;
                ranges.resize (segCount);
                for (i=0; i<segCount; ++i) {
		    ranges[i].end_code = this->getushort (fpos); fpos+=2;
                }
                if (this->getushort (fpos)!=0 )
                    fprintf (stderr, "Expected 0 in true type font\n");
                fpos += 2;
                for (i=0; i<segCount; ++i) {
                    ranges[i].start_code = this->getushort (fpos); fpos+=2;
                }
                for (i=0; i<segCount; ++i) {
                    ranges[i].id_delta = this->getushort (fpos); fpos+=2;
                }
                for (i=0; i<segCount; ++i) {
                    ranges[i].id_range_off = this->getushort (fpos); fpos+=2;
                }
                slen = enc->length () - 16 - segCount*8;
                /* that's the amount of space left in the subtable and it must */
                /*  be filled with glyphIDs */
                glyphs.reserve (slen);
                for (i=0; i<slen/2; ++i) {
                    glyphs.push_back (this->getushort (fpos)); fpos+=2;
                }
                for (i=0; i<segCount; ++i) {
                    if (ranges[i].id_range_off==0 && ranges[i].start_code==0xffff)
                        /* Done */;
                    else if (ranges[i].id_range_off==0)
                        enc->addMapping (ranges[i].start_code,
			    (uint16_t) (ranges[i].start_code+ranges[i].id_delta),
			    ranges[i].end_code-ranges[i].start_code+1);
                    else if (ranges[i].id_range_off!=0xffff) {
                        /* It isn't explicitly mentioned by a rangeOffset of 0xffff*/
                        /*  means no glyph */
                        for (j=ranges[i].start_code; j<=ranges[i].end_code; ++j) {
                            index = glyphs[ (i-segCount+ranges[i].id_range_off/2) + j-ranges[i].start_code ];
                            if (index!=0) {
                                index = (uint16_t) (index+ranges[i].id_delta);
                                if (index>=font->glyph_cnt) {
                                    FontShepherd::postWarning (
                                        QCoreApplication::tr ("Bad index"),
                                        QCoreApplication::tr ("Bad glyph index in a CMAP "
                                            "subtable format 4: 0x%1").arg (index, 4, 16, QLatin1Char('0')),
                                        container->parent ());
                                    break;
                                    /* Actually MS uses this in kaiu.ttf to mean */
                                    /*  notdef */;
                                } else {
                                    enc->addMapping (j, index, 1);
                                }
                            }
                        }
                    }
                }
            }
            break;

          case 6:
	    {
                /* For contiguous ranges of codes, such as in 8-bit encodings */
                uint16_t first = this->getushort (fpos); fpos+=2;
                uint16_t count = this->getushort (fpos); fpos+=2;
                for (i=0; i<count; ++i) {
                    j = this->getushort (fpos); fpos+=2;
                    enc->addMapping (first+i, j);
                }
            }
            break;

          case 8:
            FontShepherd::postWarning (
                QCoreApplication::tr ("Unsupported CMAP format"),
                QCoreApplication::tr ("Warning: CMAP subtable format 8 is currently "
                    "not supported (too badly described in the spec)."),
                container->parent ());
            break;

          case 10:
	    {
                uint32_t first, count;
                first = this->getlong (fpos); fpos+=4;
                count = this->getlong (fpos); fpos+=4;
                for (i=0; i<count; ++i) {
                    j = this->getlong (fpos); fpos+=4;
                    enc->addMapping (first+i, j, 1);
                }
            }
            break;

          case 12:
          case 13:
            {
                uint32_t ngroups = this->getlong (fpos); fpos+=4;
                for (i=0; i<ngroups; i++) {
                    uint32_t start = this->getlong (fpos); fpos+=4;
                    uint32_t end = this->getlong (fpos); fpos+=4;
                    uint32_t startgc = this->getlong (fpos); fpos+=4;
                    enc->addMapping (start, startgc, end - start + 1);
                }
            }
            break;

          case 14: // Variation sequences
	    {
		uint16_t count = this->getlong (fpos); fpos += 4;
		enc->var_selectors.resize (count);
		for (i=0; i<count; i++) {
		    enc->var_selectors[i] = std::unique_ptr<VarSelRecord> (new VarSelRecord ());
		    enc->var_selectors[i]->selector = this->get3bytes (fpos); fpos += 3;
		    enc->var_selectors[i]->default_offset = this->getlong (fpos); fpos += 4;
		    enc->var_selectors[i]->non_default_offset = this->getlong (fpos); fpos += 4;
		}
		for (i=0; i<count; i++) {
		    var_selector_record *cur = enc->var_selectors[i].get ();
		    if (cur->default_offset) {
			fpos = enc->offset () + cur->default_offset;
			uint32_t num_ranges = this->getlong (fpos); fpos +=4;
			cur->default_vars.resize (num_ranges);
			for (j=0; j<num_ranges; j++) {
			    uint32_t start_uni = this->get3bytes (fpos); fpos += 3;
			    uint8_t add_count = (uint8_t) this->data[fpos++];
			    for (k=0; k<=add_count; k++)
				cur->default_vars[j] = start_uni + k;
			}
		    }

		    if (cur->non_default_offset) {
			fpos = enc->offset () + cur->non_default_offset;
			uint32_t num_ranges = this->getlong (fpos); fpos +=4;
			cur->non_default_vars.resize (num_ranges);
			for (j=0; j<num_ranges; j++) {
			    cur->non_default_vars[j].code = this->get3bytes (fpos); fpos += 3;
			    cur->non_default_vars[j].gid = this->getushort (fpos); fpos += 2;
			}
		    }
		}
	    }
            break;

          default:
            FontShepherd::postWarning (
                QCoreApplication::tr ("Unknown CMAP format"),
                QCoreApplication::tr ("Warning: got an unknown "
                    "CMAP subtable format (%1).").arg (enc->format ()),
                container->parent ());
	}
    }

    sortSubTables ();
    td_loaded = true;
}

void CmapTable::findBestSubTable (sFont *font) {
    uint16_t bestval=0;
    CmapEnc *best=nullptr;

    /* Find the best table we can */
    for (auto &encptr : cmap_subtables) {
        CmapEnc *enc = encptr.get ();

        if (!enc->isUnicode () && !enc->hasConverter ())
	    /* Can't parse, unusable */;
	else if (enc->format () == 14)
	    /* Unicode variation selectors -- useless for our task */;
	else if (enc->isUnicode () && enc->numBits () == 32 && bestval < 4) {
	    // Prefer 32 bit Unicode if available
            best = enc;
            bestval = 4;
	} else if (enc->isUnicode () && enc->numBits () == 16 && bestval < 3) {
	    // 16 bit Unicode
            best = enc;
            bestval = 3;
	} else if (enc->numBits () == 16 && bestval < 2) {
	    // If there is no Unicode, take 16 bit CJK
	    best = enc;
	    bestval = 2;
	} else if (enc->numBits () == 8 && bestval < 1) {
	    // Mac 8 bit otherwise
	    best = enc;
	    bestval = 1;
	}
    }
    best->setCurrent (true);
    font->enc = best;
}

void CmapTable::clearMappingsForGid (uint16_t gid) {
    for (auto &enc: cmap_subtables)
	changed |= enc->deleteMappingsForGid (gid);
}

void CmapTable::addCommonMapping (uint32_t uni, uint16_t gid) {
    for (auto &enc: cmap_subtables)
	changed |= enc->insertUniMapping (uni, gid);
}

void CmapTable::encodeFormat0 (std::ostream &os, CmapEnc *enc) {
    int i;

    putushort (os, (uint16_t) 0);		// format
    putushort (os, (uint16_t) (3*2 + 256));
    putushort (os, enc->language ());		// language/version

    for (i=0; i<256; i++)
	os.put ((uint8_t) enc->map[i]);
}

void CmapTable::encodeFormat2 (std::ostream &os, CmapEnc *enc) {
    uint16_t i, j, k, last_subh=0, subhead_idx=0;
    uint32_t start_pos, end_pos;
    int plane0_min = -1, plane0_max = -1;
    int plane_min = -1, plane_max = -1;
    int base = -1, bound = -1;
    uint16_t plane0_size, plane_size, subhead_cnt=0;
    uint16_t table[256] = { 0 };
    bool single[256] = { false };
    bool double_first[256] = { false };
    bool double_secnd[256] = { false };
    std::vector<struct subhead> subheads;
    std::vector<uint16_t> glyphs;

    // first we need to know which values for first and second bytes are possible
    for (i=0; i<enc->count (); i++) {
	uint16_t code = enc->encByPos (i);
        uint8_t first = (code>>8);
        uint8_t secnd = code&0xff;
	if (first == 0) {
	    single[secnd] = true;
	} else {
	    double_first[first] = true;
	    double_secnd[secnd] = true;
	}
    }

    /* Make sure no byte value is used both to encode a single character
     * and to signal the first byte of a 2-byte character.
     * Then determine the range of possible values for the first and second
     * bytes for single byte and 2-byte characters */
    for (i=0; i<256; i++) {
	if (single[i] && double_first[i]) {
            FontShepherd::postError (
                QCoreApplication::tr ("Can't compile table"),
                QCoreApplication::tr (
		    "Can't compile cmap subtable format 2: data not suitable for this format"),
                container->parent ());
	    return;
	}
	if (single[i] && plane0_min < 0)
	    plane0_min = i;
	if (single[i] && (i > plane0_max))
	    plane0_max = i;
	if (double_secnd[i] && plane_min < 0)
	    plane_min = i;
	if (double_secnd[i] && (i > plane_max))
	    plane_max = i;
	if (double_first[i] && base < 0)
	    base = i;
	if (double_first[i] && (i > bound))
	    bound = i;
    }
    plane_size = plane_max - plane_min + 1;
    /* In CJK fonts I have seen the length of the first (single byte) plane
     * is just set to 0x100. Nevertheless for now I follow FontForge's
     * alorithm which calculates the real count of single byte characters */
    plane0_size = (plane0_max > plane0_min) ? plane0_max - plane0_min + 1 : 0;

    // prepare SubHeader keys table
    for (i=base; i<=bound; i++) {
	if (double_first[base+i]) {
	    table[i] = 8*(i-base+1);
	    subhead_cnt++;
	}
    }

    // prepage array of SubHeader records
    glyphs.reserve (subhead_cnt*plane_size + plane0_size);
    subheads.resize (subhead_cnt, {0, 0, 0, 0});
    subheads[0].first = 0;
    subheads[0].cnt = plane0_size;
    for (i=1; i<=subhead_cnt; ++i) {
	subheads[i].first = plane_min;
	subheads[i].cnt = plane_size;
    }

    // put single byte glyphs to the list
    if (plane0_size) {
	for (i=plane0_min; i<=plane0_max; ++i) {
	    int gid = enc->gidByEnc (i);
	    if (gid > 0)
		glyphs.push_back ((uint16_t) gid);
	    else
		glyphs.push_back (0);
	}
	subhead_idx = 1;
    }

    // proceed to 2-byte glyphs
    for (i=base; i<=bound; i++) if (double_first[i]) {
	std::vector<uint16_t> temp_glyphs;
	for (j=plane_min; j<=plane_max; j++) {
	    int gid = enc->gidByEnc ((i<<8) + j);
	    if (gid > 0)
		temp_glyphs.push_back ((uint16_t) gid);
	    else
		temp_glyphs.push_back (0);
	}
	for (j=0; j<last_subh; j++) {
	    uint16_t off = plane0_size + j*plane_size;
	    uint16_t delta = 0;
	    for (k=0; k<plane_size; k++) {
		if (temp_glyphs[k]==0 && glyphs[off+k]==0)
		    /* Still matches */;
		else if (temp_glyphs[k]==0 || glyphs[off+k]==0)
		    break;  /* Doesn't match */
		else if (delta==0)
		    delta = temp_glyphs[k] - glyphs[off+k];
		else if (temp_glyphs[k] == glyphs[off+k]+delta)
		    /* Still matches */;
		else
		    break;
	    }

	    if (k==plane_size) {
		subheads[subhead_idx].delta = delta;
		subheads[subhead_idx].rangeoff = off;
		break;
	    }
	}
	if (subheads[subhead_idx].rangeoff==0) {
	    subheads[subhead_idx].rangeoff = glyphs.size ();
	    glyphs.insert (glyphs.end (), temp_glyphs.begin (), temp_glyphs.end ());
	    last_subh++;
	}
	subhead_idx++;
    }

    /* fixup offsets
     * GWW: my rangeoffsets are indexes into the glyph array. That's nice and
     *  simple. Unfortunately ttf says they are offsets from the current
     *  location in the file (sort of) so we now fix them up. */
    for (i=0; i<=subhead_cnt; ++i) {
	subheads[i].rangeoff = subheads[i].rangeoff*sizeof(uint16_t) +
		(subhead_cnt-i)*sizeof(struct subhead) + sizeof(uint16_t);
    }

    // Now proceed to filling the table
    start_pos = os.tellp ();
    putushort (os, 2);		/* 8/16 format */
    putushort (os, 0);		/* Subtable length, we'll come back and fix this */
    putushort (os, enc->language ());
    for (i=0; i<256; ++i)
	putushort (os, table[i]);
    for (i=0; i<=subhead_cnt; i++) {
	putushort (os, subheads[i].first);
	putushort (os, subheads[i].cnt);
	putushort (os, subheads[i].delta);
	putushort (os, subheads[i].rangeoff);
    }
    for (i=0; i<glyphs.size (); ++i )
	putushort (os, glyphs[i]);

    // Fixup subtable length
    end_pos = os.tellp ();
    os.seekp (start_pos + 2);
    putushort (os, end_pos - start_pos);
    os.seekp (end_pos);
}

void CmapTable::encodeFormat4 (std::ostream &os, CmapEnc *enc) {
    struct enc_range seg, prevseg;
    uint32_t segcnt=0, gidcnt=0;
    uint16_t i, j;
    std::vector<struct enc_range4> ranges;
    std::vector<uint16_t> gids;

    enc->segments.clear ();
    enc->segments.reserve (enc->mappings.size ());
    seg.first_enc = enc->mappings[0].code;
    seg.first_gid = enc->mappings[0].gid;
    seg.length = 1;
    for (i=1; i<enc->mappings.size (); i++) {
	struct enc_mapping *em = &enc->mappings[i];
	if (em->code == seg.first_enc + seg.length && em->gid == seg.first_gid + seg.length) {
	    seg.length++;
	} else {
	    enc->segments.push_back (seg);
	    seg.first_enc = enc->mappings[i].code;
	    seg.first_gid = enc->mappings[i].gid;
	    seg.length = 1;
	}
    }
    enc->segments.push_back (seg);
    // create a dummy segment to mark the end of the table
    seg.first_enc = 0xffff;
    seg.first_gid = 0;
    seg.length = 1;
    enc->segments.push_back (seg);

    ranges.reserve (enc->segments.size ());
    gids.reserve (enc->count ());
    struct enc_range4 rng;
    rng.start_code = enc->segments[0].first_enc;
    rng.end_code = enc->segments[0].first_enc + enc->segments[0].length - 1;
    rng.id_delta = enc->segments[0].first_gid - enc->segments[0].first_enc;
    rng.id_range_off = 0;
    for (i=1; i<enc->segments.size (); i++) {
	prevseg = enc->segments[i-1];
	seg = enc->segments[i];
	if (seg.first_enc == prevseg.first_enc + prevseg.length && seg.first_enc < 0xffff) {
	    for (j=0; j<prevseg.length; j++)
		gids.push_back (prevseg.first_gid + j);
	    rng.end_code = seg.first_enc + seg.length - 1;
	    rng.id_delta = 0;
	} else {
	    if (!rng.id_delta) {
		for (j=0; j<prevseg.length; j++)
		    gids.push_back (prevseg.first_gid + j);
	    } else {
		rng.id_range_off = 0;
	    }
	    ranges.push_back (rng);
	    rng.start_code = seg.first_enc;
	    rng.end_code = seg.first_enc + seg.length - 1;
	    rng.id_delta = seg.first_gid - seg.first_enc;
	    rng.id_range_off = (gids.size () - ranges.size ()) * sizeof (uint16_t);
	}
    }
    // Finalize the last dummy range
    rng.id_range_off = 0;
    ranges.push_back (rng);

    segcnt = ranges.size ();
    gidcnt = gids.size ();
    for (i=0; i<segcnt; i++) {
	if (!ranges[i].id_delta)
	    ranges[i].id_range_off += segcnt * sizeof (uint16_t);
    }

    putushort (os, (uint16_t) 4);		// format
    putushort (os, (uint16_t) ((8 + 4*segcnt+gidcnt) * sizeof (uint16_t)));
    putushort (os, enc->language ());		// language/version
    putushort (os, 2*segcnt);			// segCountX2
    for (j=0,i=1; i<=segcnt; i<<=1, ++j);
    putushort (os, (uint16_t) i);		// searchRange: 2*2^floor(log2(segcnt))
    putushort (os, (uint16_t) (j-1));		// entrySelector: log2(searchRange/2)
    putushort (os, (uint16_t) (2*segcnt - i));	// rangeShift: 2 * segCount - searchRange
    for (i=0; i<segcnt; i++)
	putushort (os, ranges[i].end_code);
    putushort (os, (uint16_t) 0);		// reservedPad: 0
    for (i=0; i<segcnt; i++)
	putushort (os, ranges[i].start_code);
    for (i=0; i<segcnt; i++)
	putushort (os, ranges[i].id_delta);
    for (i=0; i<segcnt; i++)
	putushort (os, ranges[i].id_range_off);
    for (i=0; i<gidcnt; i++)
	putushort (os, gids[i]);
}

void CmapTable::encodeFormat6 (std::ostream &os, CmapEnc *enc) {
    int i;
    uint16_t entry_count = 0, len, first_code = 0;

    first_code = enc->mappings[0].code;
    entry_count = enc->count ();
    len = (entry_count + 5) * sizeof (uint16_t);

    putushort (os, (uint16_t) 6);		// format
    putushort (os, len);
    putushort (os, enc->language ());		// language/version
    putushort (os, first_code);
    putushort (os, entry_count);

    for (i=first_code; i < first_code+entry_count; i++)
        putushort (os, enc->mappings[i].gid);
}

void CmapTable::encodeFormat10 (std::ostream &os, CmapEnc *enc) {
    uint32_t i;
    uint32_t start_char_code = enc->mappings[0].code;
    uint32_t num_chars = enc->count ();
    uint32_t length = 2*sizeof (uint16_t) + 4*sizeof (uint32_t) + num_chars*sizeof (uint16_t);

    putushort (os, (uint16_t) 10);		// format
    putushort (os, 0);				// reserved
    putlong (os, length);
    putlong (os, enc->language ());		// language/version
    putlong (os, start_char_code);
    putlong (os, num_chars);

    for (i=0; i<length; i++)
	putushort (os, enc->mappings[i].gid);
}

void CmapTable::encodeFormat12 (std::ostream &os, CmapEnc *enc, bool many_to_one) {
    uint16_t format = many_to_one ? 13 : 12;
    uint32_t length, num_groups;
    uint32_t i;
    struct enc_range seg;

    enc->segments.clear ();
    enc->segments.reserve (enc->mappings.size ());
    seg.first_enc = enc->mappings[0].code;
    seg.first_gid = enc->mappings[0].gid;
    seg.length = 1;
    for (i=1; i<enc->mappings.size (); i++) {
	struct enc_mapping *em = &enc->mappings[i];
	if (em->code == seg.first_enc + seg.length && em->gid == seg.first_gid + seg.length) {
	    seg.length++;
	} else {
	    enc->segments.push_back (seg);
	    seg.first_enc = enc->mappings[i].code;
	    seg.first_gid = enc->mappings[i].gid;
	    seg.length = 1;
	}
    }
    enc->segments.push_back (seg);

    num_groups = enc->segments.size ();
    length = (2*sizeof (uint16_t)) + (3*sizeof (uint32_t)) + (num_groups*3*sizeof (uint32_t));

    putushort (os, format);		// format
    putushort (os, 0);			// reserved
    putlong (os, length);
    putlong (os, enc->language ());	// language/version
    putlong (os, num_groups);

    for (i=0; i<num_groups; i++) {
	seg = enc->segments[i];
	putlong (os, seg.first_enc);
	putlong (os, seg.first_enc + seg.length - 1);
	putlong (os, seg.first_gid);
    }
}

void CmapTable::encodeFormat14 (std::ostream &os, CmapEnc *enc) {
    uint32_t i, j;
    uint32_t num_records = enc->count (), off;
    uint32_t start_pos, end_pos;
    struct vsr_range rng;
    VarSelRecord *vsr;

    for (i=0; i<num_records; i++) {
        vsr = enc->var_selectors[i].get ();
        vsr->default_ranges.clear ();
        vsr->default_ranges.reserve (vsr->default_vars.size ());
	if (vsr->default_vars.size () > 0) {
	    rng.start_uni = vsr->default_vars[0];
	    rng.add_count = 0;
	    for (j=1; j<vsr->default_vars.size (); j++) {
		if (vsr->default_vars[j] == rng.start_uni + rng.add_count + 1) {
		    rng.add_count++;
		} else {
		    vsr->default_ranges.push_back (rng);
		    rng.start_uni = vsr->default_vars[j];
		    rng.add_count = 0;
		}
	    }
	    vsr->default_ranges.push_back (rng);
	}
    }


    start_pos = os.tellp ();
    putushort (os, 14);			// format
    putlong (os, 0);			// Byte length of this subtable (to be filled later)
    putlong (os, num_records);		// numVarSelectorRecords

    off = sizeof (uint16_t) + 2*sizeof (uint32_t);  // Table header size
    off += num_records*(3 + 2*sizeof (uint32_t));   // size of VariationSelector records array
    for (i=0; i<num_records; i++) {
        vsr = enc->var_selectors[i].get ();
	uint32_t num_dflt = vsr->default_ranges.size ();
	uint32_t num_non_dflt = vsr->non_default_vars.size ();
	put3bytes (os, vsr->selector);
	putlong (os, num_dflt ? off : 0);		// defaultUVSOffset
	if (num_dflt)
	    off += (sizeof (uint32_t) + num_dflt * 4);
	putlong (os, num_non_dflt ? off : 0);		// nonDefaultUVSOffset
	if (num_non_dflt)
	    off += (sizeof (uint32_t) + num_non_dflt * 5);
    }

    for (i=0; i<num_records; i++) {
	vsr = enc->var_selectors[i].get ();
	uint32_t num_dflt = vsr->default_ranges.size ();
	if (num_dflt) {
	    putlong (os, num_dflt);	    // Number of UVS Mappings that follow
	    for (j=0; j<num_dflt; j++) {
		rng = vsr->default_ranges[j];
		put3bytes (os, rng.start_uni);
		os.put (rng.add_count);
	    }
	}
	uint32_t num_non_dflt = vsr->non_default_vars.size ();
	if (num_non_dflt) {
	    putlong (os, num_non_dflt);	    // Number of UVS Mappings that follow
	    for (j=0; j<num_non_dflt; j++) {
		enc_mapping m = vsr->non_default_vars[j];
		put3bytes (os, m.code);
		putushort (os, m.gid);
	    }
	}
    }

    end_pos = os.tellp ();
    os.seekp (start_pos + 2);
    putlong (os, end_pos - start_pos);
    os.seekp (end_pos);
}

void CmapTable::packData () {
    int pos;
    std::stringstream s;
    std::string st;

    delete[] data; data = nullptr;
    putushort (s, (uint16_t) 0);
    putushort (s, cmap_tables.size ());
    for (auto &et : cmap_tables) {
	putushort (s, et->platform ());
	putushort (s, et->specific ());
	putlong   (s, (uint32_t) 0); // offset
    }

    for (auto &encptr : cmap_subtables) {
	CmapEnc *enc = encptr.get ();
	pos = s.tellp ();
	// Set offsets at the header of the cmap table
	for (size_t j=0; j<cmap_tables.size (); j++) {
	    if (cmap_tables[j]->subtable () == enc) {
		s.seekp ((2 + j*4 + 2)*sizeof (uint16_t));
		putlong (s, (uint32_t) pos);
	    }
	}
	s.seekp (pos);

	switch (enc->format ()) {
	  case 0:
	    encodeFormat0 (s, enc);
	    break;
	  case 2:
	    encodeFormat2 (s, enc);
	    break;
	  case 4:
	    encodeFormat4 (s, enc);
	    break;
	  case 6:
	    encodeFormat6 (s, enc);
	    break;
	  case 10:
	    encodeFormat10 (s, enc);
	    break;
	  case 12:
	    encodeFormat12 (s, enc, false);
	    break;
	  case 13:
	    encodeFormat12 (s, enc, true);
	    break;
	  case 14:
	    encodeFormat14 (s, enc);
	    break;
	}
	enc->setModified (false);
    }
    td_changed = true;
    changed = false;
    start = 0xffffffff;
    m_tables_changed = false;
    m_subtables_changed = false;
    st = s.str ();
    newlen = st.length ();
    data = new char[newlen];
    std::copy (st.begin (), st.end (), data);
}

uint16_t CmapTable::numTables () {
    return cmap_tables.size ();
}

uint16_t CmapTable::numSubTables () {
    return cmap_subtables.size ();
}

CmapEncTable* CmapTable::getTable (uint16_t idx) {
    if (idx < cmap_tables.size ())
	return cmap_tables[idx].get ();
    return nullptr;
}

bool compareTables (std::unique_ptr<CmapEncTable> &p1, std::unique_ptr<CmapEncTable> &p2) {
    CmapEncTable *t1 = p1.get ();
    CmapEncTable *t2 = p2.get ();
    if (t1->platform () != t2->platform ())
	return (t1->platform () < t2->platform ());
    else if (t1->specific () != t2->specific ())
	return (t1->specific () < t2->specific ());
    else
	return (t1->subtable ()->language () < t2->subtable ()->language ());
}

uint16_t CmapTable::addTable (uint16_t platform, uint16_t specific, CmapEnc *subtable) {
    uint16_t ret=0;
    CmapEncTable *newt = new CmapEncTable (platform, specific, 0);
    newt->setSubTable (subtable);
    cmap_tables.push_back (std::unique_ptr<CmapEncTable> (newt));
    std::sort (cmap_tables.begin(), cmap_tables.end(), compareTables);
    for (size_t i=0; i<cmap_tables.size (); i++) {
	CmapEncTable *test = cmap_tables[i].get ();
	if (test->platform () == platform && test->specific () == specific && test->subtable () == subtable) {
	    ret = i;
	    break;
	}
    }
    return ret;
}

void CmapTable::removeTable (uint16_t idx) {
    if (idx < cmap_tables.size ())
	cmap_tables.erase (cmap_tables.begin () + idx);
}


CmapEnc* CmapTable::getSubTable (uint16_t idx) {
    if (idx < cmap_subtables.size ())
	return cmap_subtables[idx].get ();
    return nullptr;
}

CmapEnc* CmapTable::addSubTable (std::map<std::string, int> &args, const std::string &encoding, GlyphNameProvider *gnp) {
    CmapEnc *newenc = nullptr;
    uint16_t source_subtable_idx = (uint16_t) args["source"];
    CmapEnc *source = (source_subtable_idx < cmap_subtables.size ()) ?
	this->getSubTable (source_subtable_idx) : nullptr;

    if (gnp) {
	// create a temporary Cmap subtable, based on glyph name data
	std::unique_ptr<CmapEnc> temp (new CmapEnc (gnp, dynamic_cast<FontTable*> (this)));
	newenc = new CmapEnc (
	    args, temp.get (), encoding, dynamic_cast<FontTable*> (this));
    } else {
	newenc = new CmapEnc (
	    args, source, encoding, dynamic_cast<FontTable*> (this));
    }
    if (newenc) {
	newenc->setIndex (cmap_subtables.size ());
	cmap_subtables.push_back (std::unique_ptr<CmapEnc> (newenc));
    }

    return newenc;
}

void CmapTable::removeSubTable (uint16_t idx, sFont *font) {
    if (idx < cmap_subtables.size ()) {
	CmapEnc *subtable = this->getSubTable (idx);
	bool was_default = subtable->isCurrent ();

	cmap_subtables.erase (cmap_subtables.begin () + idx);
	for (size_t i=0; i<cmap_subtables.size (); i++)
	    cmap_subtables[i]->setIndex (i);

	if (was_default)
	    findBestSubTable (font);
    }
}

bool compareSubTables (std::unique_ptr<CmapEnc> &ce1, std::unique_ptr<CmapEnc> &ce2) {
    return (ce1->offset () < ce2->offset ());
}

void CmapTable::sortSubTables () {
    std::sort (cmap_subtables.begin(), cmap_subtables.end(), compareSubTables);
    for (size_t i=0; i<cmap_subtables.size (); i++)
	cmap_subtables[i]->setIndex (i);
}

void CmapTable::reorderSubTables (int from, int to) {
    std::iter_swap (cmap_subtables.begin() + from, cmap_subtables.begin() + to);
    for (size_t i=0; i<cmap_subtables.size (); i++)
	cmap_subtables[i]->setIndex (i);
}

bool CmapTable::tablesModified () {
    return m_tables_changed;
}

bool CmapTable::subTablesModified () {
    return m_subtables_changed;
}

void CmapTable::setTablesModified (bool val) {
    m_tables_changed = val;
}

void CmapTable::setSubTablesModified (bool val) {
    m_subtables_changed = val;
}

void CmapTable::edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller) {
    if (data == nullptr)
        fillup ();

    if (tv == nullptr) {
        CmapEdit *cmapedit = new CmapEdit (tptr, fnt, caller);
        tv = cmapedit;
        cmapedit->show ();
    } else {
        tv->raise ();
    }
}

CmapEncTable::CmapEncTable (uint16_t platform, uint16_t specific, uint32_t offset) :
    m_platform (platform), m_specific (specific), m_offset (offset), m_subtable (nullptr) {
}

CmapEncTable::~CmapEncTable () {
}

uint16_t CmapEncTable::platform () {
    return m_platform;
}

std::string CmapEncTable::strPlatform () {
    std::ostringstream s;
    const std::vector<FontShepherd::numbered_string> &lst = FontShepherd::platforms;
    for (size_t i=0; i<lst.size (); i++) {
	if (m_platform == lst[i].ID) {
	    s << lst[i].ID << ": " << lst[i].name;
	    return s.str ();
	}
    }
    s << "Unknown platform: " << m_platform;
    return s.str ();
}

uint16_t CmapEncTable::specific () {
    return m_specific;
}

std::string CmapEncTable::strSpecific () {
    std::ostringstream s;
    const std::vector<FontShepherd::numbered_string> &lst = FontShepherd::specificList (m_platform);
    for (size_t i=0; i<lst.size (); i++) {
	if (m_specific == lst[i].ID) {
	    s << lst[i].ID << ": " << lst[i].name;
	    return s.str ();
	}
    }
    s << "Unknown specific: " << m_specific;
    return s.str ();
}

uint32_t CmapEncTable::offset () {
    return m_offset;
}

void CmapEncTable::setSubTable (CmapEnc *subtable) {
    m_subtable = subtable;
}

CmapEnc* CmapEncTable::subtable () {
    return m_subtable;
}

bool CmapEncTable::isCJK (uint16_t platform, uint16_t specific) {
    switch (platform) {
      case plt_mac:
	switch (specific) {
	  case 1:
	  case 2:
	  case 3:
	  case 25:
	    return true;
	  default:
	    return false;
	}
      case plt_ms:
	switch (specific) {
	  case 2:
	  case 3:
	  case 4:
	  case 5:
	  case 6:
	    return true;
	  default:
	    return false;
	}
    }
    return false;
}

CmapEnc::CmapEnc (uint16_t platID, uint16_t encID, FontTable *tbl) :
    m_length (0), m_format (0), m_language (0), m_current (false), m_changed (false),
    m_lockCounter (0), m_index (0), m_parent (tbl) {
    const char *csname = nullptr;

    switch (platID) {
      case plt_unicode: /* Unicode */
      case plt_iso10646: /* Obsolete ISO 10646 */
        m_charset = em_unicode;
        /* GWW: the various specific values say what version of unicode. I'm not */
        /* keeping track of that (no mapping table of unicode1->3) */
        /* except for CJK it's mostly just extensions */
        break;
      case plt_mac:
        switch (encID) {
          case 0:
            m_charset = mac_roman;
            csname = "MACINTOSH";
            break;
          case 1:
            m_charset = em_shift_jis;
            csname = "SHIFT_JISX0213";
            break;
          case 2:
            m_charset = em_big5;
            csname = "BIG5-HKSCS";
            break;
          case 3:
            m_charset = em_wansung;
            csname = "EUC-KR";
            break;
          /* 4, Arabic */
          /* 5, Hebrew */
          /* 6, Greek */
          case 7:
            m_charset = mac_cyrillic;
            csname = "MAC-CYRILLIC";
            break;
          /* 8, RSymbol */
          /* 9, Devanagari */
          /* 10, Gurmukhi */
          /* 11, Gujarati */
          /* 12, Oriya */
          /* 13, Bengali */
          /* 14, Tamil */
          /* 15, Telugu */
          /* 16, Kannada */
          /* 17, Malayalam */
          /* 18, Sinhalese */
          /* 19, Burmese */
          /* 20, Khmer */
          /* 21, Thai */
          /* 22, Laotian */
          /* 23, Georgian */
          /* 24, Armenian */
          case 25:
            m_charset = em_gbk;
            csname = "GB18030";
            break;
          /* 26, Tibetan */
          /* 27, Mongolian */
          /* 28, Geez */
          /* 29, Slavic */
          /* 30, Vietnamese */
          /* 31, Sindhi */
        }
        break;
      case plt_ms:		/* MS */
        switch (encID) {
          case 0:
            m_charset = em_symbol;
            break;
          case 1:
            m_charset = em_unicode;
            break;
          case 2:
            m_charset = em_shift_jis;
            csname = "SHIFT_JISX0213";
            break;
          case 3:
            m_charset = em_gbk;
            csname = "GB18030";
            break;
          case 4:
            m_charset = em_big5;
            csname = "BIG5-HKSCS";
            break;
          case 5:
            m_charset = em_wansung;
            csname = "EUC-KR";
            break;
          case 6:
            m_charset = em_johab;
            csname = "JOHAB";
            break;
          /* 4byte iso10646 */
          case 10:
            m_charset = em_unicode;
            break;
        }
	break;
      case plt_custom:
        switch (encID) {
          case 161:
            m_charset = ms_greek;
            csname = "WINDOWS-1253";
            break;
          case 162:
            m_charset = ms_turkish;
            csname = "WINDOWS-1254";
            break;
          case 163:
            m_charset = ms_vietnamese;
            csname = "WINDOWS-1258";
            break;
          case 177:
            m_charset = ms_hebrew;
            csname = "WINDOWS-1255";
            break;
          case 178:
            m_charset = ms_arabic;
            csname = "WINDOWS-1256";
            break;
          case 186:
            m_charset = ms_baltic;
            csname = "WINDOWS-1257";
            break;
          case 204:
            m_charset = ms_cyrillic;
            csname = "WINDOWS-1251";
            break;
          case 238:
            m_charset = ms_ce;
            csname = "WINDOWS-1250";
            break;
        }
        break;
    }

    m_codec = m_unicodec = (iconv_t)(-1);
    if (csname) {
        m_codec = iconv_open ("UCS-4BE", csname);
        m_unicodec = iconv_open (csname, "UCS-4BE");
        if (m_codec == (iconv_t)(-1) || m_unicodec == (iconv_t)(-1)) {
            FontShepherd::postWarning (
                QCoreApplication::tr ("Unsupported Encoding"),
                QCoreApplication::tr ("Warning: could not find a suitable converter "
                             "for %1.").arg (csname),
                m_parent->containerFile ()->parent ());
        }
    }
}

CmapEnc::CmapEnc (std::map<std::string, int> &args, CmapEnc *source, const std::string &encoding, FontTable *tbl) :
    m_length (0), m_current (false), m_changed (true),
    m_lockCounter (0), m_index (0), m_parent (tbl) {

    uint16_t i;
    const char *csname = encoding.c_str ();
    uint32_t min_code = args["minimum"], max_code = args["maximum"];

    m_format = (uint16_t) args["format"];
    m_language = (uint16_t) args["language"];
    m_codec = m_unicodec = (iconv_t)(-1);

    if (m_format >= 13) {
	// Return an empty table: no way to fill
	return;
    } else if (!source) {
	// Nothing to do, but in case of a trimmed array subtable format
	// just prepare emty mappings for the entire range
	if (m_format == 6 || m_format == 10) {
	    for  (i=min_code; i<=max_code; i++) {
		this->addMapping (i, 0);
	    }
	}
	return;
    } else if (!encoding.compare ("Unicode")) {
	m_charset = em_unicode;
	mappings.reserve (source->count ());
	if (m_format == 6 || m_format == 10) {
	    for (i=min_code; i<=max_code; i++) {
		uint16_t gid = source->gidByUnicode (i);
		// if gid is zero, still add the mapping
		this->addMapping (i, gid);
	    }
	} else {
	    for (i=0; i<source->count (); i++) {
		uint32_t uni = source->unicodeByPos (i);
		if (uni)
		    this->addMapping (uni, source->gidByPos (i));
	    }
	}
	std::sort (mappings.begin(), mappings.end(), rcomp_mappings_by_code);
    } else if (encoding.length () > 0) {
        m_codec = iconv_open ("UTF-32BE", csname);
        m_unicodec = iconv_open (csname, "UTF-32BE");
	mappings.reserve (source->count ());
        if (m_codec != (iconv_t)(-1) && m_unicodec != (iconv_t)(-1)) {
	    if (m_format == 6) {
		for (i=min_code; i<=max_code; i++) {
		    uint32_t uni = recodeChar (i, true);
		    this->addMapping (i, source->gidByUnicode (uni));
		}
	    } else {
		for (i=0; i<source->count (); i++) {
		    uint32_t code = recodeChar (source->unicodeByPos (i), false);
		    if (code)
			this->addMapping (code, source->gidByPos (i));
		}
	    }
	}
	if (m_format > 0)
	    std::sort (mappings.begin(), mappings.end(), rcomp_mappings_by_code);
    }
}

// Temporary subtable, based on glyph name data. Set format to 12, so that
// 32-bit characters can be included
CmapEnc::CmapEnc (GlyphNameProvider *source, FontTable *tbl) :
    m_length (0), m_format (12), m_current (false), m_changed (true),
    m_lockCounter (0), m_charset (em_unicode), m_index (0), m_parent (tbl) {

    uint16_t i;

    m_codec = m_unicodec = (iconv_t)(-1);

    for (i=0; i<source->countGlyphs (); i++) {
        std::string name = source->nameByGid (i);
        uint32_t uni = source->uniByName (name);
	if (uni>0)
	    this->addMapping (uni, i);
    }
    std::sort (mappings.begin(), mappings.end(), rcomp_mappings_by_code);
}

CmapEnc::~CmapEnc () {
    if (m_codec != (iconv_t)(-1)) iconv_close (m_codec);
    if (m_unicodec != (iconv_t)(-1)) iconv_close (m_unicodec);
}

uint32_t CmapEnc::count () const {
    uint32_t ret;
    switch (m_format) {
      case 0:
	ret = 256;
	break;
      case 13:
	for (auto &seg : segments)
	    ret += seg.length;
	break;
      case 14:
	ret = var_selectors.size ();
	break;
      default:
	ret = mappings.size ();
    }
    return ret;
}

bool CmapEnc::hasConverter () const {
    return (m_codec != nullptr);
}

bool CmapEnc::isUnicode () const {
    return (m_format != 14 && m_charset == em_unicode);
}

uint8_t CmapEnc::numBits () const {
    switch (m_format) {
      case 0:
	return 8;
      case 6:
      case 2:
      case 4:
	return 16;
      case 8:
      case 10:
      case 12:
      case 13:
	return 32;
    }
    return (0);
}

std::string CmapEnc::stringName () const {
    std::stringstream ret;
    ret << m_index;
    ret << ": language ";
    ret << m_language;
    ret << ", format ";
    ret << m_format;
    return ret.str ();
}

void CmapEnc::addMapping (uint32_t enc, uint32_t gid, uint32_t len) {
    if (m_format == 0) {
        assert (enc < 256);
        map[enc] = gid;
    } else if (m_format != 14) {
        struct enc_range seg, last;
	uint32_t i;

        if (m_format == 13) {
	    if (!segments.empty ())
		last = segments.back ();

	    if (!segments.empty () &&
		(enc == last.first_enc + last.length) &&
		(gid == last.first_gid + last.length)) {
		last.length += len;
		segments.back () = last;

	    } else {
		seg.first_enc = enc;
		seg.first_gid = gid;
		seg.length = len;

		segments.push_back (seg);
	    }
	} else {
	    for (i=0; i<len; i++) {
		struct enc_mapping m;
		m.code = enc+i;
		m.gid = gid+i;
		mappings.push_back (m);
	    }
	}
        //std::sort (segments.begin (), segments.end (), rcomp_by_code);
    }
}

bool CmapEnc::deleteMapping (uint32_t code) {
    if (m_format == 14)
	return false;
    size_t map_cnt = mappings.size ();
    mappings.erase (
        std::remove_if (
	    mappings.begin (),
	    mappings.end (),
	    [code](enc_mapping &em){return em.code == code;}
	), mappings.end ()
    );
    return (map_cnt > mappings.size ());
}

bool CmapEnc::deleteMappingsForGid (uint16_t gid) {
    bool ret = false;
    switch (m_format) {
      case 0:
	for (size_t i=0; i<256; i++) {
	    if (map[i] == gid) {
		map[i] = 0;
		ret = true;
	    }
	}
	break;
      case 6:
      case 10:
	for (auto &em : mappings) {
	    if (em.gid == gid) {
		em.gid = 0;
		ret = true;
	    }
	}
	break;
      case 13: {
	size_t map_cnt = segments.size ();
	segments.erase (
	    std::remove_if (
		segments.begin (),
		segments.end (),
		[gid](enc_range &er) { return er.first_gid == gid; }
	    ), segments.end ()
	);
	ret = (map_cnt > segments.size ());
	} break;
      case 14:
	ret = false;
	break;
      default: {
	size_t map_cnt = mappings.size ();
	mappings.erase (
	    std::remove_if (
		mappings.begin (),
		mappings.end (),
		[gid](enc_mapping &em) { return em.gid == gid; }
	    ), mappings.end ()
	);
	ret = (map_cnt > mappings.size ());
      }
    }
    return ret;
}

bool CmapEnc::insertMapping (uint32_t code, uint16_t gid) {
    uint32_t pos=0;
    struct enc_mapping add = {code, gid};

    switch (m_format) {
      case 0:
      case 13:
      case 14:
	return false;
      case 6:
      case 10:
	if (mappings.empty ()) {
	    mappings.push_back (add);
	    return true;
	} else if (mappings.front ().code > 0 && code == mappings.front ().code-1) {
	    mappings.insert (mappings.begin (), add);
	    return true;
	} else if (code == mappings.back ().code + 1) {
	    mappings.push_back (add);
	    return true;
	} else
	    return false;
      default:
	while (mappings[pos].code <= code && pos < this->count ()) {
	    if (mappings[pos].code == code)
		return false;
	    pos++;
	}
	if (pos < this->count ())
	    mappings.insert (mappings.begin () + pos, add);
	else
	    mappings.push_back (add);
    }
    return true;
}

bool CmapEnc::insertUniMapping (uint32_t uni, uint16_t gid) {
    uint32_t code = uni;
    if (m_format ==  14 || (!isUnicode () && !hasConverter ()))
	return false;
    else if (!isUnicode ())
	code = recodeChar (uni, false);
    if (code == 0)
	return false;

    switch (m_format) {
      case 0:
	map[code] = gid;
	return true;
      case 6:
      case 10:
	for (auto &em : mappings) {
	    if (em.code == code) {
		em.gid = gid;
		break;
	    }
	}
	return true;
      case 13: {
	auto &first_seg = segments.front ();
	auto &last_seg = segments.back ();
        enc_range er;
        er.first_gid = gid;
        er.first_enc = code;
        er.length = 1;
	if (gid == first_seg.first_gid && code == first_seg.first_enc - 1) {
	    first_seg.first_enc = code;
	    first_seg.length++;
	    return true;
	} else if (gid == last_seg.first_gid && code == last_seg.first_enc + last_seg.length) {
	    last_seg.length++;
	    return true;
	} else if (code < first_seg.first_enc) {
	    segments.insert (segments.begin (), er);
	    return true;
	} else if (code >= last_seg.first_enc + last_seg.length) {
	    segments.insert (segments.end (), er);
	    return true;
	}

	for (size_t i=1; i<segments.size (); i++) {
	    auto &prev = segments[i-1];
	    auto &seg = segments[i];
	    if (prev.first_gid == gid && seg.first_gid == gid &&
		prev.first_enc + prev.length + 1 == seg.first_enc &&
		seg.first_enc - 1 == code) {
		prev.length = prev.length + seg.length + 1;
		segments.erase (segments.begin () + i);
		return true;
	    } else if (prev.first_gid == gid && prev.first_enc + prev.length == code) {
		prev.length++;
		return true;
	    } else if (seg.first_gid == gid && seg.first_enc == code + 1) {
		seg.first_enc--;
		seg.length++;
		return true;
	    } else if (code >= prev.first_enc + prev.length && code < seg.first_enc) {
		segments.insert (segments.begin () + i, er);
		return true;
	    }
	}
      } return false;
      default:
	return insertMapping (code, gid);
    }
}

bool CmapEnc::setGidByPos (uint32_t pos, uint16_t gid) {
    if (m_format == 14)
	return false;
    if (pos < mappings.size ()) {
	mappings[pos].gid = gid;
	return true;
    }
    return false;
}

int CmapEnc::firstAvailableCode () const {
    uint32_t i;
    switch (m_format) {
      case 0:
      case 14:
	return -1;
      case 6:
      case 10:
        if (mappings.size () == 0)
	    return 0;
	else if (mappings[0].code > 0)
	    return mappings[0].code - 1;
	else
	    return mappings[mappings.size () - 1].code + 1;
      default:
	if (mappings[0].code > 0)
	    return mappings[0].code - 1;
	else {
	    for (i=1; i<mappings.size (); i++) {
		if (mappings[i].code > mappings[i-1].code + 1)
		    return mappings[i-1].code + 1;
	    }
	}
    }
    return -1;
}

int CmapEnc::codeAvailable (uint32_t code) const {
    uint32_t i;
    switch (m_format) {
      case 0:
      case 14:
	return -1;
      case 6:
      case 10:
	if (mappings.size () == 0 || code == mappings[0].code - 1)
	    return 0;
	else if (code == mappings[mappings.size () - 1].code + 1)
	    return (mappings.size ());
	else
	    return -1;
      default:
	if (mappings.size () == 0 || code == mappings[0].code - 1)
	    return 0;
	else {
	    for (i=1; i<mappings.size (); i++) {
		if (mappings[i].code == code)
		    return -1;
		else if (code > mappings[i-1].code && code < mappings[i].code)
		    return i;
	    }
	    return mappings.size ();
	}
    }
    return -1;
}

uint32_t CmapEnc::numRanges () const {
    return segments.size ();
}

struct enc_range *CmapEnc::getRange (uint32_t idx) {
    if (idx < segments.size ())
	return &segments[idx];
    return nullptr;
}

bool CmapEnc::deleteRange (uint32_t idx) {
    if (idx<segments.size ()) {
	segments.erase (segments.begin () + idx);
	return true;
    }
    return false;
}

int CmapEnc::firstAvailableRange (uint32_t *start, uint32_t *length) {
    uint32_t i;
    if (segments.size () == 0) {
	*start = 0; *length = 0xffffff;
	return 0;
    } else if (segments[0].first_enc > 0) {
	*start = 0; *length = segments[0].first_enc - 1;
	return 0;
    } else {
	for (i=1; i<segments.size (); i++) {
	    uint32_t prev_code = segments[i-1].first_enc + segments[i-1].length;
	    if (prev_code < segments[i].first_enc) {
		*start = prev_code;
		*length = segments[i].first_enc - prev_code;
		return i;
	    }
	}
	if ((segments[segments.size () - 1].first_enc + segments[segments.size () - 1].length - 1) < 0xffffff) {
	    *start = segments[segments.size () - 1].first_enc + segments[segments.size () - 1].length;
	    *length = 0xffffff - *start + 1;
	    return segments.size ();
	}
    }
    return -1;
}

int CmapEnc::rangeAvailable (uint32_t first_enc, uint32_t length) {
    uint32_t seglen = segments.size (), i;

    if (segments.size () == 0)
	return 0;
    else if (segments[0].first_enc > first_enc + length)
	return 0;
    else if ((segments[seglen-1].first_enc + segments[seglen-1].length) <= first_enc)
	return seglen;
    for (i=1; i<seglen; i++) {
	uint32_t prev_code = segments[i-1].first_enc + segments[i-1].length - 1;
	if (prev_code < first_enc && segments[i].first_enc >= (first_enc + length))
	    return i;
    }
    return -1;
}

bool CmapEnc::insertRange (uint32_t first_enc, uint16_t first_gid, uint32_t length) {
    uint32_t seglen = segments.size (), i;
    struct enc_range add = {first_enc, length, first_gid};

    if (segments.size () == 0) {
	segments.push_back (add);
	return true;
    } else if (segments[0].first_enc > first_enc + length) {
	segments.insert (segments.begin (), add);
	return true;
    } else if ((segments[seglen-1].first_enc + segments[seglen-1].length) <= first_enc) {
	segments.push_back (add);
	return true;
    }
    for (i=1; i<seglen; i++) {
	uint32_t prev_code = segments[i-1].first_enc + segments[i-1].length - 1;
	if (prev_code < first_enc && segments[i].first_enc >= (first_enc + length)) {
	    segments.insert (segments.begin () + i, add);
	    return true;
	}
    }
    return false;
}

VarSelRecord *CmapEnc::getVarSelectorRecord (uint32_t idx) {
    if (m_format == 14 && idx < var_selectors.size ())
	return var_selectors[idx].get ();
    return nullptr;
}

bool CmapEnc::deleteVarSelectorRecord (uint32_t code) {
    size_t sel_cnt = var_selectors.size ();
    var_selectors.erase (
        std::remove_if (
	    var_selectors.begin (),
	    var_selectors.end (),
	    [code](std::unique_ptr<VarSelRecord> &vsr){return vsr->selector == code;}
	), var_selectors.end ()
    );
    return (sel_cnt > var_selectors.size ());
}

VarSelRecord *CmapEnc::addVariationSequence
    (uint32_t selector, bool is_dflt, uint32_t code, uint16_t gid) {

    uint32_t i, j;
    std::unique_ptr<VarSelRecord> add (new VarSelRecord ());
    add->selector = selector;
    if (is_dflt) {
	add->default_offset = 0xffffffff;
	add->default_vars.push_back (code);
    } else {
	add->non_default_offset = 0xffffffff;
	struct enc_mapping m = {code, gid};
	add->non_default_vars.push_back (m);
    }

    if (var_selectors.size () == 0 || selector > var_selectors[var_selectors.size () - 1]->selector) {
	var_selectors.push_back (std::move (add));
	return var_selectors.back ().get ();
    }
    for (i=0; i<var_selectors.size (); i++) {
	if (var_selectors[i]->selector == selector) {
	    if (is_dflt) {
		for (j=0; j<var_selectors[i]->default_vars.size (); j++) {
		    if (var_selectors[i]->default_vars[j] == code)
			return nullptr;
		}
		var_selectors[i]->default_vars.push_back (code);
		if (!var_selectors[i]->default_offset)
		    var_selectors[i]->default_offset = 0xffffffff;
		std::sort (var_selectors[i]->default_vars.begin (), var_selectors[i]->default_vars.end ());
		return var_selectors[i].get ();
	    } else {
		for (j=0; j<var_selectors[i]->non_default_vars.size (); j++) {
		    if (var_selectors[i]->non_default_vars[j].code == code)
			return nullptr;
		}
		var_selectors[i]->non_default_vars.push_back (add->non_default_vars[0]);
		if (!var_selectors[i]->non_default_offset)
		    var_selectors[i]->non_default_offset = 0xffffffff;
		std::sort (var_selectors[i]->non_default_vars.begin (), var_selectors[i]->non_default_vars.end (), rcomp_mappings_by_code);
		return var_selectors[i].get ();
	    }
	} else if (var_selectors[i]->selector > selector) {
	    var_selectors.insert (var_selectors.begin () + i, std::move (add));
	    return var_selectors[i].get ();
	}
    }
    return nullptr;
}

std::vector<uint32_t> CmapEnc::encoded (uint16_t gid) {
    unsigned int i;
    std::vector<uint32_t> ret;

    if (numBits () == 8) {
        for (i=0; i<256; i++) {
            if (gid == map[i])
		ret.push_back (i);
        }
    } else if (m_format == 13) {
        for (i=0; i<segments.size (); i++) {
            if (gid == segments[i].first_gid) {
		for (uint16_t j=0; j<segments[i].length; i++)
		    ret.push_back (segments[i].first_enc + j);
		break;
            }
        }
    } else {
        for (i=0; i<mappings.size (); i++) {
            if (gid == mappings[i].gid)
                ret.push_back (mappings[i].code);
        }
    }

    return ret;
}

std::vector<uint32_t> CmapEnc::unicode (uint16_t gid) {
    if (!isUnicode () && !hasConverter ())
        return std::vector<uint32_t> ();

    std::vector<uint32_t> ret = encoded (gid);
    if (m_codec != (iconv_t)(-1)) {
        for (unsigned int i=0; i<ret.size (); i++)
            ret[i] = recodeChar (ret[i], true);
    }
    return ret;
}

uint32_t CmapEnc::unicodeByPos (uint32_t pos) const {
    uint32_t i, cur = 0, ret;

    if ((!isUnicode () && !hasConverter ()) || pos > this->count ())
        return 0;

    if (m_format == 0 && hasConverter ()) {
        for (i=0; i<256; i++) {
            if (map[i] != 0) cur++;

            if (pos == cur)
                return recodeChar (i, true);
        }
    } else if (m_format == 13) {
        for (i=0; i<segments.size (); i++) {
            if (pos >= cur && pos < cur + segments[i].length)
                return segments[i].first_enc + (pos - cur);
            cur += segments[i].length;
        }
    } else if (numBits () > 8) {
        for (i=0; i<mappings.size (); i++) {
	    if (i == pos) {
		ret = mappings[i].code;
                if (m_codec != (iconv_t)(-1))
                    ret = recodeChar (ret, true);
                return ret;
	    }
	}
    }
    return 0;
}

uint32_t CmapEnc::encByPos (uint32_t pos) const {
    uint32_t i, cur = 0;

    if (pos > this->count ())
        return 0;

    if (m_format == 0) {
        return pos;
    } else if (m_format == 13) {
        for (i=0; i<segments.size (); i++) {
            if (pos >= cur && pos < cur + segments[i].length)
                return (segments[i].first_enc + (pos - cur));
            cur += segments[i].length;
        }
    } else if (numBits () > 8) {
	return mappings[pos].code;
    }
    return 0;
}

uint16_t CmapEnc::gidByPos (uint32_t pos) const {
    uint32_t i, j, cur = 0;

    if (pos > this->count ())
        return 0;

    if (m_format == 0 && pos < 256) {
	return map[pos];
    } else if (m_format == 13) {
        for (i=0; i<segments.size (); i++) {
	    for (j=segments[i].first_gid; j<segments[i].length; j++) {
		if (cur == pos)
		    return (j);
		cur++;
	    }
        }
    } else if (numBits () > 8) {
	return mappings[pos].gid;
    }
    return 0;
}

uint16_t CmapEnc::gidByEnc (uint32_t code) const {
    uint16_t i;
    if (m_format == 0 && code < 256)
        return map[code];
    else if (m_format == 13) {
        for (i=0; i<segments.size (); i++) {
            if (code >= segments[i].first_enc && code < segments[i].first_enc + segments[i].length)
		return segments[i].first_gid;
        }
    } else {
        for (i=0; i<mappings.size (); i++) {
            if (code == mappings[i].code)
		return mappings[i].gid;
        }
    }
    return 0;
}

uint16_t CmapEnc::gidByUnicode (uint32_t uni) const {
    if (!isUnicode () && !hasConverter ())
        return 0;

    uint32_t code = (m_codec != (iconv_t)(-1)) ? recodeChar (uni, false) : uni;
    return gidByEnc (code);
}

uint32_t CmapEnc::recodeChar (uint32_t code, bool to_uni) const {
    int i, last = 0;
    uint32_t ret = 0;
    size_t s_size = 5, t_size = 33;
    char source[t_size] = {0};
    char target[s_size] = {0};
    iconv_t conv = to_uni ? m_codec : m_unicodec;

    if (code == 0) return 0;

    for (i=3; i>=0; i--) {
        uint8_t ch = (uint8_t) (code>>(8*i))&0xff;
        // Need exactly 4 bytes for conversion from UCS-4,
        // otherwise take them beginning from the first significant
        if (ch > 0 || last > 0 || !to_uni)
            source[last++] = ch;
    }
    char *ptarget = target;
    char *psource = source;

    iconv (conv, &psource, &s_size, &ptarget, &t_size);

    if (s_size == 5)
        return ret;

    last = 0;
    for (i=3; i>=0; i--) {
        // The opposite thing: for conversion to UCS-4 take exactly
        // 4 bytes, otherwise beginning from the last significant one
        if ((uint8_t) target[i] > 0 || last > 0 || to_uni) {
            ret += ((uint8_t) target[i])<<(8*last);
            last++;
        }
    }

    return ret;
}

std::vector<uint32_t> CmapEnc::unencoded (uint32_t glyph_cnt) {
    uint32_t i;
    std::vector<uint32_t> ret;

    ret.reserve (glyph_cnt);
    if (m_format == 0) {
        bool glyphs[glyph_cnt] = { 0 };
        for (i=0; i<256; i++) {
            if (map[i] < glyph_cnt)
                glyphs[map[i]] = true;
        }

        for (i=0; i<glyph_cnt; i++) {
            if (!glyphs[i])
                ret.push_back (i);
        }
    } else {
	if (m_format == 13) {
	    std::vector<struct enc_range> segments_by_gid (segments);
	    std::sort (
		segments_by_gid.begin (),
		segments_by_gid.end (),
		[](const enc_range &r1, const enc_range &r2) {
		    return (r1.first_gid < r2.first_gid);
		}
	    );

	    uint32_t prev = 0;
	    for (i=0; i < segments_by_gid.size (); i++) {
		for (uint32_t j=prev; j<segments_by_gid[i].first_gid; j++) {
		    ret.push_back (j);
		}
		uint32_t last = segments_by_gid[i].first_gid + segments_by_gid[i].length;
		if (last > prev)
		    prev = last;
	    }
	    if (prev < glyph_cnt) {
		for (i=prev; i<glyph_cnt; i++)
		    ret.push_back (i);
	    }
	} else {
	    std::vector<struct enc_mapping> mappings_by_gid (mappings);
	    std::sort (
		mappings_by_gid.begin (),
		mappings_by_gid.end (),
		[](const enc_mapping &m1, const enc_mapping &m2) {
		    return (m1.gid < m2.gid);
		}
	    );

	    uint32_t next = 0;
	    for (i=0; i < mappings_by_gid.size (); i++) {
		for (uint32_t j=next; j<mappings_by_gid[i].gid; j++)
		    ret.push_back (j);
		next = mappings_by_gid[i].gid+1;
	    }
	    if (next < glyph_cnt) {
		for (i=next; i<glyph_cnt; i++)
		    ret.push_back (i);
	    }
	}
    }
    return ret;
}

uint32_t CmapEnc::index () const {
    return m_index;
}

void CmapEnc::setIndex (uint32_t idx) {
    m_index = idx;
}

uint32_t CmapEnc::offset () const {
    return (m_offset);
}

void CmapEnc::setOffset (uint32_t val) {
    m_offset = val;
}

uint32_t CmapEnc::length () const {
    return (m_length);
}

void CmapEnc::setLength (uint32_t val) {
    m_length = val;
}

uint16_t CmapEnc::format () const {
    return (m_format);
}

void CmapEnc::setFormat (uint16_t val) {
    m_format = val;
}

uint16_t CmapEnc::language () const {
    return (m_language);
}

void CmapEnc::setLanguage (uint16_t val) {
    m_language = val;
}

bool CmapEnc::isCurrent () const {
    return m_current;
}

void CmapEnc::setCurrent (bool val) {
    m_current = val;
}

bool CmapEnc::isModified () const {
    return m_changed;
}

void CmapEnc::setModified (bool val) {
    m_changed = val;
}

void CmapEnc::addLock () {
    m_lockCounter++;
}

void CmapEnc::removeLock () {
    if (m_lockCounter > 0) m_lockCounter--;
}

int CmapEnc::isLocked () {
    return m_lockCounter;
}

const QString CmapEnc::codeRepr (uint32_t pos) {
    QString ret_str;
    char32_t *pos_ptr = reinterpret_cast<char32_t *> (&pos);

    if (pos == 0xFFFF)
	ret_str = QString ("<unencoded>");
    else if (this->numBits () == 8)
	ret_str = QString ("0x%1").arg (pos, 2, 16, QLatin1Char ('0'));
    else if (this->isUnicode ()) {
	ret_str = QString ("U+%1: %2")
	    .arg (pos, pos <= 0xFFFF ? 4 : 6, 16, QLatin1Char ('0'))
	    .arg (pos <= 0xFFFF ? QChar (pos) : QString::fromUcs4 (pos_ptr, 1));
    } else
	ret_str = QString ("0x%1").arg (pos, pos <= 0xFFFF ? 4 : 6, 16, QLatin1Char ('0'));
    return ret_str;
}

const QString CmapEnc::gidCodeRepr (uint16_t gid) {
    const std::vector<uint32_t> &encoded = this->isUnicode () ?
	this->unicode (gid) : this->encoded (gid);
    uint32_t pos = encoded.empty () ? 0xFFFF : encoded[0];
    return codeRepr (pos);
}
