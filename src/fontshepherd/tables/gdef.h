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

#include "tables/variations.h"

namespace GlyphClassDef {
    enum GlyphClass {
	Base = 1, Ligature = 2, Mark = 3, Component = 4
    };
};

namespace DeltaFormat {
    enum DeltaFormat {
	LOCAL_2_BIT_DELTAS = 1,
	LOCAL_4_BIT_DELTAS = 2,
	LOCAL_8_BIT_DELTAS = 3,
	VARIATION_INDEX = 0x8000
    };
};

struct device_table {
    uint16_t deltaFormat;
    union {
	uint16_t startSize;
	uint16_t deltaSetOuterIndex;
    };
    union {
	uint16_t endSize;
	uint16_t deltaSetInnerIndex;
    };
    std::vector<uint16_t> deltaValues;
};

namespace OpenType {
    void readClassDefTable (char *data, uint16_t pos, std::vector<uint16_t> &glyph_list);
    void readCoverageTable (char *data, uint16_t pos, std::vector<uint16_t> &glyph_list);
    void readDeviceTable (char *data, uint16_t pos, device_table &dtab);
};

class ConicGlyph;

struct caret_value {
    uint16_t format;
    uint16_t point_index;
    int coord;
    uint16_t table_off;
    device_table dev_table;
};

class GdefTable : public FontTable {
    friend class ConicGlyph;
public:
    GdefTable (sfntFile* fontfile, TableHeader &props);
    ~GdefTable () {};
    void unpackData (sFont *font);
    void packData ();

    double version () const;

private:
    void readAttachList ();
    void readLigCaretList ();
    void readMarkGlyphSets ();

    double m_version;
    uint16_t glyphClassDefOffset = 0;
    uint16_t attachListOffset = 0;
    uint16_t ligCaretListOffset = 0;
    uint16_t markAttachClassDefOffset = 0;
    uint16_t markGlyphSetsDefOffset = 0;
    uint16_t itemVarStoreOffset = 0;

    std::vector<uint16_t> m_glyphClasses;
    std::map<uint16_t, std::vector<uint16_t>> m_attachList;
    std::map<uint16_t, std::vector<caret_value>> m_ligCaretList;
    std::vector<uint16_t> m_attachClasses;
    std::vector<std::vector<uint16_t>> m_markGlyphSets;
    variation_store m_varStore;
};
