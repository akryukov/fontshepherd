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

enum class GaspFlags {
    GRIDFIT = 1, DOGRAY = 2, SYMMETRIC_GRIDFIT = 4, SYMMETRIC_SMOOTHING = 8
};

struct GaspRange {
    uint16_t rangeMaxPPEM;
    uint16_t rangeGaspBehavior;
};

struct gasp_data {
    double version;
    std::vector<struct GaspRange> ranges;
};

class FontTable;

class GaspTable : public FontTable {
    friend class GaspEdit;
public:
    GaspTable (sfntFile* fontfile, TableHeader &props);
    ~GaspTable () {};
    void unpackData (sFont *font);
    void packData ();
    void edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller);

    uint16_t version () const;
    uint16_t numRanges () const;
    uint16_t maxPPEM (uint16_t idx) const;
    uint16_t gaspBehavior (uint16_t idx) const;

    void setVersion (uint16_t ver);

private:
    struct gasp_data contents;
};
