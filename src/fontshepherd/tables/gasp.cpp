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
#include "editors/gaspedit.h"
#include "tables.h"
#include "gasp.h"

GaspTable::GaspTable (sfntFile *fontfile, TableHeader &props) :
    FontTable (fontfile, props) {
    contents = {};
}

void GaspTable::unpackData (sFont*) {
    uint32_t pos = 0;
    this->fillup ();

    contents.version = getushort (pos); pos+=2;
    uint16_t num_Ranges = getushort (pos); pos+=2;
    contents.ranges.resize (num_Ranges);
    for (size_t i=0; i<num_Ranges; i++) {
	contents.ranges[i].rangeMaxPPEM = getushort (pos); pos+=2;
	contents.ranges[i].rangeGaspBehavior = getushort (pos); pos+=2;
    }
}

void GaspTable::packData () {
    std::ostringstream s;
    std::string st;

    delete[] data; data = nullptr;

    putushort (s, contents.version);
    putushort (s, contents.ranges.size ());

    for (size_t i=0; i<numRanges (); i++) {
	putushort (s, contents.ranges[i].rangeMaxPPEM);
	putushort (s, contents.ranges[i].rangeGaspBehavior);
    }

    changed = false;
    td_changed = true;
    start = 0xffffffff;

    st = s.str ();
    newlen = st.length ();
    data = new char[newlen];
    std::copy (st.begin (), st.end (), data);
}

void GaspTable::edit (sFont* fnt, QWidget* caller) {
    if (data == nullptr)
        fillup ();

    if (tv == nullptr) {
	unpackData (fnt);
        GaspEdit *gaspedit = new GaspEdit (this, fnt, caller);
        tv = gaspedit;
        gaspedit->show ();
    } else {
        tv->raise ();
    }
}

uint16_t GaspTable::version () const {
    return contents.version;
}

uint16_t GaspTable::numRanges () const {
    return contents.ranges.size ();
}

uint16_t GaspTable::maxPPEM (uint16_t idx) const {
    if (idx < contents.ranges.size ()) {
	return contents.ranges[idx].rangeMaxPPEM;
    }
    return 0;
}

uint16_t GaspTable::gaspBehavior (uint16_t idx) const {
    if (idx < contents.ranges.size ()) {
	return contents.ranges[idx].rangeGaspBehavior;
    }
    return 0;
}

void GaspTable::setVersion (uint16_t ver) {
    contents.version = ver;
}

