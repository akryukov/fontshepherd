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
#include <chrono>

#include "sfnt.h"
#include "editors/headedit.h"
#include "tables/head.h"

HeadTable::HeadTable (sfntFile *fontfile, TableHeader &props) :
    FontTable (fontfile, props) {
    contents = {};
}

HeadTable::HeadTable (HeadTable* source) : FontTable (source) {
    contents = source->contents;
}

void HeadTable::unpackData (sFont*) {
    uint32_t pos = 0, date1, date2;
    this->fillup ();

    contents.version = getversion (pos); pos+=4;
    contents.fontRevision = getfixed (pos); pos+=4;
    contents.checkSumAdjustment = getlong (pos); pos +=4;
    contents.magicNumber = getlong (pos); pos +=4;
    contents.flags = getushort (pos); pos +=2;
    contents.unitsPerEm = getushort (pos); pos +=2;

    date1 = getlong (pos); pos +=4;
    date2 = getlong (pos); pos +=4;
    contents.created = quad2date (date1, date2);
    date1 = getlong (pos); pos +=4;
    date2 = getlong (pos); pos +=4;
    contents.modified = quad2date (date1, date2);

    contents.xMin = (int16_t) getushort (pos); pos +=2;
    contents.yMin = (int16_t) getushort (pos); pos +=2;
    contents.xMax = (int16_t) getushort (pos); pos +=2;
    contents.yMax = (int16_t) getushort (pos); pos +=2;
    contents.macStyle = getushort (pos); pos +=2;
    contents.lowestRecPPEM = getushort (pos); pos +=2;
    contents.fontDirectionHint = (int16_t) getushort (pos); pos +=2;
    contents.indexToLocFormat = (int16_t) getushort (pos); pos +=2;
    contents.glyphDataFormat = (int16_t) getushort (pos); pos +=2;
}

void HeadTable::packData () {
    std::ostringstream s;
    std::string st;

    delete[] data; data = nullptr;
    putfixed (s, contents.version);
    putfixed (s, contents.fontRevision);
    putlong (s, contents.checkSumAdjustment);
    putlong (s, contents.magicNumber);
    putushort (s, static_cast<uint16_t> (contents.flags.to_ulong ()));
    putushort (s, contents.unitsPerEm);
    std::array<uint32_t,2> c_date = unix_to_1904 (contents.created);
    std::array<uint32_t,2> m_date = unix_to_1904 (contents.modified);
    putlong (s, c_date[1]);
    putlong (s, c_date[0]);
    putlong (s, m_date[1]);
    putlong (s, m_date[0]);
    putushort (s, contents.xMin);
    putushort (s, contents.yMin);
    putushort (s, contents.xMax);
    putushort (s, contents.yMax);
    putushort (s, static_cast<uint16_t> (contents.macStyle.to_ulong ()));
    putushort (s, contents.lowestRecPPEM);
    putushort (s, contents.fontDirectionHint);
    putushort (s, contents.indexToLocFormat);
    putushort (s, contents.glyphDataFormat);

    changed = false;
    td_changed = true;
    start = 0xffffffff;

    st = s.str ();
    newlen = st.length ();
    data = new char[newlen];
    std::copy (st.begin (), st.end (), data);
}

void HeadTable::edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller) {
    if (data == nullptr)
        fillup ();

    if (tv == nullptr) {
	unpackData (fnt);
        HeadEdit *headedit = new HeadEdit (tptr, fnt, caller);
        tv = headedit;
        headedit->show ();
    } else {
        tv->raise ();
    }
}

double HeadTable::version () const {
    return contents.version;
}

double HeadTable::fontRevision () const {
    return contents.fontRevision;
}

uint32_t HeadTable::checkSumAdjustment () const {
    return contents.checkSumAdjustment;
}

uint32_t HeadTable::magicNumber () const {
    return contents.magicNumber;
}

bool HeadTable::flags (int nbit) const {
    if (nbit>=0 &&nbit<16)
	return (contents.flags[nbit]);
    return false;
}

void HeadTable::setBitFlag (int nbit, bool val) {
    if (nbit>=0 &&nbit<16)
	contents.flags[nbit] = val;
    changed = true;
}

uint16_t HeadTable::unitsPerEm () const {
    return contents.unitsPerEm;
}

time_t HeadTable::created () const {
    return contents.created;
}

time_t HeadTable::modified () const {
    return contents.modified;
}

int16_t HeadTable::xMin () const {
    return contents.xMin;
}

int16_t HeadTable::yMin () const {
    return contents.yMin;
}

int16_t HeadTable::xMax () const {
    return contents.xMax;
}

int16_t HeadTable::yMax () const {
    return contents.yMax;
}

bool HeadTable::macStyle (int nbit) const {
    if (nbit>=0 &&nbit<16)
	return (contents.macStyle[nbit]);
    return false;
}

uint16_t HeadTable::lowestRecPPEM () const {
    return contents.lowestRecPPEM;
}

int16_t HeadTable::fontDirectionHint () const {
    return contents.fontDirectionHint;
}

int16_t HeadTable::indexToLocFormat () const {
    return contents.indexToLocFormat;
}

int16_t HeadTable::glyphDataFormat () const {
    return contents.glyphDataFormat;
}

void HeadTable::updateModified () {
    auto now = std::chrono::system_clock::now ();
    contents.modified = std::chrono::system_clock::to_time_t (now);
}

void HeadTable::setCheckSumAdjustment (uint32_t adj) {
    putlong (data+2*sizeof (uint32_t), adj);
    contents.checkSumAdjustment = adj;
}

void HeadTable::setIndexToLocFormat (bool is_long) {
    putushort (data+32, is_long);
    contents.indexToLocFormat = is_long;
}

time_t HeadTable::quad2date (uint32_t date1, uint32_t date2) {
    time_t date;

    /* convert from a time based on 1904 to a unix time based on 1970 */
    if (sizeof (time_t)>32) {
	/* as unixes switch over to 64 bit times, this will be the better */
	/*  solution */
	date = ((((time_t) date1)<<16)<<16) | date2;
	date -= ((time_t) 60)*60*24*365*(70-4);
	date -= 60*60*24*(70-4)/4;		/* leap years */
    } else {
	/* But for now, we're stuck with this on most machines */
	int year[2], date1904[4], i;
	date1904[0] = date1>>16;
	date1904[1] = date1&0xffff;
	date1904[2] = date2>>16;
	date1904[3] = date2&0xffff;
	year[0] = 60*60*24*365;
	year[1] = year[0]>>16; year[0] &= 0xffff;
	for (i=4; i<70; ++i) {
	    date1904[3] -= year[0];
	    date1904[2] -= year[1];
	    if ((i&3) == 0)
		date1904[3] -= 60*60*24;
	    date1904[2] += date1904[3]>>16;
	    date1904[3] &= 0xffff;
	    date1904[1] += date1904[2]>>16;
	    date1904[2] &= 0xffff;
	}
	date = ((date1904[2]<<16) | date1904[3]);
    }
    return date;
}

std::array<uint32_t,2> HeadTable::unix_to_1904 (time_t date) {
    uint32_t date1970[4] = { 0 }, tm[4];
    uint32_t year[2];
    std::array<uint32_t,2> ret;
    int i;

    if (sizeof (time_t)>32) {
	tm[0] =  date     &0xffff;
	tm[1] = (date>>16)&0xffff;
	tm[2] = (date>>32)&0xffff;
	tm[3] = (date>>48)&0xffff;
    } else {
	tm[0] =  date     &0xffff;
	tm[1] = (date>>16)&0xffff;
	tm[2] = 0;
	tm[3] = 0;
    }
    year[0] = (60*60*24*365L)&0xffff;
    year[1] = (60*60*24*365L)>>16;
    for (i=1904; i<1970; ++i) {
        date1970[0] += year[0];
        date1970[1] += year[1];
        if ((i&3)==0 && (i%100!=0 || i%400==0))
    	date1970[0] += 24*60*60L;		/* Leap year */
        date1970[1] += (date1970[0]>>16);
        date1970[0] &= 0xffff;
        date1970[2] += date1970[1]>>16;
        date1970[1] &= 0xffff;
        date1970[3] += date1970[2]>>16;
        date1970[2] &= 0xffff;
    }

    for (i=0; i<3; ++i) {
        tm[i] += date1970[i];
        tm[i+1] += tm[i]>>16;
        tm[i] &= 0xffff;
    }
    tm[3] -= date1970[3];

    ret[0] = (tm[1]<<16) | tm[0];
    ret[1] = (tm[3]<<16) | tm[2];
    return ret;
}
