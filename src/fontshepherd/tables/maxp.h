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
#include <ctime>

class ConicGlyph;

struct maxp_data {
    double version;
    uint16_t numGlyphs = 0;
    uint16_t maxPoints = 0;
    uint16_t maxContours = 0;
    uint16_t maxCompositePoints = 0;
    uint16_t maxCompositeContours = 0;
    uint16_t maxZones = 0;
    uint16_t maxTwilightPoints = 0;
    uint16_t maxStorage = 0;
    uint16_t maxFunctionDefs = 0;
    uint16_t maxInstructionDefs = 0;
    uint16_t maxStackElements = 0;
    uint16_t maxSizeOfInstructions = 0;
    uint16_t maxComponentElements = 0;
    uint16_t maxComponentDepth = 0;
};

class FontTable;
class MaxpEdit;

class MaxpTable : public FontTable {
    friend class ConicGlyph;
    friend class MaxpEdit;
public:
    MaxpTable (sfntFile* fontfile, TableHeader &props);
    MaxpTable (MaxpTable* source);
    ~MaxpTable () {};
    void unpackData (sFont *font);
    void packData ();
    void edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller);

    void setGlyphCount (uint16_t cnt);

    double version () const;
    uint16_t numGlyphs () const;
    uint16_t maxPoints () const;
    uint16_t maxContours () const;
    uint16_t maxCompositePoints () const;
    uint16_t maxCompositeContours () const;
    uint16_t maxZones () const;
    uint16_t maxTwilightPoints () const;
    uint16_t maxStorage () const;
    uint16_t maxFunctionDefs () const;
    uint16_t maxInstructionDefs () const;
    uint16_t maxStackElements () const;
    uint16_t maxSizeOfInstructions () const;
    uint16_t maxComponentElements () const;
    uint16_t maxComponentDepth () const;

private:
    struct maxp_data contents;
};
