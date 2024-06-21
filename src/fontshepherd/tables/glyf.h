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
class HeadTable;
class LocaTable;
class ConicGlyph;

class GlyfTable : public GlyphContainer {
public:
    GlyfTable (sfntFile* fontfile, TableHeader &props);
    ~GlyfTable ();
    void unpackData (sFont *font);
    void packData ();
    ConicGlyph* glyph (sFont* fnt, uint16_t gid);
    uint16_t addGlyph (sFont* fnt, uint8_t subfont=0);
    bool usable () const;

private:
    std::shared_ptr<LocaTable> m_loca;
};

class LocaTable : public FontTable {
public:
    LocaTable (sfntFile* fontfile, TableHeader &props);
    ~LocaTable () {};
    void unpackData (sFont *font);
    void packData ();
    uint32_t getGlyphOffset (uint16_t gid) const;
    void setGlyphOffset (uint16_t gid, uint32_t off);
    void setGlyphCount (uint16_t cnt);

private:
    uint16_t m_version;
    uint16_t m_enc_cnt;
    HeadTable *m_head;
    std::vector<uint32_t> offsets;
};
