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

#include "tables.h"
#include "tables/variations.h"

void FontVariations::readVariationStore (char *data, uint16_t pos, struct variation_store &vstore) {
    uint32_t start = pos;
    /*uint16_t length = */ FontTable::getushort (data, pos); pos +=2;
    vstore.format = FontTable::getushort (data, pos); pos +=2;
    uint32_t reg_offset = FontTable::getlong (data, pos); pos +=4;
    uint16_t data_count = FontTable::getushort (data, pos); pos +=2;

    std::vector<uint32_t> data_off_list;
    data_off_list.resize (data_count);
    for (uint16_t i=0; i<data_count; i++) {
	data_off_list[i] = FontTable::getlong (data, pos);
	pos +=4;
    }
    uint16_t axis_count = FontTable::getushort (data, pos); pos+=2;
    uint16_t region_count = FontTable::getushort (data, pos); pos+=2;
    pos = start + reg_offset;
    vstore.regions.reserve (region_count);

    for (uint16_t i=0; i<region_count; i++) {
	std::vector<struct axis_coordinates> region;
	for (uint16_t j=0; j<axis_count; j++) {
	    struct axis_coordinates va;
	    va.startCoord = FontTable::get2dot14 (data, pos); pos+=2;
	    va.peakCoord = FontTable::get2dot14 (data, pos); pos+=2;
	    va.endCoord = FontTable::get2dot14 (data, pos); pos+=2;
	    region.push_back (va);
	}
	vstore.regions.push_back (region);
    }
    for (uint16_t i=0; i<data_count; i++) {
	// this offset is from the start of ItemVariationStore, i. e. VariationStore Data + length field
	pos = start + data_off_list[i] + 2;
	struct variation_data vd;
	uint16_t item_count = FontTable::getushort (data, pos); pos+=2;
	uint16_t short_count = FontTable::getushort (data, pos); pos+=2;
	uint16_t reg_count = FontTable::getushort (data, pos); pos+=2;
	vd.shortDeltaCount = short_count;
	vd.regionIndexes.resize (reg_count);
	vstore.data.reserve (item_count);

	for (uint16_t j=0; j<reg_count; j++) {
	    vd.regionIndexes[j] = FontTable::getushort (data, pos); pos+=2;
	}

	std::vector<std::vector<int16_t>> dsets;
	for (uint16_t j=0; j<item_count; j++) {
	    std::vector<int16_t> deltas;
	    deltas.resize (reg_count);
	    for (uint16_t k=0; k<short_count; k++) {
		deltas[k] = FontTable::getushort (data, pos); pos+=2;
	    }
	    for (uint16_t k=short_count; k<reg_count; k++) {
		deltas[k] = data[pos]; pos++;
	    }
	    dsets.push_back (deltas);
	}
	vstore.data.push_back (vd);
    }
}

void FontVariations::writeVariationStore (QDataStream &os, QBuffer &buf, variation_store &vstore) {
    int init_pos = buf.pos ();
    int cur_pos;
    // Table size is uint16, while internal offsets, relative to the
    // start of the table, are uint32. Is it OK?
    os << (uint16_t) 0; // placeholder for table size
    os << vstore.format;
    os << (uint32_t) 0; // variationRegionListOffset
    os << (uint16_t) vstore.data.size ();

    for (size_t i=0; i<vstore.data.size (); i++)
	os << (uint32_t) 0;

    cur_pos = buf.pos ();
    buf.seek (init_pos + 4);
    // Table size field is not the part of the table itself,
    // hence subtract 2 from the offset
    os << (uint32_t) (cur_pos - init_pos - 2);
    buf.seek (cur_pos);
    uint16_t axis_cnt = vstore.regions[0].size ();
    uint16_t  reg_cnt = vstore.regions.size ();
    os << axis_cnt;
    os << reg_cnt;
    for (size_t i=0; i<reg_cnt; i++) {
	for (size_t j=0; j<axis_cnt; j++) {
	    FontTable::put2dot14 (os, vstore.regions[i][j].startCoord);
	    FontTable::put2dot14 (os, vstore.regions[i][j].peakCoord);
	    FontTable::put2dot14 (os, vstore.regions[i][j].endCoord);
	}
    }

    for (size_t i=0; i<vstore.data.size (); i++) {
	cur_pos = buf.pos ();
	buf.seek (init_pos + 10 + i*4);
	os << (uint32_t) (cur_pos - init_pos - 2);
	buf.seek (cur_pos);

	os << (uint16_t) vstore.data[i].deltaSets.size ();
	os << (uint16_t) vstore.data[i].shortDeltaCount;
	os << (uint16_t) vstore.data[i].regionIndexes.size ();
	for (size_t j=0; j<vstore.data[i].regionIndexes.size (); j++)
	    os << vstore.data[i].regionIndexes[j];
	for (size_t j=0; j<vstore.data[i].deltaSets.size (); j++) {
	    for (size_t k=0; k<vstore.data[i].regionIndexes.size (); k++) {
		if (k<vstore.data[i].shortDeltaCount)
		    os << vstore.data[i].deltaSets[j][k];
		else
		    os << (int8_t) vstore.data[i].deltaSets[j][k];
	    }
	}
    }
    cur_pos = buf.pos ();
    buf.seek (init_pos);
    os << (uint16_t) (cur_pos - init_pos - 2);
    buf.seek (cur_pos);
}
