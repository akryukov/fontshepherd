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

#include <QtWidgets>
#include "tables.h" // Have to load it here due to inheritance from TableEdit
#include "sfnt.h"

class sfntFile;
typedef struct ttffont sFont;
class FontTable;
class GlyfTable;
class GlyphContainer;
class ConicGlyph;

class TinyFontProvider {
public:
    TinyFontProvider (sFont* font, QWidget *parent);
    ~TinyFontProvider () {};

    uint16_t appendOrReloadGlyph (uint16_t gid);
    void reloadGlyphs ();
    void compile ();

    const char *fontData () const;
    uint32_t fontDataSize () const;
    uint16_t gidCorr (uint16_t gid) const;
    bool valid () const;

private:
    void prepare ();

    bool m_valid;
    sFont* m_origFont;
    QWidget *m_widget;
    GlyfTable *m_origContainer;
    sFont m_font;
    std::map<uint16_t, uint16_t> m_gidCorr;
    QByteArray ba;
};
