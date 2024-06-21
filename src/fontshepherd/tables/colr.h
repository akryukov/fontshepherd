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

#include <QtGlobal>
#include <bitset>

#include "colors.h"
#include "tables/glyphcontainer.h"
#include "tables/variations.h"

class FontTable;
class NameTable;
class Drawable;

struct layer_record {
    uint16_t GID, paletteIndex;
};

struct base_glyph_record {
    uint16_t GID, firstLayerIndex, numLayers;
    std::vector<struct layer_record> layers;
};

struct PaintColrLayers {
    uint8_t numLayers;
    uint32_t firstLayerIndex;
};

struct PaintSolid {
    bool isVariable;
    uint16_t paletteIndex;
    double alpha;
    uint32_t varIndexBase;
};

struct PaintLinearGradient {
    bool isVariable;
    uint32_t colorLineOffset;
    int16_t x0, y0, x1, y1, x2, y2;
    uint32_t varIndexBase;
    std::shared_ptr<ColorLine> colorLine;
    std::string id;
};

struct PaintRadialGradient {
    bool isVariable;
    uint32_t colorLineOffset;
    int16_t x0, y0, x1, y1;
    uint16_t radius0, radius1;
    uint32_t varIndexBase;
    std::shared_ptr<ColorLine> colorLine;
    std::string id;
};

struct PaintSweepGradient {
    bool isVariable;
    uint32_t colorLineOffset;
    int16_t centerX, centerY;
    double startAngle, endAngle;
    uint32_t varIndexBase;
    std::shared_ptr<ColorLine> colorLine;
    std::string id;
};

struct PaintTable;

struct PaintGlyph {
    uint32_t paintOffset;
    int16_t GID;
    std::shared_ptr<PaintTable> paintTable;
};

struct PaintColrGlyph {
    int16_t GID;
};

struct PaintTransform {
    bool isVariable;
    uint32_t paintOffset, transformOffset;
    std::array<double, 6> transform = { 1, 0, 0, 1, 0, 0 };
    uint32_t varIndexBase;
    std::shared_ptr<PaintTable> paintTable;
};

namespace CompositeMode {
    enum CompositeMode {
	COMPOSITE_CLEAR = 0,
	COMPOSITE_SRC = 1,
	COMPOSITE_DEST = 2,
	COMPOSITE_SRC_OVER = 3,
	COMPOSITE_DEST_OVER = 4,
	COMPOSITE_SRC_IN = 5,
	COMPOSITE_DEST_IN = 6,
	COMPOSITE_SRC_OUT = 7,
	COMPOSITE_DEST_OUT = 8,
	COMPOSITE_SRC_ATOP = 9,
	COMPOSITE_DEST_ATOP = 10,
	COMPOSITE_XOR = 11,
	COMPOSITE_PLUS = 12,
	// Separable color blend modes:
	COMPOSITE_SCREEN = 13,
	COMPOSITE_OVERLAY = 14,
	COMPOSITE_DARKEN = 15,
	COMPOSITE_LIGHTEN = 16,
	COMPOSITE_COLOR_DODGE = 17,
	COMPOSITE_COLOR_BURN = 18,
	COMPOSITE_HARD_LIGHT = 19,
	COMPOSITE_SOFT_LIGHT = 20,
	COMPOSITE_DIFFERENCE = 21,
	COMPOSITE_EXCLUSION = 22,
	COMPOSITE_MULTIPLY = 23,
	// Non-separable color blend modes:
	COMPOSITE_HSL_HUE = 24,
	COMPOSITE_HSL_SATURATION = 25,
	COMPOSITE_HSL_COLOR = 26,
	COMPOSITE_HSL_LUMINOSITY = 27
    };
};

struct PaintComposite {
    uint32_t sourcePaintOffset, backdropPaintOffset;
    uint8_t compositeMode;
    std::shared_ptr<PaintTable> sourcePaintTable, backdropPaintTable;
};

struct PaintTable {
    uint8_t format;
    union {
	struct PaintColrLayers paintColrLayers;
	struct PaintSolid paintSolid;
	struct PaintLinearGradient paintLinearGradient;
	struct PaintRadialGradient paintRadialGradient;
	struct PaintSweepGradient paintSweepGradient;
	struct PaintGlyph paintGlyph;
	struct PaintColrGlyph paintColrGlyph;
	struct PaintTransform paintTransform;
	struct PaintComposite paintComposite;
    };
    PaintTable (uint8_t fmt);
    ~PaintTable ();
};

struct BaseGlyphPaintRecord {
    uint16_t GID;
    uint32_t paintOffset;
    std::shared_ptr<PaintTable> paintTable;
};

struct ClipRecord {
    uint16_t startGlyphID;
    uint16_t endGlyphID;
    uint32_t clipBoxOffset;
    uint8_t clipBoxFormat;
    int16_t xMin, yMin, xMax, yMax;
    uint32_t varIndexBase;
};

class ColrTable : public GlyphContainer {
public:
    ColrTable (sfntFile* fontfile, TableHeader &props);
    ~ColrTable ();
    void unpackData (sFont *font);
    void edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller);

    std::vector<struct layer_record> &glyphLayers (uint16_t gid);
    uint16_t numGlyphLayers (uint16_t gid);

    void packData ();
    ConicGlyph* glyph (sFont* fnt, uint16_t gid);
    uint16_t addGlyph (sFont* fnt, uint8_t subfont=0);
    bool usable () const;

private:
    std::shared_ptr<PaintTable> readPaintTable (
	uint32_t off,
	std::map<uint32_t, std::shared_ptr<PaintTable>> &ptab_map,
	std::map<uint32_t, std::shared_ptr<ColorLine>> &cline_map
    );
    std::shared_ptr<ColorLine> readColorLine (
	uint32_t off,
	std::map<uint32_t, std::shared_ptr<ColorLine>> &cline_map,
	bool var
    );
    void appendPaintTableToGlyph (ConicGlyph *g, PaintTable *ptab, OutlinesType otype, Drawable *parent);

    uint16_t m_version;
    uint32_t baseGlyphListOffset = 0;
    uint32_t layerListOffset = 0;
    uint32_t clipListOffset = 0;
    uint32_t varIndexMapOffset = 0;
    uint32_t itemVariationStoreOffset = 0;

    CpalTable *m_cpal;

    std::vector<struct base_glyph_record> m_baseGlyphRecords;
    std::vector<BaseGlyphPaintRecord> m_baseGlyphList;
    std::vector<std::shared_ptr<PaintTable>> m_layerList;
    std::vector<ClipRecord> m_clipRecords;

    struct delta_set_index_map m_deltaSetIndexMap;
    struct variation_store m_varStore;
};

struct cpal_palette {
    std::vector<rgba_color> color_records;
    uint16_t label_idx = 0xFFFF;
    std::bitset<32> flags = { 0 };
};

class CpalTable : public FontTable {
    friend class CpalEdit;
public:
    CpalTable (sfntFile* fontfile, TableHeader &props);
    ~CpalTable ();
    void unpackData (sFont *font);
    void packData ();
    void edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller);
    uint16_t version () const;
    uint16_t numPalettes () const;
    void setNumPalettes (uint16_t val);
    uint16_t numPaletteEntries () const;
    uint16_t numColorRecords () const;
    uint16_t paletteNameID (uint16_t idx) const;
    uint16_t colorNameID (uint16_t idx) const;
    struct cpal_palette *palette (uint16_t idx);
    QStringList paletteList (NameTable *name=nullptr);

private:
    uint16_t m_version, m_numPaletteEntries;
    uint32_t m_offsetFirstColorRecord;
    std::vector<std::unique_ptr<struct cpal_palette>> m_paletteList;
    std::vector<uint16_t> m_paletteLabelIndices;
};
