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
#include <bitset>

class FontTable;
class HeadEdit;

struct head_data {
    double   version;
    double   fontRevision;
    uint32_t checkSumAdjustment;
    uint32_t magicNumber;
    std::bitset<16> flags;
    uint16_t unitsPerEm;
    time_t   created;
    time_t   modified;
    int16_t  xMin;
    int16_t  yMin;
    int16_t  xMax;
    int16_t  yMax;
    std::bitset<16> macStyle;
    uint16_t lowestRecPPEM;
    int16_t  fontDirectionHint;
    int16_t  indexToLocFormat;
    int16_t  glyphDataFormat;
};

class HeadTable : public FontTable {
    friend class HeadEdit;

public:
    HeadTable (sfntFile* fontfile, TableHeader &props);
    ~HeadTable () {};
    void unpackData (sFont *font);
    void packData ();
    void edit (sFont* fnt, QWidget* caller) override;

    double version () const;
    double fontRevision () const;
    uint32_t checkSumAdjustment () const;
    uint32_t magicNumber () const;
    bool flags (int nbit) const;
    uint16_t unitsPerEm () const;
    time_t created () const;
    time_t modified () const;
    int16_t xMin () const;
    int16_t yMin () const;
    int16_t xMax () const;
    int16_t yMax () const;
    bool macStyle (int nbit) const;
    uint16_t lowestRecPPEM () const;
    int16_t fontDirectionHint () const;
    int16_t indexToLocFormat () const;
    int16_t glyphDataFormat () const;

    void updateModified ();
    void setCheckSumAdjustment (uint32_t adj);
    void setIndexToLocFormat (bool is_long);

protected:
    struct head_data contents;

private:
    static time_t quad2date (uint32_t date1, uint32_t date2);
    static std::array<uint32_t,2> unix_to_1904 (time_t date);
};
