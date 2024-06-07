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

#ifndef _FONSHEPHERD_SFNT_H
#define _FONSHEPHERD_SFNT_H

#include <QtWidgets>

#define CHR(ch1,ch2,ch3,ch4) (((ch1)<<24)|((ch2)<<16)|((ch3)<<8)|(ch4))

class sfntFile;
class CmapEnc;
class FontTable;

// Data types representing the font itself

typedef struct ttffont {
    QString fontname;
    int32_t  version, version_pos;
    std::vector<std::shared_ptr<FontTable>> tbls;
    uint16_t glyph_cnt;
    uint16_t units_per_em;
    uint16_t ascent, descent;
    CmapEnc *enc;
    sfntFile *container;
    int index;                  /* In TTC file */
    int file_index = 0;

    FontTable *table (uint32_t tag) const;
    double italicAngle () const;
    int tableCount () const;
} sFont;

class sfntFile {
public:
    sfntFile (const QString &path, QWidget *w);
    ~sfntFile ();
    bool save (const QString &newpath, bool ttc, int fidx=0);
    QString name () const;
    const QString path (int idx) const;
    bool hasSource (int idx, bool ttc);
    QWidget* parent ();
    int fontCount () const;
    sFont *font (int index);
    void addToCollection (const QString &path);
    void removeFromCollection (int index);

private:
    static uint16_t getushort (QDataStream *in);
    static uint32_t getlong (QDataStream *in);
    static double getfixed (QDataStream *in);
    static double getvfixed (QDataStream *in);
    static double get2dot14 (QDataStream *in);

    static void putushort (QDataStream *out, uint16_t val);
    static void putlong (QDataStream *out, uint32_t val);
    static void put2d14 (QDataStream *out, double dval);
    static uint32_t fileCheck (QFile *f, QDataStream *in);
    static uint32_t figureCheck (QFile *f, QDataStream *in, uint32_t start, uint32_t lcnt);

    bool checkFSType (sFont *tf);
    QString getFontName (sFont *tf);
    QString getFamilyName (sFont *tf);
    void getGlyphCnt (sFont *tf);
    void getEmSize (sFont *tf);

    void doLoadFile (QFile *newf, QDataStream &news);
    std::shared_ptr<FontTable> readTableHead (QFile *f, QDataStream *s, int file_idx);
    void readSfntHeader (QFile *f, QDataStream *s, int file_idx);
    void readTtcfHeader (QFile *f, QDataStream *s, int file_idx);

    void dumpFontHeader (QDataStream *news, sFont *fnt);
    void dumpFontTables (QFile *newf, QDataStream *news);
    void fntWrite (QFile *newf, QDataStream *news, sFont *fnt);
    void ttcWrite (QFile *newf, QDataStream *news);
    QFile *makeBackup (QFile *origf);
    void restoreFromBackup (QFile *target, QFile *source, int backidx);

    std::vector<std::unique_ptr<sFont>> m_fonts;
    std::vector<std::unique_ptr<QFile>> m_files;
    QString m_font_name;
    QWidget *m_parent;

    bool changed;
    bool backedup;		/* a backup file has been created */
};

#endif
