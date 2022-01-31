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

class FontTable;
class NameTable;

struct layer_record {
    uint16_t GID, paletteIndex;
};

struct base_glyph_record {
    uint16_t GID, firstLayerIndex, numLayers;
    std::vector<struct layer_record> layers;
};

#ifndef _FS_STRUCT_RGBA_COLOR_DEFINED
#define _FS_STRUCT_RGBA_COLOR_DEFINED
struct rgba_color {
    unsigned char red=0, green=0, blue=0, alpha=255;
};
#endif

class ColrTable : public FontTable {
public:
    ColrTable (sfntFile* fontfile, TableHeader &props);
    ~ColrTable ();
    void unpackData (sFont *font);
    void edit (sFont* fnt, QWidget* caller);

    std::vector<struct layer_record> &glyphLayers (uint16_t gid);
    uint16_t numGlyphLayers (uint16_t gid);

private:
    uint16_t m_version;
    std::vector<struct base_glyph_record> m_baseGlyphRecords;
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
    void edit (sFont* fnt, QWidget* caller);
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
