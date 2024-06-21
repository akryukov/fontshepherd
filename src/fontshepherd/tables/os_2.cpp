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

#include <cstring>
#include <sstream>
#include <ios>
#include <assert.h>

#include "sfnt.h"
#include "editors/os_2edit.h"
#include "tables/os_2.h"

OS_2Table::OS_2Table (sfntFile *fontfile, TableHeader &props) :
    FontTable (fontfile, props) {
    contents = {};
}

void OS_2Table::unpackData (sFont*) {
    uint32_t pos = 0;
    this->fillup ();

    contents.version = getushort (pos); pos +=2;
    contents.xAvgCharWidth = getushort (pos); pos +=2;
    contents.usWeightClass = getushort (pos); pos +=2;
    contents.usWidthClass = getushort (pos); pos +=2;
    contents.fsType = getushort (pos); pos +=2;
    contents.ySubscriptXSize = getushort (pos); pos +=2;
    contents.ySubscriptYSize = getushort (pos); pos +=2;
    contents.ySubscriptXOffset = getushort (pos); pos +=2;
    contents.ySubscriptYOffset = getushort (pos); pos +=2;
    contents.ySuperscriptXSize = getushort (pos); pos +=2;
    contents.ySuperscriptYSize = getushort (pos); pos +=2;
    contents.ySuperscriptXOffset = getushort (pos); pos +=2;
    contents.ySuperscriptYOffset = getushort (pos); pos +=2;
    contents.yStrikeoutSize = getushort (pos); pos +=2;
    contents.yStrikeoutPosition = getushort (pos); pos +=2;
    contents.sFamilyClass = data[pos]; pos ++;
    contents.sFamilySubClass = data[pos]; pos ++;
    for (int i=0; i<10; i++) {
	contents.panose[i] = data[pos]; pos++;
    }
    contents.ulUnicodeRange1 = getlong (pos); pos +=4;
    contents.ulUnicodeRange2 = getlong (pos); pos +=4;
    contents.ulUnicodeRange3 = getlong (pos); pos +=4;
    contents.ulUnicodeRange4 = getlong (pos); pos +=4;
    for (int i=0; i<4; i++) {
	contents.achVendID[i] = data[pos]; pos++;
    }
    contents.fsSelection = getushort (pos); pos +=2;
    contents.usFirstCharIndex = getushort (pos); pos +=2;
    contents.usLastCharIndex = getushort (pos); pos +=2;
    // Trunkated Apple's version of the table format 0
    if (contents.version == 0 && len == pos)
	return;
    contents.sTypoAscender = getushort (pos); pos +=2;
    contents.sTypoDescender = getushort (pos); pos +=2;
    contents.sTypoLineGap = getushort (pos); pos +=2;
    contents.usWinAscent = getushort (pos); pos +=2;
    contents.usWinDescent = getushort (pos); pos +=2;
    if (contents.version == 0)
	return;
    contents.ulCodePageRange1 = getlong (pos); pos +=4;
    contents.ulCodePageRange2 = getlong (pos); pos +=4;
    if (contents.version == 1)
	return;
    contents.sxHeight = getushort (pos); pos +=2;
    contents.sCapHeight = getushort (pos); pos +=2;
    contents.usDefaultChar = getushort (pos); pos +=2;
    contents.usBreakChar = getushort (pos); pos +=2;
    contents.usMaxContext = getushort (pos); pos +=2;
    if (contents.version < 5)
	return;
    contents.usLowerOpticalPointSize = getushort (pos); pos +=2;
    contents.usUpperOpticalPointSize = getushort (pos); pos +=2;
}

void OS_2Table::packData () {
    std::ostringstream s;
    std::string st;

    delete[] data; data = nullptr;

    putushort (s, contents.version);
    putushort (s, contents.xAvgCharWidth);
    putushort (s, contents.usWeightClass);
    putushort (s, contents.usWidthClass);
    putushort (s, static_cast<uint16_t> (contents.fsType.to_ulong ()));
    putushort (s, contents.ySubscriptXSize);
    putushort (s, contents.ySubscriptYSize);
    putushort (s, contents.ySubscriptXOffset);
    putushort (s, contents.ySubscriptYOffset);
    putushort (s, contents.ySuperscriptXSize);
    putushort (s, contents.ySuperscriptYSize);
    putushort (s, contents.ySuperscriptXOffset);
    putushort (s, contents.ySuperscriptYOffset);
    putushort (s, contents.yStrikeoutSize);
    putushort (s, contents.yStrikeoutPosition);
    s.put (contents.sFamilyClass);
    s.put (contents.sFamilySubClass);
    for (int i=0; i<10; i++)
	s.put (contents.panose[i]);
    putlong (s, contents.ulUnicodeRange1.to_ulong ());
    putlong (s, contents.ulUnicodeRange2.to_ulong ());
    putlong (s, contents.ulUnicodeRange3.to_ulong ());
    putlong (s, contents.ulUnicodeRange4.to_ulong ());
    for (int i=0; i<4; i++)
	s.put (contents.achVendID[i]);
    putushort (s, static_cast<uint16_t> (contents.fsSelection.to_ulong ()));
    putushort (s, contents.usFirstCharIndex);
    putushort (s, contents.usLastCharIndex);
    putushort (s, contents.sTypoAscender);
    putushort (s, contents.sTypoDescender);
    putushort (s, contents.sTypoLineGap);
    putushort (s, contents.usWinAscent);
    putushort (s, contents.usWinDescent);
    if (contents.version > 0) {
	putlong (s, contents.ulCodePageRange1.to_ulong ());
	putlong (s, contents.ulCodePageRange2.to_ulong ());
    }
    if (contents.version > 1) {
	putushort (s, contents.sxHeight);
	putushort (s, contents.sCapHeight);
	putushort (s, contents.usDefaultChar);
	putushort (s, contents.usBreakChar);
	putushort (s, contents.usMaxContext);
    }
    if (contents.version > 4) {
	putushort (s, contents.usLowerOpticalPointSize);
	putushort (s, contents.usUpperOpticalPointSize);
    }

    changed = false;
    td_changed = true;
    start = 0xffffffff;

    st = s.str ();
    newlen = st.length ();
    data = new char[newlen];
    std::copy (st.begin (), st.end (), data);
}

void OS_2Table::edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller) {
    if (data == nullptr)
        fillup ();

    if (tv == nullptr) {
	unpackData (fnt);
        OS_2Edit *os_2edit = new OS_2Edit (tptr, fnt, caller);
        tv = os_2edit;
        os_2edit->show ();
    } else {
        tv->raise ();
    }
}

uint16_t OS_2Table::version () const {
    return contents.version;
}

int16_t OS_2Table::xAvgCharWidth () const {
    return contents.xAvgCharWidth;
}

uint16_t OS_2Table::usWeightClass () const {
    return contents.usWeightClass;
}

uint16_t OS_2Table::usWidthClass () const {
    return contents.usWidthClass;
}

bool OS_2Table::fsType (int nbit) const {
    if (nbit>=0 &&nbit<16)
	return (contents.fsType[nbit]);
    return false;
}

int16_t OS_2Table::ySubscriptXSize () const {
    return contents.ySubscriptXSize;
}

int16_t OS_2Table::ySubscriptYSize () const {
    return contents.ySubscriptYSize;
}

int16_t OS_2Table::ySubscriptXOffset () const {
    return contents.ySubscriptXOffset;
}

int16_t OS_2Table::ySubscriptYOffset () const {
    return contents.ySubscriptYOffset;
}

int16_t OS_2Table::ySuperscriptXSize () const {
    return contents.ySuperscriptXSize;
}

int16_t OS_2Table::ySuperscriptYSize () const {
    return contents.ySuperscriptYSize;
}

int16_t OS_2Table::ySuperscriptXOffset () const {
    return contents.ySuperscriptXOffset;
}

int16_t OS_2Table::ySuperscriptYOffset () const {
    return contents.ySuperscriptYOffset;
}

int16_t OS_2Table::yStrikeoutSize () const {
    return contents.yStrikeoutSize;
}

int16_t OS_2Table::yStrikeoutPosition () const {
    return contents.yStrikeoutPosition;
}

int8_t OS_2Table::sFamilyClass () const {
    return contents.sFamilyClass;
}

int8_t OS_2Table::sFamilySubClass () const {
    return contents.sFamilySubClass;
}

uint8_t OS_2Table::panose (int index) const {
    return contents.panose[index];
}

bool OS_2Table::ulUnicodeRange (int nbit) const  {
    if (nbit>=0 &&nbit<32)
	return (contents.ulUnicodeRange1[nbit]);
    else if (nbit>=32 &&nbit<64)
	return (contents.ulUnicodeRange2[nbit]);
    else if (nbit>=64 &&nbit<96)
	return (contents.ulUnicodeRange3[nbit]);
    else if (nbit>=96 &&nbit<128)
	return (contents.ulUnicodeRange4[nbit]);
    return false;
}

std::string OS_2Table::achVendID () const {
    return std::string (std::begin (contents.achVendID), std::end (contents.achVendID));
}

bool OS_2Table::fsSelection (int nbit) const {
    if (nbit>=0 &&nbit<16)
	return (contents.fsSelection[nbit]);
    return false;
}

uint16_t OS_2Table::usFirstCharIndex () const {
    return contents.usFirstCharIndex;
}

uint16_t OS_2Table::usLastCharIndex () const {
    return contents.usLastCharIndex;
}

int16_t OS_2Table::sTypoAscender () const {
    return contents.sTypoAscender;
}

int16_t OS_2Table::sTypoDescender () const {
    return contents.sTypoDescender;
}

int16_t OS_2Table::sTypoLineGap () const {
    return contents.sTypoLineGap;
}

uint16_t OS_2Table::usWinAscent () const {
    return contents.usWinAscent;
}

uint16_t OS_2Table::usWinDescent () const {
    return contents.usWinDescent;
}

bool OS_2Table::ulCodePageRange (int nbit) const {
    if (nbit>=0 &&nbit<32)
	return (contents.ulCodePageRange1[nbit]);
    else if (nbit>=32 &&nbit<64)
	return (contents.ulCodePageRange2[nbit]);
    return false;
}

int16_t OS_2Table::sxHeight () const {
    return contents.sxHeight;
}

int16_t OS_2Table::sCapHeight () const {
    return contents.sCapHeight;
}

uint16_t OS_2Table::usDefaultChar () const {
    return contents.usDefaultChar;
}

uint16_t OS_2Table::usBreakChar () const {
    return contents.usBreakChar;
}

uint16_t OS_2Table::usMaxContext () const {
    return contents.usMaxContext;
}

uint16_t OS_2Table::usLowerOpticalPointSize () const {
    return contents.usLowerOpticalPointSize;
}

uint16_t OS_2Table::usUpperOpticalPointSize () const {
    return contents.usUpperOpticalPointSize;
}
