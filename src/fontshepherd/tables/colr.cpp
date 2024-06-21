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

#include <cstring>
#include <iostream>
#include <sstream>
#include <ios>
#include <assert.h>
#include <exception>
#define _USE_MATH_DEFINES
#include <cmath>

#include "sfnt.h"
#include "editors/cpaledit.h"
#include "editors/fontview.h"
#include "tables/colr.h"
#include "tables/mtx.h"
#include "tables/name.h"

#include "fs_math.h"

ColrTable::ColrTable (sfntFile *fontfile, TableHeader &props) :
    GlyphContainer (fontfile, props) {
}

ColrTable::~ColrTable () {
}

void ColrTable::unpackData (sFont *fnt) {
    uint32_t pos = 0;
    uint16_t i;
    std::vector<layer_record> layer_records;
    if (td_loaded)
	return;

    GlyphContainer::unpackData (fnt);
    m_cpal = dynamic_cast<CpalTable *> (fnt->table (CHR ('C','P','A','L')));
    m_version = getushort (0);
    uint16_t numBaseGlyphRecords = getushort (2);
    uint32_t offsetBaseGlyphRecord = getlong (4);
    uint32_t offsetLayerRecord = getlong (8);
    uint16_t numLayerRecords = getushort (12);

    pos = offsetBaseGlyphRecord;
    m_baseGlyphRecords.resize (numBaseGlyphRecords);
    for (i=0; i<numBaseGlyphRecords; i++) {
        m_baseGlyphRecords[i].GID = getushort (pos); pos+=2;
        m_baseGlyphRecords[i].firstLayerIndex = getushort (pos); pos+=2;
        m_baseGlyphRecords[i].numLayers = getushort (pos); pos+=2;
    }

    pos = offsetLayerRecord;
    layer_records.resize (numLayerRecords);
    for (i=0; i<numLayerRecords; i++) {
        layer_records[i].GID = getushort (pos); pos+=2;
        layer_records[i].paletteIndex = getushort (pos); pos+=2;
    }

    for (i=0; i<numBaseGlyphRecords; i++) {
	struct base_glyph_record &bgr = m_baseGlyphRecords[i];
	bgr.layers.reserve (bgr.numLayers);
	for (uint16_t j=bgr.firstLayerIndex; j<bgr.firstLayerIndex+bgr.numLayers; j++)
	    bgr.layers.push_back (layer_records[j]);
    }

    if (m_version == 0) {
	td_loaded = true;
	return;
    }

    std::map<uint32_t,std::shared_ptr<PaintTable>> ptab_map;
    std::map<uint32_t,std::shared_ptr<ColorLine>> cline_map;

    // v. 1--specific data
    pos = 14;
    baseGlyphListOffset = getlong (pos); pos+=4;
    layerListOffset = getlong (pos); pos+=4;
    clipListOffset = getlong (pos); pos+=4;
    varIndexMapOffset = getlong (pos); pos+=4;
    itemVariationStoreOffset = getlong (pos); pos+=4;

    if (baseGlyphListOffset) {
	pos = baseGlyphListOffset;
	uint32_t numBaseGlyphPaintRecords = getlong (pos); pos+=4;
	m_baseGlyphList.reserve (numBaseGlyphPaintRecords);
	for (uint32_t i=0; i<numBaseGlyphPaintRecords; i++) {
	    m_baseGlyphList.emplace_back ();
	    auto &gl = m_baseGlyphList.back ();
	    gl.GID = getushort (pos); pos+=2;
	    gl.paintOffset = getlong (pos); pos+=4;
	    gl.paintTable = readPaintTable (gl.paintOffset + baseGlyphListOffset, ptab_map, cline_map);
	}
    }

    std::vector<uint32_t> layerPaintOffsets;
    if (layerListOffset) {
	pos = layerListOffset;
	uint32_t numLayers = getlong (pos); pos+=4;
	layerPaintOffsets.resize (numLayers);
	m_layerList.reserve (numLayers);
	for (size_t i=0; i<numLayers; i++) {
	    layerPaintOffsets[i] = getlong (pos); pos+=4;
	    m_layerList.push_back
		(readPaintTable (layerPaintOffsets[i] + layerListOffset, ptab_map, cline_map));
	}
    }

    if (clipListOffset) {
	// skip 1 byte for format
	pos = clipListOffset+1;
	uint32_t numClips = getlong (pos); pos+=4;
	m_clipRecords.reserve (numClips);
	for (size_t i=0; i<numClips; i++) {
	    m_clipRecords.emplace_back ();
	    auto &cr = m_clipRecords.back ();
	    cr.startGlyphID = getushort (pos); pos+=2;
	    cr.endGlyphID = getushort (pos); pos+=2;
	    cr.clipBoxOffset = get3bytes (pos); pos+=3;
	}
    }

    for (auto &cr : m_clipRecords) {
	pos = clipListOffset + cr.clipBoxOffset;
	cr.clipBoxFormat = data[pos]; pos++;
	cr.xMin = getushort (pos); pos+=2;
	cr.yMin = getushort (pos); pos+=2;
	cr.xMax = getushort (pos); pos+=2;
	cr.yMax = getushort (pos); pos+=2;
	if (cr.clipBoxFormat == 2) {
	    cr.varIndexBase = getlong (pos); pos+=4;
	}
    }

    if (varIndexMapOffset)
	FontVariations::readIndexMap (data, varIndexMapOffset, m_deltaSetIndexMap);
    if (itemVariationStoreOffset)
	FontVariations::readVariationStore (data, itemVariationStoreOffset, m_varStore);
    td_loaded = true;
}

std::shared_ptr<ColorLine> ColrTable::readColorLine (
	uint32_t off,
	std::map<uint32_t, std::shared_ptr<ColorLine>> &cline_map,
	bool var
    ) {
    if (cline_map.count (off))
	return cline_map.at (off);

    cline_map.emplace (std::make_pair (off, std::make_shared<ColorLine> ()));
    auto clptr = cline_map.at (off);
    clptr->isVariable = var;
    clptr->extend = data[off];
    uint32_t pos = off + 1;
    uint16_t num_stops = getushort (pos); pos+=2;
    clptr->colorStops.reserve (num_stops);

    for (size_t i=0; i<num_stops; i++) {
	clptr->colorStops.emplace_back ();
	auto &cstop = clptr->colorStops.back ();
	cstop.isVariable = var;
	cstop.stopOffset = get2dot14 (pos); pos+=2;
	cstop.paletteIndex = getushort (pos); pos+=2;
	cstop.alpha = get2dot14 (pos); pos+=2;
	if (var) {
	    cstop.varIndexBase = getlong (pos); pos+=4;
	}
    }


    return clptr;
}

std::shared_ptr<PaintTable> ColrTable::readPaintTable (
	uint32_t off,
	std::map<uint32_t, std::shared_ptr<PaintTable>> &ptab_map,
	std::map<uint32_t, std::shared_ptr<ColorLine>> &cline_map
    ) {
    if (ptab_map.count (off))
	return ptab_map.at (off);

    uint8_t format = data[off];
    ptab_map.emplace (std::make_pair (off, std::make_shared<PaintTable> (format)));
    auto ptptr = ptab_map.at (off);
    auto &pt = *ptptr;

    uint32_t pos = off+1;
    std::cerr << "read format " << (int) format << std::endl;

    switch (format) {
      case 1:
	pt.paintColrLayers.numLayers = data[pos]; pos++;
	pt.paintColrLayers.firstLayerIndex = getlong (pos); pos+=4;
	break;
      case 2:
      case 3:
	pt.paintSolid.isVariable = (format == 3);
        pt.paintSolid.paletteIndex = getushort (pos); pos+=2;
        pt.paintSolid.alpha = get2dot14 (pos); pos+=2;
	if (format == 3) {
	    pt.paintSolid.varIndexBase = getlong (pos); pos+=4;
	}
	break;
      case 4:
      case 5: {
	std::stringstream ss;
	ss << "gradient-" << off;
	pt.paintLinearGradient.id = ss.str ();
	pt.paintLinearGradient.isVariable = (format == 5);
	pt.paintLinearGradient.colorLineOffset = get3bytes (pos); pos+=3;
	pt.paintLinearGradient.x0 = getushort (pos); pos+=2;
	pt.paintLinearGradient.y0 = getushort (pos); pos+=2;
	pt.paintLinearGradient.x1 = getushort (pos); pos+=2;
	pt.paintLinearGradient.y1 = getushort (pos); pos+=2;
	pt.paintLinearGradient.x2 = getushort (pos); pos+=2;
	pt.paintLinearGradient.y2 = getushort (pos); pos+=2;
	if (format == 5) {
	    pt.paintLinearGradient.varIndexBase = getlong (pos); pos+=4;
	}
	pt.paintLinearGradient.colorLine =
	    readColorLine (off + pt.paintLinearGradient.colorLineOffset, cline_map, format == 5);
      } break;
      case 6:
      case 7: {
	std::stringstream ss;
	ss << "gradient-" << off;
	pt.paintLinearGradient.id = ss.str ();
	pt.paintRadialGradient.isVariable = (format == 7);
	pt.paintRadialGradient.colorLineOffset = get3bytes (pos); pos+=3;
	pt.paintRadialGradient.x0 = getushort (pos); pos+=2;
	pt.paintRadialGradient.y0 = getushort (pos); pos+=2;
	pt.paintRadialGradient.radius0 = getushort (pos); pos+=2;
	pt.paintRadialGradient.x1 = getushort (pos); pos+=2;
	pt.paintRadialGradient.y1 = getushort (pos); pos+=2;
	pt.paintRadialGradient.radius1 = getushort (pos); pos+=2;
	if (format == 7) {
	    pt.paintRadialGradient.varIndexBase = getlong (pos); pos+=4;
	}
	pt.paintRadialGradient.colorLine =
	    readColorLine (off + pt.paintRadialGradient.colorLineOffset, cline_map, format == 7);
      } break;
      case 8:
      case 9: {
	std::stringstream ss;
	ss << "gradient-" << off;
	pt.paintLinearGradient.id = ss.str ();
	pt.paintSweepGradient.isVariable = (format == 9);
	pt.paintSweepGradient.colorLineOffset = get3bytes (pos); pos+=3;
	pt.paintSweepGradient.centerX = getushort (pos); pos+=2;
	pt.paintSweepGradient.centerY = getushort (pos); pos+=2;
	pt.paintSweepGradient.startAngle = getushort (pos); pos+=2;
	pt.paintSweepGradient.endAngle = getushort (pos); pos+=2;
	if (format == 9) {
	    pt.paintSweepGradient.varIndexBase = getlong (pos); pos+=4;
	}
	pt.paintSweepGradient.colorLine =
	    readColorLine (off + pt.paintSweepGradient.colorLineOffset, cline_map, format == 9);
      } break;
      case 10:
	pt.paintGlyph.paintOffset = get3bytes (pos); pos+=3;
	pt.paintGlyph.GID = getushort (pos); pos+=2;
	pt.paintGlyph.paintTable = readPaintTable (off + pt.paintGlyph.paintOffset, ptab_map, cline_map);
	break;
      case 11:
	pt.paintColrGlyph.GID = getushort (pos); pos+=2;
	break;
      case 12:
      case 13:
	pt.paintTransform.isVariable = (format == 13);
	pt.paintTransform.paintOffset = get3bytes (pos); pos+=3;
	pt.paintTransform.transformOffset = get3bytes (pos); pos+=3;
	pos = off + pt.paintTransform.transformOffset;
	for (size_t i=0; i<6; i++) {
	    pt.paintTransform.transform[i] = getfixed (pos); pos+=4;
	}
	std::cerr << "transform " << off << '+' << pt.paintTransform.transformOffset << ' ';
        for (double a: pt.paintTransform.transform)
	    std::cerr << a << ' ';
        std::cerr << std::endl;
	if (format == 13) {
	    pt.paintTransform.varIndexBase = getlong (pos); pos+=4;
	}
	pt.paintTransform.paintTable = readPaintTable (off + pt.paintTransform.paintOffset, ptab_map, cline_map);
	break;
      case 14:
      case 15:
	pt.paintTransform.isVariable = (format == 15);
	pt.paintTransform.paintOffset = get3bytes (pos); pos+=3;
	pt.paintTransform.transform[4] = static_cast<short> (getushort (pos)); pos+=2;
	pt.paintTransform.transform[5] = static_cast<short> (getushort (pos)); pos+=2;
	if (format == 15) {
	    pt.paintTransform.varIndexBase = getlong (pos); pos+=4;
	}
	pt.paintTransform.paintTable = readPaintTable (off + pt.paintTransform.paintOffset, ptab_map, cline_map);
	break;
      case 16:
      case 17:
      case 18:
      case 19:
	pt.paintTransform.isVariable = (format == 17 || format == 19);
	pt.paintTransform.paintOffset = get3bytes (pos); pos+=3;
	pt.paintTransform.transform[0] = get2dot14 (pos); pos+=2;
	pt.paintTransform.transform[3] = get2dot14 (pos); pos+=2;
	if (format == 18 || format == 19) {
	    int16_t centerX = getushort (pos); pos+=2;
	    int16_t centerY = getushort (pos); pos+=2;
	    pt.paintTransform.transform[4] = centerX*pt.paintTransform.transform[0] - centerX;
	    pt.paintTransform.transform[5] = centerY*pt.paintTransform.transform[3] - centerY;
	}
	if (format == 17 || format == 19) {
	    pt.paintTransform.varIndexBase = getlong (pos); pos+=4;
	}
	pt.paintTransform.paintTable = readPaintTable (off + pt.paintTransform.paintOffset, ptab_map, cline_map);
	break;
      case 20:
      case 21:
      case 22:
      case 23:
	pt.paintTransform.isVariable = (format == 21 || format == 23);
	pt.paintTransform.paintOffset = get3bytes (pos); pos+=3;
	pt.paintTransform.transform[0] = get2dot14 (pos); pos+=2;
	pt.paintTransform.transform[3] = pt.paintTransform.transform[0];
	if (format == 22 || format == 23) {
	    int16_t centerX = getushort (pos); pos+=2;
	    int16_t centerY = getushort (pos); pos+=2;
	    pt.paintTransform.transform[4] = centerX - pt.paintTransform.transform[0]*centerX;
	    pt.paintTransform.transform[5] = centerY - pt.paintTransform.transform[3]*centerY;
	}
	if (format == 21 || format == 23) {
	    pt.paintTransform.varIndexBase = getlong (pos); pos+=4;
	}
	pt.paintTransform.paintTable = readPaintTable (off + pt.paintTransform.paintOffset, ptab_map, cline_map);
	break;
      case 24:
      case 25:
      case 26:
      case 27: {
	pt.paintTransform.isVariable = (format == 25 || format == 27);
	pt.paintTransform.paintOffset = get3bytes (pos); pos+=3;
	auto &trans = pt.paintTransform.transform;
	double angle = get2dot14 (pos); pos+=2;
	double ar = angle*M_PI/180;
        trans[0] = trans[3] = cos (ar);
        trans[1] = sin (ar);
        trans[2] = -trans[1];
	if (format == 26 || format == 27) {
	    int16_t centerX = getushort (pos); pos+=2;
	    int16_t centerY = getushort (pos); pos+=2;
	    trans[4] = centerX - trans[0]*centerX - trans[2]*centerY;
	    trans[5] = centerY - trans[1]*centerX - trans[3]*centerY;
	}
	if (format == 25 || format == 27) {
	    pt.paintTransform.varIndexBase = getlong (pos); pos+=4;
	}
	pt.paintTransform.paintTable = readPaintTable (off + pt.paintTransform.paintOffset, ptab_map, cline_map);
	} break;
      case 28:
      case 29:
      case 30:
      case 31: {
	pt.paintTransform.isVariable = (format == 29 || format == 31);
	pt.paintTransform.paintOffset = get3bytes (pos); pos+=3;
	auto &trans = pt.paintTransform.transform;
	double xangle = get2dot14 (pos); pos+=2;
	double yangle = get2dot14 (pos); pos+=2;
	double xar = xangle*M_PI/180;
	double yar = yangle*M_PI/180;
	trans[2] = tan (xar);
	trans[1] = tan (yar);
	if (format == 30 || format == 31) {
	    int16_t centerX = getushort (pos); pos+=2;
	    int16_t centerY = getushort (pos); pos+=2;
	    trans[4] = centerX - trans[0]*centerX - trans[2]*centerY;
	    trans[5] = centerY - trans[1]*centerX - trans[3]*centerY;
	}
	if (format == 29 || format == 31) {
	    pt.paintTransform.varIndexBase = getlong (pos); pos+=4;
	}
	pt.paintTransform.paintTable = readPaintTable (off + pt.paintTransform.paintOffset, ptab_map, cline_map);
	} break;
      case 32:
	pt.paintComposite.sourcePaintOffset = get3bytes (pos); pos+=3;
	pt.paintComposite.compositeMode = data[pos]; pos++;
	pt.paintComposite.backdropPaintOffset = get3bytes (pos); pos+=3;
	pt.paintComposite.sourcePaintTable = readPaintTable
	    (off + pt.paintComposite.sourcePaintOffset, ptab_map, cline_map);
	pt.paintComposite.backdropPaintTable = readPaintTable
	    (off + pt.paintComposite.backdropPaintOffset, ptab_map, cline_map);
    }
    return ptptr;
}

uint16_t ColrTable::numGlyphLayers (uint16_t gid) {
    for (uint16_t i=0; i<m_baseGlyphRecords.size (); i++) {
        if (m_baseGlyphRecords[i].GID == gid)
	    return m_baseGlyphRecords[i].numLayers;
    }
    return 0;
}

std::vector<struct layer_record> &ColrTable::glyphLayers (uint16_t gid) {
    for (uint16_t i=0; i<m_baseGlyphRecords.size (); i++) {
        if (m_baseGlyphRecords[i].GID == gid)
	    return m_baseGlyphRecords[i].layers;
    }
    throw std::exception ();
}

// NB: same function as for GlyphContainer
void ColrTable::edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller) {
    // No fillup here, as it is done by fontview
    if (!tv) {
        FontView *fv = new FontView (tptr, fnt, caller);
        if (!fv->isValid ()) {
            fv->close ();
            return;
        }
        tv = fv;
        fv->show ();
    } else {
        tv->raise ();
    }
}

void ColrTable::packData () {
}

void ColrTable::appendPaintTableToGlyph (ConicGlyph *g, PaintTable *ptab, OutlinesType otype, Drawable *parent) {
    switch (ptab->format) {
      case 1: {
	size_t start_idx = ptab->paintColrLayers.firstLayerIndex;
	size_t end_idx = start_idx + ptab->paintColrLayers.numLayers;
	for (size_t i=start_idx; i<end_idx; i++) {
	    PaintTable *child = m_layerList[i].get ();
	    appendPaintTableToGlyph (g, child, otype, nullptr);
	}
      } break;
      case 2:
      case 3: {
	if (parent) {
	    uint16_t color_idx = ptab->paintSolid.paletteIndex;
	    if (m_cpal && color_idx != 0xFFFF) {
		struct cpal_palette *pal = m_cpal->palette (0);
		SvgState &state = parent->svgState;
		state.fill = pal->color_records[color_idx];
		state.fill.alpha *= ptab->paintSolid.alpha;
		state.fill_idx = color_idx;
		state.fill_set = true;
	    }
	}
      } break;
      case 4:
      case 5: {
	if (parent) {
	    std::string grad_id = ptab->paintLinearGradient.id;
	    if (!g->gradients.count (grad_id)) {
		g->gradients.emplace (std::make_pair (
		    grad_id, Gradient (ptab->paintLinearGradient.colorLine.get (), m_cpal, 0)));
		Gradient &grad = g->gradients[grad_id];
		grad.type = GradientType::LINEAR;
		grad.props["x1"] = ptab->paintLinearGradient.x0;
		grad.props["y1"] = ptab->paintLinearGradient.y0;
		grad.props["x2"] = ptab->paintLinearGradient.x1;
		grad.props["y2"] = ptab->paintLinearGradient.y1;
	    }
	    SvgState &state = parent->svgState;
	    state.fill_source_id = grad_id;
	    state.fill_set = true;
	}
      } break;
      case 6:
      case 7:  {
	if (parent) {
	    std::string grad_id = ptab->paintRadialGradient.id;
	    if (!g->gradients.count (grad_id)) {
		g->gradients.emplace (std::make_pair (
		    grad_id, Gradient (ptab->paintRadialGradient.colorLine.get (), m_cpal, 0)));
		Gradient &grad = g->gradients[grad_id];
		grad.type = GradientType::RADIAL;
		grad.props["cx"] = ptab->paintRadialGradient.x0;
		grad.props["cy"] = ptab->paintRadialGradient.y0;
		grad.props["fx"] = ptab->paintRadialGradient.x1;
		grad.props["fy"] = ptab->paintRadialGradient.y1;
		grad.props["r"] = ptab->paintRadialGradient.radius0;
	    }
	    SvgState &state = parent->svgState;
	    state.fill_source_id = grad_id;
	    state.fill_set = true;
	}
      } break;
      case 8:
      case 9: {
	if (parent) {
	    std::string grad_id = ptab->paintSweepGradient.id;
	    if (!g->gradients.count (grad_id)) {
		g->gradients.emplace (std::make_pair (
		    grad_id, Gradient (ptab->paintSweepGradient.colorLine.get (), m_cpal, 0)));
		Gradient &grad = g->gradients[grad_id];
		grad.type = GradientType::RADIAL;
		grad.props["cx"] = ptab->paintSweepGradient.centerX;
		grad.props["cy"] = ptab->paintSweepGradient.centerY;
		grad.props["a1"] = 180*ptab->paintSweepGradient.startAngle;
		grad.props["a2"] = 180*ptab->paintSweepGradient.endAngle;
	    }
	    SvgState &state = parent->svgState;
	    state.fill_source_id = grad_id;
	    state.fill_set = true;
	}
      } break;
      case 10: {
	DrawableReference *ref = dynamic_cast<DrawableReference *> (parent);
	if (!ref) {
	    g->refs.emplace_back ();
	    ref = &g->refs.back ();
	}
	ref->GID = ptab->paintGlyph.GID;
	ref->outType = otype;
	appendPaintTableToGlyph (g, ptab->paintGlyph.paintTable.get (), otype, ref);
      } break;
      case 11: {
	g->refs.emplace_back ();
	auto &ref = g->refs.back ();
	ref.GID = ptab->paintGlyph.GID;
	ref.outType = OutlinesType::COLR;
      } break;
      case 12:
      case 13:
      case 14:
      case 15:
      case 16:
      case 17:
      case 18:
      case 19:
      case 20:
      case 21:
      case 22:
      case 23:
      case 24:
      case 25:
      case 26:
      case 27:
      case 28:
      case 29:
      case 30: {
	g->refs.emplace_back ();
	auto &ref = g->refs.back ();
	ref.transform = ptab->paintTransform.transform;
	ref.GID = 0;
	appendPaintTableToGlyph (g, ptab->paintTransform.paintTable.get (), otype, &ref);
      } break;
      case 31:
	appendPaintTableToGlyph (g, ptab->paintComposite.sourcePaintTable.get (), otype, parent);
	appendPaintTableToGlyph (g, ptab->paintComposite.backdropPaintTable.get (), otype, parent);
    }
}

ConicGlyph* ColrTable::glyph (sFont* fnt, uint16_t gid) {
    if (!usable () || gid >= m_glyphs.size ())
	return nullptr;
    if (m_glyphs[gid])
        return m_glyphs[gid];
    struct base_glyph_record *grptr = nullptr;
    struct BaseGlyphPaintRecord *paintptr = nullptr;

    for (auto &grec : m_baseGlyphRecords) {
        if (grec.GID == gid) {
	    grptr = &grec;
	    break;
	}
    }
    for (auto &paintrec : m_baseGlyphList) {
        if (paintrec.GID == gid) {
	    paintptr = &paintrec;
	    break;
	}
    }
    if (!grptr && !paintptr)
	return nullptr;
    OutlinesType otype;
    if (fnt->table (CHR ('g','l','y','f')))
	otype = OutlinesType::TT;
    else if (fnt->table (CHR ('C','F','F',' ')) || fnt->table (CHR ('C','F','F','2')))
	otype = OutlinesType::PS;

    BaseMetrics gm = {fnt->units_per_em, fnt->ascent, fnt->descent};
    ConicGlyph *g = glyph_pool.construct (gid, gm);
    g->setOutlinesType (OutlinesType::COLR);
    m_glyphs[gid] = g;
    if (m_hmtx)
        g->setHMetrics (m_hmtx->lsb (gid), m_hmtx->aw (gid));

    if (grptr) {
	for (auto layer : grptr->layers) {
	    g->refs.emplace_back ();
	    auto &ref = g->refs.back ();
	    ref.outType = otype;
	    ref.GID = layer.GID;
	    ref.transform = { 1, 0, 0, 1, 0, 0 };
	    uint16_t palidx = layer.paletteIndex;

	    if (m_cpal && palidx != 0xFFFF) {
		struct cpal_palette *pal = m_cpal->palette (0);
		ref.svgState.fill = pal->color_records[layer.paletteIndex];
		ref.svgState.fill_idx = layer.paletteIndex;
		ref.svgState.fill_set = true;
	    }
	}
    } else if (paintptr) {
	appendPaintTableToGlyph (g, paintptr->paintTable.get (), otype, nullptr);
	for (auto &cr : m_clipRecords) {
	    if (gid >= cr.startGlyphID && gid <= cr.endGlyphID) {
		g->clipBox = { cr.xMin, cr.xMax, cr.yMin, cr.yMax };
		break;
	    }
	}
    }

    g->setModified (false);
    return g;
}

uint16_t ColrTable::addGlyph (sFont* fnt, uint8_t) {
    return 0xFFFF;
}

bool ColrTable::usable () const {
    return td_loaded;
}

CpalTable::CpalTable (sfntFile *fontfile, TableHeader &props) :
    FontTable (fontfile, props) {
}

CpalTable::~CpalTable () {
}

void CpalTable::edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller) {
    if (data == nullptr)
        fillup ();

    if (tv == nullptr) {
	unpackData (fnt);
        CpalEdit *cpaledit = new CpalEdit (tptr, fnt, caller);
        tv = cpaledit;
        cpaledit->show ();
    } else {
        tv->raise ();
    }
}

void CpalTable::unpackData (sFont *) {
    uint32_t pos = 0;
    uint16_t i;
    std::vector<uint16_t> firstColorIndices;
    std::vector<rgba_color> colorRecords;
    uint32_t offsetPaletteTypeArray, offsetPaletteLabelArray, offsetPaletteEntryLabelArray;

    m_paletteLabelIndices.resize (0);
    m_paletteList.resize (0);

    m_version = getushort (0);
    m_numPaletteEntries = getushort (2);
    uint16_t numPalettes = getushort (4);
    uint16_t numColorRecords = getushort (6);
    uint32_t offsetFirstColorRecord = getlong (8);

    pos = 12;
    m_paletteList.reserve (numPalettes);
    firstColorIndices.resize (numPalettes);

    for (i=0; i<numPalettes; i++) {
        firstColorIndices[i] = getushort (pos);
        pos += 2;
    }
    if (m_version > 0) {
	offsetPaletteTypeArray = getlong (pos); pos+= 4;
	offsetPaletteLabelArray = getlong (pos); pos+= 4;
	offsetPaletteEntryLabelArray = getlong (pos); pos+= 4;
    }

    pos = offsetFirstColorRecord;
    colorRecords.resize (numColorRecords);
    for (i=0; i<numColorRecords; i++) {
        colorRecords[i].blue = data[pos]; pos++;
        colorRecords[i].green = data[pos]; pos++;
        colorRecords[i].red = data[pos]; pos++;
        colorRecords[i].alpha = data[pos]; pos++;
    }

    m_paletteLabelIndices.resize (m_numPaletteEntries);
    for (i=0; i<m_numPaletteEntries; i++)
	m_paletteLabelIndices[i] = 0xFFFF;

    for (i=0; i<numPalettes; i++) {
	auto pal = std::unique_ptr<cpal_palette> (new cpal_palette ());
	pal->color_records.resize (m_numPaletteEntries);
	uint16_t color_idx=firstColorIndices[i];
	for (uint16_t j=0; j<m_numPaletteEntries; j++) {
	    pal->color_records[j] = colorRecords[color_idx];
	    color_idx++;
	}
	m_paletteList.push_back (std::move (pal));
    }

    if (m_version > 0) {
	if (offsetPaletteTypeArray) {
	    pos = offsetPaletteTypeArray;
	    for (i=0; i<numPalettes; i++) {
		m_paletteList[i]->flags = getlong (pos);
		pos+= 4;
	    }
	}
	if (offsetPaletteLabelArray) {
	    pos = offsetPaletteLabelArray;
	    for (i=0; i<numPalettes; i++) {
		m_paletteList[i]->label_idx = getushort (pos);
		pos+= 2;
	    }
	}
	if (offsetPaletteLabelArray) {
	    pos = offsetPaletteEntryLabelArray;
	    for (i=0; i<m_numPaletteEntries; i++) {
		m_paletteLabelIndices[i] = getushort (pos);
		pos+= 2;
	    }
	}
    }
}

void CpalTable::packData () {
    std::ostringstream os;
    uint32_t cr_off, type_off, plbl_off, elbl_off;

    delete[] data; data = nullptr;
    putushort (os, m_version);
    putushort (os, m_numPaletteEntries);
    putushort (os, m_paletteList.size ());
    putushort (os, m_paletteList.size ()*m_numPaletteEntries);
    putlong (os, 0); // offsetFirstColorRecord
    for (uint16_t i=0; i<m_paletteList.size (); i++)
	putushort (os, i*m_numPaletteEntries);
    if (m_version > 0) {
	putlong (os, 0); // offsetPaletteTypeArray
	putlong (os, 0); // offsetPaletteLabelArray
	putlong (os, 0); // offsetPaletteEntryLabelArray
    }
    cr_off = os.tellp ();
    os.seekp (8);
    putlong (os, cr_off);
    os.seekp (cr_off);
    for (size_t i=0; i<m_paletteList.size (); i++) {
	for (uint16_t j=0; j<m_numPaletteEntries; j++) {
	    rgba_color &rec = m_paletteList[i]->color_records[j];
	    os.put (rec.blue);
	    os.put (rec.green);
	    os.put (rec.red);
	    os.put (rec.alpha);
	}
    }
    if (m_version > 0) {
	type_off = os.tellp ();
	for (size_t i=0; i<m_paletteList.size (); i++)
	    putlong (os, m_paletteList[i]->flags.to_ulong ());
	plbl_off = os.tellp ();
	for (size_t i=0; i<m_paletteList.size (); i++)
	    putushort (os, m_paletteList[i]->label_idx);
	elbl_off = os.tellp ();
	for (size_t i=0; i<m_numPaletteEntries; i++)
	    putushort (os, m_paletteLabelIndices[i]);
	os.seekp (cr_off-12);
	putlong (os, type_off);
	putlong (os, plbl_off);
	putlong (os, elbl_off);
    }

    changed = false;
    td_changed = true;
    start = 0xffffffff;

    std::string st = os.str ();
    newlen = st.length ();
    data = new char[newlen];
    std::copy (st.begin (), st.end (), data);
}

uint16_t CpalTable::version () const {
    return m_version;
}

uint16_t CpalTable::numPalettes () const {
    return m_paletteList.size ();
}

void CpalTable::setNumPalettes (uint16_t val) {
    if (val < m_paletteList.size ())
	m_paletteList.resize (val);
    else {
	uint16_t old_cnt = m_paletteList.size ();
        for (uint16_t i = old_cnt; i<val; i++) {
	    auto pal = std::unique_ptr<cpal_palette> (new cpal_palette ());
	    pal->color_records.resize (m_numPaletteEntries);
	    m_paletteList.push_back (std::move (pal));
        }
    }
}

uint16_t CpalTable::numPaletteEntries () const {
    return m_numPaletteEntries;
}

uint16_t CpalTable::paletteNameID (uint16_t idx) const {
    if (idx < m_paletteList.size ())
	return m_paletteList[idx]->label_idx;
    return 0xFFFF;
}

uint16_t CpalTable::colorNameID (uint16_t idx) const {
    if (idx < m_numPaletteEntries)
	return m_paletteLabelIndices[idx];
    return 0xFFFF;
}

struct cpal_palette *CpalTable::palette (uint16_t idx) {
    if (idx < m_paletteList.size ())
	return m_paletteList[idx].get ();
    return nullptr;
}

QStringList CpalTable::paletteList (NameTable *name) {
    QStringList ret;
    ret.reserve (m_paletteList.size ());
    for (uint16_t i=0; i<m_paletteList.size (); i++) {
	QString user_name = "Palette";
	if (m_version > 0 && name)
	    user_name = name->bestName (m_paletteList[i]->label_idx, "Palette");
	ret.push_back (QString (
	    QApplication::tr ("%1: %2")).arg (i).arg (user_name));
    }
    return ret;
}

PaintTable::PaintTable (uint8_t fmt) : format (fmt) {
    switch (format) {
      case 1:
	new (&paintColrLayers) PaintColrLayers ();
	break;
      case 2:
	new (&paintSolid) PaintSolid ();
	break;
      case 4:
      case 5:
	new (&paintLinearGradient) PaintLinearGradient ();
	break;
      case 6:
      case 7:
	new (&paintRadialGradient) PaintRadialGradient ();
	break;
      case 8:
      case 9:
	new (&paintSweepGradient) PaintSweepGradient ();
	break;
      case 10:
        new (&paintGlyph) PaintGlyph ();
	break;
      case 11:
        new (&paintColrGlyph) PaintColrGlyph ();
	break;
      case 32:
        new (&paintComposite) PaintComposite ();
	break;
      default:
        new (&paintTransform) PaintTransform ();
    }
}

PaintTable::~PaintTable () {
    switch (format) {
      case 1:
	paintColrLayers.~PaintColrLayers ();
	break;
      case 2:
      case 3:
	paintSolid.~PaintSolid ();
	break;
      case 4:
      case 5:
        paintLinearGradient.~PaintLinearGradient ();
	break;
      case 6:
      case 7:
        paintRadialGradient.~PaintRadialGradient ();
	break;
      case 8:
      case 9:
        paintSweepGradient.~PaintSweepGradient ();
	break;
      case 10:
        paintGlyph.~PaintGlyph ();
	break;
      case 11:
        paintColrGlyph.~PaintColrGlyph ();
	break;
      case 32:
        paintComposite.~PaintComposite ();
	break;
      default:
        paintTransform.~PaintTransform ();
    }
}

Gradient::Gradient (ColorLine *cline, CpalTable *cpal, uint16_t palidx) {
    sm = static_cast<GradientExtend> (cline->extend);
    units = GradientUnits::userSpaceOnUse;
    for (auto &lstop : cline->colorStops) {
	stops.emplace_back ();
	auto &stop = stops.back ();
	stop.color_idx = lstop.paletteIndex;
	stop.offset = lstop.stopOffset;

	if (cpal && palidx != 0xFFFF) {
	    struct cpal_palette *pal = cpal->palette (palidx);
	    stop.color = pal->color_records[stop.color_idx];
	    stop.color.alpha *= lstop.alpha;
        }
    }
}

void Gradient::transformProps (const std::array<double, 6> &trans) {
    double minx = bbox.minx, maxx = bbox.maxx;
    double miny = bbox.miny, maxy = bbox.maxy;
    bbox.minx = trans[0]*minx + trans[2]*miny + trans[4];
    bbox.miny = trans[1]*minx + trans[3]*miny + trans[5];
    bbox.maxx = trans[0]*maxx + trans[2]*maxy + trans[4];
    bbox.maxy = trans[1]*maxx + trans[3]*maxy + trans[5];
    if (bbox.minx > bbox.maxx) std::swap (bbox.minx, bbox.maxx);
    if (bbox.miny > bbox.maxy) std::swap (bbox.miny, bbox.maxy);
    for (auto &pair : props) {
	std::string key = pair.first;
	double val = pair.second;
	if (key == "x1") {
	    double x1 = val;
	    double y1 = props.count ("y1") ? props["y1"] : 0;
	    props["x1"] = trans[0]*x1 + trans[2]*y1 + trans[4];
	} else if (key == "y1") {
	    double x1 = props.count ("x1") ? props["x1"] : 0;
	    double y1 = val;
	    props["y1"] = trans[1]*x1 + trans[3]*y1 + trans[5];
	} else if (key == "x2") {
	    double x2 = val;
	    double y2 = props.count ("y2") ? props["y2"] : 0;
	    props["x2"] = trans[0]*x2 + trans[2]*y2 + trans[4];
	} else if (key == "y2") {
	    double x2 = props.count ("x2") ? props["x2"] : 0;;
	    double y2 = val;
	    props["y2"] = trans[1]*x2 + trans[3]*y2 + trans[5];
	} else if (key == "cx") {
	    double cx = val;
	    double cy = props["cy"];
	    props["cx"] = trans[0]*cx + trans[2]*cy + trans[4];
	    props["cy"] = trans[1]*cx + trans[3]*cy + trans[5];
	} else if (key == "fx") {
	    double fx = val;
	    double fy = props["fy"];
	    props["fx"] = trans[0]*fx + trans[2]*fy + trans[4];
	    props["fy"] = trans[1]*fx + trans[3]*fy + trans[5];
	} else if (key == "r") {
	    double r = val;
	    props["r"] = (std::abs (trans[0]*r) + std::abs (trans[3]*r))/2;
	}
    }
}

void Gradient::convertBoundingBox (DBounds &bb) {
    static std::set<std::string> x_coord = { "x1", "x2", "cx", "fx" };
    static std::set<std::string> y_coord = { "y1", "y2", "cy", "fy" };
    if (units == GradientUnits::objectBoundingBox)
	return;
    for (auto &pair : props) {
	std::string key = pair.first;
	double &val = pair.second;
	if (x_coord.count (key))
	    val = (val - bb.minx) / (bb.maxx-bb.minx);
	else if (y_coord.count (key))
	    val = (val - bb.miny) / (bb.maxy-bb.miny);
	else if (key == "r") {
	    double offx = (bb.maxx-bb.minx)/2;
	    double offy = (bb.maxy-bb.miny)/2;
	    if (offx || offy)
		val /= sqrt (4*(offx*offx + offy*offy));
	}
    }
    units = GradientUnits::objectBoundingBox;
    this->bbox = bb;
}
