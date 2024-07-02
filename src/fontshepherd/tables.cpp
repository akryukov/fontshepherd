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

#include "exceptions.h"

#include "sfnt.h"
#include "editors/fontview.h" // also includes tables.h
#include "tables/maxp.h"
#include "tables/mtx.h"
#include "tables/glyphcontainer.h" // also includes splineglyph.h

FontTable::FontTable (sfntFile *fontfile, const TableHeader &props) :
    container (fontfile),
    infile (props.file),
    m_tags {props.iname, 0, 0, 0},
    oldchecksum (props.checksum),
    start (props.off),
    len (props.length),
    newlen (len),
    data (nullptr),
    tv (nullptr) {

    changed = td_changed = required = is_new = freeing = inserted = processed = td_loaded = false;
    if (infile == nullptr) is_new = true;
}

FontTable::FontTable (FontTable* table) {
    container = table->container;
    m_tags = table->m_tags;
    start = table->start;
    len = table->len;
    newlen = table->newlen == 0 ? table->len : table->newlen;
    oldchecksum = table->oldchecksum;

    if (table->data) {
        data = new char[(newlen+3)&~3]; // padding to uint32
	std::copy (table->data, table->data + ((newlen+3)&~3), data);
    } else
        data = nullptr;
}

FontTable::FontTable (QByteArray storage) {
    QDataStream buf (&storage, QIODevice::ReadOnly);
    uint8_t flags;
    bool has_data;

    for (int i=0; i<4; i++)
        buf >> m_tags[i];
    buf >> start;
    buf >> len;
    buf >> newlen;
    buf >> oldchecksum;
    buf >> newchecksum;
    buf >> flags;
    has_data   = (flags>>7)&1;
    changed    = (flags>>6)&1;
    td_changed = (flags>>5)&1;
    required    = (flags>>4)&1;
    is_new     = (flags>>3)&1;
    freeing    = (flags>>2)&1;
    inserted   = (flags>>1)&1;
    processed  = flags&1;

    data = nullptr;
    tv = nullptr;

    if (has_data > 0) {
        data = new char[(len+3)&~3](); // padding to uint32
        buf.readRawData (data, len);
    }
}

FontTable::~FontTable () {
    if (data) {
        delete[] data;
        data = nullptr;
    }
    if (tv) {
        tv->close ();
        tv = nullptr;
    }
}

uint32_t FontTable::iName (int index) const {
    if (index>=0 && index<4)
	return this->m_tags[index];
    return 0;
}

uint32_t FontTable::dataLength () const {
    return newlen;
}

bool FontTable::isRequired () const {
    return required;
}

std::string FontTable::stringName (int index) const {
    std::stringbuf sb;

    if (index>=0 && index<4) {
	for (int i = 3; i >= 0; --i)
	    sb.sputc ((char) ((m_tags[index] & (0xFF << (i*8))) >> (i*8)));
    }
    return sb.str ();
}

uint16_t FontTable::getushort (char *bdata, uint32_t pos) {
    uint8_t ch1 = bdata[pos];
    uint8_t ch2 = bdata[pos+1];
    return ((ch1<<8)|ch2);
}

uint16_t FontTable::getushort (uint32_t pos) {
    if (pos+1 >= newlen)
        throw TableDataCorruptException (stringName ().c_str ());
    return getushort (data, pos);
}

uint32_t FontTable::get3bytes (uint32_t pos) {
    if (pos+2 >= newlen)
        throw TableDataCorruptException (stringName ().c_str ());
    uint8_t ch1 = data[pos];
    uint8_t ch2 = data[pos+1];
    uint8_t ch3 = data[pos+2];
    return ((ch1<<16)|(ch2<<8)|ch3);
}

uint32_t FontTable::getlong (char *bdata, uint32_t pos) {
    uint8_t ch1 = bdata[pos];
    uint8_t ch2 = bdata[pos+1];
    uint8_t ch3 = bdata[pos+2];
    uint8_t ch4 = bdata[pos+3];
    return ((ch1<<24)|(ch2<<16)|(ch3<<8)|ch4);
}

uint32_t FontTable::getlong (uint32_t pos) {
    if (pos+3 >= newlen)
        throw TableDataCorruptException (stringName ().c_str ());
    return getlong (data, pos);
}

double FontTable::getfixed (uint32_t pos) {
    uint32_t val = getlong (pos);
    int mant = val&0xffff;
    /* GWW: This oddity may be needed to deal with the first 16 bits being signed */
    /*  and the low-order bits unsigned */
    return ((double) (val>>16) + (mant/65536.0));
}

/* GWW: In table version numbers, the high order nibble of mantissa is in bcd, not hex */
/* I've no idea whether the lower order nibbles should be bcd or hex */
/* But let's assume some consistancy... */
// AMK: The following format is solely for 'post', 'maxp' and 'vea'
double FontTable::getvfixed (uint32_t pos) {
    uint32_t val = getlong (pos);
    int mant = val&0xffff;
    mant = ((mant&0xf000)>>12)*1000 + ((mant&0xf00)>>8)*100 + ((mant&0xf0)>>4)*10 + (mant&0xf);
    return ((double) (val>>16) + (mant/10000.0));
}

// And the following one is for most "normal" tables
double FontTable::getversion (uint32_t pos) {
    uint32_t val = getlong (pos);
    double mant = val&0xffff;
    for (; mant>1; mant /= 10);
    return (val>>16) + mant;
}

double FontTable::get2dot14 (char *bdata, uint32_t pos) {
    uint32_t val = getushort (bdata, pos);
    int mant = val&0x3fff;
    /* GWW: This oddity may be needed to deal with the first 2 bits being signed */
    /*  and the low-order bits unsigned */
    return ((double) ((val<<16)>>(16+14)) + (mant/16384.0));
}

double FontTable::get2dot14 (uint32_t pos) {
    if (pos+2 >= newlen)
        throw TableDataCorruptException (stringName ().c_str ());
    return get2dot14 (data, pos);
}

uint32_t FontTable::getoffset (uint32_t pos, uint8_t size) {
    switch (size) {
      case 1:
        return (uint8_t) data[pos];
      case 2:
        return getushort (pos);
      case 3:
        return get3bytes (pos);
      default:
        return getlong (pos);
    }
}

void FontTable::putushort (char *data, uint16_t val) {
    data[0] = (val>>8);
    data[1] = val&0xff;
}

void FontTable::putlong (char *data, uint32_t val) {
    data[0] = (val>>24);
    data[1] = (val>>16)&0xff;
    data[2] = (val>>8)&0xff;
    data[3] = val&0xff;
}

void FontTable::putfixed (char *data, double val) {
    int ints = floor(val);
    int mant = (val-ints)*65536;
    uint32_t ival = (ints<<16) | mant;
    putlong (data, ival);
}

void FontTable::putvfixed (char *data, double val) {
    int ints = floor (val);
    int mant = (val-ints)*10000;
    uint16_t ival = ints<<16;

    ival |= (mant/1000)<<12;
    ival |= (mant/100%10)<<8;
    ival |= (mant/10%10)<<4;
    ival |= (mant%10);

    putlong (data, ival);
}

void FontTable::putushort (std::ostream &os, uint16_t val) {
    char data[2];
    data[0] = (val>>8);
    data[1] = val&0xff;
    os.write (data, 2);
}

void FontTable::put3bytes (std::ostream &os, uint32_t val) {
    char data[3];
    data[0] = (val>>16)&0xff;
    data[1] = (val>>8)&0xff;
    data[2] = val&0xff;
    os.write (data, 3);
}

void FontTable::putlong (std::ostream &os, uint32_t val) {
    char data[4];
    data[0] = (val>>24);
    data[1] = (val>>16)&0xff;
    data[2] = (val>>8)&0xff;
    data[3] = val&0xff;
    os.write (data, 4);
}

void FontTable::putfixed (std::ostream &os, double val) {
    int ints = floor (val);
    int mant = (val-ints)*65536;
    uint32_t ival = (ints<<16) | mant;
    putlong (os, ival);
}

void FontTable::putvfixed (std::ostream &os, double val) {
    int ints = floor (val);
    int mant = (val-ints)*10000;
    uint16_t ival = ints<<16;

    ival |= (mant/1000)<<12;
    ival |= (mant/100%10)<<8;
    ival |= (mant/10%10)<<4;
    ival |= (mant%10);

    putlong (os, ival);
}

void FontTable::put2dot14 (QDataStream &os, double dval) {
    uint16_t val, mant;

    val = floor (dval);
    mant = floor (16384.*(dval-val));
    val = (val<<14) | mant;
    os << val;
}

void FontTable::fillup () {
    if (infile && !data) {
	QDataStream in (infile);
	bool was_open = infile->isOpen ();
	if (!was_open) infile->open (QIODevice::ReadOnly);

	uint32_t padded = (len+3)&~3;
	data = new char[padded] (); // padding to uint32
	infile->seek (start);
	in.readRawData (data, len);
	if (!was_open) infile->close ();
    }
}

bool FontTable::loaded () const {
    return (data != nullptr);
}

bool FontTable::isNew () const {
    return is_new;
}

bool FontTable::compiled () const {
    return td_changed;
}

void FontTable::clearData () {
    delete[] data;
    data = nullptr;
    td_loaded = false;
}

// See https://docs.microsoft.com/en-us/typography/opentype/otspec140/recom,
// "Optimized table ordering", for reference. This order is recommended
// for the TrueType fonts to be used on Windows platform. We don't attempt to
// mantain a special order for OpenType-CFF fonts, which basically
// differs at 2 points: 'name' is placed before 'cmap' and 'hmtx' is
// not listed at all. However, 'hmtx' would be relevant at least for CFF2 fonts,
// which probably makes this special CFF order not applicable at least
// if CFF2 is used
int FontTable::orderingVal () {
    switch (m_tags[0]) {
      case CHR ('h','e','a','d'):
	return 0;
      case CHR ('h','h','e','a'):
	return 1;
      case CHR ('m','a','x','p'):
	return 2;
      case CHR ('O','S','/','2'):
	return 3;
      case CHR ('h','m','t','x'):
	return 4;
      case CHR ('L','T','S','H'):
	return 5;
      case CHR ('V','D','M','X'):
	return 6;
      case CHR ('h','d','m','x'):
	return 7;
      case CHR ('c','m','a','p'):
	return 8;
      case CHR ('f','p','g','m'):
	return 9;
      case CHR ('p','r','e','p'):
	return 10;
      case CHR ('c','v','t',' '):
	return 11;
      case CHR ('l','o','c','a'):
	return 12;
      case CHR ('g','l','y','f'):
	return 13;
      case CHR ('k','e','r','n'):
	return 14;
      case CHR ('n','a','m','e'):
	return 15;
      case CHR ('p','o','s','t'):
	return 16;
      case CHR ('g','a','s','p'):
	return 17;
      case CHR ('P','C','L','T'):
	return 18;
      case CHR ('D','S','I','G'):
	return 19;
      case CHR ('C','F','F',' '):
	return 20;
      case CHR ('C','F','F','2'):
	return 20;
      default:
	return 0xFF;
    }
}

void FontTable::copyData (FontTable *source) {
    if (data) {
        delete[] data;
        data = nullptr;
    }

    if (!source->data)
        return;

    newlen = source->newlen == 0 ? source->len : source->newlen;
    oldchecksum = source->oldchecksum;
    data = new char[(newlen+3)&~3]; // padding to uint32
    std::copy (source->data, source->data + ((newlen+3)&~3), data);
}

QByteArray FontTable::serialize () {
    QByteArray ret;
    QDataStream buf (&ret, QIODevice::WriteOnly);

    for (int i=0; i<4; i++)
        buf << m_tags[i];
    buf << start;
    buf << len;
    buf << newlen;
    buf << oldchecksum;
    buf << newchecksum;
    buf << (uint8_t) ((data!=nullptr)<<7|changed<<6|td_changed<<5|required<<4|is_new<<3|freeing<<2|inserted<<1|processed);
    if (data)
        buf.writeRawData (data, (len+3)&~3);
    return ret;
}

void FontTable::setModified (bool val) {
    this->changed = val;
    if (!val) this->start = 0xffffffff;
}

bool FontTable::modified () const {
    return this->changed;
}

void FontTable::setContainer (sfntFile *cont_file) {
    this->container = cont_file;
}

void FontTable::clearEditor () {
    this->tv = nullptr;
}

void FontTable::setEditor (TableEdit *editor) {
    this->tv = editor;
}

TableEdit *FontTable::editor () {
    return this->tv;
}

void FontTable::hexEdit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller) {
    if (data == nullptr && !is_new)
        fillup ();

    if (tv == nullptr) {
        HexTableEdit *hexedit = new HexTableEdit (tptr, caller);
        hexedit->setWindowTitle
	    (QString ("%1 - %2").arg (QString::fromStdString (stringName ())).arg (fnt->fontname));
        hexedit->setData (data, newlen);
        tv = hexedit;
        hexedit->show ();
    } else {
        tv->raise ();
    }
}

void FontTable::edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller) {
    hexEdit (fnt, tptr, caller);
}

sfntFile* FontTable::containerFile () {
    return container;
}

GlyphContainer::GlyphContainer (sfntFile* fontfile, const TableHeader &props) :
    FontTable (fontfile, props),
    m_hmtx (nullptr) {
};

void GlyphContainer::unpackData (sFont* fnt) {
    m_glyphs.reserve (fnt->glyph_cnt + 256);
    m_glyphs.resize (fnt->glyph_cnt, nullptr);

    m_hmtx = dynamic_cast<HmtxTable *> (fnt->table (CHR ('h','m','t','x')));
    m_maxp = dynamic_cast<MaxpTable *> (fnt->table (CHR ('m','a','x','p')));
    if (!m_hmtx || !m_maxp)
        return;

    m_hmtx->fillup ();
    m_hmtx->unpackData (fnt);
}

void GlyphContainer::edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller) {
    // No fillup here, as it is done by fontview
    FontView *fv = caller->findChild<FontView *> ();

    if (fv) {
        fv->setTable (tptr);
        fv->raise ();
    } else {
        fv = new FontView (fnt->sharedTable (this->iName ()), fnt, caller);
        if (!fv->isValid ()) {
            fv->close ();
            return;
        }
        tv = fv;
        fv->show ();
    }
}

uint16_t GlyphContainer::countGlyphs () {
    return m_glyphs.size ();
}

OutlinesType GlyphContainer::outlinesType () const {
    uint32_t tag = m_tags[0];
    switch (tag) {
      case CHR('C','F','F',' '):
      case CHR('C','F','F','2'):
	return OutlinesType::PS;
	break;
      case CHR('g','l','y','f'):
	return OutlinesType::TT;
	break;
      case CHR('S','V','G',' '):
	return OutlinesType::SVG;
	break;
      default:
	return OutlinesType::NONE;
    }
}

/* Default editor, based on the QHexEdit widget */
HexTableEdit::HexTableEdit (std::shared_ptr<FontTable> tab, QWidget* parent) :
    TableEdit (parent, Qt::Window), m_table (tab) {

    m_edited = m_valid = false;

    saveAction = new QAction (tr ("&Export to font"), this);
    closeAction = new QAction (tr ("C&lose"), this);

    connect (saveAction, &QAction::triggered, this, &HexTableEdit::save);
    connect (closeAction, &QAction::triggered, this, &HexTableEdit::close);

    saveAction->setShortcut (QKeySequence::Save);
    closeAction->setShortcut (QKeySequence::Close);

    undoAction = new QAction (tr ("&Undo"), this);
    redoAction = new QAction (tr ("Re&do"), this);
    toggleReadOnlyAction = new QAction (tr ("&Read only"), this);
    toggleOverwriteAction = new QAction (tr ("&Overwrite mode"), this);

    undoAction->setShortcut (QKeySequence::Undo);
    redoAction->setShortcut (QKeySequence::Redo);
    toggleReadOnlyAction->setCheckable (true);
    toggleReadOnlyAction->setChecked (true);
    toggleOverwriteAction->setShortcut (QKeySequence (Qt::Key_Insert));
    toggleOverwriteAction->setCheckable (true);
    toggleOverwriteAction->setChecked (false);

    connect (toggleReadOnlyAction, &QAction::triggered, this, &HexTableEdit::toggleReadOnly);
    connect (toggleOverwriteAction, &QAction::triggered, this, &HexTableEdit::toggleOverwrite);

    fileMenu = menuBar ()->addMenu (tr ("&File"));
    fileMenu->addAction (saveAction);
    fileMenu->addSeparator ();
    fileMenu->addAction (closeAction);

    editMenu = menuBar ()->addMenu (tr ("&Edit"));
    editMenu->addAction (undoAction);
    editMenu->addAction (redoAction);
    fileMenu->addSeparator ();
    editMenu->addAction (toggleReadOnlyAction);
    editMenu->addAction (toggleOverwriteAction);

    m_hexedit = new QHexEdit ();
    setAttribute (Qt::WA_DeleteOnClose);
    m_hexedit->setOverwriteMode (false);
    m_hexedit->setReadOnly (true);
    QFontMetrics hexmetr = m_hexedit->fontMetrics();
    QString line = QString (76, '0');
    m_hexedit->resize (hexmetr.boundingRect (line).width (), hexmetr.height () * 16);
    resize (hexmetr.boundingRect (line).width (), hexmetr.height () * 16);

    connect (m_hexedit, &QHexEdit::dataChanged, this, &HexTableEdit::edited);
    connect (undoAction, &QAction::triggered, m_hexedit, &QHexEdit::undo);
    connect (redoAction, &QAction::triggered, m_hexedit, &QHexEdit::redo);

    QVBoxLayout *layout;
    layout = new QVBoxLayout ();
    layout->addWidget (m_hexedit);

    QWidget *window = new QWidget ();
    window->setLayout (layout);
    setCentralWidget (window);
}

HexTableEdit::~HexTableEdit () {
}

void HexTableEdit::edited () {
    m_edited = true;
}

void HexTableEdit::save () {
    QByteArray ba = m_hexedit->data ();
    if (m_table->loaded ())
        delete[] m_table->data;
    m_table->data = new char[ba.size ()];
    std::copy (ba.data (), ba.data () + ba.size (), m_table->data);
    m_table->newlen = ba.size ();
    m_table->changed = false;
    m_table->td_changed = true;
    m_edited = false;
    emit update (m_table);
}

void HexTableEdit::toggleReadOnly (bool val) {
    m_hexedit->setReadOnly (val);
}

void HexTableEdit::toggleOverwrite (bool val) {
    m_hexedit->setOverwriteMode (val);
}

void HexTableEdit::setData (char *data, int len) {
    m_hexedit->setData (QByteArray (data, ((len+3)&~3)));
    m_edited = false;
    m_valid = true;
}

void HexTableEdit::resetData () {
    int len = m_table->newlen;
    m_hexedit->setData (QByteArray (m_table->data, ((len+3)&~3)));
    m_edited = false;
    m_valid = true;
}

bool HexTableEdit::checkUpdate (bool can_cancel) {
    if (isModified ()) {
        QMessageBox::StandardButton ask;
        ask = QMessageBox::question (this,
            tr ("Unsaved Changes"),
            tr ("This table has been modified. "
                "Would you like to export the changes back into the font?"),
            can_cancel ?  (QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel) :
                          (QMessageBox::Yes|QMessageBox::No));
        if (ask == QMessageBox::Cancel) {
            return false;
        } else if (ask == QMessageBox::Yes) {
            save ();
        }
    }
    return true;
}

bool HexTableEdit::isModified () {
    return m_edited;
}

bool HexTableEdit::isValid () {
    return m_valid;
}

std::shared_ptr<FontTable> HexTableEdit::table () {
    return m_table;
}

void HexTableEdit::closeEvent (QCloseEvent *event) {
    // If we are going to delete the font, ignore changes in table edits
    if (!isModified () || m_table->freeing || checkUpdate (true)) {
        m_table->tv = nullptr;
    } else {
        event->ignore ();
    }
}
