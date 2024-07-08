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

struct hea_data {
    double version;
    int16_t ascent;
    int16_t descent;
    int16_t lineGap;
    // advance width/height specified as uFWORD in 'hhead', but int16 in 'vhead' spec
    int advanceMax;
    int16_t minStartSideBearing;
    int16_t minEndSideBearing;
    int16_t maxExtent;
    int16_t caretSlopeRise;
    int16_t caretSlopeRun;
    int16_t caretOffset;
    int16_t reserved1;
    int16_t reserved2;
    int16_t reserved3;
    int16_t reserved4;
    int16_t metricDataFormat;
    uint16_t numOfMetrics;
};

class FontTable;
class HeaEdit;

class HeaTable : public FontTable {
    friend class HeaEdit;

public:
    HeaTable (sfntFile* fontfile, TableHeader &props);
    HeaTable (HeaTable* source);
    ~HeaTable () {};
    void unpackData (sFont *font);
    void packData ();
    void edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller) override;

    bool isVertical () const;

    double version () const;
    int16_t ascent () const;
    int16_t descent () const;
    int16_t lineGap () const;
    int advanceMax () const;
    int16_t minStartSideBearing () const;
    int16_t minEndSideBearing () const;
    int16_t maxExtent () const;
    int16_t caretSlopeRise () const;
    int16_t caretSlopeRun () const;
    int16_t caretOffset () const;
    int16_t metricDataFormat () const;
    uint16_t numOfMetrics () const;

    void setNumOfMetrics (uint16_t num);

private:
    struct hea_data contents;
};
