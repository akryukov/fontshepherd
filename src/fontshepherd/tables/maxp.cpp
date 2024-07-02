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

#include <assert.h>
#include <sstream>
#include <ios>

#include "sfnt.h"
#include "tables.h"
#include "maxp.h"
#include "editors/maxpedit.h"

MaxpTable::MaxpTable (sfntFile *fontfile, TableHeader &props) :
    FontTable (fontfile, props) {
    contents = {};
}

void MaxpTable::unpackData (sFont*) {
    uint32_t pos = 0;
    this->fillup ();

    contents.version = getvfixed (pos); pos+=4;
    contents.numGlyphs = getushort (pos); pos +=2;
    if (contents.version < 1)
      return;

    contents.maxPoints = getushort (pos); pos +=2;
    contents.maxContours = getushort (pos); pos +=2;
    contents.maxCompositePoints = getushort (pos); pos +=2;
    contents.maxCompositeContours = getushort (pos); pos +=2;
    contents.maxZones = getushort (pos); pos +=2;
    contents.maxTwilightPoints = getushort (pos); pos +=2;
    contents.maxStorage = getushort (pos); pos +=2;
    contents.maxFunctionDefs = getushort (pos); pos +=2;
    contents.maxInstructionDefs = getushort (pos); pos +=2;
    contents.maxStackElements = getushort (pos); pos +=2;
    contents.maxSizeOfInstructions = getushort (pos); pos +=2;
    contents.maxComponentElements = getushort (pos); pos +=2;
    contents.maxComponentDepth = getushort (pos); pos +=2;
}

void MaxpTable::packData () {
    std::ostringstream s;
    std::string st;

    delete[] data; data = nullptr;

    putfixed (s, contents.version);
    putushort (s, contents.numGlyphs);

    if (contents.version >= 1) {
	putushort (s, contents.maxPoints);
	putushort (s, contents.maxContours);
	putushort (s, contents.maxCompositePoints);
	putushort (s, contents.maxCompositeContours);
	putushort (s, contents.maxZones);
	putushort (s, contents.maxTwilightPoints);
	putushort (s, contents.maxStorage);
	putushort (s, contents.maxFunctionDefs);
	putushort (s, contents.maxInstructionDefs);
	putushort (s, contents.maxStackElements);
	putushort (s, contents.maxSizeOfInstructions);
	putushort (s, contents.maxComponentElements);
	putushort (s, contents.maxComponentDepth);
    }

    changed = false;
    td_changed = true;
    start = 0xffffffff;

    st = s.str ();
    newlen = st.length ();
    data = new char[newlen];
    std::copy (st.begin (), st.end (), data);
}

void MaxpTable::edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller) {
    if (data == nullptr && !is_new)
        fillup ();

    if (tv == nullptr) {
	unpackData (fnt);
        MaxpEdit *edit = new MaxpEdit (tptr, fnt, caller);
        tv = edit;
        edit->show ();
    } else {
        tv->raise ();
    }
}

double MaxpTable::version () const {
    return contents.version;
}

uint16_t MaxpTable::numGlyphs () const {
    return contents.numGlyphs;
}

uint16_t MaxpTable::maxPoints () const {
    return contents.maxPoints;
}

uint16_t MaxpTable::maxContours () const {
    return contents.maxContours;
}

uint16_t MaxpTable::maxCompositePoints () const {
    return contents.maxCompositePoints;
}

uint16_t MaxpTable::maxCompositeContours () const {
    return contents.maxCompositeContours;
}

uint16_t MaxpTable::maxZones () const {
    return contents.maxZones;
}

uint16_t MaxpTable::maxTwilightPoints () const {
    return contents.maxTwilightPoints;
}

uint16_t MaxpTable::maxStorage () const {
    return contents.maxStorage;
}

uint16_t MaxpTable::maxFunctionDefs () const {
    return contents.maxFunctionDefs;
}

uint16_t MaxpTable::maxInstructionDefs () const {
    return contents.maxInstructionDefs;
}

uint16_t MaxpTable::maxStackElements () const {
    return contents.maxStackElements;
}

uint16_t MaxpTable::maxSizeOfInstructions () const {
    return contents.maxSizeOfInstructions;
}

uint16_t MaxpTable::maxComponentElements () const {
    return contents.maxComponentElements;
}

uint16_t MaxpTable::maxComponentDepth () const {
    return contents.maxComponentDepth;
}

void MaxpTable::setGlyphCount (uint16_t cnt) {
    //putushort (data+4, cnt);
    contents.numGlyphs = cnt;
    changed = true;
}
