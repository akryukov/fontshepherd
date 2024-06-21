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

#include "ftwrapper.h"
#include <iostream>

class FontTable;
class HdmxEdit;

class HdmxTable : public FontTable {
    friend class DeviceMetricsProvider;
    friend class HdmxEdit;
public:
    HdmxTable (sfntFile* fontfile, TableHeader &props);
    ~HdmxTable () {};
    void unpackData (sFont *font);
    void packData ();
    void edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller);

    uint16_t version () const;
    uint16_t numRecords () const;
    uint16_t numGlyphs () const;
    uint8_t maxWidth (uint8_t size) const;
    uint8_t maxSize () const;

    void setNumGlyphs (uint16_t cnt);
    void addSize (uint8_t size);
    void clear ();

private:
    uint16_t m_version;
    std::map<uint8_t, std::vector<uint8_t>> records;
};

class LtshTable : public FontTable {
    friend class DeviceMetricsProvider;
public:
    LtshTable (sfntFile* fontfile, TableHeader &props);
    ~LtshTable () {};
    void unpackData (sFont *font);
    void packData ();
    //void edit (sFont* fnt, QWidget* caller);

    uint16_t version () const;
    uint16_t numGlyphs () const;
    uint8_t yPixel (uint16_t gid) const;

    void setNumGlyphs (uint16_t cnt, bool clear);

private:
    uint16_t m_version;
    std::vector<uint8_t> yPixels;
};

struct vdmx_vTable {
    uint16_t yPelHeight;
    int16_t yMax;
    int16_t yMin;
};

struct vdmx_group {
    uint8_t bCharSet;
    uint8_t xRatio;
    uint8_t yStartRatio;
    uint8_t yEndRatio;
    uint16_t groupOff;
    uint8_t startsz;
    uint8_t endsz;
    std::vector<vdmx_vTable> entries;
};

class VdmxTable : public FontTable {
    friend class DeviceMetricsProvider;
    friend class VdmxEdit;
public:
    VdmxTable (sfntFile* fontfile, TableHeader &props);
    ~VdmxTable () {};
    void unpackData (sFont *font);
    void packData ();
    void edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller);

    uint16_t version () const;
    uint16_t numRatios () const;

    int addRatio (uint8_t x, uint8_t yStart, uint8_t yEnd);
    void setRatioRange (uint16_t idx, uint8_t start_size, uint8_t end_size);
    void clear ();

private:
    uint16_t m_version;
    std::vector<vdmx_group> records;
};

#ifndef _FS_STRUCT_DBOUNDS_DEFINED
#define _FS_STRUCT_DBOUNDS_DEFINED
typedef struct dbounds {
    double minx, maxx;
    double miny, maxy;
} DBounds;
#endif

class GlyphContainer;

class DeviceMetricsProvider {
    Q_DECLARE_TR_FUNCTIONS (DeviceMetricsProvider)
public:
    DeviceMetricsProvider (sFont &fnt);
    ~DeviceMetricsProvider () {};

    bool checkHead (const char *tag, QWidget *parent);
    void checkGlyphCount (GlyphContainer *glyf, uint16_t gcnt);

    int calculateHdmx (HdmxTable &hdmx, QWidget *parent);
    int calculateLtsh (LtshTable &ltsh, QWidget *parent);
    int calculateVdmx (VdmxTable &hdmx, QWidget *parent);

private:
    int calculateVdmxLimit (VdmxTable &vdmx, std::vector<std::pair<int, DBounds>> &metrics, bool up, QWidget *parent);

    sFont &m_font;
    FTWrapper ftWrapper;
};
