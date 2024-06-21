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
#include <bitset>

class FontTable;
class OS_2Edit;

struct os_2_data {
    uint16_t version;
    int16_t  xAvgCharWidth;
    uint16_t usWeightClass;
    uint16_t usWidthClass;
    std::bitset<16> fsType;
    int16_t  ySubscriptXSize;
    int16_t  ySubscriptYSize;
    int16_t  ySubscriptXOffset;
    int16_t  ySubscriptYOffset;
    int16_t  ySuperscriptXSize;
    int16_t  ySuperscriptYSize;
    int16_t  ySuperscriptXOffset;
    int16_t  ySuperscriptYOffset;
    int16_t  yStrikeoutSize;
    int16_t  yStrikeoutPosition;
    int8_t   sFamilyClass;
    int8_t   sFamilySubClass;
    std::array<uint8_t, 10> panose;
    std::bitset<32> ulUnicodeRange1;
    std::bitset<32> ulUnicodeRange2;
    std::bitset<32> ulUnicodeRange3;
    std::bitset<32> ulUnicodeRange4;
    std::array<char, 4> achVendID;
    std::bitset<16> fsSelection;
    uint16_t usFirstCharIndex;
    uint16_t usLastCharIndex;
    int16_t  sTypoAscender;
    int16_t  sTypoDescender;
    int16_t  sTypoLineGap;
    uint16_t usWinAscent;
    uint16_t usWinDescent;
    std::bitset<32> ulCodePageRange1;
    std::bitset<32> ulCodePageRange2;
    int16_t  sxHeight;
    int16_t  sCapHeight;
    uint16_t usDefaultChar;
    uint16_t usBreakChar;
    uint16_t usMaxContext;
    uint16_t usLowerOpticalPointSize;
    uint16_t usUpperOpticalPointSize;
};

class OS_2Table : public FontTable {
    friend class OS_2Edit;

public:
    OS_2Table (sfntFile* fontfile, TableHeader &props);
    ~OS_2Table () {};
    void unpackData (sFont *font);
    void packData ();
    void edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller) override;

    uint16_t version () const;
    int16_t  xAvgCharWidth () const;
    uint16_t usWeightClass () const;
    uint16_t usWidthClass () const;
    bool fsType (int nbit) const;
    int16_t ySubscriptXSize () const;
    int16_t ySubscriptYSize () const;
    int16_t ySubscriptXOffset () const;
    int16_t ySubscriptYOffset () const;
    int16_t ySuperscriptXSize () const;
    int16_t ySuperscriptYSize () const;
    int16_t ySuperscriptXOffset () const;
    int16_t ySuperscriptYOffset () const;
    int16_t yStrikeoutSize () const;
    int16_t yStrikeoutPosition () const;
    int8_t sFamilyClass () const;
    int8_t sFamilySubClass () const;
    uint8_t panose (int index) const;
    bool ulUnicodeRange (int nbit) const;
    std::string achVendID () const;
    bool fsSelection (int nbit) const;
    uint16_t usFirstCharIndex () const;
    uint16_t usLastCharIndex () const;
    int16_t sTypoAscender () const;
    int16_t sTypoDescender () const;
    int16_t sTypoLineGap () const;
    uint16_t usWinAscent () const;
    uint16_t usWinDescent () const;
    bool ulCodePageRange (int nbit) const;
    int16_t sxHeight () const;
    int16_t sCapHeight () const;
    uint16_t usDefaultChar () const;
    uint16_t usBreakChar () const;
    uint16_t usMaxContext () const;
    uint16_t usLowerOpticalPointSize () const;
    uint16_t usUpperOpticalPointSize () const;

protected:
    struct os_2_data contents;
};
