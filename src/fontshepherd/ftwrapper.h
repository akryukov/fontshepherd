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

#ifndef _FONSHEPHERD_FTWRAPPER_H
#define _FONSHEPHERD_FTWRAPPER_H

#include <ft2build.h>
#include FT_FREETYPE_H

struct freetype_raster {
    bool valid;
    uint16_t rows, cols;
    int16_t  as, lb;
    uint16_t bytes_per_row;
    uint16_t num_grays;
    uint16_t advance;
    uint16_t linear_advance;
    std::vector<uint8_t> bitmap;
    QPixmap *pixmap;

    freetype_raster (): valid (false), rows (0), cols(0), as(0), lb(0), bytes_per_row(0), num_grays(0) {}
};

class FTWrapper {
    Q_DECLARE_TR_FUNCTIONS (FTWrapper);

public:
    static unsigned long qDeviceRead (FT_Stream stream, unsigned long offset, unsigned char* buffer, unsigned long count);
    static void qDeviceClose (FT_Stream stream);

    static int moveToFunction (const FT_Vector *to, void *user);
    static int lineToFunction (const FT_Vector *to, void *user);
    static int conicToFunction (const FT_Vector *control, const FT_Vector *to, void *user);
    static int cubicToFunction (const FT_Vector *c1, const FT_Vector *c2, const FT_Vector *to, void *user);

    FTWrapper ();
    ~FTWrapper ();

    void init (const char* filename, int idx);
    void init (const QString &fpath, int idx);
    int setPixelSize (int xsize, int ysize);
    struct freetype_raster gridFitGlyph (uint16_t gid, uint16_t flags, QPainterPath *p);
    bool hasContext ();
    bool hasFace ();

private:
    QFile m_fontf;
    FT_StreamRec m_stream;

    bool m_hasContext, m_hasFace;
    FT_Library m_context;
    FT_Face m_aface;
};

#endif
