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

#ifndef _FONSHEPHERD_GLYPH_NAMES_H
#define _FONSHEPHERD_GLYPH_NAMES_H
class CffTable;
struct post_data {
    double version;
    double italicAngle;
    int16_t underlinePosition, underlineThickness;
    uint32_t isFixedPitch;
    uint32_t minMemType42, maxMemType42;
    uint32_t minMemType1, maxMemType1;
    uint16_t numberOfGlyphs;
};

class CmapEnc;
class GlyphNameProvider;

class PostTable : public FontTable {
    friend class PostEdit;

public:
    PostTable (sfntFile* fontfile, const TableHeader &props);
    ~PostTable ();
    void unpackData (sFont *font);
    void packData ();
    void edit (sFont* fnt, QWidget* caller);
    std::string glyphName (uint16_t gid);

    double version () const;
    double setVersion (double val, GlyphNameProvider *gnp);
    double italicAngle () const;
    int16_t underlinePosition () const;
    int16_t underlineThickness () const;
    bool isFixedPitch () const;
    uint32_t minMemType42 () const;
    uint32_t maxMemType42 () const;
    uint32_t minMemType1 () const;
    uint32_t maxMemType1 () const;
    uint16_t numberOfGlyphs () const;
    void setGlyphName (uint16_t gid, const std::string &name);

private:
    static std::array<std::string, 258> macRomanNames;
    struct post_data contents;

    std::vector<std::string> m_glyphNames;
};

class GlyphNameProvider {
public:
    GlyphNameProvider (sFont &fnt);
    ~GlyphNameProvider ();

    std::string nameByGid (uint16_t gid);
    std::string nameByUni (uint32_t uni);
    uint32_t uniByName (std::string &name);
    bool fontHasGlyphNames ();
    uint16_t countGlyphs ();
    uint32_t glyphNameSource ();
    CmapEnc *encoding ();
    void setGlyphName (uint16_t gid, const std::string &name);
    sFont &font () const;

private:
    void parseAglfn (std::string &path);
    void parseGlyphlist (std::string &path);

    sFont &m_font;
    PostTable *m_post;
    CffTable *m_cff;
    CmapEnc *m_enc;
    std::map<uint32_t, std::string> by_uni;
    std::map<std::string, uint32_t> by_name;
};
#endif
