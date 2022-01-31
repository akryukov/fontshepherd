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

class FontTable;
class CffTable;
class GlyfTable;

// Used to temporary store data prepared for output
struct svg_document_index_range {
    uint16_t startGlyphID, endGlyphID;
    uint32_t svgDocOffset, svgDocLength;
};

struct svg_document_index_entry {
    uint32_t svgDocOffset, svgDocLength;
    std::vector<uint16_t> glyphs;
    bool changed = false;
};

class SvgTable : public GlyphContainer {
public:
    SvgTable (sfntFile* fontfile, const TableHeader &props);
    ~SvgTable ();
    void unpackData (sFont *font);
    void packData ();
    std::string getSvgDocument (uint16_t gid);
    ConicGlyph* glyph (sFont* fnt, uint16_t gid);
    void addGlyphAt (sFont* fnt, uint16_t gid);
    uint16_t addGlyph (sFont* fnt, uint8_t subfont=0);
    bool hasGlyph (uint16_t gid) const;
    void clearGlyph (uint16_t gid);
    bool usable () const;

private:
    void dumpGradients (std::stringstream &ss, svg_document_index_entry &ie);
    void cleanupDocEntries ();
    CffTable *m_cff = nullptr;
    GlyfTable *m_glyf = nullptr;

    uint32_t m_version, m_offsetToSVGDocIndex;
    std::vector<struct svg_document_index_entry> m_iEntries;
    std::vector<int> m_docIdx;
};
