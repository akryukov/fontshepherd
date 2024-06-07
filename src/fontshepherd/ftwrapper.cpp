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

#include <QCoreApplication>
#include <QtGlobal>
#include <QImage>
#include <QPainterPath>
#include "fs_notify.h"
#include "ftwrapper.h"
#include <freetype/tttables.h>
#include FT_OUTLINE_H
#include FT_MODULE_H
#include <iostream>
#include <cstring>

unsigned long FTWrapper::qDeviceRead
    (FT_Stream stream, unsigned long offset, unsigned char* buffer, unsigned long count) {
    QFileDevice* fd = static_cast<QFileDevice *> (stream->descriptor.pointer);
    int64_t ret = 0;
    fd->seek (offset);
    ret = fd->read (reinterpret_cast<char *> (buffer), count);
    return static_cast<unsigned long> (ret);
}

void FTWrapper::qDeviceClose (FT_Stream stream) {
    QFileDevice* fd = static_cast<QFileDevice *> (stream->descriptor.pointer);
    fd->close ();
}

int FTWrapper::moveToFunction (const FT_Vector *to, void *user) {
    QPainterPath *path = static_cast<QPainterPath *> (user);
    path->moveTo (QPointF (to->x, to->y));
    return 0;
}

int FTWrapper::lineToFunction (const FT_Vector *to, void *user) {
    QPainterPath *path = static_cast<QPainterPath *> (user);
    path->lineTo (QPointF (to->x, to->y));
    return 0;
}

int FTWrapper::conicToFunction (const FT_Vector *control, const FT_Vector *to, void *user) {
    QPainterPath *path = static_cast<QPainterPath *> (user);
    path->quadTo (QPointF (control->x, control->y), QPointF (to->x, to->y));
    return 0;
}

int FTWrapper::cubicToFunction (const FT_Vector *c1, const FT_Vector *c2, const FT_Vector *to, void *user) {
    QPainterPath *path = static_cast<QPainterPath *> (user);
    path->cubicTo (QPointF (c1->x, c1->y), QPointF (c2->x, c2->y), QPointF (to->x, to->y));
    return 0;
}


FTWrapper::FTWrapper () : m_hasContext (false), m_hasFace (false) {
    int err = FT_Init_FreeType (&m_context);
    if (!err) m_hasContext = true;

    // Switch to traditional bytecode interpreter, taking into account horizontal hinting
    int version = 35;
    const char* module = "truetype";
    const char* property = "interpreter-version";
    FT_Property_Set (m_context, module, property, (void*)(&version));
}

FTWrapper::~FTWrapper () {
    if (m_hasFace) {
	FT_Done_Face (m_aface);
    }
}

void FTWrapper::init (const char* fpath, int idx) {
    if (m_hasContext) {
	if (FT_New_Face (m_context, fpath, idx, &m_aface))
	    FT_Done_Face (m_aface);
	else
	    m_hasFace = true;
    }
}

void FTWrapper::init (const QString &fpath, int idx) {
    if (m_hasContext) {
	m_fontf.setFileName (fpath);
	if (!m_fontf.open (QIODevice::ReadOnly)) {
	    FontShepherd::postError (
		tr ("File access error"),
		tr ("The file %1 is no longer accessible").arg (fpath),
		nullptr);
	    return;
	}

	std::memset (&m_stream, 0, sizeof (FT_StreamRec));
	m_stream.base = 0;
	m_stream.size = static_cast<unsigned long> (m_fontf.size ());
	m_stream.descriptor.pointer = static_cast<void *> (&m_fontf);
	m_stream.read = &qDeviceRead;
	m_stream.close = &qDeviceClose;

	FT_Open_Args args;
	std::memset (&args, 0, sizeof (FT_Open_Args));
	args.flags = FT_OPEN_STREAM;
	args.stream = &m_stream;
	args.driver = 0;

	if (FT_Open_Face (m_context, &args, idx, &m_aface))
	    FT_Done_Face (m_aface);
	else
	    m_hasFace = true;
    }
}

struct freetype_raster FTWrapper::gridFitGlyph (uint16_t gid, int xsize, int ysize, uint16_t flags, QPainterPath *p) {
    struct freetype_raster ret;

    if (FT_Set_Pixel_Sizes (m_aface, xsize, ysize)) {
        std::cout << "Error setting pixel size: could not load glyph " << gid << "\n";
        return ret;
    }

    // FT_LOAD_RENDER | FT_LOAD_NO_BITMAP | FT_LOAD_NO_AUTOHINT | FT_LOAD_MONOCHROME
    if (FT_Load_Glyph (m_aface, gid, flags)) {
        std::cout << "Missing glyph: could not load glyph " << gid << "\n";
        return ret;
    }

    FT_GlyphSlot slot = m_aface->glyph;
    ret.rows = slot->bitmap.rows;
    ret.cols = slot->bitmap.width;
    ret.bytes_per_row = slot->bitmap.pitch;
    ret.as = slot->bitmap_top;
    ret.lb = slot->bitmap_left;
    ret.num_grays = flags&FT_LOAD_MONOCHROME ? 2 : slot->bitmap.num_grays;
    ret.advance = slot->advance.x;
    size_t bsize = ret.rows*ret.bytes_per_row;
    ret.bitmap.insert (ret.bitmap.end (), slot->bitmap.buffer, slot->bitmap.buffer + bsize);
    ret.valid = true;

    FT_Outline_Funcs callbacks;
    callbacks.move_to = &moveToFunction;
    callbacks.line_to = &lineToFunction;
    callbacks.conic_to = &conicToFunction;
    callbacks.cubic_to = &cubicToFunction;
    callbacks.shift = 0;
    callbacks.delta = 0;

    FT_Outline &outline = slot->outline;

    if (FT_Outline_Decompose (&outline, &callbacks, p)) {
        std::cout << "Missing glyph: could not decompose outline " << gid << "\n";
    }
    return ret;
}

bool FTWrapper::hasContext () {
    return m_hasContext;
}

bool FTWrapper::hasFace () {
    return m_hasFace;
}
