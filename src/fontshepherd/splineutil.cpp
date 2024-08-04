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

#define _USE_MATH_DEFINES
#include <cmath>
#include <cctype>
#include <assert.h>
#include <set>

#include "splineglyph.h"
#include "stemdb.h"
#include "editors/glyphcontext.h"
#include "fs_notify.h"
#include "fs_math.h"

using namespace FontShepherd::math;

struct dotbounds {
    BasePoint unit;
    BasePoint base;
    double len;
    /* GWW: If min<0 || max>len the spline extends beyond its endpoints */
    double min, max;
};

static void approx_bounds (DBounds *b, std::vector<TPoint> &mid, struct dotbounds *db) {
    b->minx = b->maxx = mid[0].x;
    b->miny = b->maxy = mid[0].y;
    db->min = 0; db->max = db->len;
    for (size_t i=1; i<mid.size (); ++i) {
	if (mid[i].x>b->maxx) b->maxx = mid[i].x;
	if (mid[i].x<b->minx) b->minx = mid[i].x;
	if (mid[i].y>b->maxy) b->maxy = mid[i].y;
	if (mid[i].y<b->miny) b->miny = mid[i].y;
	double dot = (mid[i].x-db->base.x)*db->unit.x + (mid[i].y-db->base.y)*db->unit.y;
	if (dot<db->min ) db->min = dot;
	if (dot>db->max ) db->max = dot;
    }
}

static bool bp_colinear (BasePoint *first, BasePoint *mid, BasePoint *last) {
    BasePoint dist_f, unit_f, dist_l, unit_l;
    double len, off_l, off_f;

    dist_f.x = first->x - mid->x; dist_f.y = first->y - mid->y;
    len = sqrt (dist_f.x*dist_f.x + dist_f.y*dist_f.y);
    if (len==0)
	return false;
    unit_f.x = dist_f.x/len; unit_f.y = dist_f.y/len;

    dist_l.x = last->x - mid->x; dist_l.y = last->y - mid->y;
    len = sqrt (dist_l.x*dist_l.x + dist_l.y*dist_l.y);
    if (len==0)
	return false;
    unit_l.x = dist_l.x/len; unit_l.y = dist_l.y/len;

    off_f = dist_l.x*unit_f.y - dist_l.y*unit_f.x;
    off_l = dist_f.x*unit_l.y - dist_f.y*unit_l.x;
    if ((off_f<-1.5 || off_f>1.5) && (off_l<-1.5 || off_l>1.5))
	return false;

    return true;
}

static bool intersect_lines (BasePoint *inter,
	BasePoint *line1_1, BasePoint *line1_2, BasePoint *line2_1, BasePoint *line2_2) {
    // GWW: A lot of functions call this with the same address as an input and the output.
    // In order to avoid unexpected behavior, we delay writing to the output until the end.
    double s1, s2;
    BasePoint _output;
    BasePoint * output = &_output;
    if (line1_1->x == line1_2->x) {
        // Line 1 is vertical.
	output->x = line1_1->x;
	if (line2_1->x == line2_2->x) {
            // Line 2 is vertical.
	    if (line2_1->x!=line1_1->x)
		return false ;		/* Parallel vertical lines */
	    output->y = (line1_1->y+line2_1->y)/2;
	} else {
	    output->y = line2_1->y + (output->x-line2_1->x) * (line2_2->y - line2_1->y)/(line2_2->x - line2_1->x);
        }
        *inter = *output;
        return true ;
    } else if (line2_1->x == line2_2->x) {
        // Line 2 is vertical, but we know that line 1 is not.
	output->x = line2_1->x;
	output->y = line1_1->y + (output->x-line1_1->x) * (line1_2->y - line1_1->y)/(line1_2->x - line1_1->x);
        *inter = *output;
        return true;
    } else {
        // Both lines are oblique.
	s1 = (line1_2->y - line1_1->y)/(line1_2->x - line1_1->x);
	s2 = (line2_2->y - line2_1->y)/(line2_2->x - line2_1->x);
	if (realNear(s1,s2)) {
	    if (!realNear(line1_1->y + (line2_1->x-line1_1->x) * s1,line2_1->y))
		return false;
	    output->x = (line1_2->x+line2_2->x)/2;
	    output->y = (line1_2->y+line2_2->y)/2;
	} else {
	    output->x = (s1*line1_1->x - s2*line2_1->x - line1_1->y + line2_1->y)/(s1-s2);
	    output->y = line1_1->y + (output->x-line1_1->x) * s1;
	}
        *inter = *output;
        return true;
    }
}

static bool closer (const Conic *s1, const Conic *s2, extended_t t1, extended_t t2, extended_t t1p, extended_t t2p) {
    extended_t x1 = ((s1->conics[0].a*t1+s1->conics[0].b)*t1+s1->conics[0].c)*t1+s1->conics[0].d;
    extended_t y1 = ((s1->conics[1].a*t1+s1->conics[1].b)*t1+s1->conics[1].c)*t1+s1->conics[1].d;
    extended_t x2 = ((s2->conics[0].a*t2+s2->conics[0].b)*t2+s2->conics[0].c)*t2+s2->conics[0].d;
    extended_t y2 = ((s2->conics[1].a*t2+s2->conics[1].b)*t2+s2->conics[1].c)*t2+s2->conics[1].d;
    extended_t diff = (x1-x2)*(x1-x2) + (y1-y2)*(y1-y2);
    extended_t x1p = ((s1->conics[0].a*t1p+s1->conics[0].b)*t1p+s1->conics[0].c)*t1p+s1->conics[0].d;
    extended_t y1p = ((s1->conics[1].a*t1p+s1->conics[1].b)*t1p+s1->conics[1].c)*t1p+s1->conics[1].d;
    extended_t x2p = ((s2->conics[0].a*t2p+s2->conics[0].b)*t2p+s2->conics[0].c)*t2p+s2->conics[0].d;
    extended_t y2p = ((s2->conics[1].a*t2p+s2->conics[1].b)*t2p+s2->conics[1].c)*t2p+s2->conics[1].d;
    extended_t diffp = (x1p-x2p)*(x1p-x2p) + (y1p-y2p)*(y1p-y2p);

    if (diff<diffp)
	return false;
    return true;
}


static int add_point (extended_t x, extended_t y, extended_t t, extended_t s,
    std::array<BasePoint, 9> &pts, extended_t t1s[3], extended_t t2s[3], int soln) {
    int i;

    for (i=0; i<soln; ++i) {
	if (x==pts[i].x && y==pts[i].y)
	    return soln;
    }
    if (soln>=9) {
        FontShepherd::postError (
            QCoreApplication::tr ("Too many solutions!"),
            QCoreApplication::tr ("Too many solutions!"),
            nullptr);
    }
    t1s[soln] = t;
    t2s[soln] = s;
    pts[soln].x = x;
    pts[soln].y = y;
    return soln+1;
}

static int ic_add_inter (
    int cnt, BasePoint *foundpos, extended_t *foundt1, extended_t *foundt2,
    const Conic *s1, extended_t t1, extended_t t2, int maxcnt
    ) {

    if (cnt>=maxcnt)
	return cnt;

    foundt1[cnt] = t1;
    foundt2[cnt] = t2;
    foundpos[cnt].x = ((s1->conics[0].a*t1+s1->conics[0].b)*t1+
			s1->conics[0].c)*t1+s1->conics[0].d;
    foundpos[cnt].y = ((s1->conics[1].a*t1+s1->conics[1].b)*t1+
			s1->conics[1].c)*t1+s1->conics[1].d;
    return cnt+1;
}

static int ic_binary_search (
    int cnt, BasePoint *foundpos, extended_t *foundt1, extended_t *foundt2, int other,
    const Conic *s1, const Conic *s2,
    extended_t t1low, extended_t t1high, extended_t t2low, extended_t t2high, int maxcnt
    ) {
    int major;
    extended_t t1, t2;
    extended_t o1o, o2o, o1n, o2n, m;

    major = !other;
    o1o = ((s1->conics[other].a*t1low+s1->conics[other].b)*t1low+
	    s1->conics[other].c)*t1low+s1->conics[other].d;
    o2o = ((s2->conics[other].a*t2low+s2->conics[other].b)*t2low+
	    s2->conics[other].c)*t2low+s2->conics[other].d;
    while (true) {
	t1 = (t1low+t1high)/2;
	m = ((s1->conics[major].a*t1+s1->conics[major].b)*t1+
	    s1->conics[major].c)*t1+s1->conics[major].d;
	t2 = s2->iSolveWithin (major, m, t2low, t2high);
	if (t2==-1)
	    return cnt;

	o1n = ((s1->conics[other].a*t1+s1->conics[other].b)*t1+
		s1->conics[other].c)*t1+s1->conics[other].d;
	o2n = ((s2->conics[other].a*t2+s2->conics[other].b)*t2+
		s2->conics[other].c)*t2+s2->conics[other].d;
	if ((o1n-o2n<.001 && o1n-o2n>-.001) || (t1-t1low<.0001 && t1-t1low>-.0001))
	    return ic_add_inter (cnt, foundpos, foundt1, foundt2, s1, t1, t2, maxcnt);
	if ((o1o>o2o && o1n<o2n) || (o1o<o2o && o1n>o2n)) {
	    t1high = t1;
	    t2high = t2;
	} else {
	    t1low = t1;
	    t2low = t2;
	}
    }
}

static int cubics_intersect (
    const Conic *s1, extended_t lowt1, extended_t hight1, BasePoint *min1, BasePoint *max1,
    const Conic *s2, extended_t lowt2, extended_t hight2, BasePoint *min2, BasePoint *max2,
    BasePoint *foundpos, extended_t *foundt1, extended_t *foundt2, int maxcnt
    ) {
    int major, other;
    BasePoint max, min;
    extended_t t1max, t1min, t2max, t2min, t1, t2, t1diff, oldt2;
    extended_t o1o, o2o, o1n, o2n, m;
    int cnt=0;

    if ((min.x = min1->x)<min2->x) min.x = min2->x;
    if ((min.y = min1->y)<min2->y) min.y = min2->y;
    if ((max.x = max1->x)>max2->x) max.x = max2->x;
    if ((max.y = max1->y)>max2->y) max.y = max2->y;

    if (max.x<min.x || max.y<min.y)
	return 0;
    if (max.x-min.x > max.y-min.y)
	major = 0;
    else
	major = 1;
    other = 1-major;

    t1max = s1->iSolveWithin (major, (&max.x)[major], lowt1, hight1);
    t1min = s1->iSolveWithin (major, (&min.x)[major], lowt1, hight1);
    t2max = s2->iSolveWithin (major, (&max.x)[major], lowt2, hight2);
    t2min = s2->iSolveWithin (major, (&min.x)[major], lowt2, hight2);
    if (t1max==-1 || t1min==-1 || t2max==-1 || t2min==-1)
	return 0 ;
    t1diff = (t1max-t1min)/64.0;
    if (realNear (t1diff, 0))
	return 0;

    t1 = t1min; t2 = t2min;
    o1o = t1==0   ? (&s1->from->me.x)[other] :
	  t1==1.0 ? (&s1->to->me.x)[other] :
	    ((s1->conics[other].a*t1+s1->conics[other].b)*t1+
	    s1->conics[other].c)*t1+s1->conics[other].d;
    o2o = t2==0   ? (&s2->from->me.x)[other] :
	  t2==1.0 ? (&s2->to->me.x)[other] :
	    ((s2->conics[other].a*t2+s2->conics[other].b)*t2+
	    s2->conics[other].c)*t2+s2->conics[other].d;
    if (o1o==o2o)
	cnt = ic_add_inter (cnt, foundpos, foundt1, foundt2, s1, t1, t2, maxcnt);
    while (true) {
	if (cnt>=maxcnt)
	    break;
	t1 += t1diff;
	if ((t1max>t1min && t1>t1max ) || (t1max<t1min && t1<t1max) || cnt>3)
	    break;
	m =   t1==0   ? (&s1->from->me.x)[major] :
	      t1==1.0 ? (&s1->to->me.x)[major] :
		((s1->conics[major].a*t1+s1->conics[major].b)*t1+
		s1->conics[major].c)*t1+s1->conics[major].d;
	oldt2 = t2;
	t2 = s2->iSolveWithin (major, m, lowt2, hight2);
	if (t2==-1)
	    continue;

	o1n = t1==0   ? (&s1->from->me.x)[other] :
	      t1==1.0 ? (&s1->to->me.x)[other] :
		((s1->conics[other].a*t1+s1->conics[other].b)*t1+
		s1->conics[other].c)*t1+s1->conics[other].d;
	o2n = t2==0   ? (&s2->from->me.x)[other] :
	      t2==1.0 ? (&s2->to->me.x)[other] :
		((s2->conics[other].a*t2+s2->conics[other].b)*t2+
		s2->conics[other].c)*t2+s2->conics[other].d;
	if (o1n==o2n)
	    cnt = ic_add_inter (cnt, foundpos, foundt1, foundt2, s1, t1, t2, maxcnt);
	if ((o1o>o2o && o1n<o2n) || (o1o<o2o && o1n>o2n))
	    cnt = ic_binary_search (
		cnt, foundpos, foundt1, foundt2, other,
		s1, s2, t1-t1diff, t1, oldt2, t2, maxcnt
	    );
	o1o = o1n; o2o = o2n;
    }
    return cnt;
}

void BasePoint::transform (BasePoint *from, const std::array<double, 6> &transform) {
    BasePoint p;
    p.x = transform[0]*from->x + transform[2]*from->y + transform[4];
    p.y = transform[1]*from->x + transform[3]*from->y + transform[5];
    this->x = rint (1024*p.x)/1024;
    this->y = rint (1024*p.y)/1024;
}

void ConicPointList::doTransform (const std::array<double, 6> &transform) {
    Conic *spl, *first_spl=nullptr;

    first->doTransform (transform);
    for (spl = first->next; spl && spl!=first_spl; spl=spl->to->next) {
        if (spl->to != first)
            spl->to->doTransform (transform);
        spl->refigure ();
        if (!first_spl) first_spl = spl;
    }
}

void ConicPointList::reverse () {
    ConicPoint *sp = first;
    BasePoint tp;
    Conic *spl;
    bool swap;
    /* GWW: reverse the splineset so that what was the start point becomes the end */
    /*  and vice versa. This entails reversing every individual spline, and */
    /*  each point */

    do {
	tp = sp->nextcp;
        sp->nextcp = sp->prevcp;
        sp->prevcp = tp;
        swap = sp->nonextcp;
        sp->nonextcp = sp->noprevcp;
        sp->noprevcp = swap;

        spl = sp->next;
        sp->next = sp->prev;
        sp->prev = spl;

        if (spl) {
            sp = spl->to;
            spl->to = spl->from;
            spl->from = sp;
        } else
            sp = nullptr;
    } while (sp && sp != first);

    if (first != last) {
	sp = first;
	first = last;
	last = sp;
	first->prev = nullptr;
	last->next = nullptr;
	ensureStart ();
    }

    Conic *s = first->next;
    if (s) do {
	s->refigure ();
	s = s->to->next;
    } while (s && s != first->next);
}

int ConicPointList::toPointCollection (
    int ptcnt, std::vector<BasePoint> &pts, char *flags) {

    ConicPoint *sp, *first, *nextsp;
    int startcnt = ptcnt;

    if (this->first->prev && this->first->prev->from->nextcpindex==startcnt) {
	if (flags) flags[ptcnt] = 0;
	pts[ptcnt].x = rint (this->first->prevcp.x);
	pts[ptcnt++].y = rint (this->first->prevcp.y);
    } else if (this->first->ttfindex!=ptcnt && this->first->ttfindex!=0xfffe)
        FontShepherd::postError (
            QCoreApplication::tr ("Unexpected point count"),
            QCoreApplication::tr ("Unexpected point count in SSAddPoints"),
            nullptr);

    first = nullptr;
    for (sp=this->first; sp!=first ;) {
	if (sp->ttfindex!=0xffff) {
	    if (flags) flags[ptcnt] = _On_Curve;
	    pts[ptcnt].x = rint (sp->me.x);
	    pts[ptcnt].y = rint (sp->me.y);
	    sp->ttfindex = ptcnt++;
	}
	nextsp = sp->next ? sp->next->to : nullptr;
	if (sp->nextcpindex == startcnt)
	    /* This control point is actually our first point, not our last */
            break;
	if ((sp->nextcpindex !=0xffff && sp->nextcpindex!=0xfffe) || !sp->nonextcp) {
	    if (flags) flags[ptcnt] = 0;
	    pts[ptcnt].x = rint (sp->nextcp.x);
	    pts[ptcnt++].y = rint (sp->nextcp.y);
	}
	if (!nextsp)
            break;
	if (!first) first = sp;
	sp = nextsp;
    }
    return (ptcnt);
}

void ConicPointList::selectAll () {
    ConicPoint *sp = this->first;
    do {
	sp->selected = true;
        sp = (sp->next) ? sp->next->to : nullptr;
    } while (sp && sp != this->first);
}

bool ConicPointList::isSelected () const {
    bool anypoints = false;
    Conic *spline, *head;

    head = nullptr;
    if (first->selected)
	anypoints = true;
    for (spline=first->next; spline && spline!=head && !anypoints; spline = spline->to->next) {
	if (spline->to->selected)
	    anypoints = true;
	if (!head)
	    head = spline;
    }
    return anypoints;
}

void ConicPointList::ensureStart () {
    Conic *spl = this->first->next;
    this->first->isfirst = true;
    while (spl && spl->to != this->first) {
	spl->to->isfirst = false;
	spl = spl->to->next;
    }
}

uint16_t ConicPointList::lastPointIndex () {
    if (first == last && last->prev) {
	ConicPoint *prevsp = last->prev->from;
	if (last->ttfindex != -1 && !prevsp->nonextcp)
	    return prevsp->nextcpindex;
	else if (prevsp->ttfindex != -1)
	    return prevsp->ttfindex;
	else if (prevsp->prev)
	    return prevsp->prev->from->nextcpindex;
    }
    return last->ttfindex;
}

void ConicPointList::findBounds (DBounds &b) {
    b.minx = b.maxx = first->me.x;
    b.miny = b.maxy = first->me.y;

    Conic *spline = first->next;
    if (spline) do {
	spline->findBounds (b);
	spline = spline->to->next;
    } while (spline && spline != first->next);
}

ConicPoint::ConicPoint () {
    me.x = 0; me.y = 0;
    prevcp = nextcp = me;
    // This should eventually be changed, but currently many places in code rely on this
    nonextcp = noprevcp = false;
    ttfindex = nextcpindex = 0;
    next = prev = nullptr;
    item = nullptr;
    isfirst = me_changed = nextcp_changed = prevcp_changed = checked = selected = false;
    pointtype = pt_corner;
}

ConicPoint::~ConicPoint () {
    if (this->item)
	this->item->setValid (false);
}

ConicPoint::ConicPoint (const ConicPoint &other_pt) {
    *this = other_pt;
}

ConicPoint& ConicPoint::operator = (const ConicPoint &other_pt) {
    me = other_pt.me;
    nextcp = other_pt.nextcp; prevcp = other_pt.prevcp;
    pointtype = other_pt.pointtype;
    nonextcp = other_pt.nonextcp;
    noprevcp = other_pt.noprevcp;
    checked = other_pt.checked;
    selected = other_pt.selected;
    isfirst = other_pt.isfirst;
    ttfindex = other_pt.ttfindex;
    nextcpindex = other_pt.nextcpindex;
    ptindex = other_pt.ptindex;
    next = other_pt.next; prev = other_pt.prev;
    if (this->hintmask)
	this->hintmask.reset ();
    if (other_pt.hintmask)
	hintmask = std::unique_ptr<HintMask> (new HintMask (*other_pt.hintmask));
    item = nullptr;

    me_changed = other_pt.me_changed;
    nextcp_changed = other_pt.nextcp_changed;
    prevcp_changed = other_pt.prevcp_changed;

    return *this;
}

ConicPoint::ConicPoint (double x, double y) : ConicPoint () {
    me.x = x; me.y = y;
    prevcp = nextcp = me;
    nonextcp = noprevcp = true;
}

void ConicPoint::doTransform (const std::array<double, 6> &transform) {
    me.transform (&me, transform);
    if (!nonextcp)
	nextcp.transform (&nextcp, transform);
    else
	nextcp = me;
    if (!noprevcp)
	prevcp.transform (&prevcp, transform);
    else
	prevcp = me;
}

void ConicPoint::categorize () {
    pointtype = pt_corner;

    if (!next && !prev)
	;
    /* Empty segments */
    else if ((next && next->to->me.x==me.x && next->to->me.y==me.y) ||
	    (prev && prev->from->me.x==me.x && prev->from->me.y==me.y))
	;
    else if (!next) {
	pointtype = noprevcp ? pt_corner : pt_curve;
    } else if (!prev) {
	pointtype = nonextcp ? pt_corner : pt_curve;
    } else if (nonextcp && noprevcp) {
	;
    } else {
	BasePoint ndir, ncdir, ncunit, pdir, pcdir, pcunit;
	double nlen, nclen, plen, pclen;
	double dot;

	ndir.x = ndir.y = pdir.x = pdir.y = 0;

        if (next) {
            ndir.x = next->to->me.x - me.x; ndir.y = next->to->me.y - me.y;
        }
	if (nonextcp) {
            ncdir.x = ncdir.y = 0;;
            nclen = nlen = 0;
        } else {
            ncdir.x = nextcp.x - me.x; ncdir.y = nextcp.y - me.y;
            nclen = sqrt (ncdir.x*ncdir.x + ncdir.y*ncdir.y);
        }
        nlen = sqrt (ndir.x*ndir.x + ndir.y*ndir.y);

        if (prev) {
            pdir.x = prev->from->me.x - me.x; pdir.y = prev->from->me.y - me.y;
        }
	if (noprevcp) {
            pcdir.x = pcdir.y = 0;;
            pclen = plen = 0;
        } else {
            pcdir.x = prevcp.x - me.x; pcdir.y = prevcp.y - me.y;
            pclen = sqrt (pcdir.x*pcdir.x + pcdir.y*pcdir.y);
        }
        plen = sqrt (pdir.x*pdir.x + pdir.y*pdir.y);

	ncunit = ncdir; pcunit = pcdir;
	if (nclen!=0) { ncunit.x /= nclen; ncunit.y /= nclen; }
	if (pclen!=0) { pcunit.x /= pclen; pcunit.y /= pclen; }
	if (nlen!=0) { ndir.x /= nlen; ndir.y /= nlen; }
	if (plen!=0) { pdir.x /= plen; pdir.y /= plen; }

	/* GWW: find out which side has the shorter control vector. Dot that vector */
	/*  with the normal of the unit vector on the other side. If the */
	/*  result is less than 1 em-unit then we've got colinear control points */
	/*  (within the resolution of the integer grid) */
	/* Not quite... they could point in the same direction */
	if (nclen!=0 && pclen!=0 && (
            (nclen>=pclen && (dot = pcdir.x*ncunit.y - pcdir.y*ncunit.x)<1.0 && dot>-1.0) ||
            (pclen>nclen && (dot = ncdir.x*pcunit.y - ncdir.y*pcunit.x)<1.0 && dot>-1.0)) &&
            ncdir.x*pcdir.x + ncdir.y*pcdir.y < 0)
	    pointtype = pt_curve;
	/* GWW: Dot product of control point with unit vector normal to line in */
	/*  opposite direction should be less than an em-unit for a tangent */
	else if (
            (realNear (nclen, 0) && !realNear (pclen, 0) &&
                (dot = pcdir.x*ndir.y-pcdir.y*ndir.x)<1.0 && dot>-1.0) ||
            (realNear (pclen, 0) && !realNear (nclen, 0) &&
                (dot = ncdir.x*pdir.y-ncdir.y*pdir.x)<1.0 && dot>-1.0))
	    pointtype = pt_tangent;
    }
}

bool ConicPoint::isExtremum () {
    BasePoint *prevp, *nextp;
    ConicPoint *psp, *nsp;

    // AMK: in FF this test currently returns true. I opposed this change when
    // it was introduced, as such extrema aren't interesting to mark them
    if (!prev || !next)
        return false;

    nsp = next->to;
    psp = prev->from;

    if (prev->islinear)
	prevp = &psp->me;
    else if (!noprevcp)
	prevp = &prevcp;
    else
	prevp = &psp->nextcp;
    if (next->islinear)
	nextp = &nsp->me;
    else if (!nonextcp)
	nextp = &nextcp;
    else
	nextp = &nsp->prevcp;

    if ((next->islinear && (realNear (me.x, nsp->me.x) || realNear (me.y, nsp->me.y))) ||
	(prev->islinear && (realNear (me.x, psp->me.x) || realNear (me.y, psp->me.y))))
	return false;

    if (next->islinear && prev->islinear && (
      (realNear (me.x, nsp->me.x) && realNear (me.x, psp->me.x) &&
        ((me.y<=nsp->me.y && me.y<=me.y) ||
        (me.y>=nsp->me.y && psp->me.y>=me.y))) ||
      (realNear (me.y, nsp->me.y) && realNear (me.y, psp->me.y) &&
        ((me.x<=nsp->me.x && psp->me.x<=me.x) ||
        (me.x>=nsp->me.x && psp->me.x>=me.x)))))
        /* GWW: A point in the middle of a horizontal/vertical line */
        /*  is not an extremum and can be removed */
        return false;

    if (realNear (prevp->x, me.x) && realNear (nextp->x, me.x)) {
	if (realNear (prevp->y, me.y) && realNear (nextp->y, me.y))
            return false;
        return true;
    } else if (realNear (prevp->y, me.y) && realNear (nextp->y, me.y)) {
	if (realNear (prevp->x, me.x) && realNear (nextp->x, me.x))
            return false;
        return true;
    } else if ((prevp->x<=me.x && nextp->x<=me.x ) || (prevp->x>=me.x && nextp->x>=me.x))
        return true;
    else if ((prevp->y<=me.y && nextp->y<=me.y) || (prevp->y>=me.y && nextp->y>=me.y ))
        return true;

    return false;
}

void ConicPoint::moveBasePoint (BasePoint newpos) {
    double x_shift = newpos.x - me.x;
    double y_shift = newpos.y - me.y;

    me.x += x_shift;
    me.y += y_shift;
    if (noprevcp)
        prevcp = me;
    else {
        prevcp.x += x_shift;
        prevcp.y += y_shift;
        if (prev && prev->order2 && prev->from->item == nullptr)
            prev->from->moveControlPoint (prevcp, true);
    }

    if (nonextcp)
        nextcp = me;
    else {
        nextcp.x += x_shift;
        nextcp.y += y_shift;
        if (next && next->order2 && next->to->item == nullptr)
            next->to->moveControlPoint (nextcp, true);
    }
}

void ConicPoint::moveControlPoint (BasePoint newpos, bool is_next) {
    Conic *spl=nullptr, *opp_spl=nullptr;
    ConicPoint *fw_pt, *bw_pt;
    BasePoint &pt = is_next ? nextcp : prevcp;
    BasePoint &opp_pt = is_next ? prevcp : nextcp;
    double newx, newy;

    // Do nothing, if point is already at the desired position.
    // This prevents endless recursion between two items, representing the
    // same control point on a quadratic spline and attempting to update each other
    if (FontShepherd::math::realNear (newpos.x, pt.x) &&
        FontShepherd::math::realNear (newpos.y, pt.y))
        return;

    if (is_next) {
        spl = next; opp_spl = prev;
        if (spl) fw_pt = spl->to;
        if (opp_spl) bw_pt = opp_spl->from;
    } else {
        spl = prev; opp_spl = next;
        if (spl) fw_pt = spl->from;
        if (opp_spl) bw_pt = opp_spl->to;
    }

    me_changed = false;
    setCpChanged (!is_next, false);
    setCpChanged (is_next, true);
    pt = newpos;
    newx = newpos.x - me.x;
    newy = newpos.y - me.y;

    if (FontShepherd::math::realNear (newpos.x, me.x) &&
        FontShepherd::math::realNear (newpos.y, me.y)) {

        setNoCP (is_next, true);
        if (spl && spl->order2) {
            fw_pt->setNoCP (!is_next, true);
            if (fw_pt->ttfindex == -1)
                fw_pt->ttfindex = 0;
        }

    } else if (pointtype == pt_tangent) {
        BasePoint unit;

        if (opp_spl && opp_spl->islinear) {
            double xlen = me.x - bw_pt->me.x;
            double ylen = me.y - bw_pt->me.y;
            double opp_len = sqrt (xlen*xlen + ylen*ylen);
            unit.x = xlen/opp_len; unit.y = ylen/opp_len;
            double dot = (xlen*newx) + (ylen*newy);
            double len = dot/opp_len;
            newx = len * unit.x; newy = len * unit.y;
            pt.x = me.x + newx;
            pt.y = me.y + newy;
        }

    // If our spline point is faked (i. e. represents a middle position between
    // two control points on a quadratic (TTF) spline), then interpolate its new
    // position between two control points
    } else if (spl && spl->order2 && ttfindex == -1) {
    	me.x = (pt.x + opp_pt.x)/2;
    	me.y = (pt.y + opp_pt.y)/2;
        me_changed = true;

    // Maintain the opposite control point position for a curved spline point. This
    // is safe for a cubic spline, but needs a caution in case of a quadratic one.
    // If the current point is curved (and not interpolated) and the next point
    // in the given direction is also curved, then just convert the current point
    // to corner and keep the opposite control point as is, as we have to stop somewhere
    // anyway
    } else if (pointtype == pt_curve && opp_spl &&
        (!opp_spl->order2 || bw_pt->pointtype != pt_curve || bw_pt->ttfindex == -1)) {
        double opp_x = opp_pt.x - me.x;
        double opp_y = opp_pt.y - me.y;
        double hyp = sqrt (newx * newx + newy * newy);
        double hyp_opp = sqrt (opp_x * opp_x + opp_y * opp_y);

        if (hyp != 0) {
            double new_opp_x = -hyp_opp/hyp*newx;
            double new_opp_y = -hyp_opp/hyp*newy;
            double x_shift = new_opp_x - opp_x;
            double y_shift = new_opp_y - opp_y;
            if (fabs (x_shift) >= .1 || fabs (y_shift) >= .1) {
                opp_pt.x += x_shift;
                opp_pt.y += y_shift;
            }
        }
        if (!bw_pt->item)
            bw_pt->moveControlPoint (opp_pt, is_next);
        setCpChanged (!is_next, true);
    // Turn to corner, unless there is no opposite spline (which is the
    // case when we are adding new points to an open contour)
    } else if (opp_spl && opp_spl->order2 && !bp_colinear (&prevcp, &me, &nextcp)) {
	pointtype = pt_corner;
    }
    setNoCP (is_next, realNear (pt.x, me.x) && realNear (pt.y, me.y));
    if (!fw_pt->item && spl && spl->order2)
        fw_pt->moveControlPoint (pt, !is_next);
}

// Just calclulate the position, but don't make any changes to the point itself
BasePoint ConicPoint::defaultCP (bool is_next, bool order2, bool snaptoint) {
    ConicPoint *bwpt=nullptr, *fwpt;
    extended_t len, ulen;
    BasePoint unit;
    Conic *fws = is_next ? next : prev;
    Conic *bws = is_next ? prev : next;
    BasePoint cp = me;
    double ratio = order2 ? .5 : .39;

    if (!fws)
        return cp;

    fwpt = is_next ? next->to : prev->from;
    if (bws)
	bwpt = is_next ? prev->from : next->to;

    unit.x = fwpt->me.x - me.x;
    unit.y = fwpt->me.y - me.y;
    ulen = sqrt (unit.x*unit.x + unit.y*unit.y);
    if (ulen!=0) {
	unit.x /= ulen; unit.y /= ulen;
    }

    if (pointtype == pt_curve) {
	if (bws) {
	    unit.x = fwpt->me.x - bwpt->me.x;
	    unit.y = fwpt->me.y - bwpt->me.y;
	    ulen = sqrt (unit.x*unit.x + unit.y*unit.y);
	    if (ulen!=0) {
		unit.x /= ulen;
                unit.y /= ulen;
            }
	}

    } else if (pointtype == pt_tangent) {
	if (fwpt->pointtype != pt_corner && bws && bws->islinear) {
            unit.x = me.x-bwpt->me.x;
            unit.y = me.y-bwpt->me.y;
            ulen = sqrt (unit.x*unit.x + unit.y*unit.y);
            if (ulen!=0) {
                unit.x /= ulen;
                unit.y /= ulen;
            }
	}
    }
    len = ratio * sqrt (
        (me.x-fwpt->me.x)*(me.x-fwpt->me.x) +
        (me.y-fwpt->me.y)*(me.y-fwpt->me.y));
    if ((pointtype == pt_corner && fwpt->pointtype == pt_corner) ||
        (pointtype + fwpt->pointtype == pt_corner + pt_tangent))
	cp = me;
    else {
	cp.x = me.x + len*unit.x;
	cp.y = me.y + len*unit.y;
	if (snaptoint) {
	    cp.x = rint (cp.x);
	    cp.y = rint (cp.y);
	} else {
	    cp.x = rint (cp.x*1024)/1024;
	    cp.y = rint (cp.y*1024)/1024;
	}
    }
    return cp;
}

#if 0
#define NICE_PROPORTION	.39
void ConicPoint::defaultCP (bool is_next, bool snaptoint) {
    SplinePoint *prev=NULL, *next;
    double len, plen, ulen;
    BasePoint unit;
    Conic *fws = is_next ? next : prev;
    Conic *bws = is_next ? prev : next;
    ConicPoint *fwp = nullptr, *bwp = nullptr;
    BasePoint &fwcp = is_next ? nextcp : prevcp;
    BasePoint &bwcp = is_next ? prevcp : nextcp;
    bool fwptdef = is_next ? !nonextcp : !noprevcp;
    bool &nofwcp = is_next ? nonextcp : noprevcp;
    bool &nobwcp = is_next ? noprevcp : nonextcp;

    if (fws)
	fwp = is_next ? fws->to : fws->from;
    else
	return;
    if (bws)
	bwp = is_next ? bws->from : bws->to;

    if (fws->order2) {
	fws->refigureFixup ();
	return;
    }

    if (!fwcpdef) {
	if (pointtype==pt_tangent )
	    tangentCP (is_next, snaptopoint);
	return;
    }

    len = NICE_PROPORTION * sqrt(
	(me.x-fwp->me.x)*(me.x-fwp->me.x) +
	(me.y-fwp->me.y)*(me.y-fwp->me.y));
    unit.x = fwp->me.x - fwp->me.x;
    unit.y = fwp->me.y - fwp->me.y;
    ulen = sqrt (unit.x*unit.x + unit.y*unit.y);
    if (ulen) {
	unit.x /= ulen; unit.y /= ulen;
    }
    nofwcp = false;

    if (pointtype == pt_curve) {
	if (bws && nobwcp) {
	    unit.x = fwpt->me.x - fwpt->me.x;
	    unit.y = fwpt->me.y - fwpt->me.y;
	    ulen = sqrt (unit.x*unit.x + unit.y*unit.y);
	    if (ulen!=0) {
		unit.x /= ulen; unit.y /= ulen;
	    }
	    plen = sqrt(
		(bwcp.x-me.x)*(bwcp.x-me.x) +
		(bwcp.y-me.y)*(bwcp.y-me.y));
	    bwcp.x = me.x - plen*unit.x;
	    bwcp.y = me.y - plen*unit.y;
	    if (snaptoint) {
		bwcp.x = rint(base->prevcp.x);
		bwcp.y = rint(base->prevcp.y);
	    }
	    bws->refigureFixup ();
	} else if (bws) {
	    /* The prev control point is fixed. So we've got to use the same */
	    /*  angle it uses */
	    unit.x = me.x-bwcp.x;
	    unit.y = me.y-bwcp.y;
	    ulen = sqrt (unit.x*unit.x + unit.y*unit.y);
	    if (ulen!=0) {
		unit.x /= ulen; unit.y /= ulen;
	    }
	} else {
	    bwcp = base->me;
	    nobwcp = true;
	}
    } else if (pointtype == pt_corner) {
	if (fwpt->pointtype != pt_curve) {
	    nofwcp = true;
	}
    } else /* tangent */ {
	if (fwpt->pointtype != pt_curve) {
	    nofwcp = true;
	} else {
	    if (bws) {
		if (!nobwcp) {
		    plen = sqrt (
			(bwcp.x-me.x)*(bwcp.x-me.x) +
			(bwcp.y-me.y)*(bwcp.y-me.y));
		    bwcp.x = base->me.x - plen*unit.x;
		    bwcp.y = base->me.y - plen*unit.y;
		    bws->refigureFixup ();
		}
		unit.x = me.x-bwp->me.x;
		unit.y = me.y-bwp->me.y;
		ulen = sqrt (unit.x*unit.x + unit.y*unit.y);
		if (ulen!=0) {
		    unit.x /= ulen; unit.y /= ulen;
		}
	    }
	}
    }
    if (nofwcp)
	fwcp = base->me;
    else {
	fwcp.x = me.x + len*unit.x;
	fwcp.y = me.y + len*unit.y;
	if (snaptoint) {
	    fwcp.x = rint (fwcp.x);
	    fwcp.y = rint (fwcp.y);
	} else {
	    fwcp.x = rint (fwcp.x*1024)/1024;
	    fwcp.y = rint (fwcp.y*1024)/1024;
	}
	if (fws)
	    fws->refigureFixup ();
    }
}
#undef NICE_PROPORTION
#endif

bool ConicPoint::cpChanged (bool is_next) const {
    return (is_next ? nextcp_changed : prevcp_changed);
}

bool ConicPoint::meChanged () const {
    return me_changed;
}

void ConicPoint::setCpChanged (bool is_next, bool val) {
    if (is_next) nextcp_changed = val;
    else prevcp_changed = val;
}

bool ConicPoint::noCP (bool is_next) const {
    return (is_next ? nonextcp : noprevcp);
}

void ConicPoint::setNoCP (bool is_next, bool val) {
    if (is_next) nonextcp = val;
    else noprevcp = val;
}

void ConicPoint::joinCpFixup (bool order2) {
    BasePoint ndir, pdir;
    double nlen, plen;
    bool fixprev=false, fixnext=false;

    if (pointtype == pt_corner)
	/* Leave control points as they are */;
    else if (pointtype == pt_tangent) {
	nextcp = defaultCP (true, order2);
	prevcp = defaultCP (false, order2);
	fixprev = fixnext = true;
    } else if (!bp_colinear (&prevcp, &me, &nextcp)) {
	ndir.x = nextcp.x - me.x;
	ndir.y = nextcp.y - me.y;
	nlen = sqrt (ndir.x*ndir.x + ndir.y*ndir.y);
	if (nlen!=0) {
	    ndir.x /= nlen; ndir.y/=nlen;
	}
	pdir.x = prevcp.x - me.x;
	pdir.y = prevcp.y - me.y;
	plen = sqrt (pdir.x*pdir.x + pdir.y*pdir.y);
	if (plen!=0) {
	    pdir.x /= plen; pdir.y/=plen;
	}
        nextcp = defaultCP (true, order2);
        prevcp = defaultCP (false, order2);
        fixprev = fixnext = true;
    }
    if (next && next->to->pointtype==pt_tangent && next->to->next) {
	next->to->nextcp = next->to->defaultCP (true, order2);
	next->to->next->refigure ();
    }
    if (prev && prev->from->pointtype==pt_tangent && prev->from->prev) {
	prev->from->prevcp = prev->from->defaultCP (false, order2);
	prev->from->prev->refigure ();
    }
    if (fixprev && prev)
	prev->refigure ();
    if (fixnext && next)
	next->refigure ();
}

bool ConicPoint::isFirst () const {
    return isfirst;
}

bool ConicPoint::roundToInt (bool order2) {
    bool ret = false;
    std::vector<double *> coords;
    coords.reserve (6);
    if (!order2 || ttfindex != -1) {
	coords.push_back (&me.x);
	coords.push_back (&me.y);
    }
    if (!noprevcp) {
	coords.push_back (&prevcp.x);
	coords.push_back (&prevcp.y);
    }
    if (!nonextcp) {
	coords.push_back (&prevcp.x);
	coords.push_back (&prevcp.y);
    }

    for (double *coord: coords) {
	double test = roundf (*coord);
	if (test != *coord) {
	    *coord = test;
	    ret |= true;
	}
    }
    if (order2 && ttfindex == -1)
	interpolate (0);
    return ret;
}

const double Conic::CURVATURE_ERROR=-1e9;

Conic::Conic () : islinear (false), order2 (false), from (nullptr), to (nullptr) {
}

Conic::Conic (ConicPoint *from, ConicPoint *to, bool order2) :
    order2 (order2), from (from), to (to) {

    from->next = to->prev = this;
    islinear = (from->nonextcp && to->noprevcp);
    refigure ();
}

void Conic::refigure () {
    Conic1D &xsp = conics[0], &ysp = conics[1];
    double t, y;

    islinear = (from->nonextcp && to->noprevcp);
    // NB: should I also set nonextcp/noprevcp if I mark a spline as linear?
    if (!islinear) {
	if (from->me.x==to->me.x) {
	    if (from->me.x==from->nextcp.x &&
                ((from->nextcp.y>=from->me.y && from->nextcp.y<=to->me.y) ||
                (from->nextcp.y<=from->me.y && from->nextcp.y>=to->me.y)))
		islinear = true;
	} else if (from->me.y==to->me.y) {
	    if (from->me.y==from->nextcp.y &&
                ((from->nextcp.x>=from->me.x && from->nextcp.x<=to->me.x) ||
                (from->nextcp.x<=from->me.x && from->nextcp.x>=to->me.x)))
		islinear = true;
	} else if (order2) {
	    t = (from->nextcp.x-from->me.x)/(to->me.x-from->me.x);
	    y = t*(to->me.y-from->me.y) + from->me.y;
	    if (rint (y) == rint (from->nextcp.y))
		islinear = true;
	}
    }

    xsp.d = from->me.x;
    ysp.d = from->me.y;
    if (islinear) {
	xsp.a = xsp.b = 0;
	ysp.a = ysp.b = 0;
	xsp.c = to->me.x-from->me.x;
        ysp.c = to->me.y-from->me.y;
    } else {
        if (order2) {
            xsp.c = 2*(from->nextcp.x-from->me.x);
            ysp.c = 2*(from->nextcp.y-from->me.y);
            xsp.b = to->me.x-from->me.x-xsp.c;
            ysp.b = to->me.y-from->me.y-ysp.c;
            xsp.a = ysp.a = 0;
        } else {
            xsp.c = 3*(from->nextcp.x-xsp.d);
            ysp.c = 3*(from->nextcp.y-ysp.d);
            xsp.b = 3*(to->prevcp.x-from->nextcp.x)-xsp.c;
            ysp.b = 3*(to->prevcp.y-from->nextcp.y)-ysp.c;
            xsp.a = to->me.x-from->me.x-xsp.c-xsp.b;
            ysp.a = to->me.y-from->me.y-ysp.c-ysp.b;
            if (realNear (xsp.a, 0)) xsp.a=0;
            if (realNear (ysp.a, 0)) ysp.a=0;
        }
	if (realNear (xsp.c, 0)) xsp.c=0;
	if (realNear (ysp.c, 0)) ysp.c=0;
	if (realNear (xsp.b, 0)) xsp.b=0;
	if (realNear (ysp.b, 0)) ysp.b=0;
	if (ysp.a==0 && xsp.a==0 && ysp.b==0 && xsp.b==0)
            /* GWW: This seems extremely unlikely... */
            islinear = true;
        // AMK: removed test for order2 (if xsp.a=0 and ysp.a=0), as this sometimes
        // produces positive results on cubic outlines, and we don't need this
    }

    if (std::isnan (ysp.a) || std::isnan (xsp.a) || std::isnan (ysp.b) || std::isnan (xsp.b)) {
        std::cerr << "from " << from->me.x << ", " << from->me.y
            << " via " << from->nextcp.x << ", " << from->nextcp.y
            << " and " << to->prevcp.x << ", " << from->prevcp.y
            << " to " << to->me.x << ", " << to->me.y<< std::endl;
        FontShepherd::postError (
            QCoreApplication::tr ("Bad glyf data"),
            QCoreApplication::tr ("NaN value in conic creation"),
            nullptr);
    }
}

static extended_t esqrt (extended_t e) {
    extended_t rt, temp;

    rt = sqrt ((double) e);
    if (e<=0)
        return rt;

    temp = e/rt;
    rt = (rt+temp)/2;
    temp = e/rt;
    rt = (rt+temp)/2;
    return rt;
}

void Conic1D::findExtrema (extended_t *_t1, extended_t *_t2) const {
    extended_t t1= -1, t2= -1;
    extended_t b2_fourac;

    /* GWW: Find the extreme points on the curve */
    /*  Set to -1 if there are none or if they are outside the range [0,1] */
    /*  Order them so that t1<t2 */
    /*  If only one valid extremum it will be t1 */
    /*  (Does not check the end points unless they have derivative==0) */
    /*  (Does not check to see if d/dt==0 points are inflection points (rather than extrema) */
    if (a!=0) {
	/* cubic, possibly 2 extrema (possibly none) */
	b2_fourac = 4*(extended_t) b*b - 12*(extended_t) a*c;
	if (b2_fourac>=0) {
	    b2_fourac = esqrt (b2_fourac);
	    t1 = (-2*b - b2_fourac) / (6*a);
	    t2 = (-2*b + b2_fourac) / (6*a);
	    if (t1>t2) {
		extended_t temp = t1; t1 = t2; t2 = temp;
	    }
	    else if (t1==t2) t2 = -1;
	    if (realNear(t1,0)) t1=0; else if (realNear(t1,1)) t1=1;
	    if (realNear(t2,0)) t2=0; else if (realNear(t2,1)) t2=1;
	    if (t2<=0 || t2>=1) t2 = -1;
	    if (t1<=0 || t1>=1) { t1 = t2; t2 = -1; }
	}
    } else if (b!=0) {
	/* Quadratic, at most one extremum */
	t1 = -c/(2.0*(extended_t) b);
	if ( t1<=0 || t1>=1 ) t1 = -1;
    } else /*if ( c!=0 )*/ {
	/* linear, no extrema */
    }
    *_t1 = t1; *_t2 = t2;
}

/* GWW: This returns all real solutions, even those out of bounds */
/* I use -999999 as an error flag, since we're really only interested in */
/*  solns near 0 and 1 that should be ok. -1 is perhaps a little too close */
/* Sigh. When solutions are near 0, the rounding errors are appalling. */
bool Conic1D::_cubicSolve (extended_t sought, std::array<extended_t, 3> &ts) {
    extended_t d1, xN, yN, delta2, temp, delta, h, t2, t3, theta;
    extended_t sa=a, sb=b, sc=c, sd=d-sought;
    int i=0;

    ts[0] = ts[1] = ts[2] = -999999;
    if (sd==0 && sa!=0) {
	/* one of the roots is 0, the other two are the soln of a quadratic */
	ts[0] = 0;
	if ( sc==0 ) {
	    ts[1] = -sb/(extended_t) sa;	/* two zero roots */
	} else {
	    temp = sb*(extended_t) sb-4*(extended_t) sa*sc;
	    if (realNear (temp, 0))
		ts[1] = -sb/(2*(extended_t) sa);
	    else if ( temp>=0 ) {
		temp = sqrt(temp);
		ts[1] = (-sb+temp)/(2*(extended_t) sa);
		ts[2] = (-sb-temp)/(2*(extended_t) sa);
	    }
	}
    } else if ( sa!=0 ) {
    /* GWW: http://www.m-a.org.uk/eb/mg/mg077ch.pdf */
    /* this nifty solution to the cubic neatly avoids complex arithmatic */
	xN = -sb/(3*(extended_t) sa);
	yN = ((sa*xN + sb)*xN+sc)*xN + sd;

	delta2 = (sb*(extended_t) sb-3*(extended_t) sa*sc)/(9*(extended_t) sa*sa);
	/*if ( RealWithin(delta2,0,.00000001) ) delta2 = 0;*/

	/* GWW: the descriminant is yN^2-h^2, but delta might be <0 so avoid using h */
	d1 = yN*yN - 4*sa*sa*delta2*delta2*delta2;
	if (((yN>.01 || yN<-.01) && realNear (d/yN, 0)) || ((yN<=.01 && yN>=-.01) && realNear (d, 0)))
	    d1 = 0;
	if (d1>0) {
	    temp = sqrt (d1);
	    t2 = (-yN-temp)/(2*sa);
	    t2 = (t2==0) ? 0 : (t2<0) ? -pow(-t2,1./3.) : pow(t2,1./3.);
	    t3 = (-yN+temp)/(2*sa);
	    t3 = t3==0 ? 0 : (t3<0) ? -pow(-t3,1./3.) : pow(t3,1./3.);
	    ts[0] = xN + t2 + t3;
	} else if (d1<0) {
	    if (delta2>=0) {
		delta = sqrt (delta2);
		h = 2*sa*delta2*delta;
		temp = -yN/h;
		if ( temp>=-1.0001 && temp<=1.0001 ) {
		    if ( temp<-1 ) temp = -1; else if ( temp>1 ) temp = 1;
		    theta = acos(temp)/3;
		    ts[i++] = xN+2*delta*cos(theta);
		    ts[i++] = xN+2*delta*cos(2.0943951+theta);	/* 2*pi/3 */
		    ts[i++] = xN+2*delta*cos(4.1887902+theta);	/* 4*pi/3 */
		}
	    }
	} else if ( /* d1==0 && */ delta2!=0 ) {
	    delta = yN/(2*sa);
	    delta = delta==0 ? 0 : delta>0 ? pow (delta, 1./3.) : -pow (-delta, 1./3.);
	    ts[i++] = xN + delta;	/* this root twice, but that's irrelevant to me */
	    ts[i++] = xN - 2*delta;
	} else if (/* d1==0 && */ delta2==0) {
	    if (xN>=-0.0001 && xN<=1.0001) ts[0] = xN;
	}
    } else if (sb!=0) {
	extended_t d2 = sc*(extended_t) sc-4*(extended_t) sb*sd;
	if (d2<0 && realNear (d2, 0)) d2=0;
	if (d2<0)
            return false;		/* All roots imaginary */
	d2 = sqrt (d2);
	ts[0] = (-sc-d2)/(2*(extended_t) sb);
	ts[1] = (-sc+d2)/(2*(extended_t) sb);
    } else if ( sc!=0 ) {
	ts[0] = -sd/(extended_t) sc;
    } else {
	/* GWW: If it's a point then either everything is a solution, or nothing */
    }
    return (ts[0]!=-999999);
}

bool Conic1D::cubicSolve (extended_t sought, std::array<extended_t, 3> &ts) {
    extended_t t;
    std::array<extended_t, 3> ts2;
    int i, j;
    /* GWW: This routine gives us all solutions between [0,1] with -1 as an error flag */
    /* http://mathforum.org/dr.math/faq/faq.cubic.equations.html */

    ts[0] = ts[1] = ts[2] = -1;
    if (!_cubicSolve (sought, ts2))
        return false;

    for (i=j=0; i<3; ++i) {
	if (ts2[i]>-.0001 && ts2[i]<1.0001) {
	    if (ts2[i]<0) ts[j++] = 0;
	    else if (ts2[i]>1) ts[j++] = 1;
	    else
		ts[j++] = ts2[i];
	}
    }
    if (j==0)
        return false;

    if (ts[0]>ts[2] && ts[2]!=-1) {
	t = ts[0]; ts[0] = ts[2]; ts[2] = t;
    }
    if (ts[0]>ts[1] && ts[1]!=-1) {
	t = ts[0]; ts[0] = ts[1]; ts[1] = t;
    }
    if (ts[1]>ts[2] && ts[2]!=-1) {
	t = ts[1]; ts[1] = ts[2]; ts[2] = t;
    }
        return true;
}

extended_t Conic1D::solve (double tmin, double tmax, extended_t sought) {
    /* GWW: We want to find t so that spline(t) = sought */
    /*  the curve must be monotonic */
    /* returns t which is near sought or -1 */
    std::array<extended_t, 3> ts;
    int i;
    extended_t t;

    cubicSolve (sought, ts);
    if (tmax<tmin) { t = tmax; tmax = tmin; tmin = t; }
    for (i=0; i<3; ++i) {
	if (ts[i]>=tmin && ts[i]<=tmax)
            return ts[i];
    }
    return -1;
}

bool Conic::xSolve (double tmin, double tmax, BasePoint bp, double fudge, double *tptr) {
    Conic1D &yspline = conics[1], &xspline = conics[0];
    long double t, x, y;

    *tptr = t = xspline.solve (tmin, tmax, bp.x);
    if (t>=0 && t<=1) {
	y = ((yspline.a*t+yspline.b)*t+yspline.c)*t + yspline.d;
	if (bp.y-fudge<y && bp.y+fudge>y)
            return true;
    }
    /* GWW: Although we know that globaly there's more x change, locally there */
    /*  maybe more y change */
    *tptr = t = yspline.solve (tmin, tmax, bp.y);
    if (t>=0 && t<=1) {
	x = ((xspline.a*t+xspline.b)*t+xspline.c)*t + xspline.d;
	if (bp.x-fudge<x && bp.x+fudge>x)
            return true;
    }
    return false;
}

bool Conic::ySolve (double tmin, double tmax, BasePoint bp, double fudge, double *tptr) {
    Conic1D &yspline = conics[1], &xspline = conics[0];
    long double t,x,y;

    *tptr = t = yspline.solve (tmin, tmax, bp.y);
    if (t>=0 && t<=1) {
	x = ((xspline.a*t+xspline.b)*t+xspline.c)*t + xspline.d;
	if ( bp.x-fudge<x && bp.x+fudge>x )
        return true;
    }
    /* GWW: Although we know that globaly there's more y change, locally there */
    /*  maybe more x change */
    *tptr = t = xspline.solve (tmin, tmax, bp.x);
    if (t>=0 && t<=1) {
	y = ((yspline.a*t+yspline.b)*t+yspline.c)*t + yspline.d;
	if (bp.y-fudge<y && bp.y+fudge>y)
            return true;
    }
    return false;
}

extended_t Conic::iSolveWithin (int major, extended_t val, extended_t tlow, extended_t thigh) const {
    std::array<extended_t, 3> ts;
    const Conic1D &sp = this->conics[major];
    Conic1D temp (sp);
    int i;

    /* GWW: Calculation for t=1 can yield rounding errors. Insist on the endpoints */
    /*  (the Spline1D is not a perfectly accurate description of the spline,  */
    /*   but the control points are right -- at least that's my defn.) */
    if (tlow==0 && val==(major ? from->me.y : from->me.x))
	return 0;
    if (thigh==1.0 && val==(major ? to->me.y : to->me.x))
	return 1.0;

    temp.d -= val;
    temp.iterateSolve (ts);
    if (tlow<thigh) {
	for (i=0; i<3; ++i) {
	    if (ts[i]>=tlow && ts[i]<=thigh)
		return ts[i];
	}
	for (i=0; i<3; ++i) {
	    if (ts[i]>=tlow-1./1024. && ts[i]<=tlow)
		return tlow;
	    if (ts[i]>=thigh && ts[i]<=thigh+1./1024)
		return thigh;
	}
    } else {
	for (i=0; i<3; ++i)
	    if (ts[i]>=thigh && ts[i]<=tlow)
		return ts[i];
	for (i=0; i<3; ++i) {
	    if (ts[i]>=thigh-1./1024. && ts[i]<=thigh)
		return thigh;
	    if (ts[i]>=tlow && ts[i]<=tlow+1./1024)
		return tlow;
	}
    }
    return -1;
}

extended_t Conic1D::iterateConicSolve (extended_t tmin, extended_t tmax, extended_t sought) {
    extended_t t, low, high, test;
    Conic1D temp (*this);
    /* Now the closed form CubicSolver can have rounding errors so if we know */
    /*  the spline to be monotonic, an iterative approach is more accurate */

    if (tmin>tmax) {
	t=tmin; tmin=tmax; tmax=t;
    }
    temp.d -= sought;

    if (temp.a==0 && temp.b==0 && temp.c!=0) {
	t = -temp.d/temp.c;
	if (t<tmin || t>tmax)
	    return -1;
	return t;
    }

    low = ((temp.a*tmin+temp.b)*tmin+temp.c)*tmin+temp.d;
    high = ((temp.a*tmax+temp.b)*tmax+temp.c)*tmax+temp.d;
    if (low==0)
	return tmin;
    if (high==0)
	return tmax;
    if ((low<0 && high>0 ) || (low>0 && high<0)) {
	while (true) {
	    t = (tmax+tmin)/2;
	    if ( t==tmax || t==tmin )
		return t;
	    test = ((temp.a*t+temp.b)*t+temp.c)*t+temp.d;
	    /* GWW: someone complained that this test relied on exact
    	     * arithmetic. In fact this test will almost never be hit,
	     * the real exit test is the line above, when tmin/tmax are
	     * so close that there is no space between them in the
	     * floating representation */
	    if (test==0)
		return t;
	    if ((low<0 && test<0) || (low>0 && test>0))
		tmin=t;
	    else
		tmax = t;
	}
    /* Rounding errors */
    } else if (low<.0001 && low>-.0001)
	return tmin;
    else if (high<.0001 && high>-.0001)
	return  tmax;

    return -1;
}

void Conic1D::iterateSolve (std::array<extended_t, 3> &ts) {
    /* GWW: The closed form solution has too many rounding errors for my taste... */
    int i, j;

    ts[0] = ts[1] = ts[2] = -1;

    if (this->a!=0) {
	extended_t e[4];
	e[0] = 0; e[1] = e[2] = e[3] = 1.0;
	findExtrema (&e[1], &e[2]);
	if (e[1]==-1) e[1] = 1;
	if (e[2]==-1) e[2] = 1;
	for (i=j=0; i<3; ++i) {
	    ts[j] = iterateConicSolve (e[i], e[i+1], 0);
	    if (ts[j]!=-1) ++j;
	    if (e[i+1]==1.0)
		break;
	}
    } else if (this->b!=0) {
	extended_t b2_4ac = this->c*this->c - 4*this->b*this->d;
	if (b2_4ac>=0) {
	    b2_4ac = sqrt(b2_4ac);
	    ts[0] = (-this->c-b2_4ac)/(2*this->b);
	    ts[1] = (-this->c+b2_4ac)/(2*this->b);
	    if ( ts[0]>ts[1] ) {
		extended_t t = ts[0]; ts[0] = ts[1]; ts[1] = t;
	    }
	}
    } else if (this->c!=0) {
	ts[0] = -this->d/this->c;
    } else {
	/* No solutions, or all solutions */
	;
    }

    for (i=j=0; i<3; ++i)
	if (ts[i]>=0 && ts[i]<=1)
	    ts[j++] = ts[i];
    for (i=0; i<j-1; ++i)
	if (ts[i]+.0000001>ts[i+1]) {
	    ts[i] = (ts[i]+ts[i+1])/2;
	    --j;
	    for (++i; i<j; ++i)
		ts[i] = ts[i+1];
	}
    if (j!=0) {
	if (ts[0]!=0) {
	    extended_t d0 = this->d;
	    extended_t dt = ((this->a*ts[0]+this->b)*ts[0]+this->c)*ts[0]+this->d;
	    if (d0<0) d0=-d0;
	    if (dt<0) dt=-dt;
	    if (d0<dt)
		ts[0] = 0;
	}
	if (ts[j-1]!=1.0) {
	    extended_t d1 = this->a+this->b+this->c+this->d;
	    extended_t dt = ((this->a*ts[j-1]+this->b)*ts[j-1]+this->c)*ts[j-1]+this->d;
	    if (d1<0) d1=-d1;
	    if (dt<0) dt=-dt;
	    if (d1<dt)
		ts[j-1] = 1;
	}
    }
    for (; j<3; ++j)
	ts[j] = -1;
}

static const double D_RE_Factor	= 1024.0*1024.0*1024.0*1024.0*1024.0*2.0;

extended_t Conic1D::iterateSplineSolveFixup (extended_t tmin, extended_t tmax, extended_t sought) {
    extended_t t;
    double factor;
    extended_t val, valp, valm;

    if (tmin>tmax) {
	t=tmin; tmin=tmax; tmax=t;
    }
    t = iterateConicSolve (tmin, tmax, sought);

    if (t==-1)
	return -1;

    if ((val = (((this->a*t+this->b)*t+this->c)*t+this->d) - sought)<0)
	val=-val;
    if (val) {
	for (factor=1024.0*1024.0*1024.0*1024.0*1024.0; factor>.5; factor/=2.0) {
	    extended_t tp = t + (factor*t)/D_RE_Factor;
	    extended_t tm = t - (factor*t)/D_RE_Factor;
	    if (tp>tmax) tp=tmax;
	    if (tm<tmin) tm=tmin;
	    if ((valp = (((this->a*tp+this->b)*tp+this->c)*tp+this->d) - sought)<0)
		valp = -valp;
	    if ((valm = (((this->a*tm+this->b)*tm+this->c)*tm+this->d) - sought)<0)
		valm = -valm;
	    if (valp<val && valp<valm) {
		t = tp;
		val = valp;
	    } else if (valm<val) {
		t = tm;
		val = valm;
	    }
	}
    }
    if (t==0 && !within16RoundingErrors (sought, sought+val))
	return -1;
    /* GWW: if t!=0 then we we get the chance of far worse rounding errors */
    else if (t==tmax || t==tmin) {
	if (within16RoundingErrors (sought, sought+val) ||
	    within16RoundingErrors (this->a, this->a+val) ||
	    within16RoundingErrors (this->b, this->b+val) ||
	    within16RoundingErrors (this->c, this->c+val) ||
	    within16RoundingErrors (this->c, this->c+val) ||
	    within16RoundingErrors (this->d, this->d+val))
	    return t;
	else
	    return -1;
    }

    if (t>=tmin && t<=tmax)
	return t;

    /* GWW: I don't think this can happen... */
    return -1;
}

double Conic1D::closestSplineSolve (double sought, double close_to_t) {
    /* We want to find t so that spline(t) = sought */
    /*  find the value which is closest to close_to_t */
    /* on error return closetot */
    std::array<extended_t, 3> ts;
    int i;
    double t, best, test;

    _cubicSolve (sought, ts);
    best = 9e20; t= close_to_t;
    for (i=0; i<3; ++i) {
	if (ts[i]>-.0001 && ts[i]<1.0001) {
	    if ((test=ts[i]-close_to_t)<0)
		test = -test;
	    if ( test<best ) {
		best = test;
		t = ts[i];
	    }
	}
    }

    return t;
}

bool Conic::nearXSpline (BasePoint bp, double fudge, double *tptr) {
    /* If we get here we've checked the bounding box and we're in it */
    /*  the y spline is a horizontal line */
    /*  the x spline is not linear */
    long double t, y;
    Conic1D &yspline = conics[1], &xspline = conics[0];

    if (xspline.a!=0) {
	extended_t t1, t2, tbase;
	xspline.findExtrema (&t1, &t2);
	tbase = 0;
	if (t1!=-1) {
	    if (xSolve (0, t1, bp, fudge, tptr))
                return true;
	    tbase = t1;
	}
	if ( t2!=-1 ) {
	    if (xSolve (tbase, t2, bp, fudge, tptr))
                return true;
	    tbase = t2;
	}
	if (xSolve (tbase, 1.0, bp, fudge, tptr))
            return true;
    } else if (xspline.b!=0) {
	extended_t root = xspline.c*xspline.c - 4*xspline.b*(xspline.d-bp.x);
	if (root < 0)
            return false;
	root = sqrt(root);
	*tptr = t = (-xspline.c + root)/(2*xspline.b);
	if ( t>=0 && t<=1 ) {
	    y = ((yspline.a*t+yspline.b)*t+yspline.c)*t + yspline.d;
	    if (bp.y-fudge<y && bp.y+fudge>y)
                return true;
	}
	*tptr = t = (-xspline.c - root)/(2*xspline.b);
	if (t>=0 && t<=1) {
	    y = ((yspline.a*t+yspline.b)*t+yspline.c)*t + yspline.d;
	    if (bp.y-fudge<y && bp.y+fudge>y)
                return true;
	}
    } else /* xspline.c can't be 0 because dx>dy => dx!=0 => xspline.c!=0 */ {
	*tptr = t = (bp.x-xspline.d)/xspline.c;
	y = ((yspline.a*t+yspline.b)*t+yspline.c)*t + yspline.d;
	if (bp.y-fudge<y && bp.y+fudge>y)
            return true;
    }
    return false;
}

bool Conic::pointNear (BasePoint bp, double fudge, double *tptr) {
    double t, x, y;
    Conic1D &yspline = conics[1], &xspline = conics[0];
    double dx, dy;

    dx = fabs (to->me.x-from->me.x);
    dy = fabs (to->me.y-from->me.y);

    if (islinear) {
	if (bp.x-fudge > from->me.x && bp.x-fudge > to->me.x)
            return false;
	if (bp.x+fudge < from->me.x && bp.x+fudge < to->me.x)
            return false;
	if (bp.y-fudge > from->me.y && bp.y-fudge > to->me.y)
            return false;
	if (bp.y+fudge < from->me.y && bp.y+fudge < to->me.y)
            return false;
	if (xspline.c == 0 && yspline.c == 0) 	/* It's a point. */
            return true;
	if (dy>dx) {
	    t = (bp.y-yspline.d)/yspline.c;
	    *tptr = t;
	    x = xspline.c*t + xspline.d;
	    if (bp.x-fudge < x && bp.x+fudge > x && t >= 0 && t <= 1)
                return true;
	} else {
	    t = (bp.x-xspline.d)/xspline.c;
	    *tptr = t;
	    y = yspline.c*t + yspline.d;
	    if (bp.y-fudge<y && bp.y+fudge>y && t>=0 && t<=1 )
                return true;
	}
        return false;
    } else {
	if (bp.x-fudge > from->me.x && bp.x-fudge > to->me.x &&
		bp.x-fudge > from->nextcp.x && bp.x-fudge > to->prevcp.x )
            return false;
	if (bp.x+fudge < from->me.x && bp.x+fudge < to->me.x &&
		bp.x+fudge < from->nextcp.x && bp.x+fudge < to->prevcp.x )
            return false;
	if (bp.y-fudge > from->me.y && bp.y-fudge > to->me.y &&
		bp.y-fudge > from->nextcp.y && bp.y-fudge > to->prevcp.y )
            return false;
	if (bp.y+fudge < from->me.y && bp.y+fudge < to->me.y &&
		bp.y+fudge < from->nextcp.y && bp.y+fudge < to->prevcp.y )
            return false;

	if (dx>dy)
            return (nearXSpline (bp, fudge, tptr));
	else if (yspline.a == 0 && yspline.b == 0) {
	    *tptr = t = (bp.y-yspline.d)/yspline.c;
	    x = ((xspline.a*t+xspline.b)*t+xspline.c)*t + xspline.d;
	    if (bp.x-fudge<x && bp.x+fudge>x && t>=0 && t<=1)
                return true;

	} else if (yspline.a==0) {
	    double root = yspline.c*yspline.c - 4*yspline.b*(yspline.d-bp.y);
	    if (root < 0)
                return false;
	    root = sqrt (root);
	    *tptr = t = (-yspline.c + root)/(2*yspline.b);
	    x = ((xspline.a*t+xspline.b)*t+xspline.c)*t + xspline.d;
	    if (bp.x-fudge<x && bp.x+fudge>x && t>0 && t<1)
                return true;

	    *tptr = t = (-yspline.c - root)/(2*yspline.b);
	    x = ((xspline.a*t+xspline.b)*t+xspline.c)*t + xspline.d;
	    if (bp.x-fudge<x && bp.x+fudge>x && t>=0 && t<=1)
                return true;

	} else {
	    extended_t t1, t2, tbase;
	    yspline.findExtrema (&t1, &t2);
	    tbase = 0;
	    if (t1!=-1) {
		if (ySolve (0, t1, bp, fudge, tptr))
                    return true;
		tbase = t1;
	    }
	    if ( t2!=-1 ) {
		if (ySolve (tbase, t2, bp, fudge, tptr))
                    return true;
		tbase = t2;
	    }
	    if (ySolve (tbase, 1.0, bp, fudge, tptr))
                return true;
	}
    }
    return false;
}

bool Conic::cantExtremeX () const {
    /* GWW: Sometimes we get rounding errors when converting from control points */
    /*  to spline coordinates. These rounding errors can give us false */
    /*  extrema. So do a sanity check to make sure it is possible to get */
    /*  any extrema before actually looking for them */

    if (this->from->me.x>=this->from->nextcp.x &&
	this->from->nextcp.x>=this->to->prevcp.x &&
	this->to->prevcp.x>=this->to->me.x )
	return true;
    if (this->from->me.x<=this->from->nextcp.x &&
	this->from->nextcp.x<=this->to->prevcp.x &&
	this->to->prevcp.x<=this->to->me.x )
	return true;

    return false;
}

bool Conic::cantExtremeY () const {
    /* GWW: Sometimes we get rounding errors when converting from control points */
    /*  to spline coordinates. These rounding errors can give us false */
    /*  extrema. So do a sanity check to make sure it is possible to get */
    /*  any extrema before actually looking for them */

    if (this->from->me.y>=this->from->nextcp.y &&
	this->from->nextcp.y>=this->to->prevcp.y &&
	this->to->prevcp.y>=this->to->me.y)
	return true;
    if (this->from->me.y<=this->from->nextcp.y &&
	this->from->nextcp.y<=this->to->prevcp.y &&
	this->to->prevcp.y<=this->to->me.y)
	return true;

    return false;
}

int Conic::findExtrema (std::array<extended_t, 4> &extrema) const {
    int i, j;
    BasePoint last, cur, mid;

    /* GWW: If the control points are at the end-points then this (1D) spline is */
    /*  basically a line. But rounding errors can give us very faint extrema */
    /*  if we look for them */
    if (!cantExtremeX ())
	conics[0].findExtrema (&extrema[0], &extrema[1]);
    else
	extrema[0] = extrema[1] = -1;
    if (!cantExtremeY ())
	conics[1].findExtrema (&extrema[2], &extrema[3]);
    else
	extrema[2] = extrema[3] = -1;

    for (i=0; i<3; ++i) {
	for (j=i+1; j<4; ++j) {
	    if ((extrema[i]==-1 && extrema[j]!=-1) || (extrema[i]>extrema[j] && extrema[j]!=-1)) {
		extended_t temp = extrema[i];
		extrema[i] = extrema[j];
		extrema[j] = temp;
	    }
	}
    }
    for (i=j=0; i<3 && extrema[i]!=-1; ++i) {
	if (extrema[i]==extrema[i+1]) {
	    for (j=i+1; j<3; ++j)
		extrema[j] = extrema[j+1];
	    extrema[3] = -1;
	}
    }

    /* Extrema which are too close together are not interesting */
    last = from->me;
    for (i=0; i<4 && extrema[i]!=-1; ++i) {
	cur.x = ((conics[0].a*extrema[i]+conics[0].b)*extrema[i]+
		conics[0].c)*extrema[i]+conics[0].d;
	cur.y = ((conics[1].a*extrema[i]+conics[1].b)*extrema[i]+
		conics[1].c)*extrema[i]+conics[1].d;
	mid.x = (last.x+cur.x)/2; mid.y = (last.y+cur.y)/2;
	if ((mid.x==last.x || mid.x==cur.x) &&
		(mid.y==last.y || mid.y==cur.y)) {
	    for (j=i; j<3; ++j)
		extrema[j] = extrema[j+1];
	    extrema[3] = -1;
	    --i;
	} else
	    last = cur;
    }
    if (extrema[0]!=-1) {
	mid.x = (last.x+to->me.x)/2; mid.y = (last.y+to->me.y)/2;
	if ((mid.x==last.x || mid.x==cur.x) && (mid.y==last.y || mid.y==cur.y))
	    extrema[i-1] = -1;
    }
    for (i=0; i<4 && extrema[i]!=-1; ++i);
    if (i!=0) {
	cur = to->me;
	mid.x = (last.x+cur.x)/2; mid.y = (last.y+cur.y)/2;
	if ((mid.x==last.x || mid.x==cur.x) && (mid.y==last.y || mid.y==cur.y))
	    extrema[--i] = -1;
    }

    return i;
}

int Conic::findInflectionPoints (std::array<extended_t, 2> &poi) const {
    int cnt = 0;
    extended_t a, b, c, b2_fourac, t;
    /* GWW: A POI happens when d2 y/dx2 is zero. This is not the same as d2y/dt2 / d2x/dt2
     * d2 y/dx^2 = d/dt ( dy/dt / dx/dt ) / dx/dt
     *		 = ( (dx/dt) * d2 y/dt2 - ((dy/dt) * d2 x/dt2) )/ (dx/dt)^3
     * (3ax*t^2+2bx*t+cx) * (6ay*t+2by) - (3ay*t^2+2by*t+cy) * (6ax*t+2bx) == 0
     * (3ax*t^2+2bx*t+cx) * (3ay*t+by) - (3ay*t^2+2by*t+cy) * (3ax*t+bx) == 0
     *   9*ax*ay*t^3 + (3ax*by+6bx*ay)*t^2 + (2bx*by+3cx*ay)*t + cx*by
     * -(9*ax*ay*t^3 + (3ay*bx+6by*ax)*t^2 + (2by*bx+3cy*ax)*t + cy*bx)==0
     * 3*(ax*by-ay*bx)*t^2 + 3*(cx*ay-cy*ax)*t+ (cx*by-cy*bx) == 0	   */

    a = 3*((extended_t) conics[1].a*conics[0].b - (extended_t) conics[0].a*conics[1].b);
    b = 3*((extended_t) conics[0].c*conics[1].a - (extended_t) conics[1].c*conics[0].a);
    c =    (extended_t) conics[0].c*conics[1].b - (extended_t) conics[1].c*conics[0].b;
    if (!FontShepherd::math::realNear (a, 0)) {
	b2_fourac = b*b - 4*a*c;
	poi[0] = poi[1] = -1;
	if (b2_fourac<0)
	    return 0;
	b2_fourac = sqrt (b2_fourac);
	t = (-b+b2_fourac)/(2*a);
	if (t>=0 && t<=1.0)
	    poi[cnt++] = t;
	t = (-b-b2_fourac)/(2*a);
	if (t>=0 && t<=1.0) {
	    if (cnt==1 && poi[0]>t) {
		poi[1] = poi[0];
		poi[0] = t;
		++cnt;
	    } else
		poi[cnt++] = t;
	}
    } else if (!FontShepherd::math::realNear (b, 0)) {
	t = -c/b;
	if (t>=0 && t<=1.0)
	    poi[cnt++] = t;
    }
    if (cnt<2)
	poi[cnt] = -1;

    return cnt;
}

bool Conic::coincides (const Conic *s2) const {
    return (
	this->conics[0].a == s2->conics[0].a &&
	this->conics[0].b == s2->conics[0].b &&
	this->conics[0].c == s2->conics[0].c &&
	this->conics[0].d == s2->conics[0].d &&
	this->conics[1].a == s2->conics[1].a &&
	this->conics[1].b == s2->conics[1].b &&
	this->conics[1].c == s2->conics[1].c &&
	this->conics[1].d == s2->conics[1].d
    );
}

// GWW: returns 0=>no intersection, 1=>at least one, location in pts, t1s, t2s
// -1 => We couldn't figure it out in a closed form, have to do a numerical
// approximation
/* One extra for a trailing -1 */
int Conic::intersects (const Conic *s2, std::array<BasePoint, 9> &pts, std::array<extended_t, 10> &t1s, std::array<extended_t, 10> &t2s) const {
    BasePoint min1, max1, min2, max2;
    int soln = 0;
    extended_t x,y,t, ac0, ac1;
    int i, j, found;
    Conic1D spline;
    /* GWW: 3 solns for cubics, 4 for quartics */
    // AMK: tempts originally was extended[4], but iterateSolve takes extended[3], so why?
    std::array<extended_t, 3> tempts;
    // For findExtrema (): it needs extended[4]. Then copied to extrema{1,2}[1]
    std::array<extended_t, 4> textrema;
    std::array<extended_t, 6> extrema1, extrema2;
    int ecnt1, ecnt2;

    t1s[0] = t1s[1] = t1s[2] = t1s[3] = -1;
    t2s[0] = t2s[1] = t2s[2] = t2s[3] = -1;

    /* Linear and quadratics can't double back, can't self-intersect */
    if (this==s2 && (this->islinear || this->order2))
	return 0;
    /* Same spline. Intersects everywhere */
    else if (coincides (s2))
	return -1;

    /* Ignore splines which are just a point */
    if (this->islinear && this->conics[0].c==0 && this->conics[1].c==0)
	return 0;
    if (this->islinear && this->conics[0].c==0 && this->conics[1].c==0)
	return 0;

    if (this->islinear)
	/* Do Nothing */;
    else if (s2->islinear || (!this->order2 && s2->order2))
	return s2->intersects (this, pts, t2s, t1s);

    min1 = this->from->me; max1 = min1;
    min2 = s2->from->me; max2 = min2;
    if (this->from->nextcp.x>max1.x)
	max1.x = this->from->nextcp.x;
    else if (this->from->nextcp.x<min1.x)
	min1.x = this->from->nextcp.x;
    if (this->from->nextcp.y>max1.y)
	max1.y = this->from->nextcp.y;
    else if (this->from->nextcp.y<min1.y)
	min1.y = this->from->nextcp.y;
    if (this->to->prevcp.x>max1.x)
	max1.x = this->to->prevcp.x;
    else if (this->to->prevcp.x<min1.x)
	min1.x = this->to->prevcp.x;
    if (this->to->prevcp.y>max1.y)
	max1.y = this->to->prevcp.y;
    else if (this->to->prevcp.y<min1.y)
	min1.y = this->to->prevcp.y;
    if (this->to->me.x>max1.x)
	max1.x = this->to->me.x;
    else if (this->to->me.x<min1.x)
	min1.x = this->to->me.x;
    if (this->to->me.y>max1.y)
	max1.y = this->to->me.y;
    else if (this->to->me.y<min1.y)
	min1.y = this->to->me.y;

    if (s2->from->nextcp.x>max2.x)
	max2.x = s2->from->nextcp.x;
    else if (s2->from->nextcp.x<min2.x)
	min2.x = s2->from->nextcp.x;
    if (s2->from->nextcp.y>max2.y)
	max2.y = s2->from->nextcp.y;
    else if (s2->from->nextcp.y<min2.y)
	min2.y = s2->from->nextcp.y;
    if (s2->to->prevcp.x>max2.x)
	max2.x = s2->to->prevcp.x;
    else if (s2->to->prevcp.x<min2.x)
	min2.x = s2->to->prevcp.x;
    if (s2->to->prevcp.y>max2.y)
	max2.y = s2->to->prevcp.y;
    else if (s2->to->prevcp.y<min2.y)
	min2.y = s2->to->prevcp.y;
    if (s2->to->me.x>max2.x)
	max2.x = s2->to->me.x;
    else if (s2->to->me.x<min2.x)
	min2.x = s2->to->me.x;
    if (s2->to->me.y>max2.y)
	max2.y = s2->to->me.y;
    else if (s2->to->me.y<min2.y)
	min2.y = s2->to->me.y;
    /* no intersection of bounding boxes */
    if (min1.x>max2.x || min2.x>max1.x || min1.y>max2.y || min2.y>max1.y)
	return false;

    if (this->islinear) {
	spline.d =
	    this->conics[1].c*(s2->conics[0].d - this->conics[0].d)-
	    this->conics[0].c*(s2->conics[1].d - this->conics[1].d);
	spline.c =
	    this->conics[1].c*s2->conics[0].c - this->conics[0].c*s2->conics[1].c;
	spline.b = this->conics[1].c*s2->conics[0].b - this->conics[0].c*s2->conics[1].b;
	spline.a = this->conics[1].c*s2->conics[0].a - this->conics[0].c*s2->conics[1].a;
	spline.iterateSolve (tempts);
	if (tempts[0]==-1)
	    return false;
	for (i = 0; i<3 && tempts[i]!=-1; ++i) {
	    x = ((s2->conics[0].a*tempts[i]+s2->conics[0].b)*tempts[i]+
		s2->conics[0].c)*tempts[i]+s2->conics[0].d;
	    y = ((s2->conics[1].a*tempts[i]+s2->conics[1].b)*tempts[i]+
		s2->conics[1].c)*tempts[i]+s2->conics[1].d;
	    if (this->conics[0].c==0)
		x = this->conics[0].d;
	    if (this->conics[1].c==0)
		y = this->conics[1].d;
	    if ((ac0 = this->conics[0].c)<0 )
		ac0 = -ac0;
	    if ((ac1 = this->conics[1].c)<0 )
		ac1 = -ac1;
	    if (ac0>ac1)
		t = (x-this->conics[0].d)/this->conics[0].c;
	    else
		t = (y-this->conics[1].d)/this->conics[1].c;
	    if (tempts[i]>.99996 && closer (this, s2, t, tempts[i], t, 1)) {
		tempts[i] = 1;
		x = s2->to->me.x; y = s2->to->me.y;
	    } else if (tempts[i]<.00001 && closer (this, s2, t, tempts[i], t, 0)) {
		tempts[i] = 0;
		x = s2->from->me.x; y = s2->from->me.y;
	    }
	    /* GWW: I know we just did this, but we might have changed x,y so redo */
	    if (ac0>ac1)
		t = (x-this->conics[0].d)/this->conics[0].c;
	    else
		t = (y-this->conics[1].d)/this->conics[1].c;
	    if (t>.99996 && t<1.001 && closer (this, s2, t, tempts[i], 1, tempts[i])) {
		t = 1;
		x = this->to->me.x; y = this->to->me.y;
	    } else if ( t<.00001 && t>-.001 && closer (this, s2, t, tempts[i], 0, tempts[i])) {
		t = 0;
		x = this->from->me.x; y = this->from->me.y;
	    }
	    if (t<-.001 || t>1.001 || x<min1.x-.01 || y<min1.y-.01 || x>max1.x+.01 || y>max1.y+.01)
		continue;
	    if (t<=0) {
		t=0; x=this->from->me.x; y = this->from->me.y;
	    } else if (t>=1) {
		t=1; x=this->to->me.x; y = this->to->me.y;
	    }
	    /* GWW: Avoid rounding errors on hor/vert lines */
	    if (this->from->me.x==this->to->me.x)
		x = this->from->me.x;
	    else if (this->from->me.y==this->to->me.y)
		y = this->from->me.y;
	    if (s2->islinear) {
		if (s2->from->me.x==s2->to->me.x)
		    x = s2->from->me.x;
		else if (s2->from->me.y==s2->to->me.y)
		    y = s2->from->me.y;
	    }
	    soln = add_point (x, y, t, tempts[i], pts, t1s.data (), t2s.data (), soln);
	}
	return (soln!=0);
    }
    /* GWW: if one of the splines is quadratic then we can get an expression
     *  relating c*t+d to poly(s^3), and substituting this back we get
     *  a poly of degree 6 in s which could be solved iteratively
     * however mixed quadratics and cubics are unlikely

     * but if both splines are degree 3, the t is expressed as the sqrt of
     *  a third degree poly, which must be substituted into a cubic, and
     *  then squared to get rid of the sqrts leaving us with an ?18? degree
     *  poly. Ick. */

    /* GWW: So let's do it the hard way... we break the splines into little bits
     *  where they are monotonic in both dimensions, then check these for
     *  possible intersections */
    extrema1[0] = extrema2[0] = 0;
    ecnt1 = this->findExtrema (textrema);
    std::copy (std::begin (textrema), std::begin (textrema)+ecnt1, std::begin (extrema1)+1);
    ecnt2 = s2->findExtrema (textrema);
    std::copy (std::begin (textrema), std::begin (textrema)+ecnt2, std::begin (extrema2)+1);
    extrema1[++ecnt1] = 1.0;
    extrema2[++ecnt2] = 1.0;
    found=0;
    for (i=0; i<ecnt1; ++i) {
	min1.x = ((this->conics[0].a*extrema1[i]+this->conics[0].b)*extrema1[i]+
	    this->conics[0].c)*extrema1[i]+this->conics[0].d;
	min1.y = ((this->conics[1].a*extrema1[i]+this->conics[1].b)*extrema1[i]+
	    this->conics[1].c)*extrema1[i]+this->conics[1].d;
	max1.x = ((this->conics[0].a*extrema1[i+1]+this->conics[0].b)*extrema1[i+1]+
	    this->conics[0].c)*extrema1[i+1]+this->conics[0].d;
	max1.y = ((this->conics[1].a*extrema1[i+1]+this->conics[1].b)*extrema1[i+1]+
	    this->conics[1].c)*extrema1[i+1]+this->conics[1].d;
	if (max1.x<min1.x) {
	    extended_t temp = max1.x; max1.x = min1.x; min1.x = temp;
	}
	if (max1.y<min1.y) {
	    extended_t temp = max1.y; max1.y = min1.y; min1.y = temp;
	}
	for (j=(this==s2)?i+1:0; j<ecnt2; ++j) {
	    min2.x = ((s2->conics[0].a*extrema2[j]+s2->conics[0].b)*extrema2[j]+
		s2->conics[0].c)*extrema2[j]+s2->conics[0].d;
	    min2.y = ((s2->conics[1].a*extrema2[j]+s2->conics[1].b)*extrema2[j]+
		s2->conics[1].c)*extrema2[j]+s2->conics[1].d;
	    max2.x = ((s2->conics[0].a*extrema2[j+1]+s2->conics[0].b)*extrema2[j+1]+
		s2->conics[0].c)*extrema2[j+1]+s2->conics[0].d;
	    max2.y = ((s2->conics[1].a*extrema2[j+1]+s2->conics[1].b)*extrema2[j+1]+
		s2->conics[1].c)*extrema2[j+1]+s2->conics[1].d;
	    if (max2.x<min2.x) {
		extended_t temp = max2.x; max2.x = min2.x; min2.x = temp;
	    }
	    if (max2.y<min2.y) {
		extended_t temp = max2.y; max2.y = min2.y; min2.y = temp;
	    }
	    if (min1.x>max2.x || min2.x>max1.x || min1.y>max2.y || min2.y>max1.y)
		/* No possible intersection */;
	    else if (this!=s2)
		found += cubics_intersect (
		    this, extrema1[i], extrema1[i+1], &min1, &max1,
		    s2, extrema2[j], extrema2[j+1], &min2, &max2,
		    &pts[found], &t1s[found], &t2s[found], 9-found
		);
	    else {
		int k,l;
		int cnt = cubics_intersect (
		    this, extrema1[i], extrema1[i+1], &min1, &max1,
		    s2, extrema2[j], extrema2[j+1], &min2, &max2,
		    &pts[found], &t1s[found], &t2s[found], 9-found
		);
		for (k=0; k<cnt; ++k) {
		    if (realNear (t1s[found+k], t2s[found+k])) {
			for (l=k+1; l<cnt; ++l) {
			    pts[found+l-1] = pts[found+l];
			    t1s[found+l-1] = t1s[found+l];
			    t2s[found+l-1] = t2s[found+l];
			}
			--cnt; --k;
		    }
		}
		found += cnt;
	    }
	    if (found>=8) {
		/* GWW: If the splines are colinear then we might get an unbounded */
		/*  number of intersections */
		break;
	    }
	}
    }
    t1s[found] = t2s[found] = -1;
    return (found!=0);
}

/* GWW: calculating the actual length of a spline is hard, this gives a very */
/*  rough (but quick) approximation */
double Conic::lenApprox () const {
    double len, slen, temp;

    if ((temp = this->to->me.x-this->from->me.x)<0)
	temp = -temp;
    len = temp;
    if ((temp = this->to->me.y-this->from->me.y)<0)
	temp = -temp;
    len += temp;
    if (!this->to->noprevcp || !this->from->nonextcp) {
	if ((temp = this->from->nextcp.x-this->from->me.x)<0)
	    temp = -temp;
	slen = temp;
	if ((temp = this->from->nextcp.y-this->from->me.y)<0)
	    temp = -temp;
	slen += temp;
	if ((temp = this->to->prevcp.x-this->from->nextcp.x)<0)
	    temp = -temp;
	slen += temp;
	if ((temp = this->to->prevcp.y-this->from->nextcp.y)<0)
	    temp = -temp;
	slen += temp;
	if ((temp = this->to->me.x-this->to->prevcp.x)<0)
	    temp = -temp;
	slen += temp;
	if ((temp = this->to->me.y-this->to->prevcp.y)<0)
	    temp = -temp;
	slen += temp;
	len = (len + slen)/2;
    }
    return len;
}

double Conic::length () const {
    /* GWW: I ignore the constant term. It's just an unneeded addition */
    double len, t;
    double lastx = 0, lasty = 0;
    double curx, cury;

    len = 0;
    for (t=1.0/128; t<=1.0001 ; t+=1.0/128) {
	curx = ((this->conics[0].a*t+this->conics[0].b)*t+this->conics[0].c)*t;
	cury = ((this->conics[1].a*t+this->conics[1].b)*t+this->conics[1].c)*t;
	len += sqrt ((curx-lastx)*(curx-lastx) + (cury-lasty)*(cury-lasty));
	lastx = curx; lasty = cury;
    }
    return len;
}

std::vector<TPoint> Conic::figureTPsBetween (ConicPoint *from, ConicPoint *to) {
    int cnt, i, j, pcnt;
    double len, slen, lbase;
    ConicPoint *np;
    std::vector<TPoint> tp;
    std::vector<double> lens;
    std::vector<int> cnts;
    /* GWW: I used just to give every spline 10 points. But that gave much more */
    /*  weight to small splines than to big ones */

    cnt = 0;
    for (np = from->next->to; ; np = np->next->to) {
	++cnt;
	if (np==to)
	    break;
    }
    lens.resize (cnt);
    cnts.resize (cnt);
    cnt = 0; len = 0;
    for (np = from->next->to; ; np = np->next->to) {
	lens[cnt] = np->prev->lenApprox ();
	len += lens[cnt];
	++cnt;
	if (np==to)
	    break;
    }
    if (len!=0) {
	pcnt = 0;
	for (i=0; i<cnt; ++i) {
	    int pnts = rint ((10*cnt*lens[i])/len);
	    if (pnts<2) pnts = 2;
	    cnts[i] = pnts;
	    pcnt += pnts;
	}
    } else
	pcnt = 2*cnt;

    tp.resize (pcnt+1);
    if (len==0) {
	for (i=0; i<=pcnt; ++i) {
	    tp[i].t = i/(pcnt);
	    tp[i].x = from->me.x;
	    tp[i].y = from->me.y;
	}
    } else {
	lbase = 0;
	for (i=cnt=0, np = from->next->to; ; np = np->next->to, ++cnt) {
	    slen = np->prev->lenApprox ();
	    for (j=0; j<cnts[cnt]; ++j) {
		double t = j/(double) cnts[cnt];
		tp[i  ].t = (lbase+ t*slen)/len;
		tp[i  ].x = ((np->prev->conics[0].a*t+np->prev->conics[0].b)*t+np->prev->conics[0].c)*t + np->prev->conics[0].d;
		tp[i++].y = ((np->prev->conics[1].a*t+np->prev->conics[1].b)*t+np->prev->conics[1].c)*t + np->prev->conics[1].d;
	    }
	    lbase += slen;
	    if (np==to)
		break;
	}
	tp.resize (i);
    }

    return tp;
}

/* GWW: This routine should almost never be called now. It uses a flawed algorithm */
/*  which won't produce the best results. It gets called only when the better */
/*  approach doesn't work (singular matrices, etc.) */
/* Old comment, back when I was confused... */
/* Least squares tells us that:
	| S(xi*ti^3) |	 | S(ti^6) S(ti^5) S(ti^4) S(ti^3) |   | a |
	| S(xi*ti^2) | = | S(ti^5) S(ti^4) S(ti^3) S(ti^2) | * | b |
	| S(xi*ti)   |	 | S(ti^4) S(ti^3) S(ti^2) S(ti)   |   | c |
	| S(xi)	     |   | S(ti^3) S(ti^2) S(ti)   n       |   | d |
 and the definition of a spline tells us:
	| x1         | = |   1        1       1       1    | * (a b c d)
	| x0         | = |   0        0       0       1    | * (a b c d)
So we're a bit over specified. Let's use the last two lines of least squares
and the 2 from the spline defn. So d==x0. Now we've got three unknowns
and only three equations...

For order2 splines we've got
	| S(xi*ti^2) |	 | S(ti^4) S(ti^3) S(ti^2) |   | b |
	| S(xi*ti)   | = | S(ti^3) S(ti^2) S(ti)   | * | c |
	| S(xi)	     |   | S(ti^2) S(ti)   n       |   | d |
 and the definition of a spline tells us:
	| x1         | = |   1       1       1    | * (b c d)
	| x0         | = |   0       0       1    | * (b c d)
=>
    d = x0
    b+c = x1-x0
    S(ti^2)*b + S(ti)*c = S(xi)-n*x0
    S(ti^2)*b + S(ti)*(x1-x0-b) = S(xi)-n*x0
    [ S(ti^2)-S(ti) ]*b = S(xi)-S(ti)*(x1-x0) - n*x0
*/
bool Conic::_approximateFromPoints (ConicPoint *from, ConicPoint *to,
    std::vector<TPoint> &mid, BasePoint *nextcp, BasePoint *prevcp, bool order2) {
    double tt, ttn;
    int ret;
    double vx[3], vy[3], m[3][3];
    double ts[7], xts[4], yts[4];
    BasePoint nres = {0, 0}, pres = {0, 0};
    int nrescnt=0, prescnt=0;
    double nmin, nmax, pmin, pmax, test, ptest;
    double bx, by, cx, cy;

    /* Add the initial and end points */
    ts[0] = 2;
    for (int i=1; i<7; ++i)
	ts[i] = 1;
    xts[0] = from->me.x+to->me.x; yts[0] = from->me.y+to->me.y;
    xts[3] = xts[2] = xts[1] = to->me.x; yts[3] = yts[2] = yts[1] = to->me.y;
    nmin = pmin = 0;
    nmax = pmax = (to->me.x-from->me.x)*(to->me.x-from->me.x)+(to->me.y-from->me.y)*(to->me.y-from->me.y);
    for (size_t i=0; i<mid.size (); ++i) {
	xts[0] += mid[i].x;
	yts[0] += mid[i].y;
	++ts[0];
	tt = mid[i].t;
	xts[1] += tt*mid[i].x;
	yts[1] += tt*mid[i].y;
	ts[1] += tt;
	ts[2] += (ttn=tt*tt);
	xts[2] += ttn*mid[i].x;
	yts[2] += ttn*mid[i].y;
	ts[3] += (ttn*=tt);
	xts[3] += ttn*mid[i].x;
	yts[3] += ttn*mid[i].y;
	ts[4] += (ttn*=tt);
	ts[5] += (ttn*=tt);
	ts[6] += (ttn*=tt);

	test = (mid[i].x-from->me.x)*(to->me.x-from->me.x) + (mid[i].y-from->me.y)*(to->me.y-from->me.y);
	if ( test<nmin ) nmin=test;
	if ( test>nmax ) nmax=test;
	test = (mid[i].x-to->me.x)*(from->me.x-to->me.x) + (mid[i].y-to->me.y)*(from->me.y-to->me.y);
	if ( test<pmin ) pmin=test;
	if ( test>pmax ) pmax=test;
    }
    pmin *= 1.2; pmax *= 1.2; nmin *= 1.2; nmax *= 1.2;

    for (int j=0; j<3; ++j) {
	if (order2) {
	    if (realNear(ts[j+2],ts[j+1]))
		continue;
	    /* GWW: This produces really bad results!!!! But I don't see what I can do to improve it */
	    bx = (xts[j]-ts[j+1]*(to->me.x-from->me.x) - ts[j]*from->me.x) / (ts[j+2]-ts[j+1]);
	    by = (yts[j]-ts[j+1]*(to->me.y-from->me.y) - ts[j]*from->me.y) / (ts[j+2]-ts[j+1]);
	    cx = to->me.x-from->me.x-bx;
	    cy = to->me.y-from->me.y-by;

	    nextcp->x = from->me.x + cx/2;
	    nextcp->y = from->me.y + cy/2;
	    *prevcp = *nextcp;
	} else {
	    vx[0] = xts[j+1]-ts[j+1]*from->me.x;
	    vx[1] = xts[j]-ts[j]*from->me.x;
	    vx[2] = to->me.x-from->me.x;		/* always use the defn of spline */

	    vy[0] = yts[j+1]-ts[j+1]*from->me.y;
	    vy[1] = yts[j]-ts[j]*from->me.y;
	    vy[2] = to->me.y-from->me.y;

	    m[0][0] = ts[j+4]; m[0][1] = ts[j+3]; m[0][2] = ts[j+2];
	    m[1][0] = ts[j+3]; m[1][1] = ts[j+2]; m[1][2] = ts[j+1];
	    m[2][0] = 1;  m[2][1] = 1;  m[2][2] = 1;

	    /* Remove a terms from rows 0 and 1 */
	    vx[0] -= ts[j+4]*vx[2];
	    vy[0] -= ts[j+4]*vy[2];
	    m[0][0] = 0; m[0][1] -= ts[j+4]; m[0][2] -= ts[j+4];
	    vx[1] -= ts[j+3]*vx[2];
	    vy[1] -= ts[j+3]*vy[2];
	    m[1][0] = 0; m[1][1] -= ts[j+3]; m[1][2] -= ts[j+3];

	    if (fabs(m[1][1]) < fabs(m[0][1])) {
		double temp;
		temp = vx[1]; vx[1] = vx[0]; vx[0] = temp;
		temp = vy[1]; vy[1] = vy[0]; vy[0] = temp;
		temp = m[1][1]; m[1][1] = m[0][1]; m[0][1] = temp;
		temp = m[1][2]; m[1][2] = m[0][2]; m[0][2] = temp;
	    }
	    /* remove b terms from rows 0 and 2 (first normalize row 1 so m[1][1] is 1*/
	    vx[1] /= m[1][1];
	    vy[1] /= m[1][1];
	    m[1][2] /= m[1][1];
	    m[1][1] = 1;
	    vx[0] -= m[0][1]*vx[1];
	    vy[0] -= m[0][1]*vy[1];
	    m[0][2] -= m[0][1]*m[1][2]; m[0][1] = 0;
	    vx[2] -= m[2][1]*vx[1];
	    vy[2] -= m[2][1]*vy[1];
	    m[2][2] -= m[2][1]*m[1][2]; m[2][1] = 0;

	    vx[0] /= m[0][2];			/* This is cx */
	    vy[0] /= m[0][2];			/* This is cy */
	    /*m[0][2] = 1;*/

	    vx[1] -= m[1][2]*vx[0];		/* This is bx */
	    vy[1] -= m[1][2]*vy[0];		/* This is by */
	    /* m[1][2] = 0; */
	    vx[2] -= m[2][2]*vx[0];		/* This is ax */
	    vy[2] -= m[2][2]*vy[0];		/* This is ay */
	    /* m[2][2] = 0; */

	    nextcp->x = from->me.x + vx[0]/3;
	    nextcp->y = from->me.y + vy[0]/3;
	    prevcp->x = nextcp->x + (vx[0]+vx[1])/3;
	    prevcp->y = nextcp->y + (vy[0]+vy[1])/3;
	}

	test =  (nextcp->x-from->me.x)*(to->me.x-from->me.x) +
		(nextcp->y-from->me.y)*(to->me.y-from->me.y);
	ptest = (prevcp->x-to->me.x)*(from->me.x-to->me.x) +
		(prevcp->y-to->me.y)*(from->me.y-to->me.y);
	if (order2 && (test<nmin || test>nmax || ptest<pmin || ptest>pmax))
	    continue;
	if (test>=nmin && test<=nmax) {
	    nres.x += nextcp->x; nres.y += nextcp->y;
	    ++nrescnt;
	}
	if (test>=pmin && test<=pmax) {
	    pres.x += prevcp->x; pres.y += prevcp->y;
	    ++prescnt;
	}
	if (nrescnt==1 && prescnt==1)
	    break;
    }

    ret = 0;
    if (nrescnt>0) {
	ret |= 1;
	nextcp->x = nres.x/nrescnt;
	nextcp->y = nres.y/nrescnt;
    } else
	*nextcp = from->nextcp;
    if (prescnt>0) {
	ret |= 2;
	prevcp->x = pres.x/prescnt;
	prevcp->y = pres.y/prescnt;
    } else
	*prevcp = to->prevcp;
    if (order2 && ret!=3) {
	nextcp->x = (nextcp->x + prevcp->x)/2;
	nextcp->y = (nextcp->y + prevcp->y)/2;
    }
    if (order2)
	*prevcp = *nextcp;
    return ret;
}

Conic *Conic::isLinearApprox (ConicPoint *from, ConicPoint *to, std::vector<TPoint> &mid, bool order2) {
    double vx, vy, slope;

    vx = to->me.x-from->me.x; vy = to->me.y-from->me.y;
    if (vx==0 && vy==0) {
	for (size_t i=0; i<mid.size (); ++i)
	    if (mid[i].x != from->me.x || mid[i].y != from->me.y)
		return nullptr;
    } else if (fabs(vx)>fabs(vy)) {
	slope = vy/vx;
	for (size_t i=0; i<mid.size (); ++i)
	    if (!realWithin (mid[i].y, from->me.y+slope*(mid[i].x-from->me.x), .7))
		return nullptr;
    } else {
	slope = vx/vy;
	for (size_t i=0; i<mid.size (); ++i)
	    if (!realWithin (mid[i].x, from->me.x+slope*(mid[i].y-from->me.y), .7))
		return nullptr;
    }
    from->nonextcp = to->noprevcp = true;
    return new Conic (from, to, order2);
}

/* GWW: Find a spline which best approximates the list of intermediate points we */
/*  are given. No attempt is made to use fixed slope angles */
/* given a set of points (x,y,t) */
/* find the bezier spline which best fits those points */

/* GWW: OK, we know the end points, so all we really need are the control points */
  /*    For cubics.... */
/* Pf = point from */
/* CPf = control point, from nextcp */
/* CPt = control point, to prevcp */
/* Pt = point to */
/* S(t) = Pf + 3*(CPf-Pf)*t + 3*(CPt-2*CPf+Pf)*t^2 + (Pt-3*CPt+3*CPf-Pf)*t^3 */
/* S(t) = Pf - 3*Pf*t + 3*Pf*t^2 - Pf*t^3 + Pt*t^3 +                         */
/*           3*(t-2*t^2+t^3)*CPf +                                           */
/*           3*(t^2-t^3)*CPt                                                 */
/* We want to minimize Σ [S(ti)-Pi]^2 */
/* There are four variables CPf.x, CPf.y, CPt.x, CPt.y */
/* When we take the derivative of the error term above with each of these */
/*  variables, we find that the two coordinates are separate. So I shall only */
/*  work through the equations once, leaving off the coordinate */
/* d error/dCPf = Σ 2*3*(t-2*t^2+t^3) * [S(ti)-Pi] = 0 */
/* d error/dCPt = Σ 2*3*(t^2-t^3)     * [S(ti)-Pi] = 0 */
  /*    For quadratics.... */
/* CP = control point, there's only one */
/* S(t) = Pf + 2*(CP-Pf)*t + (Pt-2*CP+Pf)*t^2 */
/* S(t) = Pf - 2*Pf*t + Pf*t^2 + Pt*t^2 +     */
/*           2*(t-2*t^2)*CP                   */
/* We want to minimize Σ [S(ti)-Pi]^2 */
/* There are two variables CP.x, CP.y */
/* d error/dCP = Σ 2*2*(t-2*t^2) * [S(ti)-Pi] = 0 */
/* Σ (t-2*t^2) * [Pf - 2*Pf*t + Pf*t^2 + Pt*t^2 - Pi +     */
/*           2*(t-2*t^2)*CP] = 0               */
/* CP * (Σ 2*(t-2*t^2)*(t-2*t^2)) = Σ (t-2*t^2) * [Pf - 2*Pf*t + Pf*t^2 + Pt*t^2 - Pi] */

/*        Σ (t-2*t^2) * [Pf - 2*Pf*t + Pf*t^2 + Pt*t^2 - Pi] */
/* CP = ----------------------------------------------------- */
/*                    Σ 2*(t-2*t^2)*(t-2*t^2)                */
Conic *Conic::approximateFromPoints (ConicPoint *from, ConicPoint *to, std::vector<TPoint> &mid, bool order2) {
    int ret;
    Conic *spline;
    BasePoint nextcp, prevcp;

    if (order2) {
	double xconst, yconst, term /* Same for x and y */;
	xconst = yconst = term = 0;
	for (size_t i=0; i<mid.size (); ++i) {
	    double t = mid[i].t, t2 = t*t;
	    double tfactor = (t-2*t2);
	    term += 2*tfactor*tfactor;
	    xconst += tfactor*(from->me.x*(1-2*t+t2) + to->me.x*t2 - mid[i].x);
	    yconst += tfactor*(from->me.y*(1-2*t+t2) + to->me.y*t2 - mid[i].y);
	}
	if (term!=0) {
	    BasePoint cp;
	    cp.x = xconst/term; cp.y = yconst/term;
	    from->nextcp = to->prevcp = cp;
	    return new Conic (from,to, true);
	}
    } else {
	double xconst[2], yconst[2], f_term[2], t_term[2] /* Same for x and y */;
	double tfactor[2], determinant;
	xconst[0] = xconst[1] = yconst[0] = yconst[1] =
	    f_term[0] = f_term[1] = t_term[0] = t_term[1] =  0;
	for (size_t i=0; i<mid.size (); ++i) {
	    double t = mid[i].t, t2 = t*t, t3=t*t2;
	    double xc = (from->me.x*(1-3*t+3*t2-t3) + to->me.x*t3 - mid[i].x);
	    double yc = (from->me.y*(1-3*t+3*t2-t3) + to->me.y*t3 - mid[i].y);
	    tfactor[0] = (t-2*t2+t3); tfactor[1]=(t2-t3);
	    xconst[0] += tfactor[0]*xc;
	    xconst[1] += tfactor[1]*xc;
	    yconst[0] += tfactor[0]*yc;
	    yconst[1] += tfactor[1]*yc;
	    f_term[0] += 3*tfactor[0]*tfactor[0];
	    f_term[1] += 3*tfactor[0]*tfactor[1];
	    /*t_term[0] += 3*tfactor[1]*tfactor[0];*/
	    t_term[1] += 3*tfactor[1]*tfactor[1];
	}
	t_term[0] = f_term[1];
	determinant = f_term[1]*t_term[0] - f_term[0]*t_term[1];
	if (determinant!=0) {
	    to->prevcp.x = -(xconst[0]*f_term[1]-xconst[1]*f_term[0])/determinant;
	    to->prevcp.y = -(yconst[0]*f_term[1]-yconst[1]*f_term[0])/determinant;
	    if (f_term[0]!=0) {
		from->nextcp.x = (-xconst[0]-t_term[0]*to->prevcp.x)/f_term[0];
		from->nextcp.y = (-yconst[0]-t_term[0]*to->prevcp.y)/f_term[0];
	    } else {
		from->nextcp.x = (-xconst[1]-t_term[1]*to->prevcp.x)/f_term[1];
		from->nextcp.y = (-yconst[1]-t_term[1]*to->prevcp.y)/f_term[1];
	    }
	    to->noprevcp = from->nonextcp = false;
	    return new Conic (from, to, false);
	}
    }

    if ((spline = Conic::isLinearApprox (from, to, mid, order2))!=nullptr)
	return spline;

    ret = Conic::_approximateFromPoints (from, to, mid, &nextcp, &prevcp, order2);

    if (ret&1) {
	from->nextcp = nextcp;
	from->nonextcp = false;
    } else {
	from->nextcp = from->me;
	from->nonextcp = true;
    }
    if (ret&2) {
	to->prevcp = prevcp;
	to->noprevcp = false;
    } else {
	to->prevcp = to->me;
	to->noprevcp = true;
    }
    spline = new Conic (from,to,order2);
    spline->testForLinear ();
    return spline;
}

/* pf == point from (start point) */
/* Δf == slope from (cp(from) - from) */
/* pt == point to (end point, t==1) */
/* Δt == slope to (cp(to) - to) */

/* A spline from pf to pt with slope vectors rf*Δf, rt*Δt is: */
/* p(t) = pf +  [ 3*rf*Δf ]*t  +  3*[pt-pf+rt*Δt-2*rf*Δf] *t^2 +			*/
/*		[2*pf-2*pt+3*rf*Δf-3*rt*Δt]*t^3 */

/* So I want */
/*   d  Σ (p(t(i))-p(i))^2/ d rf  == 0 */
/*   d  Σ (p(t(i))-p(i))^2/ d rt  == 0 */
/* now... */
/*   d  Σ (p(t(i))-p(i))^2/ d rf  == 0 */
/* => Σ 3*t*Δf*(1-2*t+t^2)*
 *			[pf-pi+ 3*(pt-pf)*t^2 + 2*(pf-pt)*t^3]   +
 *			3*[t - 2*t^2 + t^3]*Δf*rf   +
 *			3*[t^2-t^3]*Δt*rt   */
/* and... */
/*   d  Σ (p(t(i))-p(i))^2/ d rt  == 0 */
/* => Σ 3*t^2*Δt*(1-t)*
 *			[pf-pi+ 3*(pt-pf)*t^2 + 2*(pf-pt)*t^3]   +
 *			3*[t - 2*t^2 + t^3]*Δf*rf   +
 *			3*[t^2-t^3]*Δt*rt   */

/* Now for a long time I looked at that and saw four equations and two unknowns*/
/*  That was I was trying to solve for x and y separately, and that doesn't work. */
/*  There are really just two equations and each sums over both x and y components */

/* Old comment: */
/* I used to do a least squares aproach adding two more to the above set of equations */
/*  which held the slopes constant. But that didn't work very well. So instead*/
/*  Then I tried doing the approximation, and then forcing the control points */
/*  to be in line (witht the original slopes), getting a better approximation */
/*  to "t" for each data point and then calculating an error array, approximating*/
/*  it, and using that to fix up the final result */
/* Then I tried checking various possible cp lengths in the desired directions*/
/*  finding the best one or two, and doing a 2D binary search using that as a */
/*  starting point. */
/* And sometimes a least squares approach will give us the right answer, so   */
/*  try that too. */
/* This still isn't as good as I'd like it... But I haven't been able to */
/*  improve it further yet */
#define TRY_CNT		2
#define DECIMATION	5
Conic *Conic::approximateFromPointsSlopes (ConicPoint *from, ConicPoint *to, std::vector<TPoint> &mid, bool order2) {
    BasePoint tounit, fromunit, ftunit;
    double flen, tlen, ftlen, dot;
    Conic *spline, temp;
    BasePoint nextcp;
    int bettern, betterp;
    double offn, offp, incrn, incrp, trylen;
    int nocnt = 0, totcnt;
    double curdiff, bestdiff[TRY_CNT];
    int besti[TRY_CNT], bestj[TRY_CNT];
    double fdiff, tdiff, fmax, tmax, fdotft, tdotft;
    DBounds b;
    struct dotbounds db;
    double offn_, offp_, finaldiff;
    double pt_pf_x, pt_pf_y, determinant;
    double consts[2], rt_terms[2], rf_terms[2];
    Conic *ret;

    size_t cnt = mid.size ();

    /* If all the selected points are at the same spot, and one of the */
    /*  end-points is also at that spot, then just copy the control point */
    /* But our caller seems to have done that for us */

    /* If the two end-points are corner points then allow the slope to vary */
    /* Or if one end-point is a tangent but the point defining the tangent's */
    /*  slope is being removed then allow the slope to vary */
    /* Except if the slope is horizontal or vertical then keep it fixed */
    if ((!from->nonextcp && (from->nextcp.x==from->me.x || from->nextcp.y==from->me.y)) ||
	(!to->noprevcp && (to->prevcp.x==to->me.x || to->prevcp.y==to->me.y)))
	/* Preserve the slope */;
    else if (
	((from->pointtype == pt_corner && from->nonextcp) ||
	    (from->pointtype == pt_tangent && ((from->nonextcp && from->noprevcp) || !from->noprevcp))) &&
	((to->pointtype == pt_corner && to->noprevcp) || (to->pointtype == pt_tangent &&
	    ((to->nonextcp && to->noprevcp) || !to->nonextcp)))
	) {
	from->pointtype = to->pointtype = pt_corner;
	return Conic::approximateFromPoints (from, to, mid, order2);
    }

    /* If we are going to honour the slopes of a quadratic spline, there is */
    /*  only one possibility */
    if (order2) {
	if (from->nonextcp)
	    from->nextcp = from->next->to->me;
	if (to->noprevcp)
	    to->prevcp = to->prev->from->me;
	from->nonextcp = to->noprevcp = false;
	fromunit.x = from->nextcp.x-from->me.x; fromunit.y = from->nextcp.y-from->me.y;
	tounit.x = to->prevcp.x-to->me.x; tounit.y = to->prevcp.y-to->me.y;

	if (!intersect_lines (&nextcp,&from->nextcp,&from->me,&to->prevcp,&to->me) ||
	    (nextcp.x-from->me.x)*fromunit.x + (nextcp.y-from->me.y)*fromunit.y < 0 ||
	    (nextcp.x-to->me.x)*tounit.x + (nextcp.y-to->me.y)*tounit.y < 0 ) {
	    /* If the slopes don't intersect then use a line */
	    /*  (or if the intersection is patently absurd) */
	    from->nonextcp = to->noprevcp = true;
	    from->nextcp = from->me;
	    to->prevcp = to->me;
	    ret = new Conic (from, to, true);
	    ret->testForLinear ();
	} else {
	    from->nextcp = to->prevcp = nextcp;
	    from->nonextcp = to->noprevcp = false;
	    ret = new Conic (from, to, true);
	}
	return ret;
    }
    /* From here down we are only working with cubic splines */

    if (cnt==0) {
	/* Just use what we've got, no data to improve it */
	/* But we do sometimes get some cps which are too big */
	double len = sqrt((to->me.x-from->me.x)*(to->me.x-from->me.x) + (to->me.y-from->me.y)*(to->me.y-from->me.y));
	if (len==0) {
	    from->nonextcp = to->noprevcp = true;
	    from->nextcp = from->me;
	    to->prevcp = to->me;
	} else {
	    BasePoint noff, poff;
	    double nlen, plen;
	    noff.x = from->nextcp.x-from->me.x; noff.y = from->nextcp.y-from->me.y;
	    poff.x = to->me.x-to->prevcp.x; poff.y = to->me.y-to->prevcp.y;
	    nlen = sqrt(noff.x*noff.x + noff.y+noff.y);
	    plen = sqrt(poff.x*poff.x + poff.y+poff.y);
	    if (nlen>len/3) {
		noff.x *= len/3/nlen; noff.y *= len/3/nlen;
		from->nextcp.x = from->me.x + noff.x;
		from->nextcp.y = from->me.y + noff.y;
	    }
	    if (plen>len/3) {
		poff.x *= len/3/plen; poff.y *= len/3/plen;
		to->prevcp.x = to->me.x + poff.x;
		to->prevcp.y = to->me.y + poff.y;
	    }
	}
	return new Conic (from, to, false);
    }

    if (to->prev && (( to->noprevcp && to->nonextcp ) || to->prev->islinear)) {
	tounit.x = to->prev->from->me.x-to->me.x; tounit.y = to->prev->from->me.y-to->me.y;
    } else if (!to->noprevcp || to->pointtype == pt_corner) {
	tounit.x = to->prevcp.x-to->me.x; tounit.y = to->prevcp.y-to->me.y;
    } else {
	tounit.x = to->me.x-to->nextcp.x; tounit.y = to->me.y-to->nextcp.y;
    }
    tlen = sqrt (tounit.x*tounit.x + tounit.y*tounit.y);
    if (from->next && (( from->noprevcp && from->nonextcp ) || from->next->islinear) ) {
	fromunit.x = from->next->to->me.x-from->me.x; fromunit.y = from->next->to->me.y-from->me.y;
    } else if ( !from->nonextcp || from->pointtype == pt_corner ) {
	fromunit.x = from->nextcp.x-from->me.x; fromunit.y = from->nextcp.y-from->me.y;
    } else {
	fromunit.x = from->me.x-from->prevcp.x; fromunit.y = from->me.y-from->prevcp.y;
    }
    flen = sqrt(fromunit.x*fromunit.x + fromunit.y*fromunit.y);
    if (tlen==0 || flen==0) {
	if (from->next)
	    temp = *from->next;
	else {
	    temp.from = from; temp.to = to;
	    temp.refigure ();
	    from->next = to->prev = NULL;
	}
    }
    if (tlen==0) {
	if (to->pointtype==pt_curve && to->next && !to->nonextcp) {
	    tounit.x = to->me.x-to->nextcp.x; tounit.y = to->me.y-to->nextcp.y;
	} else {
	    tounit.x = -( (3*temp.conics[0].a*.9999+2*temp.conics[0].b)*.9999+temp.conics[0].c );
	    tounit.y = -( (3*temp.conics[1].a*.9999+2*temp.conics[1].b)*.9999+temp.conics[1].c );
	}
	tlen = sqrt(tounit.x*tounit.x + tounit.y*tounit.y);
    }
    tounit.x /= tlen; tounit.y /= tlen;

    if (flen==0) {
	if ((from->pointtype==pt_curve) &&
	    from->prev && !from->noprevcp) {
	    fromunit.x = from->me.x-from->prevcp.x; fromunit.y = from->me.y-from->prevcp.y;
	} else {
	    fromunit.x = ( (3*temp.conics[0].a*.0001+2*temp.conics[0].b)*.0001+temp.conics[0].c );
	    fromunit.y = ( (3*temp.conics[1].a*.0001+2*temp.conics[1].b)*.0001+temp.conics[1].c );
	}
	flen = sqrt(fromunit.x*fromunit.x + fromunit.y*fromunit.y);
    }
    fromunit.x /= flen; fromunit.y /= flen;

    ftunit.x = (to->me.x-from->me.x); ftunit.y = (to->me.y-from->me.y);
    ftlen = sqrt (ftunit.x*ftunit.x + ftunit.y*ftunit.y);
    if (ftlen!=0) {
	ftunit.x /= ftlen; ftunit.y /= ftlen;
    }

    if ((dot=fromunit.x*tounit.y - fromunit.y*tounit.x)<.0001 && dot>-.0001 &&
	(dot=ftunit.x*tounit.y - ftunit.y*tounit.x)<.0001 && dot>-.0001 ) {
	/* It's a line. Slopes are parallel, and parallel to vector between (from,to) */
	from->nonextcp = to->noprevcp = true;
	from->nextcp = from->me; to->prevcp = to->me;
	return new Conic (from, to, false);
    }

    pt_pf_x = to->me.x - from->me.x;
    pt_pf_y = to->me.y - from->me.y;
    consts[0] = consts[1] = rt_terms[0] = rt_terms[1] = rf_terms[0] = rf_terms[1] = 0;
    for (size_t i=0; i<cnt; ++i) {
	double t = mid[i].t, t2 = t*t, t3=t2*t;
	double factor_from = t-2*t2+t3;
	double factor_to = t2-t3;
	double const_x = from->me.x-mid[i].x + 3*pt_pf_x*t2 - 2*pt_pf_x*t3;
	double const_y = from->me.y-mid[i].y + 3*pt_pf_y*t2 - 2*pt_pf_y*t3;
	double temp1 = 3*(t-2*t2+t3);
	double rf_term_x = temp1*fromunit.x;
	double rf_term_y = temp1*fromunit.y;
	double temp2 = 3*(t2-t3);
	double rt_term_x = -temp2*tounit.x;
	double rt_term_y = -temp2*tounit.y;

	consts[0] += factor_from*( fromunit.x*const_x + fromunit.y*const_y );
	consts[1] += factor_to *( -tounit.x*const_x + -tounit.y*const_y);
	rf_terms[0] += factor_from*( fromunit.x*rf_term_x + fromunit.y*rf_term_y);
	rf_terms[1] += factor_to*( -tounit.x*rf_term_x + -tounit.y*rf_term_y);
	rt_terms[0] += factor_from*( fromunit.x*rt_term_x + fromunit.y*rt_term_y);
	rt_terms[1] += factor_to*( -tounit.x*rt_term_x + -tounit.y*rt_term_y);
    }

 /* GWW: I've only seen singular matrices (determinant==0) when cnt==1 */
 /* but even with cnt==1 the determinant is usually non-0 (16 times out of 17)*/
    determinant = (rt_terms[0]*rf_terms[1]-rt_terms[1]*rf_terms[0]);
    if (determinant!=0) {
	double rt, rf;
	rt = (consts[1]*rf_terms[0]-consts[0]*rf_terms[1])/determinant;
	if ( rf_terms[0]!=0 )
	    rf = -(consts[0]+rt*rt_terms[0])/rf_terms[0];
	else /* if ( rf_terms[1]!=0 ) GWW: This can't happen, otherwise the determinant would be 0 */
	    rf = -(consts[1]+rt*rt_terms[1])/rf_terms[1];
	/* GWW: If we get bad values (ones that point diametrically opposed to what*/
	/*  we need), then fix that factor at 0, and see what we get for the */
	/*  other */
	if (rf>=0 && rt>0 && rf_terms[0]!=0 && (rf = -consts[0]/rf_terms[0])>0) {
	    rt = 0;
	} else if ( rf<0 && rt<=0 && rt_terms[1]!=0 && (rt = -consts[1]/rt_terms[1])<0 ) {
	    rf = 0;
	}
	if (rt<=0 && rf>=0) {
	    from->nextcp.x = from->me.x + rf*fromunit.x;
	    from->nextcp.y = from->me.y + rf*fromunit.y;
	    to->prevcp.x = to->me.x - rt*tounit.x;
	    to->prevcp.y = to->me.y - rt*tounit.y;
	    from->nonextcp = rf==0;
	    to->noprevcp = rt==0;
	    return new Conic (from, to, false);
	}
    }

    trylen = (to->me.x-from->me.x)*fromunit.x + (to->me.y-from->me.y)*fromunit.y;
    if (trylen>flen) flen = trylen;

    trylen = (from->me.x-to->me.x)*tounit.x + (from->me.y-to->me.y)*tounit.y;
    if (trylen>tlen) tlen = trylen;

    for (size_t i=0; i<cnt; ++i) {
	trylen = (mid[i].x-from->me.x)*fromunit.x + (mid[i].y-from->me.y)*fromunit.y;
	if ( trylen>flen ) flen = trylen;
	trylen = (mid[i].x-to->me.x)*tounit.x + (mid[i].y-to->me.y)*tounit.y;
	if ( trylen>tlen ) tlen = trylen;
    }

    fdotft = fromunit.x*ftunit.x + fromunit.y*ftunit.y;
    fmax = fdotft>0 ? ftlen/fdotft : 1e10;
    tdotft = -tounit.x*ftunit.x - tounit.y*ftunit.y;
    tmax = tdotft>0 ? ftlen/tdotft : 1e10;
    /* At fmax, tmax the control points will stretch beyond the other endpoint*/
    /*  when projected along the line between the two endpoints */

    db.base = from->me;
    db.unit = ftunit;
    db.len = ftlen;
    approx_bounds (&b, mid, &db);

    for (int k=0; k<TRY_CNT; ++k) {
	bestdiff[k] = 1e20;
	besti[k] = -1; bestj[k] = -1;
    }
    fdiff = flen/DECIMATION;
    tdiff = tlen/DECIMATION;
    from->nextcp = from->me;
    from->nonextcp = false;
    to->noprevcp = false;
    temp.from = from; temp.to = to;
    for (int i=1; i<DECIMATION; ++i) {
	from->nextcp.x += fdiff*fromunit.x; from->nextcp.y += fdiff*fromunit.y;
	to->prevcp = to->me;
	for (int j=1; j<DECIMATION; ++j) {
	    to->prevcp.x += tdiff*tounit.x; to->prevcp.y += tdiff*tounit.y;
	    temp.refigure ();
	    curdiff = temp.sigmaDeltas (mid, &b, &db);
	    for (int k=0; k<TRY_CNT; ++k) {
		if (curdiff<bestdiff[k]) {
		    for (int l=TRY_CNT-1; l>k; --l) {
			bestdiff[l] = bestdiff[l-1];
			besti[l] = besti[l-1];
			bestj[l] = bestj[l-1];
		    }
		    bestdiff[k] = curdiff;
		    besti[k] = i; bestj[k]=j;
		    break;
		}
	    }
	}
    }

    finaldiff = 1e20;
    offn_ = offp_ = -1;
    spline = new Conic (from, to, false);
    for (int k=-1; k<TRY_CNT; ++k) {
	if (k<0) {
	    BasePoint nextcp, prevcp;
	    double temp1, temp2;
	    int ret = Conic::_approximateFromPoints (from, to, mid, &nextcp, &prevcp, false);
	    /* sometimes least squares gives us the right answer */
	    if (!(ret&1) || !(ret&2))
		continue;
	    temp1 = (prevcp.x-to->me.x)*tounit.x + (prevcp.y-to->me.y)*tounit.y;
	    temp2 = (nextcp.x-from->me.x)*fromunit.x + (nextcp.y-from->me.y)*fromunit.y;
	    if (temp1<=0 || temp2<=0)		/* A nice solution... but the control points are diametrically opposed to what they should be */
		continue;
	    tlen = temp1; flen = temp2;
	} else {
	    if ( bestj[k]<0 || besti[k]<0 )
		continue;
	    tlen = bestj[k]*tdiff; flen = besti[k]*fdiff;
	}
	to->prevcp.x = to->me.x + tlen*tounit.x; to->prevcp.y = to->me.y + tlen*tounit.y;
	from->nextcp.x = from->me.x + flen*fromunit.x; from->nextcp.y = from->me.y + flen*fromunit.y;
	spline->refigure ();

	bettern = betterp = false;
	incrn = tdiff/2.0; incrp = fdiff/2.0;
	offn = flen; offp = tlen;
	nocnt = 0;
	curdiff = spline->sigmaDeltas (mid, &b, &db);
	totcnt = 0;
	while (true) {
	    double fadiff, fsdiff;
	    double tadiff, tsdiff;

	    from->nextcp.x = from->me.x + (offn+incrn)*fromunit.x; from->nextcp.y = from->me.y + (offn+incrn)*fromunit.y;
	    to->prevcp.x = to->me.x + offp*tounit.x; to->prevcp.y = to->me.y + offp*tounit.y;
	    spline->refigure ();
	    fadiff = spline->sigmaDeltas (mid, &b, &db);
	    from->nextcp.x = from->me.x + (offn-incrn)*fromunit.x; from->nextcp.y = from->me.y + (offn-incrn)*fromunit.y;
	    spline->refigure ();
	    fsdiff = spline->sigmaDeltas (mid, &b, &db);
	    from->nextcp.x = from->me.x + offn*fromunit.x; from->nextcp.y = from->me.y + offn*fromunit.y;
	    if (offn-incrn<=0)
		fsdiff += 1e10;

	    to->prevcp.x = to->me.x + (offp+incrp)*tounit.x; to->prevcp.y = to->me.y + (offp+incrp)*tounit.y;
	    spline->refigure ();
	    tadiff = spline->sigmaDeltas (mid, &b, &db);
	    to->prevcp.x = to->me.x + (offp-incrp)*tounit.x; to->prevcp.y = to->me.y + (offp-incrp)*tounit.y;
	    spline->refigure ();
	    tsdiff = spline->sigmaDeltas (mid, &b, &db);
	    to->prevcp.x = to->me.x + offp*tounit.x; to->prevcp.y = to->me.y + offp*tounit.y;
	    if (offp-incrp<=0)
		tsdiff += 1e10;

	    if (offn>=incrn && fsdiff<curdiff && (fsdiff<fadiff && fsdiff<tsdiff && fsdiff<tadiff)) {
		offn -= incrn;
		if (bettern>0)
		    incrn /= 2;
		bettern = -1;
		nocnt = 0;
		curdiff = fsdiff;
	    } else if (offn+incrn<fmax && fadiff<curdiff &&
		(fadiff<=fsdiff && fadiff<tsdiff && fadiff<tadiff)) {
		offn += incrn;
		if ( bettern<0 )
		    incrn /= 2;
		bettern = 1;
		nocnt = 0;
		curdiff = fadiff;
	    } else if (offp>=incrp && tsdiff<curdiff &&
		(tsdiff<=fsdiff && tsdiff<=fadiff && tsdiff<tadiff)) {
		offp -= incrp;
		if ( betterp>0 )
		    incrp /= 2;
		betterp = -1;
		nocnt = 0;
		curdiff = tsdiff;
	    } else if (offp+incrp<tmax && tadiff<curdiff &&
		(tadiff<=fsdiff && tadiff<=fadiff && tadiff<=tsdiff)) {
		offp += incrp;
		if (betterp<0)
		    incrp /= 2;
		betterp = 1;
		nocnt = 0;
		curdiff = tadiff;
	    } else {
		if (++nocnt > 6)
		    break;
		incrn /= 2;
		incrp /= 2;
	    }
	    if (curdiff<1)
		break;
	    if (incrp<tdiff/1024 || incrn<fdiff/1024)
		break;
	    if (++totcnt>200)
		break;
	    assert (offn>= 0 && offp>=0);
	}
	if (curdiff<finaldiff) {
	    finaldiff = curdiff;
	    offn_ = offn;
	    offp_ = offp;
	}
    }

    to->noprevcp = offp_==0;
    from->nonextcp = offn_==0;
    to->prevcp.x = to->me.x + offp_*tounit.x; to->prevcp.y = to->me.y + offp_*tounit.y;
    from->nextcp.x = from->me.x + offn_*fromunit.x; from->nextcp.y = from->me.y + offn_*fromunit.y;
    /* GWW: I used to check for a spline very close to linear (and if so, make it */
    /*  linear). But in when stroking a path with an eliptical pen we transform*/
    /*  the coordinate system and our normal definitions of "close to linear" */
    /*  don't apply */
    /*TestForLinear(from,to);*/
    spline->refigure ();

    return spline;
}
#undef TRY_CNT
#undef DECIMATION

double Conic::sigmaDeltas (std::vector<TPoint> &mid, DBounds *b, struct dotbounds *db) {
    double xdiff, ydiff, sum, temp, t;
    extended_t ts[2], x,y;
    struct dotbounds db2;
    double dot;
    int near_vert, near_horiz;

    if ((xdiff = to->me.x-from->me.x)<0) xdiff = -xdiff;
    if ((ydiff = to->me.y-from->me.y)<0) ydiff = -ydiff;
    near_vert = ydiff>2*xdiff;
    near_horiz = xdiff>2*ydiff;

    sum = 0;
    for (size_t i=0; i<mid.size (); ++i) {
	if (near_vert) {
	    t = conics[1].closestSplineSolve (mid[i].y, mid[i].t);
	} else if (near_horiz) {
	    t = conics[0].closestSplineSolve (mid[i].x, mid[i].t);
	} else {
	    t =(conics[1].closestSplineSolve (mid[i].y, mid[i].t) +
		conics[0].closestSplineSolve (mid[i].x, mid[i].t))/2;
	}
	temp = mid[i].x - (((conics[0].a*t+conics[0].b)*t+conics[0].c)*t + conics[0].d );
	sum += temp*temp;
	temp = mid[i].y - (((conics[1].a*t+conics[1].b)*t+conics[1].c)*t + conics[1].d );
	sum += temp*temp;
    }

    /* Ok, we've got distances from a set of points on the old spline to the */
    /*  new one. Let's do the reverse: find the extrema of the new and see how*/
    /*  close they get to the bounding box of the old */
    /* And get really unhappy if the spline extends beyond the end-points */
    db2.min = 0; db2.max = db->len;
    conics[0].findExtrema (&ts[0], &ts[1]);
    for (int i=0; i<2; ++i) {
	if (ts[i]!=-1) {
	    x = ((conics[0].a*ts[i]+conics[0].b)*ts[i]+conics[0].c)*ts[i] + conics[0].d;
	    y = ((conics[1].a*ts[i]+conics[1].b)*ts[i]+conics[1].c)*ts[i] + conics[1].d;
	    if ( x<b->minx )
		sum += (x-b->minx)*(x-b->minx);
	    else if ( x>b->maxx )
		sum += (x-b->maxx)*(x-b->maxx);
	    dot = (x-db->base.x)*db->unit.x + (y-db->base.y)*db->unit.y;
	    if ( dot<db2.min ) db2.min = dot;
	    if ( dot>db2.max ) db2.max = dot;
	}
    }
    conics[1].findExtrema (&ts[0], &ts[1]);
    for (int i=0; i<2; ++i) {
	if (ts[i]!=-1) {
	    x = ((conics[0].a*ts[i]+conics[0].b)*ts[i]+conics[0].c)*ts[i] + conics[0].d;
	    y = ((conics[1].a*ts[i]+conics[1].b)*ts[i]+conics[1].c)*ts[i] + conics[1].d;
	    if (y<b->miny)
		sum += (y-b->miny)*(y-b->miny);
	    else if (y>b->maxy)
		sum += (y-b->maxy)*(y-b->maxy);
	    dot = (x-db->base.x)*db->unit.x + (y-db->base.y)*db->unit.y;
	    if (dot<db2.min) db2.min = dot;
	    if (dot>db2.max) db2.max = dot;
	}
    }

    /* Big penalty for going beyond the range of the desired spline */
    if (db->min==0 && db2.min<0)
	sum += 10000 + db2.min*db2.min;
    else if (db2.min<db->min)
	sum += 100 + (db2.min-db->min)*(db2.min-db->min);
    if (db->max==db->len && db2.max>db->len)
	sum += 10000 + (db2.max-db->max)*(db2.max-db->max);
    else if (db2.max>db->max)
	sum += 100 + (db2.max-db->max)*(db2.max-db->max);

    return sum;
}

bool Conic::minMaxWithin () {
    extended_t dx, dy;
    int which;
    extended_t t1, t2;
    extended_t w;
    /* We know that this "spline" is basically one dimensional. As long as its*/
    /*  extrema are between the start and end points on that line then we can */
    /*  treat it as a line. If the extrema are way outside the line segment */
    /*  then it's a line that backtracks on itself */

    if ((dx = this->to->me.x - this->from->me.x)<0) dx = -dx;
    if ((dy = this->to->me.y - this->from->me.y)<0) dy = -dy;
    which = dx<dy;
    conics[which].findExtrema (&t1, &t2);
    if (t1==-1)
	return true;
    w = ((this->conics[which].a*t1 + this->conics[which].b)*t1 +
	this->conics[which].c)*t1 + this->conics[which].d;
    if (realNear(w, (&this->to->me.x)[which]) || realNear(w, (&this->from->me.x)[which]))
	/* Close enough */;
    else if ((w<(&this->to->me.x)[which] && w<(&this->from->me.x)[which]) ||
	(w>(&this->to->me.x)[which] && w>(&this->from->me.x)[which]))
	return false;		/* Outside */

    w = ((this->conics[which].a*t2 + this->conics[which].b)*t2 +
	this->conics[which].c)*t2 + this->conics[which].d;
    if (realNear(w, (&this->to->me.x)[which]) || realNear(w, (&this->from->me.x)[which]))
	/* Close enough */;
    else if ((w<(&this->to->me.x)[which] && w<(&this->from->me.x)[which]) ||
	(w>(&this->to->me.x)[which] && w>(&this->from->me.x)[which]))
	return false;		/* Outside */

    return true;
}

bool Conic::isLinear () {
    double t1,t2, t3,t4;
    bool ret;

    if (islinear)
	return true;
    else if (conics[0].a==0 && conics[0].b==0 && conics[1].a==0 && conics[1].b==0 )
	return true;

    /* Something is linear if the control points lie on the line between the */
    /*  two base points */

    /* Vertical lines */
    if (realNear (this->from->me.x, this->to->me.x)) {
	ret = realNear(this->from->me.x,this->from->nextcp.x) &&
	    realNear(this->from->me.x, this->to->prevcp.x);
	if (!((this->from->nextcp.y >= this->from->me.y &&
	    this->from->nextcp.y <= this->to->me.y &&
	    this->to->prevcp.y >= this->from->me.y &&
	    this->to->prevcp.y <= this->to->me.y ) || (
	    this->from->nextcp.y <= this->from->me.y &&
	    this->from->nextcp.y >= this->to->me.y &&
	    this->to->prevcp.y <= this->from->me.y &&
	    this->to->prevcp.y >= this->to->me.y )) )
	    ret = minMaxWithin ();
    /* Horizontal lines */
    } else if (realNear (this->from->me.y, this->to->me.y)) {
	ret = realNear (this->from->me.y, this->from->nextcp.y) &&
	    realNear (this->from->me.y,this->to->prevcp.y);
	if (!((this->from->nextcp.x >= this->from->me.x &&
	    this->from->nextcp.x <= this->to->me.x &&
	    this->to->prevcp.x >= this->from->me.x &&
	    this->to->prevcp.x <= this->to->me.x) || (
	    this->from->nextcp.x <= this->from->me.x &&
	    this->from->nextcp.x >= this->to->me.x &&
	    this->to->prevcp.x <= this->from->me.x &&
	    this->to->prevcp.x >= this->to->me.x)) )
	    ret = minMaxWithin ();
    } else {
	ret = true;
	t1 = (this->from->nextcp.y-this->from->me.y)/(this->to->me.y-this->from->me.y);
	t2 = (this->from->nextcp.x-this->from->me.x)/(this->to->me.x-this->from->me.x);
	t3 = (this->to->me.y-this->to->prevcp.y)/(this->to->me.y-this->from->me.y);
	t4 = (this->to->me.x-this->to->prevcp.x)/(this->to->me.x-this->from->me.x);
	ret =
	    (within16RoundingErrors (t1,t2) || (realApprox (t1, 0) && realApprox (t2, 0))) &&
	    (within16RoundingErrors (t3,t4) || (realApprox (t3, 0) && realApprox (t4, 0)));
	if (ret) {
	    if (t1<0 || t2<0 || t3<0 || t4<0 || t1>1 || t2>1 || t3>1 || t4>1)
		ret = minMaxWithin ();
	}
    }
    this->islinear = ret;
    if (ret) {
	/* GWW: A few places that if the spline is knownlinear then its conics[?] */
	/*  are linear. So give the linear version and not that suggested by */
	/*  the control points */
	this->conics[0].a = this->conics[0].b = 0;
	this->conics[0].d = this->from->me.x;
	this->conics[0].c = this->to->me.x-this->from->me.x;
	this->conics[1].a = this->conics[1].b = 0;
	this->conics[1].d = this->from->me.y;
	this->conics[1].c = this->to->me.y-this->from->me.y;
    }
    return ret;
}

void Conic::testForLinear () {
    BasePoint off, cpoff, cpoff2;
    double len, co, co2;

    /* Did we make a line? */
    off.x = to->me.x-from->me.x; off.y = to->me.y-from->me.y;
    len = sqrt (off.x*off.x + off.y*off.y);
    if (len!=0) {
	off.x /= len; off.y /= len;
	cpoff.x = from->nextcp.x-from->me.x; cpoff.y = from->nextcp.y-from->me.y;
	len = sqrt (cpoff.x*cpoff.x + cpoff.y*cpoff.y);
	if (len!=0) {
	    cpoff.x /= len; cpoff.y /= len;
	}
	cpoff2.x = to->prevcp.x-from->me.x; cpoff2.y = to->prevcp.y-from->me.y;
	len = sqrt (cpoff2.x*cpoff2.x + cpoff2.y*cpoff2.y);
	if (len!=0) {
	    cpoff2.x /= len; cpoff2.y /= len;
	}
	co = cpoff.x*off.y - cpoff.y*off.x; co2 = cpoff2.x*off.y - cpoff2.y*off.x;
	if (co<.05 && co>-.05 && co2<.05 && co2>-.05) {
	    from->nextcp = from->me; from->nonextcp = true;
	    to->prevcp = to->me; to->noprevcp = true;
	} else if (isLinear ()) {
	    from->nextcp = from->me; from->nonextcp = true;
	    to->prevcp = to->me; to->noprevcp = true;
	    refigure ();
	}
    }
}

bool Conic::adjustLinear () {
    if (this->islinear)
	return true;
    if (this->isLinear ()) {
	this->islinear = this->from->nonextcp = this->to->noprevcp = true;
	this->from->nextcp = this->from->me;
	if (this->from->nonextcp && this->from->noprevcp )
	    this->from->pointtype = pt_corner;
	else if (this->from->pointtype == pt_curve)
	    this->from->pointtype = pt_tangent;
	this->to->prevcp = this->to->me;
	if (this->to->nonextcp && this->to->noprevcp )
	    this->to->pointtype = pt_corner;
	else if (this->to->pointtype == pt_curve)
	    this->to->pointtype = pt_tangent;
	refigure ();
    }
    return this->islinear;
}

double Conic::curvature (double t) const {
    /* Kappa = (x'y'' - y'x'') / (x'^2 + y'^2)^(3/2) */
    double dxdt, dydt, d2xdt2, d2ydt2, denom, numer;

    dxdt = (3*this->conics[0].a*t+2*this->conics[0].b)*t+this->conics[0].c;
    dydt = (3*this->conics[1].a*t+2*this->conics[1].b)*t+this->conics[1].c;
    d2xdt2 = 6*this->conics[0].a*t + 2*this->conics[0].b;
    d2ydt2 = 6*this->conics[1].a*t + 2*this->conics[1].b;
    denom = pow (dxdt*dxdt + dydt*dydt, 3.0/2.0);
    numer = dxdt*d2ydt2 - dydt*d2xdt2;

    if (numer==0)
	return 0;
    if (denom==0)
	return CURVATURE_ERROR;

    return (numer/denom);
}

double Conic::recalcT (ConicPoint *from, ConicPoint *to, double curt) {
    double baselen, fromlen, tolen, ret;
    Conic *cur;

    baselen = this->length ();
    fromlen = baselen * curt;
    tolen = baselen * (1 - curt);

    cur = this->from->prev;
    while (cur && cur->to != from) {
	fromlen += cur->length ();
	cur = cur->from->prev;
    }
    cur = this->to->next;
    while (cur && cur->from != to) {
	tolen += cur->length ();
	cur = cur->to->next;
    }
    ret = fromlen/(fromlen + tolen);
    return ret;
}

void Conic::findBounds (DBounds &bounds) {
    double t, b2_fourac, v;
    double min, max;
    const Conic1D *sp1;
    int i;

    for (i=0; i<2; ++i) {
        /* GWW: first try the end points */
	if (i==0) {
	    if (this->to->me.x<bounds.minx) bounds.minx = this->to->me.x;
	    if (this->to->me.x>bounds.maxx) bounds.maxx = this->to->me.x;
	    min = bounds.minx; max = bounds.maxx;
	} else {
	    if (this->to->me.y<bounds.miny) bounds.miny = this->to->me.y;
	    if (this->to->me.y>bounds.maxy) bounds.maxy = this->to->me.y;
	    min = bounds.miny; max = bounds.maxy;
	}

	/* GWW: then try the extrema of the spline (assuming they are between t=(0,1) */
	/* (I don't bother fixing up for tiny rounding errors here. they don't matter */
	/* But we could call CheckExtremaForSingleBitErrors */
	sp1 = &this->conics[i];
	if (sp1->a!=0) {
	    b2_fourac = 4*sp1->b*sp1->b - 12*sp1->a*sp1->c;
	    if (b2_fourac>=0) {
		b2_fourac = sqrt (b2_fourac);
		t = (-2*sp1->b + b2_fourac) / (6*sp1->a);
		if (t>0 && t<1) {
		    v = ((sp1->a*t+sp1->b)*t+sp1->c)*t + sp1->d;
		    if (v<min) min = v;
		    if (v>max) max = v;
		}
		t = (-2*sp1->b - b2_fourac) / (6*sp1->a);
		if (t>0 && t<1) {
		    v = ((sp1->a*t+sp1->b)*t+sp1->c)*t + sp1->d;
		    if (v<min) min = v;
		    if (v>max) max = v;
		}
	    }
	} else if (sp1->b!=0) {
	    t = -sp1->c/(2.0*sp1->b);
	    if (t>0 && t<1) {
		v = (sp1->b*t+sp1->c)*t + sp1->d;
		if (v<min) min = v;
		if (v>max) max = v;
	    }
	}
	if (i==0) {
	    bounds.minx = min; bounds.maxx = max;
	} else {
	    bounds.miny = min; bounds.maxy = max;
	}
    }
}

DrawableFigure::DrawableFigure () {}

DrawableFigure::DrawableFigure (const DrawableFigure &fig) {
    uint16_t i;

    type = fig.type;
    for (i=0; i<6; i++)
        transform[i] = fig.transform[i];
    props = fig.props;
    points = fig.points;
    svgState = fig.svgState;
    contours.clear ();
    order2 = fig.order2;

    appendSplines (fig);
}

#if 0
DrawableFigure& DrawableFigure::operator=(const DrawableFigure &fig) {
    if (this == &fig)
       return *this;
    return *this;
}
#endif

void DrawableFigure::closepath (ConicPointList *cur, bool is_type2) {
    /* GWW: The "path" is just a single point created by a moveto */
    /* Probably we're just doing another moveto */
    if (cur && cur->first==cur->last && cur->first->prev==nullptr && is_type2)
        return;
    if (cur && cur->first && cur->first!=cur->last) {
    /* GWW: I allow for greater errors here than I do in the straight postscript code */
    /*  because: 1) the rel-rel operators will accumulate more rounding errors   */
    /*  2) I only output 2 decimal digits after the decimal in type1 output */
	if (realWithin (cur->first->me.x, cur->last->me.x, .05) &&
            realWithin (cur->first->me.y, cur->last->me.y, .05)) {
	    ConicPoint *oldlast = cur->last;
	    cur->first->prevcp = oldlast->prevcp;
	    cur->first->prevcp.x += (cur->first->me.x-oldlast->me.x);
	    cur->first->prevcp.y += (cur->first->me.y-oldlast->me.y);
	    cur->first->noprevcp = oldlast->noprevcp;
            cur->first->isfirst = true;
	    oldlast->prev->from->next = nullptr;
	    cur->last = oldlast->prev->from;
	    splines_pool.free (oldlast->prev);
	    if (oldlast->hintmask)
		oldlast->hintmask.reset ();
	    points_pool.destroy (oldlast);
	}
	splines_pool.construct (cur->last, cur->first, false);
	cur->last = cur->first;
    }
}

uint16_t DrawableFigure::renumberPoints (const uint16_t first) {
    uint16_t num = first;

    for (ConicPointList &spls: contours) {
	ConicPoint *sp = spls.first, *nextsp;

	if (order2 && sp->ttfindex == -1 && sp->prev && !sp->noprevcp)
	    sp->prev->from->nextcpindex = num++;

	do {
            if (!order2 && !sp->noprevcp)
                num++;
	    if (sp->ttfindex != -1)
		sp->ttfindex = num++;
	    nextsp = sp->next ? sp->next->to : nullptr;
	    if (!sp->nonextcp && (nextsp != spls.first || spls.first->ttfindex != -1))
		sp->nextcpindex = num++;
	    sp = nextsp;
	} while (sp && sp!=spls.first);
    }
    return num;
}

// Unlike the previous, doesn't change ttfindex/nextcpindex fields.
// May count either ttf points (i. e. both oncurve and offcurve) or
// spline points depending from the 'ttf' arg.
uint16_t DrawableFigure::countPoints (const uint16_t first, bool ttf) const {
    uint16_t num = first;

    for (auto &spls: contours) {
        ConicPoint *sp, *first=nullptr;

        for (sp = spls.first; sp && sp->next && sp!=first; sp=sp->next->to) {
	    if (!ttf || (order2 && sp->ttfindex != -1))
		num++;
	    if (ttf && order2 && !sp->nonextcp)
		num++;
            if (!first) first = sp;
        }
    }
    return num;
}


ConicPointList *DrawableFigure::getPointContour (ConicPoint *sp) {
    for (auto &spls: contours) {
        ConicPoint *test = spls.first;
        do {
            if (sp == test)
                return &spls;
            test = test->next ? test->next->to : nullptr;
        } while (test && test != spls.first);
    }
    return nullptr;
}

void DrawableFigure::clearHintMasks () {
    for (ConicPointList &spls: contours) {
	ConicPoint *sp = spls.first;
	do {
	    if (sp->hintmask)
		sp->hintmask.reset ();
	    sp = sp->next ? sp->next->to : nullptr;
	} while (sp && sp!=spls.first);
    }
}

bool DrawableFigure::addExtrema (bool selected) {
    bool ret = false;
    std::array<extended_t, 4> extr;

    if (type.compare ("path") != 0)
	return ret;

    for (ConicPointList &spls: contours) {
	if (spls.isSelected () || !selected) {
	    Conic *first = spls.first->next;
	    Conic *s = first;
	    if (s) do {
		int cnt = s->findExtrema (extr);
		bool found = false;
		for (int i=0; i<cnt && !found; i++) {
		    if (extr[i] >= .001 && extr[i] <= .999) {
			found = ret = true;
			ConicPoint *mid = bisectSpline (s, extr[i]);
			s = mid->prev;
		    }
		}
		s = s->to->next;
	    } while (s && s != first);
	}
    }
    return ret;
}

bool DrawableFigure::roundToInt (bool selected) {
    bool ret = false;

    for (auto &spls: contours) {
	if (spls.isSelected () || !selected) {
	    ConicPoint *start = spls.first;
	    if (!selected || start->selected)
		ret |= start->roundToInt (this->order2);
	    ConicPoint *sp = start->next ? start->next->to : nullptr;
	    while (sp && sp != start) {
		if (!selected || sp->selected) {
		    if (sp->roundToInt (this->order2)) {
			ret |= true;
			sp->prev->refigure ();
		    }
		}
		sp = sp->next ? sp->next->to : nullptr;
	    }
	}
    }
    return ret;
}

bool DrawableFigure::correctDirection () {
    bool ret = false;
    std::vector<ConicPointList *> clist;
    std::deque<Monotonic> ms;

    clist.reserve (contours.size ());

    for (auto &spls: contours) {
	spls.findBounds (spls.bbox);
	clist.push_back (&spls);
	spls.ticked = false;
    }

    std::sort (clist.begin (), clist.end (), [](ConicPointList *ss1, ConicPointList *ss2) {
	if (ss1->bbox.miny != ss2->bbox.miny)
	    return (ss1->bbox.miny < ss2->bbox.miny);
	return ((ss1->bbox.maxy - ss1->bbox.miny) < (ss2->bbox.maxy - ss2->bbox.miny));
    });
    toMContours (ms, OverlapType::Exclude);

    for (ConicPointList *cptr: clist) {
	std::vector<Monotonic *> space;
	extended_t mpos = cptr->bbox.miny + (cptr->bbox.maxy - cptr->bbox.miny)/2;
	Monotonics::findAt (ms, true, mpos, space);
	int desired = order2 ? 1 : -1;
	int w = 0;

	for (size_t i=0; i<space.size (); i++) {
	    Monotonic *m = space[i];
	    w += (m->yup ? 1 : -1);
	    if (!m->contour->ticked && ((i == 0 && w != desired) || fabs (w) == 2)) {
		m->contour->reverse ();
		Monotonic *cur = m;
		do {
		    cur->reverse ();
		    cur = cur->next;
		} while (cur && cur != m);
		ret |= true;
		w = (fabs (w) == 2) ? 0 : desired;
	    }
	    assert (i < space.size ());
	    space[i]->contour->ticked = true;
	}
    }

    return ret;
}

void DrawableFigure::realBounds (DBounds &b, bool do_init) {
    if (do_init) {
        b.minx = b.miny = 1e10;
        b.maxx = b.maxy = -1e10;
    }
    if (type.compare ("circle") == 0 || type.compare ("ellipse") == 0) {
	// May get negative ry as a result of
	// a previous transformation applied?
	b.minx = props["cx"] - std::abs (props["rx"]);
	b.maxx = props["cx"] + std::abs (props["rx"]);
	b.miny = props["cy"] - std::abs (props["ry"]);
	b.maxy = props["cy"] + std::abs (props["ry"]);
    } else if (type.compare ("rect") == 0) {
	b.minx = props["x"];
	b.miny = props["y"];
	b.maxx = props["x"] + props["width"];
	b.maxy = props["y"] + props["height"];
	if (b.minx < b.maxx) std::swap (b.minx, b.maxx);
	if (b.miny < b.maxy) std::swap (b.miny, b.maxy);
    } else if (!contours.empty ()) {
	for (auto &spls: contours) {
	    /* GWW: Ignore contours consisting of a single point (used for hinting, anchors */
	    /*  for mark to base, etc.) */
	    if (spls.first->next && spls.first->next->to != spls.first) {
		if (spls.first->me.x<b.minx) b.minx = spls.first->me.x;
		if (spls.first->me.x>b.maxx) b.maxx = spls.first->me.x;
		if (spls.first->me.y<b.miny) b.miny = spls.first->me.y;
		if (spls.first->me.y>b.maxy) b.maxy = spls.first->me.y;

		Conic *spline = spls.first->next;
		if (spline) do {
		    spline->findBounds (b);
		    spline = spline->to->next;
		} while (spline && spline != spls.first->next);
	    }
	}
    }
    if (do_init) {
        if (b.minx>65536) b.minx = 0;
        if (b.miny>65536) b.miny = 0;
        if (b.maxx<-65536) b.maxx = 0;
        if (b.maxy<-65536) b.maxy = 0;
    }
}

void DrawableFigure::quickBounds (DBounds &b) {
    if (type.compare ("circle") == 0 || type.compare ("ellipse") == 0 ||
	type.compare ("rect") == 0) {
	realBounds (b, false);
    } else if (!contours.empty ()) {
	for (auto &spls: contours) {
	    ConicPoint *sp = spls.first;
	    do {
		if (sp->me.y < b.miny) b.miny = sp->me.y;
		if (sp->me.x < b.minx) b.minx = sp->me.x;
		if (sp->me.y > b.maxy) b.maxy = sp->me.y;
		if (sp->me.x > b.maxx) b.maxx = sp->me.x;
		sp = (!sp->next) ? nullptr : sp->next->to;
	    } while (sp && sp != spls.first);
	}
    }
}

bool DrawableFigure::hasSelected () const {
    if (selected)
	return true;
    for (auto &spls: contours) {
	if (spls.isSelected ())
	    return true;
    }
    return false;
}

bool DrawableFigure::mergeWith (const DrawableFigure &fig) {
    if (type.compare ("path") != 0 || fig.contours.empty ())
        return false;
    if (svgState != fig.svgState)
        return false;

    contours.reserve (contours.size () + fig.contours.size ());
    appendSplines (fig);
    return true;
}

void DrawableFigure::appendSplines (const DrawableFigure &fig) {
    uint16_t i;
    ConicPoint *sp;

    for (i=0; i<fig.contours.size (); i++) {
        contours.emplace_back ();
        ConicPointList &spls = contours.back ();
        const ConicPointList &source_spls = fig.contours[i];
        Conic *source, *target, *first=nullptr;

        spls.first = points_pool.construct (*source_spls.first);
        sp = spls.first;
        if (source_spls.first->hintmask)
            sp->hintmask = std::unique_ptr<HintMask> (new HintMask (*source_spls.first->hintmask));
        for (source = source_spls.first->next; source && source!=first; source=source->to->next) {
            target = splines_pool.construct (*source);
            sp->next = target;
            target->from = sp;
            if (source->to != source_spls.first) {
                sp = points_pool.construct (*source->to);
                sp->prev = target;
                target->to = sp;
                spls.last = sp;
                if (source->to->hintmask)
		    sp->hintmask = std::unique_ptr<HintMask> (new HintMask (*source->to->hintmask));
            } else {
                spls.first->prev = target;
                target->to = spls.first;
                spls.last = spls.first;
            }
            if (!first)
                first = source;
        }
    }
}

bool DrawableFigure::clearMarked () {
    bool changed = false;
    for (auto &spls: contours) {
	spls.ticked = false;
	ConicPoint *curpt = spls.first, *firstpt = nullptr;

	// Go backwards up to the first point which is not selected.
	// This is to prevent situation when we delete an arbitrary point from
	// a contiguous range of selected points, when reach another point
	// from the same range and split the contour on that point, creating
	// a new contour just to immediately delete it, as it would contain
	// only selected points.
	if (spls.first->selected) {
	    for (
		curpt = spls.first;
		curpt->prev && curpt->prev->from && curpt->prev->from->selected && curpt != firstpt;
		curpt = curpt->prev->from) {
		if (!firstpt) firstpt = curpt;
	    }
	} else
	    curpt = spls.first;

	firstpt = nullptr;
	while (curpt && curpt != firstpt) {
	    if (!firstpt) firstpt = curpt;

	    // If a contour has just a single point and this point is selected,
	    // then delete the point, mark contour for deletion and go to the next
	    // iteration of the loop.
	    if (curpt->selected && !curpt->next && !curpt->prev) {
		spls.first = spls.last = nullptr;
		points_pool.destroy (curpt);
		curpt = nullptr;
		spls.ticked = true;
		changed = true;
	    // Found a selected point on a closed contour.
	    // Turn the contour to open, delete the point.
	    } else if (curpt->selected && spls.first == spls.last) {
		spls.first = curpt->next->to;
		spls.last = curpt->prev->from;
		splines_pool.free (spls.first->prev);
		splines_pool.free (spls.last->next);
		spls.first->prev = spls.last->next = nullptr;
		points_pool.destroy (curpt);
		curpt = spls.first;
		changed = true;
	    // First point on an open contour is selected (may occur as a result
	    // of the previous condition being matched on the previous point).
	    // Adjust the start point for the contour and delete the curent point.
	    } else if (curpt->selected && curpt == spls.first) {
		spls.first = curpt->next->to;
		splines_pool.free (curpt->next);
		spls.first->prev = nullptr;
		points_pool.destroy (curpt);
		curpt = spls.first;
		changed = true;
	    // Found a selected point on an open contour (not the first one).
	    // If this is the last point on the contour, just delete it and
	    // set the last point of the contour to the previous one.
	    // Otherwise split the contour and create a new one, which itself
	    // is supposed to be processed by a similar way on a later iteration of the loop.
	    } else if (curpt->selected) {
		if (curpt->next) {
		    contours.emplace_back ();
		    auto &new_spls = contours.back ();
		    new_spls.first = curpt->next->to;
		    new_spls.first->prev = nullptr;
		    new_spls.last = spls.last;
		}
		spls.last = curpt->prev->from;
		splines_pool.free (spls.last->next);
		spls.last->next = nullptr;
		points_pool.destroy (curpt);
		curpt = nullptr;
		changed = true;
	    // Point is not selected and there is something more to process.
	    // Just change the current point to the next one.
	    } else if (!curpt->selected && curpt->next) {
		curpt=curpt->next->to;
	    // Point is not selected and it is the last one on its spline.
	    // Just set the current point to nullptr.
	    } else {
		curpt = nullptr;
	    }
	}
    }
    if (changed) {
	for (int i = contours.size () - 1; i>=0; i--) {
	    auto &spls = contours[i];
	    if (spls.ticked)
		deleteContour (&spls);
	}
    }
    return changed;
}

void DrawableFigure::mergeMarked () {
    Conic *spline, *first;
    ConicPoint *nextp, *curp, *selectme;
    bool all;

    /* If the entire splineset is selected, it should merge into oblivion */
    for (auto &spls: contours) {
	first = nullptr;
	all = spls.first->selected;
	spls.ticked = false;
	for (spline = spls.first->next; spline && spline!=first && all; spline=spline->to->next) {
	    if (!spline->to->selected)
		all = false;
	    if (!first)
		first = spline;
	}
	/* Merge away any splines which are just dots */
	if (spls.first->next && spls.first->next->to==spls.first &&
	    spls.first->nonextcp && spls.first->noprevcp)
	    all = true;
	// Mark contour for deletion and continue
	if (all) {
	    spls.ticked = true;
	    continue;
	}
	removeZeroLengthSplines (&spls, true, .3);

	if (spls.first!=spls.last) {
	    /* If the spline isn't closed, then any selected points at the ends */
	    /*  get deleted */
	    while (spls.first->selected) {
		nextp = spls.first->next->to;
		splines_pool.free (spls.first->next);
		spls.first = nextp;
		nextp->prev = nullptr;
	    }
	    while (spls.last->selected) {
		nextp = spls.last->prev->from;
		splines_pool.free (spls.last->prev);
		spls.last = nextp;
		nextp->next = nullptr;
	    }
	} else {
	    while (spls.first->selected) {
		spls.first = spls.first->next->to;
		spls.last = spls.first;
	    }
	}

	/* when we get here spl->first is not selected */
	assert (spls.first->selected == false);
	curp = spls.first;
	selectme = nullptr;
	while (true) {
	    while (!curp->selected) {
		if (!curp->next)
		    curp = nullptr;
		else
		    curp = curp->next->to;
		if (curp==spls.first || !curp)
		    break;
	    }
	    if (!curp || !curp->selected)
		break;
	    for (nextp=curp->next->to; nextp->selected; nextp = nextp->next->to);
	    /* we don't need to check for the end of the splineset here because */
	    /*  we know that spls.last is not selected */
	    splinesRemoveBetween (curp->prev->from, nextp);
	    curp = nextp;
	    selectme = nextp;
	}
	if (selectme) selectme->selected = true;
    }

    for (int i=contours.size ()-1; i>=0; i--) {
	if (contours[i].ticked)
	    contours.erase (contours.begin () + i);
    }
}

void DrawableFigure::deleteContour (ConicPointList *spls) {
    uint16_t i;

    for (i=0; i<contours.size (); i++) {
        if (&contours[i] == spls) {
            contours.erase (contours.begin () + i);
            break;
        }
    }
}

ConicPoint* DrawableFigure::bisectSpline (Conic *spl, extended_t t) {
    Spline1 xstart, xend;
    Spline1 ystart, yend;
    Conic *spline1, *spline2;
    ConicPoint *mid, *old0, *old1;
    Conic1D &xsp = spl->conics[0], &ysp = spl->conics[1];
    bool order2 = spl->order2;

    xstart.s0 = xsp.d; ystart.s0 = ysp.d;
    xend.s1 = (extended_t) xsp.a+xsp.b+xsp.c+xsp.d;
    yend.s1 = (extended_t) ysp.a+ysp.b+ysp.c+ysp.d;
    xstart.s1 = xend.s0 = ((xsp.a*t+xsp.b)*t+xsp.c)*t + xsp.d;
    ystart.s1 = yend.s0 = ((ysp.a*t+ysp.b)*t+ysp.c)*t + ysp.d;
    xstart.figure (0, t, xsp);
    xend.figure (t, 1, xsp);
    ystart.figure (0, t, ysp);
    yend.figure (t, 1, ysp);

    mid = points_pool.construct (xstart.s1, ystart.s1);
    if (order2) {
	mid->nextcp.x = xend.spline.d + xend.spline.c/2;
	mid->nextcp.y = yend.spline.d + yend.spline.c/2;
	mid->prevcp.x = xstart.spline.d + xstart.spline.c/2;
	mid->prevcp.y = ystart.spline.d + ystart.spline.c/2;
    } else {
	mid->nextcp.x = xend.c0;
        mid->nextcp.y = yend.c0;
	mid->prevcp.x = xstart.c1;
        mid->prevcp.y = ystart.c1;
    }
    if (mid->me.x==mid->nextcp.x && mid->me.y==mid->nextcp.y)
	mid->nonextcp = true;
    else
	mid->nonextcp = false;
    if (mid->me.x==mid->prevcp.x && mid->me.y==mid->prevcp.y)
	mid->noprevcp = true;
    else
	mid->noprevcp = false;

    old0 = spl->from; old1 = spl->to;
    if (order2) {
	old0->nextcp = mid->prevcp;
	old1->prevcp = mid->nextcp;
    } else {
	old0->nextcp.x = xstart.c0;
        old0->nextcp.y = ystart.c0;
	old1->prevcp.x = xend.c1;
        old1->prevcp.y = yend.c1;
    }
    old0->nonextcp = (old0->nextcp.x==old0->me.x && old0->nextcp.y==old0->me.y);
    old1->noprevcp = (old1->prevcp.x==old1->me.x && old1->prevcp.y==old1->me.y);

    splines_pool.free (spl);
    spline1 = splines_pool.construct (old0, mid, order2);
    spline1->refigure ();
    if (spline1->islinear) {
	spline1->from->nextcp = spline1->from->me;
	spline1->to->prevcp = spline1->to->me;
        spline1->from->nonextcp = spline1->to->noprevcp = true;
    }

    spline2 = splines_pool.construct (mid, old1, order2);
    spline2->refigure ();
    if (spline2->islinear) {
	spline2->from->nextcp = spline2->from->me;
	spline2->to->prevcp = spline2->to->me;
        spline2->from->nonextcp = spline2->to->noprevcp = true;
    }
    return mid;
}

bool DrawableFigure::removeZeroLengthSplines (ConicPointList *spls, bool onlyselected, double bound) {
    ConicPoint *curp, *next, *prev;
    double plen, nlen;
    bool ret = false;

    bound *= bound;

    for (curp = spls->first, prev=nullptr; curp; curp=next) {
	next = nullptr;
	if (curp->next)
	    next = curp->next->to;
	/* GWW: Once we've worked a contour down to a single point we
	 * can't do anything more here. Someone else will have to free the contour */
	if (curp==next)
	    return ret;
	/* Zero length splines give us NaNs */
	if (curp && (curp->selected || !onlyselected)) {
	    plen = nlen = 1e10;
	    if (curp->prev) {
		plen =
		    (curp->me.x-curp->prev->from->me.x)*(curp->me.x-curp->prev->from->me.x) +
		    (curp->me.y-curp->prev->from->me.y)*(curp->me.y-curp->prev->from->me.y);
		if (plen<=bound) {
		    plen =
			sqrt((curp->me.x-curp->prevcp.x)*(curp->me.x-curp->prevcp.x) +
			    (curp->me.y-curp->prevcp.y)*(curp->me.y-curp->prevcp.y)) +
			sqrt((curp->prevcp.x-curp->prev->from->nextcp.x)*(curp->prevcp.x-curp->prev->from->nextcp.x) +
			    (curp->prevcp.y-curp->prev->from->nextcp.y)*(curp->prevcp.y-curp->prev->from->nextcp.y)) +
			sqrt((curp->prev->from->nextcp.x-curp->prev->from->me.x)*(curp->prev->from->nextcp.x-curp->prev->from->me.x) +
			    (curp->prev->from->nextcp.y-curp->prev->from->me.y)*(curp->prev->from->nextcp.y-curp->prev->from->me.y));
		    plen *= plen;
		}
	    }
	    if (curp->next) {
		nlen =
		    (curp->me.x-next->me.x)*(curp->me.x-next->me.x) +
		    (curp->me.y-next->me.y)*(curp->me.y-next->me.y);
		if (nlen<=bound) {
		    nlen =
			sqrt((curp->me.x-curp->nextcp.x)*(curp->me.x-curp->nextcp.x) +
			    (curp->me.y-curp->nextcp.y)*(curp->me.y-curp->nextcp.y)) +
			sqrt((curp->nextcp.x-curp->next->to->prevcp.x)*(curp->nextcp.x-curp->next->to->prevcp.x) +
			    (curp->nextcp.y-curp->next->to->prevcp.y)*(curp->nextcp.y-curp->next->to->prevcp.y)) +
			sqrt((curp->next->to->prevcp.x-curp->next->to->me.x)*(curp->next->to->prevcp.x-curp->next->to->me.x) +
			    (curp->next->to->prevcp.y-curp->next->to->me.y)*(curp->next->to->prevcp.y-curp->next->to->me.y));
		    nlen *= nlen;
		}
	    }
	    if ((curp->prev && plen<=bound && plen<nlen) || (curp->next && nlen<=bound && nlen<=plen)) {
		if (curp->prev && plen<=bound && plen<nlen) {
		    ConicPoint *other = curp->prev->from;
		    other->nextcp = curp->nextcp;
		    other->nonextcp = curp->nonextcp;
		    other->next = curp->next;
		    if (curp->next)
			other->next->from = other;
		    splines_pool.free (curp->prev);
		} else {
		    ConicPoint *other = next;
		    other->prevcp = curp->prevcp;
		    other->noprevcp = curp->noprevcp;
		    other->prev = curp->prev;
		    if (curp->prev)
			other->prev->to = other;
		    splines_pool.free (curp->next);
		}
		points_pool.destroy (curp);
		if (spls->first==curp) {
		    spls->first = next;
		    if (spls->last==curp)
			spls->last = next;
		} else if (spls->last==curp)
		    spls->last = prev;
		ret |= true;
	    } else
		prev = curp;
	} else
	    prev = curp;
	if (next==spls->first)
	    break;
    }
    return ret;
}

void DrawableFigure::splinesRemoveBetween (ConicPoint *from, ConicPoint *to) {
    std::vector<TPoint> tp;
    ConicPoint *np, oldfrom;
    bool order2 = from->next->order2;

    oldfrom = *from;
    tp = Conic::figureTPsBetween (from, to);

    Conic::approximateFromPointsSlopes (from, to, tp, order2);

    /* GWW: Have to do the frees after the approximation because the approx */
    /*  uses the splines to determine slopes */
    for (Conic *spl = oldfrom.next; ;) {
	np = spl->to;
	splines_pool.free (spl);
	if (np==to)
	    break;
	spl = np->next;
    }
}

bool DrawableFigure::makeLoop (ConicPointList &spls, double fudge) {
    if (spls.first!=spls.last &&
	realWithin (spls.first->me.x, spls.last->me.x, fudge) &&
	realWithin (spls.first->me.y, spls.last->me.y, fudge)) {

	if (spls.last->selected && !spls.first->selected) {
	    spls.last->next = spls.first->next;
	    spls.first->next->from = spls.last;
	    spls.last->nextcp = spls.first->nextcp;
	    spls.last->nonextcp = spls.first->nonextcp;
	    points_pool.destroy (spls.first);
	    spls.first = spls.last;
	} else {
	    spls.first->prev = spls.last->prev;
	    spls.first->prev->to = spls.first;
	    spls.first->prevcp = spls.last->prevcp;
	    spls.first->noprevcp = spls.last->noprevcp;
	    points_pool.destroy (spls.last);
	    spls.last = spls.first;
	}
	spls.first->joinCpFixup (order2);
	return true;
    }
    return false;
}

bool DrawableFigure::join (bool doall, double fudge) {
    bool changed = false;
    for (auto &spls: contours)
	spls.ticked = false;

    for (auto &spls: contours) {
	if (!spls.ticked && spls.first != spls.last && !spls.first->prev && (doall || spls.isSelected ())) {
	    if (makeLoop (spls, fudge) ) {
		changed = true;
	    } else {
		for (auto &spls2: contours) {
		    if (&spls2!=&spls && !spls2.ticked) {
			if (!realWithin (spls.first->me.x, spls2.last->me.x, fudge) &&
			    !realWithin (spls.first->me.y, spls2.last->me.y, fudge)) {
			    if ((realWithin(spls.last->me.x, spls2.last->me.x, fudge) &&
				realWithin(spls.last->me.y, spls2.last->me.y, fudge)) ||
				(realWithin(spls.last->me.x, spls2.first->me.x, fudge) &&
				realWithin(spls.last->me.y, spls2.first->me.y, fudge)))
				spls.reverse ();
			}
			if (realWithin (spls.first->me.x, spls2.first->me.x, fudge) &&
			    realWithin (spls.first->me.y, spls2.first->me.y, fudge))
			    spls2.reverse ();
			if (realWithin (spls.first->me.x, spls2.last->me.x, fudge) &&
			    realWithin (spls.first->me.y, spls2.last->me.y, fudge)) {

			    spls.first->prev = spls2.last->prev;
			    spls.first->prev->to = spls.first;
			    spls.first->prevcp = spls2.last->prevcp;
			    spls.first->noprevcp = spls2.last->noprevcp;
			    points_pool.destroy (spls2.last);
			    spls.first->joinCpFixup (order2);
			    spls.first = spls2.first;
			    spls2.last = spls2.first = nullptr;
			    // Mark contour for deletion
			    spls2.ticked = true;
			    makeLoop (spls, fudge);
			    spls.ensureStart ();

			    changed = true;
			    break;
			}
		    }
		}
	    }
	}
    }
    if (changed) {
	for (int i = contours.size () - 1; i>=0; i--) {
	    auto &spls = contours[i];
	    if (spls.ticked)
		deleteContour (&spls);
	}
    }
    return changed;
}

static int val_to_ttf (const double val, double &prev, uint8_t &flag, const bool is_x) {
    int diff = rint (val) - rint (prev);
    prev = val;
    uint8_t same_flag  = is_x ? _X_Same : _Y_Same;
    uint8_t short_flag = is_x ? _X_Short : _Y_Short;
    if (diff == 0) {
        flag |= same_flag;
    } else if (diff > 0 && diff < 256) {
        flag |= short_flag;
        flag |= same_flag;
    } else if (diff > -256 && diff < 0) {
        diff = abs (diff);
        flag |= short_flag;
    }
    return diff;
}

uint16_t DrawableFigure::toCoordList (std::vector<int16_t> &x_coords,
    std::vector<int16_t> &y_coords, std::vector<uint8_t> &flags, uint16_t gid) {
    double last_x = 0, last_y = 0;
    int diff_x, diff_y;
    const int tot = renumberPoints (0);
    x_coords.reserve (tot);
    y_coords.reserve (tot);
    flags.reserve (tot);
    int ptcnt = 0, repeat, startcnt;
    bool last_repeat = false;
    uint8_t flag;

    for (ConicPointList &spls: contours) {
	ConicPoint *sp = spls.first, *nextsp;
	startcnt = ptcnt;
	repeat = 0;

	if (sp->ttfindex == -1 && sp->prev && !sp->noprevcp) {
	    flag = 0;
	    diff_x = val_to_ttf (sp->prevcp.x, last_x, flag, true);
	    if (diff_x)
		x_coords.push_back (diff_x);
	    diff_y = val_to_ttf (sp->prevcp.y, last_y, flag, false);
	    if (diff_y)
		y_coords.push_back (diff_y);
	    flags.push_back (flag);
	    ptcnt++;
	} else if (sp->ttfindex!=startcnt && sp->ttfindex!=-1) {
	    FontShepherd::postError (
		QCoreApplication::tr ("Unexpected point count"),
		QCoreApplication::tr (
		    "Unexpected point count in DrawableFigure::toCoordList (glyph %1): "
		    "got %2, while %3 is expected").arg (gid).arg (spls.first->ttfindex).arg (ptcnt),
		nullptr);
	}

	do {
	    if (sp->ttfindex != -1) {
		flag = _On_Curve;
		diff_x = val_to_ttf (sp->me.x, last_x, flag, true);
		if (diff_x)
		    x_coords.push_back (diff_x);
		diff_y = val_to_ttf (sp->me.y, last_y, flag, false);
		if (diff_y)
		    y_coords.push_back (diff_y);
		if (flag != flags.back () || last_repeat) {
		    if (repeat) {
			flags.back () |= _Repeat;
			flags.push_back (repeat);
			last_repeat = true;
			repeat = 0;
		    }
		    flags.push_back (flag);
		    last_repeat = false;
		} else {
		    repeat++;
		}
		ptcnt++;
	    }
	    nextsp = sp->next ? sp->next->to : nullptr;
	    if (!sp->nonextcp && (nextsp != spls.first || spls.first->ttfindex != -1)) {
		flag = 0;
		diff_x = val_to_ttf (sp->nextcp.x, last_x, flag, true);
		if (diff_x)
		    x_coords.push_back (diff_x);
		diff_y = val_to_ttf (sp->nextcp.y, last_y, flag, false);
		if (diff_y)
		    y_coords.push_back (diff_y);
		if (flag != flags.back () || last_repeat) {
		    if (repeat) {
			flags.back () |= _Repeat;
			flags.push_back (repeat);
			last_repeat = true;
			repeat = 0;
		    }
		    flags.push_back (flag);
		    last_repeat = false;
		} else {
		    repeat++;
		}
		ptcnt++;
	    }
	    sp = nextsp;
	} while (sp && sp!=spls.first);
	if (repeat) {
	    flags.back () |= _Repeat;
	    flags.push_back (repeat);
	    last_repeat = true;
	    repeat = 0;
	}
    }
    return ptcnt;
}

bool DrawableFigure::startToPoint (ConicPoint *nst) {
    for (auto &spls: contours) {
	int first_num = spls.first->ttfindex;
	if (order2 && first_num == -1 && spls.first->prev && !spls.first->noprevcp)
	    first_num = spls.first->prev->from->nextcpindex;

	if (spls.startToPoint (nst)) {
	    renumberPoints (first_num);
	    return true;
	}
    }
    return false;
}

void Spline1::figure (extended_t t0, extended_t t1, Conic1D &spl) {
    extended_t s = (t1-t0);
    if (spl.a==0 && spl.b==0) {
	spline.d = spl.d + t0*spl.c;
	spline.c = s*spl.c;
	spline.b = spline.a = 0;
    } else {
	spline.d = spl.d + t0*(spl.c + t0*(spl.b + t0*spl.a));
	spline.c = s*(spl.c + t0*(2*spl.b + 3*spl.a*t0));
	spline.b = s*s*(spl.b+3*spl.a*t0);
	spline.a = s*s*s*spl.a;
    }
    c0 = spline.c/3 + spline.d;
    c1 = c0 + (spline.b+spline.c)/3;
}

Monotonic *Conic::toMonotonic
    (ConicPointList *ss, std::deque<Monotonic> &mpool, extended_t startt, extended_t endt, bool exclude) {
    Monotonic *m;
    Monotonic *last = mpool.empty () ? nullptr : &mpool.back ();
    BasePoint start, end;

    if (startt==0)
	start = this->from->me;
    else {
	start.x = ((this->conics[0].a*startt+this->conics[0].b)*startt+this->conics[0].c)*startt
		    + this->conics[0].d;
	start.y = ((this->conics[1].a*startt+this->conics[1].b)*startt+this->conics[1].c)*startt
		    + this->conics[1].d;
    }
    if (endt==1.0)
	end = this->to->me;
    else {
	end.x = ((this->conics[0].a*endt+this->conics[0].b)*endt+this->conics[0].c)*endt
		    + this->conics[0].d;
	end.y = ((this->conics[1].a*endt+this->conics[1].b)*endt+this->conics[1].c)*endt
		    + this->conics[1].d;
    }
    if ((realNear ((start.x+end.x)/2, start.x) || realNear ((start.x+end.x)/2, end.x)) &&
	(realNear ((start.y+end.y)/2, start.y) || realNear ((start.y+end.y)/2, end.y))) {
	/* The distance between the two extrema is so small */
	/*  as to be unobservable. In other words we'd end up with a zero*/
	/*  length spline */
	if (endt==1.0 && last && last->s==this)
	    last->tend = endt;
	return last;
    }

    mpool.emplace_back ();
    m = &mpool.back ();
    m->s = this;
    m->contour = ss;
    m->tstart = startt;
    m->tend = endt;
    m->exclude = exclude;

    if (end.x>start.x) {
	m->xup = true;
	m->b.minx = start.x;
	m->b.maxx = end.x;
    } else {
	m->b.minx = end.x;
	m->b.maxx = start.x;
    }
    if (end.y>start.y) {
	m->yup = true;
	m->b.miny = start.y;
	m->b.maxy = end.y;
    } else {
	m->b.miny = end.y;
	m->b.maxy = start.y;
    }

    if (last) {
	last->next = m;
	m->prev = last;
    }
    return m;
}

Monotonic *ConicPointList::toMContour (std::deque<Monotonic> &mpool, Monotonic *start, OverlapType ot) {
    std::array<extended_t, 4> ts;
    Conic *first_s, *s;
    Monotonic *head=nullptr, *last=nullptr;
    int cnt;
    bool selected = false;
    extended_t lastt;

    /* GWW: Open contours have no interior, ignore 'em */
    if (!first->prev)
	return start;
    /* GWW: Let's just remove single points */
    if (first->prev->from == first && first->noprevcp && first->nonextcp )
	return start;

    switch (ot) {
      case OverlapType::Exclude:
	selected = this->isSelected ();
	break;
      case OverlapType::RemoveSelected:
      case OverlapType::Intersel:
      case OverlapType::Fisel:
	selected = this->isSelected ();
        if (!selected)
	    return start;
        selected = false;
	break;
      default:
	;
    }

    first_s = nullptr;
    for (s=this->first->next; s!=first_s; s=s->to->next) {
	if (!first_s) first_s = s;
	cnt = s->findExtrema (ts);
	lastt = 0;
	for (int i=0; i<cnt; i++) {
	    last = s->toMonotonic (this, mpool, lastt, ts[i], selected);
	    if (!head) head = last;
	    lastt=ts[i];
	}
	if (lastt!=1.0) {
	    last = s->toMonotonic (this, mpool, lastt, 1.0, selected);
	    if (!head) head = last;
	}
    }
    head->prev = last;
    last->next = head;
    if (!start)
	start = head;
    return start;
}

Monotonic *DrawableFigure::toMContours (std::deque<Monotonic> &mpool, OverlapType ot) {
    Monotonic *head = nullptr;

    if (!type.compare ("path")) {
	for (auto &spls: contours) {
	    if (removeZeroLengthSplines (&spls, false, .3))
		continue;
	    head = spls.toMContour (mpool, head, ot);
	}
	return head;
    }
    return head;
}

inline double Det (double a, double b, double c, double d) {
	return a*d - b*c;
}

static bool intersectAt (BasePoint l1s, BasePoint l1e, BasePoint l2s, BasePoint l2e, BasePoint &inter) {
    // http://mathworld.wolfram.com/Line-LineIntersection.html
    // https://gist.github.com/TimSC/47203a0f5f15293d2099507ba5da44e6
    double detL1 = Det (l1s.x, l1s.y, l1e.x, l1e.y);
    double detL2 = Det (l2s.x, l2s.y, l2e.x, l2e.y);
    double x1mx2 = l1s.x - l1e.x;
    double x3mx4 = l2s.x - l2e.x;
    double y1my2 = l1s.y - l1e.y;
    double y3my4 = l2s.y - l2e.y;

    double xnom  = Det (detL1, x1mx2, detL2, x3mx4);
    double ynom  = Det (detL1, y1my2, detL2, y3my4);
    double denom = Det (x1mx2, y1my2, x3mx4, y3my4);
    // Lines don't seem to cross
    if (denom == 0.0) {
        inter.x = NAN;
        inter.y = NAN;
        return false;
    }

    inter.x = xnom / denom;
    inter.y = ynom / denom;
    // Probably a numerical issue
    if (!std::isfinite (inter.x) || !std::isfinite (inter.y))
        return false;

    return true;
}

static void ttfApproxSplineIndeed (DrawableFigure &fig, Conic *spl, double fudge, int depth=0) {
    Conic1D &xsp = spl->conics[0], &ysp = spl->conics[1];
    Conic1D xtest, ytest;
    ConicPoint *sp1 = spl->from;
    ConicPoint *sp2 = spl->to;

    BasePoint mid1, mid2, inter;
    double t = 0.5;

    mid1.x = ((xsp.a*t+xsp.b)*t+xsp.c)*t + xsp.d;
    mid1.y = ((ysp.a*t+ysp.b)*t+ysp.c)*t + ysp.d;
    if (!intersectAt (sp1->me, sp1->nextcp, sp2->me, sp2->prevcp, inter)) {
	std::cerr << "no intersection found: " << sp1->me.x << ' ' << sp1->me.y
		  << " to " << sp2->me.x << ' ' << sp2->me.y << std::endl;
        sp1->nonextcp = sp2->noprevcp = true;
	spl->islinear = spl->order2 = true;
	return;
    }

    xtest.d = sp1->me.x;
    xtest.c = 2*(inter.x - xtest.d);
    xtest.b = sp2->me.x - xtest.d - xtest.c;
    ytest.d = sp1->me.y;
    ytest.c = 2*(inter.y - ytest.d);
    ytest.b = sp2->me.y - ytest.d - ytest.c;

    mid2.x = (xtest.b*t+xtest.c)*t + xtest.d;
    mid2.y = (ytest.b*t+ytest.c)*t + ytest.d;

    double dist = sqrt (
        (mid1.x-mid2.x)*(mid1.x-mid2.x) +
        (mid1.y-mid2.y)*(mid1.y-mid2.y)
    );

    // Usually no more than 4 levels of recursion is needed
    if (FontShepherd::math::realWithin (0, dist, 1.0) || depth>8) {
        sp1->nonextcp = sp2->noprevcp = false;
	sp1->nextcp.x = sp2->prevcp.x = inter.x;
	sp1->nextcp.y = sp2->prevcp.y = inter.y;
	spl->order2 = true;
	spl->refigure ();
    } else {
	ConicPoint *sp = fig.bisectSpline (spl, t);
	sp->ttfindex = -1;
	sp->pointtype = pt_curve;
	ttfApproxSplineIndeed (fig, sp->prev, fudge, depth+1);
	ttfApproxSplineIndeed (fig, sp->next, fudge, depth+1);
    }
}

static void ttfApproxSpline (DrawableFigure &fig, Conic *spl, double fudge) {
    Conic1D &xsp = spl->conics[0], &ysp = spl->conics[1];
    ConicPoint *sp1 = spl->from;
    ConicPoint *sp2 = spl->to;
    std::array<extended_t, 2> poi;
    std::array<extended_t, 4> extr;
    std::vector<extended_t> magick;
    int cnt_poi = 0, cnt_extr = 0;

    if (spl->order2)
	return;
    if (spl->islinear) {
	spl->order2 = true;
	return;
    // already quadratic
    } else if (realNear (xsp.a, 0) && realNear (ysp.a, 0)) {
	spl->order2 = true;
        sp1->nextcp.x = sp2->prevcp.x = (xsp.c+2*xsp.d)/2;
        sp1->nextcp.y = sp2->prevcp.y = (ysp.c+2*ysp.d)/2;
	return;
    }
    cnt_poi  = spl->findInflectionPoints (poi);
    cnt_extr = spl->findExtrema (extr);
    if (cnt_poi || cnt_extr) {
	int i;
	magick.resize (cnt_poi + cnt_extr);
	for (i=0; i<cnt_poi; i++)
	    magick[i] = poi[i];
	for (i=0; i<cnt_extr; i++) {
	    magick[cnt_poi+i] = extr[i];
	}
	std::sort (magick.begin (), magick.end ());
	for (i=magick.size ()-1; i>0; i--)
	    magick[i] = 1-magick[i]/1-magick[i-1];
	magick.erase (
	    std::remove_if (
		magick.begin (),
		magick.end (),
		[](double t){return (realNear (t, 0) || realNear (t, 1));}),
	    magick.end ()
	);

        for (extended_t t : magick) {
	    ConicPoint *sp = fig.bisectSpline (spl, t);
	    sp->pointtype = pt_curve;
	    ttfApproxSplineIndeed (fig, sp->prev, fudge);
	    spl = sp->next;
	}
    } else
	ttfApproxSplineIndeed (fig, spl, fudge);
}

void DrawableFigure::toQuadratic (double fudge) {
    for (auto &spls: contours) {
        ConicPoint *sp = spls.first;
	do {
	    if (sp->canInterpolate ()) sp->ttfindex = -1;
	    sp = sp->next ? sp->next->to : nullptr;
	} while (sp && sp != spls.first);

        sp = spls.first;
        do {
            if (sp->next) {
		Conic *spl = sp->next;
		sp = sp->next->to;
		ttfApproxSpline (*this, spl, fudge);
	    } else
		sp = nullptr;
        } while (sp && sp != spls.first);
	// Used to reverse spline direction when converting to quadratic, but what if it was
	// already quadratic?
	//spls.reverse ();
    }
    roundToInt (false);
}

void DrawableFigure::toCubic () {
    for (auto &spls: contours) {
        ConicPoint *sp = spls.first;
        do {
	    sp->ttfindex = 0;
            if (sp->next) {
		Conic *spl = sp->next;
		if (!spl->islinear && spl->order2) {
		    ConicPoint *nsp = spl->to;
		    sp->nextcp.x  = sp->me.x + 2*(sp->nextcp.x-sp->me.x)/3;
		    sp->nextcp.y  = sp->me.y + 2*(sp->nextcp.y-sp->me.y)/3;
		    nsp->prevcp.x = nsp->me.x + 2*(nsp->prevcp.x-nsp->me.x)/3;
		    nsp->prevcp.y = nsp->me.y + 2*(nsp->prevcp.y-nsp->me.y)/3;
		}
		spl->order2 = false;
		spl->refigure ();
		sp = sp->next->to;
	    } else
		sp = nullptr;
        } while (sp && sp != spls.first);
    }
}

void Monotonic::reverse () {
    tstart = 1.0 - tstart;
    tend = 1.0 - tend;
    t = 1.0 - t;
    Monotonic *tmp = next;
    next = prev;
    prev = tmp;
    xup = !xup;
    yup = !yup;
}

namespace Simplify {
    bool ignoreSlopes = false;
    bool ignoreExtremum = false;
    bool mergeLines = false;
    bool cleanup = false;
    bool forceLines = false;
    bool chooseHV = false;
    bool smoothCurves = true;

    double error = .75;
    double lineFixup = .2;
    double tanBounds = 10;
}

static extended_t adjacent_match (Conic *s1, Conic *s2, bool s2forward) {
    /* Is every point on s2 close to a point on s1 */
    extended_t t, tdiff, t1 = -1;
    extended_t xoff, yoff;
    extended_t t1start, t1end;
    std::array<extended_t, 2> ts;
    int i;

    if ((xoff = s2->to->me.x-s2->from->me.x)<0) xoff = -xoff;
    if ((yoff = s2->to->me.y-s2->from->me.y)<0) yoff = -yoff;
    if (xoff>yoff)
	s1->conics[0].findExtrema (&ts[0], &ts[1]);
    else
	s1->conics[1].findExtrema (&ts[0], &ts[1]);
    if (s2forward) {
	t = 0;
	tdiff = 1/16.0;
	t1end = 1;
	for (i=1; i>=0 && ts[i]==-1; --i);
	t1start = i<0 ? 0 : ts[i];
    } else {
	t = 1;
	tdiff = -1/16.0;
	t1start = 0;
	t1end = (ts[0]==-1) ? 1.0 : ts[0];
    }

    for ( ; (s2forward && t<=1) || (!s2forward && t>=0 ); t += tdiff) {
	extended_t x1, y1, xo, yo;
	extended_t x = ((s2->conics[0].a*t+s2->conics[0].b)*t+s2->conics[0].c)*t+s2->conics[0].d;
	extended_t y = ((s2->conics[1].a*t+s2->conics[1].b)*t+s2->conics[1].c)*t+s2->conics[1].d;
	if (xoff>yoff)
	    t1 = s1->conics[0].iterateSplineSolveFixup (t1start, t1end, x);
	else
	    t1 = s1->conics[1].iterateSplineSolveFixup (t1start, t1end, y);
	if (t1<0 || t1>1)
	    return -1;
	x1 = ((s1->conics[0].a*t1+s1->conics[0].b)*t1+s1->conics[0].c)*t1+s1->conics[0].d;
	y1 = ((s1->conics[1].a*t1+s1->conics[1].b)*t1+s1->conics[1].c)*t1+s1->conics[1].d;
	if ((xo = (x-x1))<0) xo = -xo;
	if ((yo = (y-y1))<0) yo = -yo;
	if (xo+yo>.5)
	    return -1;
    }
    return t1;
}

bool ConicPoint::canInterpolate () const {
    return (
	!this->nonextcp && !this->noprevcp && (
        realWithin (this->me.x, (this->nextcp.x+this->prevcp.x)/2,.1) &&
	realWithin (this->me.y, (this->nextcp.y+this->prevcp.y)/2,.1)));
}

/* In truetype we can interpolate away an on curve point. Try this */
bool ConicPoint::interpolate (extended_t err) {
    ConicPoint *from, *to;
    BasePoint midme, test;
    int i, tot;
    bool good;
    std::vector<TPoint> tp;

    midme = this->me;
    from = this->prev->from; to = this->next->to;
    tp = Conic::figureTPsBetween (from, to);

    this->me.x = (this->prevcp.x + this->nextcp.x)/2;
    this->me.y = (this->prevcp.y + this->nextcp.y)/2;
    this->next->refigure ();
    this->prev->refigure ();

    i = tot = tp.size ();
    good = true;
    while (--i>0 && good) {
	/* GWW: tp[0] is the same as from (easier to include it), but the SplineNear*/
	/*  routine will sometimes reject the end-points of the spline */
	/*  so just don't check it */
	test.x = tp[i].x; test.y = tp[i].y;
	if (i>tot/2)
	    good =  this->next->pointNear (test, err, &tp[i].t) ||
		    this->prev->pointNear (test, err, &tp[i].t);
	else
	    good =  this->prev->pointNear (test, err, &tp[i].t) ||
		    this->next->pointNear (test, err, &tp[i].t);
    }
    if (!good) {
	this->me = midme;
	this->next->refigure ();
	this->prev->refigure ();
    }
    return good;
}

void ConicPoint::nextUnitVector (BasePoint *uv) {
    extended_t len;

    if (!this->next) {
	uv->x = uv->y = 0;
    } else if (this->next->islinear) {
	uv->x = this->next->to->me.x - this->me.x;
	uv->y = this->next->to->me.y - this->me.y;
    } else if (this->nonextcp) {
	uv->x = this->next->to->prevcp.x - this->me.x;
	uv->y = this->next->to->prevcp.y - this->me.y;
    } else {
	uv->x = this->nextcp.x - this->me.x;
	uv->y = this->nextcp.y - this->me.y;
    }
    len = sqrt (uv->x*uv->x + uv->y*uv->y);
    if (len!= 0) {
	uv->x /= len;
	uv->y /= len;
    }
}

/* Does the second derivative change sign around this point? If so we should */
/*  retain it for truetype */
bool ConicPoint::isD2Change () const {
    extended_t d2next = this->next->secondDerivative (0);
    extended_t d2prev = this->prev->secondDerivative (1);

    if (d2next>=0 && d2prev>=0)
	return false;
    if (d2next<=0 && d2prev<=0)
	return false;

    return true;
}

extended_t Conic::secondDerivative (extended_t t) const {
    /* That is d2y/dx2, not d2y/dt2 */

    /* dy/dx = (dy/dt) / (dx/dt) */
    /* d2 y/dx2 = d(dy/dx)/dt / (dx/dt) */
    /* d2 y/dx2 = ((d2y/dt2)*(dx/dt) - (dy/dt)*(d2x/dt2))/ (dx/dt)^2 */

    /* dy/dt = 3 ay *t^2 + 2 by * t + cy */
    /* dx/dt = 3 ax *t^2 + 2 bx * t + cx */
    /* d2y/dt2 = 6 ay *t + 2 by */
    /* d2x/dt2 = 6 ax *t + 2 bx */
    extended_t dydt = (3*this->conics[1].a*t + 2*this->conics[1].b)*t + this->conics[1].c;
    extended_t dxdt = (3*this->conics[0].a*t + 2*this->conics[0].b)*t + this->conics[0].c;
    extended_t d2ydt2 = 6*this->conics[1].a*t + 2*this->conics[1].b;
    extended_t d2xdt2 = 6*this->conics[0].a*t + 2*this->conics[0].b;
    extended_t top = (d2ydt2*dxdt - dydt*d2xdt2);

    if (dxdt==0) {
	if (top==0)
	    return 0;
	if (top>0)
	    return 1e10;
	return -1e10;
    }

    return top/(dxdt*dxdt);
}


void ConicPointList::nearlyHvLines (extended_t err) {
    Conic *s, *first=nullptr;

    for (s = this->first->next; s && s!=first; s=s->to->next) {
	if (!first) first = s;
	if (s->islinear) {
	    if (s->to->me.x-s->from->me.x<err && s->to->me.x-s->from->me.x>-err) {
		s->to->nextcp.x += (s->from->me.x-s->to->me.x);
		if (s->order2 && s->to->next)
		    s->to->next->to->prevcp.x = s->to->nextcp.x;
		s->to->me.x = s->from->me.x;
		s->to->prevcp = s->to->me;
		s->from->nextcp = s->from->me;
		s->from->nonextcp = s->to->noprevcp = true;
		s->refigure ();
		if (s->to->next)
		    s->to->next->refigure ();
	    } else if (s->to->me.y-s->from->me.y<err && s->to->me.y-s->from->me.y>-err) {
		s->to->nextcp.y += (s->from->me.y-s->to->me.y);
		if (s->order2 && s->to->next)
		    s->to->next->to->prevcp.y = s->to->nextcp.y;
		s->to->me.y = s->from->me.y;
		s->to->prevcp = s->to->me;
		s->from->nextcp = s->from->me;
		s->from->nonextcp = s->to->noprevcp = true;
		s->refigure ();
		if (s->to->next)
		    s->to->next->refigure ();
	    }
	}
    }
}

/* If the start point of a contour is not at an extremum, and the contour has */
/*  a point which is at an extremum, then make the start point be that point  */
/* leave it unchanged if start point is already extreme, or no extreme point  */
/*  could be found							      */
void ConicPointList::startToExtremum () {
    /* It's closed */
    if (this->first == this->last) {
	ConicPoint *sp;
        for (sp=this->first; !sp->isExtremum (); ) {
	    sp = sp->next->to;
	    if (sp==this->first)
		break;
        }
        if (sp!=this->first)
	    this->first = this->last = sp;
    }
}

bool ConicPointList::startToPoint (ConicPoint *nst) {
    if (first != last || nst == first)
	return false;
    ConicPoint *sp = this->first;
    do {
	if (sp == nst) {
	    first->isfirst = false;
	    first = last = nst;
	    nst->isfirst = true;
	    return true;
	}
        sp = (sp->next) ? sp->next->to : nullptr;
    } while (sp && sp != this->first);
    return false;
}

void ConicPointList::removeStupidControlPoints () {
    extended_t len, normal, dir;
    Conic *s, *first=nullptr;
    BasePoint unit, off;

    /* GWW: Also remove really stupid control points: Tiny offsets pointing in */
    /*  totally the wrong direction. Some of the TeX fonts we get have these */
    /* We get equally bad results with a control point that points beyond the */
    /*  other end point */
    for (s = this->first->next; s && s!=first; s=s->to->next) {
	unit.x = s->to->me.x-s->from->me.x;
	unit.y = s->to->me.y-s->from->me.y;
	len = sqrt (unit.x*unit.x+unit.y*unit.y);
	if (len!=0) {
	    bool refigure = false;
	    unit.x /= len; unit.y /= len;
	    if (!s->from->nonextcp) {
		off.x = s->from->nextcp.x-s->from->me.x;
		off.y = s->from->nextcp.y-s->from->me.y;
		if ((normal = off.x*unit.y - off.y*unit.x)<0) normal = -normal;
		dir = off.x*unit.x + off.y*unit.y;
		if ((normal<dir && normal<1 && dir<0) || (normal<.5 && dir<-.5) ||
			(normal<.1 && dir>len)) {
		    s->from->nextcp = s->from->me;
		    s->from->nonextcp = true;
		    refigure = true;
		}
	    }
	    if (!s->to->noprevcp) {
		off.x = s->to->me.x - s->to->prevcp.x;
		off.y = s->to->me.y - s->to->prevcp.y;
		if ((normal = off.x*unit.y - off.y*unit.x)<0 ) normal = -normal;
		dir = off.x*unit.x + off.y*unit.y;
		if ((normal<-dir && normal<1 && dir<0 ) || (normal<.5 && dir>-.5 && dir<0) ||
			(normal<.1 && dir>len)) {
		    s->to->prevcp = s->to->me;
		    s->to->noprevcp = true;
		    refigure = true;
		}
	    }
	    if (refigure)
		s->refigure ();
	}
	if (!first) first = s;
    }
}

bool ConicPointList::smoothControlPoints (extended_t tan_bounds, bool vert_check) {
    ConicPoint *sp;
    /* GWW: If a point has control points, and if those cps are in nearly the same */
    /*  direction (within tan_bounds) then adjust them so that they are in the*/
    /*  same direction */
    BasePoint unit, unit2;
    extended_t len, len2, para, norm, tn;
    bool changed=false, found;

    if (this->first->next && this->first->next->order2)
	return false;

    for (sp = this->first; ; ) {
	if ((!sp->nonextcp && !sp->noprevcp && sp->pointtype==pt_corner) ||
	    (sp->pointtype!=pt_tangent && (
		(!sp->nonextcp && sp->noprevcp && sp->prev && sp->prev->islinear) ||
		(!sp->noprevcp && sp->nonextcp && sp->next && sp->next->islinear)))) {
	    BasePoint *next = sp->nonextcp ? &sp->next->to->me : &sp->nextcp;
	    BasePoint *prev = sp->noprevcp ? &sp->prev->to->me : &sp->prevcp;
	    unit.x = next->x-sp->me.x;
	    unit.y = next->y-sp->me.y;
	    len = sqrt (unit.x*unit.x + unit.y*unit.y);
	    unit.x /= len; unit.y /= len;
	    para = (sp->me.x-prev->x)*unit.x + (sp->me.y-prev->y)*unit.y;
	    norm = (sp->me.x-prev->x)*unit.y - (sp->me.y-prev->y)*unit.x;
	    if (para==0)
		tn = 1000;
	    else
		tn = norm/para;
	    if (tn<0) tn = -tn;
	    if (tn<tan_bounds && para>0) {
		found = false;
		unit2.x = sp->me.x-sp->prevcp.x;
		unit2.y = sp->me.y-sp->prevcp.y;
		len2 = sqrt (unit2.x*unit2.x + unit2.y*unit2.y);
		unit2.x /= len2; unit2.y /= len2;
		if (vert_check) {
		    if (fabs (unit.x)>fabs (unit.y)) {
			/* Closer to horizontal */
			if ((unit.y<=0 && unit2.y>=0) || (unit.y>=0 && unit2.y<=0)) {
			    unit2.x = unit2.x<0 ? -1 : 1; unit2.y = 0;
			    found = true;
			}
		    } else {
			if ((unit.x<=0 && unit2.x>=0) || (unit.x>=0 && unit2.x<=0)) {
			    unit2.y = unit2.y<0 ? -1 : 1; unit2.x = 0;
			    found = true;
			}
		    }
		}
		/* If we're next to a line, we must extend the line. No choice */
		if (sp->nonextcp) {
		    if (len<len2)
			goto nextpt;
		    found = true;
		    unit2 = unit;
		} else if (sp->noprevcp) {
		    if (len2<len)
			goto nextpt;
		    found = true;
		} else if (!found) {
		    unit2.x = (unit.x*len + unit2.x*len2)/(len+len2);
		    unit2.y = (unit.y*len + unit2.y*len2)/(len+len2);
		}
		sp->nextcp.x = sp->me.x + len*unit2.x;
		sp->nextcp.y = sp->me.y + len*unit2.y;
		sp->prevcp.x = sp->me.x - len2*unit2.x;
		sp->prevcp.y = sp->me.y - len2*unit2.y;
		sp->pointtype = pt_curve;
		if (sp->prev)
		    sp->prev->refigure ();
		if (sp->next)
		    sp->next->refigure ();
		changed = true;
	    }
	}
    nextpt:
	if (!sp->next)
	    break;
	sp = sp->next->to;
	if (sp==this->first)
	    break;
    }
    return changed;
}

void DrawableFigure::ssRemoveBacktracks (ConicPointList &ss) {
    ConicPoint *sp = ss.first;

    do {
	if (sp->next && sp->prev) {
	    ConicPoint *nsp = sp->next->to, *psp = sp->prev->from, *isp;
	    BasePoint ndir, pdir;
	    extended_t dot, pdot, nlen, plen, t = -1;

	    ndir.x = (nsp->me.x - sp->me.x); ndir.y = (nsp->me.y - sp->me.y);
	    pdir.x = (psp->me.x - sp->me.x); pdir.y = (psp->me.y - sp->me.y);
	    nlen = ndir.x*ndir.x + ndir.y*ndir.y; plen = pdir.x*pdir.x + pdir.y*pdir.y;
	    dot = ndir.x*pdir.x + ndir.y*pdir.y;
	    if ((pdot = ndir.x*pdir.y - ndir.y*pdir.x)<0) pdot = -pdot;
	    if (dot>0 && dot>pdot) {
		if (nlen>plen && (t = adjacent_match (sp->next, sp->prev, false))!=-1) {
		    isp = bisectSpline (sp->next, t);
		    psp->nextcp.x = psp->me.x + (isp->nextcp.x-isp->me.x);
		    psp->nextcp.y = psp->me.y + (isp->nextcp.y-isp->me.y);
		    psp->nonextcp = isp->nonextcp;
		    psp->next = isp->next;
		    isp->next->from = psp;
		    splines_pool.free (isp->prev);
		    splines_pool.free (sp->prev);
		    points_pool.free (isp);
		    points_pool.free (sp);
		    if (psp->next->order2) {
			psp->nextcp.x = nsp->prevcp.x = (psp->nextcp.x+nsp->prevcp.x)/2;
			psp->nextcp.y = nsp->prevcp.y = (psp->nextcp.y+nsp->prevcp.y)/2;
			if (psp->nonextcp || nsp->noprevcp)
			    psp->nonextcp = nsp->noprevcp = true;
		    }
		    psp->next->refigure ();
		    if (ss.first==sp)
			ss.first = psp;
		    if (ss.last==sp)
			ss.last = psp;
		    sp=psp;
		} else if (nlen<plen && (t = adjacent_match (sp->prev, sp->next, true))!=-1) {
		    isp = bisectSpline (sp->prev, t);
		    nsp->prevcp.x = nsp->me.x + (isp->prevcp.x-isp->me.x);
		    nsp->prevcp.y = nsp->me.y + (isp->prevcp.y-isp->me.y);
		    nsp->noprevcp = isp->noprevcp;
		    nsp->prev = isp->prev;
		    isp->prev->to = nsp;
		    splines_pool.free (isp->next);
		    splines_pool.free (sp->next);
		    points_pool.free (isp);
		    points_pool.free (sp);
		    if (psp->next->order2) {
			psp->nextcp.x = nsp->prevcp.x = (psp->nextcp.x+nsp->prevcp.x)/2;
			psp->nextcp.y = nsp->prevcp.y = (psp->nextcp.y+nsp->prevcp.y)/2;
			if (psp->nonextcp || nsp->noprevcp)
			    psp->nonextcp = nsp->noprevcp = true;
		    }
		    nsp->prev->refigure ();
		    if (ss.first==sp)
			ss.first = psp;
		    if (ss.last==sp)
			ss.last = psp;
		    sp=psp;
		}
	    }
	}
	sp = sp->next ? sp->next->to : nullptr;
    } while (sp && sp != ss.first);
}

/* GWW: Almost exactly the same as SplinesRemoveBetween, but this one is conditional */
/*  the intermediate points/splines are removed only if we have a good match */
/*  used for simplify */
bool DrawableFigure::splinesRemoveBetweenMaybe (ConicPoint *from, ConicPoint *to, extended_t err) {
    int i;
    ConicPoint *afterfrom, *sp, *next;
    std::vector<TPoint> tp;
    BasePoint test;
    bool good;
    BasePoint fncp, tpcp;
    int fpt, tpt;
    bool order2 = from->next->order2;

    afterfrom = from->next->to;
    fncp = from->nonextcp ? from->me : from->nextcp;
    tpcp = to->noprevcp ? to->me : to->prevcp;
    fpt = from->pointtype; tpt = to->pointtype;

    if (afterfrom==to || from==to)
	return false;

    tp = Conic::figureTPsBetween (from, to);

    if (Simplify::ignoreSlopes)
	Conic::approximateFromPointsSlopes (from, to, tp, order2);
    else
	Conic::approximateFromPoints (from, to, tp, order2);

    i = tp.size ();

    good = true;
    while (--i>0 && good) {
	/* GWW: tp[0] is the same as from (easier to include it), but the SplineNear*/
	/*  routine will sometimes reject the end-points of the spline */
	/*  so just don't check it */
	test.x = tp[i].x; test.y = tp[i].y;
	good = from->next->pointNear (test, err, &tp[i].t);
    }

    if (good) {
	splines_pool.free (afterfrom->prev);
	for (sp=afterfrom; sp!=to; sp=next) {
	    next = sp->next->to;
	    splines_pool.free (sp->next);
	    points_pool.free (sp);
	}
	from->categorize ();
	to->categorize ();
    } else {
	splines_pool.free (from->next);
	from->next = afterfrom->prev;
	from->nextcp = fncp;
	from->nonextcp = (fncp.x==from->me.x && fncp.y==from->me.y);
	from->pointtype = fpt;
	for (sp=afterfrom; sp->next->to!=to; sp=sp->next->to);
	to->prev = sp->next;
	to->prevcp = tpcp;
	to->noprevcp = (tpcp.x==to->me.x && tpcp.y==to->me.y);
	to->pointtype = tpt;
    }
    return good;
}

bool DrawableFigure::splinesRemoveMidMaybeIndeed (ConicPoint *mid, extended_t err, extended_t lenmax2) {
    ConicPoint *from, *to;

    if (!mid->prev || !mid->next)
	return false;

    from = mid->prev->from; to = mid->next->to;

    /* GWW: Retain points which are horizontal or vertical, because postscript says*/
    /*  type1 fonts should always have a point at the extrema (except for small*/
    /*  things like serifs), and the extrema occur at horizontal/vertical points*/
    /* tt says something similar */
    if (!Simplify::ignoreExtremum && mid->isExtremum ())
	return false;

    /* GWW: In truetype fonts we also want to retain points where the second */
    /*  derivative changes sign */
    if (!Simplify::ignoreExtremum && mid->prev->order2 && mid->isD2Change ())
	return false;

    if (!Simplify::mergeLines &&
	(mid->pointtype==pt_corner || mid->prev->islinear || mid->next->islinear)) {
	/* GWW: Be very careful about merging straight lines. Generally they should*/
	/*  remain straight... */
	/* Actually it's not that the lines are straight, the significant thing */
	/*  is that if there is a abrupt change in direction at this point */
	/*  (a corner) we don't want to get rid of it */
	BasePoint prevu, nextu;
	extended_t plen, nlen;

	if (mid->next->islinear || mid->nonextcp) {
	    nextu.x = to->me.x-mid->me.x;
	    nextu.y = to->me.y-mid->me.y;
	} else {
	    nextu.x = mid->nextcp.x-mid->me.x;
	    nextu.y = mid->nextcp.y-mid->me.y;
	}
	if (mid->prev->islinear || mid->noprevcp) {
	    prevu.x = from->me.x-mid->me.x;
	    prevu.y = from->me.y-mid->me.y;
	} else {
	    prevu.x = mid->prevcp.x-mid->me.x;
	    prevu.y = mid->prevcp.y-mid->me.y;
	}
	nlen = sqrt (nextu.x*nextu.x + nextu.y*nextu.y);
	plen = sqrt (prevu.x*prevu.x + prevu.y*prevu.y);
	if (nlen==0 || plen==0)
	    /* GWW: Not a real corner */;
	else if ((nextu.x*prevu.x + nextu.y*prevu.y)/(nlen*plen)>((nlen+plen>20)?-.98:-.95)) {
	    /* GWW: If the cos if the angle between the two segments is too far */
	    /*  from parallel then don't try to smooth the point into oblivion */
	    extended_t flen, tlen;
	    flen =
		(from->me.x-mid->me.x)*(from->me.x-mid->me.x) +
		(from->me.y-mid->me.y)*(from->me.y-mid->me.y);
	    tlen =
		(to->me.x-mid->me.x)*(to->me.x-mid->me.x) +
		(to->me.y-mid->me.y)*(to->me.y-mid->me.y);
	    if ((flen<.7 && tlen<.7) || flen<.25 || tlen<.25)
		/* GWW: Too short to matter */;
	    else
		return false;
	}

	if (mid->prev->islinear && mid->next->islinear) {
	    /* GWW: Special checks for horizontal/vertical lines */
	    /* don't let the smoothing distort them */
	    if (from->me.x==to->me.x) {
		if (mid->me.x!=to->me.x)
		    return false;
	    } else if (from->me.y==to->me.y) {
		if (mid->me.y!=to->me.y)
		    return false;
	    } else if (!realRatio (
			(from->me.y-to->me.y)/(from->me.x-to->me.x),
			(mid->me.y-to->me.y)/(mid->me.x-to->me.x), .05)) {
		    return false;
	    }
	} else if (mid->prev->islinear) {
	    if ((mid->me.x-from->me.x)*(mid->me.x-from->me.x) +
		(mid->me.y-from->me.y)*(mid->me.y-from->me.y) > lenmax2)
		return false;
	} else {
	    if ((mid->me.x-to->me.x)*(mid->me.x-to->me.x) +
		(mid->me.y-to->me.y)*(mid->me.y-to->me.y) > lenmax2)
		return false;
	}
    }

    if (mid->next->order2) {
	if (from->canInterpolate () && to->canInterpolate () && mid->canInterpolate ())
	    return false;
    }

    return splinesRemoveBetweenMaybe (from, to, err);
}

/* GWW: A wrapper to SplinesRemoveBetweenMaybe to handle some extra checking for a */
/*  common case */
bool DrawableFigure::splinesRemoveMidMaybe (ConicPoint *mid, extended_t err, extended_t lenmax2) {
    bool changed1 = false;
    if (mid->next->order2) {
	if (!mid->nonextcp && !mid->noprevcp && !(
	    realWithin (mid->me.x, (mid->nextcp.x+mid->prevcp.x)/2, .1) &&
	    realWithin (mid->me.y, (mid->nextcp.y+mid->prevcp.y)/2, .1)))
	    changed1 = mid->interpolate (err);
    }
    return (splinesRemoveMidMaybeIndeed (mid, err, lenmax2) || changed1);
}

void DrawableFigure::forceLines (ConicPointList &spls, extended_t bump_size, int upm) {
    Conic *s, *first=nullptr;
    ConicPoint *sp;
    int any;
    BasePoint unit;
    extended_t len, minlen = upm/20.0;
    extended_t diff, xoff, yoff, len2;
    bool order2=false;

    if (spls.first->next && spls.first->next->order2)
	order2 = true;

    for (s = spls.first->next; s && s!=first; s=s->to->next) {
	if (!first) first = s;
	if (s->islinear) {
	    unit.x = s->to->me.x-s->from->me.x;
	    unit.y = s->to->me.y-s->from->me.y;
	    len = sqrt (unit.x*unit.x + unit.y*unit.y);
	    if (len<minlen)
		continue;
	    unit.x /= len; unit.y /= len;
	    do {
		any = false;
		if (s->from->prev && s->from->prev!=s) {
		    sp = s->from->prev->from;
		    len2 = sqrt(
			(sp->me.x-s->from->me.x)*(sp->me.x-s->from->me.x) +
			(sp->me.y-s->from->me.y)*(sp->me.y-s->from->me.y));
		    diff = (sp->me.x-s->from->me.x)*unit.y - (sp->me.y-s->from->me.y)*unit.x;
		    if (len2<len && fabs (diff) <= bump_size) {
			xoff = diff*unit.y; yoff = -diff*unit.x;
			sp->me.x -= xoff; sp->me.y -= yoff;
			sp->prevcp.x -= xoff; sp->prevcp.y -= yoff;
			if (order2 && sp->prev && !sp->noprevcp)
			    sp->prev->from->nextcp = sp->prevcp;
			sp->nextcp = sp->me; sp->nonextcp = true;
			if (sp->next == first) first = nullptr;
			splines_pool.free (sp->next);
			if (s->from==spls.first) {
			    if (spls.first==spls.last) spls.last = sp;
			    spls.first = sp;
			}
			points_pool.free (s->from);
			sp->next = s; s->from = sp;
			s->refigure ();
			if (sp->prev)
			    sp->prev->refigure ();
			sp->pointtype = pt_corner;
			any = true;

			/* We must recalculate the length each time, we */
			/*  might have shortened it. */
			unit.x = s->to->me.x-s->from->me.x;
			unit.y = s->to->me.y-s->from->me.y;
			len = sqrt(unit.x*unit.x + unit.y*unit.y);
			if (len<minlen)
			    break;
			unit.x /= len; unit.y /= len;
		    }
		}
		if (s->to->next && s->to->next!=s) {
		    sp = s->to->next->to;
		    /* GWW: If the next spline is a longer line than we are, then don't */
		    /*  merge it to us, rather merge us into it next time through the loop */
		    /* Hmm. Don't merge out the bump in general if the "bump" is longer than we are */
		    len2 = sqrt (
			(sp->me.x-s->to->me.x)*(sp->me.x-s->to->me.x) +
			(sp->me.y-s->to->me.y)*(sp->me.y-s->to->me.y));
		    diff = (sp->me.x-s->to->me.x)*unit.y - (sp->me.y-s->to->me.y)*unit.x;
		    if (len2<len && fabs(diff)<=bump_size) {
			xoff = diff*unit.y; yoff = -diff*unit.x;
			sp->me.x -= xoff; sp->me.y -= yoff;
			sp->nextcp.x -= xoff; sp->nextcp.y -= yoff;
			if (order2 && sp->next && !sp->nonextcp)
			    sp->next->to->prevcp = sp->nextcp;
			sp->prevcp = sp->me; sp->noprevcp = true;
			if (sp->prev==first) first = nullptr;
			splines_pool.free (sp->prev);
			if (s->to==spls.last) {
			    if (spls.first==spls.last) spls.first = sp;
			    spls.last = sp;
			}
			points_pool.free (s->to);
			sp->prev = s; s->to = sp;
			s->refigure ();
			if (sp->next)
			    sp->next->refigure ();
			sp->pointtype = pt_corner;
			any = true;

			unit.x = s->to->me.x-s->from->me.x;
			unit.y = s->to->me.y-s->from->me.y;
			len = sqrt (unit.x*unit.x + unit.y*unit.y);
			if (len<minlen)
			    break;
			unit.x /= len; unit.y /= len;
		    }
		}
	    } while (any);
	}
    }
}

void DrawableFigure::ssSimplify (ConicPointList &spls, int upm, double lenmax2) {
    ConicPoint *first, *next;
    BasePoint suv, nuv;

    removeZeroLengthSplines (&spls, false, 0.1);
    spls.removeStupidControlPoints ();
    ssRemoveBacktracks (spls);
    spls.startToExtremum ();
    /* Ignore any splines which are just dots */
    if (spls.first->next && spls.first->next->to==spls.first &&
        spls.first->nonextcp && spls.first->noprevcp)
        return;

    if (Simplify::cleanup && Simplify::forceLines) {
        spls.nearlyHvLines (Simplify::lineFixup);
        forceLines (spls, Simplify::lineFixup, upm);
    }

    if (Simplify::cleanup && spls.first->prev && spls.first->prev!=spls.first->next ) {
        /* GWW: first thing to try is to remove everything between two extrema */
        /* We do this even if they checked ignore extrema. After this pass */
        /*  we'll come back and check every point individually */
        /* However, if we turn through more than 90 degrees we can't approximate */
        /*  a good match, and it takes us forever to make the attempt and fail*/
        /*  We take a dot product to prevent that */
	ConicPoint *sp = spls.first;
        do {
	    if (sp->isExtremum ()) {
		sp->nextUnitVector (&suv);
		ConicPoint *nsp = sp->next->to;
		bool nogood = false;
		while (nsp && nsp->next && nsp != sp) {
		    if (!nsp->prev->islinear &&
			    (nsp->me.x-nsp->prev->from->me.x)*(nsp->me.x-nsp->prev->from->me.x) +
			    (nsp->me.y-nsp->prev->from->me.y)*(nsp->me.y-nsp->prev->from->me.y)
			    >= lenmax2) {
			nogood = true;
			break;
		    }
		    nsp->nextUnitVector (&nuv);
		    if (suv.x*nuv.x + suv.y*nuv.y < 0) {
			if (suv.x*nuv.x + suv.y*nuv.y > -.1)
			    break;
			nogood = true;
			break;
		    }
		    nsp = nsp->next->to;
		    if (nsp->isExtremum () || nsp==spls.first)
			break;
		}
		/* GWW: nsp is something we don't want to remove */
		if (!nogood) {
		    if (nsp == sp)
			break;
		    if (sp->next->to != nsp && splinesRemoveBetweenMaybe (sp, nsp, Simplify::error)) {
			/* GWW: We know this point didn't get removed */
			if (spls.last==spls.first)
			    spls.last = spls.first = sp;
		    }
		}
		sp = nsp;
	    } else {
		sp = sp->next->to;
	    }
        } while (sp != spls.first && sp->next);

        while (true) {
	    first = spls.first->prev->from;
	    if (first->prev == first->next)
		return;
	    if (!splinesRemoveMidMaybe (spls.first, Simplify::error, lenmax2))
		break;
	    if (spls.first==spls.last)
		spls.last = first;
	    spls.first = first;
        }
    }

    /* GWW: Special case checks for paths containing only one point */
    /*  else we get lots of nans (or only two points) */
    if (!spls.first->next)
        return;
    for (ConicPoint *sp = spls.first->next->to; sp->next; sp = next) {
        /* GWW: First see if we can turn it*/
        /* into a line, then try to merge two splines */
	sp->prev->adjustLinear ();
        next = sp->next->to;
        if (sp->prev == sp->next ||
	    (sp->next && sp->next->to->next && sp->next->to->next->to == sp))
	    return;
        if (!Simplify::cleanup) {
	    if (splinesRemoveMidMaybe (sp, Simplify::error, lenmax2)) {
		if (spls.first==sp)
		    spls.first = next;
		if (spls.last==sp)
		    spls.last = next;
		continue;
	    }
        } else {
	    while  (sp->me.x==next->me.x && sp->me.y==next->me.y &&
		    sp->nextcp.x>sp->me.x-1 && sp->nextcp.x<sp->me.x+1 &&
		    sp->nextcp.y>sp->me.y-1 && sp->nextcp.y<sp->me.y+1 &&
		    next->prevcp.x>next->me.x-1 && next->prevcp.x<next->me.x+1 &&
		    next->prevcp.y>next->me.y-1 && next->prevcp.y<next->me.y+1) {
		splines_pool.free (sp->next);
		sp->next = next->next;
		if (sp->next)
		    sp->next->from = sp;
		sp->nextcp = next->nextcp;
		sp->nonextcp = next->nonextcp;
		points_pool.free (next);
		if (sp->next)
		    next = sp->next->to;
		else
		    break;
	    }
	    if (!next)
		break;
        }
        if (next->prev && next->prev->from==spls.last)
	    break;
    }
    if (Simplify::cleanup && Simplify::smoothCurves)
        spls.smoothControlPoints (Simplify::tanBounds, Simplify::chooseHV);
}

/* GWW: Cleanup just turns splines with control points which happen to trace out */
/*  lines into simple lines */
/* it also checks for really nasty control points which point in the wrong */
/*  direction but are very close to the base point. We get these from some */
/*  TeX fonts. I assume they are due to rounding errors (or just errors) in*/
/*  some autotracer */
bool DrawableFigure::simplify (bool selected, int upm) {
    double lenmax = upm/100;
    extended_t lenmax2 = lenmax*lenmax;
    bool ret = false;

    if (contours.empty ())
	return ret;

    for (auto &spls: contours) {
	if (!selected || spls.isSelected ()) {
	    ret |= true;
	    ssSimplify (spls, upm, lenmax2);
	}
    }
    return ret;
}
