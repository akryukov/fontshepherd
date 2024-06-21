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

#ifndef _FONSHEPHERD_GLYPHCONTAINER_H
#define _FONSHEPHERD_GLYPHCONTAINER_H

#include <stdint.h>

#include "splineglyph.h"

class HmtxTable;
class CmapEnc;
class GlyphContainer : public FontTable {
public:
    GlyphContainer (sfntFile* fontfile, const TableHeader &props);
    virtual ~GlyphContainer () {};

    virtual void unpackData (sFont*);
    virtual void packData () = 0;
    virtual void edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller);
    virtual ConicGlyph* glyph (sFont* fnt, uint16_t gid) = 0;
    virtual uint16_t addGlyph (sFont* fnt, uint8_t subfont=0) = 0;
    virtual bool usable () const = 0;
    uint16_t countGlyphs ();
    OutlinesType outlinesType () const;

protected:
    MaxpTable *m_maxp;
    HmtxTable *m_hmtx;
    std::vector<ConicGlyph *> m_glyphs;
    boost::object_pool<ConicGlyph> glyph_pool;
};

#endif
