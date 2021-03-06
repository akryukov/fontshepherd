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

#ifndef _FONSHEPHERD_VARIATIONS_H
#define _FONSHEPHERD_VARIATIONS_H

#include <QtCore>

struct axis_coordinates {
    double startCoord, peakCoord, endCoord;
};

struct blend {
    double base = 0;
    bool valid = false;
    std::vector<double> deltas;

    const std::string toString () const;
};

struct delta_set_index_map {
    uint8_t format, entryFormat;
    std::string data;
};

struct variation_data {
    uint16_t shortDeltaCount;
    std::vector<uint16_t> regionIndexes;
    std::vector<std::vector<int16_t>> deltaSets;
};

struct variation_store {
    std::vector<std::vector<struct axis_coordinates>> regions;
    std::vector<struct variation_data> data;
    uint16_t format, index;
};

namespace FontVariations {
    const uint8_t INNER_INDEX_BIT_COUNT_MASK = 0x0F;
    const uint8_t MAP_ENTRY_SIZE_MASK = 0x30;

    void readVariationStore (char *data, uint32_t pos, variation_store &vstore);
    void writeVariationStore (QDataStream &os, QBuffer &buf, variation_store &vstore);
    void readIndexMap (char *data, uint32_t pos, delta_set_index_map &map);
};

#endif
