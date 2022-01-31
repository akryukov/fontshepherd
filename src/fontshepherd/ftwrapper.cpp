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

#include <QtGlobal>
#include <QImage>
#include "ftwrapper.h"
#include <freetype/tttables.h>
#include <iostream>

void FTWrapper::init () {
    int err = FT_Init_FreeType (&_context);
    if (!err) _loaded = true;
}

bool FTWrapper::_loaded = false;
FT_Library FTWrapper::_context = nullptr;

bool FTWrapper::hasContext () {
    return _loaded;
}

std::vector<struct freetype_raster> FTWrapper::getRaster (const char* filename, int index, int size) {
    FT_GlyphSlot slot;
    FT_Face aface;
    std::vector<struct freetype_raster> ret;
    int i, scaled_size;
    int ascent, descent;
    TT_OS2 *os2;
    FT_Size_RequestRec req;

    if (FT_New_Face (_context, filename, index, &aface))
        return ret;	/* Error Return */

    scaled_size = (int) (size*.88);

    req.type           = FT_SIZE_REQUEST_TYPE_NOMINAL;
    req.width          = (FT_Long)(scaled_size << 6);
    req.height         = (FT_Long)(scaled_size << 6);
    req.horiResolution = 0;
    req.vertResolution = 0;

    if (FT_Request_Size (aface, &req)) {
        FT_Done_Face (aface);
        return ret;	/* Error Return */
    }

    os2 = (TT_OS2*) FT_Get_Sfnt_Table (aface, ft_sfnt_os2);
    //hhea = (TT_HoriHeader*) FT_Get_Sfnt_Table (aface, ft_sfnt_hhea);
    ascent = os2->usWinAscent;
    descent = os2->usWinDescent;

    ret.reserve (aface->num_glyphs);
    for (i=0; i<aface->num_glyphs; i++) {
        struct freetype_raster cur = freetype_raster ();

        if (FT_Load_Glyph (aface, i, FT_LOAD_RENDER)) {
            cur.bitmap = nullptr;
            std::cout << "Missing glyph: could not load glyph " << i << "\n";
            ret.push_back (cur);
            continue;
        }

        slot = ((FT_Face) aface)->glyph;

        cur.rows = slot->bitmap.rows;
        cur.cols = slot->bitmap.width;
        cur.bytes_per_row = slot->bitmap.pitch;
        cur.as = slot->bitmap_top;
        cur.num_grays = slot->bitmap.num_grays;
        /* GWW: Can't find any description of freetype's bitendianness */
        /* These guys will probably be greyscale anyway... */
        cur.bitmap = new unsigned char [cur.rows*cur.bytes_per_row];
        memcpy (cur.bitmap, slot->bitmap.buffer, cur.rows*cur.bytes_per_row);
        ret.push_back (cur);
    }

    FT_Done_Face (aface);
    return (ret);
}
