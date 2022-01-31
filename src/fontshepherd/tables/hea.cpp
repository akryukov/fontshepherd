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
#include "editors/heaedit.h"
#include "tables/hea.h"

HeaTable::HeaTable (sfntFile *fontfile, TableHeader &props) :
    FontTable (fontfile, props) {
    contents = {};
}

void HeaTable::unpackData (sFont*) {
    uint32_t pos = 0;
    this->fillup ();

    contents.version = getfixed (pos); pos+=4;
    contents.ascent = getushort (pos); pos +=2;
    contents.descent = getushort (pos); pos +=2;
    contents.lineGap = getushort (pos); pos +=2;
    contents.advanceMax = getushort (pos); pos +=2;
    contents.minStartSideBearing = getushort (pos); pos +=2;
    contents.minEndSideBearing = getushort (pos); pos +=2;
    contents.maxExtent = getushort (pos); pos +=2;
    contents.caretSlopeRise = getushort (pos); pos +=2;
    contents.caretSlopeRun = getushort (pos); pos +=2;
    contents.caretOffset = getushort (pos); pos +=2;
    contents.reserved1 = getushort (pos); pos +=2;
    contents.reserved2 = getushort (pos); pos +=2;
    contents.reserved3 = getushort (pos); pos +=2;
    contents.reserved4 = getushort (pos); pos +=2;
    contents.metricDataFormat = getushort (pos); pos +=2;
    contents.numOfMetrics = getushort (pos); pos +=2;
}

void HeaTable::packData () {
    std::ostringstream s;
    std::string st;

    delete[] data; data = nullptr;

    putfixed (s, contents.version);
    putushort (s, contents.ascent);
    putushort (s, contents.descent);
    putushort (s, contents.lineGap);
    putushort (s, contents.advanceMax);
    putushort (s, contents.minStartSideBearing);
    putushort (s, contents.minEndSideBearing);
    putushort (s, contents.maxExtent);
    putushort (s, contents.caretSlopeRise);
    putushort (s, contents.caretSlopeRun);
    putushort (s, contents.caretOffset);
    putushort (s, contents.reserved1);
    putushort (s, contents.reserved2);
    putushort (s, contents.reserved3);
    putushort (s, contents.reserved4);
    putushort (s, contents.metricDataFormat);
    putushort (s, contents.numOfMetrics);

    changed = false;
    td_changed = true;
    start = 0xffffffff;

    st = s.str ();
    newlen = st.length ();
    data = new char[newlen];
    std::copy (st.begin (), st.end (), data);
}

bool HeaTable::isVertical () const {
    return m_tags[0] == CHR('v','h','e','a');
}

void HeaTable::edit (sFont* fnt, QWidget* caller) {
    if (data == nullptr)
        fillup ();

    if (tv == nullptr) {
	unpackData (fnt);
        HeaEdit *heaedit = new HeaEdit (this, fnt, caller);
        tv = heaedit;
        heaedit->show ();
    } else {
        tv->raise ();
    }
}

double HeaTable::version () const {
    return contents.version;
}

int16_t HeaTable::ascent () const {
    return contents.ascent;
}

int16_t HeaTable::descent () const {
    return contents.descent;
}

int16_t HeaTable::lineGap () const {
    return contents.lineGap;
}

int HeaTable::advanceMax () const {
    return contents.advanceMax;
}

int16_t HeaTable::minStartSideBearing () const {
    return contents.minStartSideBearing;
}

int16_t HeaTable::minEndSideBearing () const {
    return contents.minEndSideBearing;
}

int16_t HeaTable::maxExtent () const {
    return contents.maxExtent;
}

int16_t HeaTable::caretSlopeRise () const {
    return contents.caretSlopeRise;
}

int16_t HeaTable::caretSlopeRun () const {
    return contents.caretSlopeRun;
}

int16_t HeaTable::caretOffset () const {
    return contents.caretOffset;
}

int16_t HeaTable::metricDataFormat () const {
    return contents.metricDataFormat;
}

uint16_t HeaTable::numOfMetrics () const {
    return contents.numOfMetrics;
}

void HeaTable::setNumOfMetrics (uint16_t num) {
    contents.numOfMetrics = num;
}

