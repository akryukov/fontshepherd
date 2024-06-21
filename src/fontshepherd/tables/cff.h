/* Copyright (C) 2000-2012 by George Williams
 * Copyright (C) 2022 by Alexey Kryukov
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

#ifndef _FONSHEPHERD_CFF_H
#define _FONSHEPHERD_CFF_H
#include <stdint.h>
#include <QtGlobal>

#include "cffstuff.h"

class FontTable;
class GlyphContainer;
class ConicGlyph;
struct variation_store;
class GlyphNameProvider;

class CffTable : public GlyphContainer {
public:
    CffTable (sfntFile* fontfile, TableHeader &props);
    ~CffTable ();
    void unpackData (sFont *font);
    void packData ();
    ConicGlyph *glyph (sFont* fnt, uint16_t gid);
    uint16_t addGlyph (sFont* fnt, uint8_t subfont=0);
    std::string glyphName (uint16_t gid);
    bool cidKeyed () const;
    int version () const;
    bool usable () const;
    int numSubFonts () const;
    PrivateDict *privateDict (uint16_t subidx=0);
    TopDict *topDict ();
    std::string fontName () const;
    std::string subFontName (uint16_t subidx);
    void clearStrings ();
    // returns sid
    int addString (const std::string &s);
    void addGlyphName (uint16_t gid, const std::string &name);
    uint16_t fdSelect (uint16_t gid);
    void setFdSelect (uint16_t gid, uint16_t val);
    void setVersion (double val, sFont *fnt, GlyphNameProvider &gnp);

    static void encodeInt (QDataStream &os, int val);
    static void encodeInt (std::ostream &os, int val);
    static void encodeSizedInt (QDataStream &os, uint8_t size, int val);
    static void encodeFixed (QDataStream &os, double val);
    static void encodeFixed (std::ostream &os, double val);
    static void encodeFloat (QDataStream &os, double val);
    static void encodeOper (QDataStream &os, uint16_t oper);
    static void encodeOper (std::ostream &os, uint16_t oper);
    static void encodeOff (QDataStream &os, uint8_t offsize, uint32_t val);

private:
    void updateCharStrings (struct pschars &chars, int sub_idx, double version);
    int stdWidth (HmtxTable *hmtx, int sub_idx);

    int  readcffthing (int *_ival, double *dval, uint16_t *operand);
    void skipcfft2thing ();
    void readCffNames (std::vector<std::string> &names);
    void readSubFonts ();
    void readCffSubrs (struct pschars &subs);
    void readCffTopDict (TopDict &td, uint32_t size);
    void readCffPrivate (PrivateDict &pd, uint32_t off, uint32_t size);
    void readCffSet (int off, int len, std::vector<uint16_t> &charset);
    void readvstore (struct variation_store &vstore);
    void readfdselect (std::vector<uint16_t> &fdselect, uint16_t numglyphs);
    std::string getsid (int sid, std::vector<std::string> &strings);

    void writeCffTopDict (TopDict &td, QDataStream &os, QBuffer &buf, uint16_t off_size);
    void writeCffPrivate (PrivateDict &pd, QDataStream &os, QBuffer &buf);
    void writeCffSet (QDataStream &os, QBuffer &, uint32_t off);
    void writeSubFonts (QDataStream &os, QBuffer &buf, uint8_t off_size);
    void writefdselect (QDataStream &os, QBuffer &);
    void writevstore (QDataStream &os, QBuffer &buf);
    void updateGlyph (uint16_t gid);

    void convertToCFF (sFont *fnt, GlyphNameProvider &gnp);
    void convertToCFF2 ();

    double m_version;
    uint16_t m_td_idx; // normally zero
    bool m_bad_cff;
    uint32_t m_pos;
    struct pschars m_gsubrs;
    struct cff_font m_core_font;
};

#endif
