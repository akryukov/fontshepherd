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

#include <cstdio>
#include <iostream>
#include <stdint.h>
#include <sstream>
#include <vector>
#include <set>
#include <deque>
#include <boost/pool/object_pool.hpp>
#include <pugixml.hpp>
#include <QtCore>
#include <QUndoStack>
#include "charbuffer.h"

#include "colors.h"
#include "cffstuff.h"

#define HntMax	96		/* PS says at most 96 hints */

#define _On_Curve	1
#define _X_Short	2
#define _Y_Short	4
#define _Repeat		8
#define _X_Same		0x10
#define _Y_Same		0x20

typedef long double extended_t;
typedef struct ttffont sFont;

enum class OutlinesType {
    NONE = 0, TT = 1, PS = 2, SVG = 4, COLR = 8
};

enum class OverlapType {
    Remove, RemoveSelected, Intersect, Intersel, Exclude, FindInter, Fisel
};

enum class ElementType {
    Reference, Circle, Ellipse, Rect, Polygon, Polyline, Line, Path
};

typedef struct hint_mask {
    hint_mask () : byte { 0 } {};
    hint_mask (const struct hint_mask &hm) {
	std::copy (hm.byte, hm.byte+HntMax/8, this->byte);
    };
    ~hint_mask () {};
    uint8_t& operator[](int i) {
        return byte[i];
    };
    struct hint_mask& operator = (const struct hint_mask &hm) {
	std::copy (hm.byte, hm.byte+HntMax/8, this->byte);
	return *this;
    };
    bool bit (int pos) {
	uint8_t nbyte = pos/8;
	uint8_t nbit  = 7-(pos%8);
	return ((byte[nbyte] >> nbit)&1);
    };
    void setBit (int pos, bool val) {
	uint8_t nbyte = pos/8;
	uint8_t nbit  = 7-(pos%8);
	if (val)
	    byte[nbyte] |= (true << nbit);
	else
	    byte[nbyte] &= ~(true << nbit);
    }

    uint8_t byte[HntMax/8];
} HintMask;

typedef struct steminfo {
    int16_t hintnumber;		/* when dumping out hintmasks we need to know */
				/*  what bit to set for this hint */
    double start;		/* location at which the stem starts */
    double width;		/* or height */
} StemInfo;

#ifndef _FS_STRUCT_BASEPOINT_DEFINED
#define _FS_STRUCT_BASEPOINT_DEFINED
typedef struct ipoint {
    int32_t x = 0;
    int32_t y = 0;
} IPoint;

typedef struct basepoint {
    double x;
    double y;

    void transform (basepoint *from, const std::array<double, 6> &transform);
} BasePoint;
#endif

#if 0
typedef struct basepoint {
    double x;
    double y;

    basepoint () : x (0), y (0) {};
    basepoint (const struct basepoint &bp) : x (bp.x), y (bp.y) {};
    basepoint (double xval, double yval) : x (xval), y (yval) {};
    void transform (basepoint *from, double transform[6]);
    struct basepoint& operator = (const struct basepoint &bp) {
	x = bp.x;
	y = bp.y;
	return *this;
    };
} BasePoint;
#endif

typedef struct tpoint {
    double x;
    double y;
    double t;
} TPoint;


#ifndef _FS_STRUCT_DBOUNDS_DEFINED
#define _FS_STRUCT_DBOUNDS_DEFINED
typedef struct dbounds {
    double minx, maxx;
    double miny, maxy;
} DBounds;
#endif

typedef struct ibounds {
    short int minx, maxx;
    short int miny, maxy;
} iBounds;

#ifndef _FS_ENUM_POINTTYPE_COLOR_DEFINED
#define _FS_ENUM_POINTTYPE_COLOR_DEFINED
enum pointtype { pt_curve, pt_corner, pt_tangent };
#endif
class ConicPointItem;

class Conic;
class ConicPoint {
public:
    BasePoint me;
    BasePoint nextcp;		/* control point (nullptr for lines) */
    BasePoint prevcp;		/* control point, shared with pt at other end of conic spline */
    unsigned int pointtype;
    bool nonextcp: 1;
    bool noprevcp: 1;
    bool checked: 1;
    bool selected: 1;
    bool isfirst: 1;
    int ttfindex, nextcpindex;
    // Temporary index, may be used to assign some number to an implied point
    int ptindex;
    Conic *next, *prev;
    std::unique_ptr<HintMask> hintmask;
    ConicPointItem *item;	/* For UI */

    ConicPoint ();
    ConicPoint (const ConicPoint &other_pt);
    ConicPoint& operator = (const ConicPoint &other_pt);
    ~ConicPoint ();

    ConicPoint (double x, double y);
    void doTransform (const std::array<double, 6> &transform);
    void categorize ();
    bool isExtremum ();
    void moveBasePoint (BasePoint newpos);
    void moveControlPoint (BasePoint newpos, bool is_next);
    BasePoint defaultCP (bool is_next, bool order2, bool snaptoint=false);
    bool meChanged () const;
    bool cpChanged (bool is_next) const;
    void setCpChanged (bool is_next, bool val);
    bool noCP (bool is_next) const;
    void setNoCP (bool is_next, bool val);
    void joinCpFixup (bool order2);
    bool isFirst () const;
    bool roundToInt (bool order2);

    bool canInterpolate () const;
    bool interpolate (extended_t err);
    void nextUnitVector (BasePoint *uv);
    bool isD2Change () const;

private:
    bool me_changed: 1;		// For GUI: indicate the corresponding graphical item needs update
    bool nextcp_changed: 1;
    bool prevcp_changed: 1;
};

typedef struct conicpointlist ConicPointList;

typedef struct monotonic {
    Conic *s;
    ConicPointList *contour;
    extended_t tstart, tend;
    struct monotonic *next, *prev;	/* along original contour */
    uint8_t xup;			/* increasing t => increasing x */
    uint8_t yup;
    //bool isneeded : 1;
    //bool isunneeded : 1;
    //bool mutual_collapse : 1;
    bool exclude;
    //struct intersection *start;
    //struct intersection *end;
    DBounds b;
    extended_t other, t;
    //struct monotonic *linked;		/* singly linked list of all monotonic*/
    					/*  segments, no contour indication */
    //double when_set;			/* Debugging */
    //struct preintersection *pending;

    void reverse ();
} Monotonic;

typedef struct conic1d {
    double a, b, c, d;
    void findExtrema (extended_t *_t1, extended_t *_t2) const;
    extended_t solve (double tmin, double tmax, extended_t sought);
    void iterateSolve (std::array<extended_t, 3> &ts);
    double closestSplineSolve (double sought, double close_to_t);
    extended_t iterateSplineSolveFixup (extended_t tmin, extended_t tmax, extended_t sought);

private:
    bool  cubicSolve (extended_t sought, std::array<extended_t, 3> &ts);
    bool _cubicSolve (extended_t sought, std::array<extended_t, 3> &ts);
    extended_t iterateConicSolve (extended_t tmin, extended_t tmax, extended_t sought);
} Conic1D;

typedef struct spline1 {
    Conic1D spline;
    double s0, s1;
    double c0, c1;

    void figure (extended_t t0, extended_t t1, Conic1D &spl);
} Spline1;

class Conic {
public:
    bool islinear: 1;		/* No control points */
    bool order2: 1;		/* No control points */
    bool isticked: 1;
    bool touched: 1;
    ConicPoint *from, *to;
    Conic1D conics[2];		/* conics[0] is the x conic, conics[1] is y */

    Conic ();
    Conic (ConicPoint *from, ConicPoint *to, bool order2);

    void refigure ();
    bool xSolve (double tmin, double tmax, BasePoint bp, double fudge, double *tptr);
    bool ySolve (double tmin, double tmax, BasePoint bp, double fudge, double *tptr);
    extended_t iSolveWithin (int major, extended_t val, extended_t tlow, extended_t thigh) const;

    bool nearXSpline (BasePoint bp, double fudge, double *tptr);
    bool pointNear (BasePoint bp, double fudge, double *tptr);
    int  findExtrema (std::array<extended_t, 4> &extrema) const;
    int  findInflectionPoints (std::array<extended_t, 2> &poi) const;
    int  intersects (const Conic *s2, std::array<BasePoint, 9> &pts, std::array<extended_t, 10> &t1s, std::array<extended_t, 10> &t2s) const;
    Monotonic *toMonotonic (ConicPointList *ss, std::deque<struct monotonic> &mpool, extended_t startt, extended_t endt, bool exclude);

    double lenApprox () const;
    double length () const;
    double sigmaDeltas (std::vector<TPoint> &mid, DBounds *b, struct dotbounds *db);
    void testForLinear ();
    bool adjustLinear ();
    double curvature (double t) const;
    double recalcT (ConicPoint *from, ConicPoint *to, double curt);
    void findBounds (DBounds &b);

    extended_t secondDerivative (extended_t t) const;

    static std::vector<TPoint> figureTPsBetween (ConicPoint *from, ConicPoint *to);
    static Conic * approximateFromPoints (ConicPoint *from, ConicPoint *to, std::vector<TPoint> &mid, bool order2);
    static Conic * approximateFromPointsSlopes (ConicPoint *from, ConicPoint *to, std::vector<TPoint> &mid, bool order2);
    static Conic * isLinearApprox (ConicPoint *from, ConicPoint *to, std::vector<TPoint> &mid, bool order2);
    static const double CURVATURE_ERROR;

private:
    bool cantExtremeX () const;
    bool cantExtremeY () const;
    bool coincides (const Conic *s2) const;
    bool minMaxWithin ();
    bool isLinear ();

    static bool _approximateFromPoints (ConicPoint *from, ConicPoint *to, std::vector<TPoint> &mid, BasePoint *nextcp, BasePoint *prevcp, bool order2);
};

enum em_linecap { lc_inherit = 0, lc_butt, lc_round, lc_square };
enum em_linejoin { lj_inherit = 0, lj_miter, lj_round, lj_bevel };

typedef struct svg_state {
    struct rgba_color fill, stroke;
    uint16_t fill_idx, stroke_idx;
    bool fill_set, stroke_set;
    int stroke_width;
    em_linecap linecap;
    em_linejoin linejoin;
    bool point_props_set;
    std::string fill_source_id;
    std::string stroke_source_id;

    svg_state ();
    svg_state (const struct svg_state &oldstate);
    ~svg_state ();

    void setFillColor (const std::string &arg);
    void setStrokeColor (const std::string &arg);
    std::string fillColor ();
    std::string strokeColor ();
    float fillOpacity ();
    void setFillOpacity (float val);
    float strokeOpacity ();
    void setStrokeOpacity (float val);
    int strokeWidth ();
    void setStrokeWidth (const std::string &arg, uint16_t gid);
    std::string lineCap ();
    void setLineCap (const std::string &arg);
    std::string lineJoin ();
    void setLineJoin (const std::string &arg);

    struct svg_state& operator = (const svg_state &oldstate);

    friend bool operator==(const svg_state &lhs, const svg_state &rhs);
    friend bool operator!=(const svg_state &lhs, const svg_state &rhs);
    friend svg_state operator + (const svg_state &lhs, const svg_state &rhs);
} SvgState;
Q_DECLARE_METATYPE (SvgState);

typedef struct conicpointlist {
    ConicPoint *first, *last;
    DBounds bbox; // temporary value
    bool ticked;

    void doTransform (const std::array<double, 6> &transform);
    void reverse ();
    void selectAll ();
    bool isSelected () const;
    void ensureStart ();
    int toPointCollection (int ptcnt, std::vector<BasePoint> &pts, char *flags);
    uint16_t lastPointIndex ();
    void findBounds (DBounds &b);
    Monotonic *toMContour (std::deque<struct monotonic> &mpool, Monotonic *start, OverlapType ot);

    void nearlyHvLines (extended_t err);
    void startToExtremum ();
    bool startToPoint (ConicPoint *nst);
    void removeStupidControlPoints ();
    bool smoothControlPoints (extended_t tan_bounds, bool vert_check);
} ConicPointList;

class FigureItem;

class Drawable {
public:
    bool selected = false;
    std::string type;
    SvgState svgState;
    std::array<double, 6> transform = {1, 0, 0, 1, 0, 0};

    virtual ElementType elementType () const = 0;
    virtual void quickBounds (DBounds &b) = 0;
    virtual void realBounds (DBounds &b, bool do_init=false) = 0;
};

class DrawableFigure : public Drawable {
    friend class ConicGlyph;
    friend class GlyphContext;

public:
    DrawableFigure ();
    DrawableFigure (const DrawableFigure &fig);

    void closepath (ConicPointList *cur, bool is_type2);
    uint16_t countPoints (const uint16_t first=0, bool ttf=false) const;
    uint16_t renumberPoints (const uint16_t first=0);
    ConicPointList *getPointContour (ConicPoint *sp);
    uint16_t toCoordList (std::vector<int16_t> &x_coords, std::vector<int16_t> &y_coords, std::vector<uint8_t> &flags, uint16_t gid);

    void svgClosePath (ConicPointList *cur, bool order2);
    void svgReadPointProps (const std::string &pp, int hintcnt);
    ElementType elementType () const override;

    void realBounds (DBounds &b, bool do_init=false);
    void quickBounds (DBounds &b) override;
    bool hasSelected () const;

    bool mergeWith (const DrawableFigure &fig);
    bool clearMarked ();
    void mergeMarked ();
    bool join (bool doall, double fudge);
    void deleteContour (ConicPointList *spls);
    ConicPoint *bisectSpline (Conic *spl, extended_t t);
    Monotonic *toMContours (std::deque<struct monotonic> &mpool, OverlapType ot);
    void toQuadratic (double fudge);
    void toCubic ();

    void clearHintMasks ();

    bool addExtrema (bool selected);
    bool roundToInt (bool selected);
    bool correctDirection ();
    bool simplify (bool selected, int upm);

    bool startToPoint (ConicPoint *nst);

    //DrawableFigure& operator=(const DrawableFigure &fig);

    std::map<std::string, double> props;
    std::vector<BasePoint> points;
    std::vector<ConicPointList> contours;
    bool order2 = false;
    FigureItem *item = nullptr;

private:
    void appendSplines (const DrawableFigure &fig);
    bool removeZeroLengthSplines (ConicPointList *spls, bool onlyselected, double bound);
    void splinesRemoveBetween (ConicPoint *from, ConicPoint *to);
    bool makeLoop (ConicPointList &spls, double fudge);

    void ssRemoveBacktracks (ConicPointList &ss);
    bool splinesRemoveBetweenMaybe (ConicPoint *from, ConicPoint *to, extended_t err);
    bool splinesRemoveMidMaybeIndeed (ConicPoint *mid, extended_t err, extended_t lenmax2);
    bool splinesRemoveMidMaybe (ConicPoint *mid, extended_t err, extended_t lenmax2);
    void forceLines (ConicPointList &spls, extended_t bump_size, int upm);
    void ssSimplify (ConicPointList &spls, int upm, double lenmax2);

    boost::object_pool<ConicPoint> points_pool;
    boost::object_pool<Conic> splines_pool;
};

class ConicGlyph;
class RefItem;

class DrawableReference : public Drawable {
public:
    bool use_my_metrics = false;
    bool round = false;
    bool point_match = false;
    uint8_t adobe_enc = 0;
    uint16_t GID = 0;
    uint16_t match_pt_base;
    uint16_t match_pt_ref;
    // for fonts with COLR table: may refer either to the glyphs defined in the
    // table itself, or to the main glyph container (glyf or CFF(2))
    OutlinesType outType = OutlinesType::NONE;
    // for GUI: that's where the source glyph name is going to be displayed
    BasePoint top;
    ConicGlyph *cc;
    RefItem *item;

    void setGlyph (ConicGlyph *g);
    ElementType elementType () const override;
    void quickBounds (DBounds &b);
    void realBounds (DBounds &b, bool do_init=false);
    uint16_t numContours () const;
    uint16_t numPoints () const;
    uint16_t depth (uint16_t val) const;
    bool useMyMetrics () const;
    void setFirstPointNumber (const uint16_t first=0);
    uint16_t firstPointNumber () const;

private:
    uint16_t m_first_pt_num = 0;
};

// Need this to reduce number of constructor arguments for ConicGlyph,
// so that it could be used with boost object_pool::construct ()
typedef struct base_glyph_metrics {
    uint16_t upm;
    uint16_t ascent, descent;
} BaseMetrics;

struct pschars;
class GlyphGraphicsView;
class AdvanceWidthItem;
class CffTable;
class ColrTable;
class CpalTable;
class MaxpTable;
class MoveCommand;

namespace SVGOptions {
    enum SVGOptions {
	dumpHeader = 1,
	onlySelected = 2,
	doExtras = 4,
	doAppSpecific = 8,
	asReference = 16
    };
};

class GlyphContainer;

class ConicGlyph {
    Q_DECLARE_TR_FUNCTIONS (ConicGlyph)
    friend class MoveCommand;
    friend class GlyphContext;
    friend class CffTable;
    friend class SvgTable;
    friend class ColrTable;
    friend class TinyFontProvider;

public:
    ConicGlyph (uint16_t gid, BaseMetrics gm);
    ~ConicGlyph ();

    void fromPS (BoostIn &buf, const struct cffcontext &ctx);
    void fromTTF (BoostIn &buf, uint32_t off);
    uint32_t toTTF (QBuffer &buf, QDataStream &os, MaxpTable *maxp);
    uint32_t toPS (QBuffer &buf, QDataStream &os, const struct cffcontext &ctx);
    void splitToPS (std::vector<std::pair<int, std::string>> &splitted, const struct cffcontext &ctx);

    // g_idx: if -1, then look for element with id 'glyph<GID>', as defined in the
    // spec for the SVG table. Otherwise look only for <g> elements whose 'id'
    // conforms the same form, but ignore the <GID> part and take the element
    // with the specified index (if available) instead. This is needed for pasting
    // serialized glyph data into glyph cells, where the GID of the source glyph
    // is just irrelevant;
    // target: figure "active" in the GUI (nullptr if none).
    // If this figure is of the "path" type, when any added paths are merged with
    // this figure instead of creating a new one. This is the normal situation
    // for TTF/CFF fonts, where there is just one figure for each glyph and there
    // is no need to produce more.
    bool fromSVG (pugi::xml_document &doc, int g_idx=-1, DrawableFigure *target=nullptr);
    bool fromSVG (std::istream &buf, int g_idx=-1, DrawableFigure *target=nullptr);
    std::string toSVG (struct rgba_color *palette=nullptr,
	uint8_t flags = SVGOptions::dumpHeader | SVGOptions::doExtras | SVGOptions::doAppSpecific);
    void clear ();

    std::vector<uint16_t> refersTo () const;
    void provideRefGlyphs (sFont *fnt, GlyphContainer *gc);
    int checkRefs (uint16_t gid, uint16_t gcnt);
    void finalizeRefs ();
    void renumberPoints ();
    // for reference processing
    uint16_t getTTFPoint (uint16_t pnum, uint16_t add, BasePoint *&pt);
    void unlinkRef (DrawableReference &ref);
    void unlinkRefs (bool selected);

    uint16_t gid ();
    uint16_t upm ();
    int leftSideBearing ();
    int advanceWidth ();
    QUndoStack *undoStack ();
    const PrivateDict *privateDict () const;
    OutlinesType outlinesType () const;

    void setHMetrics (int lsb, int aw);
    void setAdvanceWidth (int val);
    void findTop (BasePoint *top, std::array<double, 6> &transform);
    bool isEmpty ();
    bool isModified () const;
    void setModified (bool val);
    void setOutlinesType (OutlinesType val);

    uint16_t numCompositeContours () const;
    uint16_t numCompositePoints () const;
    uint16_t componentDepth (uint16_t val=0) const;
    uint16_t useMyMetricsGlyph () const;

    bool addExtrema (bool selected);
    bool roundToInt (bool selected);
    bool correctDirection (bool);
    bool simplify (bool selected);
    bool reverseSelected ();

    bool autoHint (sFont &fnt);
    bool hmUpdate (sFont &fnt);
    bool clearHints ();

    void removeFigure (DrawableFigure &fig);
    void swapFigures (int pos1, int pos2);
    void mergeContours ();

    DBounds bb;
    iBounds clipBox;
    std::vector<StemInfo> hstem;
    std::vector<StemInfo> vstem;
    std::list<DrawableFigure> figures;
    std::map<std::string, Gradient> gradients;
    std::vector<uint8_t> instructions;

private:
    static void svgDumpGradient (std::stringstream &ss, Gradient &grad, const std::string &grad_id);

    void checkBounds (DBounds &b, bool quick=true, const std::array<double, 6> &transform={1, 0, 0, 1, 0, 0}, bool dotransform=false);
    void svgDumpHeader (std::stringstream &ss, bool do_fsh_specific);
    void svgDumpGlyph (std::stringstream &ss, std::set<uint16_t> &processed, uint8_t flags);
    void svgAsRef (std::stringstream &ss, uint8_t flags);

    Conic* conicMake (ConicPoint *from, ConicPoint *to, bool order2);
    void ttfBuildContours (int path_cnt, uint16_t *endpt, uint8_t *flags, std::vector<BasePoint> &pts);
    void readttfsimpleglyph (BoostIn &buf, int path_cnt, uint32_t off);
    void readttfcompositeglyph (BoostIn &buf);
    void categorizePoints ();
    uint16_t appendHint (double start, double width, bool is_v);
    bool hasHintMasks ();

    void svgProcessNode (pugi::xml_document &doc, pugi::xml_node &root, std::array<double, 6> &transform, SvgState &state);
    void svgCheckArea (pugi::xml_node svg, std::array<double, 6> &matrix);
    void svgTraceArc (DrawableFigure &fig, ConicPointList *cur, BasePoint *current,
        std::map<std::string, double> props, int large_arc, int sweep);
    void svgParsePath (DrawableFigure &fig, const std::string &d);
    void svgParseEllipse (DrawableFigure &fig, bool inv=false);
    void svgParseRect (DrawableFigure &fig, bool inv=false);
    void svgParseLine (DrawableFigure &fig);
    void svgParsePoly (DrawableFigure &fig, bool is_gon);
    void figureAddGradient (pugi::xml_document &doc, Drawable &fig, std::array<double, 6> &transform, bool is_stroke);
    void svgDumpHints (std::stringstream &ss);

    // NB: each glyph object knows neither its name nor its unicode, but only GID,
    // which is supposed to be immutable in the context of our application.
    // That's because both glyph names and encoding can be changed by table editors
    // which don't deal with glyphs themselves
    uint16_t GID, units_per_em;
    int16_t m_ascent, m_descent;
    int32_t glyph_offset, glyph_len;
    int m_lsb, m_aw;
    int point_cnt;
    bool loaded: 1;
    bool widthset: 1;
    const PrivateDict *m_private = nullptr;
    BasePoint origPoint, awPoint;
    OutlinesType m_outType = OutlinesType::NONE;

    std::list<DrawableReference> refs;
    std::vector<ConicGlyph*> dependents;
    std::vector <HintMask> countermasks;
    std::unique_ptr<QUndoStack> m_undoStack;
};
