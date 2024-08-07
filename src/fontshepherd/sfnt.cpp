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

#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "exceptions.h"
#include "sfnt.h"
#include "editors/fontview.h"
#include "tables/cmap.h"
#include "tables/devmetrics.h"
#include "tables/hea.h"
#include "tables/name.h"
#include "tables/maxp.h"
#include "tables/os_2.h"
#include "tables/head.h"
#include "tables/glyphcontainer.h" // also includes splineglyph.h
#include "tables/glyf.h"
#include "tables/instr.h"
#include "tables/cff.h"
#include "tables/svg.h"
#include "tables/colr.h"
#include "tables/mtx.h"
#include "tables/glyphnames.h"
#include "tables/gdef.h"
#include "tables/gasp.h"

#include "fs_notify.h"

std::shared_ptr<FontTable> ttffont::sharedTable (uint32_t tag) const {
    for (auto tptr: tbls) {
	for (int i=0; i<4 && tptr->iName (i); i++) {
	    if (tptr->iName (i) == tag)
		return tptr;
	}
    }
    return nullptr;
}

FontTable *ttffont::table (uint32_t tag) const {
    for (auto tptr: tbls) {
	FontTable *tbl = tptr.get ();
	for (int i=0; i<4 && tbl->iName (i); i++) {
	    if (tbl->iName (i) == tag)
		return tbl;
	}
    }
    return nullptr;
}

double ttffont::italicAngle () const {
    for (auto tptr: tbls) {
	FontTable *tbl = tptr.get ();
	if (tbl->iName (0) == CHR ('p','o','s','t')) {
	    PostTable *post = dynamic_cast<PostTable *> (tbl);
	    return post->italicAngle ();
	}
    }
    return 0;
}

int ttffont::tableCount () const {
    return static_cast<int> (tbls.size ());
}

uint16_t sfntFile::getushort (QIODevice *f) {
    char ch[2] = { 0 };
    if ((f->read (ch, 2)) < 0) {
	QFile *qf = qobject_cast<QFile *> (f);
	std::string path = qf ? qf->fileName ().toStdString() : "<IO Device>";
        throw FileDamagedException (path);
    }
    return (((uint8_t) ch[0]<<8)|(uint8_t) ch[1]);
}

uint32_t sfntFile::getlong (QIODevice *f) {
    char ch[4] = { 0 };
    if ((f->read (ch, 4)) < 0) {
	QFile *qf = qobject_cast<QFile *> (f);
	std::string path = qf ? qf->fileName ().toStdString() : "<IO Device>";
        throw FileDamagedException (path);
    }
    return (((uint8_t) ch[0]<<24)|((uint8_t) ch[1]<<16)|((uint8_t) ch[2]<<8)|(uint8_t) ch[3]);
}

double sfntFile::getfixed (QIODevice *f) {
    uint32_t val = getlong (f);
    int mant = val&0xffff;
    /* GWW: This oddity may be needed to deal with the first 16 bits being signed */
    /*  and the low-order bits unsigned */
    return ((double) (val>>16) + (mant/65536.0));
}

/* GWW: In table version numbers, the high order nibble of mantissa is in bcd, not hex */
/* I've no idea whether the lower order nibbles should be bcd or hex */
/* But let's assume some consistancy... */
double sfntFile::getvfixed (QIODevice *f) {
    uint32_t val = getlong (f);
    int mant = val&0xffff;
    mant = ((mant&0xf000)>>12)*1000 + ((mant&0xf00)>>8)*100 + ((mant&0xf0)>>4)*10 + (mant&0xf);
    return ((double) (val>>16) + (mant/10000.0));
}

double sfntFile::get2dot14 (QIODevice *f) {
    uint16_t val = getushort (f);
    int mant = val&0x3fff;
    /* GWW: This oddity may be needed to deal with the first 2 bits being signed */
    /*  and the low-order bits unsigned */
    return ((double) ((val<<16)>>(16+14)) + (mant/16384.0));
}

void sfntFile::putushort (QIODevice *f, uint16_t val) {
    f->putChar (val>>8);
    f->putChar (val&0xff);
}

void sfntFile::putlong (QIODevice *f, uint32_t val) {
    f->putChar (val>>24);
    f->putChar ((val>>16)&0xff);
    f->putChar ((val>>8)&0xff);
    f->putChar (val&0xff);
}

void sfntFile::put2d14 (QIODevice *f, double dval) {
    uint16_t val, mant;

    val = floor (dval);
    mant = floor (16384.*(dval-val));
    val = (val<<14) | mant;
    putushort (f, val);
}

uint32_t sfntFile::fileCheck (QIODevice *f) {
    uint32_t sum = 0, chunk;

    f->seek (0);
    chunk = getlong (f);
    while (!f->atEnd()) {
	sum += chunk;
	chunk = getlong (f);
    }
    f->seek (0);
    return (sum);
}

uint32_t sfntFile::figureCheck (QIODevice *f, uint32_t start, uint32_t lcnt) {
    uint32_t sum = 0, chunk;

    f->seek (start);
    while (!f->atEnd () && lcnt > 0) {
	chunk = getlong (f);
	sum += chunk;
        lcnt--;
    }
    return (sum);
}

QString sfntFile::getFontName (sFont *tf) {
    NameTable *name = dynamic_cast<NameTable *> (tf->table (CHR ('n','a','m','e')));
    if (name) {
	name->unpackData (tf);
	// First check full name, then PostScript name and finally family name
	QString ret = name->bestName (4);
	if (ret.compare ("<nameless>") == 0)
	    ret = name->bestName (6);
	if (ret.compare ("<nameless>") == 0)
	    ret = name->bestName (1);
	return ret;
    }
    return QString ("<nameless>");
}

QString sfntFile::getFamilyName (sFont *tf) {
    NameTable *name = dynamic_cast<NameTable *> (tf->table (CHR ('n','a','m','e')));
    if (name) {
	name->unpackData (tf);
	QString ret = name->bestName (1);
	return ret;
    }
    return QString ("<nameless>");
}

bool sfntFile::checkFSType (sFont *tf) {
    OS_2Table *os_2 = dynamic_cast<OS_2Table *> (tf->table (CHR ('O','S','/','2')));
    if (!os_2)
	return true;
    os_2->unpackData (tf);
    bool no_edit = os_2->fsType (1);

    if (no_edit) {
        QMessageBox::StandardButton ask;
        ask = QMessageBox::question (m_parent,
            m_parent->tr ("Restricted font"),
            m_parent->tr ("This font is marked with an FSType of 2 "
                        "(Restricted License). That means it is "
                        "not editable without the permission of the "
                        "legal owner.\n\nDo you have such a permission?"),
            QMessageBox::Yes|QMessageBox::No);
	if (ask == QMessageBox::No)
            return false;
    }

    return true;
}

void sfntFile::getGlyphCnt (sFont *tf) {
    MaxpTable *maxp = dynamic_cast<MaxpTable *> (tf->table (CHR ('m','a','x','p')));
    maxp->unpackData (tf);
    tf->glyph_cnt = maxp->numGlyphs ();
}

void sfntFile::getEmSize (sFont *tf) {
    HeadTable *head = dynamic_cast<HeadTable *> (tf->table (CHR ('h','e','a','d')));
    HeaTable *hhea = dynamic_cast<HeaTable *> (tf->table (CHR ('h','h','e','a')));
    head->unpackData (tf);
    tf->units_per_em = head->unitsPerEm ();
    tf->descent = head->yMin ();
    tf->ascent = head->yMax ();
    // Just for the case the ascent field is not properly filled in the head table...
    if (!tf->ascent && hhea) {
	tf->ascent = hhea->ascent ();
	if (tf->units_per_em > tf->ascent)
	    tf->descent = tf->units_per_em - tf->ascent;
    }
}

std::shared_ptr<FontTable> sfntFile::readTableHead (QFile *f, int file_idx) {
    TableHeader props;
    props.file = f;
    props.iname = getlong (f);
    props.checksum = getlong (f);
    props.off = getlong (f);
    props.length = getlong (f);
    FontTable *table;

    /* GWW: In a TTC file some tables may be shared, check through previous fonts */
    /*  in the file to see if we've got this already */
    int fcnt = m_fonts.size ();
    for (int i=0; i<fcnt; ++i) {
	sFont *fnt = m_fonts[i].get ();
	if (fnt->file_index != file_idx)
	    continue;
	for (int j=0; j<fnt->tableCount (); ++j) {
	    std::shared_ptr<FontTable> tptr = fnt->tbls[j];
	    if (tptr->start==props.off && tptr->len==props.length) {
		if (tptr->iName ()==props.iname)
                    return (tptr);
		/* GWW: EBDT/bdat, EBLC/bloc use the same structure and could share tables */
		for (size_t k=0; k<sizeof(tptr->m_tags)/sizeof(tptr->m_tags[0]); ++k) {
		    if (tptr->m_tags[k]==props.iname || tptr->m_tags[k]==0) {
			tptr->m_tags[k] = props.iname;
                        return (tptr);
		    }
		}
	    }
	}
    }

    switch (props.iname) {
        case CHR('C','F','F',' '):
        case CHR('C','F','F','2'):
            table = new CffTable (this, props);
            break;
        case CHR('c','m','a','p'):
            table = new CmapTable (this, props);
            break;
        case CHR('C','O','L','R'):
            table = new ColrTable (this, props);
            break;
        case CHR('C','P','A','L'):
            table = new CpalTable (this, props);
            break;
        case CHR('f','p','g','m'):
        case CHR('p','r','e','p'):
            table = new InstrTable (this, props);
            break;
        case CHR('g','a','s','p'):
            table = new GaspTable (this, props);
            break;
        case CHR('G','D','E','F'):
            table = new GdefTable (this, props);
            break;
        case CHR('h','d','m','x'):
            table = new HdmxTable (this, props);
            break;
        case CHR('h','e','a','d'):
            table = new HeadTable (this, props);
            break;
        case CHR('h','h','e','a'):
            table = new HeaTable (this, props);
            break;
        case CHR('h','m','t','x'):
            table = new HmtxTable (this, props);
            break;
        case CHR('g','l','y','f'):
            table = new GlyfTable (this, props);
            break;
        case CHR('l','o','c','a'):
            table = new LocaTable (this, props);
            break;
        case CHR('L','T','S','H'):
            table = new LtshTable (this, props);
            break;
        case CHR('m','a','x','p'):
            table = new MaxpTable (this, props);
            break;
        case CHR('n','a','m','e'):
            table = new NameTable (this, props);
            break;
        case CHR('O','S','/','2'):
            table = new OS_2Table (this, props);
            break;
        case CHR('p','o','s','t'):
            table = new PostTable (this, props);
            break;
        case CHR('S','V','G',' '):
            table = new SvgTable (this, props);
            break;
        case CHR('V','D','M','X'):
            table = new VdmxTable (this, props);
            break;
        case CHR('v','h','e','a'):
            table = new HeaTable (this, props);
            break;
        default:
            table = new FontTable (this, props);
    }
    return (std::shared_ptr<FontTable> (table));
}

void sfntFile::readSfntHeader (QFile *f, int file_idx) {
    m_fonts.emplace_back (new sFont ());
    sFont *tf = m_fonts.back ().get ();

    tf->version = getlong (f);
    int tbl_cnt = getushort (f);
    tf->container = this;
    /* searchRange = */ getushort (f);
    /* entrySelector = */ getushort (f);
    /* rangeshift = */ getushort (f);
    tf->tbls.reserve (tbl_cnt);
    for (int i=0; i<tbl_cnt; ++i)
	tf->tbls.push_back (readTableHead (f, file_idx));
    tf->fontname = getFontName (tf);
    tf->index = 0;
    tf->file_index = file_idx;

    getGlyphCnt (tf);
    getEmSize (tf);
    CmapTable *cmap = dynamic_cast<CmapTable *> (tf->table (CHR ('c','m','a','p')));
    if (cmap) {
	cmap->unpackData (tf);
	cmap->findBestSubTable (tf);
    }
}

void sfntFile::readTtcfHeader (QFile *f, int file_idx) {
    int base_cnt = m_fonts.size ();
    std::vector<int64_t> offsets;

    /* TTCF version = */ getlong (f);
    int add_cnt = getlong (f);
    m_fonts.reserve (base_cnt + add_cnt);
    offsets.resize (add_cnt);
    for (int i=0; i<add_cnt; ++i)
	offsets[i] = getlong (f);
    for (int i=0; i<add_cnt; ++i) {
	f->seek (offsets[i]);
	readSfntHeader (f, file_idx);
        m_fonts.back ()->index = base_cnt+i;
    }
    if (!checkFSType (m_fonts[0].get ()))
        throw FileLoadCanceledException (f->fileName ().toStdString());
}

void sfntFile::dumpFontHeader (QIODevice *newf, sFont *fnt) {
    int bit, i;
    int tbl_cnt = fnt->tableCount ();
    FontTable *tab;

    putlong (newf, fnt->version);
    putushort (newf, tbl_cnt);
    for (i= -1, bit = 1; bit<tbl_cnt; bit<<=1, ++i);
    bit>>=1;
    putushort (newf, bit*16);
    putushort (newf, i);
    putushort (newf, (tbl_cnt-bit)*16);
    for (i=0; i<tbl_cnt; ++i) {
	tab = fnt->tbls[i].get ();
	putlong (newf, tab->iName ());
	putlong (newf, tab->newchecksum);
	putlong (newf, tab->newstart);
	putlong (newf, tab->newlen);
    }
}

void sfntFile::dumpFontTables (QIODevice *newf, std::vector<sFont *> fonts) {
    int cnt;
    int font_cnt = fonts.size ();
    FontTable *tab;
    std::vector<std::shared_ptr<FontTable>> ordered;

    for (int i=cnt=0; i<font_cnt; ++i)
        cnt += fonts[i]->tableCount ();
    ordered.reserve (cnt);
    for (int i=0; i<font_cnt; ++i) {
	sFont *fnt = fonts[i];
        for (int j=0; j<fnt->tableCount (); ++j) {
	    tab = fnt->tbls[j].get ();
	    if (!tab->inserted) {
		ordered.push_back (fnt->tbls[j]);
		tab->inserted = true;
	    }
	}
    }
    cnt = ordered.size ();
    std::sort (ordered.begin (), ordered.end (),
	[](const std::shared_ptr<FontTable> &t1, const std::shared_ptr<FontTable> &t2) {
	    if (t1->orderingVal () == t2->orderingVal ())
		return (t1->iName () < t2->iName ());
	    else
		return (t1->orderingVal () < t2->orderingVal ());
    });

    for (int i=0; i<cnt; ++i) {
	tab = ordered[i].get ();
	if (tab->newstart!=0)		/* Saved by some earlier font in a ttc */
            continue;			/* Don't save again */
	// Resaving font would invalidate DSIG, so just write a dummy DSIG
	// instead of the existing one. If user doesn't want DSIG at all, (s)he
	// can simply delete it before saving the font
	if (tab->iName () == CHR ('D','S','I','G')) {
	    tab->clearData ();
	    tab->data = new char[8] {'\0', '\0', '\0', '\1', '\0', '\0', '\0', '\0'};
	    tab->newlen = 8;
	}
	tab->newstart = newf->pos ();
	{
            bool clear_data = false;
            if (!tab->data) {
                tab->fillup ();
                clear_data = true;
            }
	    newf->write (tab->data, tab->newlen);
            if (clear_data)
		tab->clearData ();
	}
	tab->newlen = newf->pos () - tab->newstart;
	if (tab->newlen&1)
	    newf->putChar ('\0');
	if ((tab->newlen+1)&2)
	    putushort (newf, 0);
    }

    for (int i=0; i<cnt; ++i) {
	tab = ordered[i].get ();
	if (tab->newchecksum != 0)
            continue;
	tab->newchecksum = figureCheck (newf, tab->newstart, (tab->newlen+3)>>2);
    }
}

void sfntFile::fntWrite (QIODevice *newf, sFont *fnt) {
    fnt->version_pos = newf->pos ();
    dumpFontHeader (newf, fnt);		/* Placeholder */
    dumpFontTables (newf, { fnt });
    newf->seek (fnt->version_pos);
    dumpFontHeader (newf, fnt);		/* Filling with correct values now we know them */
}

void sfntFile::ttcWrite (QIODevice *newf) {
    int32_t pos;
    int i;
    int font_cnt = m_fonts.size ();

    putlong (newf, CHR ('t','t','c','f'));
    putlong (newf, 0x20000);			/* AMK: Version */
    putlong (newf, (uint32_t) font_cnt);
    for (i=0; i<font_cnt; ++i)
	putlong (newf, 0);			/* GWW: Placeholder */
    for (i=0; i<font_cnt; ++i) {
	sFont *fnt = m_fonts[i].get ();
        fnt->version_pos = newf->pos ();
	dumpFontHeader (newf, fnt);		/* GWW: Also Placeholders */
    }
    pos = newf->pos ();
    newf->seek (3*sizeof (int32_t));
    for (i=0; i<font_cnt; ++i)
	putlong (newf, m_fonts[i]->version_pos);/* GWW: Fill in first set of placeholders */

    newf->seek (pos);
    std::vector<sFont *> fnt_raw (m_fonts.size ());
    for (auto &fptr : m_fonts) fnt_raw.push_back (fptr.get ());
    dumpFontTables (newf, fnt_raw);

    newf->seek (m_fonts[0]->version_pos);
    for (i=0; i<font_cnt; ++i) {
	sFont *fnt = m_fonts[i].get ();
        fnt->version_pos = newf->pos ();
	dumpFontHeader (newf, fnt);		/* GWW: Fill in final set */
    }
}

QFile *sfntFile::makeBackup (QFile *origf) {
    /* GWW: Traditionally we only make a backup on the first save. But due to my */
    /*  data structures I need to make a backup before each save */
    /* (while I'm saving the current file I still need some place where I can */
    /*  look up the data which are referenced by pointers into that file. */
    /* If we return successfully then ttf->file points to the backup file */
    /*  (which has all the right offsets), and returns a FILE pointing to */
    /*  the file to be saved */
    int len;
    QString backupname = QString (origf->fileName ().append (QChar ('~')));
    QFile *backup = new QFile (backupname);

    if (!backup->open (QIODevice::WriteOnly) || !origf->open (QIODevice::ReadOnly)) {
	delete backup;
        throw CantBackupException (backupname.toStdString ());
    }

    origf->seek (0);
    len = backup->write (origf->readAll ());
    backup->close ();

    if (len < 0) {
	delete backup;
        throw CantBackupException (backupname.toStdString ());
    }

    origf->close ();
    return backup;
}

void sfntFile::restoreFromBackup (QFile *target, QFile *source, int backidx) {
    source->open (QIODevice::ReadOnly);
    source->seek (0);
    target->open (QIODevice::WriteOnly);
    target->resize (0);
    int len = target->write (source->readAll ());
    if (len<0)
        throw CantRestoreException (source->fileName ().toStdString ());
    source->close ();
    target->close ();

    for (size_t i=0; i<m_fonts.size (); ++i) {
	sFont *tf = m_fonts[i].get ();
	if (tf->file_index == backidx) {
	    for (int j=0; j < tf->tableCount (); ++j)
		tf->tbls[j]->infile = target;
	}
    }
}

bool sfntFile::save (const QString &newpath, bool ttc, int fidx) {
    QTemporaryFile newf;
    int file_idx = hasSource (fidx, ttc) ? m_fonts[fidx]->file_index : 0;
    int backup_idx = -1;
    size_t font_cnt = m_fonts.size ();
    size_t imin = ttc ? 0 : fidx;
    size_t imax = ttc ? font_cnt : fidx+1;
    uint32_t checksum;

    // QTemporaryFile will always be opened in QIODevice::ReadWrite mode
    if (!newf.open ())
        throw FileAccessException (newf.fileName ().toStdString ());

    // Check if we are going to save a font into the same location (so a backup is needed)
    QFile testf;
    testf.setFileName (newpath);
    QFileInfo info (testf);
    if (info.exists ()) {
	for (size_t i=0; i<m_files.size (); i++) {
	    QFile *oldf = m_files[i].get ();
	    QFileInfo orig (*oldf);
	    if (orig.canonicalFilePath () == info.canonicalFilePath ()) {
		backup_idx = i;
		break;
	    }
	}
    }

    /* GWW: Mark all tables as unsaved */
    for (size_t i=imin; i<imax; ++i) {
	sFont *tf = m_fonts[i].get ();
	// If some of the source files are going to be rewritten, then we are
	// supposed to have a backup at this point. So make internal pointers
	// in table structures to refer to the backed up file rather than
	// to the original one. We need this because table data aren't loaded
	// into application until they are requested to, so it is necessary
	// to have a relevant data source on the disk
	for (int j=0; j < tf->tableCount (); ++j) {
	    FontTable *tab = tf->tbls[j].get ();
	    tab->newstart = 0;
	    tab->newchecksum = 0;
	    tab->inserted = false;
       }
       // Sort tables alphabetically for font header output and further
       // displaying in table view. The actual order of table data in the
       // file is different and is defined by table's orderingVal ()
       std::sort (tf->tbls.begin (), tf->tbls.end (),
	    [](const std::shared_ptr<FontTable> &t1, const std::shared_ptr<FontTable> &t2) {
		return (t1->iName () < t2->iName ());
       });
    }

    for (size_t i=imin; i<imax; ++i) {
	sFont *tf = m_fonts[i].get ();
	HeadTable *head = dynamic_cast<HeadTable *> (tf->table (CHR ('h','e','a','d')));
	if (head) {
	    head->updateModified ();
	    head->packData ();
	}
    }

    if (ttc)
	ttcWrite (&newf);
    else
	fntWrite (&newf, m_fonts[fidx].get ());

    checksum = fileCheck (&newf);
    for (size_t i=imin; i<imax; ++i) {
	sFont *tf = m_fonts[i].get ();
	HeadTable *head = dynamic_cast<HeadTable *> (tf->table (CHR ('h','e','a','d')));
	// GWW notes he has seen a TTC font where a 0xdcd07d3e magick was
	// used to calculate checksum adjustment. However, that font had
	// a shared head table, which is not always the case. The spec
	// now says the checksum adjustment field is irrelevant for TTC fonts
	// and should be ignored. So just set it to zero in case of TTC.
	if (head) {
	    checksum = ttc ? 0 : 0xb1b0afba - checksum;
	    newf.seek (head->newstart+2*sizeof (uint32_t));
	    putlong (&newf, checksum);
	    head->setCheckSumAdjustment (checksum);
	    // Redisplay modified checksumadjust fields
	    if (head->editor ())
		head->editor ()->resetData ();
	}
    }

    if (backup_idx >= 0) {
	QFile *oldf = m_files[backup_idx].get ();
	makeBackup (oldf);
	oldf->remove ();
    } else if (testf.exists ()) {
	testf.remove ();
    }

    if (newf.copy (newpath)) {
	newf.close ();

	// if a TTC file has successfully been written, then we now have just one
	// source file, so all existing pointers to source files shoud be cleaned
	// up. Otherwise make sure a correct source file is attached to the
	// font we have just written
	if (!ttc && backup_idx < 0) {
	    m_files[file_idx]->setFileName (newpath);
	} else if (ttc) {
	    for (auto &file: m_files)
		file->close ();
	    m_files.clear ();
	    m_files.emplace_back (new QFile ());
	    m_files.back ()->setFileName (newpath);
	}

	for (size_t i=imin; i<imax; ++i) {
	    sFont *tf = m_fonts[i].get ();
	    tf->file_index = file_idx;
	    for (int j=0; j<tf->tableCount (); ++j) {
		FontTable *tab = tf->tbls[j].get ();
		tab->start = tab->newstart;
		tab->len = tab->newlen;
		tab->oldchecksum = tab->newchecksum;
		tab->changed = tab->td_changed = false;
		tab->inserted = false;
		tab->infile = m_files.back ().get ();
		tab->is_new = false;
	    }
	}
	if (m_fonts.size () == 1 || (ttc && m_fonts.size () > 1)) {
	    changed = false;
	}
	return (true);
    }
    throw FileAccessException (newf.fileName ().toStdString ());
}

QString sfntFile::name () const {
    return m_font_name;
}

const QString sfntFile::path (int idx) const {
    int fcnt = m_fonts.size ();
    if (idx >= 0 && idx < fcnt)
	return m_files[m_fonts[idx]->file_index]->fileName ();
    return QString ();
}

bool sfntFile::hasSource (int, bool ttc) {
    if (ttc && m_files.size () > 1)
	return false;
    return true;
}

QWidget *sfntFile::parent () {
    return m_parent;
}

int sfntFile::fontCount () const {
    return m_fonts.size ();
}

sFont *sfntFile::font (int index) {
    if (index >=0 && (size_t) index < m_fonts.size ())
	return m_fonts[index].get ();
    return nullptr;
}

void sfntFile::doLoadFile (QFile *newf) {
    uint32_t version = getlong (newf);
    int file_idx = m_fonts.empty () ? 0 : m_fonts.back ()->file_index+1;

    if (version==CHR('t','t','c','f')) {
        readTtcfHeader (newf, file_idx);
        QFileInfo fi = QFileInfo (*newf);
	if (m_fonts.size () > 0)
	    m_font_name = getFamilyName (m_fonts[0].get ());
	if (m_font_name.compare ("<nameless>") == 0)
	    m_font_name = QString (fi.fileName ());
    } else if (version==0x00010000 ||
        version == CHR('O','T','T','O') || version == CHR('t','r','u','e')) {
	newf->seek (0);
        readSfntHeader (newf, file_idx);
        m_font_name = m_fonts[0]->fontname;
        if (!checkFSType (m_fonts[0].get ()))
            throw FileLoadCanceledException (newf->fileName ().toStdString ());
    } else {
        throw FileDamagedException (newf->fileName ().toStdString ());
    }
}

void sfntFile::addToCollection (const QString &path) {
    assert (!m_files.empty ());
    m_files.emplace_back (new QFile ());
    QFile *newf = m_files.back ().get ();

    for (size_t i=0; i<m_files.size ()-1; i++) {
	QFile *origf = m_files[i].get ();
	const QString &origpath = origf->fileName ();
	if (path == origpath)
	    throw FileDuplicateException (origpath.toStdString ());
    }
    newf->setFileName (path);

    if (!newf->open (QIODevice::ReadOnly))
        throw FileNotFoundException (newf->fileName ().toStdString ());

    for (size_t i=0; i<m_files.size ()-1; i++) {
	QFile *origf = m_files[i].get ();
	QFileInfo orig (*origf);
	QFileInfo info (*newf);
	if (orig.canonicalFilePath () == info.canonicalFilePath ()) {
	    newf->close ();
	    throw FileDuplicateException (orig.absoluteFilePath ().toStdString ());
	}
    }

    doLoadFile (newf);
    newf->close ();
    changed = true;
}

void sfntFile::removeFromCollection (int index) {
    if (m_fonts.size () > 1 && index >=0 && (size_t) index < m_fonts.size ())
	m_fonts.erase (m_fonts.begin () + index);
    changed = true;
}

int sfntFile::tableRefCount (FontTable *tbl) {
    int cnt = 0;
    for (auto &fnt: m_fonts) {
	for (auto &tptr : fnt->tbls) {
	    if (tptr.get () == tbl)
		cnt++;
	}
    }
    return cnt;
}

sfntFile::sfntFile (const QString &path, QWidget *w) : m_parent (w), changed (false) {
    m_files.emplace_back (new QFile ());
    QFile *newf = m_files.back ().get ();

    newf->setFileName (path);
    if (!newf->open (QIODevice::ReadOnly))
        throw FileNotFoundException (newf->fileName ().toStdString ());

    doLoadFile (newf);
    newf->close ();
}

sfntFile::~sfntFile () {}
