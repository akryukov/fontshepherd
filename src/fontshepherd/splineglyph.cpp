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
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#define _USE_MATH_DEFINES
#include <cmath>
#include <cctype>
#include <assert.h>
#include <inttypes.h>
#include <set>
#include <iterator>

#include "cffstuff.h"
#include "tables.h"
// also includes splineglyph.h
#include "tables/glyphcontainer.h"
#include "tables/cff.h"
#include "tables/colr.h"
#include "tables/maxp.h"
#include "stemdb.h"
#include "fs_notify.h"
#include "fs_math.h"

using namespace FontShepherd::math;

#define _ARGS_ARE_WORDS	1
#define _ARGS_ARE_XY	2
#define _ROUND		4		/* offsets rounded to grid */
#define _SCALE		8
#define _MORE		0x20
#define _XY_SCALE	0x40
#define _MATRIX		0x80
#define _INSTR		0x100
#define _USE_MY_METRICS	0x200
/* GWW: Used in Apple GX fonts */
/* Means the components overlap (which? this one and what other?) */
#define _OVERLAP_COMPOUND	0x400
/* These two described in OpenType specs, not by Apple yet */
#define _SCALED_OFFSETS		0x800	/* Use Apple definition of offset interpretation */
#define _UNSCALED_OFFSETS	0x1000	/* Use MS definition */

static float get2dot14 (BoostIn &buf) {
    uint16_t val;
    buf >> val;
    int mant = val&0x3fff;
    /* GWW: This oddity may be needed to deal with the first 2 bits being signed */
    /*  and the low-order bits unsigned */
    return ((float) ((val<<16)>>(16+14)) + (mant/16384.0));
}

static void put2dot14 (QDataStream &os, double dval) {
    uint16_t val, mant;

    val = floor (dval);
    mant = floor (16384.*(dval-val));
    val = (val<<14) | mant;
    os << val;
}

ConicGlyph::ConicGlyph (uint16_t gid, BaseMetrics gm) :
    GID (gid), units_per_em (gm.upm), m_ascent (gm.ascent), m_descent (gm.descent) {
    instrdata = {};
    bb = DBounds ();
    clipBox = { 0, 0, 0, 0 };
    m_undoStack = std::unique_ptr<QUndoStack> (new QUndoStack ());
};

ConicGlyph::~ConicGlyph () {
    if (instrdata.instrs)
	delete[] instrdata.instrs;
};

void ConicGlyph::clear () {
    figures.clear ();
    refs.clear ();
    gradients.clear ();
    dependents.clear ();
    countermasks.clear ();
    hstem.clear ();
    vstem.clear ();
}

static void attachControls (ConicPoint *from, ConicPoint *to, BasePoint *cp, int &num) {
    from->nextcp = to->prevcp = *cp;
    from->nextcpindex = num++;
    from->nonextcp = to->noprevcp = false;
}

void ConicGlyph::ttfBuildContours (int path_cnt, uint16_t *endpt, uint8_t *flags, std::vector<BasePoint> &pts) {
    int i, path, start;
    bool last_off;
    ConicPoint *sp;
    int num=0;

    figures.emplace_back ();
    figures.back ().type = "path";
    figures.back ().order2 = true;
    std::vector<ConicPointList> &conics = figures.back ().contours;
    conics.reserve (path_cnt);
    boost::object_pool<ConicPoint> &points_pool = figures.back ().points_pool;
    boost::object_pool<Conic> &splines_pool = figures.back ().splines_pool;

    for (path=i=0; path<path_cnt; ++path) {
	if (endpt[path]<i)	/* GWW: Sigh. Yes there are fonts with bad endpt info */
	    continue;
	ConicPointList cur = ConicPointList ();
	last_off = false;
	start = i;
	while (i<=endpt[path]) {
	    if (flags[i]&_On_Curve) {
		sp = points_pool.construct ();
		sp->me = pts[i];
		sp->nonextcp = sp->noprevcp = true;
		if (last_off && cur.last)
		    attachControls (cur.last, sp, &pts[i-1], num);
		sp->ttfindex = num++;
		last_off = false;
	    } else if (last_off) {
		/* GWW: two off curve points get a third on curve point created */
		/* half-way between them. Now isn't that special */
		sp = points_pool.construct ();
		sp->me.x = (pts[i].x+pts[i-1].x)/2;
		sp->me.y = (pts[i].y+pts[i-1].y)/2;
		sp->nonextcp = sp->noprevcp = true;
		if (last_off && cur.last)
		    attachControls (cur.last, sp, &pts[i-1], num);
		sp->ttfindex = -1;
		/* GWW: last_off continues to be true */
	    } else {
		// Contour starts from an offcurve point. Can't assign a correct
		// number to it right now, as in our model it will belong to the
		// previous (i. e. last for the given contour) spline point. However,
		// it is important to increase the current number by one to make
		// the subsequent numbering correct.
		if (i==start)
		    num++;
		last_off = true;
		sp = nullptr;
	    }
	    if (sp) {
		if (cur.first==nullptr) {
		    cur.first = sp;
                    sp->isfirst = true;
		} else
		    splines_pool.construct (cur.last, sp, true);
		cur.last = sp;
	    }
	    ++i;
	}
	if (start==i-1) {
	    /* GWW: MS chinese fonts have contours consisting of a single off curve*/
	    /*  point. What on earth do they think that means? */
	    sp = points_pool.construct ();
	    sp->me.x = pts[start].x;
	    sp->me.y = pts[start].y;
            sp->nonextcp = sp->noprevcp = true;
	    sp->ttfindex = num++;
	    cur.first = cur.last = sp;
            sp->isfirst = true;

	} else if (!(flags[start]&_On_Curve) && !(flags[i-1]&_On_Curve)) {
	    sp = points_pool.construct ();
	    sp->me.x = (pts[start].x+pts[i-1].x)/2;
	    sp->me.y = (pts[start].y+pts[i-1].y)/2;
	    sp->nonextcp = sp->noprevcp = true;
	    attachControls (cur.last, sp, &pts[i-1], num);
	    sp->ttfindex = -1;
	    splines_pool.construct (cur.last, sp, true);
	    cur.last = sp;
	    attachControls (sp, cur.first, &pts[start], num);

	} else if (!(flags[i-1]&_On_Curve)) {
	    attachControls (cur.last, cur.first, &pts[i-1], num);

	} else if (!(flags[start]&_On_Curve)) {
	    attachControls (cur.last, cur.first, &pts[start], num);
	}
	// Fixup the number of the starting point of the contour
	// in case it was an offcurve point.
	if (!(flags[start]&_On_Curve)) {
	    sp->nextcpindex = start; num--;
	}

	splines_pool.construct (cur.last, cur.first, true);
	cur.last = cur.first;
	conics.push_back (cur);
    }
}

void ConicGlyph::categorizePoints () {
    for (auto it=figures.begin (); it != figures.end (); it++) {
	auto &fig = *it;
        std::vector<ConicPointList> &conics = fig.contours;
	if (fig.svgState.point_props_set)
	    continue;

        for (size_t j=0; j<conics.size (); j++) {
            ConicPointList *spls = &conics[j];
            ConicPoint *sp = spls->first;
            do {
                sp->categorize ();
                sp = (sp->next) ? sp->next->to : nullptr;
            } while (sp && sp != spls->first);
        }
    }
}

void ConicGlyph::readttfsimpleglyph (BoostIn &buf, int path_cnt, uint32_t start_pos) {
    uint16_t endpt[path_cnt + 1] = { 0 };
    std::vector<BasePoint> pts;
    int i, j, tot;
    int last_pos;

    for (i=0; i<path_cnt; ++i)
	buf >> endpt[i];

    if (path_cnt==0) {
	tot = 0;
	pts.resize (1);
    } else {
	tot = endpt[path_cnt-1]+1;
	pts.resize (tot);
    }

    instrdata.in_composit = false;
    buf >> instrdata.instr_cnt;
    instrdata.instrs = new unsigned char[instrdata.instr_cnt];
    for (i=0; i<instrdata.instr_cnt; ++i )
	buf >> instrdata.instrs[i];

    uint8_t flags[tot] = { 0 };
    for (i=0; i<tot; ++i) {
	buf >> flags[i];
	if (flags[i]&_Repeat) {
	    unsigned char cnt;
	    buf >> cnt;
	    for (j=0; j<cnt; ++j)
		flags[i+j+1] = flags[i];
	    i += cnt;
	}
    }
    if (i!=tot)
        FontShepherd::postError (
            tr ("Bad glyf data"),
            tr ("Flag count in %1 at 0x%2 is %3, while %4 is expected")
		.arg (GID)
		.arg (start_pos, 0, 16)
		.arg (i)
		.arg (tot),
            nullptr);

    last_pos = 0;
    for (i=0; i<tot; ++i) {
	int16_t off;
	if (flags[i]&_X_Short) {
	    uint8_t ch;
	    buf >> ch;
	    off = (flags[i]&_X_Same) ? ch : -ch;
	} else if (flags[i]&_X_Same)
	    off = 0;
	else
	    buf >> off;

	pts[i].x = last_pos + off;
	last_pos = pts[i].x;
    }

    last_pos = 0;
    for (i=0; i<tot; ++i) {
	int16_t off;
	if (flags[i]&_Y_Short) {
	    uint8_t ch;
	    buf >> ch;
	    off = (flags[i]&_Y_Same) ? ch : -ch;
	} else if (flags[i]&_Y_Same)
	    off = 0;
	else
	    buf >> off;

	pts[i].y = last_pos + off;
	last_pos = pts[i].y;
    }

    ttfBuildContours (path_cnt, endpt, flags, pts);
    point_cnt = tot;
    categorizePoints ();
}

void ConicGlyph::readttfcompositeglyph (BoostIn &buf) {
    static bool default_to_Apple = false;
    uint16_t flags, i;

    do {
	if ((int) buf.peek () == EOF) {
            FontShepherd::postError (
                tr ("Bad glyf data"),
                tr ("Reached end of table when reading composite glyph : %1").arg (GID),
                nullptr);
	    break;
	}

	DrawableReference cur;
	cur.outType = OutlinesType::TT;
	int16_t arg1, arg2;

	buf >> flags;
	buf >> cur.GID;
	if (flags&_ARGS_ARE_WORDS) {
	    buf >> arg1;
	    buf >> arg2;
	} else {
	    int8_t ch1, ch2;
	    buf >> ch1; buf >> ch2;
	    arg1= ch1; arg2 = ch2;
	}

	if (flags & _ARGS_ARE_XY) {
	    /* GWW: There is some very strange stuff (half-)documented on the apple*/
	    /*  site about how these should be interpretted when there are */
	    /*  scale factors, or rotations */
	    /* It isn't well enough described to be comprehensible */
	    /*  http://fonts.apple.com/TTRefMan/RM06/Chap6glyf.html */
	    /* Microsoft says nothing about this */
	    /* Adobe implies this is a difference between MS and Apple */
	    /*  MS doesn't do this, Apple does (GRRRGH!!!!) */
	    /* Adobe says that setting bit 12 means that this will not happen */
	    /* Adobe says that setting bit 11 means that this will happen */
	    /*  So if either bit is set we know when this happens, if neither */
	    /*  we guess... But I still don't know how to interpret the */
	    /*  apple mode under rotation... */
	    /* I notice that FreeType does nothing about rotation nor does it */
	    /*  interpret bits 11&12 */
	    /* Ah. It turns out that even Apple does not do what Apple's docs */
	    /*  claim it does. I think I've worked it out (see below), but... */
	    /*  Bleah! */
	    cur.transform[4] = arg1;
	    cur.transform[5] = arg2;

	} else {
	    /* GWW: Somehow we can get offsets by looking at the points in the */
	    /*  points so far generated and comparing them to the points in */
	    /*  the current componant */
	    /* How exactly is not described on any of the Apple, MS, Adobe */
	    /* freetype looks up arg1 in the set of points we've got so far */
	    /*  looks up arg2 in the new component (before renumbering) */
	    /*  offset.x = arg1.x - arg2.x; offset.y = arg1.y - arg2.y; */
	    /* This fixup needs to be done later though (after all glyphs */
	    /*  have been loaded) */
	    cur.match_pt_base = arg1;
	    cur.match_pt_ref = arg2;
	    cur.point_match = true;
	}
	cur.transform[0] = cur.transform[3] = 1.0;
	if (flags & _SCALE)
	    cur.transform[0] = cur.transform[3] = get2dot14 (buf);
	else if (flags & _XY_SCALE) {
	    cur.transform[0] = get2dot14 (buf);
	    cur.transform[3] = get2dot14 (buf);
	} else if (flags & _MATRIX) {
	    cur.transform[0] = get2dot14 (buf);
	    cur.transform[1] = get2dot14 (buf);
	    cur.transform[2] = get2dot14 (buf);
	    cur.transform[3] = get2dot14 (buf);
	}

	/* GWW: If neither SCALED/UNSCALED specified I'll just assume MS interpretation */
	if ( ((default_to_Apple && !(flags&_UNSCALED_OFFSETS)) ||
	    (flags & _SCALED_OFFSETS)) &&
	    (flags & _ARGS_ARE_XY) && (flags&(_SCALE|_XY_SCALE|_MATRIX))) {
	    /* GWW: This is not what Apple documents on their website. But it is */
	    /*  what appears to match the behavior of their rasterizer */
	    /* Apple has changed their documentation (without updating their */
	    /*  changelog), but I believe they are still incorrect */
	    /* Apple's Chicago is an example */
	    cur.transform[4] *= sqrt(cur.transform[0]*cur.transform[0]+
		    cur.transform[1]*cur.transform[1]);
	    cur.transform[5] *= sqrt(cur.transform[2]*cur.transform[2]+
		    cur.transform[3]*cur.transform[3]);
	}
	cur.use_my_metrics = (flags&_USE_MY_METRICS)?1:0;
	cur.round = (flags&_ROUND) ? 1 : 0;
	cur.cc = nullptr;
	refs.push_back (cur);
    } while (flags&_MORE);

    if (flags&_INSTR) {
	instrdata.in_composit = true;
	buf >> instrdata.instr_cnt;
	instrdata.instrs = new uint8_t[instrdata.instr_cnt];
	for (i=0; i<instrdata.instr_cnt; ++i)
	    buf >> instrdata.instrs[i];
    }
}

std::vector<uint16_t> ConicGlyph::refersTo () const {
    std::vector<uint16_t> ret;
    ret.reserve (refs.size ());
    for (uint16_t i=0; i<refs.size (); i++)
	ret.push_back (refs[i].GID);
    return ret;
}

int ConicGlyph::checkRefs (uint16_t gid, uint16_t gcnt) {
    for (auto &ref : refs) {
        if (ref.GID == gid && ref.outType == m_outType) {
            FontShepherd::postError (
                tr ("Self-referencial glyph"),
                tr ("Attempt to make a glyph that refers to itself: %1").arg (ref.GID),
                nullptr);
            return ref.GID;
        } else if (ref.GID >= gcnt) {
            FontShepherd::postError (
                tr ("Reference to a wrong GID"),
                tr ("Attempt to make a reference to glyph %1, "
                    "which doesn't exist in the font").arg (ref.GID),
                nullptr);
            return ref.GID;
        } else if (ref.cc) {
            int ret = ref.cc->checkRefs (gid, gcnt);
            if (ret > 0)
                return ret;
        }

	// Couldn't do this before reference glyphs are available
	if (m_outType == OutlinesType::COLR && ref.cc && !ref.svgState.fill_source_id.empty ()) {
	    auto &grad = gradients[ref.svgState.fill_source_id];
	    DBounds bb;
	    ref.quickBounds (bb);
	    grad.convertBoundingBox (bb);
	}
    }
    return 0;
}

void ConicGlyph::provideRef (ConicGlyph *g, uint16_t refidx) {
    assert (refidx < refs.size ());
    refs[refidx].cc = g;
}

uint16_t ConicGlyph::getTTFPoint (uint16_t pnum, uint16_t add, BasePoint *&pt) {
    if (!figures.empty ()) {
        std::vector<ConicPointList> &conics = figures.front ().contours;
        for (auto &spls : conics) {
            ConicPoint *sp = spls.first;
            do {
                if ((sp->ttfindex + add) == pnum) {
                    pt = &sp->me;
                    return pnum;
                } else if (!sp->nonextcp && (sp->nextcpindex+add) == pnum) {
                    pt = &sp->nextcp;
                    return pnum;
                }
                sp = (sp->next) ? sp->next->to : nullptr;
            } while (sp && sp != spls.first);
        }
        if (!conics.empty ()) {
	    auto &spls = conics.back ();
            add = (spls.last->ttfindex > spls.last->nextcpindex) ?
                spls.last->ttfindex : spls.last->nextcpindex;
	}
    }

    for (auto &ref : refs) {
	add = ref.cc->getTTFPoint (pnum, add, pt);
	if (pt)
	    break;
    }

    return (add);
}

void ConicGlyph::finalizeRefs () {
    for (auto &ref : refs) {
	if (ref.point_match) {
	    BasePoint *p1=nullptr, *p2=nullptr;
	    getTTFPoint (ref.match_pt_base, 0, p1);
	    ref.cc->getTTFPoint (ref.match_pt_ref, 0, p2);
	    if (!p1 || !p2) {
                FontShepherd::postError (
                    tr ("Bad glyf data"),
                    tr ("Could not do a point match when !ARGS_ARE_XY: "
                        "base point %1 in glyph %2, reference point %3 in glyph %4")
                        .arg (ref.match_pt_base).arg (GID).arg (ref.match_pt_ref).arg (ref.cc->GID),
                    nullptr);
		ref.transform[4] = ref.transform[5] = 0;
	    } else {
		ref.transform[4] = p1->x-p2->x;
		ref.transform[5] = p1->y-p2->y;
	    }
	}
    }
}

void ConicGlyph::renumberPoints () {
    uint16_t lastpt=0;
    for (auto &fig : figures)
	lastpt = fig.renumberPoints (lastpt);
}

void ConicGlyph::unlinkRef (DrawableReference &ref) {
    for (auto &fig: ref.cc->figures) {
	DrawableFigure newf (fig);
	for (auto &spls: newf.contours)
	    spls.doTransform (ref.transform);
	if (this->figures.empty () || m_outType == OutlinesType::SVG)
	    figures.push_back (newf);
	else
	    this->figures.front ().mergeWith (newf);
    }
    for (auto &stem: ref.cc->hstem) {
	BasePoint spos {0, stem.start};
	BasePoint epos {0, stem.start + stem.width};
	spos.transform (&spos, ref.transform);
	epos.transform (&epos, ref.transform);
	appendHint (spos.y, epos.y-spos.y, false);
    }
    for (auto &stem: ref.cc->vstem) {
	BasePoint spos {stem.start, 0};
	BasePoint epos {stem.start + stem.width, 0};
	spos.transform (&spos, ref.transform);
	epos.transform (&epos, ref.transform);
	appendHint (spos.x, epos.x-spos.x, true);
    }
}

void ConicGlyph::unlinkRefs (bool selected) {
    int lastpt = 0;
    for (int i=this->refs.size ()-1; i>=0; i--) {
	auto &ref = this->refs[i];
	if (ref.selected || !selected) {
	    unlinkRef (ref);
	    this->refs.erase (this->refs.begin () + i);
	}
    }
    for (auto &figure : this->figures)
	lastpt = figure.renumberPoints (lastpt);
    checkBounds (bb, false);
}

void ConicGlyph::fromTTF (BoostIn &buf, uint32_t off) {
    int16_t path_cnt;
    int16_t min_x, max_x, min_y, max_y;

    if ((int) buf.peek () == EOF) {
	loaded= true;
	return;
    }

    buf >> path_cnt;
    buf >> min_x; bb.minx = min_x;
    buf >> min_y; bb.miny = min_y;
    buf >> max_x; bb.maxx = max_x;
    buf >> max_y; bb.maxy = max_y;

    if (path_cnt>=0)
	readttfsimpleglyph (buf, path_cnt, off);
    else
	readttfcompositeglyph (buf);
    m_outType = OutlinesType::TT;
    loaded = true;
}

uint32_t ConicGlyph::toTTF (QBuffer &buf, QDataStream &os, MaxpTable *maxp) {
    static bool mixed_glyph_warned = false;
    DBounds bb;
    checkBounds (bb, true);
    maxp_data &maxp_contents = maxp->contents;

    if (!figures.empty () && !refs.empty ()) {
	if (!mixed_glyph_warned) {
	    FontShepherd::postWarning (
		tr ("Mixed glyph format"),
		tr ("Some glyphs contain both splines and references. "
		    "TrueType format doesn't allow this. "
		    "I will unlink such references, converting them to splines."),
		nullptr);
	    mixed_glyph_warned = true;
	}
	unlinkRefs (false);
    }

    int16_t ccnt = refs.size () ? -1 :
	figures.size () ? figures.front ().contours.size () : 0;
    if (ccnt > maxp->maxContours ())
	maxp_contents.maxContours = ccnt;
    uint32_t startpos = buf.pos ();

    // No data for empty glyphs
    if (figures.size () || refs.size ()) {
	os << ccnt;
	os << (int16_t) bb.minx;
	os << (int16_t) bb.miny;
	os << (int16_t) bb.maxx;
	os << (int16_t) bb.maxy;
    }

    if (figures.size ()) {
	std::vector<int16_t> x_coords;
	std::vector<int16_t> y_coords;
	std::vector<uint8_t>  flags;

	uint16_t ptcnt = figures.front ().toCoordList (x_coords, y_coords, flags, GID);
	if (ptcnt > maxp->maxPoints ())
	    maxp_contents.maxPoints = ptcnt;

	for (int i=0; i<ccnt; i++) {
	    auto &spls = figures.front ().contours[i];
	    os << spls.lastPointIndex ();
	}
        os << instrdata.instr_cnt;
        if (instrdata.instr_cnt > maxp->maxSizeOfInstructions ())
    	maxp_contents.maxSizeOfInstructions = instrdata.instr_cnt;
        for (int j=0; j<instrdata.instr_cnt; j++)
	    os << instrdata.instrs[j];
        for (size_t j=0; j<flags.size (); j++)
	    os << flags[j];
        for (size_t j=0; j<x_coords.size (); j++) {
	    if (x_coords[j] >= 0 && x_coords[j] < 256)
		os << (uint8_t) x_coords[j];
	    else
		os << x_coords[j];
        }
        for (size_t j=0; j<y_coords.size (); j++) {
	    if (y_coords[j] >= 0 && y_coords[j] < 256)
		os << (uint8_t) y_coords[j];
	    else
		os << y_coords[j];
	}
    } else if (refs.size ()) {
	if (refs.size () > maxp->maxComponentElements ())
	    maxp_contents.maxComponentElements = refs.size ();
	for (size_t i=0; i<refs.size (); i++) {
	    DrawableReference &ref = refs[i];
	    uint16_t flags = 0;
	    int16_t arg1, arg2;
	    if (ref.round)
		flags |= _ROUND;
	    if (ref.use_my_metrics)
		flags |= _USE_MY_METRICS;
	    if (i<refs.size ()-1)
		flags |= _MORE;			/* More components */
	    else if (instrdata.instr_cnt)	/* Composits also inherit instructions */
		flags |= _INSTR;		/* Instructions appear after last ref */
	    if (ref.transform[1]!=0 || ref.transform[2]!=0)
		flags |= _MATRIX;		/* Need a full matrix */
	    else if (ref.transform[0]!=ref.transform[3])
		flags |= _XY_SCALE;		/* different xy scales */
	    else if (ref.transform[0]!=1.)
		flags |= _SCALE;		/* xy scale is same */
	    if (ref.point_match) {
		arg1 = ref.match_pt_base;
		arg2 = ref.match_pt_ref;
	    } else {
		arg1 = rint (ref.transform[4]);
		arg2 = rint (ref.transform[5]);
		flags |= _ARGS_ARE_XY|_UNSCALED_OFFSETS;
		/* GWW: The values I output are the values I want to see */
		/* There is some very strange stuff wrongly-documented on the apple*/
		/*  site about how these should be interpretted when there are */
		/*  scale factors, or rotations */
		/* That description does not match the behavior of their rasterizer*/
		/*  I've reverse engineered something else (see parsettf.c) */
		/*  http://fonts.apple.com/TTRefMan/RM06/Chap6glyf.html */
		/* Adobe says that setting bit 12 means that this will not happen */
		/*  Apple doesn't mention bit 12 though...(but they do support it) */
	    }
	    if (arg1<-128 || arg1>127 || arg2<-128 || arg2>127)
		flags |= _ARGS_ARE_WORDS;
	    os << flags;
	    os << ref.cc->gid ();
	    if (flags&_ARGS_ARE_WORDS) {
		os << arg1;
		os << arg2;
	    } else {
		os << (int8_t) arg1;
		os << (int8_t) arg2;
	    }
	    if (flags&_MATRIX) {
		put2dot14 (os, ref.transform[0]);
		put2dot14 (os, ref.transform[1]);
		put2dot14 (os, ref.transform[2]);
		put2dot14 (os, ref.transform[3]);
	    } else if (flags&_XY_SCALE) {
		put2dot14 (os, ref.transform[0]);
		put2dot14 (os, ref.transform[3]);
	    } else if (flags&_SCALE) {
		put2dot14 (os, ref.transform[0]);
	    }
	    uint16_t comp_pt = numCompositePoints ();
	    uint16_t comp_cc = numCompositeContours ();
	    uint16_t comp_dp = componentDepth ();
	    if (maxp->maxCompositePoints () < comp_pt)
		maxp_contents.maxCompositePoints = comp_pt;
	    if (maxp->maxCompositeContours () < comp_cc)
		maxp_contents.maxCompositeContours = comp_cc;
	    if (maxp->maxComponentDepth () < comp_dp)
		maxp_contents.maxComponentDepth = comp_dp;
	}
        if (instrdata.instr_cnt) {
	    os << instrdata.instr_cnt;
	    if (instrdata.instr_cnt > maxp->maxSizeOfInstructions ())
		maxp_contents.maxSizeOfInstructions = instrdata.instr_cnt;
	    for (int j=0; j<instrdata.instr_cnt; j++)
		os << instrdata.instrs[j];
        }
    }
    uint32_t len = buf.pos () - startpos;
    if (len&3) {
	if (len&1)
	    os << (uint8_t) 0;
	if (len&2)
	    os << (uint16_t) 0;
    }
    maxp->setModified (true);
    return buf.pos ();
}

uint16_t ConicGlyph::appendHint (double start, double width, bool is_v) {
    std::vector<StemInfo> &hints = is_v ? vstem : hstem;
    StemInfo newh = StemInfo ();
    uint16_t lastpos, i;

    newh.start = start;
    newh.width = width;

    if (hints.empty ()) {
        newh.hintnumber = 1;
        hints.push_back (newh);
        return 1;
    }

    lastpos = hints.size () -1;
    if (start >= hints[lastpos].start ||
        (start == hints[lastpos].start && width > hints[lastpos].width)) {
        newh.hintnumber = lastpos + 1;
        hints.push_back (newh);
        return lastpos + 1;
    }

    for (i=0; i<hints.size (); i++) {
        // AMK: there is already a hint with given width and position,
        // so no additional hints should be added.
        // IIUC, this cannot occur in type2
        if (start == hints[i].start && width == hints[i].width)
            return hints[i].hintnumber;
    }

    for (i=0; i<hints.size (); i++) {
        if (start < hints[i].start || (start == hints[i].start && width < hints[i].width)) {
            newh.hintnumber = lastpos + 1;
            hints.insert (hints.begin () + i, newh);
            return lastpos + 1;
        } else if (start == hints[i].start && width > hints[i].width) {
            newh.hintnumber = lastpos + 1;
            hints.insert (hints.begin () + i + 1, newh);
            return lastpos + 1;
        }
    }
    // AMK: should never reach here
    assert (0);
    return 0;
}

bool ConicGlyph::hasHintMasks () {
    uint16_t i, j;

    for (i=0; i<figures.size (); i++) {
        std::vector<ConicPointList> &conics = figures.front ().contours;

        for (j=0; j<conics.size (); j++) {
            ConicPointList *spls = &conics[j];
            ConicPoint *sp = spls->first;
            do {
                if (sp->hintmask)
		    return true;
                sp = (sp->next) ? sp->next->to : nullptr;
            } while (sp && sp != spls->first);
        }
    }
    return false;
}

void ConicGlyph::findTop (BasePoint *top, std::array<double, 6> &transform) {
    uint16_t i, j;
    BasePoint test;

    for (i=0; i<figures.size (); i++) {
        std::vector<ConicPointList> &conics = figures.front ().contours;

        for (j=0; j<conics.size (); j++) {
            ConicPointList &spls = conics[j];
            Conic *conic, *first=nullptr, *last=nullptr;
            for (conic = spls.first->next; conic && conic!=first; conic=conic->to->next) {
                test.transform (&conic->from->me, transform);
                if (test.y > top->y)
                    *top = test;
                last = conic;
                if (!first) first = conic;
            }
            if (!conic && last) {
                test.transform (&last->to->me, transform);
                if (test.y > top->y)
                    *top = test;
            }
        }
    }

    for (auto &ref : refs) {
        assert (ref.cc);
        ref.cc->findTop (top, ref.transform);
    }
}

void ConicGlyph::fromPS (BoostIn &buf, const struct cffcontext &ctx) {
    /* GWW: Type1 stack is about 25 long, Type2 stack is 48 */
    // AMK: increased to 513 in CFF v2
    uint16_t max_stack = ctx.version > 1 ? 513 : 48;
    std::vector<double> stack;
    std::array<double, 32> transient;
    ConicPointList *cur=nullptr;
    int16_t oldpos = -1;
    BasePoint current;
    double dx, dy, dx2, dy2, dx3, dy3, dx4, dy4, dx5, dy5, dx6, dy6;
    ConicPoint *pt;
    /* GWW: subroutines may be nested to a depth of 10 */
    std::vector<BoostIn *> buf_stack;
    std::array<double, 30> pops;
    int popsp=0;
    int base, polarity;
    double coord;
    const struct pschars *s;
    std::unique_ptr<HintMask> pending_hm;
    int cp=0;
    int sp=0;
    uint8_t v;
    bool is_type2 = (ctx.version > 0);

    stack.resize (max_stack+2);
    buf_stack.reserve (11);
    buf_stack.push_back (&buf);

    if (!widthset) m_aw = 0x8000;
    current.x = current.y = 0;
    bb.minx = 0;

    m_private = &ctx.pdict;
    figures.emplace_back ();
    figures.back ().type = "path";
    figures.back ().order2 = false;
    std::vector<ConicPointList> &conics = figures. back ().contours;
    boost::object_pool<ConicPoint> &points_pool = figures.back ().points_pool;
    boost::object_pool<Conic> &splines_pool = figures.back ().splines_pool;

    while (!(buf_stack.size () == 1 && (int) buf_stack.back ()->peek () == EOF)) {
	if ((int) buf_stack.back ()->peek () == EOF) {
	    if (ctx.version > 1) {
		delete buf_stack.back ();
		buf_stack.pop_back ();
		continue;
	    } else
		goto done;
	}
	if (sp>max_stack) {
	    FontShepherd::postError (tr ("Stack got too big"));
	    sp = max_stack;
	}
	base = 0;
	if ((v = buf_stack.back ()->get ())>=32) {
	    if (v<=246) {
		stack[sp++] = v - 139;
	    } else if (v<=250) {
		stack[sp++] =  (v-247)*256 + (uint8_t) buf_stack.back ()->get () + 108;
	    } else if (v<=254) {
		stack[sp++] = -(v-251)*256 - (uint8_t) buf_stack.back ()->get () - 108;
	    } else { /* 255 */
		uint32_t val;
                *buf_stack.back () >> val;
		stack[sp++] = val;
      		/* GWW: The type2 spec is contradictory. It says this is a */
      		/*  two's complement number, but it also says it is a */
      		/*  Fixed, which in truetype is not two's complement */
      		/*  (mantisa is always unsigned) */
		/* AMK: as I read the spec, it mentions a "16-bit signed */
		/* integer with 16 bits of fraction". So mantissa is supposed */
		/* to be unsigned indeed */
		if (is_type2) {
		    uint16_t mant = val&0xffff;
		    stack[sp-1] = ((int16_t) (val>>16)) + mant/65536.;
		}
	    }
	} else if (v==28) {
	    stack[sp++] = (short) ((uint8_t) buf_stack.back ()->get ()<<8 | (uint8_t) buf_stack.back ()->get ());
	/* In the Dict tables of CFF, a 5byte fixed value is prefixed by a */
	/*  29 code. In Type2 strings the prefix is 255. */
	} else if (v==12) {
	    v = buf_stack.back ()->get ();
	    switch (v) {
	      case 0: /* dotsection */
		if (is_type2)
		    FontShepherd::postNotice (tr ("dotsection operator in %1 is deprecated for Type2").arg (GID));
		sp = 0;
              break;
	      case 1: /* vstem3 */	/* specifies three v hints zones at once */
		if (sp<6) FontShepherd::postError (tr ("Stack underflow on vstem3 in %1").arg (GID));
		/* according to the standard, if there is a vstem3 there can't */
		/*  be any vstems, so there can't be any confusion about hint order */
		/*  so we don't need to worry about unblended stuff */
		if (is_type2)
		    FontShepherd::postError (tr ("vstem3 operator in %1 is not supported for Type2").arg (GID));
                else {
                    uint16_t hn1, hn2, hn3;
                    hn1 = appendHint (stack[0] + bb.minx, stack[1], true);
                    hn2 = appendHint (stack[2] + bb.minx, stack[3], true);
                    hn3 = appendHint (stack[4] + bb.minx, stack[5], true);
                    if (hn3<HntMax) {
                        if (!pending_hm)
                            pending_hm = std::unique_ptr<HintMask> (new HintMask ());
			pending_hm->setBit (hn1, true);
			pending_hm->setBit (hn2, true);
			pending_hm->setBit (hn3, true);
                    }
                    sp = 0;
                }
              break;
	      case 2: /* hstem3 */	/* specifies three h hints zones at once */
		if (sp<6) FontShepherd::postError (tr ("Stack underflow on hstem3 in %1").arg (GID));
		if (is_type2)
		    FontShepherd::postError (tr ("hstem3 operator in %1 is not supported for Type2").arg (GID));
                else {
                    uint16_t hn1, hn2, hn3;
                    hn1 = appendHint (stack[0], stack[1], false);
                    hn2 = appendHint (stack[2], stack[3], false);
                    hn3 = appendHint (stack[4], stack[5], false);
                    if (hn3<HntMax) {
                        if (!pending_hm)
                            pending_hm = std::unique_ptr<HintMask> (new HintMask ());
			pending_hm->setBit (hn1, true);
			pending_hm->setBit (hn2, true);
			pending_hm->setBit (hn3, true);
                    }
                    sp = 0;
                    sp = 0;
                }
              break;
	      case 6: {/* seac */	/* build accented characters */
                seac:
		if (sp<5)
		    FontShepherd::postError (tr ("Stack underflow on seac in %1").arg (GID));
		if (is_type2) {
		    if (v==6)
			FontShepherd::postError (tr ("SEAC operator in %1 is invalid for Type2").arg (GID));
		    else
			FontShepherd::postWarning (tr ("SEAC-like endchar in %1 is deprecated for Type2").arg (GID));
		}
		/* stack[0] must be the lsidebearing of the accent. I'm not sure why */
		DrawableReference r1;
		DrawableReference r2;
		r2.transform[0] = 1;
                r2.transform[3] = 1;
		r2.transform[4] = stack[1] - (stack[0]-bb.minx);
		r2.transform[5] = stack[2];
		/* the translation of the accent here is said to be relative */
		/*  to the origins of the base character. I think they place */
		/*  the origin at the left bearing. And they don't mean the  */
		/*  base char at all, they mean the current char's lbearing  */
		/*  (which is normally the same as the base char's, except   */
		/*  when I has a big accent (like diaerisis) */
		r1.transform[0] = 1; r1.transform[3] = 1;
		r1.adobe_enc = stack[3];
		r2.adobe_enc = stack[4];
		if (stack[3]<0 || stack[3]>=256 || stack[4]<0 || stack[4]>=256) {
		    FontShepherd::postError (tr ("Reference encoding out of bounds in %1").arg (GID));
		    r1.adobe_enc = 0;
		    r2.adobe_enc = 0;
		}
                refs.push_back (r1);
                refs.push_back (r2);
		sp = 0;
              } break;
	      case 7: /* sbw */		/* generalized width/sidebearing command */
		if (sp<4) FontShepherd::postError (tr ("Stack underflow on sbw in %1").arg (GID));
		if (is_type2)
		    FontShepherd::postError (tr ("sbw operator in %1 is not supported for Type2").arg (GID));
		m_lsb = stack[0];
		/* GWW: stack[1] is lsidebearing y (only for vertical writing styles, CJK) */
		m_aw = stack[2];
		/* GWW: stack[3] is height (for vertical writing styles, CJK) */
		sp = 0;
              break;
	      case 5: case 9: case 14: case 26:
		if (sp<1) FontShepherd::postError (tr ("Stack underflow on unary operator in %1").arg (GID));
		switch (v) {
		  case 5: stack[sp-1] = (stack[sp-1]==0); break;	/* not */
		  case 9: if ( stack[sp-1]<0 ) stack[sp-1]= -stack[sp-1]; break;	/* abs */
		  case 14: stack[sp-1] = -stack[sp-1]; break;		/* neg */
		  case 26: stack[sp-1] = sqrt(stack[sp-1]); break;	/* sqrt */
		}
              break;
	      case 3: case 4: case 10: case 11: case 12: case 15: case 24:
		if (sp<2)
                    FontShepherd::postError (tr ("Stack underflow on binary operator in %1").arg (GID));
		else {
                    switch (v) {
                      case 3: /* and */
                        stack[sp-2] = (stack[sp-1]!=0 && stack[sp-2]!=0);
                      break;
                      case 4: /* and */
                        stack[sp-2] = (stack[sp-1]!=0 || stack[sp-2]!=0);
                      break;
                      case 10: /* add */
                        stack[sp-2] += stack[sp-1];
                      break;
                      case 11: /* sub */
                        stack[sp-2] -= stack[sp-1];
                      break;
                      case 12: /* div */
                        stack[sp-2] /= stack[sp-1];
                      break;
                      case 24: /* mul */
                        stack[sp-2] *= stack[sp-1];
                      break;
                      case 15: /* eq */
                        stack[sp-2] = (stack[sp-1]==stack[sp-2]);
                      break;
                    }
                }
		--sp;
              break;
	      case 22: /* ifelse */
		if (sp<4) FontShepherd::postError (tr ("Stack underflow on ifelse in %1").arg (GID));
		else {
		    if (stack[sp-2]>stack[sp-1])
			stack[sp-4] = stack[sp-3];
		    sp -= 3;
		}
              break;
	      case 23: /* random */
		/* GWW: This function returns something (0,1]. It's not clear to me*/
		/*  if rand includes 0 and RAND_MAX or not, but this approach */
		/*  should work no matter what */
		do {
		    stack[sp] = (rand ()/(RAND_MAX-1));
		} while (stack[sp]==0 || stack[sp]>1);
		++sp;
              break;
	      case 16: /* callothersubr */
		/* GWW: stack[sp-1] is the number of the thing to call in the othersubr array */
		/* stack[sp-2] is the number of args to grab off our stack and put on the */
		/*  real postscript stack */
		if (is_type2)
		    FontShepherd::postError (tr ("Type2 fonts do not support the Type1 callothersubrs operator"));
		if (sp<2 || sp < 2+stack[sp-2]) {
		    FontShepherd::postError (tr ("Stack underflow on callothersubr in %1").arg (GID));
		    sp = 0;
		} else {
		    int tot = stack[sp-2], i;
		    popsp = 0;
		    for (i=sp-3; i>=sp-2-tot; --i)
			pops[popsp++] = stack[i];
		    /* GWW: othersubrs 0-3 must be interpretted. 0-2 are Flex, 3 is Hint Replacement */
		    /* othersubrs 12,13 are for counter hints. We don't need to */
		    /*  do anything to ignore them */
		    /* Subroutines 14-18 are multiple master blenders. */
		    switch ((int) stack[sp-1]) {
		      case 3: {
			/* GWW: when we weren't capabable of hint replacement we */
			/*  punted by putting 3 on the stack (T1 spec page 70) */
			/*  subroutine 3 is a noop */
			/*pops[popsp-1] = 3;*/
			/* We can manage hint substitution from hintmask though*/
			/*  well enough that we needn't clear the manualhints bit */
                        ; /* AMK: Seems there is no need to do anything here. Is it right? */
                      } break;
		      case 1: {
			/* GWW: Essentially what we want to do is draw a line from */
			/*  where we are at the beginning to where we are at */
			/*  the end. So we save the beginning here (this starts*/
			/*  the flex sequence), we ignore all calls to othersub*/
			/*  2, and when we get to othersub 0 we put everything*/
			/*  back to where it should be and free up whatever */
			/*  extranious junk we created along the way and draw */
			/*  our line. */
			/* Let's punt a little less, and actually figure out */
			/*  the appropriate rrcurveto commands and put in a */
			/*  dished serif */
			/* We should never get here in a type2 font. But we did*/
			/*  this code won't work if we follow type2 conventions*/
			/*  so turn off type2 until we get 0 callothersubrs */
			/*  which marks the end of the flex sequence */
			is_type2 = false;
			if (cur)
			    oldpos = conics.size () - 1;
			else
			    FontShepherd::postError (tr ("Bad flex subroutine in %1").arg (GID));
		      }
                      break;
		      case 2: /* No op */
		      break;
		      case 0:
                        if (oldpos!=-1 && conics.size () > (uint16_t) (oldpos + 6)) {
                            BasePoint old_nextcp, mid_prevcp, mid, mid_nextcp,
                                    end_prevcp, end;
                            old_nextcp	= conics[oldpos+2].first->me;
                            mid_prevcp	= conics[oldpos+3].first->me;
                            mid		= conics[oldpos+4].first->me;
                            mid_nextcp	= conics[oldpos+5].first->me;
                            end_prevcp	= conics[oldpos+6].first->me;
                            end		= conics[oldpos+7].first->me;
                            cur = &conics[oldpos];
                            if (cur->first && (cur->first!=cur->last || !cur->first->next)) {
                                cur->last->nextcp = old_nextcp;
                                cur->last->nonextcp = false;
                                pt = points_pool.construct ();
                                pt->hintmask = std::move (pending_hm);
                                pt->prevcp = mid_prevcp;
                                pt->me = mid;
                                pt->nextcp = mid_nextcp;
                                /*pt->flex = pops[2];*/
                                conics.resize (oldpos+1);
                                splines_pool.construct (cur->last, pt, false);
                                cur->last = pt;
                                pt = points_pool.construct ();
                                pt->prevcp = end_prevcp;
                                pt->me = end;
                                pt->nonextcp = true;
                                splines_pool.construct (cur->last, pt, false);
                                cur->last = pt;
                            } else {
                                /* GWW: Um, something's wrong. Let's just draw a line */
                                /* do the simple method, which consists of creating */
                                /*  the appropriate line */
                                pt = points_pool.construct ();
                                pt->me.x = pops[1]; pt->me.y = pops[0];
                                pt->noprevcp = true; pt->nonextcp = true;
                                conics.resize (oldpos+1);
                                cur = &conics.back ();
                                if (cur->first && (cur->first!=cur->last || !cur->first->next)) {
                                    splines_pool.construct (cur->last, pt, false);
                                    cur->last = pt;
                                } else
                                    FontShepherd::postError (tr ("No previous point on path in lineto from flex 0 in %1").arg (GID));
                            }
                            --popsp;
                        } else
                            FontShepherd::postError (tr ("Bad flex subroutine in %1").arg (GID));

			is_type2 = (ctx.version > 0);
			/* GWW: If we found a type2 font with a type1 flex sequence */
			/*  (an illegal idea, but never mind, someone gave us one)*/
			/*  then we had to turn off type2 untill the end of the */
			/*  flex sequence. Which is here */
		      break;
		      case 14: 		/* results in 1 blended value */
		      case 15:		/* results in 2 blended values */
		      case 16:		/* results in 3 blended values */
		      case 17:		/* results in 4 blended values */
		      case 18:		/* results in 6 blended values */
                        FontShepherd::postError (tr ("Attempt to use a multiple master subroutine in a non-mm font in %1.").arg (GID));
		      break;
		    }
		    sp = i+1;
		}
	      break;
	      case 20: /* put */
		if (sp<2)
                    FontShepherd::postError (tr ("Too few items on stack for put in %1").arg (GID));
		else if (stack[sp-1]<0 || stack[sp-1]>=32)
                    FontShepherd::postError (tr ("Reference to transient memory out of bounds in put in %1").arg (GID));
		else {
		    transient[(int) stack[sp-1]] = stack[sp-2];
		    sp -= 2;
		}
	      break;
	      case 21: /* get */
		if (sp<1)
                    FontShepherd::postError (tr ("Too few items on stack for get in %1").arg (GID));
		else if (stack[sp-1]<0 || stack[sp-1]>=32)
                    FontShepherd::postError (tr ("Reference to transient memory out of bounds in put in %1").arg (GID));
		else
		    stack[sp-1] = transient[(int)stack[sp-1]];
	      break;
	      case 17: /* pop */
		/* GWW: pops something from the postscript stack and pushes it on ours */
		/* used to get a return value from an othersubr call */
		/* Bleah. Adobe wants the pops to return the arguments if we */
		/*  don't understand the call. What use is the subroutine then?*/
		if (popsp<=0)
		    FontShepherd::postError (tr ("Pop stack underflow on pop in %1").arg (GID));
		else
		    stack[sp++] = pops[--popsp];
	      break;
	      case 18: /* drop */
		if ( sp>0 ) --sp;
	      break;
	      case 27: /* dup */
		if ( sp>=1 ) {
		    stack[sp] = stack[sp-1];
		    ++sp;
		}
	      break;
	      case 28: /* exch */
		if ( sp>=2 ) {
		    double temp = stack[sp-1];
		    stack[sp-1] = stack[sp-2]; stack[sp-2] = temp;
		}
	      break;
	      case 29: /* index */
		if (sp>=1) {
		    int index = stack[--sp];
		    if ( index<0 || sp<index+1 )
			FontShepherd::postError (tr ("Index out of range in %1").arg (GID));
		    else {
			stack[sp] = stack[sp-index-1];
			++sp;
		    }
		}
	      break;
	      case 30: /* roll */
		if ( sp>=2 ) {
		    int j = stack[sp-1], N=stack[sp-2];
		    if ( N>sp || j>=N || j<0 || N<0 )
			FontShepherd::postError (tr ("roll out of range in %1").arg (GID));
		    else if ( j==0 || N==0 )
			/* No op */;
		    else {
			double temp[N] = { 0 };
			int i;
			for (i=0; i<N; ++i)
			    temp[i] = stack[sp-N+i];
			for (i=0; i<N; ++i)
			    stack[sp-N+i] = temp[(i+j)%N];
		    }
		}
	      break;
	      case 33: /* setcurrentpoint */
		if (is_type2)
		    FontShepherd::postError (tr ("Type2 fonts do not support the Type1 setcurrentpoint operator"));
		if (sp<2) FontShepherd::postError (tr ("Stack underflow on setcurrentpoint in %1").arg (GID));
		else {
		    current.x = stack[0];
		    current.y = stack[1];
		}
		sp = 0;
	      break;
	      case 34:	/* hflex */
	      case 35:	/* flex */
	      case 36:	/* hflex1 */
	      case 37:	/* flex1 */
		dy = dy3 = dy4 = dy5 = dy6 = 0;
		dx = stack[base++];
		if (v!=34)
		    dy = stack[base++];
		dx2 = stack[base++];
		dy2 = stack[base++];
		dx3 = stack[base++];
		if (v!=34 && v!=36)
		    dy3 = stack[base++];
		dx4 = stack[base++];
		if (v!=34 && v!=36)
		    dy4 = stack[base++];
		dx5 = stack[base++];
		if (v==34)
		    dy5 = -dy2;
		else
		    dy5 = stack[base++];
		switch (v) {
		    double xt, yt;
		    case 35:    /* flex */
			dx6 = stack[base++];
			dy6 = stack[base++];
			break;
		    case 34:    /* hflex */
			dx6 = stack[base++];
			break;
		    case 36:    /* hflex1 */
			dx6 = stack[base++];
			dy6 = -dy-dy2-dy5;
			break;
		    case 37:    /* flex1 */
			xt = dx+dx2+dx3+dx4+dx5;
			yt = dy+dy2+dy3+dy4+dy5;
			if ( xt<0 ) xt= -xt;
			if ( yt<0 ) yt= -yt;
			if ( xt>yt ) {
			    dx6 = stack[base++];
			    dy6 = -dy-dy2-dy3-dy4-dy5;
			} else {
			    dy6 = stack[base++];
			    dx6 = -dx-dx2-dx3-dx4-dx5;
			}
			break;
		}
		if (cur && cur->first && (cur->first!=cur->last || !cur->first->next)) {
		    current.x = rint ((current.x+dx)*1024)/1024;
                    current.y = rint ((current.y+dy)*1024)/1024;
		    cur->last->nextcp.x = current.x;
                    cur->last->nextcp.y = current.y;
		    cur->last->nonextcp = false;
		    current.x = rint ((current.x+dx2)*1024)/1024;
                    current.y = rint ((current.y+dy2)*1024)/1024;
		    pt = points_pool.construct ();
		    pt->hintmask = std::move (pending_hm);
		    pt->prevcp.x = current.x;
                    pt->prevcp.y = current.y;
		    current.x = rint ((current.x+dx3)*1024)/1024;
                    current.y = rint ((current.y+dy3)*1024)/1024;
		    pt->me.x = current.x;
                    pt->me.y = current.y;
		    pt->nonextcp = true;
		    splines_pool.construct (cur->last, pt, false);
		    cur->last = pt;

		    current.x = rint((current.x+dx4)*1024)/1024;
                    current.y = rint((current.y+dy4)*1024)/1024;
		    cur->last->nextcp.x = current.x;
                    cur->last->nextcp.y = current.y;
		    cur->last->nonextcp = false;
		    current.x = rint((current.x+dx5)*1024)/1024;
                    current.y = rint((current.y+dy5)*1024)/1024;
		    pt = points_pool.construct ();
		    pt->prevcp.x = current.x; pt->prevcp.y = current.y;
		    current.x = rint ((current.x+dx6)*1024)/1024; current.y = rint((current.y+dy6)*1024)/1024;
		    pt->me.x = current.x; pt->me.y = current.y;
		    pt->nonextcp = true;
		    splines_pool.construct (cur->last, pt, false);
		    cur->last = pt;
		} else
		    FontShepherd::postError (tr ("No previous point on path in flex operator in %1").arg (GID));
		sp = 0;
	      break;
	      default:
		FontShepherd::postError (tr ("Uninterpreted opcode 12,%1 in %2").arg (v).arg (GID));
	      break;
	    }
	} else {
	    switch (v) {
	      case 1: /* hstem */
              case 18: /* hstemhm */
		base = 0;
		if ((sp&1) && m_aw == 0x8000)
		    m_aw = stack[0];
		if (sp&1)
		    base=1;
		if (sp-base<2)
		    FontShepherd::postError (tr ("Stack underflow on hstem in %1").arg (GID));
		/* GWW: stack[0] is absolute y for start of horizontal hint */
		/*	(actually relative to the y specified as lsidebearing y in sbw*/
		/* stack[1] is relative y for height of hint zone */
		coord = 0;
		while (sp-base>=2) {
		    uint16_t hn = appendHint (stack[base]+coord, stack[base+1], false);
		    if (!is_type2 && hn<HntMax) {
			if (!pending_hm)
			    pending_hm = std::unique_ptr<HintMask> (new HintMask ());
			pending_hm->setBit (hn, true);
		    }
		    coord += (stack[base] + stack[base+1]);
		    base+=2;
		}
		sp = 0;
              break;
              case 19: /* hintmask */
              case 20: /* cntrmask */
		/* GWW: If there's anything on the stack treat it as a vstem hint */
              case 3: /* vstem */
              case 23: /* vstemhm */
		base = 0;
		if (!cur || v==3 || v==23) {
		    if ((sp&1) && is_type2 && m_aw == 0x8000)
			m_aw = stack[0];

		    if (sp&1)
			base=1;
		    /* GWW: I've seen a vstemhm with no arguments. I've no idea what that */
		    /*  means. It came right after a hintmask */
		    /* I'm confused about v/hstemhm because the manual says it needs */
		    /*  to be used if one uses a hintmask, but that's not what the */
		    /*  examples show.  Or I'm not understanding. */
		    if (sp-base<2 && v!=19 && v!=20)
			FontShepherd::postError (tr ("Stack underflow on vstem in %1").arg (GID));
		    /* stack[0] is absolute x for start of vertical hint */
		    /*	(actually relative to the x specified as lsidebearing in h/sbw*/
		    /* stack[1] is relative x for height of hint zone */
		    coord = bb.minx;
		    while (sp-base>=2) {
			uint16_t hn = appendHint (stack[base]+coord, stack[base+1], true);
			if (!is_type2 && hn<HntMax) {
			    if (!pending_hm)
				pending_hm = std::unique_ptr<HintMask> (new HintMask ());
			    pending_hm->setBit (hn, true);
			}
			coord += (stack[base] + stack[base+1]);
			base+=2;
		    }
		    sp = 0;
		}
		if (v==19 || v==20) {		/* hintmask, cntrmask */
		    uint8_t i, bytes = (hstem.size () + vstem.size () +7)/8;
		    HintMask tocopy = HintMask ();
		    if (bytes>sizeof (HintMask)) bytes = sizeof (HintMask);
		    for (i=0; i<bytes; i++)
			*buf_stack.back () >> tocopy[i];
		    if (v==19) {
			if (!pending_hm)
			    pending_hm = std::unique_ptr<HintMask> (new HintMask (tocopy));
		    } else if (cp<HntMax) {
			countermasks.push_back (tocopy);
			++cp;
		    }
		    if (bytes!=(hstem.size () + vstem.size ())/8) {
			int mask = 0xff>>((hstem.size () + vstem.size ()) &7);
			if (tocopy[bytes-1]&mask)
			    FontShepherd::postError (tr ("Hint mask (or counter mask) with too many hints in %1").arg (GID));
		    }
		}
              break;
              case 14: /* endchar */
		/* GWW: endchar is allowed to terminate processing even within a subroutine */
		if ((sp&1) && is_type2 && m_aw == 0x8000)
		    m_aw = stack[0];
		if (ctx.painttype!=2)
		    figures.back ().closepath (cur, is_type2);
		for (size_t i=1; i<buf_stack.size (); i++)
		    delete buf_stack[i];
		buf_stack.resize (1);
		if (sp==4) {
		    /* GWW: In Type2 strings endchar has a depreciated function of doing */
		    /*  a seac (which doesn't exist at all). Except endchar takes */
		    /*  4 args and seac takes 5. Bleah */
		    stack[4] = stack[3]; stack[3] = stack[2]; stack[2] = stack[1]; stack[1] = stack[0];
		    stack[0] = 0;
		    sp = 5;
		    goto seac;
		} else if (sp==5) {
		    /* same as above except also specified a width */
		    stack[0] = 0;
		    goto seac;
		}
		/* GWW: the docs say that endchar must be the last command in a char */
		/*  (or the last command in a subroutine which is the last in the */
		/*  char) So in theory if there's anything left we should complain*/
		/*  In practice though, the EuroFont has a return statement after */
		/*  the endchar in a subroutine. So we won't try to catch that err*/
		/*  and just stop. */
		/* Adobe says it's not an error, but I can't understand their */
		/*  logic */
		if (ctx.version > 1)
		    FontShepherd::postError (tr ("endchar is deprecated for CFF2: found in %1").arg (GID));
		goto done;
              break;
              case 13: /* hsbw (set left sidebearing and width) */
		if (sp<2)
		    FontShepherd::postError (tr ("Stack underflow on hsbw in %1").arg (GID));
		m_lsb = stack[0];
		current.x = stack[0];		/* sets the current point too */
		m_aw = stack[1];
		sp = 0;
              break;
              case 9: /* closepath */
		sp = 0;
		figures.back ().closepath (cur, is_type2);
              break;
              case 21: /* rmoveto */
              case 22: /* hmoveto */
              case 4:  /* vmoveto */
		if (is_type2) {
		    if (((v==21 && sp==3) || (v!=21 && sp==2)) && m_aw == 0x8000)
			/* Character's width may be specified on the first moveto */
			m_aw = stack[0];
		    if (v==21 && sp>2) {
			stack[0] = stack[sp-2]; stack[1] = stack[sp-1];
			sp = 2;
		    } else if ( v!=21 && sp>1 ) {
			stack[0] = stack[sp-1];
			sp = 1;
		    }
		    if (ctx.painttype!=2)
			figures.back ().closepath (cur, true);
		}
	      /* fall through */
              case 5: /* rlineto */
              case 6: /* hlineto */
              case 7: /* vlineto */
		polarity = 0;
		base = 0;
		while (base<sp) {
		    dx = dy = 0;
		    if (v==5 || v==21) {
			if (sp<base+2) {
			    FontShepherd::postError (tr ("Stack underflow on rlineto/rmoveto in %1").arg (GID));
			    break;
			}
			dx = stack[base++];
			dy = stack[base++];
		    } else if ((v==6 && !(polarity&1)) || (v==7 && (polarity&1)) || v==22) {
			if (sp<=base) {
			    FontShepherd::postError (tr ("Stack underflow on hlineto/hmoveto in %1").arg (GID));
			    break;
			}
			dx = stack[base++];
		    } else /*if ( (v==7 && !(parity&1)) || (v==6 && (parity&1) || v==4 )*/ {
			if (sp<=base) {
			    FontShepherd::postError (tr ("Stack underflow on vlineto/vmoveto in %1").arg (GID));
			    break;
			}
			dy = stack[base++];
		    }
		    ++polarity;
		    current.x = rint ((current.x+dx)*1024)/1024;
		    current.y = rint ((current.y+dy)*1024)/1024;
		    pt = points_pool.construct ();
		    pt->hintmask = std::move (pending_hm);
		    pt->me.x = current.x;
		    pt->me.y = current.y;
		    pt->noprevcp = true;
		    pt->nonextcp = true;
		    if (v==4 || v==21 || v==22) {
			if (cur && cur->first==cur->last && !cur->first->prev && is_type2) {
			    /* Two adjacent movetos should not create single point paths */
			    cur->first->me.x = current.x;
			    cur->first->me.y = current.y;
			    points_pool.destroy (pt);
			} else {
			    ConicPointList newss = ConicPointList ();
			    pt->isfirst = true;
			    newss.first = newss.last = pt;
			    conics.push_back (newss);
			    cur = &conics.back ();
			}
			break;
		    } else {
			if (cur && cur->first && (cur->first!=cur->last || !cur->first->next)) {
			    splines_pool.construct (cur->last, pt, false);
			    cur->last = pt;
			} else
			    FontShepherd::postError (tr ("No previous point on path in lineto in %1").arg (GID));
			if (!is_type2)
			    break;
		    }
		}
		sp = 0;
              break;
              case 25: /* rlinecurve */
		base = 0;
		while ( sp>base+6 ) {
		    current.x = rint ((current.x+stack[base++])*1024)/1024;
		    current.y = rint ((current.y+stack[base++])*1024)/1024;
		    if (cur) {
			pt = points_pool.construct ();
			pt->hintmask = std::move (pending_hm);
			pt->me.x = current.x;
			pt->me.y = current.y;
			pt->noprevcp = true; pt->nonextcp = true;
			splines_pool.construct (cur->last, pt, false);
			cur->last = pt;
		    }
		}
	      /* fall through */
              case 24: /* rcurveline */
              case 8:  /* rrcurveto */
              case 31: /* hvcurveto */
              case 30: /* vhcurveto */
              case 27: /* hhcurveto */
              case 26: /* vvcurveto */
		polarity = 0;
		while ( sp>base+2 ) {
		    dx = dy = dx2 = dy2 = dx3 = dy3 = 0;
		    if (v==8 || v==25 || v==24) {
			if ( sp<6+base ) {
			    FontShepherd::postError (tr ("Stack underflow on rrcurveto in %1").arg (GID));
			    base = sp;
			} else {
			    dx = stack[base++];
			    dy = stack[base++];
			    dx2 = stack[base++];
			    dy2 = stack[base++];
			    dx3 = stack[base++];
			    dy3 = stack[base++];
			}
		    } else if (v==27) {		/* hhcurveto */
			if (sp<4+base) {
			    FontShepherd::postError (tr ("Stack underflow on hhcurveto in %1").arg (GID));
			    base = sp;
			} else {
			    if ( (sp-base)&1 ) dy = stack[base++];
			    dx = stack[base++];
			    dx2 = stack[base++];
			    dy2 = stack[base++];
			    dx3 = stack[base++];
			}
		    } else if (v==26) {		/* vvcurveto */
			if (sp<4+base) {
			    FontShepherd::postError (tr ("Stack underflow on hhcurveto in %1").arg (GID));
			    base = sp;
			} else {
			    if ( (sp-base)&1 ) dx = stack[base++];
			    dy = stack[base++];
			    dx2 = stack[base++];
			    dy2 = stack[base++];
			    dy3 = stack[base++];
			}
		    } else if ((v==31 && !(polarity&1)) || (v==30 && (polarity&1))) {
			if (sp<4+base) {
			    FontShepherd::postError (tr ("Stack underflow on hvcurveto in %1").arg (GID));
			    base = sp;
			} else {
			    dx = stack[base++];
			    dx2 = stack[base++];
			    dy2 = stack[base++];
			    dy3 = stack[base++];
			    if ( sp==base+1 )
				dx3 = stack[base++];
			}
		    } else /*if ( (v==30 && !(polarity&1)) || (v==31 && (polarity&1)) )*/ {
			if (sp<4+base) {
			    FontShepherd::postError (tr ("Stack underflow on vhcurveto in %1").arg (GID));
			    base = sp;
			} else {
			    dy = stack[base++];
			    dx2 = stack[base++];
			    dy2 = stack[base++];
			    dx3 = stack[base++];
			    if ( sp==base+1 )
				dy3 = stack[base++];
			}
		    }
		    ++polarity;
		    if (cur && cur->first && (cur->first!=cur->last || !cur->first->next)) {
			current.x = rint((current.x+dx)*1024)/1024;
			current.y = rint((current.y+dy)*1024)/1024;
			cur->last->nextcp.x = current.x;
			cur->last->nextcp.y = current.y;
			cur->last->nonextcp = false;
			current.x = rint ((current.x+dx2)*1024)/1024;
			current.y = rint ((current.y+dy2)*1024)/1024;
			pt = points_pool.construct ();
			pt->hintmask = std::move (pending_hm);
			pt->prevcp.x = current.x;
			pt->prevcp.y = current.y;
			current.x = rint ((current.x+dx3)*1024)/1024;
			current.y = rint ((current.y+dy3)*1024)/1024;
			pt->me.x = current.x;
			pt->me.y = current.y;
			pt->nonextcp = true;
			splines_pool.construct (cur->last, pt, false);
			cur->last = pt;
		    } else
			FontShepherd::postError (tr ("No previous point on path in curveto in %1").arg (GID));
		}
		if ( v==24 ) {
		    current.x = rint ((current.x+stack[base++])*1024)/1024;
		    current.y = rint ((current.y+stack[base++])*1024)/1024;
		    if (cur) {	/* In legal code, cur can't be null here, but I got something illegal... */
			pt = points_pool.construct ();
			pt->hintmask = std::move (pending_hm);
			pt->me.x = current.x;
			pt->me.y = current.y;
			pt->noprevcp = true;
			pt->nonextcp = true;
			splines_pool.construct (cur->last, pt, false);
			cur->last = pt;
		    }
		}
		sp = 0;
              break;
              case 29: /* callgsubr */
              case 10: /* callsubr */
		/* stack[sp-1] contains the number of the subroutine to call */
		if (sp<1) {
		    FontShepherd::postError (tr ("Stack underflow on callsubr in %1").arg (GID));
		    break;
		} else if (buf_stack.size () > 10) {
		    FontShepherd::postError (tr ("Too many subroutine calls in %1").arg (GID));
		    break;
		}
		s = &ctx.lsubrs;
		if (v==29) s = &ctx.gsubrs;
		if (s) stack[sp-1] += s->bias;
		/* GWW: Type2 subrs have a bias that must be added to the subr-number */
		/* Type1 subrs do not. We set the bias on them to 0 */
		if (!s || stack[sp-1]>=s->cnt || stack[sp-1]<0 || s->css[(int) stack[sp-1]].sdata.empty ()) {
		    FontShepherd::postError (tr ("Subroutine number out of bounds in %1").arg (GID));
		} else {
		    uint16_t len = s->css[(int) stack[sp-1]].sdata.length ();
		    buf_stack.push_back (new BoostIn (s->css[(int) stack[sp-1]].sdata.c_str (), len));
		}
		if (--sp<0) sp = 0;
              break;
              case 11: /* return */
		/* return from a subroutine */
		if (buf_stack.size () < 1)
		    FontShepherd::postError (tr ("return when not in subroutine in %1").arg (GID));
		else {
		    delete buf_stack.back ();
		    buf_stack.pop_back ();
		}
		if (ctx.version > 1)
		    FontShepherd::postError (tr ("return is deprecated for CFF2: found in %1").arg (GID));
	      break;
	      // vsindex -- added in CFF2
	      case 15:
		ctx.vstore.index = stack[sp-1];
	      break;
	      // blend -- obsolete multiple master operator, now relevant again for CFF2
              case 16:
		if (ctx.version < 2) {
		    FontShepherd::postError (tr ("Attempt to use a multiple master subroutine in a non-mm font."));
		} else {
		    // Currently just attemting to skip the deltas and show the default design,
		    // but it is not so trivial to do this correctly
		    double n_base = stack[sp-1];
		    if (ctx.vstore.data.size () > ctx.vstore.index) {
			int n_regions = ctx.vstore.data[ctx.vstore.index].regionIndexes.size ();
			if (sp >= n_base*(n_regions+1) + 1)
			    sp -= (n_base*(n_regions) + 1);
			else
			    FontShepherd::postError (tr (
				"Stack depth on blend operator is %1, while at least %2 is expected.")
				.arg (sp).arg (n_base*(n_regions+1) + 1));
		    } else {
			FontShepherd::postError (tr (
			    "Blend operator in CFF charstring, while no Variation Data available"));
		    }
		}
	      break;
              default:
                FontShepherd::postError (tr ("Uninterpreted opcode %1 in %2").arg (v).arg (GID));
	      break;
	    }
        }
    }
    done:
    if (buf_stack.size () > 1 && (ctx.version < 2))
	FontShepherd::postError (tr ("end of subroutine reached with no return in %1").arg (GID));
    // endchar is implicit in CFF2
    if (ctx.version > 1)
        figures.back ().closepath (cur, is_type2);
    categorizePoints ();

    /* GWW: Even in type1 fonts all paths should be closed. But if we close them at*/
    /*  the obvious moveto, that breaks flex hints. So we have a hack here at */
    /*  the end which closes any open paths. */
    /* If we do have a PaintType 2 font, then presumably the difference between*/
    /*  open and closed paths matters */
    if (!is_type2 && !ctx.painttype) {
        for (uint16_t i=0; i<conics.size (); i++) {
            cur = &conics[i];
	    splines_pool.construct (cur->last, cur->first, false);
	    cur->last = cur->first;
	}
    }
    checkBounds (bb);
    figures.back ().renumberPoints ();
    m_outType = OutlinesType::PS;
}

#define FLEX_DEPTH 5

static uint16_t point_flexible (ConicPoint *mid) {
    if (!mid->prev || !mid->next || mid->prev == mid->next || mid->hintmask ||
	mid->prev->islinear || mid->next->islinear)
	return 0;

    ConicPoint *sp1 = mid->prev->from;
    ConicPoint *sp2 = mid->next->to;
    if (realNear (sp1->me.y, sp2->me.y) && fabs (mid->me.y - sp1->me.y) <= FLEX_DEPTH) {
	if (realNear (mid->prevcp.y, mid->me.y) && realNear (mid->me.y, mid->nextcp.y)) {
	    if (realNear (sp1->me.y, sp1->nextcp.y) && realNear (sp2->prevcp.y, sp2->me.y))
		return cff::cs::hflex;
	    else
		return cff::cs::hflex1;
	} else {
	    return cff::cs::flex1;
	}
    } else if (realNear (sp1->me.x, sp2->me.x) && fabs (mid->me.x - sp1->me.x) <= FLEX_DEPTH) {
	return cff::cs::flex1;
    }
    return 0;
}

static void ps_start_contour (std::vector<std::pair<int, std::string>> &splitted,
    ConicPoint *start, BasePoint &prevpt, uint8_t hm_len) {
    BasePoint curpt = start->me;
    std::stringstream ss;
    int oper;

    if (start->hintmask) {
	CffTable::encodeOper (ss, cff::cs::hintmask);
	ss.write (reinterpret_cast<char*> (start->hintmask->byte), hm_len);
	splitted.emplace_back (std::make_pair (cff::cs::hintmask, ss.str ()));
        ss.str (std::string ());
    }
    if (!realNear (prevpt.x, curpt.x) && !realNear (prevpt.y, curpt.y)) {
	CffTable::encodeFixed (ss, curpt.x - prevpt.x);
	CffTable::encodeFixed (ss, curpt.y - prevpt.y);
	oper = cff::cs::rmoveto;
    } else if (realNear (prevpt.x, curpt.x)) {
	CffTable::encodeFixed (ss, curpt.y - prevpt.y);
	oper = cff::cs::vmoveto;
    } else {
	CffTable::encodeFixed (ss, curpt.x - prevpt.x);
	oper = cff::cs::hmoveto;
    }
    CffTable::encodeOper (ss, oper);
    splitted.emplace_back (std::make_pair (oper, ss.str ()));
    ss.str (std::string ());
}

static bool spline_representable (Conic *spl, uint8_t op, bool even, bool first, bool last=false) {
    if (point_flexible (spl->to))
	return false;
    bool linear = spl->islinear;
    bool hstart = realNear (spl->from->me.y, spl->from->nextcp.y);
    bool vstart = realNear (spl->from->me.x, spl->from->nextcp.x);
    bool hend = realNear (spl->to->prevcp.y, spl->to->me.y);
    bool vend = realNear (spl->to->prevcp.x, spl->to->me.x);
    bool is_h = linear && realNear (spl->from->me.y, spl->to->me.y);
    bool is_v = linear && realNear (spl->from->me.x, spl->to->me.x);

    switch (op) {
      case cff::cs::hlineto:
	return (is_h && !even) || (is_v && even);
      case cff::cs::vlineto:
	return (is_v && !even) || (is_h && even);
      case cff::cs::rlineto:
	return (linear && !is_h && !is_v);
      case cff::cs::hhcurveto:
	return !linear && ((first && !vstart && hend) || (hstart && hend));
      case cff::cs::vvcurveto:
	return !linear && ((first && !hstart && vend) || (vstart && vend));
      case cff::cs::hvcurveto:
	return !linear && ((hstart && (last || vend) && !even) || (vstart && (last || hend) && even));
      case cff::cs::vhcurveto:
	return !linear && ((vstart && (last || hend) && !even) || (hstart && (last || vend) && even));
      case cff::cs::rrcurveto:
        return !linear && !(hstart || vstart || hend || vend);
      default:
	return false;
    }
}

static BasePoint ps_encode_contour (std::vector<std::pair<int, std::string>> &splitted,
    ConicPointList &spls, BasePoint prevpt, uint8_t hm_len, int version) {
    Conic *spl, *first=nullptr;
    bool even;
    size_t stack=0, maxstack=(version > 1) ? 512 : 48;
    std::stringstream ss;

    ps_start_contour (splitted, spls.first, prevpt, hm_len);
    for (spl = spls.first->next; spl && spl!=first; ) {
	int oper;
        if (!first) first = spl;
	// the starting point hintmask has to be encoded before the initial moveto
	if (spl->from->hintmask && spl->from != spls.first) {
	    CffTable::encodeOper (ss, cff::cs::hintmask);
	    ss.write (reinterpret_cast<char*> (spl->from->hintmask->byte), hm_len);
	    splitted.emplace_back (std::make_pair (cff::cs::hintmask, ss.str ()));
	    ss.str (std::string ());
	}
	uint16_t flex_op = point_flexible (spl->to);
	if (flex_op) {
	    ConicPoint *sp1 = spl->from;
	    ConicPoint *mid = spl->to;
	    ConicPoint *sp2 = spl->to->next->to;
	    switch (flex_op) {
	      case cff::cs::hflex:
		CffTable::encodeFixed (ss, sp1->nextcp.x - sp1->me.x);
		CffTable::encodeFixed (ss, mid->prevcp.x - sp1->nextcp.x);
		CffTable::encodeFixed (ss, mid->me.y - sp1->me.y);
		CffTable::encodeFixed (ss, mid->me.x - mid->prevcp.x);
		CffTable::encodeFixed (ss, mid->nextcp.x - mid->me.x);
		CffTable::encodeFixed (ss, sp2->prevcp.x - mid->nextcp.x);
		CffTable::encodeFixed (ss, sp2->me.x - sp2->prevcp.x);
		break;
	      case cff::cs::hflex1:
		CffTable::encodeFixed (ss, sp1->nextcp.x - sp1->me.x);
		CffTable::encodeFixed (ss, sp1->nextcp.y - sp1->me.y);
		CffTable::encodeFixed (ss, mid->prevcp.x - sp1->nextcp.x);
		CffTable::encodeFixed (ss, mid->prevcp.y - sp1->nextcp.y);
		CffTable::encodeFixed (ss, mid->me.x - mid->prevcp.x);
		CffTable::encodeFixed (ss, mid->nextcp.x - mid->me.x);
		CffTable::encodeFixed (ss, sp2->prevcp.x - mid->nextcp.x);
		CffTable::encodeFixed (ss, sp2->prevcp.y - mid->nextcp.y);
		CffTable::encodeFixed (ss, sp2->me.x - sp2->prevcp.x);
		break;
	      case cff::cs::flex1:
		CffTable::encodeFixed (ss, sp1->nextcp.x - sp1->me.x);
		CffTable::encodeFixed (ss, sp1->nextcp.y - sp1->me.y);
		CffTable::encodeFixed (ss, mid->prevcp.x - sp1->nextcp.x);
		CffTable::encodeFixed (ss, mid->prevcp.y - sp1->nextcp.y);
		CffTable::encodeFixed (ss, mid->me.x - mid->prevcp.x);
		CffTable::encodeFixed (ss, mid->me.y - mid->prevcp.y);
		CffTable::encodeFixed (ss, mid->nextcp.x - mid->me.x);
		CffTable::encodeFixed (ss, mid->nextcp.y - mid->me.y);
		CffTable::encodeFixed (ss, sp2->prevcp.x - mid->nextcp.x);
		CffTable::encodeFixed (ss, sp2->prevcp.y - mid->nextcp.y);
		CffTable::encodeFixed (ss,
		    realNear (sp2->me.y, sp1->me.y) ? sp2->me.x - sp2->prevcp.x : sp2->me.y - sp2->prevcp.y);
		break;
	      default:
		;
	    }
	    oper = flex_op;
	    stack = 0;
	    spl = spl->to->next->to->next;
	} else if (spline_representable (spl, cff::cs::hlineto, false, true)) {
	    even = false;
	    do {
		CffTable::encodeFixed (ss, even ? spl->to->me.y - spl->from->me.y : spl->to->me.x - spl->from->me.x);
		even = !even;
		spl = spl->to->next;
		stack++;
	    } while (spl && spl != first && !spl->from->hintmask && stack < maxstack &&
		spline_representable (spl, cff::cs::hlineto, even, false));
	    oper = cff::cs::hlineto;
	    stack = 0;
	} else if (spline_representable (spl, cff::cs::vlineto, false, true)) {
	    even = false;
	    do {
		CffTable::encodeFixed (ss, even ? spl->to->me.x - spl->from->me.x : spl->to->me.y - spl->from->me.y);
		even = !even;
		spl = spl->to->next;
		stack++;
	    } while (spl && spl != first && !spl->from->hintmask && stack < maxstack &&
		spline_representable (spl, cff::cs::vlineto, even, false));
	    oper = cff::cs::vlineto;
	    stack = 0;
	} else if (spline_representable (spl, cff::cs::rlineto, false, true)) {
	    do {
		CffTable::encodeFixed (ss, spl->to->me.x - spl->from->me.x);
		CffTable::encodeFixed (ss, spl->to->me.y - spl->from->me.y);
		spl = spl->to->next;
		stack+=2;
	    } while (spl && spl != first && !spl->from->hintmask && stack < maxstack &&
		spline_representable (spl, cff::cs::rlineto, false, false));
	    if (spl && spl != first && !spl->from->hintmask && stack < (maxstack-6) &&
		spline_representable (spl, cff::cs::rrcurveto, false, true)) {
		CffTable::encodeFixed (ss, spl->from->nextcp.x - spl->from->me.x);
		CffTable::encodeFixed (ss, spl->from->nextcp.y - spl->from->me.y);
		CffTable::encodeFixed (ss, spl->to->prevcp.x - spl->from->nextcp.x);
		CffTable::encodeFixed (ss, spl->to->prevcp.y - spl->from->nextcp.y);
		CffTable::encodeFixed (ss, spl->to->me.x - spl->to->prevcp.x);
		CffTable::encodeFixed (ss, spl->to->me.y - spl->to->prevcp.y);
		spl = spl->to->next;
		oper = cff::cs::rlinecurve;
	    } else
		oper = cff::cs::rlineto;
	    stack = 0;
	} else if (spline_representable (spl, cff::cs::hhcurveto, false, true)) {
	    if (!realNear (spl->from->me.y, spl->from->nextcp.y))
		CffTable::encodeFixed (ss, spl->from->nextcp.y - spl->from->me.y);
	    do {
		CffTable::encodeFixed (ss, spl->from->nextcp.x - spl->from->me.x);
		CffTable::encodeFixed (ss, spl->to->prevcp.x - spl->from->nextcp.x);
		CffTable::encodeFixed (ss, spl->to->prevcp.y - spl->from->nextcp.y);
		CffTable::encodeFixed (ss, spl->to->me.x - spl->to->prevcp.x);
		spl = spl->to->next;
		stack += 4;
	    } while (spl && spl != first && !spl->from->hintmask && stack < maxstack &&
		spline_representable (spl, cff::cs::hhcurveto, false, false));
	    oper = cff::cs::hhcurveto;
	    stack = 0;
	} else if (spline_representable (spl, cff::cs::vvcurveto, false, true)) {
	    if (!realNear (spl->from->me.x, spl->from->nextcp.x))
		CffTable::encodeFixed (ss, spl->from->nextcp.x - spl->from->me.x);
	    do {
		CffTable::encodeFixed (ss, spl->from->nextcp.y - spl->from->me.y);
		CffTable::encodeFixed (ss, spl->to->prevcp.x - spl->from->nextcp.x);
		CffTable::encodeFixed (ss, spl->to->prevcp.y - spl->from->nextcp.y);
		CffTable::encodeFixed (ss, spl->to->me.y - spl->to->prevcp.y);
		spl = spl->to->next;
		stack += 4;
	    } while (spl && spl != first && !spl->from->hintmask && stack < maxstack &&
		spline_representable (spl, cff::cs::vvcurveto, false, false));
	    oper = cff::cs::vvcurveto;
	    stack = 0;
	} else if (spline_representable (spl, cff::cs::hvcurveto, false, true)) {
	    even = false;
	    do {
		CffTable::encodeFixed (ss, even ? spl->from->nextcp.y - spl->from->me.y : spl->from->nextcp.x - spl->from->me.x);
		CffTable::encodeFixed (ss, spl->to->prevcp.x - spl->from->nextcp.x);
		CffTable::encodeFixed (ss, spl->to->prevcp.y - spl->from->nextcp.y);
		CffTable::encodeFixed (ss, even ? spl->to->me.x - spl->to->prevcp.x : spl->to->me.y - spl->to->prevcp.y);
		even = !even;
		spl = spl->to->next;
		stack += 4;
	    } while (spl && spl != first && !spl->from->hintmask && stack < maxstack &&
		spline_representable (spl, cff::cs::hvcurveto, even, false));
	    if (spl && spl != first && !spl->from->hintmask && stack < (maxstack-5) &&
		spline_representable (spl, cff::cs::hvcurveto, even, false, true)) {
		CffTable::encodeFixed (ss, even ? spl->from->nextcp.y - spl->from->me.y : spl->from->nextcp.x - spl->from->me.x);
		CffTable::encodeFixed (ss, spl->to->prevcp.x - spl->from->nextcp.x);
		CffTable::encodeFixed (ss, spl->to->prevcp.y - spl->from->nextcp.y);
		CffTable::encodeFixed (ss, even ? spl->to->me.x - spl->to->prevcp.x : spl->to->me.y - spl->to->prevcp.y);
		CffTable::encodeFixed (ss, even ? spl->to->me.y - spl->to->prevcp.y : spl->to->me.x - spl->to->prevcp.x);
		spl = spl->to->next;
	    }
	    oper = cff::cs::hvcurveto;
	    stack = 0;
	// The start is horizontal, but the end is neither horizontal nor vertical.
	// Can encode one single hvcurveto op, but no loop.
	} else if (spline_representable (spl, cff::cs::hvcurveto, false, false, true)) {
	    CffTable::encodeFixed (ss, spl->from->nextcp.x - spl->from->me.x);
	    CffTable::encodeFixed (ss, spl->to->prevcp.x - spl->from->nextcp.x);
	    CffTable::encodeFixed (ss, spl->to->prevcp.y - spl->from->nextcp.y);
	    CffTable::encodeFixed (ss, spl->to->me.y - spl->to->prevcp.y);
	    CffTable::encodeFixed (ss, spl->to->me.x - spl->to->prevcp.x);
	    spl = spl->to->next;
	    oper = cff::cs::hvcurveto;
	    stack = 0;
	} else if (spline_representable (spl, cff::cs::vhcurveto, false, true)) {
	    even = false;
	    do {
		CffTable::encodeFixed (ss, even ? spl->from->nextcp.x - spl->from->me.x : spl->from->nextcp.y - spl->from->me.y);
		CffTable::encodeFixed (ss, spl->to->prevcp.x - spl->from->nextcp.x);
		CffTable::encodeFixed (ss, spl->to->prevcp.y - spl->from->nextcp.y);
		CffTable::encodeFixed (ss, even ? spl->to->me.y - spl->to->prevcp.y : spl->to->me.x - spl->to->prevcp.x);
		even = !even;
		spl = spl->to->next;
		stack+=4;
	    } while (spl && spl != first && !spl->from->hintmask && stack < maxstack &&
		spline_representable (spl, cff::cs::vhcurveto, even, false));
	    if (spl && spl != first && !spl->from->hintmask && stack < (maxstack-5) &&
		spline_representable (spl, cff::cs::vhcurveto, even, false, true)) {
		CffTable::encodeFixed (ss, even ? spl->from->nextcp.x - spl->from->me.x : spl->from->nextcp.y - spl->from->me.y);
		CffTable::encodeFixed (ss, spl->to->prevcp.x - spl->from->nextcp.x);
		CffTable::encodeFixed (ss, spl->to->prevcp.y - spl->from->nextcp.y);
		CffTable::encodeFixed (ss, even ? spl->to->me.y - spl->to->prevcp.y : spl->to->me.x - spl->to->prevcp.x);
		CffTable::encodeFixed (ss, even ? spl->to->me.x - spl->to->prevcp.x : spl->to->me.y - spl->to->prevcp.y);
		spl = spl->to->next;
	    }
	    oper = cff::cs::vhcurveto;
	    stack = 0;
	// The start is vertical, but the end is neither horizontal nor vertical.
	// Can encode one single vhcurveto op, but no loop.
	} else if (spline_representable (spl, cff::cs::vhcurveto, false, false, true)) {
	    CffTable::encodeFixed (ss, spl->from->nextcp.y - spl->from->me.y);
	    CffTable::encodeFixed (ss, spl->to->prevcp.x - spl->from->nextcp.x);
	    CffTable::encodeFixed (ss, spl->to->prevcp.y - spl->from->nextcp.y);
	    CffTable::encodeFixed (ss, spl->to->me.x - spl->to->prevcp.x);
	    CffTable::encodeFixed (ss, spl->to->me.y - spl->to->prevcp.y);
	    spl = spl->to->next;
	    oper = cff::cs::vhcurveto;
	    stack = 0;
	} else if (spline_representable (spl, cff::cs::rrcurveto, false, true)) {
	    do {
		CffTable::encodeFixed (ss, spl->from->nextcp.x - spl->from->me.x);
		CffTable::encodeFixed (ss, spl->from->nextcp.y - spl->from->me.y);
		CffTable::encodeFixed (ss, spl->to->prevcp.x - spl->from->nextcp.x);
		CffTable::encodeFixed (ss, spl->to->prevcp.y - spl->from->nextcp.y);
		CffTable::encodeFixed (ss, spl->to->me.x - spl->to->prevcp.x);
		CffTable::encodeFixed (ss, spl->to->me.y - spl->to->prevcp.y);
		spl = spl->to->next;
		stack += 6;
	    } while (spl && spl != first && !spl->from->hintmask && stack < maxstack &&
		spline_representable (spl, cff::cs::rrcurveto, false, false));
	    if (spl && spl != first && !spl->from->hintmask && stack < (maxstack-2) &&
		spline_representable (spl, cff::cs::rlineto, false, true)) {
		CffTable::encodeFixed (ss, spl->to->me.x - spl->from->me.x);
		CffTable::encodeFixed (ss, spl->to->me.y - spl->from->me.y);
		spl = spl->to->next;
		oper = cff::cs::rcurveline;
	    } else
		oper = cff::cs::rrcurveto;
	    stack = 0;
	} else {
	    std::cerr << "Something got wrong: could not encode spline from "
		<< spl->from->me.x << ',' << spl->from->me.y << " to "
		<< spl->to->me.x << ',' << spl->to->me.y << std::endl;
	    spl = spl->to->next;
	}
	CffTable::encodeOper (ss, oper);
	splitted.emplace_back (std::make_pair (oper, ss.str ()));
        ss.str (std::string ());
    }
    return (spls.last->me);
}

void ConicGlyph::splitToPS (std::vector<std::pair<int, std::string>> &splitted, const struct cffcontext &ctx) {
    const PrivateDict &pd = ctx.pdict;
    int stdw = pd.has_key (cff::defaultWidthX) ? pd[cff::defaultWidthX].i : 0;
    int nomw = pd.has_key (cff::nominalWidthX) ? pd[cff::defaultWidthX].i : 0;
    int version = ctx.version;
    static bool refs_warned = false;
    std::stringstream ss;

    if (!refs.empty ()) {
	if (!refs_warned) {
	    FontShepherd::postWarning (
		tr ("References in CFF font"),
		tr ("There are some composite glyphs in this font. "
		    "Glyph references aren't supported by the CFF "
		    "format, so I will convert them to splines."),
		nullptr);
	    refs_warned = true;
	}
	unlinkRefs (false);
    }

    if (version < 2 && this->advanceWidth () != stdw) {
        CffTable::encodeInt (ss, this->advanceWidth () - nomw);
	splitted.emplace_back (std::make_pair (-2, ss.str ()));
	ss.str (std::string ());
    }
    if (this->hstem.size ()) {
	double laststempos = 0;
	for (size_t i=0; i<this->hstem.size (); i++) {
	    CffTable::encodeFixed (ss, this->hstem[i].start - laststempos);
	    CffTable::encodeFixed (ss, this->hstem[i].width);
	    laststempos = this->hstem[i].start + this->hstem[i].width;
	}
	int oper = this->hasHintMasks () ? cff::cs::hstemhm : cff::cs::hstem;
	CffTable::encodeOper (ss, oper);
	splitted.emplace_back (std::make_pair (oper, ss.str ()));
	ss.str (std::string ());
    }
    if (this->vstem.size ()) {
	double laststempos = 0;
	for (size_t i=0; i<this->vstem.size (); i++) {
	    CffTable::encodeFixed (ss, this->vstem[i].start - laststempos);
	    CffTable::encodeFixed (ss, this->vstem[i].width);
	    laststempos = this->vstem[i].start + this->vstem[i].width;
	}
	int oper = this->hasHintMasks () ? cff::cs::vstemhm : cff::cs::vstem;
	CffTable::encodeOper (ss, oper);
	splitted.emplace_back (std::make_pair (oper, ss.str ()));
	ss.str (std::string ());
    }
    for (size_t i=0; i<this->countermasks.size (); i++) {
	CffTable::encodeOper (ss, cff::cs::cntrmask);
	uint8_t nbytes = (hstem.size() + vstem.size () +7)/8;
	for (uint8_t j=0; j<nbytes; j++)
	    ss.put (this->countermasks[i][j]);
	splitted.emplace_back (std::make_pair (cff::cs::cntrmask, ss.str ()));
	ss.str (std::string ());
    }

    uint8_t hm_len = (hstem.size() + vstem.size () +7)/8;
    for (auto &fig : figures) {
        std::vector<ConicPointList> &conics = fig.contours;
	BasePoint pos {0, 0};

        for (size_t j=0; j<conics.size (); j++)
	    pos = ps_encode_contour (splitted, conics[j], pos, hm_len, version);
    }

    if (version < 2) {
	CffTable::encodeOper (ss, cff::cs::endchar);
	splitted.emplace_back (std::make_pair (cff::cs::endchar, ss.str ()));
	ss.str (std::string ());
    }
}

uint32_t ConicGlyph::toPS (QBuffer &buf, QDataStream &os, const struct cffcontext &ctx) {
    std::vector<std::pair<int, std::string>> splitted;
    splitToPS (splitted, ctx);
    for (auto &pair: splitted) {
	// NB: can't just pass a null-terminated string to the overloaded << operator,
	// as it is serialized with QDataStream::writeBytes. And writeBytes does
	// the same thing as writeRawData, except that it prepends the count of bytes
	// written to the data itself, which makes it pretty useless for most cases
	os.writeRawData (pair.second.c_str (), pair.second.length ());
    }
    return buf.pos ();
}

bool ConicGlyph::isEmpty () {
    bool has_contours = false;
    for (auto &fig : figures) {
	if (fig.type != "path" || fig.contours.size ()) {
	    has_contours = true;
	    break;
	}
    }
    return (!has_contours && refs.empty ());
}

bool ConicGlyph::isModified () const {
    return !m_undoStack->isClean ();
}

void ConicGlyph::setModified (bool val) {
    if (val) m_undoStack->resetClean ();
    else m_undoStack->setClean ();
}

void ConicGlyph::setOutlinesType (OutlinesType val) {
    m_outType = val;
}

void ConicGlyph::checkBounds (DBounds &b, bool quick, const std::array<double, 6> &transform, bool dotransform) {
    b.minx = b.miny = 1e10;
    b.maxx = b.maxy = -1e10;

    for (auto &orig : figures) {
        // Make a placeholder for a figure which can be later referred to, but don't
        // create the figure itself, unless we really need it
        std::vector<DrawableFigure> fcopy;
        if (dotransform) {
            fcopy.emplace_back (orig);
            for (auto &spls : fcopy[0].contours)
                spls.doTransform (transform);
        }
        DrawableFigure &fig = !dotransform ? orig : fcopy[0];
        if (quick)
            fig.quickBounds (b);
        else
            fig.realBounds (b);
    }

    for (auto &ref : refs) {
        DBounds rb;
        std::array<double, 6> rtrans;
        if (ref.cc) {
            if (dotransform)
                matMultiply (transform.data (), ref.transform.data (), rtrans.data ());
            else
		rtrans = ref.transform;
            ref.cc->checkBounds (rb, quick, rtrans);
            if (rb.minx < b.minx) b.minx = rb.minx;
            if (rb.miny < b.miny) b.miny = rb.miny;
            if (rb.maxx > b.maxx) b.maxx = rb.maxx;
            if (rb.maxy > b.maxy) b.maxy = rb.maxy;
        }
    }

    if (b.minx>65536) b.minx = .0;
    if (b.miny>65536) b.miny = .0;
    if (b.maxx<-65536) b.maxx = .0;
    if (b.maxy<-65536) b.maxy = .0;
}

void ConicGlyph::setHMetrics (int lsb, int aw) {
    m_lsb = lsb;
    m_aw = aw;
    widthset = true;
}

uint16_t ConicGlyph::gid () {
    return GID;
}

uint16_t ConicGlyph::upm () {
    return units_per_em;
}

int ConicGlyph::advanceWidth () {
    return m_aw;
}

void ConicGlyph::setAdvanceWidth (int val) {
    m_aw = val;
    widthset = true;
}

int ConicGlyph::leftSideBearing () {
    return m_lsb;
}

const PrivateDict *ConicGlyph::privateDict () const {
    return m_private;
}

OutlinesType ConicGlyph::outlinesType () const {
    return m_outType;
}

QUndoStack *ConicGlyph::undoStack () {
    return m_undoStack.get ();
}

ElementType DrawableReference::elementType () const {
    return ElementType::Reference;
}

void DrawableReference::quickBounds (DBounds &b) {
    if (cc)
	b = cc->bb;
    else
	b = { 0, 0, 0, 0 };
}

void DrawableReference::realBounds (DBounds &b, bool) {
    return quickBounds (b);
}

uint16_t DrawableReference::numContours () const {
    return cc->numCompositeContours ();
}

uint16_t DrawableReference::numPoints () const {
    return cc->numCompositePoints ();
}

uint16_t DrawableReference::depth (uint16_t val) const {
    return (val + cc->componentDepth (val));
}

uint16_t ConicGlyph::numCompositePoints () const {
    uint16_t ret = 0;
    if (figures.size ()) {
	ret = figures.front ().countPoints (0, true);
    } else {
	for (auto &ref : refs)
	    ret += ref.numPoints ();
    }
    return ret;
}

uint16_t ConicGlyph::numCompositeContours () const {
    uint16_t ret = 0;
    if (figures.size ()) {
	ret = figures.front ().contours.size ();
    } else {
	for (auto &ref : refs)
	    ret += ref.numContours ();
    }
    return ret;
}

uint16_t ConicGlyph::componentDepth (uint16_t val) const {
    uint16_t ret = val;
    for (auto &ref : refs) {
	uint16_t rd = ref.depth (val);
	if (rd > ret)
	    ret = rd;
    }
    return ret;
}

bool ConicGlyph::autoHint (sFont &fnt) {
    if (m_outType != OutlinesType::PS)
	return false;
    bool ret = clearHints ();
    if (figures.empty () || figures.front ().contours.empty ())
	return ret;

    GlyphData gd (&fnt, *this, true, false);
    int16_t cnt = 0;
    for (StemData *sd : gd.hbundle.stemlist) {
	double s = sd->right.y;
	double w = sd->left.y - sd->right.y;
	if (sd->ghost) {
	    s += w; w = -w;
	}
	hstem.push_back ({ cnt++, s, w });
    }
    for (StemData *sd : gd.vbundle.stemlist) {
	double s = sd->left.x;
	double w = sd->right.x - sd->left.x;
	if (sd->ghost) {
	    s += w; w = -w;
	}
	vstem.push_back ({ cnt++, s, w });
    }
    gd.figureHintMasks ();
    gd.figureCounterMasks (countermasks);
    return true;
}

bool ConicGlyph::hmUpdate (sFont &fnt) {
    if (m_outType != OutlinesType::PS)
	return false;
    if (figures.empty () || figures.front ().contours.empty ())
	return false;

    for (auto &fig:figures)
	fig.clearHintMasks ();
    GlyphData gd (&fnt, *this, true, true);
    gd.figureHintMasks ();
    return true;
}

bool ConicGlyph::clearHints () {
    if (hstem.empty () && vstem.empty () && countermasks.empty ())
	return false;
    hstem.clear ();
    vstem.clear ();
    countermasks.clear ();

    for (auto &fig:figures)
	fig.clearHintMasks ();
    return true;
}

void ConicGlyph::removeFigure (DrawableFigure &fig) {
    for (auto it = figures.begin (); it != figures.end (); it++) {
	auto &cur = *it;
	if (&cur == &fig) {
	    figures.erase (it);
	    break;
	}
    }
}

void ConicGlyph::swapFigures (int pos1, int pos2) {
    if (pos1 >= pos2 || pos2 >= (int) figures.size ())
	return;

    std::list<DrawableFigure> temporary;
    auto move_from = std::next (figures.begin (), pos2);
    temporary.splice (temporary.begin (), figures, move_from, std::next (move_from));
    auto move_to = std::next (figures.begin (), pos1);
    figures.splice (move_to, temporary);
}

void ConicGlyph::mergeContours () {
    if (!figures.empty ()) {
	auto &fig = figures.front ();
	auto it = figures.begin ();
	it++;
	while (it != figures.end ()) {
	    fig.mergeWith (*it);
	    figures.erase (it);
	    it = figures.begin ();
	    it++;
	}
    }
}

bool ConicGlyph::addExtrema (bool selected) {
    bool ret = false;
    for (auto &fig : figures)
	ret |= fig.addExtrema (selected);
    if (ret)
	renumberPoints ();
    return ret;
}

bool ConicGlyph::roundToInt (bool selected) {
    bool ret = false;
    for (auto &fig : figures)
	ret |= fig.roundToInt (selected);
    return ret;
}

bool ConicGlyph::simplify (bool selected) {
    bool ret = false;
    for (auto &fig : figures)
	ret |= fig.simplify (selected, units_per_em);
    return ret;
}

bool ConicGlyph::correctDirection (bool) {
    bool ret = false;
    for (auto &fig : figures)
	ret |= fig.correctDirection ();
    return ret;
}

bool ConicGlyph::reverseSelected () {
    bool ret = false;
    for (auto &fig : figures) {
	for (auto &spls: fig.contours) {
	    if (spls.isSelected ()) {
		spls.reverse ();
		ret |= true;
	    }
	}
    }
    if (ret)
	renumberPoints ();
    return ret;
}
