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

#include <iostream>
#include <iomanip>
#include <sstream>
#define _USE_MATH_DEFINES
#include <cmath>
#include <cctype>
#include <assert.h>
#include <set>

#include "splineglyph.h"
#include "tables.h"
#include "fs_notify.h"
#include "fs_math.h"

using namespace FontShepherd::math;

static double stringToDouble (std::string str) {
    double ret;
    std::istringstream ss;

    ss.str (str);
    ss.imbue (std::locale::classic ());
    if ((ss >> ret).fail ())
        return (0.0);
    return (ret);
}

static void svgFigureTransform (std::string &str_attr, std::array<double, 6> &trans) {
    std::array<double, 6> tmp = {1, 0, 0, 1, 0, 0};
    std::array<double, 6> res = {1, 0, 0, 1, 0, 0};
    double a, cx, cy;
    char trash;
    std::stringstream ss;
    char op[16], *opptr = op;

    tmp = trans;
    ss.str (str_attr);
    ss.imbue (std::locale::classic ());

    while (!ss.eof ()) {
        ss >> std::ws;
        ss.getline (opptr, 16, '(');
        if (strcmp (opptr, "matrix")==0) {
            for (uint8_t i=0; i<6; i++) {
                ss >> std::ws;
                ss >> res[i];
                ss >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws;
            }
        } else if (strcmp (opptr, "rotate")==0) {
	    ss >> std::ws >> a >> std::ws;
            a = a*M_PI/180;
	    res[0] = res[3] = cos (a);
	    res[1] = sin (a);
	    res[2] = -res[1];
            ss >> std::ws;
	    if (ss.peek () != ')' && ss.peek () != EOF) {
                if (ss.peek () == ',') ss.ignore (1);
		ss >> std::ws >> cx >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
		ss >> std::ws >> cy >> std::ws;
		res[4] = cx - res[0]*cx - res[2]*cy;
		res[5] = cy - res[1]*cx - res[3]*cy;
	    }
        } else if (strcmp (opptr, "scale")==0) {
            ss >> std::ws >> res[0] >> std::ws;
            if (ss.peek () != ')' && ss.peek () != EOF) {
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> res[3] >> std::ws;
            } else
                res[3] = res[0];
        } else if (strcmp (opptr, "translate")==0) {
            ss >> std::ws >> res[4] >> std::ws;
            if (ss.peek () != ')' && ss.peek () != EOF) {
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> res[5];
            }
        } else if (strcmp (opptr, "skewX")==0) {
            ss >> std::ws >> a >> std::ws;
	    res[2] = tan (a)*M_PI/180;
        } else if (strcmp (opptr, "skewY")==0) {
            ss >> std::ws >> a >> std::ws;
	    res[1] = tan (a)*M_PI/180;
        } else
            break;
        ss >> std::ws >> trash;
        if (trash != ')')
            break;
    }
    matMultiply (trans.data (), res.data (), trans.data ());
}

static bool xmlParseColor (std::string attr, uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) {
    uint16_t i;
    static struct { const char *name; uint32_t col; } stdcols[] = {
	{ "red", 0xff0000 },
	{ "green", 0x008000 },
	{ "blue", 0x0000ff },
	{ "crimson", 0xdc143c },
	{ "cyan", 0x00ffff },
	{ "magenta", 0xff00ff },
	{ "yellow", 0xffff00 },
	{ "black", 0x000000 },
	{ "darkblue", 0x00008b },
	{ "darkgray", 0x404040 },
	{ "darkgreen", 0x006400 },
	{ "darkgrey", 0x404040 },
	{ "gold", 0xffd700 },
	{ "gray", 0x808080 },
	{ "grey", 0x808080 },
	{ "lightgray", 0xc0c0c0 },
	{ "lightgrey", 0xc0c0c0 },
	{ "white", 0xffffff },
	{ "maroon", 0x800000 },
	{ "olive", 0x808000 },
	{ "navy", 0x000080 },
	{ "purple", 0x800080 },
	{ "lime", 0x00ff00 },
	{ "aqua", 0x00ffff },
	{ "teal", 0x008080 },
	{ "fuchsia", 0xff0080 },
	{ "silver", 0xc0c0c0 },
	{ nullptr, 0}};

    if (attr.compare ("none")==0 || attr.compare ("transparent")==0) {
        alpha = 0;
        return true;
    } else if (attr.compare ("currentColor")==0)
        return false;

    for (i=0; stdcols[i].name!=nullptr; ++i) {
        if (attr.compare (stdcols[i].name)==0)
            break;
    }
    if (stdcols[i].name!=nullptr) {
        red =   stdcols[i].col >> 16;
        green = (stdcols[i].col >> 8) & 0xFF;
        blue =  stdcols[i].col & 0xFF;
        return true;
    } else if (attr[0]=='#') {
        unsigned int temp=0;
        std::istringstream ss (attr);
        ss.ignore (1);
        if ((ss >> std::hex >> temp).fail ()) {
            FontShepherd::postError (QCoreApplication::tr (
                "Bad hex color spec: %1").arg (attr.c_str ()));
            return false;
        }
        if (attr.length ()==4) {
            red =   ((temp&0xf00)*0x11)>>8;
            green = ((temp&0x0f0)*0x11)>>4;
            blue =  ((temp&0x00f)*0x11);
        } else if (attr.length ()==7) {
            red =   temp >> 16;
            green = (temp >> 8) & 0xFF;
            blue =  temp & 0xFF;
        } else {
            FontShepherd::postError (QCoreApplication::tr (
                "Bad hex color spec: %1").arg (attr.c_str ()));
            return false;
        }
        return true;
    } else if (attr.compare (0, 3, "rgb")==0) {
        float r=0, g=0, b=0;
        char trash;
        std::istringstream ss (attr);
        ss.imbue (std::locale::classic ());
        ss.ignore (attr.length (), '(');
        if ((ss >> trash >> std::ws >> r >> trash >> std::ws >> g >> trash >> std::ws >> b).fail ()) {
            FontShepherd::postError (QCoreApplication::tr (
                "Bad rgb color spec: %1").arg (attr.c_str ()));
            return false;
        }

        if (attr.find ('.')) {
            red = (r>=1) ? 0xFF : (r<=0) ? 0 : rint (r*0xFF);
            green = (g>=1) ? 0xFF : (g<=0) ? 0 : rint (g*0xFF);
            blue = (b>=1) ? 0xFF : (b<=0) ? 0 : rint (b*0xFF);
        } else {
            red = (r>=0xFF) ? 0xFF : (r<=0) ? 0 : (unsigned char) r;
            green = (g>=0xFF) ? 0xFF : (g<=0) ? 0 : (unsigned char) g;
            blue = (b>=0xFF) ? 0xFF : (b<=0) ? 0 : (unsigned char) b;
        }
        return true;
    } else if (attr.compare (0, 3, "url")==0) {
        // can't treat this outside of a class
        return false;
    } else {
        FontShepherd::postError (QCoreApplication::tr (
            "Failed to parse color %1").arg (attr.c_str ()));
    }
    return false;
}

static double parseGCoord (std::string prop) {
    std::istringstream ss (prop);
    double val;

    ss >> val;
    if (ss.peek () == '%')
	val /= 100.0;
    return (val);
}

static std::string parseSourceUrl (const std::string attr) {
    char buf[64];
    char *bufptr = buf;

    std::istringstream ss (attr);
    ss.ignore (attr.length (), '(');
    ss >> std::ws;
    if (ss.peek () == '#')
        ss.ignore (1);
    else {
      	FontShepherd::postError (QCoreApplication::tr (
            "Incorrect color source URL specification %1.").arg (attr.c_str ()));
        return "";
    }
    ss.getline (bufptr, 64, ')');

    return std::string (bufptr);
}

static uint8_t parseVariableColor (const std::string &str_attr, SvgState state, bool is_stroke) {
    size_t first = str_attr.find_first_of ('(', 3);
    size_t last = str_attr.find_last_of (')');
    int ret = 0;
    if (first == std::string::npos || last == std::string::npos)
	return ret;
    std::string &source_id = is_stroke ? state.stroke_source_id : state.fill_source_id;
    uint16_t &idx = is_stroke ? state.stroke_idx : state.fill_idx;

    std::string sub = str_attr.substr (first, last-first);
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream ts (sub);
    while (std::getline (ts, token, ','))
	tokens.push_back (token);
    for (auto &token : tokens) {
	token.erase (0, token.find_first_not_of (" \n\r\t")-1);
	if (token.compare (0, 7, "--color")) {
	    idx = std::stoul (token.substr (7));
	    ret |= 1;
	} else if (token.compare (0, 3, "url")==0) {
	    source_id = parseSourceUrl (token);
	    ret |= 2;
	} else {
	    if (is_stroke)
		state.setStrokeColor (token);
	    else
		state.setFillColor (token);
	    ret |= 4;
	}
    }
    return ret;
}

static bool xmlParseColorSource (pugi::xml_document &doc, const std::string &id,
    const DBounds &bbox, struct rgba_color &default_color, std::array<double, 6> &transform,
    Gradient &grad, bool do_init) {

    bool bbox_units = true;
    std::stringstream ss;
    pugi::xpath_node_set match;
    pugi::xml_node n_grad;
    uint32_t i;

    ss << "(//linearGradient[@id='" << id << "']|//radialGradient[@id='" << id << "'])";

    match = doc.select_nodes (ss.str ().c_str ());
    if (match.size () > 0)
        n_grad = match[0].node ();
    else {
      	FontShepherd::postError (QCoreApplication::tr (
            "Could not find Color Source with id %1.").arg (id.c_str ()));
        return false;
    }

    if (!strcmp (n_grad.name (), "linearGradient"))
	grad.type = GradientType::LINEAR;
    else if (!strcmp (n_grad.name (), "radialGradient"))
	grad.type = GradientType::RADIAL;
    if (grad.type == GradientType::LINEAR || grad.type == GradientType::RADIAL) {
        pugi::xml_attribute units_attr = n_grad.attribute ("gradientUnits");
        pugi::xml_attribute transform_attr = n_grad.attribute ("gradientTransform");
        pugi::xml_attribute sm_attr = n_grad.attribute ("spreadMethod");
        pugi::xml_attribute href_attr = n_grad.attribute ("xlink:href");

	if (units_attr)
            bbox_units = strcmp (units_attr.value (), "userSpaceOnUse");
	if (!bbox_units)
	    grad.units = GradientUnits::userSpaceOnUse;
        // Don't know what to do with transforms in 'userSpaceOnUse' mode:
        // converting them to objectBoundingBox coordinate system seems not so trivial
        if (transform_attr) {
            std::string tr = transform_attr.value ();
            svgFigureTransform (tr, grad.transform);
	    //matMultiply (grad.transform.data (), transform.data (), grad.transform.data ());
        }

	if (do_init) grad.sm = GradientExtend::EXTEND_PAD;
	if (sm_attr) {
	    if (strcmp (sm_attr.value (), "reflect")==0 )
		grad.sm = GradientExtend::EXTEND_REFLECT;
	    else if (strcmp (sm_attr.value (), "repeat")==0 )
		grad.sm = GradientExtend::EXTEND_REPEAT;
	}

	if (grad.type == GradientType::LINEAR) {
            pugi::xml_attribute x1_attr = n_grad.attribute ("x1");
            pugi::xml_attribute x2_attr = n_grad.attribute ("x2");
            pugi::xml_attribute y1_attr = n_grad.attribute ("y1");
            pugi::xml_attribute y2_attr = n_grad.attribute ("y2");

	    if (x1_attr)
		grad.props["x1"] = parseGCoord (x1_attr.value ());
	    if (x2_attr)
		grad.props["x2"] = parseGCoord (x2_attr.value ());
	    if (y1_attr)
		grad.props["y1"] = parseGCoord (y1_attr.value ());
	    if (y2_attr)
		grad.props["y2"] = parseGCoord (y2_attr.value ());

	} else if (grad.type == GradientType::RADIAL) {
            pugi::xml_attribute cx_attr = n_grad.attribute ("cx");
            pugi::xml_attribute cy_attr = n_grad.attribute ("cy");
            pugi::xml_attribute fx_attr = n_grad.attribute ("fx");
            pugi::xml_attribute fy_attr = n_grad.attribute ("fy");
            pugi::xml_attribute r_attr = n_grad.attribute ("r");

	    if (cx_attr)
		grad.props["cx"] = parseGCoord (cx_attr.value ());
	    if (cy_attr)
		grad.props["cy"] = parseGCoord (cy_attr.value ());
	    if (r_attr)
		grad.props["r"] = parseGCoord (r_attr.value ());
	    if (fx_attr)
		grad.props["fx"] = parseGCoord (fx_attr.value ());
	    if (fy_attr)
		grad.props["fy"] = parseGCoord (fy_attr.value ());
	}
        // recursion to another gradient (where actual stops are possibly specified)
        if (href_attr && href_attr.value ()[0] == '#') {
	    Gradient temp;
            std::string href_id = href_attr.value ()+1;
            xmlParseColorSource (doc, href_id, bbox, default_color, transform, temp, false);
	    grad.stops = temp.stops;
        }
	if (!bbox_units)
	    grad.transformProps (transform);

        match = n_grad.select_nodes ("child::stop");
	if (match.size () > 0) {
	    grad.stops.resize (match.size ());
	    for (i=0; i<match.size (); i++) {
                pugi::xml_node n_stop = match[i].node ();
                pugi::xml_attribute off_attr = n_stop.attribute ("offset");
                pugi::xml_attribute sc_attr = n_stop.attribute ("stop-color");
                pugi::xml_attribute so_attr = n_stop.attribute ("stop-opacity");

		grad.stops[i].color = default_color;

		if (off_attr)
		    grad.stops[i].offset = parseGCoord (off_attr.value ());

		if (sc_attr) {
                    struct rgba_color &col = grad.stops[i].color;
		    xmlParseColor (sc_attr.value (), col.red, col.green, col.blue, col.alpha);
		}

		if (so_attr)
		    grad.stops[i].color.alpha = 255*stringToDouble (so_attr.value ());
		else
		    grad.stops[i].color.alpha = 255;
	    }
	}
        if (grad.stops.size () == 0) {
	    grad.stops.resize (1);
	    grad.stops[0].offset = 1;
	    grad.stops[0].color = default_color;
	}
	grad.bbox = bbox;
	if (!bbox_units)
	    grad.transformProps (std::array<double, 6> { 1, 0, 0, -1, 0, 0 });
    } else if (strcmp (n_grad.name (), "pattern")==0) {
      	FontShepherd::postError (QCoreApplication::tr (
            "I don't currently parse pattern Color Sources (%1).").arg (id.c_str ()));
        return false;
    } else {
      	FontShepherd::postError (QCoreApplication::tr (
            "Color Source with id %1 had an unexpected type %2."). arg (id.c_str ()).arg (n_grad.name ()));
        return false;
    }
    return true;
}

SvgState::svg_state () :
    fill_idx (0xFFFF), stroke_idx (0xFFFF),
    fill_set (false), stroke_set (false),
    stroke_width (1), linecap (lc_inherit), linejoin (lj_inherit), point_props_set (false) {
    new (&fill_source_id) std::string;
    new (&stroke_source_id) std::string;
};

SvgState::svg_state (const struct svg_state &oldstate) {
    fill = oldstate.fill;
    stroke = oldstate.stroke;
    fill_idx = oldstate.fill_idx;
    stroke_idx = oldstate.stroke_idx;
    fill_source_id = oldstate.fill_source_id;
    stroke_source_id = oldstate.stroke_source_id;
    fill_set = oldstate.fill_set;
    stroke_set = oldstate.stroke_set;
    stroke_width = oldstate.stroke_width;
    linecap = oldstate.linecap;
    linejoin = oldstate.linejoin;
    point_props_set = oldstate.point_props_set;
}

SvgState::~svg_state () {}

void SvgState::setFillColor (const std::string &attr) {
    fill_set = xmlParseColor (attr, fill.red, fill.green, fill.blue, fill.alpha);
}

void SvgState::setStrokeColor (const std::string &attr) {
    stroke_set = xmlParseColor (attr, stroke.red, stroke.green, stroke.blue, stroke.alpha);
}

std::string SvgState::fillColor () {
    std::stringstream ss;
    if (!fill_set)
        ss << "currentColor";
    else if (!fill_source_id.empty ())
        ss << "url(#" << fill_source_id << ")";
    else if (fill_idx != 0xFFFF) {
	ss << "var(--color" << fill_idx << ", #";
        ss << std::uppercase << std::setfill ('0') << std::hex;
        ss << std::setw (2) << (int) fill.red;
        ss << std::setw (2) << (int) fill.green;
        ss << std::setw (2) << (int) fill.blue;
	ss << ")";
    } else {
        ss << '#';
        ss << std::uppercase << std::setfill ('0') << std::hex;
        ss << std::setw (2) << (int) fill.red;
        ss << std::setw (2) << (int) fill.green;
        ss << std::setw (2) << (int) fill.blue;
    }
    return ss.str ();
}

std::string SvgState::strokeColor () {
    std::stringstream ss;
    if (!stroke_set)
        ss << "currentColor";
    else if (!stroke_source_id.empty ())
        ss << "url(#" << stroke_source_id << ")";
    else if (stroke_idx != 0xFFFF) {
	ss << "var(--color" << fill_idx << ", #";
        ss << std::uppercase << std::setfill ('0') << std::hex;
        ss << std::setw (2) << (int) fill.red;
        ss << std::setw (2) << (int) fill.green;
        ss << std::setw (2) << (int) fill.blue;
	ss << ")";
    } else {
        ss << '#';
        ss << std::uppercase << std::setfill ('0') << std::hex;
        ss << std::setw (2) << (int) stroke.red;
        ss << std::setw (2) << (int) stroke.green;
        ss << std::setw (2) << (int) stroke.blue;
    }
    return ss.str ();
}

float SvgState::fillOpacity () {
    if (!fill_set) return 1;
    return (fill.alpha/255);
}

void SvgState::setFillOpacity (float val) {
    fill.alpha = (val>=1) ? 0xFF : (val<=0) ? 0 : rint (val*0xFF);
    if (!fill_set) {
        fill.red = fill.green = fill.blue = 0;
    }
    fill_set = true;
}

float SvgState::strokeOpacity () {
    if (!stroke_set) return 1;
    return (stroke.alpha/255);
}

void SvgState::setStrokeOpacity (float val) {
    stroke.alpha = (val>=1) ? 0xFF : (val<=0) ? 0 : rint (val*0xFF);
    if (!stroke_set) {
        stroke.red = stroke.green = stroke.blue = 0;
    }
    stroke_set = true;
}

int SvgState::strokeWidth () {
    return (stroke_width);
}

void SvgState::setStrokeWidth (const std::string &arg, uint16_t gid) {
    std::istringstream ss (arg);
    ss.imbue (std::locale::classic ());
    if ((ss >> stroke_width).fail ()) {
        FontShepherd::postError (QCoreApplication::tr (
            "Bad stroke width value in glyph %1: %2").arg (gid).arg (arg.c_str ()));
    }
}

std::string SvgState::lineCap () {
    switch (linecap) {
      case lc_inherit:
        return "inherit";
      break;
      case lc_butt:
        return "butt";
      break;
      case lc_round:
        return "round";
      break;
      case lc_square:
        return "square";
      break;
    }
    return "inherit";
}

void SvgState::setLineCap (const std::string &arg) {
    if (arg.compare ("inherit")==0)
        linecap = lc_inherit;
    else if (arg.compare ("butt")==0)
        linecap = lc_butt;
    else if (arg.compare ("round")==0)
        linecap = lc_round;
    else if (arg.compare ("square")==0)
        linecap = lc_square;
    else {
        FontShepherd::postError (QCoreApplication::tr (
            "Unknown linecap value: %1").arg (arg.c_str ()));
        linecap = lc_inherit;
    }
}

std::string SvgState::lineJoin () {
    switch (linecap) {
      case lj_inherit:
        return "inherit";
      break;
      case lj_miter:
        return "miter";
      break;
      case lj_round:
        return "round";
      break;
      case lj_bevel:
        return "bevel";
      break;
    }
    return "inherit";
}

void SvgState::setLineJoin (const std::string &arg) {
    if (arg.compare ("inherit")==0)
        linejoin = lj_inherit;
    else if (arg.compare ("miter")==0)
        linejoin = lj_miter;
    else if (arg.compare ("round")==0)
        linejoin = lj_round;
    else if (arg.compare ("bevel")==0)
        linejoin = lj_bevel;
    else {
        FontShepherd::postError (QCoreApplication::tr (
            "Unknown linejoin value: %1").arg (arg.c_str ()));
        linejoin = lj_inherit;
    }
}

bool operator==(const SvgState &lhs, const SvgState &rhs) {
    return (
        (!(lhs.fill_set & rhs.fill_set) || ((lhs.fill_set & rhs.fill_set) &&
            lhs.fill == rhs.fill && lhs.fill_idx == rhs.fill_idx &&
	    lhs.fill_source_id == rhs.fill_source_id)) &&
        (!(lhs.stroke_set & rhs.stroke_set) || ((lhs.stroke_set & rhs.stroke_set) &&
            lhs.stroke == rhs.stroke && lhs.stroke_idx == rhs.stroke_idx &&
	    lhs.stroke_source_id == rhs.stroke_source_id)) &&
        lhs.stroke_width == rhs.stroke_width &&
        lhs.linecap == rhs.linecap && lhs.linejoin == rhs.linejoin);
}

bool operator!=(const SvgState &lhs, const SvgState &rhs) {
    return !(lhs==rhs);
}

SvgState operator + (const SvgState &lhs, const SvgState &rhs) {
    SvgState ret (lhs);
    if (!lhs.fill_set && rhs.fill_set) {
	ret.fill = rhs.fill;
	ret.fill_source_id = rhs.fill_source_id;
	ret.fill_set = true;
	ret.fill_idx = rhs.fill_idx;
    }
    if (!lhs.stroke_set && rhs.stroke_set) {
	ret.stroke = rhs.stroke;
	ret.stroke_width = rhs.stroke_width;
	ret.stroke_source_id = rhs.stroke_source_id;
	ret.stroke_set = true;
	ret.stroke_idx = rhs.stroke_idx;
    }
    if (lhs.linecap == lc_inherit)
	ret.linecap = rhs.linecap;
    if (lhs.linejoin == lj_inherit)
	ret.linejoin = rhs.linejoin;
    return ret;
}

SvgState& SvgState::operator = (const struct svg_state &oldstate) {
    fill = oldstate.fill;
    stroke = oldstate.stroke;
    fill_source_id = oldstate.fill_source_id;
    stroke_source_id = oldstate.stroke_source_id;
    fill_set = oldstate.fill_set;
    stroke_set = oldstate.stroke_set;
    stroke_width = oldstate.stroke_width;
    linecap = oldstate.linecap;
    linejoin = oldstate.linejoin;
    point_props_set = oldstate.point_props_set;

    return *this;
}

static void svgDumpColorProps (std::stringstream &ss, SvgState &state) {
    std::string fill = state.fillColor ();
    std::string stroke = state.strokeColor ();
    float fill_op = state.fillOpacity ();
    float stroke_op = state.strokeOpacity ();
    int sw = state.strokeWidth ();
    std::string lc = state.lineCap ();
    std::string lj = state.lineJoin ();

    if (fill.compare ("currentColor"))
        ss << " fill=\"" << fill << "\"";
    if (fill_op != 1)
        ss << " fill-opacity=\"" << fill_op << "\"";
    if (stroke.compare ("currentColor"))
        ss << " stroke=\"" << stroke << "\"";
    if (fill_op != 1)
        ss << " stroke-opacity=\"" << stroke_op << "\"";
    if (sw != 1)
        ss << " stroke-width=\"" << sw << "\"";
    if (lc.compare ("inherit"))
        ss << " stroke-linecap=\"" << lc << "\"";
    if (lj.compare ("inherit"))
        ss << " stroke-linejoin=\"" << lj << "\"";
}

static void svgDumpMatrix (std::stringstream &ss, std::array<double, 6> &matrix, std::string &attr_name) {
    uint8_t i;
    if (matrix == std::array<double, 6> { 1, 0, 0, 1, 0, 0 })
        return;

    ss << " " << attr_name << "=\"matrix(";
    for (i=0; i<5; i++)
        ss << matrix[i] << ' ';
    ss << matrix[5] << ")\"";
}

static void svgTransformEllipse (DrawableFigure &el, std::array<double, 6> &trans) {
    std::map<std::string, double>::iterator it;
    ElementType ftype = el.elementType ();

    if (ftype != ElementType::Circle && ftype != ElementType::Ellipse)
        return;

    if (trans[1] != 0.0 || trans[2] != 0.0) {
        matMultiply (el.transform.data (), trans.data (), el.transform.data ());
        return;
    }

    for (it = el.props.begin (); it != el.props.end (); ++it) {
        std::string key = it->first;
        double val = it->second;

        if (key.compare ("cx") == 0)
            el.props[key] = trans[0]*val + trans[4];
        else if (key.compare ("rx") == 0)
            el.props[key] = trans[0]*val;
        else if (key.compare ("cy") == 0)
            el.props[key] = trans[3]*val + trans[5];
        else if (key.compare ("ry") == 0)
            el.props[key] = trans[3]*val;
        else if (key.compare ("r") == 0) {
            if (realNear (std::abs (trans[0]), std::abs (trans[3])))
                el.props[key] = trans[0]*val;
            else {
                el.type = "ellipse";
                el.props["rx"] = trans[0]*val;
                el.props["ry"] = trans[3]*val;
                el.props.erase ("r");
            }
        }
    }
}

static void svgTransformRect (DrawableFigure &el, std::array<double, 6> &trans) {
    std::map<std::string, double>::iterator it;
    ElementType ftype = el.elementType ();

    if (ftype != ElementType::Rect)
        return;

    if (trans[1] != 0.0 || trans[2] != 0.0) {
        matMultiply (trans.data (), el.transform.data (), el.transform.data ());
        return;
    }

    for (it = el.props.begin (); it != el.props.end (); ++it) {
        std::string key = it->first;
        double val = it->second;

        if (key.compare ("x") == 0)
            el.props[key] = trans[0]*val + trans[4];
        else if (key.compare ("y") == 0)
            el.props[key] = trans[3]*val + trans[5];
        else if (key.compare ("width") == 0 || key.compare ("rx") == 0)
            el.props[key] = trans[0]*val;
        else if (key.compare ("height") == 0 || key.compare ("ry") == 0)
            el.props[key] = trans[3]*val;
    }
    if (el.props["height"] < 0) {
	el.props["height"] = std::abs (el.props["height"]);
	el.props["y"] -= el.props["height"];
    }
}

static void svgTransformLine (DrawableFigure &el, std::array<double, 6> &trans) {
    std::map<std::string, double>::iterator it;
    double x, y;

    if (el.type.compare ("line") != 0)
        return;

    if (el.props.count ("x1") && el.props.count ("y1")) {
        x = el.props["x1"]; y = el.props["y1"];
        el.props["x1"] = trans[0]*x + trans[2]*y + trans[4];
        el.props["y1"] = trans[1]*x + trans[3]*y + trans[5];
    }
    if (el.props.count ("x2") && el.props.count ("y2")) {
        x = el.props["x2"]; y = el.props["y2"];
        el.props["x2"] = trans[0]*x + trans[2]*y + trans[4];
        el.props["y2"] = trans[1]*x + trans[3]*y + trans[5];
    }
}

static void svgTransformPoly (DrawableFigure &el, std::array<double, 6> &trans) {
    std::map<std::string, double>::iterator it;
    double x, y;
    uint16_t i;

    if (el.type.compare ("polygon") != 0 && el.type.compare ("polyline") != 0)
        return;

    for (i=0; i<el.points.size (); i++) {
        BasePoint &bp = el.points[i];
        x = bp.x; y = bp.y;
        bp.x = trans[0]*x + trans[2]*y + trans[4];
        bp.y = trans[1]*x + trans[3]*y + trans[5];
    }
}

void ConicGlyph::svgDumpGradient (std::stringstream &ss, Gradient &grad, const std::string &grad_id) {
    uint16_t i;
    std::map<std::string, double>::iterator it;
    std::map<std::string, double> saveprops = grad.props;
    if (grad.units == GradientUnits::userSpaceOnUse)
	grad.transformProps (std::array<double, 6> { 1, 0, 0, -1, 0, 0 });

    ss << "   " << ((grad.type == GradientType::LINEAR) ? "<linearGradient" : "<radialGradient");
    ss << " id=\"" << grad_id << "\"";
    for (it = grad.props.begin (); it != grad.props.end (); ++it)
        ss << " " << it->first << "=\"" << it->second << "\"";
    if (grad.sm != GradientExtend::EXTEND_PAD)
        ss << " spreadMethod=\"" << (grad.sm == GradientExtend::EXTEND_REFLECT ? "reflect" : "repeat") << "\"";
    std::string trans_attr = "gradientTransform";
    svgDumpMatrix (ss, grad.transform, trans_attr);
    if (grad.units == GradientUnits::userSpaceOnUse)
	ss << " gradientUnits=\"userSpaceOnUse\"";
    ss << ">\n";
    for (i=0; i<grad.stops.size (); i++) {
        struct gradient_stop &stop = grad.stops[i];
        std::stringstream hexbuf;
        ss << "    <stop offset=\"" << stop.offset << "\"";
        ss << " stop-color=\"#";
        hexbuf << std::uppercase << std::setfill ('0') << std::hex;
        hexbuf << std::setw (2) << (int) stop.color.red;
        hexbuf << std::setw (2) << (int) stop.color.green;
        hexbuf << std::setw (2) << (int) stop.color.blue;
        ss << hexbuf.str () << "\"";
        if (stop.color.alpha < 255)
            ss << " stop-opacity=\"" << (static_cast<double>(stop.color.alpha)/255) << "\"";
        ss << "/>\n";
    }
    ss << "   " << ((grad.type == GradientType::LINEAR) ? "</linearGradient>\n" : "</radialGradient>\n");
    grad.props = saveprops;
}

static std::string svgDumpPointProps (ConicPoint *sp, int hintcnt) {
    uint8_t i;
    std::stringstream ss;
    switch (sp->pointtype) {
      case pt_curve:
        ss << 'c';
      break;
      case pt_tangent:
        ss << 't';
      break;
      case pt_corner:
        ss << 'a';
    }
    ss << "{";
    ss << sp->ttfindex << ",";
    ss << (sp->nonextcp ? -1 : sp->nextcpindex);
    if (sp->hintmask) {
        std::stringstream temps;
        temps << std::uppercase << std::setfill ('0') << std::hex;
        for (i=0; i<(hintcnt+7)/8; i++)
            temps << std::setw (2) << (0xff&sp->hintmask->byte[i]);
        ss << ",hm:" << temps.str ();
    }
    ss << "}";
    return ss.str ();
}

void ConicGlyph::svgDumpHints (std::stringstream &ss) {
    switch (m_outType) {
      case OutlinesType::PS:
	if (!hstem.empty () || !vstem.empty () || !countermasks.empty ()) {
	    ss << "    <fsh:ps-hints ";
	    if (!hstem.empty ()) {
		ss << "fsh:hstem=\"";
		for (size_t i=0; i<hstem.size (); i++) {
		    auto &stem = hstem[i];
		    ss << stem.start << ' ' << stem.width;
		    if (i < hstem.size ()-1)
			ss << ' ';
		}
		ss << "\" ";
	    }
	    if (!vstem.empty ()) {
		ss << "fsh:vstem=\"";
		for (size_t i=0; i<vstem.size (); i++) {
		    auto &stem = vstem[i];
		    ss << stem.start << ' ' << stem.width;
		    if (i < vstem.size ()-1)
			ss << ' ';
		}
		ss << "\" ";
	    }
	    if (!countermasks.empty ()) {
		int hintcnt = hstem.size () + vstem.size ();
		std::ios init (nullptr);
		init.copyfmt (ss);
		ss << std::hex << std::setfill ('0');
		ss << "fsh:countermasks=\"";
		for (size_t i=0; i<countermasks.size (); i++) {
		    for (int j=0; j<(hintcnt+7)/8; j++)
			ss << std::setw (2) << (0xff&countermasks[i].byte[j]);
		    if (i < countermasks.size ()-1)
			ss << ' ';
		}
		ss.copyfmt (init);
		ss << "\" ";
	    }
	    ss << "/>\n";
	}
      case OutlinesType::TT:
      default:
	;
    }
}

void ConicGlyph::svgDumpGlyph (std::stringstream &ss, std::set<uint16_t> &processed, uint8_t flags) {
    BasePoint last;
    ConicPointList *spls;
    Conic *spl, *first;
    std::string glyph_tag = (flags & SVGOptions::asReference) ? "symbol" : "g";
    std::string id_base = (m_outType == OutlinesType::COLR) ? "colr-glyph" : "glyph";
    int hintcnt = hstem.size () + vstem.size ();
    std::string trans_attr = "transform";
    bool selected = flags & SVGOptions::onlySelected;

    bool need_defs = (flags & SVGOptions::doExtras) && (!gradients.empty () || !refs.empty ());
    if (need_defs) {
	if (gradients.size () > 0) {
	    ss << "  <defs>\n";
	    for (auto it = gradients.begin (); it != gradients.end (); ++it) {
		std::string grad_id = it->first;
		Gradient &grad = it->second;
		svgDumpGradient (ss, grad, grad_id);
	    }
	    ss << "  </defs>\n";
	}

	for (auto &ref : refs) {
	    if (selected && !ref.selected)
		continue;
	    if (ref.cc && !processed.count (ref.cc->gid ())) {
		ref.cc->svgDumpGlyph (ss, processed, flags | SVGOptions::asReference);
		processed.insert (ref.cc->gid ());
	    }
	}
    }
    /* GWW: as I see it there is nothing to be gained by optimizing out the */
    /* command characters, since they just have to be replaced by spaces */
    /* so I don't bother to */
    last.x = last.y = 0;

    // ss << "  <" << glyph_tag << " id=\"glyph" << GID << "\" transform=\"translate(0 " << bb.maxy << ")\">\n";
    ss << "  <" << glyph_tag << " id=\"" << id_base << GID << "\" >\n";
    if (flags & SVGOptions::doAppSpecific)
	ss << "    <fsh:horizontal-metrics fsh:left-sidebearing=\"" << m_lsb << "\" fsh:advance-width=\"" << m_aw << "\" />\n";
    svgDumpHints (ss);
    for (auto &fig : figures) {
        std::vector<ConicPointList> &conics = fig.contours;
	std::vector<std::string> props_lst;
	props_lst.reserve (fig.countPoints ());
	ElementType ftype = fig.elementType ();
	if (selected && !fig.hasSelected ())
	    continue;

	switch (ftype) {
	  case ElementType::Circle:
	  case ElementType::Ellipse: {
	    double rx = std::abs (fig.props["rx"]);
	    double ry = std::abs (fig.props["ry"]);
	    if (FontShepherd::math::realNear (rx, ry)) {
		ss << "    <circle";
		svgDumpColorProps (ss, fig.svgState);
		svgDumpMatrix (ss, fig.transform, trans_attr);
		ss << " cx=\"" << fig.props["cx"] << "\" cy=\"" << -fig.props["cy"]
		    << "\" r=\"" << rx << "\" />\n";
	    } else {
		ss << "    <ellipse";
		svgDumpColorProps (ss, fig.svgState);
		svgDumpMatrix (ss, fig.transform, trans_attr);
		ss << " cx=\"" << fig.props["cx"] << "\" cy=\"" << -fig.props["cy"]
		    << "\" rx=\"" << rx << "\" ry=\"" << ry << "\" />\n";
	    }
	  } break;
	  case ElementType::Rect: {
	    ss << "    <rect";
	    svgDumpColorProps (ss, fig.svgState);
	    svgDumpMatrix (ss, fig.transform, trans_attr);
	    ss << " x=\"" << fig.props["x"] << "\" y=\"" << -fig.props["y"]-fig.props["height"] << "\"";
	    ss << " width=\"" << fig.props["width"] << "\" height=\"" << fig.props["height"] << "\"";
	    if (fig.props.count ("rx"))
		ss << " rx=\"" << fig.props["rx"] << "\"";
	    if (fig.props.count ("ry"))
		ss << " ry=\"" << fig.props["ry"] << "\"";
	    ss << " />\n";
	  } break;
	  case ElementType::Line: {
	    auto &spls = fig.contours.front ();
	    ss << "    <line";
	    svgDumpColorProps (ss, fig.svgState);
	    svgDumpMatrix (ss, fig.transform, trans_attr);
	    ss << " x1=\"" << spls.first->me.x << "\" y1=\"" << -spls.first->me.y << "\"";
	    ss << " x2=\"" << spls.last->me.x << "\" y2=\"" << -spls.last->me.y << "\"";
	    ss << " />\n";
	  } break;
	  case ElementType::Polygon:
	  case ElementType::Polyline: {
	    auto &spls = fig.contours.front ();
	    ConicPoint *sp = spls.first;
	    if (ftype == ElementType::Polygon)
		ss << "    <polygon";
	    else
		ss << "    <polyline";
	    svgDumpColorProps (ss, fig.svgState);
	    svgDumpMatrix (ss, fig.transform, trans_attr);
	    ss << " points=\"";
	    do {
		ss << sp->me.x << ',' << -sp->me.y << ' ';
		sp = sp->next ? sp->next->to : nullptr;
	    } while (sp && sp!=spls.first);
	    ss << "\" />\n";
	  } break;
	  case ElementType::Path: if (conics.size () > 0) {
	    bool doall = !selected || fig.selected;
            ss << "    <path";
            svgDumpColorProps (ss, fig.svgState);
            ss << " d=\"";
            for (size_t j=0; j<conics.size (); j++) {
                bool open = false;
		ConicPoint *startpt = nullptr, *curpt, *headpt;
                spls = &conics[j];

		// Make sure we are at the start of the selected part
		if (!doall && spls->first->selected) {
		    for (
			curpt = spls->first;
			curpt->prev && curpt->prev->from && curpt->prev->from->selected && curpt != startpt;
			curpt = curpt->prev->from) {
			if (!startpt) startpt = curpt;
		    }
		    headpt = curpt;
		} else
		    headpt = spls->first;

                // Take care of single point contours
		if (!headpt->next && (doall || headpt->selected)) {
		    props_lst.push_back (svgDumpPointProps (headpt, hintcnt));
		    ss << "M " << headpt->me.x << ' ' << -headpt->me.y << ' ';
		} else {
		    last = headpt->me;
		    first = nullptr;
		    for (spl = headpt->next; spl && spl!=first; spl = spl->to->next) {
			if (!first) first=spl;
			if (doall || spl->from->selected)
			    props_lst.push_back (svgDumpPointProps (spl->from, hintcnt));
			if (!open) {
			    if (doall || spl->from->selected) {
				ss << "M " << spl->from->me.x << ' ' << -spl->from->me.y << ' ';
				open = true;
			    }
			}
			if (!doall && !spl->to->selected)
			    open = false;

			if (open) {
			    if (spl->from->nonextcp && spl->to->noprevcp) {
				if (spl->to->me.x==spl->from->me.x)
				    ss << "v " << -(spl->to->me.y-last.y) << ' ';
				else if (spl->to->me.y==spl->from->me.y)
				    ss << "h " << (spl->to->me.x-last.x) << ' ';
				else if (spl->to->next==first) {
				    ss << "z ";
				    open = false;
				} else
				    ss << "l " << (spl->to->me.x-last.x) << ' ' << -(spl->to->me.y-last.y) << ' ';
			    } else if (spl->order2) {
				if (!spl->from->noprevcp && spl->from!=spls->first &&
					realNear (spl->from->me.x-spl->from->prevcp.x, spl->from->nextcp.x-spl->from->me.x) &&
					realNear (spl->from->me.y-spl->from->prevcp.y, spl->from->nextcp.y-spl->from->me.y))
				    ss << "t " << (spl->to->me.x-last.x) << ' ' << -(spl->to->me.y-last.y) << ' ';
				else
				    ss << "q " << (spl->to->prevcp.x-last.x) << ' ' << -(spl->to->prevcp.y-last.y) << ' '
					    << (spl->to->me.x-last.x) << ' ' << -(spl->to->me.y-last.y) << ' ';
			    } else {
				if (!spl->from->noprevcp && spl->from!=headpt && spl->from->prev &&
					(doall || spl->from->prev->from->selected) &&
					realNear (spl->from->me.x-spl->from->prevcp.x, spl->from->nextcp.x-spl->from->me.x) &&
					realNear (spl->from->me.y-spl->from->prevcp.y, spl->from->nextcp.y-spl->from->me.y))
				    ss << "s " << (spl->to->prevcp.x-last.x) << ' ' << -(spl->to->prevcp.y-last.y) << ' '
					    << (spl->to->me.x-last.x) << ' ' << -(spl->to->me.y-last.y) << ' ';
				else
				    ss << "c " << (spl->from->nextcp.x-last.x) << ' ' << -(spl->from->nextcp.y-last.y) << ' '
					    << (spl->to->prevcp.x-last.x) << ' ' << -(spl->to->prevcp.y-last.y) << ' '
					    << (spl->to->me.x-last.x) << ' ' << -(spl->to->me.y-last.y) << ' ';
			    }
			}
			last = spl->to->me;
		    }
		    if (open && spls->first->prev && (doall || headpt->selected))
			ss << "z ";
		}
            }
            ss << "\" ";

	    if (flags & SVGOptions::doAppSpecific) {
		ss << "fsh:point-properties=\"";
		for (auto str : props_lst)
		    ss << str;
		ss << "\" ";
	    }
	    ss << "/>\n";
	  } break;
	  case ElementType::Reference:
	    ;
        }
    }

    for (auto &ref : refs) {
	float ref_y_shift = 0;
	std::string ref_id_base = (ref.outType == OutlinesType::COLR) ? "colr-glyph" : "glyph";
	// compensate for the difference in the reference and base glyph translate transformations
	//if (ref->cc)
	//    ref_y_shift = -ref->cc->bb.maxy;
        if (selected && !ref.selected)
            continue;

	ss << "    <use xlink:href=\"#" << ref_id_base << ref.GID << "\"";
	if (ref.svgState.fill_set || ref.svgState.stroke_set)
	    svgDumpColorProps (ss, ref.svgState);
	ss << " transform=\"matrix(";
	for (size_t j=0; j<4; j++)
	    ss << ref.transform[j] << ' ';
	ss << (ref.transform[4]) << ' ' << (ref_y_shift - ref.transform[5]) << ")\"/>\n";
    }
    ss << "  </" << glyph_tag << ">\n";
}

void ConicGlyph::svgAsRef (std::stringstream &ss, uint8_t flags) {
    std::set<uint16_t> processed_refs;
    svgDumpGlyph (ss, processed_refs, flags | SVGOptions::asReference);
    ss << "  <g id=\"glyph" << GID << "\" >\n";
    ss << "    <fsh:horizontal-metrics fsh:left-sidebearing=\"" << m_lsb << "\" fsh:advance-width=\"" << m_aw << "\" />\n";
    ss << "    <use xlink:href=\"#glyph" << GID << "\" />\n";
    ss << "  </g>\n";
}

void ConicGlyph::svgDumpHeader (std::stringstream &ss, bool do_fsh_specific) {
    // set canvas width to glyph advance width, unless it has a negative left bearing
    int svg_w = bb.minx < 0 ? bb.maxx > 0 ? bb.maxx - bb.minx : 0 - bb.minx : m_aw;
    int svg_h = units_per_em;
    int svg_startx = bb.minx < 0 ? bb.minx : 0;
    int svg_starty = 0;

    ss << "<?xml version=\"1.0\" standalone=\"no\"?>\n";
    ss << "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\" >\n";
    ss << "<svg xmlns=\"http://www.w3.org/2000/svg\"";
    if (!do_fsh_specific)
        ss << " xmlns:fsh=\"http://www.fontsheferd.github.io/svg\"";
    ss << " width=\"" <<  svg_w << "\" height=\"" << svg_h
        << "\" viewBox=\"" << svg_startx << ' ' << svg_starty << ' ' << svg_w << " " << svg_h << "\">\n";
}

std::string ConicGlyph::toSVG (struct rgba_color *palette, uint8_t flags) {
    std::stringstream ss;
    std::set<uint16_t> processed_refs;

    checkBounds (bb, false);
    if (palette) {
	for (auto &fig : figures) {
            fig.svgState.fill = *palette;
            fig.svgState.fill_set = true;
        }
    }

    ss.imbue (std::locale::classic ());
    ss << std::fixed << std::setprecision (2);
    if (flags & SVGOptions::dumpHeader)
	svgDumpHeader (ss, flags & SVGOptions::doAppSpecific);
    if (flags & SVGOptions::asReference)
        svgAsRef (ss, flags);
    else
        svgDumpGlyph (ss, processed_refs, flags);
    if (flags & SVGOptions::dumpHeader)
        ss << "</svg>\n";
    return ss.str ();
}

void ConicGlyph::svgTraceArc (DrawableFigure &fig, ConicPointList *cur, BasePoint *current,
    std::map<std::string, double> props, int large_arc, int sweep) {

    double cosr, sinr;
    double x1p, y1p;
    double lambda, factor;
    double cxp, cyp, cx, cy;
    double tmpx, tmpy, t2x, t2y;
    double startangle, delta, a;
    ConicPoint *fin, *sp;
    BasePoint arcp[4], prevcp[4], nextcp[4], firstcp[2];
    int i, j, ia, firstia;
    static double sines[] = { 0, 1, 0, -1, 0, 1, 0, -1, 0, 1, 0, -1 };
    static double cosines[]={ 1, 0, -1, 0, 1, 0, -1, 0, 1, 0, -1, 0 };
    double rx = props["rx"], ry = props["ry"];

    fin = fig.points_pool.construct (props["x"], props["y"]);
    if (rx < 0) rx = -rx;
    if (ry < 0) ry = -ry;
    if (rx!=0 && ry!=0) {
	/* Page 647 in the SVG 1.1 spec describes how to do this */
	/* This is Appendix F (Implementation notes) section 6.5 */
	cosr = cos (props["axisrot"]); sinr = sin (props["axisrot"]);
	x1p = cosr*(current->x-props["x"])/2 + sinr*(current->y-props["y"])/2;
	y1p =-sinr*(current->x-props["x"])/2 + cosr*(current->y-props["y"])/2;
	/* Correct for bad radii */
	lambda = x1p*x1p/(rx*rx) + y1p*y1p/(ry*ry);
	if (lambda>1) {
	   lambda = sqrt(lambda);
	   rx *= lambda;
	   ry *= lambda;
	}
	factor = rx*rx*ry*ry - rx*rx*y1p*y1p - ry*ry*x1p*x1p;
	if (realNear (factor, 0))
	    factor = 0;		/* Avoid rounding errors that lead to small negative values */
	else
	    factor = sqrt (factor/(rx*rx*y1p*y1p+ry*ry*x1p*x1p));
	if (large_arc==sweep)
	    factor = -factor;
	cxp = factor*(rx*y1p)/ry;
	cyp =-factor*(ry*x1p)/rx;
	cx = cosr*cxp - sinr*cyp + (current->x+props["x"])/2;
	cy = sinr*cxp + cosr*cyp + (current->y+props["y"])/2;

	tmpx = (x1p-cxp)/rx; tmpy = (y1p-cyp)/ry;
	startangle = acos(tmpx/sqrt(tmpx*tmpx+tmpy*tmpy));
	if ( tmpy<0 )
	    startangle = -startangle;
	t2x = (-x1p-cxp)/rx; t2y = (-y1p-cyp)/ry;
	delta = (tmpx*t2x+tmpy*t2y)/
		  sqrt((tmpx*tmpx+tmpy*tmpy)*(t2x*t2x+t2y*t2y));
	/* We occasionally got rounding errors near -1 */
	if (delta<=-1)
	    delta = M_PI;
	else if ( delta>=1 )
	    delta = 0;
	else
	    delta = acos(delta);
	if (tmpx*t2y-tmpy*t2x<0)
	    delta = -delta;
	if (sweep==0 && delta>0)
	    delta -= 2*M_PI;
	if (sweep && delta<0)
	    delta += 2*M_PI;

	if (delta>0) {
	    i = 0;
	    ia = firstia = floor(startangle/(M_PI/2))+1;
	    for (a=ia*(M_PI/2), ia+=4; a<startangle+delta && !realNear (a,startangle+delta); a += M_PI/2, ++i, ++ia) {
		t2x = rx*cosines[ia]; t2y = ry*sines[ia];
		arcp[i].x = cosr*t2x - sinr*t2y + cx;
		arcp[i].y = sinr*t2x + cosr*t2y + cy;
		if ( t2x==0 ) {
		    t2x = rx*cosines[ia+1]; t2y = 0;
		} else {
		    t2x = 0; t2y = ry*sines[ia+1];
		}
		prevcp[i].x = arcp[i].x - .552*(cosr*t2x - sinr*t2y);
		prevcp[i].y = arcp[i].y - .552*(sinr*t2x + cosr*t2y);
		nextcp[i].x = arcp[i].x + .552*(cosr*t2x - sinr*t2y);
		nextcp[i].y = arcp[i].y + .552*(sinr*t2x + cosr*t2y);
	    }
	} else {
	    i = 0;
	    ia = firstia = ceil(startangle/(M_PI/2))-1;
	    for ( a=ia*(M_PI/2), ia += 8; a>startangle+delta && !realNear (a,startangle+delta); a -= M_PI/2, ++i, --ia) {
		t2x = rx*cosines[ia]; t2y = ry*sines[ia];
		arcp[i].x = cosr*t2x - sinr*t2y + cx;
		arcp[i].y = sinr*t2x + cosr*t2y + cy;
		if ( t2x==0 ) {
		    t2x = rx*cosines[ia+1]; t2y = 0;
		} else {
		    t2x = 0; t2y = ry*sines[ia+1];
		}
		prevcp[i].x = arcp[i].x + .552*(cosr*t2x - sinr*t2y);
		prevcp[i].y = arcp[i].y + .552*(sinr*t2x + cosr*t2y);
		nextcp[i].x = arcp[i].x - .552*(cosr*t2x - sinr*t2y);
		nextcp[i].y = arcp[i].y - .552*(sinr*t2x + cosr*t2y);
	    }
	}
	if ( i!=0 ) {
	    double firsta=firstia*M_PI/2;
	    double d = (firsta-startangle)/2;
	    double th = startangle+d;
	    double hypot = 1/cos(d);
	    BasePoint temp;
	    t2x = rx*cos(th)*hypot; t2y = ry*sin(th)*hypot;
	    temp.x = cosr*t2x - sinr*t2y + cx;
	    temp.y = sinr*t2x + cosr*t2y + cy;
	    firstcp[0].x = cur->last->me.x + .552*(temp.x-cur->last->me.x);
	    firstcp[0].y = cur->last->me.y + .552*(temp.y-cur->last->me.y);
	    firstcp[1].x = arcp[0].x + .552*(temp.x-arcp[0].x);
	    firstcp[1].y = arcp[0].y + .552*(temp.y-arcp[0].y);
	}
	for ( j=0; j<i; ++j ) {
	    sp = fig.points_pool.construct ();
            sp->me.x = arcp[j].x; sp->me.y = arcp[j].y;
	    if ( j!=0 ) {
		sp->prevcp = prevcp[j];
		cur->last->nextcp = nextcp[j-1];
	    } else {
		sp->prevcp = firstcp[1];
		cur->last->nextcp = firstcp[0];
	    }
	    sp->noprevcp = cur->last->nonextcp = false;
	    fig.splines_pool.construct (cur->last, sp, false);
	    cur->last = sp;
	}
	double hypot, c, s;
	BasePoint temp;
	if (i==0) {
	    double th = startangle+delta/2;
	    hypot = 1.0/cos(delta/2);
	    c = cos(th); s=sin(th);
	} else {
	    double lasta = delta<0 ? a+M_PI/2 : a-M_PI/2;
	    double d = (startangle+delta-lasta);
	    double th = lasta+d/2;
	    hypot = 1.0/cos(d/2);
	    c = cos(th); s=sin(th);
	}
	t2x = rx*c*hypot; t2y = ry*s*hypot;
	temp.x = cosr*t2x - sinr*t2y + cx;
	temp.y = sinr*t2x + cosr*t2y + cy;
	cur->last->nextcp.x = cur->last->me.x + .552*(temp.x-cur->last->me.x);
	cur->last->nextcp.y = cur->last->me.y + .552*(temp.y-cur->last->me.y);
	fin->prevcp.x = fin->me.x + .552*(temp.x-fin->me.x);
	fin->prevcp.y = fin->me.y + .552*(temp.y-fin->me.y);
	cur->last->nonextcp = fin->noprevcp = false;
    }
    *current = fin->me;
    fig.splines_pool.construct (cur->last, fin, false);
    cur->last = fin;
}

void ConicGlyph::svgParsePath (DrawableFigure &fig, const std::string &d) {
    BasePoint current;
    ConicPointList *cur = nullptr;
    ConicPoint *sp;
    char type = 'M';
    double x1, x2, x, y1, y2, y;
    std::stringstream ss;

    std::vector<ConicPointList> &conics = fig.contours;
    boost::object_pool<ConicPoint> &points_pool = fig.points_pool;
    boost::object_pool<Conic> &splines_pool = fig.splines_pool;

    current.x = current.y = 0;
    ss.str (d);
    ss.imbue (std::locale::classic ());

    while (!ss.eof () && ss.peek () != '\0') {
        ss >> std::ws;
        if (isalpha ((char) ss.peek ())) ss >> type;
	if (type=='m' || type=='M') {
            //fig.svgClosePath (cur, fig.order2);
	    if (cur)
		current = cur->first->me;
	    ss >> std::ws >> x >> std::ws;
	    if (ss.peek () == ',') ss.ignore (1);
	    ss >> std::ws >> y >> std::ws;
	    if (type=='m') {
		x += current.x; y += current.y;
	    }
            sp = points_pool.construct (x, y);
	    current = sp->me;
	    conics.emplace_back ();
            cur = &conics.back ();
            cur->first = cur->last = sp;
            sp->isfirst = true;
	    /* GWW: If you omit a command after a moveto then it defaults to lineto */
	    if (type=='m') type='l';
	    else type = 'L';
	} else if (type=='z' || type=='Z') {
            fig.svgClosePath (cur, fig.order2);
	    if (cur)
		current = cur->first->me;
	    cur = nullptr;
	    type = ' ';
	} else {
	    if (!cur) {
                sp = points_pool.construct (current.x, current.y);
		conics.emplace_back ();
                cur = &conics.back ();
                cur->first = cur->last = sp;
                sp->isfirst = true;
	    }
	    switch (type) {
	      case 'l': case'L':
                ss >> std::ws >> x >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> y >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
		if (type=='l') {
		    x += current.x; y += current.y;
		}
                cur->last->nonextcp = true;
                sp = points_pool.construct (x, y);
		current = sp->me;
		splines_pool.construct (cur->last, sp, fig.order2);
		cur->last = sp;
	      break;
	      case 'h': case'H':
                ss >> std::ws >> x >> std::ws;
		y = current.y;
		if ( type=='h' ) {
		    x += current.x;
		}
                cur->last->nonextcp = true;
                sp = points_pool.construct (x, y);
		current = sp->me;
		splines_pool.construct (cur->last, sp, fig.order2);
		cur->last = sp;
	      break;
	      case 'v': case 'V':
		x = current.x;
                ss >> std::ws >> y >> std::ws;
		if ( type=='v' ) {
		    y += current.y;
		}
                cur->last->nonextcp = true;
                sp = points_pool.construct (x, y);
		current = sp->me;
		splines_pool.construct (cur->last, sp, fig.order2);
		cur->last = sp;
	      break;
	      case 'c': case 'C':
                ss >> std::ws >> x1 >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> y1 >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> x2 >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> y2 >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> x >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> y >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
		if (type=='c') {
		    x1 += current.x; y1 += current.y;
		    x2 += current.x; y2 += current.y;
		    x += current.x; y += current.y;
		}
                sp = points_pool.construct (x, y);
		sp->prevcp.x = x2; sp->prevcp.y = y2; sp->noprevcp = false;
		cur->last->nextcp.x = x1; cur->last->nextcp.y = y1; cur->last->nonextcp = false;
		current = sp->me;
		splines_pool.construct (cur->last, sp, false);
		cur->last = sp;
	      break;
	      case 's': case 'S':
                // 's' is not necessarily preceded by a bezier curve
                if (!cur->last->noprevcp) {
                    x1 = 2*cur->last->me.x - cur->last->prevcp.x;
                    y1 = 2*cur->last->me.y - cur->last->prevcp.y;
                }
                ss >> std::ws >> x2 >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> y2 >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> x >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> y >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
		if (type=='s') {
		    x2 += current.x; y2 += current.y;
		    x += current.x; y += current.y;
		}
                sp = points_pool.construct (x, y);
		sp->prevcp.x = x2; sp->prevcp.y = y2;
                sp->noprevcp = false;
		if (!cur->last->noprevcp) {
                    cur->last->nextcp.x = x1; cur->last->nextcp.y = y1;
                    cur->last->nonextcp = false;
                } else {
                    cur->last->nextcp = cur->last->me;
                    cur->last->nonextcp = true;
                }
		current = sp->me;
		splines_pool.construct (cur->last, sp, false);
		cur->last = sp;
	      break;
	      case 'Q': case 'q':
                ss >> std::ws >> x1 >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> y1 >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> x >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> y >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
		if (type=='q') {
		    x1 += current.x; y1 += current.y;
		    x += current.x; y += current.y;
		}
                sp = points_pool.construct (x, y);
		sp->prevcp.x = x1; sp->prevcp.y = y1; sp->noprevcp = false;
		cur->last->nextcp.x = x1; cur->last->nextcp.y = y1; cur->last->nonextcp = false;
		current = sp->me;
		splines_pool.construct (cur->last, sp, true);
		cur->last = sp;
		fig.order2 = true;
	      break;
	      case 'T': case 't':
                ss >> std::ws >> x >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> y >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
		if (type=='t') {
		    x += current.x; y += current.y;
		}
                sp = points_pool.construct (x, y);
                if (!cur->last->noprevcp) {
                    x1 = 2*cur->last->me.x - cur->last->prevcp.x;
                    y1 = 2*cur->last->me.y - cur->last->prevcp.y;
                    cur->last->nextcp.x = x1; cur->last->nextcp.y = y1; cur->last->nonextcp = false;
                    sp->prevcp.x = x1; sp->prevcp.y = y1; sp->noprevcp = false;
                } else {
                    cur->last->nonextcp = true;
                    sp->noprevcp = true;
                }
		current = sp->me;
		splines_pool.construct (cur->last, sp, true);
		cur->last = sp;
		fig.order2 = true;
	      break;
	      case 'A': case 'a': {
                std::map<std::string, double> arc_props;
                double axisrot;
                int large_arc, sweep;
                ss >> std::ws >> arc_props["rx"] >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> arc_props["ry"] >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> axisrot >> std::ws;
		arc_props["axisrot"] = axisrot*M_PI/180;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> large_arc >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> sweep >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> x >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
                ss >> std::ws >> y >> std::ws;
                if (ss.peek () == ',') ss.ignore (1);
		if (type=='a') {
		    x += current.x; y += current.y;
		}
                arc_props["x"] = x; arc_props["y"] = y;
		if (x!=current.x || y!=current.y)
		    svgTraceArc (fig, cur, &current, arc_props, large_arc, sweep);
	      } break;
	      default:
		FontShepherd::postError (QCoreApplication::tr (
                    "Unknown type '%1' found in path specification").arg (type));
	      break;
	    }
	}
	ss >> std::ws;
    }
    //fig.svgClosePath (cur, order2);
}

// When preparing an SVG figure for paste into a TrueType/PS glyph, use
// inverted spline direction, as we are going to inverte all y-coordinates
// later in order to compensate for the SVG coordinate system.
// However, this inversion is not needed if we are drawing an ellipse
// in the graphics scene nd then immedately converting it to a path
void ConicGlyph::svgParseEllipse (DrawableFigure &fig, bool inv) {
    double cx, cy, rx, ry;
    ConicPoint *sp;
    ConicPointList cur;
    double x_ctl_off, y_ctl_off;
    boost::object_pool<ConicPoint> &points_pool = fig.points_pool;
    boost::object_pool<Conic> &splines_pool = fig.splines_pool;
    auto &conics = fig.contours;

    if (fig.type.compare ("circle") == 0 && fig.props.count ("r")) {
        rx = ry = std::abs (fig.props["r"]);
    } else {
        rx = std::abs (fig.props["rx"]);
        ry = std::abs (fig.props["ry"]);
    }
    cx = fig.props["cx"];
    cy = fig.props["cy"];

    if (inv) ry = -ry;
    x_ctl_off = rx*4*(sqrt(2)-1)/3;
    y_ctl_off = ry*4*(sqrt(2)-1)/3;

    cur = ConicPointList ();
    conics.push_back (cur);
    ConicPointList &newss = conics.back ();
    newss.first = points_pool.construct (cx-rx, cy);
    newss.first->prevcp.x = cx-rx;
    newss.first->prevcp.y = cy+y_ctl_off;
    newss.first->nextcp.x = cx-rx;
    newss.first->nextcp.y = cy-y_ctl_off;
    newss.first->noprevcp = newss.first->nonextcp = false;
    newss.first->isfirst = true;
    newss.first->pointtype = pt_curve;
    newss.last = points_pool.construct (cx, cy-ry);
    newss.last->prevcp.x = cx-x_ctl_off;
    newss.last->prevcp.y = cy-ry;
    newss.last->nextcp.x = cx+x_ctl_off;
    newss.last->nextcp.y = cy-ry;
    newss.last->noprevcp = newss.last->nonextcp = false;
    newss.last->pointtype = pt_curve;
    splines_pool.construct (newss.first, newss.last, false);
    sp = points_pool.construct (cx+rx, cy);
    sp->prevcp.x = cx+rx;
    sp->prevcp.y = cy-y_ctl_off;
    sp->nextcp.x = cx+rx;
    sp->nextcp.y = cy+y_ctl_off;
    sp->nonextcp = sp->noprevcp = false;
    sp->pointtype = pt_curve;
    splines_pool.construct (newss.last, sp, false);
    newss.last = sp;
    sp = points_pool.construct (cx, cy+ry);
    sp->prevcp.x = cx+x_ctl_off;
    sp->prevcp.y = cy+ry;
    sp->nextcp.x = cx-x_ctl_off;
    sp->nextcp.y = cy+ry;
    sp->nonextcp = sp->noprevcp = false;
    sp->pointtype = pt_curve;
    splines_pool.construct (newss.last, sp, false);
    splines_pool.construct (sp, newss.first, false);
    newss.last = newss.first;
}

void ConicGlyph::svgParseRect (DrawableFigure &fig, bool inv) {
    ConicPoint *sp;
    ConicPointList cur;
    double x = fig.props["x"];
    double y = fig.props["y"];
    double width = fig.props["width"];
    double height = fig.props["height"];
    double rx = fig.props.count ("rx") ? fig.props["rx"] : 0;
    double ry = fig.props.count ("ry") ? fig.props["ry"] : rx;
    boost::object_pool<ConicPoint> &points_pool = fig.points_pool;
    boost::object_pool<Conic> &splines_pool = fig.splines_pool;
    auto &conics = fig.contours;

    if (2*rx>width) rx = width/2;
    if (2*ry>height) ry = height/2;
    if (inv) {
	y += height;
	height = -height;
    }

    cur = ConicPointList ();
    conics.push_back (cur);
    ConicPointList &newss = conics.back ();
    if (rx==0) {
        newss.first = points_pool.construct (x, y);
        newss.first->isfirst = true;
        newss.last = points_pool.construct (x+width, y);
	splines_pool.construct (newss.first, newss.last, false);
        sp = points_pool.construct (x+width, y+height);
	splines_pool.construct (newss.last, sp, false);
	newss.last = sp;
        sp = points_pool.construct (x, y+height);
	splines_pool.construct (newss.last, sp, false);
	splines_pool.construct (sp, newss.first, false);
	newss.last = newss.first;
    } else {
        newss.first = points_pool.construct (x, y+ry);
        newss.first->nonextcp = false;
	newss.first->nextcp.x = x;
        newss.first->nextcp.y = y;
        newss.first->isfirst = true;
	newss.first->pointtype = pt_tangent;
        newss.last = points_pool.construct (x+rx, y);
        newss.first->noprevcp = false;
        newss.last->prevcp = newss.first->nextcp;
	newss.last->pointtype = pt_tangent;
	newss.first->pointtype = pt_tangent;
	splines_pool.construct (newss.first, newss.last, false);

	if (rx<2*width) {
	    sp = points_pool.construct (x+width-rx, y);
	    sp->pointtype = pt_tangent;
	    splines_pool.construct (newss.last, sp, false);
	    newss.last = sp;
	}
        sp = points_pool.construct (x+width, y+ry);
	sp->prevcp.x = x+width; sp->prevcp.y = y;
	sp->pointtype = pt_tangent;
	newss.last->nextcp = sp->prevcp;
	newss.last->nonextcp = sp->noprevcp = false;
	splines_pool.construct (newss.last, sp, false);
	newss.last = sp;

	if (ry<2*width) {
	    sp = points_pool.construct (x+width, y+height-ry);
	    sp->pointtype = pt_tangent;
	    splines_pool.construct (newss.last, sp, false);
	    newss.last = sp;
	}
        sp = points_pool.construct (x+width-rx, y+height);
	sp->prevcp.x = x+width; sp->prevcp.y = y+height;
	sp->pointtype = pt_tangent;
	newss.last->nextcp = sp->prevcp;
	newss.last->nonextcp = sp->noprevcp = false;
	splines_pool.construct (newss.last, sp, false);
	newss.last = sp;

	if (rx<2*width) {
	    sp = points_pool.construct (x+rx, y+height);
	    sp->pointtype = pt_tangent;
	    splines_pool.construct (newss.last, sp, false);
	    newss.last = sp;
	}
	newss.last->nextcp.x = x; newss.last->nextcp.y = y+height;
	newss.last->nonextcp = false;
	if (ry>=2*height) {
	    newss.first->prevcp = newss.last->nextcp;
	    newss.first->noprevcp = false;
	} else {
	    sp = points_pool.construct (x, y+height-ry);
            sp->noprevcp = false;
	    sp->prevcp.x = x; sp->prevcp.y = y+height;
	    sp->pointtype = pt_tangent;
	    splines_pool.construct (newss.last, sp, false);
	    newss.last = sp;
	}
	splines_pool.construct (newss.last, newss.first, false);
	newss.first = newss.last;
    }
}

void ConicGlyph::svgParseLine (DrawableFigure &fig) {
    auto &conics = fig.contours;
    ConicPointList cur = ConicPointList ();
    conics.push_back (cur);
    ConicPointList &newss = conics.back ();
    boost::object_pool<ConicPoint> &points_pool = fig.points_pool;
    boost::object_pool<Conic> &splines_pool = fig.splines_pool;

    newss.first = points_pool.construct (fig.props["x1"], fig.props["y1"]);
    newss.first->isfirst = true;
    newss.last = points_pool.construct (fig.props["x2"], fig.props["y2"]);
    splines_pool.construct (newss.first, newss.last, false);
}

void ConicGlyph::svgParsePoly (DrawableFigure &fig, bool is_gon) {
    ConicPoint *sp;
    ConicPointList cur;
    uint32_t i;
    boost::object_pool<ConicPoint> &points_pool = fig.points_pool;
    boost::object_pool<Conic> &splines_pool = fig.splines_pool;
    auto &conics = fig.contours;

    if (fig.points.empty ())
        return;

    cur = ConicPointList ();
    conics.push_back (cur);
    ConicPointList &newss = conics.back ();
    newss.first = newss.last = points_pool.construct ();
    newss.first->me = newss.first->nextcp = newss.first->prevcp = fig.points[0];
    newss.first->nonextcp = newss.first->noprevcp = true;
    newss.first->isfirst = true;
    for (i=1; i<fig.points.size (); i++) {
	sp = points_pool.construct ();
        sp->noprevcp = sp->nonextcp = true;
        sp->me = sp->nextcp = sp->prevcp = fig.points[i];
	splines_pool.construct (newss.last, sp, false);
	newss.last = sp;
    }
    if (is_gon) {
	if (realNear (newss.last->me.x, newss.first->me.x) &&
            realNear (newss.last->me.y, newss.first->me.y)) {
	    newss.first->prev = newss.last->prev;
	    newss.first->prev->to = newss.first;
            points_pool.destroy (newss.last);
	} else
	    splines_pool.construct (newss.last, newss.first, false);
	newss.last = newss.first;
    }
}

void ConicGlyph::figureAddGradient (
    pugi::xml_document &doc, Drawable &fig, std::array<double, 6> &transform, bool is_stroke) {

    DBounds fb;
    bool res;
    Gradient grad;
    bool &set = is_stroke ? fig.svgState.stroke_set : fig.svgState.fill_set;
    std::string &grad_id = is_stroke ? fig.svgState.stroke_source_id : fig.svgState.fill_source_id;
    struct rgba_color &default_color = is_stroke ? fig.svgState.stroke : fig.svgState.fill;

    fig.realBounds (fb, true);
    res = xmlParseColorSource (doc, grad_id, fb, default_color, transform, grad, true);
    if (!res)
	grad_id.clear ();
    else {
	set = true;
	gradients[grad_id] = grad;
    }
}

void ConicGlyph::svgProcessNode (
    pugi::xml_document &doc, pugi::xml_node &root, std::array<double, 6> &transform, SvgState &state) {

    static std::string svg_figs[] = { "path", "circle", "ellipse", "rect", "polygon", "polyline", "line" };
    static std::set<std::string> figures_set (svg_figs, svg_figs + 7);
    std::array<double, 6> newtrans = {1, 0, 0, 1, 0, 0};
    std::array<double, 6> combtrans = {1, 0, 0, 1, 0, 0};
    SvgState newstate (state);
    std::string name = root.name ();
    pugi::xml_attribute trans_attr = root.attribute ("transform");
    pugi::xml_attribute fill_attr = root.attribute ("fill");
    pugi::xml_attribute fill_opacity_attr = root.attribute ("fill-opacity");
    pugi::xml_attribute stroke_attr = root.attribute ("stroke");
    pugi::xml_attribute stroke_opacity_attr = root.attribute ("stroke-opacity");
    pugi::xml_attribute width_attr = root.attribute ("stroke-width");
    pugi::xml_attribute linecap_attr = root.attribute ("stroke-linecap");
    pugi::xml_attribute linejoin_attr = root.attribute ("stroke-linejoin");
    int hintcnt = hstem.size () + vstem.size ();

    if (trans_attr && name.compare ("svg")!=0) {
        std::string tstr = trans_attr.value ();
        svgFigureTransform (tstr, newtrans);
        matMultiply (newtrans.data (), transform.data (), combtrans.data ());
    } else
	combtrans = transform;

    if (m_outType == OutlinesType::SVG || m_outType == OutlinesType::COLR) {
	if (fill_attr) {
	    std::string str_attr = fill_attr.value ();
	    // can process gradients only after parsing contours/shapes
	    if (str_attr.compare (0, 3, "url")==0) {
		newstate.fill_source_id = parseSourceUrl (str_attr);
		if (gradients.count (newstate.fill_source_id))
		    newstate.fill_set = true;
	    } else if (str_attr.compare (0, 3, "var")==0) {
		uint8_t res = parseVariableColor (str_attr, newstate, false);
		newstate.fill_set = res;
	    } else
		newstate.setFillColor (fill_attr.value ());
	}
	if (fill_opacity_attr)
	    newstate.setFillOpacity (fill_opacity_attr.as_float ());
	if (stroke_attr) {
	    std::string str_attr = stroke_attr.value ();
	    if (str_attr.compare (0, 3, "url")==0) {
		newstate.stroke_source_id = parseSourceUrl (str_attr);
		if (gradients.count (newstate.stroke_source_id))
		    newstate.stroke_set = true;
	    } else if (str_attr.compare (0, 3, "var")==0) {
		uint8_t res = parseVariableColor (str_attr, newstate, true);
		newstate.stroke_set = res;
	    } else
		newstate.setStrokeColor (stroke_attr.value ());
	}
	if (stroke_opacity_attr)
	    newstate.setStrokeOpacity (stroke_opacity_attr.as_float ());
	if (width_attr)
	    newstate.setStrokeWidth (width_attr.value (), GID);
	if (linecap_attr)
	    newstate.setLineCap (linecap_attr.value ());
	if (linejoin_attr)
	    newstate.setLineJoin (linejoin_attr.value ());
    }

    if (figures_set.count (name)) {
        figures.emplace_back ();
        DrawableFigure &fig = figures.back ();
        std::vector<ConicPointList> &conics = fig.contours;
        uint16_t i;

        fig.type = name;
        fig.svgState = newstate;
	fig.order2 = false;
        if (name.compare ("path") == 0) {
            pugi::xml_attribute d_attr = root.attribute ("d");
            pugi::xml_attribute pp_attr = root.attribute ("fsh:point-properties");
            if (d_attr) {
                std::string d = d_attr.value ();
                svgParsePath (fig, d);
                if (pp_attr) {
                    std::string pp = pp_attr.value ();
                    fig.svgReadPointProps (pp, hintcnt);
                }
            }

        } else if (name.compare ("circle") == 0 || name.compare ("ellipse") == 0) {
            bool is_circle = (name.compare ("circle") == 0);
            pugi::xml_attribute r_attr = root.attribute ("r");
            pugi::xml_attribute rx_attr = root.attribute ("rx");
            pugi::xml_attribute ry_attr = root.attribute ("ry");
            pugi::xml_attribute cx_attr = root.attribute ("cx");
            pugi::xml_attribute cy_attr = root.attribute ("cy");

            if (((is_circle && r_attr) || (!is_circle && rx_attr && ry_attr)) && cx_attr && cy_attr) {
                if (is_circle) {
                    fig.props["r"] = stringToDouble (r_attr.value ());
                    fig.props["rx"] = fig.props["r"];
                    fig.props["ry"] = fig.props["r"];
                } else {
                    fig.props["rx"] = stringToDouble (rx_attr.value ());
                    fig.props["ry"] = stringToDouble (ry_attr.value ());
                }
                fig.props["cx"] = stringToDouble (cx_attr.value ());
                fig.props["cy"] = stringToDouble (cy_attr.value ());
		if (m_outType != OutlinesType::SVG ||
		    (trans_attr && (combtrans[1] || combtrans[2]))) {
		    svgParseEllipse (fig, true);
		    fig.type = "path";
		} else {
		    svgTransformEllipse (fig, combtrans);
		}
            }

        } else if (name.compare ("rect") == 0) {
            pugi::xml_attribute x_attr = root.attribute ("x");
            pugi::xml_attribute y_attr = root.attribute ("y");
            pugi::xml_attribute w_attr = root.attribute ("width");
            pugi::xml_attribute h_attr = root.attribute ("height");
            pugi::xml_attribute rx_attr = root.attribute ("rx");
            pugi::xml_attribute ry_attr = root.attribute ("ry");

            if (x_attr && y_attr && w_attr && h_attr) {
                fig.props["x"] = stringToDouble (x_attr.value ());
                fig.props["y"] = stringToDouble (y_attr.value ());
                fig.props["width"] = stringToDouble (w_attr.value ());
                fig.props["height"] = stringToDouble (h_attr.value ());
                if (rx_attr)
                    fig.props["rx"] = stringToDouble (rx_attr.value ());
                if (ry_attr)
                    fig.props["ry"] = stringToDouble (ry_attr.value ());

		if (m_outType != OutlinesType::SVG ||
		    (trans_attr && (combtrans[1] || combtrans[2]))) {
		    svgParseRect (fig, true);
		    fig.type = "path";
		} else {
		    svgTransformRect (fig, combtrans);
		}
            }

        } else if (name.compare ("line") == 0) {
            pugi::xml_attribute x1_attr = root.attribute ("x1");
            pugi::xml_attribute y1_attr = root.attribute ("y1");
            pugi::xml_attribute x2_attr = root.attribute ("x2");
            pugi::xml_attribute y2_attr = root.attribute ("y2");

            if (x1_attr && y1_attr && x2_attr && y2_attr) {
                fig.props["x1"] = stringToDouble (x1_attr.value ());
                fig.props["y1"] = stringToDouble (y1_attr.value ());
                fig.props["x2"] = stringToDouble (x2_attr.value ());
                fig.props["y2"] = stringToDouble (y2_attr.value ());

                svgParseLine (fig);
                svgTransformLine (fig, combtrans);
            }

        } else if (name.compare ("polyline") == 0 || name.compare ("polygon") == 0) {
            pugi::xml_attribute pt_attr = root.attribute ("points");
            if (pt_attr) {
                std::istringstream iss (pt_attr.value ());
                BasePoint bp;
                while (!iss.eof () && iss.good ()) {
                    iss >> std::ws >> bp.x >>std::ws;
                    if (iss.peek () == ',') iss.ignore (1);
                    iss >> std::ws >> bp.y >>std::ws;
                    if (iss.peek () == ',') iss.ignore (1);
                    fig.points.push_back (bp);
                }

                svgParsePoly (fig, name.compare ("polygon") == 0);
                svgTransformPoly (fig, combtrans);
            }
        }
        for (i=0; i<conics.size (); i++)
            conics[i].doTransform (combtrans);
        if (!fig.svgState.fill_source_id.empty () && !fig.svgState.fill_set)
            figureAddGradient (doc, fig, combtrans, false);
        if (!fig.svgState.stroke_source_id.empty () && !fig.svgState.stroke_set)
            figureAddGradient (doc, fig, combtrans, true);

    } else if (name.compare ("use")==0) {
        pugi::xml_attribute href_attr = root.attribute ("xlink:href");
        if (href_attr && href_attr.value ()[0] == '#') {
            std::string href = href_attr.value ()+1;
            std::stringstream ss;
            pugi::xpath_node_set match;

            ss << "//*[@id='" << href << "']";
            match = doc.select_nodes (ss.str ().c_str ());
            if (match.size () > 0) {
                pugi::xml_node source = match[0].node ();
                uint16_t ref_gid = 0;
		OutlinesType ref_type = (m_outType == OutlinesType::COLR) ?
		    OutlinesType::NONE : m_outType;
                if (href.compare (0, 5, "glyph")==0) {
                    std::istringstream iss (href.substr (5));
                    if ((iss >> ref_gid).fail ()) ref_gid = 0;
                } else if (href.compare (0, 10, "colr-glyph")==0) {
                    std::istringstream iss (href.substr (10));
                    if ((iss >> ref_gid).fail ()) ref_gid = 0;
		    ref_type = OutlinesType::COLR;
		}

                // link to another glyph described in the same document
                if (ref_gid) {
                    DrawableReference cur;
                    cur.GID = ref_gid;
		    cur.outType = ref_type;
		    cur.svgState = newstate;
		    cur.transform = combtrans;

		    if (!cur.svgState.fill_source_id.empty () && !cur.svgState.fill_set)
			figureAddGradient (doc, cur, combtrans, false);
		    if (!cur.svgState.stroke_source_id.empty () && !cur.svgState.stroke_set)
			figureAddGradient (doc, cur, combtrans, true);
                    refs.push_back (cur);
                } else
                    svgProcessNode (doc, source, combtrans, newstate);
            }
        }
    } else if (name.compare ("g") == 0 || name.compare ("svg") == 0) {
        for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling())
            svgProcessNode (doc, child, combtrans, newstate);
    } else if (name.compare ("fsh:horizontal-metrics") == 0) {
        pugi::xml_attribute aw_attr = root.attribute ("fsh:advance-width");
        pugi::xml_attribute lsb_attr = root.attribute ("fsh:left-sidebearing");
        m_aw = aw_attr.as_int ();
        m_lsb = lsb_attr.as_int ();
    } else if (name.compare ("fsh:ps-hints") == 0) {
        pugi::xml_attribute hstem_attr = root.attribute ("fsh:hstem");
        pugi::xml_attribute vstem_attr = root.attribute ("fsh:vstem");
        pugi::xml_attribute cntrm_attr = root.attribute ("fsh:countermasks");

	if (hstem_attr) {
	    std::istringstream is (hstem_attr.value ());
	    for (double start, w; is >> start, is >> w;)
		appendHint (start, w, false);
	}
	if (vstem_attr) {
	    std::istringstream is (vstem_attr.value ());
	    for (double start, w; is >> start, is >> w;)
		appendHint (start, w, true);
	}
	hintcnt = hstem.size () + vstem.size ();
	if (cntrm_attr) {
	    std::istringstream is (cntrm_attr.value ());
	    std::string hex;
	    while (std::getline (is, hex, ' ')) {
		HintMask cm;
		for (size_t i = 0; i < hex.length (); i += 2) {
		    std::string bstr = hex.substr (i, 2);
                    std::istringstream hss (bstr);
		    unsigned int tmp;
                    hss >> std::hex >> tmp;
		    cm[i] = tmp;
		}
		countermasks.push_back (cm);
	    }
	}
    }
}

static void readViewBox (std::string &str_attr, DBounds &vb) {
    std::stringstream ss;
    ss.str (str_attr);
    ss.imbue (std::locale::classic ());
    ss >> std::ws >> vb.minx >> std::ws;
    if (ss.peek () == ',') ss.ignore (1);
    ss >> std::ws >> vb.miny >> std::ws;
    if (ss.peek () == ',') ss.ignore (1);
    // NB: the following values are actually width and height here
    ss >> std::ws >> vb.maxx >> std::ws;
    if (ss.peek () == ',') ss.ignore (1);
    ss >> std::ws >> vb.maxy >> std::ws;
    if (ss.peek () == ',') ss.ignore (1);
}

void ConicGlyph::svgCheckArea (pugi::xml_node svg, std::array<double, 6> &matrix) {
    double h;
    pugi::xml_attribute vb_attr = svg.attribute ("viewBox");
    pugi::xml_attribute transform = svg.attribute ("transform");
    DBounds vb = DBounds ();

    if (vb_attr) {
        std::string vbstr = vb_attr.value ();
        readViewBox (vbstr, vb);
	// maxy here represents the image height
	h = vb.maxy;
    }
    if (transform) {
        std::string tstr = transform.value ();
        svgFigureTransform (tstr, matrix);
	h *= matrix[3];
    }

    // In SVG - Emoji One Color (TTF version): View Box is specified in
    // unscaled image coordinates, while vertical shift is provided
    // in font units (for a 1000 UPM version). Additionally there is a 2.048
    // scale value (as the TTF outlines themselves are scaled to 2048 UPM).
    // So assume we should adjust only scale ratios here, while the shift
    // values (if previously provided) are already scaled and should be
    // only corrected by subtracting the minimum Y value of the View Box
    // (see the MS SVG table spec, the "Coordinate Systems and Glyph
    // Metrics" section).
    if (vb_attr) {
        double rat = units_per_em/h;
	for (int i=0; i<4; i++)
	    matrix[i] *=rat;
	// No need to subtract vb.minx*rat from matrix[4] (this would result
	// to resetting a negative left bearing to zero).
        matrix[5] -= vb.miny*rat;
    }
};

bool ConicGlyph::fromSVG (std::istream &buf, int g_idx, DrawableFigure *target) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load (buf);

    // Here's how we can extract entire buffer contents into a string
    //std::string s (std::istreambuf_iterator<char> (buf), {});

    if (result.status != pugi::status_ok) {
	buf.seekg (0);
	std::string s (std::istreambuf_iterator<char> (buf), {});
        FontShepherd::postError (
            QCoreApplication::tr ("Bad glyf data"),
            QCoreApplication::tr (
                "Could not load SVG data for glyph %1: "
                "doesn't seem to be an SVG document").arg (GID),
            nullptr);
        return false;
    }
    return fromSVG (doc, g_idx, target);
}

bool ConicGlyph::fromSVG (pugi::xml_document &doc, int g_idx, DrawableFigure *target) {
    std::array<double, 6> trans = {1, 0, 0, 1, 0, 0};
    SvgState state;
    std::array<double, 6> inv = {1, 0, 0, -1, 0, 0};
    uint16_t old_fig_cnt = figures.size (), old_ref_cnt = refs.size ();

    char element_spec[32] = {0};
    char *elptr = element_spec;
    pugi::xml_node svg, root;
    pugi::xpath_node_set match;

    // used when updating existing glyph by undo/redo commands, so make sure
    // we are not going to occasionally change its outlines type
    if (m_outType == OutlinesType::NONE)
	m_outType = OutlinesType::SVG;

    sprintf (elptr, "//svg");
    match = doc.select_nodes (elptr);
    if (match.size () > 0) {
        svg = match[0].node ();
        svgCheckArea (svg, trans);
    } else {
        FontShepherd::postError (
            QCoreApplication::tr ("Bad glyf data"),
            QCoreApplication::tr (
                "Could not load SVG data for glyph %1: "
                "doesn't seem to be an SVG document").arg (GID),
            nullptr);
        return false;
    }

    std::string glyph_prefix = (m_outType == OutlinesType::COLR) ? "colr-glyph" : "glyph";
    if (g_idx < 0)
        sprintf (elptr, "//*[@id='%s%d']", glyph_prefix.c_str (), GID);
    else
        sprintf (elptr, "//g[starts-with (@id, 'glyph')]");
    match = doc.select_nodes (elptr);
    if (figures.empty ())
        target = nullptr;

    if ((int) match.size () <= g_idx)
        return false;
    else if (g_idx < 0 && match.size () > 0) {
        root = match[0].node ();
        svgProcessNode (doc, root, trans, state);
    } else if (g_idx >=0) {
        root = match[g_idx].node ();
        svgProcessNode (doc, root, trans, state);
    } else {
        FontShepherd::postError (
            QCoreApplication::tr (
                "There is no block with id='glyph%1' attribute in the corresponding "
                "SVG document. I will attempt to read the whole document instead").arg (GID));
        svgProcessNode (doc, svg, trans, state);
    }
    // compensate for the SVG coordinate system
    auto it = figures.begin ();
    std::advance (it, old_fig_cnt);
    for (; it != figures.end (); it++) {
        DrawableFigure &fig = *it;
	ElementType ftype = fig.elementType ();
        if (ftype == ElementType::Circle || ftype == ElementType::Ellipse) {
            svgTransformEllipse (fig, inv);
        } else if (ftype == ElementType::Rect) {
            svgTransformRect (fig, inv);
        } else if (fig.contours.size () > 0) {
            std::vector<ConicPointList> &conics = fig.contours;
            for (auto &c : conics)
                c.doTransform (inv);
        }
    }
    auto refit = refs.begin ();
    std::advance (refit, old_ref_cnt);
    for (; refit != refs.end (); refit++) {
        DrawableReference &ref = *refit;
        ref.transform[5] *= -1;
    }
    if (target && (int) figures.size () == old_fig_cnt + 1) {
        auto &source = figures.back ();
        if (target->mergeWith (source))
            figures.pop_back ();
    }

    if (m_outType != OutlinesType::SVG && !figures.empty ()) {
	mergeContours ();
	auto &fig = figures.back ();
	if (m_outType == OutlinesType::TT)
	    fig.toQuadratic (upm ()/1000);
	else if (m_outType == OutlinesType::PS)
	    fig.toCubic ();
    }

    categorizePoints ();
    checkBounds (bb, false);
    renumberPoints ();
#if 0
    std::cerr << std::fixed << "for GID " << GID << " got bounds "
        << bb.minx << ' ' << bb.miny << ' ' << bb.maxx << ' ' << bb.maxy << std::endl;
#endif
    return true;
}

static bool svgReadSinglePointProps (std::istringstream &ss, ConicPoint *sp, int hintcnt) {
    char ptype;
    ss >> ptype;
    switch (ptype) {
      case 'a':
        sp->pointtype = pt_corner;
      break;
      case 'c':
        sp->pointtype = pt_curve;
      break;
      case 't':
        sp->pointtype = pt_tangent;
      break;
      default:
        FontShepherd::postError (
            QCoreApplication::tr ("Unknown point type when parsing fsh:point-properties: %1").arg (ptype));
        sp->pointtype = pt_corner;
        return false;
    }
    if (ss.peek () == '{') {
        std::string spec;
        ss.ignore (1);
        if ((std::getline (ss, spec, '}')).fail ())
            return false;
        std::istringstream specs (spec);
        std::vector<std::string> toks;
        std::string item;
        while (std::getline (specs, item, ','))
            toks.push_back (item);
        if (toks.size () > 1) {
            uint16_t i, j;
            std::istringstream is0 (toks[0]), is1 (toks[1]);
            is0 >> sp->ttfindex;
            is1 >> sp->nextcpindex;
            for (i=2; i<toks.size (); i++) {
                std::istringstream tok_s (toks[i]);
                std::string cmd;
                if ((std::getline (tok_s, cmd, ':')).fail ())
                    return false;
                tok_s.ignore (1);
                if (cmd.compare ("hm")) {
                    std::unique_ptr<HintMask> hm = std::unique_ptr<HintMask> (new HintMask ());
                    for (j=0; j<(hintcnt+7)/8 && !tok_s.fail (); j++) {
                        char tmp_hex[3];
                        char *hexptr = tmp_hex;
                        tok_s.get (hexptr, 2);
                        std::istringstream hss (hexptr);
                        hss >> std::hex >> (*hm)[j];
                    }
                    sp->hintmask = std::move (hm);
                }
            }
        }
    }
    return true;
}

void DrawableFigure::svgReadPointProps (const std::string &pp, int hintcnt) {
    std::istringstream ss (pp);
    uint16_t i;

    for (i=0; i<contours.size (); i++) {
        ConicPointList &spls = contours[i];
        Conic *spl, *first=nullptr;
        svgReadSinglePointProps (ss, spls.first, hintcnt);
        for (spl = spls.first->next; spl && spl!=first && spl->to != spls.first; spl=spl->to->next) {
            svgReadSinglePointProps (ss, spl->to, hintcnt);
            if (!first)
                first=spls.first->next;
        }
    }
    svgState.point_props_set = true;
}

void DrawableFigure::svgClosePath (ConicPointList *cur, bool order2) {
    if (cur && cur->last!=cur->first) {
	if (realWithin (cur->last->me.x, cur->first->me.x, .05) &&
            realWithin (cur->last->me.y, cur->first->me.y, .05)) {
	    cur->first->prevcp = cur->last->prevcp;
	    cur->first->noprevcp = cur->last->noprevcp;
	    cur->first->prev = cur->last->prev;
	    cur->first->prev->to = cur->first;
	    points_pool.destroy (cur->last);
	} else {
	    cur->last->nonextcp = cur->first->noprevcp = true;
	    splines_pool.construct (cur->last, cur->first, order2);
        }
	cur->last = cur->first;
    }
}

ElementType DrawableFigure::elementType () const {
    bool linear = true;
    int spl_cnt = 0;

    if (type.compare ("circle") == 0 || type.compare ("ellipse") == 0) {
	if (realNear (std::abs (props.at ("rx")), std::abs (props.at ("ry"))))
	    return ElementType::Circle;
	else
	    return ElementType::Ellipse;
    } else if (type.compare ("rect") == 0) {
	return ElementType::Rect;
    }

    if (contours.size () != 1)
	return ElementType::Path;

    auto &spls = contours.front ();
    ConicPoint *sp = spls.first;
    do {
        ConicPoint *next = sp->next ? sp->next->to : nullptr;
        if (next) {
	    spl_cnt++;
	    linear &= sp->next->islinear;
        }
        sp = next;
    } while (linear && sp && sp!=spls.first);

    if (linear && spl_cnt) {
	if (spl_cnt == 1)
	    return ElementType::Line;
	else if (spls.first == spls.last)
	    return ElementType::Polygon;
	else
	    return ElementType::Polyline;
    }
    return ElementType::Path;
}
