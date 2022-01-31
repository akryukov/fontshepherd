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

#ifndef _FONTSHEPHERD_STEMDB_H_
#define _FONTSHEPHERD_STEMDB_H_

struct st {
    Conic *s;
    extended_t st, lt;
};

struct segment {
    double start, end, sbase, ebase;
    bool curved, scurved, ecurved;
};

namespace Units {
    int isHV (const BasePoint *unit, bool strict);
    int closerToHV (BasePoint *u1, BasePoint *u2);
    double getAngle (BasePoint *u1, BasePoint *u2);
    bool orthogonal (BasePoint *u1, BasePoint *u2, bool strict);
    bool parallel (BasePoint *u1, BasePoint *u2, bool strict);
    BasePoint calcMiddle (BasePoint *unit1, BasePoint *unit2);
}

namespace Monotonics {
    int findAt (std::deque<Monotonic> &ms, bool which, extended_t test, std::vector<Monotonic *> &space);
    int order (Conic *line, std::vector<Conic *> sspace, std::vector<struct st> &stspace);
    Conic *findAlong (Conic *line, std::vector<struct st> &stspace, Conic *findme, double *other_t);
}

class GlyphData;
class StemData;
class PointData {
public:
    PointData ();
    void init (GlyphData &gd, ConicPoint *sp, ConicPointList *ss);

    void assignStem (StemData *stem, bool is_next, bool left);
    bool parallelToDir (bool checknext, BasePoint *dir, BasePoint *opposite, ConicPoint *basesp, uint8_t is_stub);
    StemData *checkRelated (bool next);

    ConicPoint *sp = nullptr;
    ConicPointList *m_ss = nullptr;

    /* normally same as sp->me, but needed for offcurve points */
    BasePoint base;
    /* unit vectors pointing in the next/prev directions */
    BasePoint nextunit { 0, 0 }, prevunit { 0, 0 };
    /* any other points lying on approximately the same line */
    struct linedata *nextline = nullptr, *prevline = nullptr;
    /* There should always be a matching spline, which may end up as part of a stem, and may not */
    std::array<Conic *, 2> nextedges { nullptr }, prevedges { nullptr };
    /* Location on other edge where our normal hits it */
    std::array<double, 2> next_e_t { 0 }, prev_e_t { 0 };
    /* Distance from the point to the matching edge */
    std::array<double, 2> next_dist { 0 }, prev_dist { 0 };
    int next_e_cnt = 0, prev_e_cnt = 0;

    // findMatchingEdge needs an array here, as next_e_t/prev_e_t
    // may be used in the same context. Naturally, in the original C code
    // a pointer to a single double or to a double array could easily
    // be interchanged. Currently we are assuming only bothedge[0]
    // and both_e_t[0] are actually used
    std::array<Conic *, 2> bothedge { 0 };
    std::array<double, 2> both_e_t { 0 };

    std::vector<StemData *> nextstems, prevstems;
    std::vector<int> next_is_l, prev_is_l;
    int next_pref = -1, prev_pref = -1;
    /* GWW: Temporary value, used to compare points assigned to the same
     * edge and determine which one can be used as a reference point */
    int value;

    bool nextlinear: 1;
    bool nextzero: 1;
    bool prevlinear: 1;
    bool prevzero: 1;
    bool colinear: 1;
    bool symetrical_h: 1;	/* Are next & prev symetrical? */
    bool symetrical_v: 1;	/* Are next & prev symetrical? */
    bool next_hor: 1;
    bool next_ver: 1;
    bool prev_hor: 1;
    bool prev_ver: 1;
    bool ticked: 1;

    uint8_t touched = 0, affected = 0;
    uint8_t x_extr = 0, y_extr = 0;
    uint8_t x_corner = 0, y_corner = 0;
    BasePoint newpos {0, 0};
    BasePoint newnext {0, 0}, newprev {0, 0};
    /* If point has been positioned in 1 direction, this is that direction */
    BasePoint posdir {0, 0};

    /* temporary value */
    double projection = 0;
};

typedef struct linedata {
    BasePoint unit { 0, 0 };
    BasePoint online { 0, 0 };
    uint8_t is_left = 0;
    double length = 0;
    std::vector<PointData *> points;

    bool fitsHV () const;
    std::string repr () const;
} LineData;

struct stem_chunk {
    StemData *parent;
    PointData *l;
    PointData *r;
    uint8_t lpotential = 0, rpotential = 0;
    /* are we using the next/prev side of the left/right points */
    uint8_t lnext = 0, rnext = 0;
    uint8_t ltick = 0, rtick = 0;
    uint8_t stub = 0;
    /* It's not a real stem, but it's something we'd like PostScript to hint for us */
    uint8_t stemcheat = 0;
    /* Specifies if this chunk marks the opposite sides of a ball terminal (useful for TTF instructions) */
    bool is_ball = false;
    StemData *ball_m = nullptr;
    /* Which of the opposed edges assigned to the left and right points corresponds to this chunk */
    int l_e_idx = 0, r_e_idx = 0;
};

typedef struct vchunk {
    struct stem_chunk *chunk;
    double dist;
    int parallel;
    int value;
} VChunk;

struct dependent_serif {
    StemData *stem;
    double width;           /* The distance from an edge of the main stem to the opposite edge of the serif stem */
    bool lbase;
    bool is_ball;
};

struct dependent_stem {
    StemData *stem;
    bool lbase;
    char dep_type;          /* can be 'a' (align), 'i' (interpolate), 'm' (move) or 's' (serif) */
};

class StemData {
public:
    StemData (BasePoint *dir, BasePoint *pos1, BasePoint *pos2);

    bool onStem (BasePoint *test, int left);
    bool bothOnStem (BasePoint *test1, BasePoint *test2, int force_hv,bool strict, bool cove);
    bool pointOnDiag (PointData *pd, bool only_hv);
    int assignedToPoint (PointData *pd, bool is_next);
    bool fitsHV (bool is_x, uint8_t mask);

    bool recalcOffsets (BasePoint *dir, bool left, bool right);
    void setUnit (BasePoint dir);

    PointData *findClosestOpposite (stem_chunk **chunk, ConicPoint *sp, int *next);

    bool wouldConflict (StemData *other);
    bool validConflictingStem (StemData *other);
    double activeOverlap (StemData *other);

    bool hasDependentStem (StemData *slave);
    bool preferEndDep (StemData *smaster, StemData *emaster, char s_type, char e_type);
    void lookForMasterHVStem ();
    void clearUnneededDeps ();

    std::string repr () const;

    /* Unit vector pointing in direction of stem */
    BasePoint unit = { 0, 0 };
    /* Unit vector pointing from left to right (across stem) */
    BasePoint l_to_r = { 0, 0 };
    /* a point on one side of the stem (not necissarily left, even for vertical stems) */
    BasePoint left = { 0, 0 };
    /* and one on the other */
    BasePoint right = { 0, 0 };
    /* Unit vector after repositioning (e. g. in Metafont) */
    BasePoint newunit = { 0, 0 };
    /* Left and right edges after repositioning */
    BasePoint newleft = { 0, 0 }, newright = { 0, 0 };
    /* TTF indices of the left and right key points */
    int leftidx=0, rightidx=0;
    /* Uppest and lowest points on left and right edges. Used for positioning diagonal stems */
    std::array<PointData *, 4> keypts { { 0, 0 } };
    double lmin=0, lmax=0, rmin=0, rmax=0;
    double width=0;
    std::vector<struct stem_chunk> chunks;
    std::vector<struct segment> active;
    /* Stem is fatter than tall, unlikely to be a real stem */
    int  toobig = 0;
    bool positioned: 1;
    bool ticked: 1;
    bool ghost: 1;
    bool bbox: 1;
    bool ldone: 1, rdone: 1;
    bool italic: 1;
    StemInfo  *blue = nullptr;		/* Blue zone a ghost hint is attached to */
    double len=0, clen=0;		/* Length of linear segments. clen adds "length" of curved bits */
    struct stembundle *bundle = nullptr;
    int lpcnt = 0, rpcnt = 0;           /* Count of points assigned to left and right edges of this stem */
    struct linedata *leftline = nullptr, *rightline = nullptr;
    StemData *master = nullptr, *next_c_m = nullptr, *prev_c_m = nullptr;
    int confl_cnt = 0;

    int stem_idx = -1;

    /* Lists other stems dependent from the given stem */
    std::vector<struct dependent_stem> dependent;
    /* Lists serifs and other elements protruding from the base stem */
    std::vector<struct dependent_serif> serifs;
};

typedef struct stembundle {
    /* All these stems are parallel, pointing in unit direction */
    BasePoint unit = { 0, 0 };
    /* Axis along which these stems are ordered (normal to unit) */
    BasePoint l_to_r = { 0, 0 };
    /* Base point for measuring by l_to_r (stem->lpos,rpos) */
    BasePoint bp = { 0, 0 };
    std::vector<StemData *> stemlist;

    std::string repr () const;
} StemBundle;

class GlyphData {
public:
    GlyphData (sFont *fnt, ConicGlyph &g, bool only_hv, bool use_existing);

    bool order2 () const;
    int realCnt () const;
    int pointCnt () const;
    PointData *points (int idx);
    int isSplinePeak (PointData *pd, bool outer, bool is_x, int flags);
    bool figureHintMasks ();
    bool figureCounterMasks (std::vector<HintMask> &cm_list);

    struct stembundle hbundle;
    struct stembundle vbundle;
    struct stembundle ibundle;

private:
    void figureBlues (const PrivateDict *pd);
    bool isInflectionPoint (PointData *pd);
    int getValidPointDataIndex (ConicPoint *sp, StemData *stem);
    int monotonicFindStemBounds (Conic *line, std::vector<struct st> &stspace, double fudge, StemData *stem);
    int findMatchingHVEdge (PointData *pd, int is_next, std::array<Conic *, 2> &edges, std::array<double, 2> &other_t, std::array<double, 2> &dist);
    void makeVirtualLine (BasePoint *perturbed, BasePoint *dir, Conic *myline, ConicPoint *end1, ConicPoint *end2);
    int findMatchingEdge (PointData *pd, int is_next, std::array<Conic *, 2> &edges);
    bool stillStem (double fudge, BasePoint *pos, StemData *stem);
    bool isCorrectSide (PointData *pd, bool is_next, bool is_l, BasePoint *dir);
    struct linedata *buildLine (PointData *pd, bool is_next);
    uint8_t isStubOrIntersection (BasePoint *dir1, PointData *pd1, PointData *pd2, bool is_next1, bool is_next2);
    void swapStemEdges (StemData *stem);
    struct stem_chunk *addToStem (StemData *stem, PointData *pd1, PointData *pd2, int is_next1, int is_next2, bool cheat);
    StemData *findStem (PointData *pd, PointData *pd2, BasePoint *dir, bool is_next2, bool de);
    StemData *findOrMakeHVStem (PointData *pd, PointData *pd2, bool is_h, bool require_existing);
    int isDiagonalEnd (PointData *pd1, PointData *pd2, bool is_next);
    StemData *testStem (PointData *pd, BasePoint *dir, ConicPoint *match, bool is_next, bool is_next2, bool require_existing, uint8_t is_stub, int eidx);
    int halfStemNoOpposite (PointData *pd, StemData *stem, BasePoint *dir, bool is_next);
    StemData *halfStem (PointData *pd, BasePoint *dir, Conic *other, double other_t, bool is_next, int eidx);
    bool connectsAcross (ConicPoint *sp, bool is_next, Conic *findme, int eidx);
    bool connectsAcrossToStem (PointData *pd, bool is_next, StemData *target, bool is_l, int eidx);
    int buildStem (PointData *pd, bool is_next, bool require_existing, bool has_existing, int eidx);
    void assignLinePointsToStems ();
    StemData *diagonalCornerStem (PointData *pd, int require_existing);
    int valueChunk (std::vector<struct vchunk> &vchunks, int idx, bool l_base);
    void checkPotential (PointData *pd, int is_next);
    bool stemIsActiveAt (StemData *stem, double stempos);
    int walkSpline (PointData *pd, bool gonext, StemData *stem, bool is_l, bool force_ac, BasePoint *res);
    int addLineSegment (StemData *stem, std::vector<struct segment> &space, bool is_l, PointData *pd, bool base_next);
    void figureStemActive (StemData *stem);
    void stemsFixupIntersects ();
    void normalizeStubs ();
    void findUnlikelyStems ();
    void findRefPointsExisting (StemData *stem);
    void findRefPointsNew (StemData *stem);
    void normalizeStem (StemData *stem);
    void assignPointsToBBoxHint (DBounds *bounds, StemData *stem, bool is_v);
    void checkForBoundingBoxHints ();
    StemData *findOrMakeGhostStem (ConicPoint *sp, StemInfo *blue, double width, bool use_existing);
    void figureGhostActive (StemData *stem);
    void checkForGhostHints (bool use_existing);
    void markDStemCorner (PointData *pd);
    void markDStemCorners ();
    void bundleStems (int maxtoobig);
    void addSerifOrBall (StemData *master, StemData *slave, bool lbase, bool is_ball);
    bool isBall (PointData *pd, StemData *master, bool lbase);
    void getSerifData (StemData *stem);
    void findCounterGroups (bool is_v);
    void stemInfoToStemData (StemInfo *si, DBounds *bounds, int is_v);

    sFont *m_font;
    ConicGlyph &m_glyph;
    bool m_hv;

    DrawableFigure *m_fig;
    int m_fuzz;
    int m_realcnt;
    bool m_hasSlant = false;
    BasePoint m_slantUnit = { 0, 0 };

    // Let's store blues as stems: above all, they also a start and a width,
    // and that's exactly what we need
    std::vector<StemInfo> m_blues;

    /* Includes control points, excludes implied points */
    std::vector<PointData> m_points;
    std::vector<struct linedata> m_lines;
    std::vector<StemData> m_stems;

    std::deque<struct monotonic> m_ms;
    std::vector<Conic *> m_sspace;
    DBounds m_size;

public:
    /* A diagonal end is like the top or bottom of a slash. Should we add a vertical stem at the end?
     * A diagonal corner is like the bottom of circumflex. Should we add a horizontal stem? */
    static bool
	hint_diagonal_ends,
    	hint_diagonal_intersections,
    	hint_bounding_boxes,
    	detect_diagonal_stems;

    /* The maximum possible distance between the edge of an active zone for
     * a curved spline segment and the spline itself */
    static double
	dist_error_hv,
	dist_error_diag,
	/* GWW: It's easy to get horizontal/vertical lines aligned properly
	 * it is more difficult to get diagonal ones done
	 * The "A" glyph in Apple's Times.dfont(Roman) is off by 6 in one spot */
	dist_error_curve;

    const static double stem_slope_error, stub_slope_error;
    static bool splineFigureOpticalSlope (Conic *s, bool start_at_from, BasePoint *dir);

private:
    static int getBlueFuzz (const PrivateDict *pd);

private:
    GlyphData (const GlyphData &);
    GlyphData& operator=(const GlyphData &);
};

#endif
