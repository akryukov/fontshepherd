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

#include <cstring>
#include <sstream>
#include <ios>
#include <assert.h>
#include <iconv.h>

#include "sfnt.h"
#include "editors/nameedit.h"
#include "tables/name.h"
#include "fs_notify.h"
#include "exceptions.h"
#include "commonlists.h"

NameTable::NameTable (sfntFile* fontfile, const TableHeader &props) :
    FontTable (fontfile, props),
    m_version (0),
    m_names_changed (0),
    m_lang_tags_changed (0) {
}

NameTable::~NameTable () {
}

// https://stackoverflow.com/questions/1001307/detecting-endianness-programmatically-in-a-c-program
static bool is_big_endian () {
    union {
        uint32_t i;
        char c[4];
    } bint = {0x01020304};

    return bint.c[0] == 1;
}

static QString strip_null_chars (QString source) {
    QString ret = source.replace (QString (QChar (0)), QString (""));
    return ret;
}

static bool is_ascii (const std::string source) {
    for (size_t i=0; i<source.size (); i++) {
	if ((uint8_t) source[i] > 127)
	    return false;
    }
    return true;
}

static bool is_ascii (const QString source) {
    for (int i=0; i<source.length (); i++) {
	const QChar ch = source[i];
	if (ch.unicode () > 127)
	    return false;
    }
    return true;
}

QString NameTable::decodeString (std::string &uni_enc, uint16_t platformID, uint16_t encodingID, char *strptr, uint16_t len) {
    std::string cs = FontShepherd::iconvCharsetName (platformID, encodingID);
    /* Peter Constable in th opentype-list@indx.co.uk list:
     * The original intent was that, for each cmap subtable, there would
     * be corresponding 'name' records with matching platform and encoding
     * IDs — hence a 3/0 cmap subtable having corresponding 3/0 name records.
     * But, the key detail missing from the spec is that, in the case
     * of 3/0 name records, the referenced string data would be encoded
     * in UTF-16 — an exception from other 3/x cases. */
    if (cs == "SYMBOL") cs = "UTF-16BE";
    /* Unfortunately, have to use iconv even for Unicode strings, as
     * they are expected to be big endian, while QString would be
     * less endian on most systems */
    iconv_t codec = iconv_open (uni_enc.c_str (), cs.c_str ());
    std::string raw_str = std::string (strptr, len);
    size_t s_size = len, t_size = len*4;
    char source[s_size] = { 0 };
    std::copy (raw_str.begin (), raw_str.end (), source);
    char *psource = source;
    char target[t_size] = { 0 };
    char *ptarget = target;
    /* Monaco.ttf from Mac OS X distribution has some strings said to be
     * Arabic, which assumes the MacArabic codepage. In fact they are
     * in English and contain nothing but ASCII characters, which makes
     * the decoding task trivial */
    if (codec == (iconv_t)(-1) && is_ascii (raw_str))
	codec = iconv_open (uni_enc.c_str (), "US-ASCII");

    if (codec != (iconv_t)(-1)) {
        iconv (codec, &psource, &s_size, &ptarget, &t_size);
        char16_t *strptr = reinterpret_cast<char16_t *> (target);
	/* Proper decoding from CJK charsets is not guaranteed, although
	 * we attempt to do our best:
	 * - in older versions of wcl* fonts all name strings are in Big5,
	 *   but ASCII characters are padded with zero bytes, although they
	 *   are expected to be 8-bit in this encoding. That's why we
	 *   use strip_null_chars () before storing the Unicode-converted string;
	 * - in XANO-Mincho-U32 there is a full set of name strings marked
	 *   with platformID 3 (Windows) and encodingID 2 (Shift-JIS), which
	 *   are, however, Unicode-encoded. Note that according to the MS spec
	 *   "All naming table strings for the Windows platform (platform ID 3)
	 *   must be encoded in UTF-16BE", but why to mark them as if they were
	 *   Shift-JIS then?
	 * - just can't decode name strings from tt103.ttf, except ASCII
	 *   characters: looks like all bytes (not just ASCII) are padded
	 *   with zero bytes.
	 * One more problem is that the 'name' table stores string length
	 * in bytes and there is no obvious way to say how many characters
	 * they would contain for a CJK encoding. However as we convert
	 * everything to 16-bit Unicode, it seems easy to calculate the
	 * number of bytes in the resulting string and divide this value by two.
	 * Fortunately non-CJK fonts would most probably have all their name
	 * strings either in ASCII or in 16-bit Unicode. */

	iconv_close (codec);
	return strip_null_chars (QString::fromUtf16 (strptr, (len*4-t_size)/2));
    }
    FontShepherd::postWarning (
        QCoreApplication::tr ("Unsupported Encoding"),
        QCoreApplication::tr ("Warning: could not find a suitable converter "
	    "for %1.").arg (cs.c_str ()),
        this->containerFile ()->parent ());
    return QString ("");
}

void NameTable::unpackData (sFont *) {
    uint32_t fpos=0;
    uint16_t count, string_off;
    std::string uni_enc = is_big_endian () ? "UTF-16BE" : "UTF-16LE";
    if (td_loaded)
        return;

    this->fillup ();
    m_version = this->getushort (fpos); fpos += 2;
    count = this->getushort (fpos); fpos += 2;
    string_off = this->getushort (fpos); fpos += 2;
    name_records.resize (count);

    for (uint16_t i=0; i<count; i++) {
	name_records[i].platformID = this->getushort (fpos); fpos += 2;
	name_records[i].encodingID = this->getushort (fpos); fpos += 2;
	name_records[i].languageID = this->getushort (fpos); fpos += 2;
	name_records[i].nameID = this->getushort (fpos); fpos += 2;
	uint16_t len = this->getushort (fpos); fpos += 2;
	uint16_t off = this->getushort (fpos); fpos += 2;
        char *strptr = this->data+string_off+off;
	name_records[i].name = decodeString
	    (uni_enc, name_records[i].platformID, name_records[i].encodingID, strptr, len);
    }

    if (m_version > 0) {
	uint16_t lang_tag_count = this->getushort (fpos); fpos += 2;
	lang_tag_records.resize (lang_tag_count);

	uint16_t lang_id = 0x8000;
	for (uint16_t i=0; i<lang_tag_count; i++) {
	    lang_tag_records[i].ID = lang_id; lang_id++;
	    uint16_t len = this->getushort (fpos); fpos += 2;
	    uint16_t off = this->getushort (fpos); fpos += 2;
	    char *strptr = this->data+string_off+off;
	    lang_tag_records[i].language = decodeString (uni_enc, 0, 3, strptr, len);
	}
    }
    td_loaded = true;
}

std::string NameTable::encodeString (bool big_endian, uint16_t platformID, uint16_t encodingID, QString &uni_str) {
    std::string uni_enc = big_endian ? "UTF-16BE" : "UTF-16LE";
    std::string cs = FontShepherd::iconvCharsetName (platformID, encodingID);
    if (cs == "SYMBOL") cs = "UTF-16BE";
    iconv_t codec = iconv_open (cs.c_str (), uni_enc.c_str ());
    size_t s_size = uni_str.length (), t_size = uni_str.length () * 2;
    const uint16_t *uni = uni_str.utf16 ();
    uint16_t uni_source[s_size] = { 0 };
    std::copy (uni, uni+uni_str.length (), uni_source);
    char *psource = reinterpret_cast<char *> (uni_source);
    s_size *= 2;
    char target[t_size] = { 0 };
    char *ptarget = target;
    if (codec == (iconv_t)(-1) && is_ascii (uni_str))
	codec = iconv_open ("US-ASCII", uni_enc.c_str ());

    if (codec != (iconv_t)(-1)) {
        iconv (codec, &psource, &s_size, &ptarget, &t_size);
        return std::string (target, (uni_str.size () * 2-t_size));
    }
    FontShepherd::postWarning (
        QCoreApplication::tr ("Unsupported Encoding"),
        QCoreApplication::tr ("Warning: could not find a suitable converter "
	    "for %1.").arg (cs.c_str ()),
        this->containerFile ()->parent ());
    return std::string (psource, uni_str.size () * 2);
}

void NameTable::packData () {
    std::ostringstream s;
    std::string st;
    bool big_endian = is_big_endian ();

    uint16_t count = name_records.size ();
    uint16_t lang_tag_count = lang_tag_records.size ();
    uint16_t format = (lang_tag_count > 0);
    uint16_t off = sizeof (uint16_t)*3 + sizeof (uint16_t)*6 * count;
    std::vector<std::string> encoded;
    std::vector<uint16_t> str_off;
    encoded.reserve (count);
    str_off.reserve (count);
    uint16_t prev_str_off = 0;

    if (format > 0)
	off += (sizeof (uint16_t) + sizeof (uint16_t)*2*lang_tag_count);
    for (uint16_t i=0; i<count; i++) {
	name_records[i].encoded_idx = -1;
	std::string enc_str = encodeString (big_endian, name_records[i].platformID, name_records[i].encodingID, name_records[i].name);
	for (uint16_t j=0; j<encoded.size (); j++) {
	    if (encoded[j] == enc_str) {
		name_records[i].encoded_idx = j;
		break;
	    }
	}
	if (name_records[i].encoded_idx < 0) {
	    name_records[i].encoded_idx = encoded.size ();
	    encoded.push_back (enc_str);
	    str_off.push_back (prev_str_off);
	    prev_str_off += enc_str.length ();
	}
    }

    delete[] data; data = nullptr;
    putushort (s, format);
    putushort (s, count);
    putushort (s, off);
    for (uint16_t i=0; i<count; i++) {
	name_record &rec = name_records[i];
	std::string &es = encoded[rec.encoded_idx];
	putushort (s, rec.platformID);
	putushort (s, rec.encodingID);
	putushort (s, rec.languageID);
	putushort (s, rec.nameID);
	putushort (s, es.length ());
	putushort (s, str_off[rec.encoded_idx]);
    }

    if (format > 0) {
	uint16_t loff = 0;
	putushort (s, lang_tag_count);
	for (auto s: encoded)
	    loff += s.length ();
	for (uint16_t i=0; i<lang_tag_count; i++) {
	    putushort (s, lang_tag_records[i].language.length ()*2);
	    putushort (s, loff);
	    loff += lang_tag_records[i].language.length ()*2;
	}
    }
    for (size_t i=0; i<encoded.size (); i++)
	s.write (encoded[i].c_str (), encoded[i].length ());
    if (format > 0) {
	for (uint16_t i=0; i<lang_tag_count; i++) {
	    std::string enc_l = encodeString (big_endian, 0, 3, lang_tag_records[i].language);
	    s.write (enc_l.c_str (), enc_l.length ());
	}
    }

    changed = false;
    m_names_changed = false;
    m_lang_tags_changed = false;
    td_changed = true;
    start = 0xffffffff;

    st = s.str ();
    newlen = st.length ();
    data = new char[newlen];
    std::copy (st.begin (), st.end (), data);
}

void NameTable::edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller) {
    if (data == nullptr)
        fillup ();

    if (tv == nullptr) {
	unpackData (fnt);
        NameEdit *nameedit = new NameEdit (tptr, fnt, caller);
        tv = nameedit;
        nameedit->show ();
    } else {
        tv->raise ();
    }
}

uint16_t NameTable::version () {
    return m_version;
}

uint16_t NameTable::numNameRecords () const {
    return name_records.size ();
}

name_record *NameTable::nameRecord (int index) {
    if (index >=0 && (uint16_t) index < name_records.size ())
	return &name_records[index];
    return nullptr;
}

void NameTable::removeNameRecord (int index) {
    if (index >=0 && (uint16_t) index < name_records.size ())
	name_records.erase (name_records.begin () + index);
}

void NameTable::setNameString (int index, QString text) {
    if (index >=0 && (uint16_t) index < name_records.size ())
	name_records[index].name = text;
}

int NameTable::insertNameRecord (name_record rec) {
    size_t i;
    for (i=0; i<name_records.size () && rec > name_records[i]; i++);
    if (i<name_records.size () && rec.samePos (name_records[i]))
	return -1;
    name_records.insert (name_records.begin () + i, rec);
    return i;
}

bool NameTable::checkNameRecord (const name_record rec, int *pos) {
    size_t i;
    for (i=0; i<name_records.size () && rec > name_records[i]; i++);
    *pos = i;
    if (i<name_records.size () && rec.samePos (name_records[i]))
	return false;
    return true;
}

QString NameTable::bestName (uint16_t nameID, QString name_dflt) {
    int bestval=0;
    name_record *best=nullptr;

    for (uint16_t i=0; i<name_records.size (); i++) {
        name_record *cur = &name_records[i];
        if (cur->nameID == nameID) {
	    // Unicode platform with unspecified language should be the best choice
	    if (cur->platformID == FontShepherd::plt_unicode && cur->languageID == 0xFFFF) {
		bestval = 6;
		best = cur;
	    // Language 0 may refer to a custom language in the 'ldef' table,
	    // but most probably it would be also unspecified
	    } else if (cur->platformID == FontShepherd::plt_unicode && cur->languageID == 0 && bestval < 5) {
		bestval = 5;
		best = cur;
	    // English (US) for MS platform is the most common choice, of course
	    } else if (cur->platformID == FontShepherd::plt_windows && cur->languageID == 0x409 && bestval < 4) {
		bestval = 4;
		best = cur;
	    // English (US) for Mac will also do
	    } else if (cur->platformID == FontShepherd::plt_mac && cur->languageID == 0 && bestval < 3) {
		bestval = 3;
		best = cur;
	    // Is there a non-English entry for the Windows platform?
	    } else if (cur->platformID == FontShepherd::plt_windows && bestval < 2) {
		bestval = 2;
		best = cur;
	    // Take any entry available, if there is one
	    } else if (bestval < 1) {
		bestval = 1;
		best = cur;
	    }
	}
    }
    if (best)
	return best->name;
    return name_dflt;
}

const std::vector<FontShepherd::numbered_string> &NameTable::nameList () const {
    return FontShepherd::nameIDs;
}

uint16_t NameTable::numLangTags () const {
    return lang_tag_records.size ();
}

QString NameTable::langTagRecord (int index) const {
    if (index >=0 && (uint16_t) index < lang_tag_records.size ())
	return lang_tag_records[index].language;
    return QString ("Undefined");
}

void NameTable::clearLangTags () {
    lang_tag_records.clear ();
    for (int i=name_records.size ()-1; i>=0; i--) {
	if (name_records[i].languageID >= 0x8000)
	    name_records.erase (name_records.begin () + i);
    }
}

void NameTable::clearCustomLangTagDependent () {
    for (int i=name_records.size ()-1; i>=0; i--) {
	if (name_records[i].languageID >= 0x8000)
	    name_records.erase (name_records.begin () + i);
    }
}

void NameTable::setLangTag (uint16_t idx, QString name) {
    int val = idx - 0x8000;
    if (val >= 0 && (uint16_t) val < lang_tag_records.size ())
	lang_tag_records[idx].language = name;
}

void NameTable::removeLangTag (uint16_t idx) {
    int val = idx - 0x8000;
    if (val >= 0 && (uint16_t) val < lang_tag_records.size ()) {
	lang_tag_records.erase (lang_tag_records.begin () + val);
	updateCustomLangIDs (val);
    }
}

int NameTable::insertLangTag (QString name, int row) {
    lang_tag_record rec = {name, 0};
    for (size_t i=0; i<lang_tag_records.size (); i++) {
	if (name == lang_tag_records[i].language)
	    return -1;
    }
    lang_tag_records.insert (lang_tag_records.begin () + row, rec);
    updateCustomLangIDs (row);
    return row;
}

bool NameTable::checkLangTag (QString name) const {
    for (size_t i=0; i<lang_tag_records.size (); i++) {
	if (name == lang_tag_records[i].language)
	    return false;
    }
    return true;
}

void NameTable::setNamesModified (bool val) {
    m_names_changed = val;
}

void NameTable::setLangTagsModified (bool val) {
    m_lang_tags_changed = val;
}

static bool rcomp_lang_tags_by_lang
    (const lang_tag_record r1, const lang_tag_record r2) {
    return (r1.language < r2.language);
}

static bool rcomp_lang_tags_by_ID
    (const lang_tag_record r1, const lang_tag_record r2) {
    return (r1.ID < r2.ID);
}

void NameTable::sortLangTags () {
    std::sort (lang_tag_records.begin (), lang_tag_records.end (), rcomp_lang_tags_by_lang);
    updateCustomLangIDs (0);
}

void NameTable::setLangTagOrder (QList<QString> &order) {
    for (int i=0; i<order.size (); i++) {
	for (size_t j=0; j<lang_tag_records.size (); j++) {
	    if (order[i] == lang_tag_records[j].language) {
		lang_tag_records[j].ID = i+0x80000;
		break;
	    }
	}
    }
    for (size_t i=0; i<name_records.size (); i++) {
	name_record &rec = name_records[i];
	if (rec.languageID >= 0x8000) {
	    for (size_t j=0; j<lang_tag_records.size (); j++) {
		if (rec.languageID == j+0x8000)
		    rec.languageID = lang_tag_records[j].ID;
	    }
	}
    }
    std::sort (lang_tag_records.begin (), lang_tag_records.end (), rcomp_lang_tags_by_ID);
}

bool NameTable::namesModified () const {
    return m_names_changed;
}

bool NameTable::langTagsModified () const {
    return m_lang_tags_changed;
}

void NameTable::updateCustomLangIDs (int row) {
    for (size_t i=0; i<name_records.size (); i++) {
	name_record &rec = name_records[i];
	if (rec.languageID >= 0x8000 + row) {
	    for (size_t j=row; j<lang_tag_records.size (); j++) {
		if (rec.languageID == lang_tag_records[j].ID)
		    rec.languageID = j+0x8000;
	    }
	}
    }
    for (size_t i=row; i<lang_tag_records.size (); i++)
	lang_tag_records[i].ID = 0x8000 + row;
}

std::string name_record::strPlatform () {
    std::ostringstream s;
    const std::vector<FontShepherd::numbered_string> &lst = FontShepherd::platforms;
    for (size_t i=0; i<lst.size (); i++) {
	if (platformID == lst[i].ID) {
	    s << lst[i].ID << ": " << lst[i].name;
	    return s.str ();
	}
    }
    s << "Unknown platform: " << platformID;
    return s.str ();
}

std::string name_record::strEncoding () {
    std::ostringstream s;
    const std::vector<FontShepherd::numbered_string> &lst = FontShepherd::specificList (platformID);
    for (size_t i=0; i<lst.size (); i++) {
	if (encodingID == lst[i].ID) {
	    s << lst[i].ID << ": " << lst[i].name;
	    return s.str ();
	}
    }
    s << "Unknown encoding: " << encodingID;
    return s.str ();
}

std::string name_record::strLanguage () {
    std::ostringstream s;
    // Macintosh
    if (platformID == 1) {
	const std::vector<FontShepherd::numbered_string> &lst = FontShepherd::macLanguages;
	for (size_t i=0; i<lst.size (); i++) {
	    if (languageID == lst[i].ID) {
		s << lst[i].name;
		return s.str ();
	    }
	}
    // Windows
    } else if (platformID == 3) {
	const std::vector<FontShepherd::ms_language> &lst = FontShepherd::windowsLanguages;
	for (size_t i=0; i<lst.size (); i++) {
	    if (languageID == lst[i].code) {
		s << lst[i].language << " (" << lst[i].region << ")";
		return s.str ();
	    }
	}
    }
    s << "Unknown language: " << languageID;
    return s.str ();
}

std::string name_record::nameDescription () {
    std::ostringstream s;
    const std::vector<FontShepherd::numbered_string> &lst = FontShepherd::nameIDs;
    for (size_t i=0; i<lst.size (); i++) {
	if (nameID == lst[i].ID) {
	    s << lst[i].ID << ": " << lst[i].name;
	    return s.str ();
	}
    }
    if (nameID >= 256)
	s << "Font-specific name: " << nameID;
    else
	s << "Undefined name: " << nameID;
    return s.str ();
}

bool name_record::samePos (const name_record &rhs) const {
    return
	(platformID == rhs.platformID && encodingID == rhs.encodingID &&
	 languageID == rhs.languageID && nameID == rhs.nameID);
}

bool name_record::operator< (const name_record &rhs) const {
    return
	(platformID < rhs.platformID ||
	(platformID == rhs.platformID && encodingID <  rhs.encodingID) ||
	(platformID == rhs.platformID && encodingID == rhs.encodingID && languageID <  rhs.languageID) ||
	(platformID == rhs.platformID && encodingID == rhs.encodingID && languageID == rhs.languageID && nameID < rhs.nameID));
}

bool name_record::operator> (const name_record &rhs) const {
    return
	(platformID > rhs.platformID ||
	(platformID == rhs.platformID && encodingID >  rhs.encodingID) ||
	(platformID == rhs.platformID && encodingID == rhs.encodingID && languageID >  rhs.languageID) ||
	(platformID == rhs.platformID && encodingID == rhs.encodingID && languageID == rhs.languageID && nameID > rhs.nameID));
}

NameProxy::NameProxy (NameTable *name) :
    NameTable (nullptr, {nullptr, 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}), m_name (name) {
}

void NameProxy::update (std::vector<FontShepherd::numbered_string> name_list) {
    m_name_list = name_list;
    name_records.clear ();
    for (uint16_t i=0; i<m_name->numNameRecords (); i++) {
	name_record *rec = m_name->nameRecord (i);
	for (size_t j=0; j<m_name_list.size (); j++) {
	    if (m_name_list[j].ID != 0xFFFF && rec->nameID == name_list[j].ID)
		name_records.push_back (*rec);
	}
    }
}

void NameProxy::flush () {
    for (size_t i=0; i<name_records.size (); i++) {
	int idx;
	bool can_insert = m_name->checkNameRecord (name_records[i], &idx);
	if (can_insert) {
	    m_name->insertNameRecord (name_records[i]);
	} else {
	    name_record *nr = m_name->nameRecord (idx);
	    nr->name = name_records[i].name;
	}
    }
}

const std::vector<FontShepherd::numbered_string> &NameProxy::nameList () const {
    return m_name_list;
}
