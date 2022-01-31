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

#include <cmath>
#include <QTranslator>

#include "sfnt.h"
#include "splineglyph.h"
#include "stemdb.h"
#include "cffstuff.h"
#include "fs_notify.h"
#include "fs_math.h"

#define GLYPH_DATA_DEBUG 0

using namespace FontShepherd::math;
static const double PI = 3.14159265358979323846264338327;

static bool line_pt_cmp (PointData *pd1, PointData *pd2) {
    LineData *line;
    double ppos1=0, ppos2=0;

    if (pd1->prevline && (pd1->prevline == pd2->prevline || pd1->prevline == pd2->nextline))
	line = pd1->prevline;
    else if (pd1->nextline && (pd1->nextline == pd2->prevline || pd1->nextline == pd2->nextline))
	line = pd1->nextline;
    else
	return false;

    ppos1 = (pd1->sp->me.x - line->online.x) * line->unit.x +
	    (pd1->sp->me.y - line->online.y) * line->unit.y;
    ppos2 = (pd2->sp->me.x - line->online.x) * line->unit.x +
	    (pd2->sp->me.y - line->online.y) * line->unit.y;

    return (ppos1 < ppos2);
}

static bool chunk_cmp (struct stem_chunk &ch1, struct stem_chunk &ch2) {
    StemData *stem;
    double loff1=0,roff1=0,loff2=0,roff2=0;

    stem = ch1.parent;
    if (!stem)
	return false;

    if (ch1.l)
	loff1 = (ch1.l->sp->me.x - stem->left.x) * stem->unit.x +
		(ch1.l->sp->me.y - stem->left.y) * stem->unit.y;
    if (ch1.r)
	roff1 = (ch1.r->sp->me.x - stem->right.x) * stem->unit.x +
		(ch1.r->sp->me.y - stem->right.y) * stem->unit.y;
    if (ch2.l)
	loff2 = (ch2.l->sp->me.x - stem->left.x) * stem->unit.x +
		(ch2.l->sp->me.y - stem->left.y) * stem->unit.y;
    if (ch2.r)
	roff2 = (ch2.r->sp->me.x - stem->right.x) * stem->unit.x +
		(ch2.r->sp->me.y - stem->right.y) * stem->unit.y;

    if (!realNear (loff1, loff2))
	return (loff1 < loff2);
    return (roff1 < roff2);
}

static int BBox_intersects_line (Conic *s, Conic *line) {
    double t, x, y;
    DBounds b;

    b.minx = b.maxx = s->from->me.x;
    b.miny = b.maxy = s->from->me.y;
    if (s->to->me.x<b.minx) b.minx = s->to->me.x;
    else if (s->to->me.x>b.maxx) b.maxx = s->to->me.x;
    if (s->to->me.y<b.miny) b.miny = s->to->me.y;
    else if (s->to->me.y>b.maxy) b.maxy = s->to->me.y;
    if (s->to->prevcp.x<b.minx) b.minx = s->to->prevcp.x;
    else if (s->to->prevcp.x>b.maxx) b.maxx = s->to->prevcp.x;
    if (s->to->prevcp.y<b.miny) b.miny = s->to->prevcp.y;
    else if (s->to->prevcp.y>b.maxy) b.maxy = s->to->prevcp.y;
    if (s->from->nextcp.x<b.minx) b.minx = s->from->nextcp.x;
    else if (s->from->nextcp.x>b.maxx) b.maxx = s->from->nextcp.x;
    if (s->from->nextcp.y<b.miny) b.miny = s->from->nextcp.y;
    else if (s->from->nextcp.y>b.maxy) b.maxy = s->from->nextcp.y;

    if (line->conics[0].c!=0) {
	t = (b.minx-line->conics[0].d)/line->conics[0].c;
	y = line->conics[1].c*t+line->conics[1].d;
	if (y>=b.miny && y<=b.maxy)
	    return true;
	t = (b.maxx-line->conics[0].d)/line->conics[0].c;
	y = line->conics[1].c*t+line->conics[1].d;
	if (y>=b.miny && y<=b.maxy)
	    return true;
    }
    if (line->conics[1].c!=0) {
	t = (b.miny-line->conics[1].d)/line->conics[1].c;
	x = line->conics[0].c*t+line->conics[0].d;
	if (x>=b.minx && x<=b.maxx)
	    return true;
	t = (b.maxy-line->conics[1].d)/line->conics[1].c;
	x = line->conics[0].c*t+line->conics[0].d;
	if (x>=b.minx && x<=b.maxx)
	    return true;
    }
    return false;
}

static int line_type (std::vector<struct st> &st, int i, Conic *line) {
    ConicPoint *sp;
    BasePoint nextcp, prevcp, here;
    double dn, dp;
    int cnt = st.size ();

    if (st[i].st>.01 && st[i].st<.99)
	return 0;		/* GWW: Not near an end-point, just a normal line */
    if (i+1>=cnt)
	return 0;		/* GWW: No following spline */
    if (st[i+1].st>.01 && st[i+1].st<.99)
	return 0;		/* GWW: Following spline not near an end-point, can't */
				/*  match to this one, just a normal line */
    if (st[i].st<.5 && st[i+1].st>.5) {
	if (st[i+1].s->to->next!=st[i].s)
	    return 0;
	sp = st[i].s->from;
    } else if (st[i].st>.5 && st[i+1].st<.5) {
	if (st[i].s->to->next!=st[i+1].s)
	    return 0;
	sp = st[i].s->to;
    } else
	return 0;

    if (!sp->nonextcp)
	nextcp = sp->nextcp;
    else
	nextcp = sp->next->to->me;
    if (!sp->noprevcp)
	prevcp = sp->prevcp;
    else
	prevcp = sp->prev->from->me;
    here.x = line->conics[0].c*(st[i].st+st[i+1].st)/2 + line->conics[0].d;
    here.y = line->conics[1].c*(st[i].st+st[i+1].st)/2 + line->conics[1].d;

    nextcp.x -= here.x; nextcp.y -= here.y;
    prevcp.x -= here.x; prevcp.y -= here.y;

    dn = nextcp.x*line->conics[1].c - nextcp.y*line->conics[0].c;
    dp = prevcp.x*line->conics[1].c - prevcp.y*line->conics[0].c;
    if (dn*dp<0)	/* GWW: splines away move on opposite sides of the line */
	return 1;	/* Treat this line and the next as one */
			/* We assume that a rounding error gave us one erroneous intersection (or we went directly through the endpoint) */
    else
	return 2;	/* GWW: Ignore both this line and the next */
			/* Intersects both in a normal fashion */
}

static int match_winding (std::vector<Monotonic *> space, int i, int nw, int winding, int which, int idx) {
    int cnt=0;

    if ((nw<0 && winding>0) || (nw>0 && winding<0)) {
	winding = nw;
	for (int j=i-1; j>=0; --j) {
	    Monotonic *m = space[j];
	    uint8_t up = which ? m->yup : m->xup;
	    winding += (up ? 1 : -1);
	    if (winding==0) {
		if (cnt == idx)
		    return j;
		cnt++;
	    }
	}
    } else {
	winding = nw;
	for (size_t j=i+1; j<space.size (); ++j) {
	    Monotonic *m = space[j];
	    uint8_t up = which ? m->yup : m->xup;
	    winding += (up ? 1 : -1);
	    if (winding==0) {
		if (cnt == idx)
		    return j;
		cnt++;
	    }
	}
    }
    return -1;
}

static BasePoint perturb_along_spline (Conic *s, BasePoint *bp, double t) {
    BasePoint perturbed;

    while (true) {
	perturbed.x = ((s->conics[0].a*t+s->conics[0].b)*t+s->conics[0].c)*t+s->conics[0].d;
	perturbed.y = ((s->conics[1].a*t+s->conics[1].b)*t+s->conics[1].c)*t+s->conics[1].d;
	if (!realWithin (perturbed.x, bp->x, .01) || !realWithin (perturbed.y, bp->y, .01))
	    break;
	if (t<.5) {
	    t *= 2;
	    if (t>.5)
		break;
	} else {
	    t = 1- 2*(1-t);
	    if (t<.5)
		break;
	}
    }
    return perturbed;
}

static bool corner_correct_side (PointData *pd, bool x_dir, bool is_l) {
    int corner = x_dir ? pd->x_corner : pd->y_corner;
    int start = ((x_dir && is_l) || (!x_dir && !is_l));
    double unit_p, unit_n;

    unit_p = x_dir ? pd->prevunit.x : pd->prevunit.y;
    unit_n = x_dir ? pd->nextunit.x : pd->nextunit.y;
    return
	((start && (
	(corner == 1 && unit_p > 0 && unit_n > 0) ||
	(corner == 2 && unit_p < 0 && unit_n < 0))) ||
	(!start && (
	(corner == 1 && unit_p < 0 && unit_n < 0) ||
	(corner == 2 && unit_p > 0 && unit_n > 0))));
}

static bool nearly_parallel (BasePoint *dir, Conic *other, double t) {
    BasePoint odir;
    double olen;

    odir.x = (3*other->conics[0].a*t+2*other->conics[0].b)*t+other->conics[0].c;
    odir.y = (3*other->conics[1].a*t+2*other->conics[1].b)*t+other->conics[1].c;
    olen = sqrt (pow (odir.x, 2) + pow (odir.y, 2));
    if (olen==0)
	return false;
    odir.x /= olen; odir.y /= olen;
    return Units::parallel (dir, &odir, false);
}

static double normal_dist (BasePoint *to, BasePoint *from, BasePoint *perp) {
    double len = (to->x-from->x)*perp->y - (to->y-from->y)*perp->x;
    if (len<0) len = -len;
    return len;
}

static void fixup_t (PointData *pd, int stemidx, bool isnext, int eidx) {
    /* GWW: When we calculated "next/prev_e_t" we deliberately did not use pd1->me */
    /*  (because things get hard at intersections) so our t is only an approx-*/
    /*  imation. We can do a lot better now */
    Conic *s;
    ConicPoint end1, end2;
    double width, t, sign, len, dot;
    std::array<BasePoint, 9> pts;
    std::array<extended_t, 10> lts, sts;
    BasePoint diff;
    StemData *stem ;

    if (!pd || stemidx == -1)
	return;
    stem = isnext ? pd->nextstems[stemidx] : pd->prevstems[stemidx];
    width = (stem->right.x - stem->left.x)*stem->unit.y -
	    (stem->right.y - stem->left.y)*stem->unit.x;
    s = isnext ? pd->nextedges[eidx] : pd->prevedges[eidx];
    if (!s)
	return;
    diff.x = s->to->me.x-s->from->me.x;
    diff.y = s->to->me.y-s->from->me.y;
    if (diff.x<.001 && diff.x>-.001 && diff.y<.001 && diff.y>-.001)
	return;		/* Zero length splines give us NaNs */
    len = sqrt (pow (diff.x, 2) + pow (diff.y, 2));
    dot = (diff.x*stem->unit.x + diff.y*stem->unit.y)/len;
    if (dot < .0004 && dot > -.0004)
	return;		/* It's orthogonal to our stem */

    if ((stem->unit.x==1 || stem->unit.x==-1) && s->islinear)
	t = (pd->sp->me.x-s->from->me.x)/(s->to->me.x-s->from->me.x);
    else if ((stem->unit.y==1 || stem->unit.y==-1) && s->islinear)
	t = (pd->sp->me.y-s->from->me.y)/(s->to->me.y-s->from->me.y);
    else {
	sign = ((isnext && pd->next_is_l[stemidx]) || (!isnext && pd->prev_is_l[stemidx])) ? 1 : -1;
	end1.me = pd->sp->me;
	end2.me.x = pd->sp->me.x+1.1*sign*width*stem->l_to_r.x;
	end2.me.y = pd->sp->me.y+1.1*sign*width*stem->l_to_r.y;
	end1.nextcp = end1.prevcp = end1.me;
	end2.nextcp = end2.prevcp = end2.me;
	end1.nonextcp = end1.noprevcp = end2.nonextcp = end2.noprevcp = true;
	Conic myline (&end1, &end2, false);
	if (myline.intersects (s, pts, lts, sts)<=0)
	    return;
	t = sts[0];
    }
    if (std::isnan (t)) {
	FontShepherd::postError (QObject::tr ("NaN value in fixup_t"));
	return;
    }
    if (isnext)
	pd->next_e_t[eidx] = t;
    else
	pd->prev_e_t[eidx] = t;
}

static double find_same_slope (Conic *s, BasePoint *dir, double close_to) {
    double a, b, c, desc;
    double t1, t2;
    double d1, d2;

    if (!s)
	return -1e4;

    a = dir->x*s->conics[1].a*3 - dir->y*s->conics[0].a*3;
    b = dir->x*s->conics[1].b*2 - dir->y*s->conics[0].b*2;
    c = dir->x*s->conics[1].c   - dir->y*s->conics[0].c  ;
    if (a!=0) {
	desc = b*b - 4*a*c;
	if (desc<0)
	    return -1e4;
	desc = sqrt (desc);
	t1 = (-b+desc)/(2*a);
	t2 = (-b-desc)/(2*a);
	if ((d1=t1-close_to)<0) d1 = -d1;
	if ((d2=t2-close_to)<0) d2 = -d2;
	if (d2<d1 && t2>=-.001 && t2<=1.001)
	    t1 = t2;
    } else if (b!=0)
	t1 = -c/b;
    else
	return -1e4;

    return t1;
}

static int adjust_for_imperfect_slope_match (ConicPoint *sp, BasePoint *pos,
    BasePoint *newpos, StemData *stem, bool is_l) {

    double poff, err, min, max;
    BasePoint *base;

    base = is_l ? &stem->left : &stem->right;
    err = Units::isHV (&stem->unit, true) ? GlyphData::dist_error_hv : GlyphData::dist_error_diag;
    min = is_l ? stem->lmax - 2*err : stem->rmax - 2*err;
    max = is_l ? stem->lmin + 2*err : stem->rmin + 2*err;

    /* AMK/GWW: Possible if the stem unit has been attached to a line. It is */
    /* hard to prevent this */
    if (min > max) {
	min = stem->lmin; max = stem->lmax;
    }

    poff =  (pos->x - base->x)*stem->l_to_r.x +
	    (pos->y - base->y)*stem->l_to_r.y;
    if (poff > min && poff < max) {
	*newpos = *pos;
	return false;
    } else if (poff <= min)
	err = fabs (min);
    else if (poff >= max)
	err = fabs (max);

    newpos->x = sp->me.x + err*(pos->x - sp->me.x)/fabs (poff);
    newpos->y = sp->me.y + err*(pos->y - sp->me.y)/fabs (poff);
    return true;
}

static int in_active (double projection, std::vector<struct segment> &segments) {
    for (auto &seg: segments) {
	if (projection>=seg.start && projection<=seg.end )
	    return true;
    }
    return false;
}

static int merge_segments (std::vector<struct segment> &space) {
    int i, j;
    int cnt = space.size ();
    double middle;

    for (i=j=0; i<cnt; ++i, ++j) {
	if (i!=j)
	    space[j] = space[i];
	while (i+1<cnt && space[i+1].start<space[j].end) {
	    if (space[i+1].end >= space[j].end) {

		/* AMK/GWW: If there are 2 overlapping segments and neither the  */
		/* end of the first segment nor the start of the second */
		/* one are curved we can merge them. Otherwise we have  */
		/* to preserve them both, but modify their start/end properties */
		/* so that the overlap is removed */
		if (space[j].ecurved != 1 && space[i+1].scurved != 1) {
		    space[j].end = space[i+1].end;
		    space[j].ebase = space[i+1].ebase;
		    space[j].ecurved = space[i+1].ecurved;
		    space[j].curved = false;
		} else if (space[j].ecurved != 1 && space[i+1].scurved == 1) {
		    space[i+1].start = space[j].end;
		    --i;
		} else if (space[j].ecurved == 1 && space[i+1].scurved != 1) {
		    space[j].end = space[i+1].start;
		    --i;
		} else {
		    middle = (space[j].end + space[i+1].start)/2;
		    space[j].end = space[i+1].start = middle;
		    --i;
		}
	    }
	    ++i;
	}
    }
    // Shrink to the resulting size
    space.resize (j);
    return j;
}

static int merge_segments_final (std::vector<struct segment> &space) {
    int i, j;
    int cnt = space.size ();

    for (i=j=0; i<cnt; ++i, ++j) {
	if (i!=j)
	    space[j] = space[i];
	while (i+1<cnt && space[i+1].start<=space[j].end) {
	    if (space[i+1].end>space[j].end) {
		space[j].end = space[i+1].end;
		space[j].ebase = space[i+1].ebase;
		space[j].ecurved = space[i+1].ecurved;
		space[j].curved = false;
	    }
	    ++i;
	}
    }
    return j;
}

static int add_ghost_segment (PointData *pd, double base, std::vector<struct segment> &space) {
    double s, e, temp, pos, spos, epos;
    ConicPoint *sp, *nsp, *nsp2, *psp, *psp2;
    struct segment seg;

    sp = nsp = psp = pd->sp;
    pos = pd->sp->me.y;

    /* AMK/GWW: First check if there are points on the same line lying further */
    /* in the desired direction */
    if (sp->next && (sp->next->to->me.y == pos))
	nsp = sp->next->to;
    if (sp->prev && (sp->prev->from->me.y == pos))
	psp = sp->prev->from;

    if (psp != sp) {
	s = psp->me.x;
    } else if (psp->noprevcp) {
	psp2 = psp->prev->from;
	if (psp2->me.y != psp->me.y) {
	    s = (psp->me.x - psp2->me.x)/(psp->me.y - psp2->me.y)*20.0;
	    if (s < 0) s = -s;
	    if (psp2->me.x<psp->me.x)
		s = (psp->me.x-psp2->me.x < s) ? psp2->me.x : psp->me.x-s;
	    else
		s = (psp2->me.x-psp->me.x < s) ? psp2->me.x : psp->me.x+s;
	} else
	    s = psp->me.x;
    } else {
	s = (pd->sp->me.x + psp->prevcp.x)/2;
    }

    if (nsp != sp) {
	e = nsp->me.x;
    } else if (nsp->nonextcp) {
	nsp2 = nsp->next->to;
	if (nsp2->me.y != nsp->me.y) {
	    e = (nsp->me.x - nsp2->me.x)/(nsp->me.y - nsp2->me.y)*20.0;
	    if (e < 0) e = -e;
	    if (nsp2->me.x<nsp->me.x)
		e = (nsp->me.x-nsp2->me.x < e) ? nsp2->me.x : nsp->me.x-e;
	    else
		e = (nsp2->me.x-nsp->me.x < e)  ? nsp2->me.x : nsp->me.x+e;
	} else
	    e = nsp->me.x;
    } else {
	e = (pd->sp->me.x + nsp->nextcp.x)/2;
    }

    spos = psp->me.x; epos = nsp->me.x;
    if (s > e) {
	temp = s; s = e; e = temp;
	temp = spos; spos = epos; epos = temp;
    }

    seg.start = s - base;
    seg.end = e - base;
    seg.sbase = spos - base;
    seg.ebase = epos - base;
    seg.ecurved = seg.scurved = seg.curved = false;
    space.push_back (seg);

    return space.size ();
}

static bool stem_pairs_similar (StemData *s1, StemData *s2, StemData *ts1, StemData *ts2) {
    int normal, reversed, ret;
    double olen1, olen2;

    /* AMK/GWW: Stem widths in the second pair should be nearly the same as */
    /* stem widths in the first pair */
    normal =   (ts1->width >= s1->width - GlyphData::dist_error_hv &&
		ts1->width <= s1->width + GlyphData::dist_error_hv &&
		ts2->width >= s2->width - GlyphData::dist_error_hv &&
		ts2->width <= s2->width + GlyphData::dist_error_hv);
    reversed = (ts1->width >= s2->width - GlyphData::dist_error_hv &&
		ts1->width <= s2->width + GlyphData::dist_error_hv &&
		ts2->width >= s1->width - GlyphData::dist_error_hv &&
		ts2->width <= s1->width + GlyphData::dist_error_hv);

    if (!normal && !reversed)
	return false;

    if (normal) {
	olen1 = s1->activeOverlap (ts1);
	olen2 = s2->activeOverlap (ts2);
	ret =   olen1 > s1->clen/3 && olen1 > ts1->clen/3 &&
		olen2 > s2->clen/3 && olen2 > ts2->clen/3;
    } else if (reversed) {
	olen1 = s1->activeOverlap (ts2);
	olen2 = s2->activeOverlap (ts1);
	ret =   olen1 > s1->clen/3 && olen1 > ts2->clen/3 &&
		olen2 > s2->clen/3 && olen2 > ts1->clen/3;
    }
    return ret;
}

int Units::isHV (const BasePoint *unit, bool strict) {
    double angle = atan2 (unit->y, unit->x);
    double deviation = (strict) ? GlyphData::stem_slope_error : GlyphData::stub_slope_error;

    if (fabs (angle) >= PI/2 - deviation && fabs (angle) <= PI/2 + deviation)
	return 2;
    else if (fabs (angle) <= deviation || fabs(angle) >= PI - deviation)
	return 1;
    return 0;
}

int Units::closerToHV (BasePoint *u1, BasePoint *u2) {
    double adiff1, adiff2;

    adiff1 = fabs (atan2 (u1->y,u1->x));
    adiff2 = fabs (atan2 (u2->y,u2->x));

    if (adiff1 > PI*.25 && adiff1 < PI*.75)
	adiff1 = fabs (adiff1 - PI*.5);
    else if (adiff1 >= PI*.75)
	adiff1 = PI - adiff1;

    if (adiff2 > PI*.25 && adiff2 < PI*.75)
	adiff2 = fabs (adiff2 - PI*.5);
    else if (adiff2 >= PI*.75)
	adiff2 = PI - adiff2;

    if (adiff1 < adiff2)
	return  1;
    else if (adiff1 > adiff2)
	return -1;
    else
	return  0;
}

double Units::getAngle (BasePoint *u1, BasePoint *u2) {
    double dx, dy;

    dy = u1->x*u2->y - u1->y*u2->x;
    dx = u1->x*u2->x + u1->y*u2->y;
    return atan2 (dy, dx);
}

bool Units::orthogonal (BasePoint *u1, BasePoint *u2, bool strict) {
    double angle, deviation = (strict) ? GlyphData::stem_slope_error : GlyphData::stub_slope_error;

    angle = Units::getAngle (u1,u2);
    return (fabs (angle) >= (PI/2 - deviation) && fabs (angle) <= (PI/2 + deviation));
}

bool Units::parallel (BasePoint *u1, BasePoint *u2, bool strict) {
    double angle, deviation = (strict) ? GlyphData::stem_slope_error : GlyphData::stub_slope_error;

    angle = Units::getAngle (u1, u2);
    return (fabs (angle) <= deviation || fabs (angle) >= (PI - deviation));
}

BasePoint Units::calcMiddle (BasePoint *unit1, BasePoint *unit2) {
    BasePoint u1, u2, ret;
    double hyp;
    int hv;

    u1 = *unit1; u2 = *unit2;
    if (u1.x*u2.x + u1.y*u2.y < 0) {
	u2.x = -u2.x; u2.y = -u2.y;
    }
    ret.x = (u1.x + u2.x)/2;
    ret.y = (u1.y + u2.y)/2;
    hyp = sqrt (pow (ret.x, 2) + pow (ret.y, 2));
    ret.x /= hyp;
    ret.y /= hyp;

    hv = Units::isHV (&ret, true);
    if (hv) {
	ret.x = (hv == 1) ? 1 : 0;
	ret.y = (hv == 1) ? 0 : 1;
    }
    return ret;
}

int Monotonics::findAt (std::deque<Monotonic> &ms, bool which, extended_t test, std::vector<Monotonic *> &space) {
    /* Find all monotonic sections which intersect the line (x,y)[which] == test */
    /*  find the value of the other coord on that line */
    /*  Order them (by the other coord) */
    /*  then run along that line figuring out which monotonics are needed */
    extended_t t;
    Monotonic *mm;
    bool nw = !which;

    for (auto &m: ms) {
	if ((!which && test >= m.b.minx && test <= m.b.maxx) ||
	    ( which && test >= m.b.miny && test <= m.b.maxy)) {
	    /* Lines parallel to the direction we are testing just get in the */
	    /*  way and don't add any useful info */
	    if (m.s->islinear &&
		((which && m.s->from->me.y==m.s->to->me.y) ||
		(!which && m.s->from->me.x==m.s->to->me.x)))
		continue;
	    t = m.s->conics[which].iterateSplineSolveFixup (m.tstart, m.tend, test);
	    if (t==-1) {
		if (which) {
		    if ((test-m.b.minx > m.b.maxx-test && m.xup) ||
			(test-m.b.minx < m.b.maxx-test && !m.xup))
			t = m.tstart;
		    else
			t = m.tend;
		} else {
		    if ((test-m.b.miny > m.b.maxy-test && m.yup) ||
			(test-m.b.miny < m.b.maxy-test && !m.yup))
			t = m.tstart;
		    else
			t = m.tend;
		}
	    }
	    m.t = t;
	    if (t==m.tend) t -= (m.tend-m.tstart)/100;
	    else if (t==m.tstart) t += (m.tend-m.tstart)/100;
	    m.other = ((m.s->conics[nw].a*t+m.s->conics[nw].b)*t+
			m.s->conics[nw].c)*t+m.s->conics[nw].d;
	    space.push_back (&m);
	}
    }

    /* Things get a little tricky at end-points */
    for (size_t i=0; i<space.size (); ++i) {
	Monotonic *mptr = space[i];
	if (mptr->t==mptr->tend) {
	    /* Ignore horizontal/vertical lines (as appropriate) */
	    for (mm=mptr->next; mm!=mptr; mm=mm->next) {
		if (!mm->s->islinear)
		    break;
		if (( which && mm->s->from->me.y!=mptr->s->to->me.y) ||
		    (!which && mm->s->from->me.x!=mptr->s->to->me.x))
		    break;
	    }
	} else if (mptr->t==mptr->tstart) {
	    for (mm=mptr->prev; mm!=mptr; mm=mm->prev) {
		if (!mm->s->islinear)
		    break;
		if (( which && mm->s->from->me.y!=mptr->s->to->me.y ) ||
		    (!which && mm->s->from->me.x!=mptr->s->to->me.x))
		    break;
	    }
	} else
	    break;
	/* If the next monotonic continues in the same direction, and we found*/
	/*  it too, then don't count both. They represent the same intersect */
	/* If they are in oposite directions then they cancel each other out */
	/*  and that is correct */
	uint8_t mup = which ? mptr->yup : mptr->xup;
	uint8_t mmup = which ? mm->yup : mm->xup;
	if (mm!=mptr && /* Should always be true */ mmup == mup) {
	    int j;
	    for (j=space.size ()-1; j>=0; --j)
		if (space[j]==mm)
		    break;
	    if (j!=-1) {
		space.erase (space.begin () + j);
		if ((int) i>j) --i;
	    }
	}
    }

    std::sort (space.begin (), space.end (), [](Monotonic *m1, Monotonic *m2) {
        return (m1->other < m2->other);
    });
    return (space.size ());
}

int Monotonics::order (Conic *line, std::vector<Conic *> sspace, std::vector<struct st> &stspace) {
    std::array <BasePoint, 9> pts;
    std::array <extended_t, 10> lts, sts;

    for (Conic *s: sspace) {
	if (BBox_intersects_line (s, line)) {
	    /* GWW: Lines parallel to the direction we are testing just get in the */
	    /*  way and don't add any useful info */
	    if (s->islinear &&
		realNear (line->conics[0].c*s->conics[1].c, line->conics[1].c*s->conics[0].c))
		continue;
	    if (line->intersects (s, pts, lts, sts)<=0)
		continue;
	    for (int i=0; sts[i]!=-1; i++) {
		if (sts[i]>=0 && sts[i]<=1) {
		    stspace.push_back ({s, lts[i], sts[i]});
		}
	    }
	}
    }

    std::sort (stspace.begin (), stspace.end (), [](struct st &s1, struct st &s2) {
	    return (s1.lt < s2.lt);
	});
    return stspace.size ();
}

Conic *Monotonics::findAlong (Conic *line, std::vector<struct st> &stspace, Conic *findme, double *other_t) {
    Conic *s;
    int eo = 0;		/* GWW: I do horizontal/vertical by winding number */
			/* But figuring winding number with respect to a */
			/* diagonal line is hard. So I use even-odd */
			/* instead. */

    for (size_t i=0; i<stspace.size (); i++) {
	s = stspace[i].s;
	if (s==findme) {
	    if ((eo&1) && i>0) {
		*other_t = stspace[i-1].st;
		return stspace[i-1].s;
	    } else if (!(eo&1) && i+1<stspace.size ()) {
		*other_t = stspace[i+1].st;
		return stspace[i+1].s;
	    }
	    std::cerr << "monotonicFindAlong: Ran out of intersections." << std::endl;
	    return nullptr;
	}
	if (i+1<stspace.size () && stspace[i+1].s==findme)
	    ++eo;
	else switch (line_type (stspace, i, line)) {
	  case 0:	/* GWW: Normal spline */
	    ++eo;
	    break;
	  case 1:	/* GWW: Intersects at end-point & next entry is other side */
	    ++eo;	/*  And the two sides continue in approximately the   */
	    ++i;	/*  same direction */
	    break;
	  case 2:	/* GWW: Intersects at end-point & next entry is other side */
	    ++i;	/*  And the two sides go in opposite directions */
	    break;
	}
    }
    std::cerr << "monotonicFindAlong: Never found our spline." << std::endl;
    return nullptr;
}

PointData::PointData () {
    nextlinear = nextzero = prevlinear = prevzero = false;
    colinear = symetrical_h = symetrical_v = false;
    next_hor = next_ver = prev_hor = prev_ver = false;
    ticked = false;
}

void PointData::init (GlyphData &gd, ConicPoint *point, ConicPointList *ss) {
    PointData *prevpd = nullptr, *nextpd = nullptr;
    double len, same;
    int hv;

    sp = point;
    m_ss = ss;
    this->base = sp->me;

    if (!sp->nonextcp && gd.order2 () && sp->nextcpindex < gd.realCnt ()) {
	nextpd = gd.points (sp->nextcpindex);
	nextpd->m_ss = m_ss;
	nextpd->x_extr = nextpd->y_extr = 0;
	nextpd->base = sp->nextcp;
    }
    // NB: shouldn't this be prevpd?
    if (!sp->noprevcp && gd.order2 () && sp->prev && sp->prev->from->nextcpindex < gd.realCnt ()) {
	nextpd = gd.points (sp->prev->from->nextcpindex);
	nextpd->m_ss = m_ss;
	nextpd->x_extr = nextpd->y_extr = 0;
	nextpd->base = sp->prevcp;
    }

    if (!sp->next) {
	this->nextunit.x = ss->first->me.x - sp->me.x;
	this->nextunit.y = ss->first->me.y - sp->me.y;
	this->nextlinear = true;
    } else if (sp->next->islinear) {
	this->nextunit.x = sp->next->to->me.x - sp->me.x;
	this->nextunit.y = sp->next->to->me.y - sp->me.y;
	this->nextlinear = true;
    } else if (sp->nonextcp) {
	this->nextunit.x = sp->next->to->prevcp.x - sp->me.x;
	this->nextunit.y = sp->next->to->prevcp.y - sp->me.y;
    } else {
	this->nextunit.x = sp->nextcp.x - sp->me.x;
	this->nextunit.y = sp->nextcp.y - sp->me.y;
    }
    len = sqrt (pow (this->nextunit.x, 2) + pow (this->nextunit.y, 2));
    if (len==0)
	this->nextzero = true;
    else {
	this->nextunit.x /= len;
	this->nextunit.y /= len;
	if (sp->next && !sp->next->islinear)
	    GlyphData::splineFigureOpticalSlope (sp->next, true, &this->nextunit);
	hv = Units::isHV (&this->nextunit, true);
	if (hv == 2) {
	    this->nextunit.x = 0; this->nextunit.y = this->nextunit.y>0 ? 1 : -1;
	} else if (hv == 1) {
	    this->nextunit.y = 0; this->nextunit.x = this->nextunit.x>0 ? 1 : -1;
	}
	if (this->nextunit.y==0) this->next_hor = true;
	else if (this->nextunit.x==0) this->next_ver = true;

	if (nextpd) {
	    nextpd->prevunit.x = -this->nextunit.x;
	    nextpd->prevunit.y = -this->nextunit.y;
	}
    }

    if (!sp->prev) {
	this->prevunit.x = ss->last->me.x - sp->me.x;
	this->prevunit.y = ss->last->me.y - sp->me.y;
	this->prevlinear = true;
    } else if (sp->prev->islinear) {
	this->prevunit.x = sp->prev->from->me.x - sp->me.x;
	this->prevunit.y = sp->prev->from->me.y - sp->me.y;
	this->prevlinear = true;
    } else if (sp->noprevcp) {
	this->prevunit.x = sp->prev->from->nextcp.x - sp->me.x;
	this->prevunit.y = sp->prev->from->nextcp.y - sp->me.y;
    } else {
	this->prevunit.x = sp->prevcp.x - sp->me.x;
	this->prevunit.y = sp->prevcp.y - sp->me.y;
    }
    len = sqrt (pow (this->prevunit.x, 2) + pow (this->prevunit.y, 2));
    if (len==0)
	this->prevzero = true;
    else {
	this->prevunit.x /= len;
	this->prevunit.y /= len;
	if (sp->prev && !sp->prev->islinear)
	    GlyphData::splineFigureOpticalSlope (sp->prev, false, &this->prevunit);
	hv = Units::isHV (&this->prevunit, true);
	if (hv == 2) {
	    this->prevunit.x = 0; this->prevunit.y = this->prevunit.y>0 ? 1 : -1;
	} else if (hv == 1) {
	    this->prevunit.y = 0; this->prevunit.x = this->prevunit.x>0 ? 1 : -1;
	}
	if (this->prevunit.y==0) this->prev_hor = true;
	else if (this->prevunit.x==0) this->prev_ver = true;

	if (prevpd) {
	    prevpd->nextunit.x = -this->prevunit.x;
	    prevpd->nextunit.y = -this->prevunit.y;
	}
    }
    {
	same = this->prevunit.x*this->nextunit.x + this->prevunit.y*this->nextunit.y;
	if (same<-.95)
	    this->colinear = true;
    }
    if ((this->prev_hor || this->next_hor) && this->colinear) {
	if (gd.isSplinePeak (this, false, false, 1)) this->y_extr = 1;
	else if (gd.isSplinePeak (this, true, false, 1 )) this->y_extr = 2;
    } else if ((this->prev_ver || this->next_ver) && this->colinear ) {
	if (gd.isSplinePeak (this, true, true, 1)) this->x_extr = 1;
	else if (gd.isSplinePeak (this, false, true,1)) this->x_extr = 2;
    } else {
	if ((this->nextunit.y < 0 && this->prevunit.y < 0) || (this->nextunit.y > 0 && this->prevunit.y > 0)) {
	    if (gd.isSplinePeak (this, false, false, 2)) this->y_corner = 1;
	    else if (gd.isSplinePeak (this, true, false, 2)) this->y_corner = 2;
	}
	if ((this->nextunit.x < 0 && this->prevunit.x < 0) || (this->nextunit.x > 0 && this->prevunit.x > 0)) {
	    if (gd.isSplinePeak (this, true, true, 2)) this->x_corner = 1;
	    else if (gd.isSplinePeak (this, false, true, 2)) this->x_corner = 2;
	}
    }
    if (GlyphData::hint_diagonal_intersections) {
	if ((this->y_corner || this->y_extr) &&
	    realNear (this->nextunit.x,-this->prevunit.x) &&
	    realNear (this->nextunit.y, this->prevunit.y) && !this->nextzero)
	    this->symetrical_h = true;
	else if ((this->x_corner || this->x_extr) &&
	    realNear (this->nextunit.y,-this->prevunit.y) &&
	    realNear (this->nextunit.x, this->prevunit.x) && !this->nextzero)
	    this->symetrical_v = true;
    }
}

void PointData::assignStem (StemData *stem, bool is_next, bool left) {
    std::vector<StemData *> &stems = is_next ? nextstems : prevstems;
    std::vector<int> &is_l = is_next ? next_is_l : prev_is_l;

    for (StemData *tstem : stems) {
	if (tstem == stem)
	    return;
    }

    stems.push_back (stem);
    is_l.push_back (left);
}

bool PointData::parallelToDir (bool checknext, BasePoint *dir,
    BasePoint *opposite, ConicPoint *basesp, uint8_t is_stub) {

    BasePoint n, o, *base = &basesp->me;
    ConicPoint *sp;
    double angle, mid_err = (GlyphData::stem_slope_error + GlyphData::stub_slope_error)/2;

    sp = this->sp;
    n = checknext ? this->nextunit : this->prevunit;

    angle = fabs (Units::getAngle (dir, &n));
    if ((!is_stub && angle > GlyphData::stem_slope_error && angle < PI - GlyphData::stem_slope_error) ||
	(is_stub & 1 && angle > GlyphData::stub_slope_error*1.5 && angle < PI - GlyphData::stub_slope_error*1.5) ||
	(is_stub & 6 && angle > mid_err && angle < PI - mid_err))
	return false;

    /* Now sp must be on the same side of the spline as opposite */
    o.x = opposite->x-base->x; o.y = opposite->y-base->y;
    n.x = sp->me.x-base->x; n.y = sp->me.y-base->y;
    if ((o.x*dir->y - o.y*dir->x)*(n.x*dir->y - n.y*dir->x) < 0)
	return false;

    return true;
}

bool LineData::fitsHV () const {
    int cnt;
    bool is_x, hv;
    double off, min=0, max=0;
    PointData *pd;

    cnt = this->points.size ();
    hv = Units::isHV (&this->unit, true);
    if (hv)
	return true;

    hv = Units::isHV (&this->unit, false);
    if (!hv)
	return false;

    is_x = (hv == 1) ? true : false;
    for (int i=0; i<cnt; i++) {
	pd = this->points[i];
	off =   (pd->base.x - this->online.x) * !is_x -
		(pd->base.y - this->online.y) * is_x;
	if (off < min) min = off;
	else if (off > max) max = off;
    }
    if ((max - min) < 2*GlyphData::dist_error_hv)
	return true;
    return false;
}

std::string LineData::repr () const {
    std::ostringstream ss;

    //ss.precision (4);
    ss << "line vector=" << this->unit.x << ',' << this->unit.y;
    ss << "base=" << this->online.x << ',' << this->online.y;
    ss << "length=" << this->length << "\n";
    //ss.precision (2);
    for (size_t i=0; i<this->points.size (); i++) {
	PointData *pd = points[i];

	ss << "\t point num=" << pd->sp->ptindex;
	ss << ", x=" << pd->sp->me.x << ", y=" << pd->sp->me.y;
	ss << " prev=" << (pd->prevline==this);
	ss << " next=" << (pd->nextline==this) << "\n";
    }
    ss << std::endl;
    return ss.str ();
}

StemData::StemData (BasePoint *dir, BasePoint *pos1, BasePoint *pos2) {
    double width;

    this->ldone = this->rdone = false;
    this->ghost = this->bbox = false;
    this->positioned = this->ticked = false;
    this->italic = false;

    this->unit = *dir;
    if (dir->x < 0 || dir->y == -1) {
	this->unit.x = -this->unit.x;
	this->unit.y = -this->unit.y;
    }
    width = (pos2->x - pos1->x) * this->unit.y -
	    (pos2->y - pos1->y) * this->unit.x;
    if (width > 0) {
	this->left = *pos1;
	this->right = *pos2;
	this->width = width;
    } else {
	this->left = *pos2;
	this->right = *pos1;
	this->width = -width;
    }
    /* GWW: Guess at which normal we want */
    this->l_to_r.x = dir->y; this->l_to_r.y = -dir->x;
    /* GWW: If we guessed wrong, use the other */
    if ((this->right.x-this->left.x)*this->l_to_r.x +
	(this->right.y-this->left.y)*this->l_to_r.y < 0 ) {
	this->l_to_r.x = -this->l_to_r.x;
	this->l_to_r.y = -this->l_to_r.y;
    }
}

bool StemData::onStem (BasePoint *test, int left) {
    double dist_error, off;
    BasePoint *dir = &this->unit;
    double max=0, min=0;

    /* Diagonals are harder to align */
    dist_error = Units::isHV (dir, true) ? GlyphData::dist_error_hv : GlyphData::dist_error_diag;
    if (!this->positioned) dist_error = dist_error * 2;
    if (dist_error > this->width/2) dist_error = this->width/2;
    if (left) {
	off = (test->x - this->left.x)*dir->y - (test->y - this->left.y)*dir->x;
	max = this->lmax; min = this->lmin;
    } else {
	off = (test->x - this->right.x)*dir->y - (test->y - this->right.y)*dir->x;
	max = this->rmax; min = this->rmin;
    }

    if (off > (max - dist_error) && off < (min + dist_error))
	return true;

    return false;
}

bool StemData::bothOnStem (BasePoint *test1, BasePoint *test2, int force_hv,bool strict, bool cove) {
    double dist_error, off1, off2;
    BasePoint dir = this->unit;
    int hv, hv_strict;
    double lmax=0, lmin=0, rmax=0, rmin=0;

    hv = force_hv ? Units::isHV (&dir, false) : Units::isHV (&dir, true);
    hv_strict = force_hv ? Units::isHV (&dir, true) : hv;
    if (force_hv) {
	if (force_hv != hv)
	    return false;
	if (!hv_strict && !fitsHV ((hv == 1), 7))
	    return false;
	if (!hv_strict) {
	    dir.x = (force_hv == 2) ? 0 : 1;
	    dir.y = (force_hv == 2) ? 1 : 0;
	}
    }
    /* Diagonals are harder to align */
    dist_error = hv ? GlyphData::dist_error_hv : GlyphData::dist_error_diag;
    if (!strict) {
	dist_error = dist_error * 2;
	lmax = this->lmax; lmin = this->lmin;
	rmax = this->rmax; rmin = this->rmin;
    }
    if (dist_error > this->width/2) dist_error = this->width/2;

    off1 = (test1->x-this->left.x)*dir.y - (test1->y-this->left.y)*dir.x;
    off2 = (test2->x-this->right.x)*dir.y - (test2->y-this->right.y)*dir.x;
    if (off1 > (lmax - dist_error) && off1 < (lmin + dist_error) &&
	off2 > (rmax - dist_error) && off2 < (rmin + dist_error)) {
	/* AMK/GWW: For some reasons in my patch from Feb 24 2008 I prohibited snapping */
	/* to stems point pairs which together form a bend, if at least */
	/* one point from the pair doesn't have exactly the same position as */
	/* the stem edge. Unfortunately I don't remember why I did this, but */
	/* this behavior has at least one obviously negative effect: it */
	/* prevents building a stem from chunks which describe an ark   */
	/* intersected by some straight lines, even if the intersections lie */
	/* closely enough to the ark extremum. So don't apply this test */
	/* at least if the force_hv flag is on (which means either the  */
	/* chunk or the stem itself is not exactly horizontal/vertical) */
	if (!cove || force_hv || off1 == 0 || off2 == 0)
	    return true;
    }

    off2 = (test2->x-this->left.x)*dir.y - (test2->y-this->left.y)*dir.x;
    off1 = (test1->x-this->right.x)*dir.y - (test1->y-this->right.y)*dir.x;
    if (off2 > (lmax - dist_error) && off2 < (lmin + dist_error) &&
	off1 > (rmax - dist_error) && off1 < (rmin + dist_error)) {
	if (!cove || force_hv || off1 == 0 || off2 == 0)
	    return true;
    }

    return false;
}

bool StemData::pointOnDiag (PointData *pd, bool only_hv) {
    bool is_next;

    if (only_hv || pd->colinear)
	return false;

    is_next = (this->assignedToPoint (pd, false) != -1);
    auto &stems = is_next ? pd->nextstems : pd->prevstems;

    for (StemData *tstem : stems) {
	if (!Units::isHV (&tstem->unit, true) && tstem->lpcnt >= 2 && tstem->rpcnt >=2)
	    return true;
    }
    return false;
}

int StemData::assignedToPoint (PointData *pd, bool is_next) {
    std::vector<StemData *> &stems = is_next ? pd->nextstems : pd->prevstems;

    for (size_t i=0; i<stems.size (); i++) {
	if (stems[i] == this)
	    return i;
    }
    return -1;
}

bool StemData::fitsHV (bool is_x, uint8_t mask) {
    int i, cnt;
    double loff, roff;
    double lmin=0, lmax=0, rmin=0, rmax=0;

    cnt = this->chunks.size ();

    for (i=0 ; i<cnt; i++) {
	if (this->chunks[i].stub & mask)
	    break;
    }
    if (i == cnt)
	return false;
    if (cnt == 1)
	return true;

    for (auto &chunk: this->chunks) {
	if (chunk.l) {
	    loff = (chunk.l->sp->me.x - this->left.x) * !is_x -
		   (chunk.l->sp->me.y - this->left.y) * is_x;
	    if (loff < lmin) lmin = loff;
	    else if (loff > lmax) lmax = loff;
	}
	if (chunk.r) {
	    roff = (chunk.r->sp->me.x - this->right.x) * !is_x -
		   (chunk.r->sp->me.y - this->right.y) * is_x;
	    if (roff < rmin) rmin = roff;
	    else if (roff > rmax) rmax = roff;
	}
    }
    if (((lmax - lmin) < 2*GlyphData::dist_error_hv ) &&
	((rmax - rmin) < 2*GlyphData::dist_error_hv))
	return true;
    return false;
}

bool StemData::recalcOffsets (BasePoint *dir, bool left, bool right) {
    double off, err;
    double lmin=0, lmax=0, rmin=0, rmax=0;

    if (!left && !right)
	return false;
    err = Units::isHV (dir, true) ? GlyphData::dist_error_hv : GlyphData::dist_error_diag;

    if (this->chunks.size () > 1) for (auto &chunk: this->chunks) {
	if (left && chunk.l) {
	    off =  (chunk.l->sp->me.x - this->left.x)*dir->y -
		   (chunk.l->sp->me.y - this->left.y)*dir->x;
	    if (off < lmin) lmin = off;
	    else if (off > lmax) lmax = off;
	}
	if (right && chunk.r) {
	    off =  (chunk.r->sp->me.x - this->right.x)*dir->y +
		   (chunk.r->sp->me.y - this->right.y)*dir->x;
	    if (off < rmin) rmin = off;
	    else if (off > rmax) rmax = off;
	}
    }
    if (lmax - lmin < 2*err && rmax - rmin < 2*err) {
	this->lmin = lmin; this->lmax = lmax;
	this->rmin = rmin; this->rmax = rmax;
	return true;
    }
    return false;
}

void StemData::setUnit (BasePoint dir) {
    double width;

    width = (this->right.x - this->left.x) * dir.y -
	    (this->right.y - this->left.y) * dir.x;
    if (width < 0) {
	width = -width;
	dir.x = -dir.x;
	dir.y = -dir.y;
    }
    this->unit = dir;
    this->width = width;

    /* Guess at which normal we want */
    this->l_to_r.x = dir.y; this->l_to_r.y = -dir.x;
    /* If we guessed wrong, use the other */
    if ((this->right.x-this->left.x)*this->l_to_r.x +
	(this->right.y-this->left.y)*this->l_to_r.y < 0 ) {
	this->l_to_r.x = -this->l_to_r.x;
	this->l_to_r.y = -this->l_to_r.y;
    }

    /* Recalculate left/right offsets relatively to new vectors */
    recalcOffsets (&dir, true, true);
}

PointData *StemData::findClosestOpposite (stem_chunk **chunk, ConicPoint *sp, int *next) {
    PointData *pd, *ret=nullptr;
    double test, proj=1e4;
    bool is_l;

    for (auto &tchunk : chunks) {
	pd = nullptr;
	if (tchunk.l && tchunk.l->sp==sp ) {
	    pd = tchunk.r;
	    is_l = false;
	} else if (tchunk.r && tchunk.r->sp==sp) {
	    pd = tchunk.l;
	    is_l = true;
	}

	if (pd) {
	    test = (pd->sp->me.x-sp->me.x) * this->unit.x +
		   (pd->sp->me.y-sp->me.y) * this->unit.y;
	    if (test < 0) test = -test;
	    if (test < proj) {
		ret = pd;
		proj = test;
		*chunk = &tchunk;
	    }
	}
    }
    if (ret)
	*next = is_l ? (*chunk)->lnext : (*chunk)->rnext;
    return ret;
}

bool StemData::wouldConflict (StemData *other) {
    double loff, roff, s1, s2, e1, e2;
    int acnt1, acnt2;

    if (this == other || !Units::parallel (&this->unit, &other->unit, true))
	return false;

    loff = (other->left.x  - this->left.x) * this->unit.y -
	   (other->left.y  - this->left.y) * this->unit.x;
    roff = (other->right.x - this->right.x) * this->unit.y -
	   (other->right.y - this->right.y) * this->unit.x;
    loff = fabs (loff); roff = fabs (roff);
    if (loff > this->width || roff > this->width)
	return false;

    acnt1 = this->active.size ();
    acnt2 = other->active.size ();
    if (acnt1 == 0 || acnt2 == 0)
	return false;
    s1 = this->active[0].start; e1 = this->active[acnt1-1].end;
    s2 = other->active[0].start; e2 = other->active[acnt2-1].end;

    loff = (other->left.x - this->left.x) * this->unit.x +
	   (other->left.y - this->left.y) * this->unit.y;
    if ((s2+loff >= s1 && s2+loff <= e1) || (e2+loff >= s1 && e2+loff <= e1) ||
	(s2+loff <= s1 && e2+loff >= e1) || (e2+loff <= s1 && s2+loff >= e1))
	return true;

    return false;
}

bool StemData::validConflictingStem (StemData *other) {
    int x_dir = fabs (this->unit.y) > fabs (this->unit.x);
    double s1, e1, s2, e2, temp;

    if (x_dir) {
	s1 = this->left.x - (this->left.y * this->unit.x)/this->unit.y;
	e1 = this->right.x - (this->right.y * this->unit.x)/this->unit.y;
	s2 = other->left.x - (other->left.y * other->unit.x)/other->unit.y;
	e2 = other->right.x - (other->right.y * other->unit.x)/other->unit.y;
    } else {
	s1 = this->left.y - (this->left.x * this->unit.y)/this->unit.x;
	e1 = this->right.y - (this->right.x * this->unit.y)/this->unit.x;
	s2 = other->left.y - (other->left.x * other->unit.y)/other->unit.x;
	e2 = other->right.y - (other->right.x * other->unit.y)/other->unit.x;
    }

    if (s1 > e1) {
	temp = s1; s1 = e1; e1 = temp;
    }
    if (s2 > e2) {
	temp = s2; s2 = e2; e2 = temp;
    }
    /* AMK/GWW: If stems don't overlap, then there is no conflict here */
    if (s2 >= e1 || s1 >= e2)
	return false;

    /* AMK/GWW: Stems which have no points assigned cannot be valid masters for    */
    /* other stems (however there is a notable exception for ghost hints) */
    if ((this->lpcnt > 0 || this->rpcnt > 0) &&
	other->lpcnt == 0 && other->rpcnt == 0 && !other->ghost)
	return false;

    /* Bounding box stems are always preferred */
    if (this->bbox && !other->bbox)
	return false;

    /* Stems associated with blue zones always preferred to any other stems */
    if (this->blue && !other->blue)
	return false;
    /* Don't attempt to handle together stems, linked to different zones */
    if (this->blue && other->blue && this->blue != other->blue)
	return false;
    /* If both stems are associated with a blue zone, but one of them is for */
    /* a ghost hint, then that stem is preferred */
    if (this->ghost && !other->ghost)
	return false;

    return true;
}

double StemData::activeOverlap (StemData *other) {
    double base1, base2, s1, e1, s2, e2, s, e, len = 0;
    size_t j = 0;

    bool is_x = (Units::isHV (&this->unit, true) == 2);
    base1 = is_x ? this->left.y : this->left.x;
    base2 = is_x ? other->left.y : other->left.x;

    for (size_t i=0; i<this->active.size (); i++) {
	s1 = base1 + this->active[i].start;
	e1 = base1 + this->active[i].end;
	for (; j<other->active.size (); j++) {
	    s2 = base2 + other->active[j].start;
	    e2 = base2 + other->active[j].end;
	    if (s2 > e1)
		break;

	    if (e2 < s1)
		continue;

	    s = s2 < s1 ? s1 : s2;
	    e = e2 > e1 ? e1 : e2;
	    if (e<s)
		continue;		/* Shouldn't happen */
	    len += e - s;
	}
    }
    return len;
}

bool StemData::hasDependentStem (StemData *slave) {
    if (slave->master && this->dependent.size () > 0) {
	for (auto &dep: this->dependent) {
	    StemData *tstem = dep.stem;
	    if (tstem == slave || tstem->hasDependentStem (slave))
		return true;
	}
    }
    return false;
}

bool StemData::preferEndDep (StemData *smaster, StemData *emaster, char s_type, char e_type) {
    int hv = Units::isHV (&this->unit, true);
    double sdist, edist;

    if (!hv)
	return false;

    if ((s_type == 'a' && e_type != 'a') || (s_type == 'm' && e_type == 'i'))
	return false;
    else if ((e_type == 'a' && s_type != 'a') || (e_type == 'm' && s_type == 'i'))
	return true;

    if (s_type == 'm' && s_type == e_type) {
	sdist = (hv==1) ?
	    fabs (smaster->right.y - this->right.y) :
	    fabs (smaster->left.x - this->left.x);
	edist = (hv==1) ?
	    fabs (emaster->left.y - this->left.y) :
	    fabs (emaster->right.x - this->right.x);
	return (edist < sdist);
    } else
	return (emaster->clen > smaster->clen);
}

void StemData::lookForMasterHVStem () {
    StemData *tstem, *smaster=nullptr, *emaster=nullptr;
    struct stembundle *bundle = this->bundle;
    double start, end, tstart, tend;
    double ssdist, sedist, esdist, eedist;
    double smin, smax, emin, emax, tsmin, tsmax, temin, temax;
    char stype, etype;
    int link_to_s;
    bool is_x, allow_s, allow_e;

    is_x = (bundle->unit.x == 1);
    if (is_x) {
	start = this->right.y; end = this->left.y;
	smin = start - this->rmin - 2*GlyphData::dist_error_hv;
	smax = start - this->rmax + 2*GlyphData::dist_error_hv;
	emin = end - this->lmin - 2*GlyphData::dist_error_hv;
	emax = end - this->lmax + 2*GlyphData::dist_error_hv;
    } else {
	start = this->left.x; end = this->right.x;
	smin = start + this->lmax - 2*GlyphData::dist_error_hv;
	smax = start + this->lmin + 2*GlyphData::dist_error_hv;
	emin = end + this->rmax - 2*GlyphData::dist_error_hv;
	emax = end + this->rmin + 2*GlyphData::dist_error_hv;
    }
    stype = etype = '\0';

    for (size_t i=0; i<bundle->stemlist.size (); i++) {
	tstem = bundle->stemlist[i];
	if (is_x) {
	    tstart = tstem->right.y; tend = tstem->left.y;
	    tsmin = tstart - tstem->rmin - 2*GlyphData::dist_error_hv;
	    tsmax = tstart - tstem->rmax + 2*GlyphData::dist_error_hv;
	    temin = tend - tstem->lmin - 2*GlyphData::dist_error_hv;
	    temax = tend - tstem->lmax + 2*GlyphData::dist_error_hv;
	} else {
	    tstart = tstem->left.x; tend = tstem->right.x;
	    tsmin = tstart + tstem->lmax - 2*GlyphData::dist_error_hv;
	    tsmax = tstart + tstem->lmin + 2*GlyphData::dist_error_hv;
	    temin = tend + tstem->rmax - 2*GlyphData::dist_error_hv;
	    temax = tend + tstem->rmin + 2*GlyphData::dist_error_hv;
	}

	/* AMK/GWW: In this loop we are looking if the given stem has conflicts with */
	/* other stems and if anyone of those conflicting stems should      */
	/* take precedence over it */
	if (this == tstem || tend < start || tstart > end ||
	    !this->validConflictingStem (tstem) || this->hasDependentStem (tstem))
	    continue;
	/* AMK/GWW: Usually in case of conflicts we prefer the stem with longer active */
	/* zones. However a stem linked to a blue zone is always preferred to */
	/* a stem which is not, and ghost hints are preferred to any other    */
	/* stems */
	if (this->clen > tstem->clen && tstem->validConflictingStem (this))
	    continue;

	this->confl_cnt++;

	/* AMK/GWW: If the master stem is for a ghost hint or both the stems are    */
	/* linked to the same blue zone, then we can link only to the edge */
	/* which fall into the blue zone */
	allow_s = (!tstem->ghost || tstem->width == 21) &&
	    (!this->blue || this->blue != tstem->blue || this->blue->start < 0 );
	allow_e = (!tstem->ghost || tstem->width == 20) &&
	    (!this->blue || this->blue != tstem->blue || this->blue->start > 0 );

	/* AMK/GWW: Assume there are two stems which have (almost) coincident left edges. */
	/* The hinting technique for this case is to merge all points found on   */
	/* those coincident edges together, position them, and then link to the  */
	/* opposite edges. */
	/* However we don't allow merging if both stems can be snapped to a blue    */
	/* zone, unless their edges are _exactly_ coincident, as shifting features  */
	/* relatively to each other instead of snapping them to the same zone would */
	/* obviously be wrong */
	if (allow_s && tstart > smin && tstart < smax && start > tsmin && start < tsmax &&
	    (!this->blue || realNear (tstart, start))) {

	    if (!smaster || stype != 'a' || smaster->clen < tstem->clen) {
		smaster = tstem;
		stype = 'a';
	    }
	/* AMK/GWW: The same case for right edges */
	} else if (allow_e && tend > emin && tend < emax && end > temin && end < temax &&
	    (!this->blue || realNear (tend, end))) {

	    if (!emaster || etype != 'a' || emaster->clen < tstem->clen) {
		emaster = tstem;
		etype = 'a';
	    }

	/* AMK/GWW: Nested stems. I first planned to handle them by positioning the      */
	/* narrower stem first, and then linking its edges to the opposed edges */
	/* of the nesting stem. But this works well only in those cases where   */
	/* maintaining the dependent stem width is not important. So now the    */
	/* situations where a narrower or a wider stem can be preferred         */
	/* (because it has longer active zones) are equally possible. In the    */
	/* first case I link to the master stem just one edge of the secondary  */
	/* stem, just like with overlapping stems */
	} else if (tstart > start && tend < end) {
	    if (allow_s && (!smaster || stype == 'i' ||
		(stype == 'm' && smaster->clen < tstem->clen))) {

		smaster = tstem;
		stype = 'm';
	    }
	    if (allow_e && (!emaster || etype == 'i' ||
		(etype == 'm' && emaster->clen < tstem->clen))) {

		emaster = tstem;
		etype = 'm';
	    }
	/* AMK/GWW: However if we have to prefer the nesting stem, we do as with      */
	/* overlapping stems which require interpolations, i. e. interpolate */
	/* one edge and link to another */
	} else if (tstart < start && tend > end) {
	    link_to_s = (allow_s && ( start - tstart < tend - end));
	    if (link_to_s && (!smaster ||
		(stype == 'i' && smaster->clen < tstem->clen))) {
		smaster = tstem;
		stype = 'i';
	    } else if (!link_to_s && (!emaster ||
		(etype == 'i' && emaster->clen < tstem->clen))) {
		emaster = tstem;
		etype = 'i';
	    }
	/* AMK/GWW: Overlapping stems. Here we first check all 4 distances between */
	/* 4 stem edges. If the closest distance is between left or right */
	/* edges, then the normal technique (in TrueType) is linking them */
	/* with MDRP without maintaining a minimum distance. Otherwise    */
	/* we interpolate an edge of the "slave" stem between already     */
	/* positioned edges of the "master" stem, and then gridfit it     */
	} else if ((tstart < start && start < tend && tend < end) ||
	    (start < tstart && tstart < end && end < tend)) {

	    ssdist = fabs (start - tstart);
	    sedist = fabs (start - tend);
	    esdist = fabs (end - tstart);
	    eedist = fabs (end - tend);

	    if (allow_s && (!allow_e ||
		(this->width < tstem->width/3 && ssdist < eedist) ||
		(ssdist <= eedist && ssdist <= sedist && ssdist <= esdist)) &&
		(!smaster || ( stype == 'i' ||
		(stype == 'm' && smaster->clen < tstem->clen)))) {

		smaster = tstem;
		stype = 'm';
	    } else if (allow_e && (!allow_s ||
		(this->width < tstem->width/3 && eedist < ssdist) ||
		(eedist <= ssdist && eedist <= sedist && eedist <= esdist)) &&
		(!emaster || ( etype == 'i' ||
		(etype == 'm' && emaster->clen < tstem->clen)))) {

		emaster = tstem;
		etype = 'm';
	    } else if (allow_s && allow_e && (!smaster ||
		(stype == 'i' && smaster->clen < tstem->clen )) &&
		sedist <= esdist && sedist <= ssdist && sedist <= eedist) {

		smaster = tstem;
		stype = 'i';
	    } else if (allow_s && allow_e && (!emaster ||
		(etype == 'i' && emaster->clen < tstem->clen)) &&
		esdist <= sedist && esdist <= ssdist && esdist <= eedist) {

		emaster = tstem;
		etype = 'i';
	    }
	}
    }
    if (smaster && emaster ) {
	if (this->preferEndDep (smaster, emaster, stype, etype))
	    smaster = nullptr;
	else
	    emaster = nullptr;
    }

    if (smaster) {
	this->master = smaster;
	struct dependent_stem dep {this, !is_x, stype};
	smaster->dependent.push_back (dep);
    } else if (emaster) {
	this->master = emaster;
	struct dependent_stem dep {this, is_x, etype};
	emaster->dependent.push_back (dep);
    }
}

/* AMK/GWW: If a stem has been considered depending from another stem which in   */
/* its turn has its own "master", and the first stem doesn't conflict   */
/* with the "master" of the stem it overlaps (or any other stems), then */
/* this dependency is unneeded and processing it in the autoinstructor  */
/* can even lead to undesired effects. Unfortunately we can't prevent   */
/* detecting such dependecies in LookForMasterHVStem(), because we      */
/* need to know the whole stem hierarchy first. So look for undesired   */
/* dependencies and clean them now */
void StemData::clearUnneededDeps () {
    StemData *master;
    size_t i, j;

    if (this->confl_cnt == 1 && (master = this->master) != nullptr && master->master) {
	this->master = nullptr;
	for (i=j=0; i<master->dependent.size (); i++) {
	    if (j<i)
		master->dependent[i-1] = master->dependent[i];
	    if (master->dependent[i].stem != this) j++;
	}
    }
}

std::string StemData::repr () const {
    std::ostringstream ss;

    //ss.precision (2);
    ss << "stem l=" << this->left.x << ',' << this->left.y << " idx=" << this->leftidx;
    ss << " r=" << this->right.x << ',' << this->right.y << " idx=" << this->rightidx;
    //ss.precision (4);
    ss << " vector=" << this->unit.x << ',' << this->unit.y << "\n";
    //ss.precision (2);
    ss << "\twidth=" << this->width;
    ss << " chunk_cnt=" << this->chunks.size ();
    ss << " len=" << this->len << " clen=" << this->clen << "\n";
    ss << "\tidx=" << this->stem_idx;
    ss << " ghost=" << this->ghost << " toobig=" << this->toobig << " confl_cnt=" << confl_cnt <<"\n";
    if (this->blue)
	ss << "\tblue=" << this->blue->start << ',' << this->blue->width << "\n";
    ss << "\tlmin=" << this->lmin << " lmax=" << this->lmax;
    ss << " rmin=" << this->rmin << " rmax=" << this->rmax;
    ss << " lpcnt=" << this->lpcnt << " rpcnt=" << this->rpcnt << "\n";

    for (auto &chunk: this->chunks) {
	ss << "\tchunk";
	if (chunk.l)
	    ss << " l=" << chunk.l->sp->me.x << ',' << chunk.l->sp->me.y << " potential=" << +chunk.lpotential;
	if (chunk.r)
	    ss << " r=" << chunk.r->sp->me.x << ',' << chunk.r->sp->me.y << " potential=" << +chunk.rpotential;
	if (chunk.l && chunk.r)
	    ss << " stub=" << +chunk.stub;
	ss << "\n";
    }
    ss << std::endl;
    return ss.str ();
}

std::string StemBundle::repr () const {
    std::ostringstream ss;
    char btype = (unit.x == 1) ? 'H' : (unit.y == 1) ? 'V' : 'D';

    if (!stemlist.empty ()) for (StemData *stem: stemlist) {
	//ss.precision (2);
	ss << btype << " stem idx=" << stem->stem_idx;
	ss << " l=" << stem->left.x << ',' << stem->left.y;
	ss << " r=" << stem->right.x << ',' << stem->right.y;
	ss << " slave=" << (stem->master != nullptr) << "\n";
	if (!stem->dependent.empty ()) for (auto &dep: stem->dependent) {
	    ss << "\tslave";
	    ss << " l=" << dep.stem->left.x << ',' << dep.stem->left.y;
	    ss << " r=" << dep.stem->right.x << ',' << dep.stem->right.y;
	    ss << " mode=" << dep.dep_type << " left=" << dep.lbase << "\n";
	}
	if (!stem->serifs.empty ()) for (auto &serif: stem->serifs) {
	    ss << "\tserif";
	    ss << " l=" << serif.stem->left.x << ',' << serif.stem->left.y;
	    ss << " r=" << serif.stem->right.x << ',' << serif.stem->right.y;
	    ss << " ball=" << serif.is_ball << " left=" << serif.lbase << "\n";
	}
    }
    ss << std::endl;
    return ss.str ();
}

const double GlyphData::stem_slope_error = .05061454830783555773; /*  2.9 degrees */
const double GlyphData::stub_slope_error = .317649923862967983;   /* 18.2 degrees */

double GlyphData::dist_error_hv = 3.5;
double GlyphData::dist_error_diag = 5.5;
/* GWW: It's easy to get horizontal/vertical lines aligned properly */
/* it is more difficult to get diagonal ones done */
/* The "A" glyph in Apple's Times.dfont(Roman) is off by 6 in one spot */
double GlyphData::dist_error_curve = 22;

bool GlyphData::hint_diagonal_ends = false;
bool GlyphData::hint_diagonal_intersections = true;
bool GlyphData::hint_bounding_boxes = true;
bool GlyphData::detect_diagonal_stems = true;

int GlyphData::getBlueFuzz (const PrivateDict *pd) {
    if (pd->has_key (cff::BlueFuzz))
	return pd->get (cff::BlueFuzz).n.base;
    return 1;
}

void GlyphData::figureBlues (const PrivateDict *pd) {
    // NB: add a constructor for StemInfo
    int cnt = 0;
    if (pd->has_key (cff::OtherBlues)) {
	auto &lst = pd->get (cff::OtherBlues).list;
        for (size_t i=0; i<16 && lst[i].valid; i+=2)
	    cnt++;
    }
    if (pd->has_key (cff::BlueValues)) {
	auto &lst = pd->get (cff::BlueValues).list;
        for (size_t i=0; i<16 && lst[i].valid; i+=2)
	    cnt++;
    }
    m_blues.reserve (cnt); cnt = 0;

    if (pd->has_key (cff::OtherBlues)) {
	auto &lst = pd->get (cff::OtherBlues).list;
        for (size_t i=0; i<16 && lst[i].valid; i+=2) {
	    StemInfo blue;
	    blue.start = lst[i].base;
	    blue.width = lst[i+1].base - lst[i].base;
	    blue.hintnumber = cnt++;
	    m_blues.push_back (blue);
        }
    }
    if (pd->has_key (cff::BlueValues)) {
	auto &lst = pd->get (cff::BlueValues).list;
        for (size_t i=0; i<16 && lst[i].valid; i+=2) {
	    StemInfo blue;
	    blue.start = lst[i].base;
	    blue.width = lst[i+1].base - lst[i].base;
	    blue.hintnumber = cnt++;
	    m_blues.push_back (blue);
        }
    }
}

bool GlyphData::splineFigureOpticalSlope (Conic *s, bool start_at_from, BasePoint *dir) {
    /* GWW: Sometimes splines have tiny control points, and to the eye the slope */
    /*  of the spline has nothing to do with that specified by the cps. */
    /* So see if the spline is straightish and figure the slope based on */
    /*  some average direction */
    /* dir is a input output parameter. */
    /*  it should be initialized to the unit vector determined by the appropriate cp */
    /*  if the function returns true, it will be set to a unit vector in the average direction */
    BasePoint pos, *base, average_dir {0, 0}, normal;
    double t, len, incr, off;
    double dx, dy, ax, ay, d, a;

    /* The vector is already nearly vertical/horizontal, no need to modify*/
    if (Units::isHV (dir, true))
	return false;

    if (start_at_from) {
	incr = -.1;
	base = &s->from->me;
    } else {
	incr = .1;
	base = &s->to->me;
    }

    t = .5-incr;
    while (t>0 && t<1.0) {
	pos.x = ((s->conics[0].a*t+s->conics[0].b)*t+s->conics[0].c)*t+s->conics[0].d;
	pos.y = ((s->conics[1].a*t+s->conics[1].b)*t+s->conics[1].c)*t+s->conics[1].d;

	average_dir.x += (pos.x-base->x); average_dir.y += (pos.y-base->y);
	t += incr;
    }

    len = sqrt (pow (average_dir.x, 2) + pow (average_dir.y, 2));
    if (len==0)
	return false;
    average_dir.x /= len; average_dir.y /= len;
    normal.x = average_dir.y; normal.y = - average_dir.x;

    t = .5-incr;
    while (t>0 && t<1.0) {
	pos.x = ((s->conics[0].a*t+s->conics[0].b)*t+s->conics[0].c)*t+s->conics[0].d;
	pos.y = ((s->conics[1].a*t+s->conics[1].b)*t+s->conics[1].c)*t+s->conics[1].d;
	off = (pos.x-base->x)*normal.x + (pos.y-base->y)*normal.y;
	if (off<-dist_error_hv || off>dist_error_hv)
	    return false;
	t += incr;
    }

    if (Units::parallel (dir, &normal, true)) {
	/* GWW: prefer the direction which is closer to horizontal/vertical */
	if ((dx=dir->x)<0) dx = -dx;
	if ((dy=dir->y)<0) dy = -dy;
	d = (dx<dy) ? dx : dy;
	if ((ax=average_dir.x)<0) ax = -ax;
	if ((ay=average_dir.y)<0) ay = -ay;
	a = (ax<ay) ? ax : ay;
	if (d<a)
	    return false;
    }

    *dir = average_dir;
    return true;
}

/* flags: 1 -- accept curved extrema, 2 -- accept angles, */
/*	  4 -- analyze segments (not just single points)    */
int GlyphData::isSplinePeak (PointData *pd, bool outer, bool is_x, int flags) {
    double base, next, prev, nextctl, prevctl, unit_p, unit_n;
    Conic *s, *snext, *sprev;
    std::vector<Monotonic *> space;
    int wprev, wnext, desired;
    size_t i;
    ConicPoint *sp = pd->sp;

    base = is_x ? sp->me.x : sp->me.y;
    nextctl = sp->nonextcp ? base : is_x ? sp->nextcp.x : sp->nextcp.y;
    prevctl = sp->noprevcp ? base : is_x ? sp->prevcp.x : sp->prevcp.y;
    next = prev = base;
    snext = sp->next; sprev = sp->prev;
    space.reserve (m_ms.size ());

    if (!snext->to || !sprev->from)
	return 0;
    if (!(flags & 2) && (sp->nonextcp || sp->noprevcp))
	return 0;
    else if (!(flags & 1) && (pd->colinear))
	return 0;

    if (flags & 4) {
	while (snext->to->next && snext->to != sp && next == base) {
	    next = is_x ? snext->to->me.x : snext->to->me.y;
	    snext = snext->to->next;
	}

	while (sprev->from->prev && sprev->from != sp && prev == base) {
	    prev = is_x ? sprev->from->me.x : sprev->from->me.y;
	    sprev = sprev->from->prev;
	}
    } else {
        next = is_x ? snext->to->me.x : snext->to->me.y;
        prev = is_x ? sprev->from->me.x : sprev->from->me.y;
    }

    if (prev<base && next<base && nextctl<=base && prevctl<=base)
	desired = outer ? -1 : 1;
    else if (prev>base && next>base && prevctl>=base && nextctl>=base)
	desired = outer ? 1 : -1;
    else
	return 0;

    Monotonics::findAt (m_ms, is_x, is_x ? sp->me.y : sp->me.x, space);
    wprev = wnext = 0;
    for (i=0; i<space.size (); i++) {
	Monotonic *m = space[i];
	uint8_t up = is_x ? m->yup : m->xup;

	s = m->s;
	if (s->from == sp)
	    wnext = up ? 1 : -1;
	else if (s->to == sp)
	    wprev = up ? 1 : -1;
    }

    if (wnext != 0 && wprev != 0 && wnext != wprev) {
	unit_p = is_x ? pd->prevunit.x : pd->prevunit.y;
	unit_n = is_x ? pd->nextunit.x : pd->nextunit.y;
	if (unit_p < unit_n && ((outer && wprev == 1) || (!outer && wprev == -1)))
	    return desired;
	else if (unit_p > unit_n && ((outer && wnext == 1) || (!outer && wnext == -1)))
	    return desired;
    } else {
	if (wnext == desired || wprev == desired)
	    return desired;
    }

    return 0;
}

bool GlyphData::order2 () const {
    return m_fig->order2;
}

int GlyphData::realCnt () const {
    return m_realcnt;
}

int GlyphData::pointCnt () const {
    return m_points.size ();
}

PointData *GlyphData::points (int idx) {
    return &m_points[idx];
}

bool GlyphData::isInflectionPoint (PointData *pd) {
    ConicPoint *sp = pd->sp;
    double CURVATURE_THRESHOLD = 1e-9;
    Conic *prev, *next;
    double in, out;

    if (!sp->prev || !sp->next || !pd->colinear )
	return false;

    /* point of a single-point contour can't be an inflection point. */
    if (sp->prev->from == sp)
	return false;

    prev = sp->prev;
    in = 0;
    while (prev && fabs (in) < CURVATURE_THRESHOLD) {
	in = prev->curvature (1);
	if (fabs (in) < CURVATURE_THRESHOLD) in = prev->curvature (0);
	if (fabs (in) < CURVATURE_THRESHOLD) prev = prev->from->prev;
	if (m_points[prev->to->ptindex].colinear)
	    break;
    }

    next = sp->next;
    out = 0;
    while (next && fabs (out) < CURVATURE_THRESHOLD ) {
	out = next->curvature (0);
	if (fabs (out) < CURVATURE_THRESHOLD) out = next->curvature (1);
	if (fabs (out) < CURVATURE_THRESHOLD) next = next->to->next;
	if (m_points[next->from->ptindex].colinear)
	    break;
    }

    if (in==0 || out==0 || (prev != sp->prev && next != sp->next))
	return false;

    in/=fabs(in);
    out/=fabs(out);

    return (in*out < 0);
}

int GlyphData::getValidPointDataIndex (ConicPoint *sp, StemData *stem) {
    PointData *tpd;

    if (!sp)
	return -1;
    if (sp->ttfindex < m_realcnt)
	return sp->ttfindex;
    if (!sp->nonextcp && sp->nextcpindex < m_realcnt) {
	tpd = &m_points[sp->nextcpindex];
	if (stem->assignedToPoint (tpd, false) != -1)
	    return sp->nextcpindex;
    }
    if (!sp->noprevcp && sp->prev && sp->prev->from->nextcpindex < m_realcnt ) {
	tpd = &m_points[sp->prev->from->nextcpindex];
	if (stem->assignedToPoint (tpd, true ) != -1)
	    return sp->prev->from->nextcpindex;
    }
    return -1;
}

int GlyphData::monotonicFindStemBounds (Conic *line, std::vector<struct st> &stspace, double fudge, StemData *stem) {
    int j;
    int eo;		/* I do horizontal/vertical by winding number */
			/* But figuring winding number with respect to a */
			/* diagonal line is hard. So I use even-odd */
			/* instead. */
    double pos, npos;
    double lmin = (stem->lmin < -fudge) ? stem->lmin : -fudge;
    double lmax = (stem->lmax > fudge ) ? stem->lmax :  fudge;
    double rmin = (stem->rmin < -fudge) ? stem->rmin : -fudge;
    double rmax = (stem->rmax > fudge ) ? stem->rmax :  fudge;
    lmin -= .0001; lmax += .0001; rmin -= .0001; rmax += .0001;

    eo = 0;
    for (size_t i=0; i<stspace.size (); ++i) {
	pos =   (line->conics[0].c*stspace[i].lt + line->conics[0].d - stem->left.x)*stem->l_to_r.x +
		(line->conics[1].c*stspace[i].lt + line->conics[1].d - stem->left.y)*stem->l_to_r.y;
	npos = 1e4;
	if (i+1<stspace.size ())
	    npos = (line->conics[0].c*stspace[i+1].lt + line->conics[0].d - stem->left.x)*stem->l_to_r.x +
		   (line->conics[1].c*stspace[i+1].lt + line->conics[1].d - stem->left.y)*stem->l_to_r.y;

	if (pos>=lmin && pos<=lmax) {
	    if ((eo&1) && i>0)
		j = i-1;
	    else if (!(eo&1) && i+1<stspace.size ())
		j = i+1;
	    else
		return false;
	    pos = (line->conics[0].c*stspace[j].lt + line->conics[0].d - stem->right.x)*stem->l_to_r.x +
		  (line->conics[1].c*stspace[j].lt + line->conics[1].d - stem->right.y)*stem->l_to_r.y;
	    if (pos >= rmin && pos <= rmax)
		return true;
	}
	if (i+1 < stspace.size () && npos >= lmin && npos <= lmax)
	    ++eo;
	else switch (line_type (stspace, i, line)) {
	  case 0:	/* Normal spline */
	    ++eo;
	    break;
	  case 1:	/* Intersects at end-point & next entry is other side */
	    ++eo;	/*  And the two sides continue in approximately the   */
	    ++i;	/*  same direction */
	    break;
	  case 2:	/* Intersects at end-point & next entry is other side */
	    ++i;	/*  And the two sides go in opposite directions */
	    break;
	}
    }
    return false;
}

int GlyphData::findMatchingHVEdge (PointData *pd, int is_next,
    std::array<Conic *, 2> &edges, std::array<double, 2> &other_t, std::array<double, 2> &dist) {

    double test, t, start, end;
    int which;
    Conic *s;
    int winding, nw, j, ret=0;
    std::vector<Monotonic *> space;
    BasePoint *dir, d, hv;
    size_t i;

    /* GWW: Things are difficult if we go exactly through the point. Move off */
    /*  to the side a tiny bit and hope that doesn't matter */
    if (is_next==2) {
	/* GWW: Consider the case of the bottom of the circumflex (or a chevron) */
	/*  Think of it as a flattend breve. It is symetrical and we want to */
	/*  note the vertical distance between the two points that define */
	/*  the bottom, so treat them as a funky stem */
	/*                 \ \     / /              */
	/*                  \ \   / /               */
	/*                   \ \ / /                */
	/*                    \ + /                 */
	/*                     \ /                  */
	/*                      +                   */
	hv.x = pd->symetrical_h ? 1.0 : 0.0;
	hv.y = pd->symetrical_v ? 1.0 : 0.0;
	dir = &hv;
	t = .001;
	s = pd->sp->next;		/* GWW: Could just as easily be prev */
    } else if (is_next) {
	s = pd->sp->next;
	t = .001;
	dir = &pd->nextunit;
    } else {
	s = pd->sp->prev;
	t = .999;
	dir = &pd->prevunit;
    }
    if ((d.x = dir->x )<0) d.x = -d.x;
    if ((d.y = dir->y )<0) d.y = -d.y;
    which = d.x<d.y;		/* GWW: closer to vertical */

    if (!s)			/* GWW: Somehow we got an open contour? */
	return 0;

    test = ((s->conics[which].a*t+s->conics[which].b)*t+s->conics[which].c)*t+s->conics[which].d;
    Monotonics::findAt (m_ms, which, test, space);

    winding = 0;
    for (i=0; i<space.size (); i++) {
	Monotonic *m = space[i];
	uint8_t up = which ? m->yup : m->xup;
	nw = up ? 1 : -1;
	if (m->s == s && t>=m->tstart && t<=m->tend) {
	    start = m->other;
	    break;
	}
	winding += nw;
    }
    if (i == space.size ()) {
	std::cerr << "findMatchinHVEdge didn't" << std::endl;
	return 0;
    }

    j = match_winding (space, i, nw, winding, which, 0);
    if (j!=-1) {
	other_t[0] = space[j]->t;
	end = space[j]->other;
	dist[0] = end - start;
	if (dist[0] < 0) dist[0] = -dist[0];
	edges[0] = space[j]->s;
	ret++;
    }
    if (ret > 0 && is_next != 2 && (pd->x_extr == 1 || pd->y_extr == 1)) {
	j = match_winding (space, i, nw, winding, which, 1);
	if (j!=-1) {
	    other_t[ret] = space[j]->t;
	    end = space[j]->other;
	    dist[ret] = end - start;
	    if (dist[ret] < 0) dist[ret] = -dist[ret];
	    edges[ret] = space[j]->s;
	    ret++;
	}
    }
    return ret;
}

void GlyphData::makeVirtualLine (BasePoint *perturbed, BasePoint *dir, Conic *myline, ConicPoint *end1, ConicPoint *end2) {
    BasePoint norm, absnorm;
    double t1, t2;
    DBounds bb = m_size;

    norm.x = -dir->y;
    norm.y = dir->x;
    absnorm = norm;
    if (absnorm.x<0) absnorm.x = -absnorm.x;
    if (absnorm.y<0) absnorm.y = -absnorm.y;
    bb.minx -= 10; bb.miny -= 10;
    bb.maxx += 10; bb.maxy += 10;

    myline->islinear = true;

    if (absnorm.x > absnorm.y) {
	t1 = (bb.minx-perturbed->x)/norm.x;
	t2 = (bb.maxx-perturbed->x)/norm.x;

	end1->me.x = bb.minx;
	end2->me.x = bb.maxx;
	end1->me.y = perturbed->y+t1*norm.y;
	end2->me.y = perturbed->y+t2*norm.y;
    } else {
	t1 = (bb.miny-perturbed->y)/norm.y;
	t2 = (bb.maxy-perturbed->y)/norm.y;

	end1->me.y = bb.miny;
	end2->me.y = bb.maxy;
	end1->me.x = perturbed->x+t1*norm.x;
	end2->me.x = perturbed->x+t2*norm.x;
    }
    end1->nextcp = end1->prevcp = end1->me;
    end2->nextcp = end2->prevcp = end2->me;
    end1->nonextcp = end1->noprevcp = end2->nonextcp = end2->noprevcp = true;
    end1->next = myline; end2->prev = myline;
    myline->from = end1; myline->to = end2;
    myline->islinear = true;
    myline->refigure ();
}

int GlyphData::findMatchingEdge (PointData *pd, int is_next, std::array<Conic *, 2> &edges) {
    BasePoint *dir, vert, perturbed, diff;
    Conic myline;
    ConicPoint end1, end2;
    std::array<double, 2> &other_t = is_next==2 ? pd->both_e_t : is_next ? pd->next_e_t : pd->prev_e_t;
    std::array<double, 2> &dist = is_next ? pd->next_dist : pd->prev_dist;
    double t ;
    Conic *s;
    std::vector<struct st> stspace;
    stspace.reserve (m_sspace.size ()*3);

    dist[0] = 0; dist[1] = 0;
    if (( is_next && (pd->next_hor || pd->next_ver)) ||
	(!is_next && (pd->prev_hor || pd->prev_ver)) ||
	is_next == 2)
	return findMatchingHVEdge (pd, is_next, edges, other_t, dist);

    if (is_next) {
	dir = &pd->nextunit;
	t = .001;
	s = pd->sp->next;
    } else {
	dir = &pd->prevunit;
	t = .999;
	s = pd->sp->prev;
    }
    /* AMK/GWW: For spline segments which have slope close enough to the font's italic */
    /* slant look for an opposite edge along the horizontal direction, rather */
    /* than along the normal for the point's next/previous unit. This allows  */
    /* us e. g. to detect serifs in italic fonts */
    if (m_hasSlant) {
	if (Units::parallel (dir, &m_slantUnit, true)) {
	    vert.x = 0; vert.y = 1;
	    dir = &vert;
	}
    }

    if (!s || (m_hv && !Units::isHV (dir, false)))
	return 0;

    diff.x = s->to->me.x-s->from->me.x; diff.y = s->to->me.y-s->from->me.y;
    if (diff.x<.03 && diff.x>-.03 && diff.y<.03 && diff.y>-.03)
	return 0;

    /* GWW: Don't base the line on the current point, we run into rounding errors */
    /*  where lines that should intersect it don't. Instead perturb it a tiny*/
    /*  bit in the direction along the spline */
    perturbed = perturb_along_spline (s, &pd->sp->me, t);

    makeVirtualLine (&perturbed, dir, &myline, &end1, &end2);
    /* GWW: prev_e_t = next_e_t = both_e_t =. This is where these guys are set */
    Monotonics::order (&myline, m_sspace, stspace);
    edges[0] = Monotonics::findAlong (&myline, stspace, s, &other_t[0]);
    return (edges[0] != nullptr);
}

bool GlyphData::stillStem (double fudge, BasePoint *pos, StemData *stem) {
    Conic myline;
    ConicPoint end1, end2;
    int ret;
    std::vector<struct st> stspace;
    stspace.reserve (m_sspace.size ()*3);

    makeVirtualLine (pos, &stem->unit, &myline, &end1, &end2);
    Monotonics::order (&myline, m_sspace, stspace);
    ret = monotonicFindStemBounds (&myline, stspace, fudge, stem);
    return ret;
}

bool GlyphData::isCorrectSide (PointData *pd, bool is_next, bool is_l, BasePoint *dir) {
    Conic *sbase, myline;
    ConicPoint *sp = pd->sp, end1, end2;
    BasePoint perturbed;
    int hv, winding = 0, cnt, eo;
    bool is_x, ret = false;
    double t, test;
    std::vector<Monotonic *> space;

    hv = Units::isHV (dir, true);
    if ((hv == 2 && pd->x_corner) || (hv == 1 && pd->y_corner))
	return corner_correct_side (pd, (hv == 2), is_l);

    sbase = is_next ? sp->next : sp->prev;
    t = is_next ? 0.001 : 0.999;
    perturbed = perturb_along_spline (sbase, &sp->me, t);

    if (hv) {
	is_x = (hv == 2);
	size_t i;
	int desired = is_l ? 1 : -1;
	test = is_x ? perturbed.y : perturbed.x;
	Monotonics::findAt (m_ms, is_x, test, space);
	for (i=0; i<space.size (); i++) {
	    Monotonic *m = space[i];
	    uint8_t up = is_x ? m->yup : m->xup;
	    winding = (up ? 1 : -1);
	    if (m->s == sbase)
		break;
	}
	if (i<space.size ())
	    ret = (winding == desired);
    } else {
	std::vector<struct st> stspace;
	stspace.reserve (m_sspace.size ()*3);

	makeVirtualLine (&perturbed, dir, &myline, &end1, &end2);
	cnt = Monotonics::order (&myline, m_sspace, stspace);
	eo = -1;
	is_x = fabs (dir->y) > fabs (dir->x);
	/* GWW: If a diagonal stem is more vertical than horizontal, then our     */
	/* virtual line will go from left to right. It will first intersect  */
	/* the left side of the stem, if the stem also points north-east.    */
	/* In any other case the virtual line will first intersect the right */
	/* side. */
	int i = (is_x && dir->y > 0) ? 0 : cnt-1;
	while (i >= 0 && i <= cnt-1) {
	    eo = (eo != 1) ? 1 : 0;
	    if (stspace[i].s == sbase)
		break;
	    if (is_x && dir->y > 0) i++;
	    else i--;
	}
	ret = (is_l == eo);
    }
    return ret;
}

/* GWW: In TrueType I want to make sure that everything on a diagonal line remains */
/*  on the same line. Hence we compute the line. Also we are interested in */
/*  points that are on the intersection of two lines */
LineData *GlyphData::buildLine (PointData *pd, bool is_next) {
    BasePoint *dir, *base, *start, *end;
    std::vector <PointData *> pspace;
    int pcnt=0, is_l, hv;
    double dist_error;
    LineData *line;
    double off, firstoff, lastoff, lmin=0, lmax=0;

    pspace.reserve (m_points.size ());

    dir = is_next ? &pd->nextunit : &pd->prevunit;
    is_l = isCorrectSide (pd, is_next, true, dir);
    /* Diagonals are harder to align */
    dist_error = Units::isHV (dir, true) ? dist_error_hv : dist_error_diag;
    if (dir->x==0 && dir->y==0)
	return nullptr;

    base = &pd->sp->me;
    int pidx = pd->sp->ptindex;
    for (size_t i = pidx+1; i<m_points.size (); i++) if (m_points[i].sp) {
	PointData *pd2 = &m_points[i];
	off =  (pd2->sp->me.x - base->x)*dir->y -
	       (pd2->sp->me.y - base->y)*dir->x;
	if (off <= lmax - 2*dist_error || off >= lmin + 2*dist_error)
	    continue;
	if (off < 0 && off < lmin) lmin = off;
	else if (off > 0 && off > lmax) lmax = off;

	if (((Units::parallel (dir, &pd2->nextunit, true) && !pd2->nextline) &&
	    isCorrectSide (pd2, true , is_l, dir )) ||
	    ((Units::parallel(dir, &pd2->prevunit, true) && !pd2->prevline) &&
	    isCorrectSide (pd2, false, is_l, dir )))
	    pspace.push_back (pd2);
    }

    if (pcnt==0)
	return nullptr;
    if (pcnt==1) {
	/* if the line consists of just these two points, only count it as */
	/*  a true line if the two immediately follow each other */
	if ((pd->sp->next->to != pspace[0]->sp || !pd->sp->next->islinear) &&
	    (pd->sp->prev->from != pspace[0]->sp || !pd->sp->prev->islinear))
	return nullptr;
    }

    m_lines.emplace_back ();
    line = &m_lines.back ();
    line->points.reserve (pcnt+1);
    line->points.push_back (pd);
    line->unit = *dir;
    line->is_left = is_l;
    if (dir->x < 0 || dir->y == -1) {
	line->unit.x = -line->unit.x;
	line->unit.y = -line->unit.y;
    }
    line->online = *base;
    if (is_next) {
	pd->nextline = line;
	if (pd->colinear) pd->prevline = line;
    } else {
	pd->prevline = line;
	if (pd->colinear) pd->nextline = line;
    }
    for (int i=0; i<pcnt; i++) {
	if (Units::parallel (dir, &pspace[i]->nextunit, true ) && !pspace[i]->nextline) {
	    pspace[i]->nextline = line;
	    if (pspace[i]->colinear)
		pspace[i]->prevline = line;
	}
	if (Units::parallel (dir, &pspace[i]->prevunit, true ) && !pspace[i]->prevline) {
	    pspace[i]->prevline = line;
	    if (pspace[i]->colinear )
		pspace[i]->nextline = line;
	}
	line->points[i+1] = pspace[i];
    }
    std::sort (line->points.begin (), line->points.end (), line_pt_cmp);
    start = &line->points[0]->sp->me;
    end = &line->points[pcnt]->sp->me;
    /* GWW: Now recalculate the line unit vector basing on its starting and */
    /* terminal points */
    line->unit.x = (end->x - start->x);
    line->unit.y = (end->y - start->y);
    line->length = sqrt (pow (line->unit.x, 2) + pow (line->unit.y, 2));
    line->unit.x /= line->length;
    line->unit.y /= line->length;
    hv = Units::isHV (&line->unit, true);
    if (hv == 2) {
	line->unit.x = 0; line->unit.y = 1;
    } else if (hv == 1) {
	line->unit.x = 1; line->unit.y = 0;
    } else if (m_hasSlant && Units::parallel (&line->unit, &m_slantUnit, true)) {
	firstoff =  (start->x - base->x)*m_slantUnit.y -
		    (start->y - base->y)*m_slantUnit.x;
	lastoff =   (end->x - base->x)*m_slantUnit.y -
		    (end->y - base->y)*m_slantUnit.x;
	if (fabs (firstoff) < 2*dist_error && fabs (lastoff) < 2*dist_error)
	    line->unit = m_slantUnit;
    }
    return line;
}

uint8_t GlyphData::isStubOrIntersection (BasePoint *dir1, PointData *pd1, PointData *pd2, bool is_next1, bool is_next2) {
    int i;
    int exc=0;
    double dist, off, ext, norm1, norm2, opp, angle;
    double mid_err = (stem_slope_error + stub_slope_error)/2;
    ConicPoint *sp1, *sp2, *nsp;
    BasePoint hvdir, *dir2, *odir1, *odir2;
    PointData *npd;
    LineData *line;

    sp1 = pd1->sp; sp2 = pd2->sp;
    dir2 = is_next2 ? &pd2->nextunit : &pd2->prevunit;
    hvdir.x = (int) rint (dir1->x);
    hvdir.y = (int) rint (dir1->y);

    line = is_next2 ? pd2->nextline : pd2->prevline;
    if (!Units::isHV (dir2, true) && line)
	dir2 = &line->unit;

    odir1 = (is_next1) ? &pd1->prevunit : &pd1->nextunit;
    odir2 = (is_next2) ? &pd2->prevunit : &pd2->nextunit;

    angle = fabs (Units::getAngle (dir1, dir2));
    if (angle > stub_slope_error*1.5 && angle < PI - stub_slope_error*1.5)
	return 0;

    /* AMK/GWW: First check if it is a slightly slanted line or a curve which joins */
    /* a straight line under an angle close to 90 degrees. There are many */
    /* glyphs where circles or curved features are intersected by or */
    /* connected to vertical or horizontal straight stems (the most obvious */
    /* cases are Greek Psi and Cyrillic Yu), and usually it is highly desired to */
    /* mark such an intersection with a hint */
    norm1 = (sp1->me.x - sp2->me.x) * odir2->x +
	    (sp1->me.y - sp2->me.y) * odir2->y;
    norm2 = (sp2->me.x - sp1->me.x) * odir1->x +
	    (sp2->me.y - sp1->me.y) * odir1->y;
    /* AMK/GWW if this is a real stub or intersection, then vectors on both sides */
    /* of out going-to-be stem should point in the same direction. So */
    /* the following value should be positive */
    opp = dir1->x * dir2->x + dir1->y * dir2->y;
    if ((angle <= mid_err || angle >= PI - mid_err) &&
	opp > 0 && norm1 < 0 && norm2 < 0 && Units::parallel (odir1, odir2, true) &&
	(Units::orthogonal (dir1, odir1, false ) || Units::orthogonal (dir2, odir1, false)))
	return 2;
    if ((angle <= mid_err || angle >= PI - mid_err) &&
	opp > 0 && ((norm1 < 0 && pd1->colinear &&
	Units::isHV (dir1, true) && Units::orthogonal (dir1, odir2, false)) ||
	( norm2 < 0 && pd2->colinear &&
	Units::isHV (dir2, true) && Units::orthogonal (dir2, odir1, false))))
	return 4;

    /* AMK/GWW: Now check if our 2 points form a serif termination or a feature stub */
    /* The check is pretty dumb: it returns 'true' if all the following */
    /* conditions are met: */
    /* - both the points belong to the same contour; */
    /* - there are no more than 3 other points between them; */
    /* - anyone of those intermediate points is positioned by such a way */
    /*   that it falls inside the stem formed by our 2 base point and */
    /*   the vector we are checking and its distance from the first point */
    /*   along that vector is not larger than the stem width; */
    /* - none of the intermediate points is parallel to the vector direction */
    /*   (otherwise we should have checked against that point instead) */
    if (!Units::parallel (dir1, &hvdir, false))
	return 0;

    dist = (sp1->me.x - sp2->me.x) * dir1->y -
	   (sp1->me.y - sp2->me.y) * dir1->x;
    nsp = sp1;

    for (i=0; i<4; i++) {
	if ((is_next1 && !nsp->prev) || (!is_next1 && !nsp->next))
	return 0;

	nsp = is_next1 ? nsp->prev->from : nsp->next->to;
	if ((i>0 && nsp == sp1) || nsp == sp2)
	    break;

	npd = &m_points[nsp->ptindex];
	if (Units::parallel (&npd->nextunit, &hvdir, false) ||
	    Units::parallel (&npd->prevunit, &hvdir, false))
	    break;

	ext = (sp1->me.x - nsp->me.x) * hvdir.x +
	      (sp1->me.y - nsp->me.y) * hvdir.y;
	if (ext < 0) ext = -ext;
	if ((dist > 0 && ext > dist) || (dist < 0 && ext < dist))
	    break;

	off = (sp1->me.x - nsp->me.x) * hvdir.y -
	      (sp1->me.y - nsp->me.y) * hvdir.x;
	if ((dist > 0 && (off <= 0 || off >= dist)) ||
	    (dist < 0 && (off >= 0 || off <= dist)))
	    exc++;
    }

    if (nsp == sp2 && exc == 0)
	return 1;

    return 0;
}

/* AMK/GWW: We normalize all stem unit vectors so that they point between 90 and 270 */
/* degrees, as this range is optimal for sorting diagonal stems. This means    */
/* that vertical stems will normally point top to bottom, but for diagonal     */
/* stems (even if their angle is actually very close to vertical) the opposite */
/* direction is also possible. Sometimes we "normalize" such stems converting  */
/* them to vertical. In such a case we have to swap their edges too.  */
void GlyphData::swapStemEdges (StemData *stem) {
    BasePoint tpos;
    PointData *tpd;
    LineData *tl;
    double toff;
    int temp;

    tpos = stem->left; stem->left = stem->right; stem->right = tpos;
    toff = stem->lmin; stem->lmin = stem->rmax; stem->rmax = toff;
    toff = stem->rmin; stem->rmin = stem->lmax; stem->lmax = toff;
    tl = stem->leftline; stem->leftline = stem->rightline; stem->rightline = tl;

    for (auto &chunk : stem->chunks) {
	tpd = chunk.l; chunk.l = chunk.r; chunk.r = tpd;
	temp = chunk.lpotential; chunk.lpotential = chunk.rpotential; chunk.rpotential = temp;
	temp = chunk.lnext; chunk.lnext = chunk.rnext; chunk.rnext = temp;
	temp = chunk.ltick; chunk.ltick = chunk.rtick; chunk.rtick = temp;

	tpd = chunk.l;
	if (tpd) {
	    for (size_t j=0; j<tpd->nextstems.size (); j++)
		if (tpd->nextstems[j] == stem)
		    tpd->next_is_l[j] = true;
	    for (size_t j=0; j<tpd->prevstems.size (); j++)
		if (tpd->prevstems[j] == stem)
		    tpd->prev_is_l[j] = true;
	}

	tpd = chunk.r;
	if (tpd) {
	    for (size_t j=0; j<tpd->nextstems.size (); j++)
		if (tpd->nextstems[j] == stem)
		    tpd->next_is_l[j] = false;
	    for (size_t j=0; j<tpd->prevstems.size (); j++)
		if (tpd->prevstems[j] == stem)
		    tpd->prev_is_l[j] = false;
	}
    }

    /* In case of a quadratic contour invert assignments to stem sides */
    /* also for off-curve points */
    if (order2 ()) {
	for (int i=0; i<m_realcnt; i++) if (!m_points[i].sp) {
	    tpd = &m_points[i];
	    for (size_t j=0; j<tpd->nextstems.size (); j++)
		if (tpd->nextstems[j] == stem)
		    tpd->next_is_l[j] = !tpd->next_is_l[j];
	    for (size_t j=0; j<tpd->prevstems.size (); j++)
		if (tpd->prevstems[j] == stem)
		    tpd->prev_is_l[j] = !tpd->prev_is_l[j];
	}
    }
}

struct stem_chunk *GlyphData::addToStem (StemData *stem,
    PointData *pd1, PointData *pd2, int is_next1, int is_next2, bool cheat) {

    int is_potential1 = false, is_potential2 = true;
    BasePoint *dir = &stem->unit;
    BasePoint *test;
    int lincr = 1, rincr = 1;
    double off, dist_error;
    double loff = 0, roff = 0;
    double min = 0, max = 0;
    int in, ip, cpidx;
    bool found = false;
    PointData *pd, *npd, *ppd;

    if (cheat || stem->positioned) is_potential2 = false;
    /* Diagonals are harder to align */
    dist_error = Units::isHV( dir,true ) ? 2*dist_error_hv : 2*dist_error_diag;
    if (dist_error > stem->width/2) dist_error = stem->width/2;
    max = stem->lmax;
    min = stem->lmin;

    /* The following swaps "left" and "right" points in case we have */
    /* started checking relatively to a wrong edge */
    if (pd1) {
	test = &pd1->base;
	off =   (test->x - stem->left.x)*dir->y -
		(test->y - stem->left.y)*dir->x;
	if ((!stem->ghost &&
	    (off < ( max - dist_error ) || off > ( min + dist_error ))) ||
	    (realNear (stem->unit.x, 1) && stem->ghost && stem->width == 21) ||
	    (realNear (stem->unit.x, 0) && stem->ghost && stem->width == 20)) {
	    pd = pd1; pd1 = pd2; pd2 = pd;
	    in = is_next1; is_next1 = is_next2; is_next2 = in;
	    ip = is_potential1; is_potential1 = is_potential2; is_potential2 = ip;
	}
    }

    if (!pd1) lincr = 0;
    if (!pd2) rincr = 0;
    /* Now run through existing stem chunks and see if the chunk we are */
    /* going to add doesn't duplicate an existing one.*/
    for (auto &chunk: stem->chunks) {
	if (chunk.l == pd1) lincr = 0;
	if (chunk.r == pd2) rincr = 0;

	if ((chunk.l == pd1 || !pd1) && (chunk.r == pd2 || !pd2)) {
	    if (!is_potential1) chunk.lpotential = false;
	    if (!is_potential2) chunk.rpotential = false;
	    found = true;
	    break;
	} else if ((chunk.l == pd1 && !chunk.r) || (chunk.r == pd2 && !chunk.l)) {
	    if (!chunk.l) {
		chunk.l = pd1;
		chunk.lpotential = is_potential1;
		chunk.lnext = is_next1;
		chunk.ltick = lincr;
	    } else if (!chunk.r) {
		chunk.r = pd2;
		chunk.rpotential = is_potential2;
		chunk.rnext = is_next2;
		chunk.rtick = rincr;
	    }
	    found = true;
	    break;
	}
    }

    if (!found) {
	stem->chunks.emplace_back ();
	auto &chunk = stem->chunks.back ();
	chunk.parent = stem;

	chunk.l = pd1; chunk.lpotential = is_potential1;
	chunk.r = pd2; chunk.rpotential = is_potential2;
	chunk.ltick = lincr; chunk.rtick = rincr;

	chunk.lnext = is_next1;
	chunk.rnext = is_next2;
	chunk.stemcheat = cheat;
	chunk.stub = chunk.is_ball = false;
	chunk.l_e_idx = chunk.r_e_idx = 0;
    }

    if (pd1) {
	loff =  (pd1->base.x - stem->left.x) * stem->l_to_r.x +
		(pd1->base.y - stem->left.y) * stem->l_to_r.y;
	if (is_next1==1 || is_next1==2 || pd1->colinear) {
	    pd1->assignStem (stem, true, true);
	    /* For quadratic layers assign the stem not only to   */
	    /* spline points, but to their control points as well */
	    /* (this may be important for TTF instructions */
	    if (order2 () && !pd1->sp->nonextcp && pd1->sp->nextcpindex < m_realcnt ) {
		cpidx = pd1->sp->nextcpindex;
		npd = &m_points[cpidx];
		if (stem->onStem (&npd->base, true))
		    npd->assignStem (stem, false, true );
	    }
	}
	if (is_next1==0 || is_next1==2 || pd1->colinear ) {
	    pd1->assignStem (stem, false, true);
	    if (order2 () && !pd1->sp->noprevcp && pd1->sp->prev &&
		pd1->sp->prev->from->nextcpindex < m_realcnt) {
		cpidx = pd1->sp->prev->from->nextcpindex;
		ppd = &m_points[cpidx];
		if (stem->onStem (&ppd->base, true))
		    ppd->assignStem (stem, true, true);
	    }
	}
    }
    if (pd2) {
	roff =  (pd2->base.x - stem->right.x) * stem->l_to_r.x +
		(pd2->base.y - stem->right.y) * stem->l_to_r.y;
	if (is_next2==1 || is_next2==2 || pd2->colinear) {
	    pd2->assignStem (stem, true, false);
	    if (order2 () && !pd2->sp->nonextcp && pd2->sp->nextcpindex < m_realcnt) {
		cpidx = pd2->sp->nextcpindex;
		npd = &m_points[cpidx];
		if (stem->onStem (&npd->base, false))
		    npd->assignStem (stem, false, false);
	    }
	}
	if (is_next2==0 || is_next2==2 || pd2->colinear) {
	    pd2->assignStem (stem, false, false);
	    if (order2 () && !pd2->sp->noprevcp && pd2->sp->prev &&
		pd2->sp->prev->from->nextcpindex < m_realcnt) {
		cpidx = pd2->sp->prev->from->nextcpindex;
		ppd = &m_points[cpidx];
		if (stem->onStem (&ppd->base, false))
		    ppd->assignStem (stem, true, false);
	    }
	}
    }
    if (loff < stem->lmin) stem->lmin = loff;
    else if (loff > stem->lmax) stem->lmax = loff;
    if (roff < stem->rmin) stem->rmin = roff;
    else if (roff > stem->rmax) stem->rmax = roff;
    stem->lpcnt += lincr; stem->rpcnt += rincr;
    return &stem->chunks.back ();
}

StemData *GlyphData::findStem (PointData *pd, PointData *pd2, BasePoint *dir, bool is_next2, bool de) {
    int i, cove, test_left, hv, stemcnt;
    StemData *stem;
    BasePoint newdir;

    stemcnt = is_next2 ? pd2->nextstems.size () : pd2->prevstems.size ();

    for (i=0; i<stemcnt; i++) {
	stem = is_next2 ? pd2->nextstems[i] : pd2->prevstems[i];
	test_left = is_next2 ? !pd2->next_is_l[i] : !pd2->prev_is_l[i];

	if (Units::parallel (&stem->unit, dir, true) && stem->onStem (&pd->sp->me, test_left))
	    return stem;
    }

    cove =  (dir->x == 0 && pd->x_extr + pd2->x_extr == 3) ||
	    (dir->y == 0 && pd->y_extr + pd2->y_extr == 3);

    /* First pass to check for strict matches */
    for (auto &tstem: m_stems) {
	/* Ghost hints and BBox hits are usually generated after all other   */
	/* hint types, but we can get them here in case we are generating    */
	/* glyph data for a predefined hint layout. In this case they should */
	/* be excluded from the following tests */
	if (tstem.ghost || tstem.bbox)
	    continue;

	if (Units::parallel (&tstem.unit, dir, true) &&
	    tstem.bothOnStem (&pd->sp->me, &pd2->sp->me, false, true, cove)) {
	    return &tstem;
	}
    }
    /* One more pass. At this stage larger deviations are allowed */
    for (auto &tstem: m_stems) {
	if (tstem.ghost || tstem.bbox)
	    continue;

	if (Units::parallel (&tstem.unit, dir, true) &&
	    tstem.bothOnStem (&pd->sp->me, &pd2->sp->me, false, false, cove)) {
	    return &tstem;
	}
    }
    if (de)
	return nullptr;

    hv = Units::isHV (dir, false);
    if (!hv)
	return nullptr;

    for (auto &tstem : m_stems) {
	if (tstem.ghost || tstem.bbox)
	    continue;
	if (hv && tstem.bothOnStem (&pd->base, &pd2->base, hv, false, cove)) {
	    newdir.x = (hv == 2) ? 0 : 1;
	    newdir.y = (hv == 2) ? 1 : 0;
	    if (hv == 2 && stem->unit.y < 0)
		swapStemEdges (&tstem);
	    if (tstem.unit.x != newdir.x)
		tstem.setUnit (newdir);
	    return &tstem;
	}
    }
    return nullptr;
}

StemData *GlyphData::findOrMakeHVStem (PointData *pd, PointData *pd2, bool is_h, bool require_existing) {
    int cove = false;
    BasePoint dir;

    dir.x = is_h ? 1 : 0;
    dir.y = is_h ? 0 : 1;
    if (pd2)
	cove =  (dir.x == 0 && pd->x_extr + pd2->x_extr == 3) ||
		(dir.y == 0 && pd->y_extr + pd2->y_extr == 3);

    for (auto &stem : m_stems) {
	if (Units::isHV (&stem.unit, true) &&
	    (pd2 && stem.bothOnStem (&pd->sp->me, &pd2->sp->me, false, false, cove)))
	    return &stem;
    }

    if (pd2 && !require_existing) {
	m_stems.emplace_back (&dir, &pd->sp->me, &pd2->sp->me);
	return &m_stems.back ();
    }
    return nullptr;
}

int GlyphData::isDiagonalEnd (PointData *pd1, PointData *pd2, bool is_next) {
    /* GWW: suppose we have something like */
    /*  *--*		*/
    /*   \  \		*/
    /*    \  \		*/
    /* Then let's create a vertical stem between the two points */
    /* (and a horizontal stem if the thing is rotated 90 degrees) */
    double width, length1, length2, dist1, dist2;
    BasePoint *pt1, *pt2, *dir1, *dir2, *prevdir1, *prevdir2;
    ConicPoint *prevsp1, *prevsp2;
    PointData *prevpd1, *prevpd2;
    int hv;

    if (pd1->colinear || pd2->colinear)
	return false;
    pt1 = &pd1->sp->me; pt2 = &pd2->sp->me;
    /* Both key points of a diagonal end stem should have nearly the same */
    /* coordinate by x or y (otherwise we can't determine by which axis   */
    /* it should be hinted) */
    if (pt1->x >= pt2->x - dist_error_hv &&  pt1->x <= pt2->x + dist_error_hv) {
	width = pd1->sp->me.y - pd2->sp->me.y;
	hv = 1;
    } else if (pt1->y >= pt2->y - dist_error_hv &&  pt1->y <= pt2->y + dist_error_hv) {
	width = pd1->sp->me.x - pd2->sp->me.x;
	hv = 2;
    } else
	return false;

    dir1 = is_next ? &pd1->nextunit : &pd1->prevunit;
    dir2 = is_next ? &pd2->prevunit : &pd2->nextunit;
    if (Units::isHV (dir1, true ))	/* Must be diagonal */
	return false;
    prevsp1 = is_next ? pd1->sp->next->to : pd1->sp->prev->from;
    prevsp2 = is_next ? pd2->sp->prev->from : pd2->sp->next->to;
    prevpd1 = &m_points[prevsp1->ptindex];
    prevpd2 = &m_points[prevsp2->ptindex];
    prevdir1 = is_next ? &prevpd1->prevunit : &prevpd1->nextunit;
    prevdir2 = is_next ? &prevpd2->nextunit : &prevpd2->prevunit;
    /* Ensure we have got a real diagonal, i. e. its sides are parallel */
    if (!Units::parallel (dir1, dir2, true) || !Units::parallel (prevdir1, prevdir2, true))
	return false;

    /* Diagonal width should be smaller than its length */
    length1 = pow ((prevsp1->me.x - pt1->x ), 2) + pow ((prevsp1->me.y - pt1->y ), 2);
    length2 = pow ((prevsp2->me.x - pt2->x ), 2) + pow ((prevsp2->me.y - pt2->y ), 2);
    if (length2 < length1) length1 = length2;
    if (pow (width, 2) > length1)
	return false;

    /* Finally exclude too short diagonals where the distance between key   */
    /* points of one edge at the direction orthogonal to the unit vector    */
    /* of the stem we are about to add is smaller than normal HV stem       */
    /* fudge. Such diagonals may be later turned into HV stems, and we will */
    /* result into getting two coincident hints */
    dist1 = (hv == 1) ? prevsp1->me.y - pt1->y : prevsp1->me.x - pt1->x;
    dist2 = (hv == 1) ? prevsp2->me.y - pt2->y : prevsp2->me.x - pt2->x;
    if (dist1 < 0) dist1 = -dist1;
    if (dist2 < 0) dist2 = -dist2;
    if (dist1 < 2*dist_error_hv && dist2 < 2*dist_error_hv)
	return false;

    return hv;
}

StemData *GlyphData::testStem (PointData *pd, BasePoint *dir, ConicPoint *match,
    bool is_next, bool is_next2, bool require_existing, uint8_t is_stub, int eidx) {

    PointData *pd2;
    StemData *stem, *destem;
    struct stem_chunk *chunk;
    LineData *otherline;
    double width;
    LineData *line, *line2;
    BasePoint *mdir, middle;
    bool de = false, l_changed;
    int hv;

    width = (match->me.x - pd->sp->me.x )*dir->y -
	    (match->me.y - pd->sp->me.y )*dir->x;
    if (width < 0) width = -width;
    if (width < .5)
	return nullptr;		/* Zero width stems aren't interesting */
    if ((is_next && pd->sp->next->to==match) || (!is_next && pd->sp->prev->from==match))
	return nullptr;		/* Don't want a stem between two splines that intersect */

    pd2 = &m_points[match->ptindex];

    line = is_next ? pd->nextline : pd->prevline;
    mdir = is_next2 ? &pd2->nextunit : &pd2->prevunit;
    line2 = is_next2 ? pd2->nextline : pd2->prevline;
    if (!Units::isHV (mdir, true) && line2)
	mdir = &line2->unit;
    if (mdir->x==0 && mdir->y==0)
	return nullptr;         /* cannot determine the opposite point's direction */

    if (!Units::parallel (mdir, dir, true) && !is_stub)
	return nullptr;         /* Cannot make a stem if edges are not parallel (unless it is a serif) */

    if (is_stub & 1 && !Units::isHV (dir, true)) {
	/* For serifs we prefer the vector which is closer to horizontal/vertical */
	middle = Units::calcMiddle (dir, mdir);
	if (Units::closerToHV (&middle, dir) == 1 && Units::closerToHV (&middle, mdir) == 1)
	    dir = &middle;
	else if (Units::closerToHV (mdir, dir) == 1)
	    dir = mdir;
	if (!Units::isHV (dir, true) && (hint_diagonal_ends || require_existing))
	    de = isDiagonalEnd (pd, pd2, is_next);
    }

    stem = findStem (pd, pd2, dir, is_next2, de);
    destem = nullptr;
    if (de)
	destem = findOrMakeHVStem (pd, pd2, (de == 1), require_existing);

    if (!stem && !require_existing) {
	m_stems.emplace_back (dir, &pd->sp->me, &match->me);
	stem = &m_stems.back ();
    }
    if (stem) {
	chunk = addToStem (stem, pd, pd2, is_next, is_next2, false);
	if (chunk) {
	    chunk->stub = is_stub;
	    chunk->l_e_idx = chunk->r_e_idx = eidx;
	}

	if (chunk && !m_lines.empty ()) {
	    hv = Units::isHV (&stem->unit, true);
	    /* AMK/GWW: For HV stems allow assigning a line to a stem edge only */
	    /* if that line also has an exactly HV vector */
	    if (line && ((!hv &&
		Units::parallel (&stem->unit, &line->unit, true) &&
		stem->recalcOffsets (&line->unit, true, true)) ||
		(hv && line->unit.x == stem->unit.x && line->unit.y == stem->unit.y))) {

		otherline = nullptr; l_changed = false;
		if ((!stem->leftline || stem->leftline->length < line->length) && chunk->l == pd) {
		    stem->leftline = line;
		    l_changed = true;
		    otherline = stem->rightline;
		} else if ((!stem->rightline || stem->rightline->length < line->length ) && chunk->r == pd ) {
		    stem->rightline = line;
		    l_changed = true;
		    otherline = stem->leftline;
		}
		/* If lines are attached to both sides of a diagonal stem, */
		/* then prefer the longer line */
		if (!hv && l_changed && !stem->positioned && (!otherline || (otherline->length < line->length)))
		    stem->setUnit (line->unit);
	    }
	    if (line2 && ((!hv &&
		Units::parallel (&stem->unit, &line2->unit, true) &&
		stem->recalcOffsets (&line2->unit, true, true )) ||
		(hv && line2->unit.x == stem->unit.x && line2->unit.y == stem->unit.y))) {

		otherline = nullptr; l_changed = false;
		if ((!stem->leftline || stem->leftline->length < line2->length ) && chunk->l == pd2) {
		    stem->leftline = line2;
		    l_changed = true;
		    otherline = stem->rightline;
		} else if ((!stem->rightline || stem->rightline->length < line2->length ) && chunk->r == pd2 ) {
		    stem->rightline = line2;
		    l_changed = true;
		    otherline = stem->leftline;
		}
		if (!hv && l_changed && !stem->positioned && (!otherline || (otherline->length < line2->length)))
		    stem->setUnit (line2->unit);
	    }
	}
    }

    if (destem)
	addToStem (destem, pd, pd2, is_next, !is_next, 1);
    return stem;
}

/* AMK/GWW: This function is used when generating stem data for preexisting */
/* stem hints. If we already know the desired hint position, then we */
/* can safely assign to this hint any points which meet other conditions */
/* but have no corresponding position at the opposite edge. */
int GlyphData::halfStemNoOpposite (PointData *pd, StemData *stem, BasePoint *dir, bool is_next) {
    int ret=0, allowleft, allowright, hv, corner;

    for (auto &tstem : m_stems) {
	if (tstem.bbox || !tstem.positioned || &tstem == stem)
	    continue;
	allowleft = (!tstem.ghost || tstem.width == 20);
	allowright = (!tstem.ghost || tstem.width == 21);
	hv = Units::isHV (&tstem.unit, true);
	corner = ((pd->x_corner && hv == 2) || (pd->y_corner && hv == 1));

	if (Units::parallel (&tstem.unit, dir, true) || tstem.ghost || corner ) {
	    if (tstem.onStem (&pd->sp->me, true) && allowleft) {
		if (isCorrectSide (pd, is_next, true, &tstem.unit)) {
		    addToStem (&tstem, pd, nullptr, is_next, false, false);
		    ret++;
		}
	    } else if (tstem.onStem (&pd->sp->me, false) && allowright) {
		if (isCorrectSide (pd, is_next, false, &tstem.unit)) {
		    addToStem (&tstem, nullptr, pd, false, is_next, false);
		    ret++;
		}
	    }
	}
    }
    return ret;
}

StemData *GlyphData::halfStem (PointData *pd,
    BasePoint *dir, Conic *other, double other_t, bool is_next, int eidx) {
    /* Find the spot on other where the slope is the same as dir */
    double t1;
    double width;
    BasePoint match;
    StemData *stem = nullptr;
    PointData *pd2 = nullptr;

    t1 = find_same_slope (other, dir, other_t);
    if (t1==-1e4)
	return nullptr;
    if (t1<0 && other->from->prev && m_points[other->from->ptindex].colinear) {
	other = other->from->prev;
	t1 = find_same_slope (other, dir, 1.0);
    } else if (t1>1 && other->to->next && m_points[other->to->ptindex].colinear) {
	other = other->to->next;
	t1 = find_same_slope (other, dir, 0.0);
    }

    if (t1<-.001 || t1>1.001)
	return nullptr;

    /* GWW: Ok. the opposite edge has the right slope at t1 */
    /* Now see if we can make a one sided stem out of these two */
    match.x = ((other->conics[0].a*t1+other->conics[0].b)*t1+other->conics[0].c)*t1+other->conics[0].d;
    match.y = ((other->conics[1].a*t1+other->conics[1].b)*t1+other->conics[1].c)*t1+other->conics[1].d;

    width = (match.x-pd->sp->me.x)*dir->y - (match.y-pd->sp->me.y)*dir->x;
    /* offset = (match.x-pd->sp->me.x)*dir->x + (match.y-pd->sp->me.y)*dir->y;*/
    if (width<.5 && width>-.5)
	return nullptr;		/* Zero width stems aren't interesting */

    if (std::isnan (t1))
	FontShepherd::postError (QObject::tr ("NaN value in halfStem"));

    if (is_next) {
	pd->nextedges[eidx] = other;
	pd->next_e_t[eidx] = t1;
    } else {
	pd->prevedges[eidx] = other;
	pd->prev_e_t[eidx] = t1;
    }

    /* AMK/GWW: In my experience the only case where this function may be useful */
    /* is when it occasionally finds a real spline point which for some */
    /* reasons has been neglected by other tests and yet forms a valid  */
    /* pair for the first point. So run through points and see if we    */
    /* have actually got just a position on spline midway between to points, */
    /* or it is a normal point allowing to make a normal stem chunk */
    for (auto &tpd : m_points) {
	if (tpd.sp && tpd.sp->me.x == match.x && tpd.sp->me.y == match.y) {
	    pd2 = &tpd;
	    break;
	}
    }
    for (auto &tstem : m_stems) {
	if (Units::parallel (&tstem.unit, dir, true ) &&
	    tstem.bothOnStem (&pd->base, &match, false, false, false)) {
	    stem = &tstem;
	    break;
	}
    }
    if (!stem) {
	m_stems.emplace_back (dir, &pd->sp->me, &match);
	stem = &m_stems.back ();
    }

    addToStem (stem, pd, pd2, is_next, false, false);
    return stem;
}

bool GlyphData::connectsAcross (ConicPoint *sp, bool is_next, Conic *findme, int eidx) {
    PointData *pd = &m_points[sp->ptindex];
    Conic *other, *test;
    BasePoint dir;

    other = is_next ? pd->nextedges[eidx] : pd->prevedges[eidx];

    if (other==findme)
	return true;
    if (!other)
	return false;

    dir.x = is_next ? -pd->nextunit.x : pd->prevunit.x;
    dir.y = is_next ? -pd->nextunit.y : pd->prevunit.y;
    test = other->to->next;
    while (test && test != other &&
	    m_points[test->from->ptindex].nextunit.x * dir.x +
	    m_points[test->from->ptindex].nextunit.y * dir.y > 0) {
	if (test==findme)
	    return true;
	test = test->to->next;
    }

    dir.x = is_next ? pd->nextunit.x : -pd->prevunit.x;
    dir.y = is_next ? pd->nextunit.y : -pd->prevunit.y;
    test = other->from->prev;
    while (test && test != other &&
	    m_points[test->to->ptindex].prevunit.x * dir.x +
	    m_points[test->to->ptindex].prevunit.y * dir.y > 0) {
	if (test==findme)
	    return true;
	test = test->from->prev;
    }
    return false;
}

bool GlyphData::connectsAcrossToStem (PointData *pd, bool is_next,
    StemData *target, bool is_l, int eidx) {

    Conic *other, *test;
    BasePoint dir;
    PointData *tpd;
    int ecnt, stemidx;

    ecnt = is_next ? pd->next_e_cnt : pd->prev_e_cnt;
    if (ecnt < eidx + 1)
	return false;
    other = is_next ? pd->nextedges[eidx] : pd->prevedges[eidx];

    test = other;
    dir.x = is_next ? pd->nextunit.x : -pd->prevunit.x;
    dir.y = is_next ? pd->nextunit.y : -pd->prevunit.y;
    do {
	tpd = &m_points[test->to->ptindex];
	stemidx = target->assignedToPoint (tpd, false);
	if (stemidx != -1 && tpd->prev_is_l[stemidx] == !is_l &&
	    isSplinePeak (tpd, rint (target->unit.y), rint (target->unit.y), 7))
	    return true;

	test = test->to->next;
    } while (test && test != other && stemidx == -1 &&
	(tpd->prevunit.x * dir.x + tpd->prevunit.y * dir.y >= 0));

    test = other;
    dir.x = is_next ? -pd->nextunit.x : pd->prevunit.x;
    dir.y = is_next ? -pd->nextunit.y : pd->prevunit.y;
    do {
	tpd = &m_points[test->from->ptindex];
	stemidx = target->assignedToPoint (tpd, true);
	if (stemidx != -1 && tpd->next_is_l[stemidx] == !is_l &&
	    isSplinePeak (tpd, rint(target->unit.y), rint (target->unit.y), 7))
	    return true;

	test = test->from->prev;
    } while (test && test != other && stemidx == -1 &&
	(tpd->nextunit.x * dir.x + tpd->nextunit.y * dir.y >= 0));
    return false;
}

int GlyphData::buildStem (PointData *pd, bool is_next, bool require_existing, bool has_existing, int eidx) {
    BasePoint *dir;
    Conic *other, *cur;
    double t;
    double tod, fromd, dist;
    ConicPoint *testpt, *topt, *frompt;
    LineData *line;
    PointData *testpd, *topd, *frompd;
    int tp, fp, t_needs_recalc=false, ret=0;
    uint8_t tstub=0, fstub=0;
    BasePoint opposite;
    StemData *stem=nullptr;

    if (is_next) {
	dir = &pd->nextunit;
	other = pd->nextedges[eidx];
	cur = pd->sp->next;
	t = pd->next_e_t[eidx];
	dist = pd->next_dist[eidx];
    } else {
	dir = &pd->prevunit;
	other = pd->prevedges[eidx];
	cur = pd->sp->prev;
	t = pd->prev_e_t[eidx];
	dist = pd->prev_dist[eidx];
    }
    topt = other->to; frompt = other->from;
    topd = &m_points[topt->ptindex];
    frompd = &m_points[frompt->ptindex];

    line = is_next ? pd->nextline : pd->prevline;
    if (!Units::isHV (dir, true) && line)
	dir = &line->unit;

    if (!other)
	return 0;

    opposite.x = ((other->conics[0].a*t+other->conics[0].b)*t+other->conics[0].c)*t+other->conics[0].d;
    opposite.y = ((other->conics[1].a*t+other->conics[1].b)*t+other->conics[1].c)*t+other->conics[1].d;

    if (eidx == 0) tstub = isStubOrIntersection (dir, pd, topd, is_next, false);
    if (eidx == 0) fstub = isStubOrIntersection (dir, pd, frompd, is_next, true);
    tp = topd->parallelToDir (false, dir, &opposite, pd->sp, tstub);
    fp = frompd->parallelToDir (true, dir, &opposite, pd->sp, fstub);

    /* GWW/AMK: if none of the opposite points is parallel to the needed vector, then    */
    /* give it one more chance by skipping those points and looking at the next */
    /* and previous one. This can be useful in situations where the opposite    */
    /* edge cannot be correctly detected just because there are too many points */
    /* on the spline (which is a very common situation for poorly designed      */
    /* fonts or fonts with quadratic splines). */
    /* But do that only for colinear spline segments and ensure that there are  */
    /* no bends between two splines. */
    if (!tp && (!fp || t > 0.5) && topd->colinear && &other->to->next) {
	testpt = topt->next->to;
	testpd = &m_points[testpt->ptindex];
	BasePoint *initdir = &topd->prevunit;
	while (!tp && topd->colinear && pd->sp != testpt && other->from != testpt && (
	    testpd->prevunit.x * initdir->x +
	    testpd->prevunit.y * initdir->y > 0 )) {

	    topt = testpt; topd = testpd;
	    tp = topd->parallelToDir (false, dir, &opposite, pd->sp, false);
	    testpt = topt->next->to;
	    testpd = &m_points[testpt->ptindex];
	}
	if (tp) t_needs_recalc = true;
    }
    if (!fp && (!fp || t < 0.5) && frompd->colinear && &other->from->prev) {
	testpt = frompt->prev->from;
	testpd = &m_points[testpt->ptindex];
	BasePoint *initdir = &frompd->prevunit;
	while (!fp && frompd->colinear && pd->sp != testpt && other->to != testpt && (
	    testpd->prevunit.x * initdir->x +
	    testpd->prevunit.y * initdir->y > 0)) {

	    frompt = testpt; frompd = testpd;
	    fp = frompd->parallelToDir (true, dir, &opposite, pd->sp, false);
	    testpt = frompt->prev->from;
	    testpd = &m_points[testpt->ptindex];
	}
	if (fp) t_needs_recalc = true;
    }
    if (t_needs_recalc)
	t = other->recalcT (frompt, topt, t);
    if (!tp && !fp) {
	if (has_existing)
	    ret = halfStemNoOpposite (pd, nullptr, dir, is_next);
	return ret;
    }

    /* GWW: We have several conflicting metrics for getting the "better" stem */
    /* Generally we prefer the stem with the smaller width (but not always. See tilde) */
    /* Generally we prefer the stem formed by the point closer to the intersection */
    tod = (1-t)*normal_dist (&topt->me, &pd->sp->me, dir);
    fromd = t*normal_dist (&frompt->me, &pd->sp->me, dir);

    if (tp && ((tod<fromd) ||
	(!fp && ( tod<2*fromd || dist < topd->prev_dist[eidx] ||
	    connectsAcross (frompt, true, cur, eidx) || nearly_parallel (dir, other, t))))) {
	stem = testStem (pd, dir, topt, is_next, false, require_existing, tstub, eidx);
    }
    if (!stem && fp && ((fromd<tod) ||
	(!tp && (fromd<2*tod || dist < frompd->next_dist[eidx] ||
	    connectsAcross (topt, false, cur, eidx) || nearly_parallel (dir, other, t))))) {
	stem = testStem (pd, dir, frompt, is_next, true, require_existing, fstub, eidx);
    }
    if (eidx == 0 && !stem && !require_existing && cur && !other->islinear && !cur->islinear )
	stem = halfStem (pd, dir, other, t, is_next, eidx);
    if (stem) ret = 1;
    if (has_existing)
	ret += halfStemNoOpposite (pd, stem, dir, is_next);
    return ret;
}

void GlyphData::assignLinePointsToStems () {
    PointData *pd;
    StemData *stem;
    LineData *line;
    struct stem_chunk *chunk;
    int stem_hv, line_hv, needs_hv=false;

    for (size_t i=0; i<m_stems.size (); i++) if (!m_stems[i].toobig) {
	stem = &m_stems[i];
	stem_hv = Units::isHV (&stem->unit, true);
	needs_hv = (stem_hv || (stem->chunks.size () == 1 &&
	    stem->chunks[0].stub && Units::isHV (&stem->unit, false)));

	if (stem->leftline) {
	    line = stem->leftline;
	    line_hv = (needs_hv && line->fitsHV ());

	    if (needs_hv && !line_hv)
		stem->leftline = nullptr;
	    else {
		for (size_t j=0; j<line->points.size (); j++) {
		    pd = line->points[j];
		    if (pd->prevline == line && stem->onStem (&pd->base, true) &&
			stem->assignedToPoint (pd, false) == -1) {
			chunk = addToStem (stem, pd, nullptr, false, false, false);
			chunk->lpotential = true;
		    }
		    if (pd->nextline == line && stem->onStem (&pd->base, true) &&
			stem->assignedToPoint (pd, true ) == -1 ) {
			chunk = addToStem (stem, pd, nullptr, true, false, false);
			chunk->lpotential = true;
		    }
		}
	    }
	}
	if (stem->rightline) {
	    line = stem->rightline;
	    line_hv = (needs_hv && line->fitsHV ());

	    if (needs_hv && !line_hv)
		stem->rightline = nullptr;
	    else {
		for (size_t j=0; j<line->points.size (); j++) {
		    pd = line->points[j];
		    if (pd->prevline == line && stem->onStem (&pd->base, false) &&
			stem->assignedToPoint (pd, false) == -1) {
			chunk = addToStem (stem, nullptr, pd, false, false, false);
			chunk->rpotential = true;
		    }
		    if (pd->nextline == line && stem->onStem (&pd->base, false) &&
			stem->assignedToPoint (pd, true) == -1 ) {
			chunk = addToStem (stem, nullptr, pd, false, true, false);
			chunk->rpotential = true;
		    }
		}
	    }
	}
    }
}

StemData *GlyphData::diagonalCornerStem (PointData *pd, int require_existing) {
    Conic *other = pd->bothedge[0];
    PointData *pfrom = nullptr, *pto = nullptr, *pd2 = nullptr, *pd3 = nullptr;
    double width, length;
    StemData *stem;

    pfrom = &m_points[other->from->ptindex];
    pto = &m_points[other->to->ptindex];
    if (pd->symetrical_h && pto->symetrical_h && pd->both_e_t[0]>.9)
	pd2 = pto;
    else if (pd->symetrical_h && pfrom->symetrical_h && pd->both_e_t[0]<.1)
	pd2 = pfrom;
    else if (pd->symetrical_v && pto->symetrical_v && pd->both_e_t[0]>.9)
	pd2 = pto;
    else if (pd->symetrical_v && pfrom->symetrical_v && pd->both_e_t[0]<.1)
	pd2 = pfrom;
    else if (pd->symetrical_h && other->islinear && other->conics[1].c==0) {
	pd2 = pfrom;
	pd3 = pto;
    } else if (pd->symetrical_v && other->islinear && other->conics[0].c==0) {
	pd2 = pfrom;
	pd3 = pto;
    } else
	return nullptr;

    if (pd->symetrical_v)
	width = (pd->sp->me.x-pd2->sp->me.x);
    else
	width = (pd->sp->me.y-pd2->sp->me.y);
    length = (pd->sp->next->to->me.x-pd->sp->me.x)*(pd->sp->next->to->me.x-pd->sp->me.x) +
	     (pd->sp->next->to->me.y-pd->sp->me.y)*(pd->sp->next->to->me.y-pd->sp->me.y);
    if (width*width>length)
	return nullptr;

    stem = findOrMakeHVStem (pd, pd2, pd->symetrical_h, require_existing);

    if (!pd3 && stem)
	addToStem (stem, pd, pd2, 2, 2, 2);
    else if (stem) {
	addToStem (stem, pd, pd2, 2, 2, 3);
	addToStem (stem, pd, pd3, 2, 2, 3);
    }
    return stem;
}

int GlyphData::valueChunk (std::vector<struct vchunk> &vchunks, int idx, bool l_base) {
    int peak1=0, peak2=0, val=0;
    bool is_x, base_next, opp_next;
    int chcnt = vchunks.size (), i;
    PointData *base, *opp, *frompd, *topd;
    struct stem_chunk *chunk = vchunks[idx].chunk, *tchunk;
    StemData *stem = chunk->parent;
    double norm, dist;
    Conic *sbase, *sopp, *other;

    /* If a stem was already present before generating glyph data, */
    /* then it should always be preferred in case of a conflict    */
    if (stem->positioned || chunk->stemcheat) val++;

    if (l_base) {
	base = chunk->l; opp = chunk->r;
	base_next = chunk->lnext; opp_next = chunk->rnext;
    } else {
	base = chunk->r; opp = chunk->l;
	base_next = chunk->rnext; opp_next = chunk->lnext;
    }
    sbase = base_next ? base->sp->next : base->sp->prev;
    sopp =  opp_next  ? opp->sp->next : opp->sp->prev;
    other = opp_next  ? opp->nextedges[0] : opp->prevedges[0];

    /* AMK/GWW: If there are 2 conflicting chunks belonging to different stems  */
    /* but based on the same point, then we have to decide which stem is        */
    /* "better" for that point. We compare stems (or rather chunks) by assigning*/
    /* a value to each of them and then prefer the stem whose value is positive.*/
    /* A chunk gets a +1 value bonus in the following cases:                    */
    /* - The stem is vertical/horizontal and splines are curved in the same     */
    /*   direction at both sides of the chunk;                                  */
    /* - A stem has both its width and the distance between the opposite points */
    /*   smaller than another stem;                                             */
    /* - The common side of two stems is a straight line formed by two points   */
    /*   and the opposite point can be projected to line segment between those  */
    /*   two points. */
    if (Units::isHV (&stem->unit, true) && !sbase->islinear) {
	is_x = (bool) rint (stem->unit.y);
	peak1 = is_x ? base->x_extr : base->y_extr;
	peak2 = is_x ? opp->x_extr  : opp->y_extr;

	dist =  (base->base.x - opp->base.x)*stem->unit.x +
		(base->base.y - opp->base.y)*stem->unit.y;

	/* AMK/GWW: Are there any stems attached to the same base point which   */
	/* are narrower than the distance between two points forming the        */
	/* given chunk? */
	for (i=0; i<chcnt; i++) {
	    tchunk = vchunks[i].chunk;
	    if (!tchunk || tchunk == chunk || !chunk->l || !chunk->r)
		continue;
	    norm = tchunk->parent->width;
	    if (norm < fabs (dist))
		break;
	}

	/* AMK/GWW: If both points are curved in the same direction, then check also */
	/* the "line of sight" between those points (if there are interventing */
	/* splines, then it is not a real feature bend)*/
	if (i == chcnt && peak1 + peak2 == 3 && connectsAcross (base->sp, opp_next, sopp, 0))
	    val++;
    }

    frompd = &m_points[sbase->from->ptindex];
    topd = &m_points[sbase->to->ptindex];

    if (stem->assignedToPoint (frompd, true ) != -1 &&
	stem->assignedToPoint (topd, false ) != -1)
	if (other == sbase) val++;

    dist = vchunks[idx].dist;
    for (i=0; i<chcnt; i++) {
	tchunk = vchunks[i].chunk;
	if (!tchunk || tchunk == chunk || (vchunks[idx].parallel && !vchunks[i].parallel))
	    continue;
	if (vchunks[i].dist <= dist || tchunk->parent->width <= stem->width)
	    break;
    }
    if (i==chcnt) val++;

    /* AMK/GWW: If just one of the checked chunks has both its sides parallel   */
    /* to the stem direction, then we consider it is always worth to be output. */
    /* This check was introduced to avoid situations where a stem marking       */
    /* a feature termination can be preferred to another stem which controls    */
    /* the main part of the same feature */
    if (vchunks[idx].parallel) {
	for (i=0; i<chcnt; i++) {
	    if (!vchunks[i].chunk || vchunks[i].chunk == chunk)
		continue;
	    if (vchunks[i].parallel)
		break;
	}
	if (i == chcnt) val++;
    }

    return val;
}

void GlyphData::checkPotential (PointData *pd, int is_next) {
    int i, is_l, next1, val;
    int val_cnt=0;
    BasePoint *lunit, *runit;
    std::vector<StemData *> &stems = is_next ? pd->nextstems : pd->prevstems;
    std::vector<struct vchunk> vchunks;
    struct stem_chunk *cur;
    int stemcnt = stems.size ();

    vchunks.resize (stemcnt);

    for (i=0; i<stemcnt; i++) {
	is_l = is_next ? pd->next_is_l[i] : pd->prev_is_l[i];
	stems[i]->findClosestOpposite (&vchunks[i].chunk, pd->sp, &next1);
	if (!vchunks[i].chunk)
	    continue;
	cur = vchunks[i].chunk;
	if (vchunks[i].value > 0) val_cnt++;
	vchunks[i].dist  =  pow (cur->l->base.x - cur->r->base.x, 2) +
			    pow (cur->l->base.y - cur->r->base.y, 2);
	lunit = cur->lnext ? &cur->l->nextunit : &cur->l->prevunit;
	runit = cur->rnext ? &cur->r->nextunit : &cur->r->prevunit;
	vchunks[i].parallel =   Units::parallel (lunit, &stems[i]->unit, 2) &&
				Units::parallel (runit, &stems[i]->unit, 2);
    }

    for (i=0; i<stemcnt; i++) if (vchunks[i].chunk) {
	vchunks[i].value = valueChunk (vchunks, i, is_l);
	if (vchunks[i].value) val_cnt++;
    }

    /* AMK/GWW: If we was unable to figure out any reasons for which at least */
    /* one of the checked chunks should really be output, then keep  */
    /* all the 'potential' flags as they are and do nothing */
    int max_val = -1;
    int *pref = is_next ? &pd->next_pref : &pd->prev_pref;
    if (val_cnt > 0) {
	for (i=0; i<stemcnt; i++) if (vchunks[i].chunk)  {
	    is_l = is_next ? pd->next_is_l[i] : pd->prev_is_l[i];
	    val = vchunks[i].value;
	    // Mark the "preferred" stem for point. Need this for figuring hint masks
	    if (val > max_val) {
		*pref = i;
		max_val = val;
	    }
	    for (auto &cur: stems[i]->chunks) {
		if (is_l && cur.l == pd) {
		    if (val > 0) cur.lpotential = false;
		    else cur.lpotential = true;
		} else if (!is_l && cur.r == pd) {
		    if (val > 0) cur.rpotential = false;
		    else cur.rpotential = true;
		}
	    }
	}
    }
}

bool GlyphData::stemIsActiveAt (StemData *stem, double stempos) {
    BasePoint pos, cpos, mpos;
    int which;
    double test;
    double lmin, lmax, rmin, rmax, loff, roff, minoff, maxoff;
    std::vector<Monotonic *> space;
    int winding, nw, closest, j;
    space.reserve (m_ms.size ());

    pos.x = stem->left.x + stempos*stem->unit.x;
    pos.y = stem->left.y + stempos*stem->unit.y;

    if (Units::isHV (&stem->unit, true)) {
	size_t i;
	Monotonic *m;
	which = (stem->unit.x==0);
	Monotonics::findAt (m_ms, which, which ? pos.y : pos.x, space);
	test = which ? pos.x : pos.y;

	lmin = (stem->lmax - 2*dist_error_hv < -dist_error_hv) ?
	    stem->lmax - 2*dist_error_hv : -dist_error_hv;
	lmax = (stem->lmin + 2*dist_error_hv > dist_error_hv) ?
	    stem->lmin + 2*dist_error_hv : dist_error_hv;
	rmin = (stem->rmax - 2*dist_error_hv < -dist_error_hv) ?
	    stem->rmax - 2*dist_error_hv : -dist_error_hv;
	rmax = (stem->rmin + 2*dist_error_hv > dist_error_hv) ?
	    stem->rmin + 2*dist_error_hv : dist_error_hv;
	minoff = test + (lmin * stem->unit.y - lmax * stem->unit.x);
	maxoff = test + (lmax * stem->unit.y - lmin * stem->unit.x);

	winding = 0; closest = -1;
	int desired = order2 () ? 1 : -1;
	for (i=0; i<space.size (); ++i) {
	    m = space[i];
	    uint8_t up = which ? m->yup : m->xup;
	    nw = (up ? 1 : -1);
	    if (m->other >= minoff && m->other <= maxoff && nw == desired) {
		closest = i;
		break;
	    } else if (m->other > maxoff)
		break;
	    winding += nw;
	}
	if (closest < 0)
	    return false;

	cpos.x = which ? m->other : pos.x;
	cpos.y = which ? pos.y : m->other;
	loff = (cpos.x - stem->left.x) * stem->unit.y -
	       (cpos.y - stem->left.y) * stem->unit.x;
	if (loff > lmax || loff < lmin)
	    return false;

	j = match_winding (space, i, nw, winding, which, 0);
	if (j==-1)
	    return false;
	m = space[j];

	mpos.x = which ? m->other : pos.x ;
	mpos.y = which ? pos.y : m->other ;
	roff = (mpos.x - stem->right.x) * stem->unit.y -
	       (mpos.y - stem->right.y) * stem->unit.x;
	if (roff > rmax || roff < rmin)
	    return false;
	return true;
    }
    return stillStem (dist_error_diag, &pos, stem);
}

/* AMK/GWW: This function is used to check the distance between a hint's edge */
/* and a spline and determine the extet where this hint can be */
/* considered "active". */
int GlyphData::walkSpline (PointData *pd, bool gonext, StemData *stem,
    bool is_l, bool force_ac, BasePoint *res) {

    int i, curved;
    double off, dist, min, max;
    double incr, err;
    double t, ratio, width;
    Conic *s;
    BasePoint *base, *nunit, pos, good;
    ConicPoint *sp, *nsp;
    PointData *npd;

    err = Units::isHV (&stem->unit, true) ? dist_error_hv : dist_error_diag;
    width = stem->width;
    ratio = m_glyph.upm ()/(6 * width);
    if (err > width/2) err = width/2;

    sp = pd->sp;
    base = is_l ? &stem->left : &stem->right;
    min =  is_l ? stem->lmax - 2*err : stem->rmax - 2*err;
    max =  is_l ? stem->lmin + 2*err : stem->rmin + 2*err;

    s = gonext ? sp->next : sp->prev;
    nsp = gonext ? s->to : s->from;
    npd = &m_points[nsp->ptindex];
    nunit = gonext ? &npd->prevunit : &npd->nextunit;
    good = sp->me;

    off   = (nsp->me.x - base->x)*stem->l_to_r.x +
	    (nsp->me.y - base->y)*stem->l_to_r.y;
    /* GWW: Some splines have tiny control points and are almost flat */
    /*  think of them as lines then rather than treating them as curves */
    /*  figure out how long they remain within a few orthoganal units of */
    /*  the point */
    /* AMK/GWW: We used to check the distance between a control point and a spline */
    /* and consider the segment "flat" if this distance is smaller than   */
    /* the normal allowed "error" value. However this method doesn't produce */
    /* consistent results if the spline is not long enough (as usual for  */
    /* fonts with quadratic splines). So now we consider a spline "flat"  */
    /* only if it never deviates too far from the hint's edge and both    */
    /* its terminal points are snappable to the same hint */
    curved = (stem->assignedToPoint (npd, gonext) == -1 &&
	(off < min || off > max || !Units::parallel (&stem->unit, nunit, true)));

    /* AMK/GWW: If a spline does deviate from the edge too far to consider it flat, */
    /* then we calculate the extent where the spline and the edge are still */
    /* close enough to consider the hint active at this zone. If the hint is */
    /* still active at the end of the spline, we can check some subsequent splines */
    /* too. This method produces better effect than any "magic" manipulations */
    /* with control point coordinates, because it takes into account just the */
    /* spline configuration rather than point positions */
    if (curved) {
	max = err = dist_error_curve;
	min = -dist_error_curve;
	/* AMK/GWW: The following statement forces our code to detect an active zone */
	/* even if all checks actually fail. This makes sense for stems */
	/* marking arks and bends */
	if (force_ac)
	    good = (gonext) ? sp->nextcp : sp->prevcp;
	/* AMK/GWW: If a spline is closer to the opposite stem edge than to the current edge, then we */
	/* can no longer consider the stem active at this point */
	if (err > width/2) err = width/2;

	t = gonext ? 0.9999 : 0.0001;
	for ( ; ; s = gonext ? s->to->next : s->from->prev) {
	    pos.x = ((s->conics[0].a*t+s->conics[0].b)*t+s->conics[0].c)*t+s->conics[0].d;
	    pos.y = ((s->conics[1].a*t+s->conics[1].b)*t+s->conics[1].c)*t+s->conics[1].d;
	    off   = (pos.x - base->x )*stem->l_to_r.x +
		    (pos.y - base->y )*stem->l_to_r.y;
	    dist  = (pos.x - sp->me.x)*stem->unit.x +
		    (pos.y - sp->me.y)*stem->unit.y;
	    nsp   = gonext ? s->to : s->from;
	    npd   = &m_points[nsp->ptindex];
	    if (fabs (off) < max && fabs (dist) <= (width + width * ratio) &&
		nsp != sp && npd->colinear && !npd->x_extr && !npd->y_extr &&
		stillStem (err, &pos, stem))
		good = pos;
	    else
		break;
	}
    }
    t = .5;
    incr = gonext ? .25 : -.25;
    for (i=0; i<6; ++i) {
	pos.x = ((s->conics[0].a*t+s->conics[0].b)*t+s->conics[0].c)*t+s->conics[0].d;
	pos.y = ((s->conics[1].a*t+s->conics[1].b)*t+s->conics[1].c)*t+s->conics[1].d;
	off   = (pos.x - base->x )*stem->l_to_r.x +
		(pos.y - base->y )*stem->l_to_r.y;
	dist  = (pos.x - sp->me.x)*stem->unit.x +
		(pos.y - sp->me.y)*stem->unit.y;
	/* Don't check StillStem for non-curved segments, as they are subject */
	/* to further projection-related tests anyway */
	if (off > min && off < max && (!curved ||
	    (fabs (dist) < (width + width * ratio) && stillStem (err, &pos, stem)))) {

	    good = pos;
	    t += incr;
	} else
	    t -= incr;
	incr/=2;
    }
    *res = good;
    return curved;
}

int GlyphData::addLineSegment (StemData *stem, std::vector<struct segment> &space, bool is_l,
    PointData *pd, bool base_next) {

    double s, e, t, dot;
    BasePoint stemp, etemp;
    BasePoint *start, *end, *par_unit;
    int same_dir, corner = 0;
    bool scurved = false, ecurved = false, c, hv;
    ConicPoint *sp, *psp, *nsp;
    double b;
    uint8_t extr;
    struct segment newseg;

    if (!pd || (sp = pd->sp)==nullptr || sp->checked || !sp->next || !sp->prev)
	return space.size ();
    end = &sp->me;
    start = &sp->me;
    par_unit = base_next ? &pd->nextunit : &pd->prevunit;
    /* Do the spline and the stem unit point in the same direction ? */
    dot =   (stem->unit.x * par_unit->x) +
	    (stem->unit.y * par_unit->y);
    same_dir = ((dot > 0 && base_next) || (dot < 0 && !base_next));
    if (stem->unit.x == 1) corner = pd->y_corner;
    else if (stem->unit.y == 1) corner = pd->x_corner;

    dot =   (stem->unit.x * pd->nextunit.x) +
	    (stem->unit.y * pd->nextunit.y);
    /* AMK/GWW: We used to apply normal checks only if the point's unit vector pointing */
    /* in the direction we are going to check is nearly parallel to the stem unit. */
    /* But this is not the best method, because a spline, "parallel" to our */
    /* stem, may actually have filled space at a wrong side. On the other hand, */
    /* sometimes it makes sense to calculate active space even for splines */
    /* connected to our base point under an angle which is too large to consider */
    /* the direction "parallel". So now we check the units' direction first */
    /* and then (just for straight splines) also their parallelity. */
    if ((dot > 0 && same_dir) || (dot < 0 && !same_dir)) {
	/* If the segment sp-start doesn't have exactly the right slope, then */
	/*  we can only use that bit of it which is less than a standard error */
	bool par = Units::parallel (&stem->unit, &pd->nextunit, 0);
	if (!sp->next->islinear) {
	    ecurved = walkSpline (pd, true, stem, is_l, par, &etemp);
	    /* Can merge, but treat as curved relatively to projections */
	    if (!ecurved) ecurved = 2;
	    end = &etemp;
	} else if (par || corner)  {
	    nsp = sp->next->to;
	    ecurved = adjust_for_imperfect_slope_match (sp, &nsp->me, &etemp, stem, is_l);
	    end = &etemp;
	}
    }
    dot =   (stem->unit.x * pd->prevunit.x) +
	    (stem->unit.y * pd->prevunit.y);
    if ((dot < 0 && same_dir) || (dot > 0 && !same_dir)) {
	bool par = Units::parallel (&stem->unit, &pd->prevunit, 0);
	if (!sp->prev->islinear) {
	    scurved = walkSpline (pd, false, stem, is_l, par, &stemp);
	    if (!scurved) scurved = 2;
	    start = &stemp;
	} else if (par || corner) {
	    psp = sp->prev->from;
	    scurved = adjust_for_imperfect_slope_match (sp, &psp->me, &stemp, stem, is_l);
	    start = &stemp;
	}
    }
    sp->checked = true;

    s = (start->x-stem->left.x)*stem->unit.x +
	(start->y-stem->left.y)*stem->unit.y;
    e = (  end->x-stem->left.x)*stem->unit.x +
	(  end->y-stem->left.y)*stem->unit.y;
    b = (sp->me.x-stem->left.x)*stem->unit.x +
	(sp->me.y-stem->left.y)*stem->unit.y;

    if (s == e)
	return space.size ();
    if (s > e) {
	t = s; c = scurved;
	s = e; e = t;
	scurved = ecurved; ecurved = c;
    }
    newseg.start = s;
    newseg.end = e;
    newseg.sbase = newseg.ebase = b;
    newseg.scurved = scurved;
    newseg.ecurved = ecurved;

    hv = Units::isHV (&stem->unit, true);
    if (hv) {
	/* AMK/GWW: For vertical/horizontal stems we assign a special meaning to  */
	/* the 'curved' field. It will be non-zero if the key point of   */
	/* this segment is positioned on a prominent curve:              */
	/* 1 if the inner side of that curve is inside of the contour    */
	/* and 2 otherwise. */
	/* Later, if we get a pair of "inner" and "outer" curves, then   */
	/* we are probably dealing with a feature's bend which should be */
	/* necessarily marked with a hint. Checks we apply for this type */
	/* of curved segments should be less strict than in other cases. */
	extr = (hv == 1) ? pd->y_extr : pd->x_extr;
	newseg.curved = extr;
    } else {
	/* AMK/GWW: For diagonal stems we consider a segment "curved" if both its */
	/* start and end are curved. Curved segments usually cannot be   */
	/* merged (unless scurved or ecurved is equal to 2) and are not  */
	/* checked for "projections". */
	newseg.curved = scurved && ecurved;
    }
    space.push_back (newseg);
    return space.size ();
}

void GlyphData::figureStemActive (StemData *stem) {
    int i, j, pcnt=0;
    struct stem_chunk *chunk;
    int lcnt, rcnt, acnt, cove, startset, endset;
    size_t bpos;
    double middle, width, len, clen, gap, lseg, rseg;
    double err, lmin, rmax, loff, roff, last, s, e, sbase, ebase;
    double proj, proj2, proj3, orig_proj, ptemp;
    std::vector<PointData *> pspace;
    std::vector<struct segment> lspace, rspace, bothspace;
    std::vector<struct segment> &activespace = stem->active;

    width = stem->width;
    int maxcnt = stem->chunks.size ();
    lspace.reserve (maxcnt);
    rspace.reserve (maxcnt);
    bothspace.reserve (maxcnt);

    for (auto &rpd : m_points) if (rpd.sp)
	rpd.sp->checked = false;

    lcnt = rcnt = 0;
    for (auto &chunk : stem->chunks) {
	if (chunk.stemcheat)
	    continue;
	lcnt = addLineSegment (stem, lspace, true , chunk.l, chunk.lnext);
	rcnt = addLineSegment (stem, rspace, false, chunk.r, chunk.rnext);
    }
    if (lcnt!=0 && rcnt!=0) {
	/* AMK/GWW: For curved segments we can extend left and right active segments */
	/* a bit to ensure that they do overlap and thus can be marked with an */
	/* active zone */
	int chunk_cnt = stem->chunks.size ();
	if (rcnt == lcnt && chunk_cnt == lcnt) {
	    for (i=0; i<lcnt; i++) {
		/* If it's a feature bend, then our tests should be more liberal */
		cove = ((rspace[i].curved + lspace[i].curved) == 3);
		gap = 0;
		if (lspace[i].start>rspace[i].end && lspace[i].scurved && rspace[i].ecurved)
		    gap = lspace[i].start-rspace[i].end;
		else if (rspace[i].start>lspace[i].end && rspace[i].scurved && lspace[i].ecurved)
		    gap = rspace[i].start-lspace[i].end;
		else if (!cove)
		    continue;

		lseg = lspace[i].end - lspace[i].start;
		rseg = rspace[i].end - rspace[i].start;
		if ((cove && gap < (lseg > rseg ? lseg : rseg)) ||
		    (gap < (lseg + rseg)/2 && !stem->chunks[i].stub)) {
		    if (lspace[i].ebase<rspace[i].start)
			rspace[i].start = lspace[i].ebase;
		    else if (lspace[i].sbase>rspace[i].end)
			rspace[i].end = lspace[i].sbase;
		    if (rspace[i].ebase<lspace[i].start)
			lspace[i].start = rspace[i].ebase;
		    else if (rspace[i].sbase>lspace[i].end)
			lspace[i].end = rspace[i].sbase;
		}
	    }
	}

	std::sort (lspace.begin (), lspace.end (), [](struct segment &s1, struct segment &s2) {
	    return (s1.start < s2.start);
	});
	std::sort (rspace.begin (), rspace.end (), [](struct segment &s1, struct segment &s2) {
	    return (s1.start < s2.start);
	});

	lcnt = merge_segments (lspace);
	rcnt = merge_segments (rspace);

	for (i=j=0; i<lcnt && j<rcnt; ++i) {
	    while (j<rcnt && rspace[j].end<=lspace[i].start)
		++j;
	    while (j<rcnt && rspace[j].start<=lspace[i].end) {
		cove = ((rspace[j].curved + lspace[i].curved) == 3);
		struct segment common;

		s = (rspace[j].start > lspace[i].start) ?
		    rspace[j].start : lspace[i].start;
		e = (rspace[j].end < lspace[i].end) ?
		    rspace[j].end : lspace[i].end;
		sbase = (rspace[j].start > lspace[i].start) ?
		    lspace[i].sbase : rspace[j].sbase;
		ebase = (rspace[j].end < lspace[i].end) ?
		    lspace[i].ebase : rspace[j].ebase;

		middle = (lspace[i].start + rspace[j].start)/2;
		common.start = (cove && middle < s) ? middle : s;
		if (rspace[j].start > lspace[i].start)
		    common.scurved = (rspace[j].scurved || sbase < s) ?
			rspace[j].scurved : lspace[i].scurved;
		else
		    common.scurved = (lspace[i].scurved || sbase < s) ?
			lspace[i].scurved : rspace[j].scurved;

		middle = (lspace[i].end + rspace[j].end)/2;
		common.end = (cove && middle > e) ? middle : e;
		if (rspace[j].end < lspace[i].end)
		    common.ecurved = (rspace[j].ecurved || ebase > e) ?
			rspace[j].ecurved : lspace[i].ecurved;
		else
		    common.ecurved = (lspace[i].ecurved || ebase > e) ?
			lspace[i].ecurved : rspace[j].ecurved;

		sbase = (rspace[j].sbase > lspace[i].sbase) ?
		    rspace[j].sbase : lspace[i].sbase;
		ebase = (rspace[j].ebase < lspace[i].ebase) ?
		    rspace[j].ebase : lspace[i].ebase;
		if (sbase > common.end)
		    sbase = ebase = common.end;
		else if (ebase < common.start)
		    sbase = ebase = common.start;
		else if (ebase < sbase)
		    ebase = sbase = (ebase + sbase)/2;
		common.sbase = sbase;
		common.ebase = ebase;

		common.curved = rspace[j].curved || lspace[i].curved;
		bothspace.push_back (common);

		if (rspace[j].end>lspace[i].end)
		    break;
		++j;
	    }
	}
    }
#if GLYPH_DATA_DEBUG
    std::cerr << "Active zones for stem l=" << stem->left.x << ',' << stem->left.y
	<< " r=" << stem->right.x << ',' << stem->right.y
	<< " dir=" << stem->unit.x << ',' << stem->unit.y << ':' << std::endl;
    for (size_t i=0; i<lspace.size (); i++) {
	std::cerr << "\tleft space curved=" << lspace[i].curved << std::endl;
	std::cerr   << "\t\tstart=" << lspace[i].start
		    << ", base=" << lspace[i].sbase
		    << ", curved=" << lspace[i].scurved << std::endl;
	std::cerr   << "\t\tend=" << lspace[i].end
		    << ", base=" << lspace[i].ebase
		    << ", curved=" << lspace[i].ecurved << std::endl;
    }
    for (size_t i=0; i<rspace.size (); i++) {
	std::cerr << "\tright space curved=" << rspace[i].curved << std::endl;
	std::cerr   << "\t\tstart=" << rspace[i].start
		    << ", base=" << rspace[i].sbase
		    << ", curved=" << rspace[i].scurved << std::endl;
	std::cerr   << "\t\tend=" << rspace[i].end
		    << ", base=" << rspace[i].ebase
		    << ", curved=" << rspace[i].ecurved << std::endl;
    }
    for (size_t i=0; i<bothspace.size (); i++) {
	std::cerr << "\tboth space curved=" << bothspace[i].curved << std::endl;
	std::cerr   << "\t\tstart=" << bothspace[i].start
		    << ", base=" << bothspace[i].sbase
		    << ", curved=" << bothspace[i].scurved << std::endl;
	std::cerr   << "\t\tend=" << bothspace[i].end
		    << ", base=" << bothspace[i].ebase
		    << ", curved=" << bothspace[i].ecurved << std::endl;
    }
#endif

    err = (stem->unit.x == 0 || stem->unit.y == 0) ?
	dist_error_hv : dist_error_diag;
    lmin = (stem->lmin < -err) ? stem->lmin : -err;
    rmax = (stem->rmax > err) ? stem->rmax : err;
    acnt = 0;
    activespace.resize (3*m_points.size ());
    if (!bothspace.empty ()) {
	for (auto &pd : m_points) if (pd.sp) {
	    /* GWW: Let's say we have a stem. And then inside that stem we have */
	    /*  another rectangle. So our first stem isn't really a stem any */
	    /*  more (because we hit another edge first), yet it's still reasonable*/
	    /*  to align the original stem */
	    /* Now suppose the rectangle is rotated a bit so we can't make */
	    /*  a stem from it. What do we do here? */
	    loff =  (pd.sp->me.x - stem->left.x ) * stem->unit.y -
		    (pd.sp->me.y - stem->left.y ) * stem->unit.x;
	    roff =  (pd.sp->me.x - stem->right.x) * stem->unit.y -
		    (pd.sp->me.y - stem->right.y) * stem->unit.x;

	    if (loff >= lmin && roff <= rmax) {
		pd.projection = (pd.sp->me.x - stem->left.x)*stem->unit.x +
				(pd.sp->me.y - stem->left.y)*stem->unit.y;
		if (in_active (pd.projection, bothspace))
		    pspace.push_back (&pd);
	    }
	    pcnt = pspace.size ();
	}

	std::sort (pspace.begin (), pspace.end (), [](PointData *p1, PointData *p2) {
	    return (p1->projection < p2->projection);
	});

	bpos = i = 0;
	while (bpos<bothspace.size ()) {
	    if (bothspace[bpos].curved || pcnt==0) {
		activespace[acnt++] = bothspace[bpos++];
	    } else {
		last = bothspace[bpos].start;
		startset = false; endset = false;

 		if (bothspace[bpos].scurved ||
		    stemIsActiveAt (stem, bothspace[bpos].start+0.0015)) {

		    activespace[acnt].scurved = bothspace[bpos].scurved;
		    activespace[acnt].start  = bothspace[bpos].start;
		    startset = true;
		}

		/* AMK/GWW: If the stem is preceded by a curved segment, then */
		/* skip the first point position and start from the next one. */
		/* (Otherwise StemIsActiveAt() may consider the stem is       */
		/* "inactive" at the fragment between the start of the active */
		/* space and the first point actually belonging to this stem) */
		if (bothspace[bpos].scurved) {
		    while (pcnt>i && pspace[i]->projection < bothspace[bpos].sbase) i++;

		    if (pcnt > i && pspace[i]->projection >= bothspace[bpos].sbase) {
			last = activespace[acnt].end = pspace[i]->projection;
			activespace[acnt].ecurved = false;
			activespace[acnt].curved = false;
			endset=true;
		    }
		}

		while (i<pcnt && (
		    (!bothspace[bpos].ecurved && pspace[i]->projection<bothspace[bpos].end ) ||
		    ( bothspace[bpos].ecurved && pspace[i]->projection<=bothspace[bpos].ebase ))) {
		    if (last==activespace[acnt].start && pspace[i]->projection >= last) {

			if (stemIsActiveAt (stem, last+((1.001*pspace[i]->projection-last)/2.001))) {
			    last = activespace[acnt].start = pspace[i]->projection;
			    activespace[acnt].scurved = false;
			    startset = true; endset = false;
			} else {
			    last = activespace[acnt].end = pspace[i]->projection;
			    activespace[acnt].ecurved = false;
			    activespace[acnt].curved = false;
			    endset = true;
			}
		    } else if ((last==activespace[acnt].end || !startset)
			&& pspace[i]->projection >= last) {

			if (stemIsActiveAt (stem, last+((1.001*pspace[i]->projection-last)/2.001)) ||
			    !startset) {

			    if (startset) acnt++;
			    last = activespace[acnt].start = pspace[i]->projection;
			    activespace[acnt].scurved = false;
			    startset = true; endset = false;
			} else {
			    last = activespace[acnt].end = pspace[i]->projection;
			    activespace[acnt].ecurved = false;
			    activespace[acnt].curved = false;
			    endset = true;
			}
		    }
		    ++i;
		}

		if ((bothspace[bpos].ecurved ||
		    stemIsActiveAt (stem, bothspace[bpos].end-0.0015)) &&
		    startset) {

		    activespace[acnt].end = bothspace[bpos].end;
		    activespace[acnt].ecurved = bothspace[bpos].ecurved;
		    activespace[acnt].curved = bothspace[bpos].curved;
		    endset = true;
		}
		++bpos;
		if (endset) ++acnt;
	    }
	}
    }

    for (size_t i=0; i<stem->chunks.size (); i++) {
	chunk = &stem->chunks[i];
	/* stemcheat 1 -- diagonal edge stem;
	 *           2 -- diagonal corner stem with a sharp top;
	 *           3 -- diagonal corner stem with a flat top;
	 *           4 -- bounding box hint */
	if (chunk->stemcheat==3 && chunk->l && chunk->r &&
		i+1<stem->chunks.size () && stem->chunks[i+1].stemcheat==3 &&
		(chunk->l==stem->chunks[i+1].l || chunk->r==stem->chunks[i+1].r)) {

	    ConicPoint *sp = chunk->l==stem->chunks[i+1].l ?
		chunk->l->sp : chunk->r->sp;
	    proj =  (sp->me.x - stem->left.x) *stem->unit.x +
		    (sp->me.y - stem->left.y) *stem->unit.y;

	    ConicPoint *sp2 = chunk->l==stem->chunks[i+1].l ?
		chunk->r->sp : chunk->l->sp;
	    ConicPoint *sp3 = chunk->l==stem->chunks[i+1].l ?
		stem->chunks[i+1].r->sp : stem->chunks[i+1].l->sp;
	    proj2 = (sp2->me.x - stem->left.x) *stem->unit.x +
		    (sp2->me.y - stem->left.y) *stem->unit.y;
	    proj3 = (sp3->me.x - stem->left.x) *stem->unit.x +
		    (sp3->me.y - stem->left.y) *stem->unit.y;

	    if (proj2>proj3) {
		ptemp = proj2; proj2 = proj3; proj3 = ptemp;
	    }

	    if ((proj3-proj2) < width) {
		activespace[acnt].curved = true;
		proj2 -= width/2;
		proj3 += width/2;
	    } else {
		activespace[acnt].curved = false;
	    }

	    activespace[acnt].start = proj2;
	    activespace[acnt].end = proj3;
	    activespace[acnt].sbase = activespace[acnt].ebase = proj;
	    acnt++;
	    ++i;
	/* AMK/GWW: The following is probably not needed. Bounding box hints don't */
	/* correspond to any actual glyph features, and their "active" zones  */
	/* usually look ugly when displayed. So we don't attempt to calculate */
	/* those faked "active" zones and instead just exclude bounding       */
	/* box hints from any validity checks based on the hint's "active"    */
	/* length */
	} else if (chunk->stemcheat==4 && chunk->l && chunk->r) {
#if 0
	    ConicPoint *sp = chunk->l->sp;
	    ConicPoint *sp2 = chunk->r->sp;
	    proj =  (sp->me.x - stem->left.x) *stem->unit.x +
		    (sp->me.y - stem->left.y) *stem->unit.y;
	    proj2 = (sp2->me.x - stem->left.x) *stem->unit.x +
		    (sp2->me.y - stem->left.y) *stem->unit.y;
	    activespace[acnt  ].curved = false;
	    if (proj2<proj) {
		activespace[acnt].start = proj2;
		activespace[acnt].end = proj;
	    } else {
		activespace[acnt].start = proj;
		activespace[acnt].end = proj2;
	    }
	    activespace[acnt].sbase = activespace[acnt].ebase = proj;
	    acnt++;
	    ++i;
#endif
	} else if (chunk->stemcheat && chunk->l && chunk->r) {
	    ConicPoint *sp = chunk->l->sp;
	    proj =  (sp->me.x - stem->left.x) * stem->unit.x +
		    (sp->me.y - stem->left.y) * stem->unit.y;
	    orig_proj = proj;
	    ConicPoint *other = chunk->lnext ? sp->next->to : sp->prev->from;
	    len  =  (other->me.x - sp->me.x) * stem->unit.x +
		    (other->me.y - sp->me.y) * stem->unit.y;
	    if (chunk->stemcheat == 2)
		proj -= width/2;
	    else if (len<0 )
		proj -= width;
	    activespace[acnt].curved = true;
	    activespace[acnt].start = proj;
	    activespace[acnt].end = proj+width;
	    activespace[acnt].sbase = activespace[acnt].ebase = orig_proj;
	    acnt++;
	}
    }

    if (acnt!=0) {
	acnt = merge_segments_final (activespace);
	// Shrink to the actual size
	activespace.resize (acnt);
    }

    len = clen = 0;
    for (i=0; i<acnt; ++i) {
	if (stem->active[i].curved)
	    clen += stem->active[i].end-stem->active[i].start;
	else
	    len += stem->active[i].end-stem->active[i].start;
    }
    stem->len = len; stem->clen = len+clen;
}

void GlyphData::stemsFixupIntersects () {
    int stemidx;

    for (auto &stem: m_stems) {
	for (auto &chunk: stem.chunks) {
	    if (chunk.l) {
		stemidx = stem.assignedToPoint (chunk.l, true);
		fixup_t (chunk.l,stemidx,true,chunk.l_e_idx);
		stemidx = stem.assignedToPoint (chunk.l, false);
		fixup_t (chunk.l,stemidx,false,chunk.l_e_idx);
	    }
	    if (chunk.r) {
		stemidx = stem.assignedToPoint (chunk.r, true);
		fixup_t (chunk.r,stemidx,true,chunk.r_e_idx);
		stemidx = stem.assignedToPoint (chunk.r, false);
		fixup_t (chunk.r,stemidx,false,chunk.r_e_idx);
	    }
	}
    }
}

/* AMK/GWW: Convert diagonal stems generated for stubs and intersections to horizontal */
/* or vertical, if they have just one chunk. This should be done before calculating */
/* active zones, as they are calculated against each stem's unit vector */
void GlyphData::normalizeStubs () {
    int hv;
    BasePoint newdir;

    for (auto &stem : m_stems) {
	if (stem.positioned)
	    continue;

	if (!Units::isHV (&stem.unit, true)) {
	    hv = Units::isHV (&stem.unit, false);
	    if (hv && stem.fitsHV (( hv == 1 ), 3)) {
		if (hv == 2 && stem.unit.y < 0)
		    swapStemEdges (&stem);

		newdir.x = fabs (rint (stem.unit.x));
		newdir.y = fabs (rint (stem.unit.y));
		stem.setUnit (newdir);

		for (size_t j=0; j<stem.chunks.size () && stem.leftidx == -1 && stem.rightidx == -1; j++) {
		    auto &chunk = stem.chunks[j];

		    if (stem.leftidx == -1 && chunk.l)
			stem.leftidx = getValidPointDataIndex (chunk.l->sp, &stem);
		    if (stem.rightidx == -1 && chunk.r)
			stem.rightidx = getValidPointDataIndex (chunk.r->sp, &stem);
		}
	    }
	}
    }
}

void GlyphData::findUnlikelyStems () {
    double width, minl, ratio;
    int ltick, rtick;
    PointData *lpd, *rpd;
    Conic *ls, *rs;
    ConicPoint *lsp, *rsp;
    BasePoint *lunit, *runit, *slunit, *srunit, *sunit;
    StemData *stem, *stem1, *tstem;
    std::vector<StemData *> *lstems, *rstems;
    struct stem_chunk *chunk;

    stemsFixupIntersects ();

    for (size_t i=0; i<m_stems.size (); i++) {
	stem = &m_stems[i];

	/* AMK/GWW: If stem had been already present in the spline char before we */
	/* started generating glyph data, then it should never be */
	/* considered "too big" */
	if (stem->positioned)
	    continue;

	/* If a stem has straight edges, and it is wider than tall */
	/*  then it is unlikely to be a real stem */
	width = stem->width;
	ratio = Units::isHV (&stem->unit, true) ? m_glyph.upm ()/( 6 * width ) : -0.25;
	stem->toobig =  (stem->clen + stem->clen * ratio < width);
    }

    /* AMK/GWW: One more check for curved stems. If a stem has just one active */
    /* segment, this segment is curved and the stem has no conflicts, */
    /* then select the active segment length which allows us to consider */
    /* this stem suitable for PS output by such a way, that stems connecting */
    /* the opposite sides of a circle are always accepted */
    for (size_t i=0; i<m_stems.size (); i++) if (m_stems[i].toobig) {
	stem = &m_stems[i];
	width = stem->width;

	if (Units::isHV (&stem->unit, true) && stem->active.size () == 1 &&
	    stem->active[0].curved && width/2 > dist_error_curve) {
	    size_t j;

	    for (j=0; j<m_stems.size (); j++) {
		stem1 = &m_stems[j];

		if (!stem1->toobig && stem->wouldConflict (stem1))
		    break;
	    }

	    if (j == m_stems.size ()) {
		minl = sqrt (pow (width/2, 2) - pow (width/2 - dist_error_curve, 2));
		if (stem->clen >= minl) stem->toobig = false;
	    }
	}
    }

    /* AMK/GWW: And finally a check for stubs and feature terminations. We don't */
    /* want such things to be controlled by any special hints, if there */
    /* is already a hint controlling the middle of the same feature */
    for (size_t i=0; i<m_stems.size (); i++) {
	stem = &m_stems[i];
	if (stem->positioned)
	    continue;

	if (stem->chunks.size () == 1 && stem->chunks[0].stub & 3) {
	    chunk = &stem->chunks[0];
	    slunit = chunk->lnext ? &chunk->l->nextunit : &chunk->l->prevunit;
	    srunit = chunk->rnext ? &chunk->r->nextunit : &chunk->r->prevunit;

	    /* This test is valid only for features which are not exactly horizontal/    */
	    /* vertical. But we can't check this using the stem unit, as it may have     */
	    /* already beeen reset to HV. So we use the units of this stem's base points */
	    /* instead. */
	    if (Units::isHV (slunit, true) && Units::isHV (srunit, true))
		continue;
	    if (Units::closerToHV (srunit, slunit) > 0) sunit = srunit;
	    else sunit = slunit;

	    lpd = chunk->l; lsp = lpd->sp; lstems = nullptr;
	    do {
		auto &tstems=
		    ((chunk->lnext && lpd == chunk->l) ||
		    (!chunk->lnext && lpd != chunk->l)) ? lpd->nextstems : lpd->prevstems;
		for (StemData *tstem: tstems) {
		    if (tstem != stem) {
			lstems = &tstems;
			break;
		    }
		}
		if (lstems)
		    break;
		ls = chunk->lnext ? lsp->next : lsp->prev;
		if (!ls)
		    break;
		lsp = (chunk->lnext) ? ls->to : ls->from;
		lpd = &m_points[lsp->ptindex];
		lunit = (chunk->lnext) ? &lpd->prevunit : &lpd->nextunit;
	    } while (lpd != chunk->l && lpd != chunk->r && Units::parallel (lunit, sunit, false));

	    rpd = chunk->r; rsp = rpd->sp; rstems = nullptr;
	    do {
		auto &tstems=
		    ((chunk->rnext && rpd == chunk->r) ||
		    (!chunk->rnext && rpd != chunk->r)) ? rpd->nextstems : rpd->prevstems;
		for (StemData *tstem : tstems) {
		    if (tstem != stem) {
			rstems = &tstems;
			break;
		    }
		}
		if (rstems)
		    break;
		rs = chunk->rnext ? rsp->next : rsp->prev;
		if (!rs)
	    break;
		rsp = chunk->rnext ? rs->to : rs->from;
		rpd = &m_points[rsp->ptindex];
		runit = chunk->rnext ? &rpd->prevunit : &rpd->nextunit;
	    } while (rpd != chunk->r && rpd != chunk->l && Units::parallel (runit, sunit, false));

	    if (lstems && rstems) {
		for (size_t j=0; j<lstems->size () && !stem->toobig; j++) {
		    for (size_t k=0; k<rstems->size () && !stem->toobig; k++) {
			if (lstems->at (j) == rstems->at (k) &&
			    Units::isHV (&lstems->at (j)->unit, true)) {
			    stem->toobig = true;
			}
		    }
		}
	    }
	}

	/* AMK/GWW: One more check for intersections between a curved segment and a */
	/* straight feature. Imagine a curve intersected by two bars, like in a Euro */
	/* glyph. Very probably we will get two chunks, one controlling the uppest   */
	/* two points of intersection, and another the lowest two, and most probably */
	/* these two chunks will get merged into a single stem (so this stem will    */
	/* even get an exactly vertical vector). Yet we don't need this stem because */
	/* there is already a stem controlling the middle of the curve (between two  */
	/* bars).*/
	else if (stem->chunks.size () == 2 &&
	    ((stem->chunks[0].stub & 7 && stem->chunks[1].stub & 6) ||
	     (stem->chunks[0].stub & 6 && stem->chunks[1].stub & 7))) {
	    size_t j;
	    for (j=0; j<m_stems.size (); ++j) {
		stem1 = &m_stems[j];
		if (!stem1->toobig && stem->wouldConflict (stem1))
		    break;
	    }

	    if (j < m_stems.size ())
		stem->toobig = true;
	}
    }

    for (size_t i=0; i<m_stems.size (); ++i) {
	stem = &m_stems[i];
	if (Units::isHV (&stem->unit, true))
	    continue;

	/* AMK/GWW: If a diagonal stem doesn't have at least 2 points assigned to   */
	/* each edge, then we probably can't instruct it. However we don't */
	/* disable stems which have just one point on each side, if those  */
	/* points are inflection points, as such stems may be useful for metafont */
	if (stem->lpcnt < 2 || stem->rpcnt < 2) {
	    lpd = rpd = nullptr;
	    for (size_t j=0; j<stem->chunks.size () && !lpd && !rpd; j++) {
		chunk = &stem->chunks[j];
		if (chunk->l) lpd = chunk->l;
		if (chunk->r) rpd = chunk->r;
	    }
	    if (!lpd || !rpd ||
		!isInflectionPoint (lpd) || !isInflectionPoint (rpd) || stem->clen < stem->width)
		stem->toobig = 2;
	} else if (stem->active.size () >= stem->chunks.size ())
	    stem->toobig = 2;
    }

    /* AMK/GWW: When using preexisting stem data, occasionally we can get two slightly      */
    /* different stems (one predefined, another recently detected) with nearly     */
    /* parallel vectors, sharing some points at both sides. Attempting to instruct */
    /* them both would lead to very odd effects. So we must disable one */
    for (size_t i=0; i<m_stems.size (); ++i) {
	stem = &m_stems[i];
	if (!stem->positioned || Units::isHV (&stem->unit, true))
	    continue;

	for (size_t j=0; j<m_stems.size (); ++j ) {
	    tstem = &m_stems[j];
	    if (tstem == stem || tstem->toobig || !Units::parallel (&stem->unit, &tstem->unit, false))
		continue;

	    ltick = false; rtick = false;
	    for (size_t k=0; k<stem->chunks.size () && (!ltick || !rtick); k++) {
		chunk =  &stem->chunks[k];

		if (chunk->l &&
		    stem->assignedToPoint (chunk->l, chunk->lnext) != -1 &&
		    tstem->assignedToPoint (chunk->l, chunk->lnext) != -1 )
		    ltick = true;
		if (chunk->r &&
		    stem->assignedToPoint (chunk->r, chunk->rnext) != -1 &&
		    tstem->assignedToPoint (chunk->r, chunk->rnext) != -1 )
		    rtick = true;
	    }
	    if (ltick && rtick) tstem->toobig = 2;
	}
    }
}

void GlyphData::findRefPointsExisting (StemData *stem) {
    double pos, lbase, rbase;
    bool is_x;
    PointData *pd;

    is_x = (bool) rint (stem->unit.y);
    lbase = is_x ? stem->left.x : stem->left.y;
    rbase = is_x ? stem->right.x : stem->right.y;

    for (auto &chunk: stem->chunks) {
	if (chunk.ltick) {
	    pd = chunk.l;
	    pos = is_x ? pd->sp->me.x : pd->sp->me.y;
	    if (pos == lbase) {
		pd->value++;
		if (pd->sp->ptindex < m_realcnt)
		    pd->value++;
		if (stem->pointOnDiag (pd, m_hv))
		    pd->value++;
	    }
	}

	if (chunk.rtick) {
	    pd = chunk.r;
	    pos = is_x ? pd->sp->me.x : pd->sp->me.y;
	    if (pos == rbase) {
		pd->value++;
		if (pd->sp->ptindex < m_realcnt)
		    pd->value++;
		if (stem->pointOnDiag (pd, m_hv))
		    pd->value++;
	    }
	}
    }
}

void GlyphData::findRefPointsNew (StemData *stem) {
    double pos, lpos, rpos, testpos;
    int lval, rval;
    bool is_x;
    PointData *lmost1, *lmost2, *rmost1, *rmost2;
    double llen, prevllen, rlen, prevrlen;
    ConicPoint *sp, *tsp;
    uint8_t *lextr, *rextr;

    is_x = (bool) rint (stem->unit.y);
    lpos = is_x ? stem->left.x : stem->left.y;
    rpos = is_x ? stem->right.x : stem->right.y;

    lmost1 = rmost1 = lmost2 = rmost2 = nullptr;
    llen = prevllen = rlen = prevrlen = 0;
    for (size_t i=0; i<stem->chunks.size (); i++) {
	struct stem_chunk *chunk = &stem->chunks[i];
	if (chunk->ltick) {
	    sp = chunk->l->sp;
	    pos = is_x ? sp->me.x : sp->me.y;
	    lval = 0;
	    for (size_t j=0; j<i; j++) if (stem->chunks[j].ltick) {
		tsp = stem->chunks[j].l->sp;
		testpos = is_x ? tsp->me.x : tsp->me.y;
		if (pos == testpos) {
		    lval = stem->chunks[j].l->value;
		    stem->chunks[j].l->value++;
		    /* An additional bonus for points which form together */
		    /* a longer stem segment */
		    if (sp->next->to == tsp || sp->prev->from == tsp) {
			llen = fabs ((sp->me.x - tsp->me.x)*stem->unit.x +
				    ( sp->me.y - tsp->me.y)*stem->unit.y );
			if (llen > prevllen) {
			    lmost1 = stem->chunks[j].l;
			    lmost2 = chunk->l;
			    prevllen = llen;
			}
		    }
		}
	    }
	    chunk->l->value = lval+1;

	    if (lval == 0 &&
		(stem->lmin - (pos - lpos) > -dist_error_hv ) &&
		(stem->lmax - (pos - lpos) < dist_error_hv ))
		chunk->l->value++;
	}

	if (chunk->rtick) {
	    sp = chunk->r->sp;
	    pos = is_x ? sp->me.x : sp->me.y;
	    rval = 0;
	    for (size_t j=0; j<i; j++) if (stem->chunks[j].rtick) {
		tsp = stem->chunks[j].r->sp;
		testpos = is_x ? tsp->me.x : tsp->me.y;
		if (pos == testpos) {
		    rval = stem->chunks[j].r->value;
		    stem->chunks[j].r->value++;
		    if (sp->next->to == tsp || sp->prev->from == tsp) {
			rlen = fabs ((sp->me.x - tsp->me.x)*stem->unit.x +
				    ( sp->me.y - tsp->me.y)*stem->unit.y );
			if (rlen > prevrlen) {
			    rmost1 = stem->chunks[j].r;
			    rmost2 = chunk->r;
			    prevrlen = rlen;
			}
		    }
		}
	    }
	    chunk->r->value = rval+1;

	    if (rval == 0 &&
		(stem->rmin - (pos - rpos) > -dist_error_hv) &&
		(stem->rmax - (pos - rpos) < dist_error_hv))
		chunk->r->value++;
	}
    }
    if (lmost1 && lmost2) {
	lmost1->value++; lmost2->value++;
    }
    if (rmost1 && rmost2) {
	rmost1->value++; rmost2->value++;
    }

    /* Extrema points get an additional value bonus. This should     */
    /* prevent us from preferring wrong points for stems controlling */
    /* curved segments */
    /* Third pass to assign bonuses to extrema points (especially    */
    /* to those extrema which are opposed to another extremum point) */
    for (auto &chunk: stem->chunks) {
	if (chunk.ltick) {
	    lextr = is_x ? &chunk.l->x_extr : &chunk.l->y_extr;
	    if (*lextr) chunk.l->value++;
	}
	if (chunk.rtick) {
	    rextr = is_x ? &chunk.r->x_extr : &chunk.r->y_extr;
	    if (*rextr) chunk.r->value++;
	}

	if (chunk.ltick && chunk.rtick) {
	    lextr = is_x ? &chunk.l->x_extr : &chunk.l->y_extr;
	    rextr = is_x ? &chunk.r->x_extr : &chunk.r->y_extr;
	    if (*lextr && *rextr) {
		chunk.l->value++;
		chunk.r->value++;
	    }
	}
    }
}

void GlyphData::normalizeStem (StemData *stem) {
    int lval, rval, val, lset, rset, best;
    double loff=0, roff=0;
    BasePoint lold, rold;
    ConicPoint *lbest, *rbest;

    /* AMK/GWW: First sort the stem chunks by their coordinates */
    if (Units::isHV (&stem->unit, true)) {
	std::sort (stem->chunks.begin (), stem->chunks.end (), chunk_cmp);

	/* AMK/GWW: For HV stems we have to check all chunks once more in order */
	/* to figure out "left" and "right" positions most typical */
	/* for this stem. We perform this by assigning a value to */
	/* left and right side of this chunk. */

	/* First pass to determine some point properties necessary */
	/* for subsequent operations */
	for (auto &chunk: stem->chunks) {
	    if (chunk.ltick)
		/* reset the point's "value" to zero */
		chunk.l->value = 0;
	    if (chunk.rtick)
		chunk.r->value = 0;
	}

	/* AMK/GWW: Second pass to check which positions relative to stem edges are */
	/* most common for this stem. Each position which repeats */
	/* more than once gets a plus 1 value bonus */
	if (stem->positioned) findRefPointsExisting (stem);
	else findRefPointsNew (stem);

	best = -1; val = 0;
	for (size_t i=0; i<stem->chunks.size (); i++) {
	    auto &chunk = stem->chunks[i];
	    lval = chunk.l ? chunk.l->value : 0;
	    rval = chunk.r ? chunk.r->value : 0;
	    if (((chunk.l && chunk.l->value > 0 &&
		getValidPointDataIndex (chunk.l->sp, stem) != -1) ||
		(stem->ghost && stem->width == 21 )) &&
		((chunk.r && chunk.r->value > 0 &&
		getValidPointDataIndex (chunk.r->sp,stem ) != -1 ) ||
		(stem->ghost && stem->width == 20 )) && lval + rval > val) {

		best = i;
		val = lval + rval;
	    }
	}
	if (best > -1) {
	    if (!stem->ghost || stem->width == 20) {
		lold = stem->left;
		lbest = stem->chunks[best].l->sp;
		stem->left = lbest->me;
		stem->leftidx = getValidPointDataIndex (lbest, stem);

		/* AMK/GWW: Now assign "left" and "right" properties of the stem */
		/* to point coordinates taken from the most "typical" chunk */
		/* of this stem. We also have to recalculate stem width and */
		/* left/right offset values */
		loff = (stem->left.x - lold.x) * stem->unit.y -
		       (stem->left.y - lold.y) * stem->unit.x;
		stem->lmin -= loff; stem->lmax -= loff;
	    }
	    if (!stem->ghost || stem->width == 21) {
		rold = stem->right;
		rbest = stem->chunks[best].r->sp;
		stem->right = rbest->me;
		stem->rightidx = getValidPointDataIndex (rbest, stem);
		roff = (stem->right.x - rold.x) * stem->unit.y -
		       (stem->right.y - rold.y) * stem->unit.x;
		stem->rmin -= roff; stem->rmax -= roff;
	    }
	    if (!stem->ghost)
		stem->width = (stem->right.x - stem->left.x) * stem->unit.y -
			      (stem->right.y - stem->left.y) * stem->unit.x;
	} else {
	    for (auto &chunk : stem->chunks) {
		if (chunk.l && (!stem->ghost || stem->width == 20)) {
		    stem->leftidx = getValidPointDataIndex (chunk.l->sp, stem);
		}
		if (chunk.r && (!stem->ghost || stem->width == 21)) {
		    stem->rightidx = getValidPointDataIndex (chunk.r->sp, stem);
		}
	    }
	}
    } else {
	bool found = false;
	std::sort (stem->chunks.begin (), stem->chunks.end (), chunk_cmp);
	lset = false; rset = false;
	/* AMK/GWW: Search for a pair of points whose vectors are really parallel. */
	/* This check is necessary because a diagonal stem can start from */
	/* a feature termination, and our checks for such terminations    */
	/* are more "liberal" than in other cases. However we don't want  */
	/* considering such a pair of points basic for this stem */
	for (auto &chunk: stem->chunks) {
	    BasePoint *lu, *ru;
	    if (chunk.l && chunk.r) {
		lu = chunk.lnext ? &chunk.l->nextunit : &chunk.l->prevunit;
		ru = chunk.rnext ? &chunk.r->nextunit : &chunk.r->prevunit;
		if (Units::parallel (lu, ru, true)) {
		    loff =  (chunk.l->sp->me.x - stem->left.x )*stem->l_to_r.x +
			    (chunk.l->sp->me.y - stem->left.y )*stem->l_to_r.y;
		    roff =  (chunk.r->sp->me.x - stem->right.x )*stem->l_to_r.x +
			    (chunk.r->sp->me.y - stem->right.y )*stem->l_to_r.y;
		    stem->left = chunk.l->sp->me;
		    stem->right = chunk.r->sp->me;
		    stem->recalcOffsets (&stem->unit, loff != 0, roff != 0);
		    found = true;
		    break;
		}
	    }
	}
	/* AMK/GWW: If the above check fails, just select the first point (relatively) */
	/* to the stem direction both at the left and the right edge */
	if (!found) for (auto &chunk: stem->chunks) {
	    if (!lset && chunk.l) {
		loff =  (chunk.l->sp->me.x - stem->left.x)*stem->l_to_r.x +
			(chunk.l->sp->me.y - stem->left.y)*stem->l_to_r.y;
		stem->left = chunk.l->sp->me;
		lset = true;
	    }
	    if (!rset && chunk.r) {
		roff =  (chunk.r->sp->me.x - stem->right.x )*stem->l_to_r.x +
			(chunk.r->sp->me.y - stem->right.y )*stem->l_to_r.y;
		stem->right = chunk.r->sp->me;
		rset = true;
	    }
	    if (lset && rset) {
		stem->recalcOffsets (&stem->unit, loff != 0, roff != 0);
		break;
	    }
	}
    }
}

void GlyphData::assignPointsToBBoxHint (DBounds *bounds, StemData *stem, bool is_v) {
    double min, max, test, left, right;
    double dist, prevdist;
    int closest;
    BasePoint dir;
    std::vector<ConicPoint *> lpoints, rpoints;
    PointData *pd1, *pd2;

    lpoints.reserve (m_points.size ());
    rpoints.reserve (m_points.size ());
    dir.x = !is_v; dir.y = is_v;
    for (auto &pd: m_points) if (pd.sp) {
	min = is_v ? bounds->minx : bounds->miny;
	max = is_v ? bounds->maxx : bounds->maxy;
	test = is_v ? pd.base.x : pd.base.y;
	if (test >= min && test < min + dist_error_hv && (
	    isCorrectSide (&pd, true, is_v,& dir) || isCorrectSide (&pd, false, is_v, &dir)))
	    lpoints.push_back (pd.sp);
	else if (test > max - dist_error_hv && test <= max && (
	    isCorrectSide (&pd, true, !is_v, &dir ) || isCorrectSide (&pd, false, !is_v, &dir)))
	    rpoints.push_back (pd.sp);
    }
    if (!lpoints.empty () && !rpoints.empty ()) {
	if (!stem) {
	    m_stems.emplace_back (&dir, &lpoints[0]->me, &rpoints[0]->me);
	    stem = &m_stems.back ();
	    stem->bbox = true;
	    stem->len = stem->width;
	    stem->leftidx = getValidPointDataIndex (lpoints[0], stem);
	    stem->rightidx = getValidPointDataIndex (rpoints[0], stem);
	}
	for (size_t i=0; i<lpoints.size (); i++) {
	    closest = -1;
	    dist = 1e4; prevdist = 1e4;
	    for (size_t j=0; j<rpoints.size (); j++) {
		left = is_v ? lpoints[i]->me.y : lpoints[i]->me.x;
		right = is_v ? rpoints[j]->me.y : rpoints[j]->me.x;
		dist = fabs (left - right);
		if (dist < prevdist) {
		    closest = j;
		    prevdist = dist;
		}
	    }
	    pd1 = &m_points[lpoints[i]->ptindex];
	    pd2 = &m_points[rpoints[closest]->ptindex];
	    addToStem (stem, pd1, pd2, false, true, 4);
	}
	std::sort (stem->chunks.begin (), stem->chunks.end (), chunk_cmp);
    }
}

void GlyphData::checkForBoundingBoxHints () {
    /* GWW: Adobe seems to add hints at the bounding boxes of glyphs with no hints */
    int hcnt=0, vcnt=0, hv;
    double cw, ch;
    StemData *hstem=NULL,*vstem=NULL;
    DBounds bounds = m_size;

    for (auto &stem: m_stems) {
	hv = Units::isHV (&stem.unit, true);
	if (!hv)
	    continue;
	if (stem.toobig) {
	    if (stem.left.x == bounds.minx && stem.right.x == bounds.maxx)
		vstem = &stem;
	    else if (stem.right.y == bounds.miny && stem.left.y == bounds.maxy)
		hstem = &stem;
	    continue;
	}
	if (hv == 1) {
	    if (stem.bbox) hstem = &stem;
	    else ++hcnt;
	} else if (hv == 2) {
	    if (stem.bbox) vstem = &stem;
	    else ++vcnt;
	}
    }
    if (hcnt!=0 && vcnt!=0 &&
	(!hstem || !hstem->positioned ) && (!vstem || !vstem->positioned ))
	return;

    ch = bounds.maxy - bounds.miny;
    cw = bounds.maxx - bounds.minx;

    if (ch > 0 && ((hstem && hstem->positioned) || (hcnt == 0 && ch < m_glyph.upm ()/3))) {
	if (hstem && hstem->toobig) hstem->toobig = false;
	assignPointsToBBoxHint (&bounds, hstem, false);
	if (hstem) normalizeStem (hstem);
    }
    if (cw > 0 && ((vstem && vstem->positioned) || (vcnt == 0 && cw < m_glyph.upm ()/3))) {
	if (vstem && vstem->toobig) vstem->toobig = false;
	assignPointsToBBoxHint (&bounds, vstem, true);
	if (vstem) normalizeStem (vstem);
    }
}

StemData *GlyphData::findOrMakeGhostStem (ConicPoint *sp, StemInfo *blue, double width, bool use_existing) {
    int hasl, hasr;
    StemData *stem=nullptr;
    struct stem_chunk *chunk;
    BasePoint dir,left,right;
    double min, max;

    dir.x = 1; dir.y = 0;
    for (auto &tstem: m_stems) {
	if (tstem.blue == blue && tstem.ghost && tstem.width == width) {
	    stem = &tstem;
	    break;
	/* AMK/GWW: If the stem controlling this blue zone is not for a ghost hint,    */
	/* then we check if it has both left and right points, to ensure that */
	/* we don't occasionally assign an additional point to a stem which   */
	/* has already been rejected in favor of another stem */
	} else if (tstem.blue == blue && !tstem.ghost && !tstem.toobig) {
	    min = (width == 20) ? tstem.left.y - tstem.lmin - 2*dist_error_hv :
				  tstem.right.y - tstem.rmin - 2*dist_error_hv;
	    max = (width == 20) ? tstem.left.y - tstem.lmax + 2*dist_error_hv :
				  tstem.right.y - tstem.rmax + 2*dist_error_hv;

	    if (sp->me.y <= min || sp->me.y >= max)
		continue;

	    hasl = false; hasr = false;
	    for (size_t j=0; j < tstem.chunks.size () && (!hasl || !hasr); j++) {
		chunk = &tstem.chunks[j];
		if (chunk->l && !chunk->lpotential)
		    hasl = true;
		if (chunk->r && !chunk->rpotential)
		    hasr = true;
	    }
	    if (hasl && hasr) {
		stem = &tstem;
		break;
	    }
	}
    }

    if (!stem && !use_existing) {
	left.x = right.x = sp->me.x;
	left.y =  (width == 21) ? sp->me.y + 21 : sp->me.y;
	right.y = (width == 21) ? sp->me.y : sp->me.y - 20;

	m_stems.emplace_back (&dir, &left, &right);
	stem = &m_stems.back ();
	stem->ghost = true;
	stem->width = width;
	stem->blue = blue;
    }
    return stem;
}

void GlyphData::figureGhostActive (StemData *stem) {
    double len = 0;
    std::vector<struct segment> &activespace = stem->active;
    PointData *valid;

    if (!stem->ghost)
	return;
    activespace.reserve (stem->chunks.size ());

    for (auto &chunk: stem->chunks) {
	valid = chunk.l ? chunk.l : chunk.r;
	add_ghost_segment (valid, stem->left.x, activespace);
    }
    std::sort (activespace.begin (), activespace.end (), [](struct segment &s1, struct segment &s2) {
        return (s1.start < s2.start);
    });
    merge_segments (activespace);

    for (auto &seg: stem->active) {
	len += (seg.end - seg.start);
    }
    stem->clen = stem->len = len;
}

void GlyphData::checkForGhostHints (bool use_existing) {
    /* GWW: PostScript doesn't allow a hint to stretch from one alignment zone to */
    /*  another. (Alignment zones are the things in bluevalues).  */
    /* Oops, I got this wrong. PS doesn't allow a hint to start in a bottom */
    /*  zone and stretch to a top zone. Everything in OtherBlues is a bottom */
    /*  zone. The baseline entry in BlueValues is also a bottom zone. Every- */
    /*  thing else in BlueValues is a top-zone. */
    /* This means */
    /*  that we can't define a horizontal stem hint which stretches from */
    /*  the baseline to the top of a capital I, or the x-height of lower i */
    /*  If we find any such hints we must remove them, and replace them with */
    /*  ghost hints. The bottom hint has height -21, and the top -20 */
    StemData *stem;
    double base;
    StemInfo *leftfound = nullptr, *rightfound = nullptr;
    bool has_h;
    int peak, fuzz;

    fuzz = m_fuzz;

    /* GWW: look for any stems stretching from one zone to another and remove them */
    /*  (I used to turn them into ghost hints here, but that didn't work (for */
    /*  example on "E" where we don't need any ghosts from the big stem because*/
    /*  the narrow stems provide the hints that PS needs */
    /* However, there are counter-examples. in Garamond-Pro the "T" character */
    /*  has a horizontal stem at the top which stretches between two adjacent */
    /*  bluezones. Removing it is wrong. Um... Thanks Adobe */
    /* I misunderstood. Both of these were top-zones */
    for (auto &tstem: m_stems) {
	if (Units::isHV (&tstem.unit, true) != 1)
	    continue;

	leftfound = rightfound = nullptr;
	for (auto &blue: m_blues) {
	    if (tstem.left.y>=blue.start-fuzz && tstem.left.y<=blue.start+blue.width+fuzz)
		leftfound = &blue;
	    else if (tstem.right.y>=blue.start-fuzz && tstem.right.y<=blue.start+blue.width+fuzz)
		rightfound = &blue;
	}
	/* Assign value 2 to indicate this stem should be ignored also for TTF instrs */
	if (leftfound && rightfound && (tstem.left.y > 0 && tstem.right.y <= 0))
	    tstem.toobig = 2;
	/* Otherwise mark the stem as controlling a specific blue zone */
	else if (leftfound && (!rightfound || tstem.left.y > 0))
	    tstem.blue = leftfound;
	else if (rightfound && (!leftfound || tstem.right.y <= 0))
	    tstem.blue = rightfound;
    }

    /* GWW: Now look and see if we can find any edges which lie in */
    /*  these zones.  Edges which are not currently in hints */
    /* Use the winding number to determine top or bottom */
    for (auto &pd: m_points) if (pd.sp) {
	has_h = false;
	for (size_t j=0; j<pd.prevstems.size (); j++ ) {
	    stem = pd.prevstems[j];
	    if (!stem->toobig && Units::isHV (&stem->unit, true) == 1) {
		has_h = true;
		break;
	    }
	}
	for (size_t j=0; j<pd.nextstems.size (); j++) {
	    stem = pd.nextstems[j];
	    if (!stem->toobig && Units::isHV (&stem->unit, true) == 1) {
		has_h = true;
		break;
	    }
	}
	if (has_h)
	    continue;

	base = pd.sp->me.y;
	for (auto &blue: m_blues) {
	    if (base>=blue.start-fuzz && base<=blue.start+blue.width+fuzz) {
		// NB: is this the only place where the opposite direction of splines matters?
		peak = isSplinePeak (&pd, !order2 (), false, 7);
		if (peak < 0) {
		    stem = findOrMakeGhostStem (pd.sp, &blue, 20, use_existing);
		    if (stem)
			addToStem (stem, &pd, nullptr, 2, false, false);
		} else if (peak > 0) {
		    stem = findOrMakeGhostStem (pd.sp, &blue, 21, use_existing);
		    if (stem)
			addToStem (stem, nullptr, &pd, 2, false, false);
		}
	    }
	}
    }

    for (auto &tstem : m_stems) {
	if (!tstem.ghost)
	    continue;
	normalizeStem (&tstem);
	figureGhostActive (&tstem);
    }
}

void GlyphData::markDStemCorner (PointData *pd) {
    int x_dir = pd->x_corner, peak, hv;
    bool is_l, has_stem = false;
    StemData *stem;
    BasePoint left,right,unit;
    size_t i;

    for (i=0; i<pd->prevstems.size () && !has_stem; i++) {
	stem = pd->prevstems[i];
	if (!stem->toobig && (
	    (x_dir && (hv = Units::isHV (&stem->unit, true) == 1)) ||
	    (!x_dir && hv == 2)))
	    has_stem = true;
    }
    for (i=0; i<pd->nextstems.size () && !has_stem; i++) {
	stem = pd->nextstems[i];
	if (!stem->toobig && (
	    (x_dir && (hv = Units::isHV (&stem->unit, true) == 1)) ||
	    (!x_dir && hv == 2 )))
	    has_stem = true;
    }
    if (has_stem)
	return;

    peak = isSplinePeak (pd, x_dir, x_dir, 2);
    unit.x = !x_dir; unit.y = x_dir;

    if (peak > 0) {
	left.x = x_dir ? pd->sp->me.x + 21 : pd->sp->me.x;
	right.x = x_dir ? pd->sp->me.x : pd->sp->me.x;
	left.y = x_dir ? pd->sp->me.y : pd->sp->me.y;
	right.y = x_dir ? pd->sp->me.y : pd->sp->me.y - 20;

    } else if (peak < 0) {
	left.x = x_dir ? pd->sp->me.x : pd->sp->me.x;
	right.x = x_dir ? pd->sp->me.x - 20 : pd->sp->me.x;
	left.y = x_dir ? pd->sp->me.y : pd->sp->me.y + 21;
	right.y = x_dir ? pd->sp->me.y : pd->sp->me.y;
    }
    is_l = isCorrectSide (pd, true, true, &unit);
    for (i=0; m_stems.size (); i++) {
	stem = &m_stems[i];
	if (!stem->toobig && Units::parallel (&unit, &stem->unit, true) &&
	    stem->onStem (&pd->sp->me, is_l))
	    break;
    }
    if (i == m_stems.size ()) {
	m_stems.emplace_back (&unit, &left, &right);
	stem = &m_stems.back ();
	stem->ghost = 2;
    }
    addToStem (stem, pd, nullptr, 2, false, false);
}

void GlyphData::markDStemCorners () {
    struct stem_chunk *schunk, *echunk;

    for (auto &stem: m_stems) {
	if (stem.toobig || Units::isHV (&stem.unit, true))
	    continue;

	schunk = &stem.chunks[0];
	echunk = &stem.chunks[stem.chunks.size () - 1];

	if (schunk->l && schunk->r &&
	    fabs (schunk->l->base.x - schunk->r->base.x) > dist_error_hv &&
            fabs (schunk->l->base.y - schunk->r->base.y) > dist_error_hv && (
	    (schunk->l->x_corner == 1 && schunk->r->y_corner == 1) ||
	    (schunk->l->y_corner == 1 && schunk->r->x_corner == 1))) {
	    markDStemCorner (schunk->l);
	    markDStemCorner (schunk->r);
	}
	if (echunk->l && echunk->r &&
	    fabs (echunk->l->base.x - echunk->r->base.x) > dist_error_hv &&
            fabs (echunk->l->base.y - echunk->r->base.y) > dist_error_hv && (
	    (echunk->l->x_corner == 1 && echunk->r->y_corner == 1) ||
	    (echunk->l->y_corner == 1 && echunk->r->x_corner == 1))) {
	    markDStemCorner (echunk->l);
	    markDStemCorner (echunk->r);
	}
    }
}

void GlyphData::bundleStems (int maxtoobig) {
    int hv;
    bool hasl, hasr;
    PointData *lpd, *rpd;
    double dmove;

    /* AMK/GWW: Some checks for undesired stems which we couldn't do earlier  */

    /* First filter out HV stems which have only "potential" points  */
    /* on their left or right edge. Such stems aren't supposed to be */
    /* used for PS hinting, so we mark them as "too big" */
    for (auto &stem: m_stems) {
	hasl = false; hasr = false;

	if (Units::isHV (&stem.unit,true) &&
	    !stem.toobig && !stem.ghost && !stem.positioned) {
	    for (size_t j=0; j<stem.chunks.size () && (!hasl || !hasr); ++j) {
		if (stem.chunks[j].l && !stem.chunks[j].lpotential)
		    hasl = true;
		if (stem.chunks[j].r && !stem.chunks[j].rpotential )
		    hasr = true;
	    }
	    if (!hasl || !hasr)
		stem.toobig = true;
	}
    }

    /* Filter out HV stems which have both their edges controlled by */
    /* other, narrower HV stems */
    for (auto &stem: m_stems) {
	if (Units::isHV (&stem.unit, true)) {
	    hasl = hasr = false;
	    for (auto &chunk: stem.chunks) {
		lpd = chunk.l;
		rpd = chunk.r;
		if (lpd) {
		    auto &tstems = chunk.lnext ? lpd->nextstems : lpd->prevstems;
		    for (StemData *tstem: tstems) {
			/* AMK/GWW: Used to test tstem->toobig <= stem->toobig, but got into troubles with*/
			/* a weird terminal stem preventing a ball terminal from being properly detected, */
			/* because both the stems initially have toobig == 1 */
			/* See the "f" from Heuristica-Italic */
			if (tstem != &stem &&
			    !tstem->toobig && tstem->positioned >= stem.positioned &&
			    Units::parallel (&stem.unit, &tstem->unit, true) && tstem->width < stem.width) {
			    hasl = true;
			    break;
			}
		    }
		}
		if (rpd) {
		    auto &tstems = chunk.rnext ? rpd->nextstems : rpd->prevstems;
		    for (StemData *tstem: tstems) {
			if (tstem != &stem &&
			    !tstem->toobig && tstem->positioned >= stem.positioned &&
			    Units::parallel (&stem.unit, &tstem->unit, true) && tstem->width < stem.width) {
			    hasr = true;
			    break;
			}
		    }
		}
		if (hasl && hasr) {
		    stem.toobig = 2;
		    break;
		}
	    }
	}
    }

    hbundle.stemlist.reserve (m_stems.size ());
    hbundle.unit.x = 1; hbundle.unit.y = 0;
    hbundle.l_to_r.x = 0; hbundle.l_to_r.y = -1;

    vbundle.stemlist.reserve (m_stems.size ());
    vbundle.unit.x = 0; vbundle.unit.y = 1;
    vbundle.l_to_r.x = 1; vbundle.l_to_r.y = 0;

    if (m_hasSlant && !m_hv) {
	ibundle.stemlist.reserve (m_stems.size ());
	ibundle.unit.x = m_slantUnit.x;
	ibundle.unit.y = m_slantUnit.y;
	ibundle.l_to_r.x = -ibundle.unit.y;
	ibundle.l_to_r.y = ibundle.unit.x;
    }

    for (auto &stem: m_stems) {
	if (stem.toobig > maxtoobig)
	    continue;
	hv = Units::isHV (&stem.unit, true);

	if (hv == 1) {
	    hbundle.stemlist.push_back (&stem);
	    stem.bundle = &hbundle;
	} else if (hv == 2) {
	    vbundle.stemlist.push_back (&stem);
	    stem.bundle = &vbundle;
	} else if (m_hasSlant && !m_hv &&
	    realNear (stem.unit.x, m_slantUnit.x) &&
	    realNear (stem.unit.y, m_slantUnit.y)) {

	    /* Move base point coordinates to the baseline to simplify */
	    /* stem ordering and positioning relatively to each other  */
	    stem.left.x -= ((stem.left.y - m_size.miny) * stem.unit.x)/stem.unit.y;
	    stem.right.x -= ((stem.right.y - m_size.miny) * stem.unit.x)/stem.unit.y;
	    dmove = (stem.left.y - m_size.miny)/stem.unit.y;
	    stem.left.y = stem.right.y = m_size.miny;
	    for (auto &act: stem.active) {
		act.start += dmove;
		act.end += dmove;
	    }

	    ibundle.stemlist.push_back (&stem);
	    stem.bundle = &ibundle;
	    stem.italic = true;
	}
    }

    auto stemptr_cmp = [](StemData *s1, StemData *s2) {
	double start1, end1, start2, end2;

	if (fabs (s1->unit.x) > fabs(s2->unit.y)) {
	    start1 = s1->right.y; end1 = s1->left.y;
	    start2 = s2->right.y; end2 = s2->left.y;
	} else {
	    start1 = s1->left.x; end1 = s1->right.x;
	    start2 = s2->left.x; end2 = s2->right.x;
	}

	if (!realNear (start1, start2))
	    return (start1 < start2);
	else if (!realNear (end1, end2))
	    return (end1 < end2);
	return false;
    };

    std::sort (hbundle.stemlist.begin (), hbundle.stemlist.end (), stemptr_cmp);
    std::sort (vbundle.stemlist.begin (), vbundle.stemlist.end (), stemptr_cmp);
    if (m_hasSlant && !m_hv)
	std::sort (ibundle.stemlist.begin (), ibundle.stemlist.end (), stemptr_cmp);

    int cur_idx = 0;
    for (StemData *stemptr: hbundle.stemlist) {
	stemptr->stem_idx = cur_idx++;
	stemptr->lookForMasterHVStem ();
	stemptr->clearUnneededDeps ();
    }
    for (StemData *stemptr: vbundle.stemlist) {
	stemptr->stem_idx = cur_idx++;
	stemptr->lookForMasterHVStem ();
	stemptr->clearUnneededDeps ();
    }
}

void GlyphData::addSerifOrBall (StemData *master, StemData *slave, bool lbase, bool is_ball) {
    struct dependent_serif *tserif;
    PointData *spd;
    double width, min, max;
    int next;
    size_t i;

    if (lbase) {
	width = fabs(
		(slave->right.x - master->left.x) * master->unit.y -
		(slave->right.y - master->left.y) * master->unit.x);
	max = width + slave->rmin + 2*dist_error_hv;
	min = width + slave->rmax - 2*dist_error_hv;
    } else {
	width = fabs(
		(master->right.x - slave->left.x) * master->unit.y -
		(master->right.y - slave->left.y) * master->unit.x);
	max = width - slave->lmax + 2*dist_error_hv;
	min = width - slave->lmin - 2*dist_error_hv;
    }

    for (i=0; i<master->serifs.size (); i++) {
	tserif = &master->serifs[i];
	if (tserif->stem == slave && tserif->lbase == lbase)
	    break;
	else if (tserif->width > min && tserif->width < max && tserif->lbase == lbase) {
	    for (size_t j=0; j<slave->chunks.size (); j++) {
		spd = lbase ? slave->chunks[j].r : slave->chunks[j].l;
		next = lbase ? slave->chunks[j].rnext : slave->chunks[j].lnext;
		if (spd && tserif->stem->assignedToPoint (spd, next) == -1 )
		    addToStem (tserif->stem, spd, nullptr, next, false, false);
	    }
	    break;
	}
    }
    if (i<master->serifs.size ())
	return;

    struct dependent_serif serif {slave, width, lbase, is_ball};
    master->serifs.push_back (serif);

    /* Mark the dependent stem as related with a bundle, although it */
    /* is not listed in that bundle itself */
    slave->bundle = master->bundle;
}

bool GlyphData::isBall (PointData *pd, StemData *master, bool lbase) {
    double max, min, dot, coord;
    BasePoint *lbp, *rbp, *dir;
    Conic *test;
    PointData *nbase, *pbase, *tpd;
    bool is_x, peak_passed;

    if (!pd || (pd->x_extr != 1 && pd->y_extr != 1))
	return false;

    is_x = (Units::isHV (&master->unit, true) == 1);
    lbp = lbase ? &master->left : &pd->base;
    rbp = lbase ? &pd->base : &master->right;
    min = is_x ? rbp->y : lbp->x;
    max = is_x ? lbp->y : rbp->x;

    peak_passed = false;
    nbase = pbase = nullptr;
    test = pd->sp->next;
    dir = &pd->nextunit;

    if (test) do {
	tpd = &m_points[test->to->ptindex];
	if (master->assignedToPoint (tpd, true) != -1 ) {
	    nbase = tpd;
	    break;
	}
	coord = is_x ? tpd->base.y : tpd->base.x;
	dot = tpd->nextunit.x * dir->x + tpd->nextunit.y * dir->y;
	if (dot == 0 && !peak_passed) {
	    dir = &tpd->nextunit;
	    dot = 1.0;
	    peak_passed = true;
	}
	test = test->to->next;
    } while (test && test != pd->sp->next && dot > 0 && coord >= min && coord <= max);

    peak_passed = false;
    test = pd->sp->prev;
    dir = &pd->prevunit;
    if (test) do {
	tpd = &m_points[test->from->ptindex];
	if (master->assignedToPoint (tpd ,false) != -1) {
	    pbase = tpd;
	    break;
	}
	coord = is_x ? tpd->base.y : tpd->base.x;
	dot = tpd->prevunit.x * dir->x + tpd->prevunit.y * dir->y;
	if (dot == 0 && !peak_passed) {
	    dir = &tpd->prevunit;
	    dot = 1.0;
	    peak_passed = true;
	}
	test = test->from->prev;
    } while (test && test != pd->sp->prev && dot > 0 && coord >= min && coord <= max);

    if (nbase && pbase) {
	for (auto &chunk: master->chunks) {
	    if ((chunk.l == nbase && chunk.r == pbase ) ||
		(chunk.l == pbase && chunk.r == nbase ))
		return true;
	}
    }
    return false;
}

void GlyphData::getSerifData (StemData *stem) {
    int snext, enext, eidx;
    bool allow_s, allow_e, s_ball, e_ball;
    struct stem_chunk *chunk;
    StemData *smaster=nullptr, *emaster=nullptr;
    PointData *spd, *epd;
    double start, end, tstart, tend, smend, emstart;

    bool is_x = (Units::isHV (&stem->unit, true) == 1);
    auto &bundle = is_x ? hbundle : vbundle;
    start =  is_x ? stem->right.y : stem->left.x;
    end =    is_x ? stem->left.y : stem->right.x;

    allow_s = allow_e = true;
    s_ball = e_ball = 0;
    for (size_t i=0; i<stem->chunks.size () && (allow_s == true || allow_e == true); i++) {
	chunk = &stem->chunks[i];
	spd = is_x ? chunk->r : chunk->l;
	snext = is_x ? chunk->rnext : chunk->lnext;
	epd = is_x ? chunk->l : chunk->r;
	enext = is_x ? chunk->lnext : chunk->rnext;

	if (spd && allow_e) {
	    auto &stems = snext ? spd->nextstems : spd->prevstems;
	    for (StemData *tstem: stems) {
		if (realNear (tstem->unit.x, stem->unit.x) &&
		    realNear (tstem->unit.y, stem->unit.y) && !tstem->toobig) {
		    chunk->is_ball = e_ball = isBall (epd, tstem, !is_x);
		    if (e_ball) {
			chunk->ball_m = tstem;
			emaster = tstem;
			emstart = is_x ? tstem->right.y : tstem->left.x;
		    }
		    allow_s = false;
		}
	    }

	}
	if (epd && allow_s) {
	    auto &stems = snext ? epd->nextstems : spd->prevstems;
	    for (StemData *tstem: stems) {
		if (realNear (tstem->unit.x, stem->unit.x) &&
		    realNear (tstem->unit.y, stem->unit.y) && !tstem->toobig) {
		    chunk->is_ball = s_ball = isBall (spd, tstem, is_x);
		    if (s_ball) {
			chunk->ball_m = tstem;
			smaster = tstem;
			smend = is_x ? tstem->left.y : tstem->right.x;
		    }
		    allow_e = false;
		}
	    }

	}
    }

    for (size_t i=0; i<bundle.stemlist.size (); i++ ) {
	StemData *tstem = bundle.stemlist[i];
	if (tstem->unit.x != stem->unit.x || tstem->unit.y != stem->unit.y ||
	    tstem->toobig || tstem->width >= stem->width)
	    continue;

	tstart = is_x ? tstem->right.y : tstem->left.x;
	tend = is_x ? tstem->left.y : tstem->right.x;

	if (tstart >= start && tend <= end) {
	    if (allow_s && tstart > start) {
		for (size_t j=0; j<tstem->chunks.size () && smaster != tstem; j++) {
		    if (is_x) {
			spd = tstem->chunks[j].l;
			snext = tstem->chunks[j].lnext;
			eidx = tstem->chunks[j].l_e_idx;
		    } else {
			spd = tstem->chunks[j].r;
			snext = tstem->chunks[j].rnext;
			eidx = tstem->chunks[j].r_e_idx;
		    }
		    if (spd && connectsAcrossToStem (spd, snext, stem, is_x, eidx) &&
			(smaster || smend - start > tend - start)) {
			smaster = tstem;
			smend = tend;
		    }
		}
	    }
	    if ( allow_e && tend < end ) {
		for (size_t j=0; j<tstem->chunks.size () && emaster != tstem; j++) {
		    if (is_x) {
			epd = tstem->chunks[j].r;
			enext = tstem->chunks[j].rnext;
			eidx = tstem->chunks[j].r_e_idx;
		    } else {
			epd = tstem->chunks[j].l;
			enext = tstem->chunks[j].lnext;
			eidx = tstem->chunks[j].l_e_idx;
		    }
		    if (epd && connectsAcrossToStem (epd, enext, stem, !is_x, eidx) &&
			(emaster || end - emstart > end - tstart)) {
			emaster = tstem;
			emstart = tstart;
		    }
		}
	    }
	}
    }
    if (smaster)
	addSerifOrBall (smaster, stem,  is_x, s_ball);
    if (emaster)
	addSerifOrBall (emaster, stem, !is_x, e_ball);
}

void GlyphData::findCounterGroups (bool is_v) {
    struct stembundle &bundle = is_v ? vbundle : hbundle;
    StemData *curm, *prevm, *cur, *prev;
    double mdist, dist;

    prevm = nullptr;
    for (size_t i=0; i<bundle.stemlist.size (); i++) {
	curm = prev = bundle.stemlist[i];
	if (curm->master)
	    continue;
	if (!prevm || curm->prev_c_m) {
	    prevm = curm;
	    continue;
	}
	mdist = is_v ? curm->left.x - prevm->right.x : curm->right.y - prevm->left.y;
	for (size_t j=i+1; j<bundle.stemlist.size (); j++) {
	    cur = bundle.stemlist[j];
	    if (cur->master)
		continue;
	    if (cur->prev_c_m) {
		prev = cur;
		continue;
	    }

	    dist =  is_v ? cur->left.x - prev->right.x : cur->right.y - prev->left.y;
	    if (mdist > dist - dist_error_hv && mdist < dist + dist_error_hv &&
		stem_pairs_similar (prevm, curm, prev, cur)) {
		if (!prevm->next_c_m) {
		    prevm->next_c_m = curm;
		    curm->prev_c_m = prevm;
		}
		prev->next_c_m = cur;
		cur->prev_c_m = prev;
	    }
	    prev = cur;
	}
	prevm = curm;
    }
}

void GlyphData::stemInfoToStemData (StemInfo *si, DBounds *bounds, int is_v) {
    StemData *stem;
    BasePoint dir, left, right;
    double sstart, send;

    dir.x = !is_v; dir.y = is_v;

    // NB: ghost hints are handled in the StemData constructor
    sstart = si->start;
    send   = si->start + si->width;
    left.x =  (is_v) ? sstart : 0;
    left.y =  (is_v) ? 0 : send;
    right.x = (is_v) ? send : 0;
    right.y = (is_v) ? 0 : sstart;

    m_stems.emplace_back (&dir, &left, &right);
    stem = &m_stems.back ();
    stem->ghost = (si->width < 0);
    if (( is_v &&
    	left.x >= bounds->minx && left.x < bounds->minx + dist_error_hv &&
    	right.x > bounds->maxx - dist_error_hv && right.x <= bounds->maxx) ||
        (!is_v &&
    	right.y >= bounds->miny && right.y < bounds->miny + dist_error_hv &&
    	left.y > bounds->maxy - dist_error_hv && left.y <= bounds->maxy))
        stem->bbox = true;
    stem->positioned = true;
}

/* Normally we use the DetectDiagonalStems flag (set via the Preferences dialog) to determine */
/* if diagonal stems should be generated. However, sometimes it makes sense to reduce the */
/* processing time, deliberately turning the diagonal stem detection off: in particular we */
/* don't need any diagonal stems if we only want to assign points to some preexisting HV */
/* hints. For this reason  the only_hv argument still can be passed to this function. */
GlyphData::GlyphData (sFont *fnt, ConicGlyph &g, bool only_hv, bool use_existing) :
    m_font (fnt), m_glyph (g), m_hv (only_hv) {
    // Nothing to do
    if (m_glyph.figures.empty () || m_glyph.outlinesType () == OutlinesType::SVG)
	return;

    m_fig = &m_glyph.figures.front ();
    m_fuzz = getBlueFuzz (m_glyph.privateDict ());
    figureBlues (m_glyph.privateDict ());

    dist_error_hv    = .0035*m_glyph.upm ();
    dist_error_diag  = .0065*m_glyph.upm ();
    dist_error_curve = .022*m_glyph.upm ();

    if (m_font && m_font->italicAngle ()) {
	double iangle = (90 + m_font->italicAngle ());
	m_hasSlant = true;
	m_slantUnit.x = cos (iangle * (PI/180));
	m_slantUnit.y = sin (iangle * (PI/180));
    }

    /* SSToMContours can clean up the splinesets (remove 0 length splines) */
    /*  so it must be called BEFORE everything else (even though logically */
    /*  that doesn't make much sense). Otherwise we might have a pointer */
    /*  to something since freed */
    m_fig->toMContours (m_ms, OverlapType::Exclude);

    m_realcnt = m_fig->renumberPoints ();

    /* GWW: Create temporary point numbers for the implied points. We need this */
    /*  for metafont if nothing else */
    int pcnt = m_realcnt;
    for (auto &ss: m_fig->contours) {
	for (ConicPoint *sp=ss.first; ;) {
	    if (sp->ttfindex < m_realcnt)
		sp->ptindex = sp->ttfindex;
	    else if (sp->ttfindex == 0xffff)
		sp->ptindex = pcnt++;
	    if (!sp->next)
		break;
	    sp = sp->next->to;
	    if (sp==ss.first)
		break;
	}
    }

    m_points.resize (pcnt);
    for (auto &ss: m_fig->contours) {
	for (ConicPoint *sp=ss.first; ;) {
	    m_points[sp->ptindex].init (*this, sp, &ss);
	    if (!sp->next)
		break;
	    sp = sp->next->to;
	    if (sp==ss.first)
		break;
	}
    }

    // get number of splines
    int scnt = 0;
    for (auto &ss: m_fig->contours) {
	if (ss.first->prev) {
	    Conic *s = ss.first->next;
	    do {
		scnt++;
		s = s->to->next;
	    } while (s && s!=ss.first->next);
	}
    }
    m_sspace.reserve (scnt);

    // Generate list of splines
    for (auto &ss: m_fig->contours) {
	if (ss.first->prev) {
	    Conic *s = ss.first->next;
	    do {
		m_sspace.push_back (s);
		s = s->to->next;
	    } while (s && s!=ss.first->next);
	}
    }
    m_size = g.bb;

    for (auto &pd: m_points) if (pd.sp) {
	if (!pd.nextzero)
	    pd.next_e_cnt = findMatchingEdge (&pd, 1, pd.nextedges);
	if (!pd.prevzero)
	    pd.prev_e_cnt = findMatchingEdge (&pd, 0, pd.prevedges);
	if ((pd.symetrical_h || pd.symetrical_v) && (pd.x_corner || pd.y_corner))
	    findMatchingEdge (&pd, 2, pd.bothedge);
    }

    /* There will never be more lines than there are points (counting next/prev as separate) */
    m_lines.reserve (m_points.size ()*2);
    for (auto &pd: m_points) if (pd.sp) {
	if ((!m_hv || pd.next_hor || pd.next_ver) && !pd.nextline) {
	    pd.nextline = buildLine (&pd, true);
	    if (pd.colinear)
		pd.prevline = pd.nextline;
	}
	if ((!m_hv || pd.prev_hor || pd.prev_ver ) && !pd.prevline) {
	    pd.prevline = buildLine (&pd, false);
	    if (pd.colinear && !pd.nextline)
		pd.nextline = pd.prevline;
	}
    }

    /* There will never be more stems than there are points (counting next/prev as separate) */
    m_stems.reserve (m_points.size ()*2);

    if (use_existing) {
	if (!m_glyph.vstem.empty ()) {
	    for (auto &stem : m_glyph.vstem)
		stemInfoToStemData (&stem, &m_size, true );
	} if (!m_glyph.hstem.empty ()) {
	    for (auto &stem : m_glyph.hstem)
		stemInfoToStemData (&stem, &m_size, false);
	}
    }

    for (auto &pd: m_points) if (pd.sp) {
	if (pd.prev_e_cnt > 0) {
	    int ecnt = buildStem (&pd, false, use_existing, use_existing, 0);
	    if (ecnt == 0 && pd.prev_e_cnt > 1)
		buildStem (&pd, false, use_existing, use_existing, 1);
	}
	if (pd.next_e_cnt > 0) {
	    int ecnt = buildStem (&pd, true, use_existing, use_existing, 0);
	    if (ecnt == 0 && pd.next_e_cnt > 1)
		buildStem (&pd, true, use_existing, use_existing, 1);
	}
	if (pd.bothedge[0]) {
	    diagonalCornerStem (&pd, use_existing);
	}

	/* AMK/GWW: Snap corner extrema to preexisting hints if they have not */
	/* already been. This is currently done only when preparing  */
	/* glyph data for the autoinstructor */
	if (use_existing && (pd.x_corner || pd.y_corner)) {
	    bool has_h = false, has_v = false;
	    int hv;
	    BasePoint dir;

	    for (size_t j=0; j<pd.prevstems.size () &&
		((pd.x_corner && !has_v) || (pd.y_corner && !has_h)); j++) {
		hv = Units::isHV (&pd.prevstems[j]->unit, true);
		if (hv == 1) has_h = true;
		else if (hv == 2) has_v = true;
	    }
	    for (size_t j=0; j<pd.nextstems.size () &&
		((pd.x_corner && !has_v) || (pd.y_corner && !has_h)); j++) {
		hv = Units::isHV (&pd.nextstems[j]->unit, true);
		if (hv == 1) has_h = true;
		else if (hv == 2) has_v = true;
	    }
	    if (pd.x_corner && !has_v) {
		dir.x = 0; dir.y = 1;
		halfStemNoOpposite (&pd, nullptr, &dir, 2);
	    } else if (pd.y_corner && !has_h) {
		dir.x = 1; dir.y = 0;
		halfStemNoOpposite (&pd, nullptr, &dir, 2);
	    }
	}
    }
    assignLinePointsToStems ();

    /* Normalize stems before calculating active zones (as otherwise */
    /* we don't know exact positions of stem edges */
    if (!use_existing) {
	for (auto &stem: m_stems)
	    normalizeStem (&stem);
	normalizeStubs ();
    }

    /* Figure out active zones at the first order (as they are needed to */
    /* determine which stems are undesired and they don't depend from */
    /* the "potential" state of left/right points in chunks */
#if GLYPH_DATA_DEBUG
    std::cerr << "Going to calculate stem active zones for " << m_glyph.gid () << std::endl;
#endif
    for (auto &stem: m_stems)
	figureStemActive (&stem);

    /* Check this before resolving stem conflicts, as otherwise we can */
    /* occasionally prefer a stem which should be excluded from the list */
    /* for some other reasons */
    if (!use_existing)
	findUnlikelyStems ();

    /* we were cautious about assigning points to stems, go back now and see */
    /*  if there are any low-quality matches which remain unassigned, and if */
    /*  so then assign them to the stem they almost fit on. */
    for (auto &stem: m_stems) {
	for (auto &chunk: stem.chunks) {
	    if (chunk.l && chunk.lpotential) {
		int stemcnt = (chunk.lnext) ? chunk.l->nextstems.size () : chunk.l->prevstems.size ();
		if (stemcnt == 1) chunk.lpotential = false;
	    }
	    if (chunk.r && chunk.rpotential ) {
		int stemcnt = (chunk.rnext ) ? chunk.r->nextstems.size () : chunk.r->prevstems.size ();
		if (stemcnt == 1) chunk.rpotential = false;
	    }
	}
    }
    /* If there are multiple stems, find the one which is closest to this point */
    for (auto &pd: m_points) if (pd.sp) {
	if (pd.prevstems.size () > 1)
	    checkPotential (&pd, false);
	else if (pd.prevstems.size () == 1)
	    pd.prev_pref = 0;
	if (pd.nextstems.size () > 1)
	    checkPotential (&pd, true);
	else if (pd.nextstems.size () == 1)
	    pd.next_pref = 0;
    }

    if (hint_bounding_boxes && !use_existing)
	checkForBoundingBoxHints ();
    checkForGhostHints (use_existing);
    if (hint_diagonal_intersections && !use_existing)
        markDStemCorners ();

    bundleStems (0);
    if (!use_existing && !m_hv) {
	for (auto &stem: m_stems) {
	    if (stem.toobig == 1 && Units::isHV (&stem.unit, true))
		getSerifData (&stem);
	}
    }
    findCounterGroups (true);

#if GLYPH_DATA_DEBUG
    if (!m_lines.empty ()) {
	std::cerr << "Dumping line data:\n";
	for (auto &line: m_lines)
	    std::cerr << line.repr ();
    }
    if (!m_stems.empty ()) {
	std::cerr << "Dumping stem data:\n";
	for (auto &stem: m_stems)
	    std::cerr << stem.repr ();
    }
    if (!hbundle.stemlist.empty () || !vbundle.stemlist.empty ()) {
	std::cerr << "Dumping HV stem bundles:\n";
	std::cerr << hbundle.repr ();
	std::cerr << vbundle.repr ();
    }
#endif
}

// Code related with hint masks goes below

static void clear_conflicting_bits (HintMask *hm, StemData *stemptr, std::vector<bool> &bit_def) {
    hm->setBit (stemptr->stem_idx, false);
    bit_def[stemptr->stem_idx] = true;
    for (auto dep: stemptr->dependent)
	clear_conflicting_bits (hm, dep.stem, bit_def);
}

static void reset_hm_bit (HintMask *hm, StemData *stemptr, std::vector<bool> &bit_def) {
    StemData *master = stemptr;
    while (master->master) master = master->master;
    clear_conflicting_bits (hm, master, bit_def);
    hm->setBit (stemptr->stem_idx, true);
}

StemData *PointData::checkRelated (bool next) {
    auto &stems = next ? this->nextstems : this->prevstems;
    auto &is_l = next ? this->next_is_l : this->prev_is_l;
    StemData *pref = nullptr;
    int depth = 0;
    double dist = GlyphData::dist_error_hv*2;
    int pref_idx = next ? this->next_pref : this->prev_pref;

    if (pref_idx >= 0) {
	StemData *psptr = stems[pref_idx];
	if (psptr->stem_idx >= 0 && (psptr->master || !psptr->dependent.empty ()))
	    return psptr;
    }
    for (size_t i=0; i<stems.size (); i++) {
	StemData *tsptr = stems[i];
	if (tsptr->stem_idx >= 0 && (tsptr->master || !tsptr->dependent.empty ())) {
	    int curdepth = 0;
	    BasePoint stem_base = is_l[i] ? tsptr->left : tsptr->right;
	    double newdist = fabs (
		(stem_base.x - this->base.x) * tsptr->unit.y +
		(stem_base.y - this->base.y) * tsptr->unit.x);
	    StemData *master = tsptr;
	    while (master->master) {
		master = master->master;
		curdepth++;
	    }
	    if (!pref || newdist < dist || (realNear (newdist, dist) && curdepth < depth)) {
		pref = tsptr;
		dist = newdist;
		depth = curdepth;
	    }
	}
    }
    return pref;
}

bool GlyphData::figureHintMasks () {
    HintMask hm;
    bool has_conflicts = false;
    std::vector<bool> bit_def (hbundle.stemlist.size () + vbundle.stemlist.size (), false);

    // Iterate through existing stems. If stem has no conflicts, set the
    // corresponding bit in the initial hint mask and mark it as "set"
    // in the bit_def vector. Otherwise prefer the stem which previously
    // has been considered "master": set the corresponding bit, but don't
    // mark it as "set", so that it may be changed at the next stage
    for (StemBundle *bundle: std::array<StemBundle *, 2> {&hbundle, &vbundle}) {
	for (StemData *stemptr: bundle->stemlist) {
	    if (stemptr->dependent.empty () && !stemptr->master) {
		hm.setBit (stemptr->stem_idx, true);
		bit_def[stemptr->stem_idx] = true;
	    } else {
		has_conflicts = true;
		if (!stemptr->master) {
		    hm.setBit (stemptr->stem_idx, true);
		}
	    }
	}
    }
    if (!has_conflicts)
	return false;

    // Figure the initial hint mask. Iterate through points and see if
    // anyone of them is bound to a conflicting stem. If so, then
    // set the corresponding bit in the mask and mark this bit as 'set'
    // for certain. Do so until we have to change some bit already marked
    // as 'set'
    for (auto &ss: m_fig->contours) {
	ConicPoint *sp = ss.first;
	bool needs_mask_change = false;

	do {
	    PointData *pd = &m_points[sp->ptindex];

	    StemData *prevstem = pd->checkRelated (false);
	    if (prevstem && !bit_def[prevstem->stem_idx]) {
		reset_hm_bit (&hm, prevstem, bit_def);
	    } else if (prevstem)
		needs_mask_change = true;

	    StemData *nextstem = pd->checkRelated (true);
	    if (nextstem && nextstem != prevstem && !bit_def[nextstem->stem_idx]) {
		reset_hm_bit (&hm, nextstem, bit_def);
	    } else if (nextstem && nextstem != prevstem)
		needs_mask_change = true;

	    sp = sp->next ? sp->next->to : nullptr;
	} while (!needs_mask_change && sp && sp != ss.first);
	if (needs_mask_change)
	    break;
    }

    // Attach the initial mask to the first point
    m_fig->contours[0].first->hintmask = std::unique_ptr<HintMask> (new HintMask (hm));
    // Main loop: iterate through points, attach masks when needed
    for (auto &ss: m_fig->contours) {
	ConicPoint *sp = ss.first;

	do {
	    PointData *pd = &m_points[sp->ptindex];
	    bool needs_mask_change = false;

	    StemData *prevstem = pd->checkRelated (false);
	    if (prevstem && !hm.bit (prevstem->stem_idx)) {
		reset_hm_bit (&hm, prevstem, bit_def);
		needs_mask_change = true;
	    }

	    StemData *nextstem = pd->checkRelated (true);
	    if (nextstem && nextstem != prevstem && !hm.bit (nextstem->stem_idx)) {
		reset_hm_bit (&hm, nextstem, bit_def);
		needs_mask_change = true;
	    }

	    if (needs_mask_change)
		sp->hintmask = std::unique_ptr<HintMask> (new HintMask (hm));
	    sp = sp->next ? sp->next->to : nullptr;
	} while (sp && sp != ss.first);
    }
    return true;
}

bool GlyphData::figureCounterMasks (std::vector<HintMask> &cm_list) {
    std::vector<StemData *> starting;
    starting.reserve (hbundle.stemlist.size () + vbundle.stemlist.size ());

    for (StemBundle *bundle: std::array<StemBundle *, 2> {&hbundle, &vbundle}) {
	for (StemData *stemptr: bundle->stemlist) {
	    if (stemptr->next_c_m && !stemptr->prev_c_m)
		starting.push_back (stemptr);
	}
    }
    if (starting.empty ())
	return false;

    for (StemData *stemptr: starting) {
	HintMask cm;
	do {
	    cm.setBit (stemptr->stem_idx, true);
	    stemptr = stemptr->next_c_m;
	} while (stemptr);
	cm_list.push_back (cm);
    }
    return true;
}
