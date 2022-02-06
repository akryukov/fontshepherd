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

#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>

#include "sfnt.h"
#include "tables.h"
#include "tables/cmap.h"
#include "tables/glyphcontainer.h" // also includes splineglyph.h
#include "tables/glyf.h"
#include "tables/cff.h"
#include "editors/postedit.h"
#include "tables/glyphnames.h"
#include "editors/fontview.h"
#include "fs_notify.h"
#include <QtWidgets>

std::array<std::string, 258> PostTable::macRomanNames = {
    ".notdef", ".null", "nonmarkingreturn",
    "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand", "quotesingle",
    "parenleft", "parenright", "asterisk", "plus", "comma", "hyphen", "period", "slash",
    "zero", "one", "two", "three", "four", "five", "six", "seven",
    "eight", "nine", "colon", "semicolon", "less", "equal", "greater", "question",
    "at", "A", "B", "C", "D", "E", "F", "G",
    "H", "I", "J", "K", "L", "M", "N", "O",
    "P", "Q", "R", "S", "T", "U", "V", "W",
    "X", "Y", "Z", "bracketleft", "backslash", "bracketright", "asciicircum", "underscore",
    "grave", "a", "b", "c", "d", "e", "f", "g",
    "h", "i", "j", "k", "l", "m", "n", "o",
    "p", "q", "r", "s", "t", "u", "v", "w",
    "x", "y", "z", "braceleft", "bar", "braceright", "asciitilde",
    "Adieresis", "Aring", "Ccedilla", "Eacute", "Ntilde", "Odieresis", "Udieresis", "aacute",
    "agrave", "acircumflex", "adieresis", "atilde", "aring", "ccedilla", "eacute", "egrave",
    "ecircumflex", "edieresis", "iacute", "igrave", "icircumflex", "idieresis", "ntilde", "oacute",
    "ograve", "ocircumflex", "odieresis", "otilde", "uacute", "ugrave", "ucircumflex", "udieresis",
    "dagger", "degree", "cent", "sterling", "section", "bullet", "paragraph", "germandbls",
    "registered", "copyright", "trademark", "acute", "dieresis", "notequal", "AE", "Oslash",
    "infinity", "plusminus", "lessequal", "greaterequal", "yen", "mu", "partialdiff", "summation",
    "product", "pi", "integral", "ordfeminine", "ordmasculine", "Omega", "ae", "oslash",
    "questiondown", "exclamdown", "logicalnot", "radical", "florin", "approxequal", "Delta", "guillemotleft",
    "guillemotright", "ellipsis", "nonbreakingspace", "Agrave", "Atilde", "Otilde", "OE", "oe",
    "endash", "emdash", "quotedblleft", "quotedblright", "quoteleft", "quoteright", "divide", "lozenge",
    "ydieresis", "Ydieresis", "fraction", "currency", "guilsinglleft", "guilsinglright", "fi", "fl",
    "daggerdbl", "periodcentered", "quotesinglbase", "quotedblbase", "perthousand", "Acircumflex", "Ecircumflex", "Aacute",
    "Edieresis", "Egrave", "Iacute", "Icircumflex", "Idieresis", "Igrave", "Oacute", "Ocircumflex",
    "apple", "Ograve", "Uacute", "Ucircumflex", "Ugrave", "dotlessi", "circumflex", "tilde",
    "macron", "breve", "dotaccent", "ring", "cedilla", "hungarumlaut", "ogonek", "caron",
    "Lslash", "lslash", "Scaron", "scaron", "Zcaron", "zcaron", "brokenbar", "Eth",
    "eth", "Yacute", "yacute", "Thorn", "thorn", "minus", "multiply", "onesuperior",
    "twosuperior", "threesuperior", "onehalf", "onequarter", "threequarters", "franc", "Gbreve", "gbreve",
    "Idotaccent", "Scedilla", "scedilla", "Cacute", "cacute", "Ccaron", "ccaron", "dcroat"
};

PostTable::PostTable (sfntFile *fontfile, const TableHeader &props) :
    FontTable (fontfile, props) {
    contents = {};
}

PostTable::~PostTable () {
}

void PostTable::unpackData (sFont*) {
    uint32_t pos = 0;

    contents.version = getvfixed (pos); pos+=4;
    contents.italicAngle = getfixed (pos); pos+=4;
    contents.underlinePosition = getushort (pos); pos+=2;
    contents.underlineThickness = getushort (pos); pos+=2;
    contents.isFixedPitch = getlong (pos); pos+=4;
    contents.minMemType42 = getlong (pos); pos+=4;
    contents.maxMemType42 = getlong (pos); pos+=4;
    contents.minMemType1 = getlong (pos); pos+=4;
    contents.maxMemType1 = getlong (pos); pos+=4;

    if (contents.version > 1 && contents.version < 3) {
        contents.numberOfGlyphs = getushort (pos); pos+=2;
    }
    if (contents.version == 1.0) {
        for (int i=0; i<258; i++)
	    m_glyphNames[i] = macRomanNames[i];
    } else if (contents.version == 2.0) {
        uint16_t i, idx, maxidx=0, numberNewGlyphs=0;
	std::vector<uint16_t> glyphNameIndex;
	std::vector<std::string> names;
	glyphNameIndex.reserve (contents.numberOfGlyphs);
	names.reserve (contents.numberOfGlyphs);
	m_glyphNames.resize (contents.numberOfGlyphs);

        for (i=0; i<contents.numberOfGlyphs; i++) {
            idx = getushort (pos);
	    if (idx > maxidx) maxidx = idx;
            glyphNameIndex.push_back (idx);
            pos+=2;
        }
	/* NB: numberNewGlyphs should not necessarily be equal to the number
	 * of references to glyph names, listed previously (in AcademyOSTT:
	 * .notdef is present in the name list, but not referenced) */
	if ((maxidx - 257) > 0)
	    numberNewGlyphs = maxidx - 257;
        for (i=0; i<numberNewGlyphs; i++) {
            uint8_t len = data[pos]; pos++;
	    auto gn = std::string (data + pos, len);
            names.push_back (gn);
	    pos+=len;
        }
	for (i=0; i<contents.numberOfGlyphs; i++) {
	    if (glyphNameIndex[i] < 258)
		m_glyphNames[i] = macRomanNames[glyphNameIndex[i]];
	    else
		m_glyphNames[i] = names[glyphNameIndex[i]-258];
	}
    } else if (contents.version == 2.5) {
        uint16_t i;
        for (i=0; i<contents.numberOfGlyphs; i++) {
	    int8_t shift = data[pos+i];
	    int idx = i + shift;
	    m_glyphNames[i] = macRomanNames[idx];
	}
    }
}

void PostTable::packData () {
    std::ostringstream s;
    std::string st;
    std::vector<int> glyph_ids;
    glyph_ids.resize (m_glyphNames.size ());

    delete[] data; data = nullptr;
    putfixed (s, contents.version);
    putfixed (s, contents.italicAngle);
    putushort (s, contents.underlinePosition);
    putushort (s, contents.underlineThickness);
    putlong (s, contents.isFixedPitch);
    putlong (s, contents.minMemType42);
    putlong (s, contents.maxMemType42);
    putlong (s, contents.minMemType1);
    putlong (s, contents.maxMemType1);
    if (contents.version == 2.0) {
	putushort (s, contents.numberOfGlyphs);
	size_t i, idx=0;
	for (i=0; i<m_glyphNames.size ();  i++) {
	    std::string &name = m_glyphNames[i];
	    int j;
	    for (j=0; j<258; j++) {
		if (name == macRomanNames[j]) {
		    glyph_ids[i] = j;
		    break;
		}
	    }
	    if (j==258) {
		glyph_ids[i] = 258+idx;
		idx++;
	    }
	    putushort (s, glyph_ids[i]);
	}
	for (i=0; i<m_glyphNames.size ();  i++) {
	    if (glyph_ids[i] >= 258) {
		std::string &name = m_glyphNames[i];
		s.put ((uint8_t) name.length ());
		s << name;
	    }
	}
    }
    changed = false;
    td_changed = true;
    start = 0xffffffff;

    st = s.str ();
    newlen = st.length ();
    data = new char[newlen];
    std::copy (st.begin (), st.end (), data);
}

void PostTable::edit (sFont* fnt, QWidget* caller) {
    if (data == nullptr)
        fillup ();

    if (tv == nullptr) {
	unpackData (fnt);
        PostEdit *postedit = new PostEdit (this, fnt, caller);
        tv = postedit;
	FontView *fv = caller->findChild<FontView *> ();
	if (fv)
	    QObject::connect (postedit, &PostEdit::glyphNamesChanged, fv, &FontView::updateGlyphNames);
        postedit->show ();
    } else {
        tv->raise ();
    }
}

std::string PostTable::glyphName (uint16_t gid) {
    if (gid < m_glyphNames.size ())
	return m_glyphNames[gid];
    return "";
}

double PostTable::version () const {
    return contents.version;
}

double PostTable::setVersion (double val, GlyphNameProvider *gnp) {
    if (val == 1.0 && gnp->countGlyphs () != 258) {
        FontShepherd::postWarning (
            QCoreApplication::tr ("Setting 'post' table version"),
            QCoreApplication::tr ("This font doesn't contain exactly "
                "258 glyphs, so it is not compatible with 'post' table version 1.0. "
		"I will use version 2.0 instead"),
            container->parent ());
	val = 2.0;
    } else if (val != 2.0 && val != 3.0) {
        FontShepherd::postWarning (
            QCoreApplication::tr ("Setting 'post' table version"),
            QCoreApplication::tr ("Saving 'post' table version %1 not supported. "
		"I will use version 2.0 instead").arg (val),
            container->parent ());
	val = 2.0;
    }
    if (val == contents.version)
	return val;

    contents.numberOfGlyphs = gnp->countGlyphs ();
    if (val == 1.0) {
	m_glyphNames.resize (258);
        for (int i=0; i<258; i++)
	    m_glyphNames[i] = macRomanNames[i];
    } else if (val == 3.0) {
	m_glyphNames.resize (0);
    } else if (val == 2.0) {
	m_glyphNames.resize (contents.numberOfGlyphs);
	for (size_t i=0; i<contents.numberOfGlyphs; i++) {
	    m_glyphNames[i] = gnp->nameByGid (i);
	}
    }
    contents.version = val;
    return val;
}

double PostTable::italicAngle () const {
    return contents.italicAngle;
}

int16_t PostTable::underlinePosition () const {
    return contents.underlinePosition;
}

int16_t PostTable::underlineThickness () const {
    return contents.underlineThickness;
}

bool PostTable::isFixedPitch () const {
    return (contents.isFixedPitch > 0);
}

uint32_t PostTable::minMemType42 () const {
    return contents.minMemType42;
}

uint32_t PostTable::maxMemType42 () const {
    return contents.maxMemType42;
}

uint32_t PostTable::minMemType1 () const {
    return contents.minMemType1;
}

uint32_t PostTable::maxMemType1 () const {
    return contents.maxMemType1;
}

uint16_t PostTable::numberOfGlyphs () const {
    return contents.numberOfGlyphs;
}

void PostTable::setGlyphName (uint16_t gid, const std::string &name) {
    if (contents.version == 2.0) {
	if (gid >= m_glyphNames.size ()) {
	    m_glyphNames.resize (gid+1);
	    contents.numberOfGlyphs = gid+1;
	}
	m_glyphNames[gid] = name;
    }
}

GlyphNameProvider::GlyphNameProvider (sFont &fnt) :
    m_font (fnt), m_post (nullptr), m_cff (nullptr), m_enc (nullptr) {
    std::string aglfn_path = SHAREDIR;
    aglfn_path += "agl/aglfn.txt";
    std::string gl_path = SHAREDIR;
    gl_path += "agl/glyphlist.txt";

    m_cff  = dynamic_cast<CffTable *> (fnt.table (CHR ('C','F','F',' ')));
    m_post = dynamic_cast<PostTable *> (fnt.table (CHR ('p','o','s','t')));
    if (m_post) {
        m_post->fillup ();
        m_post->unpackData (&fnt);
    }
    if (m_cff) {
        m_cff->fillup ();
        m_cff->unpackData (&fnt);
    }
    if (fnt.enc) m_enc = fnt.enc;

    parseAglfn (aglfn_path);
    parseGlyphlist (gl_path);
}

GlyphNameProvider::~GlyphNameProvider () {
}

void GlyphNameProvider::parseAglfn (std::string &path) {
    std::ifstream infile (path.c_str ());
    uint16_t uni;
    char name[256];
    char *nameptr = name;

    std::string line;
    while (std::getline (infile, line)) {
        std::istringstream iss (line);
        if (iss.peek () == '#')
            continue;
        iss >> std::hex >> uni;
        if (iss.fail ()) break;
        if (iss.peek () == ';') iss.ignore (1);
        iss.getline (nameptr, 255, ';');
        if (iss.fail ()) break;

        by_uni[uni] = nameptr;
    }
    infile.close ();
}

void GlyphNameProvider::parseGlyphlist (std::string &path) {
    std::ifstream infile (path.c_str ());
    uint16_t uni;
    char name[256];
    char *nameptr = name;

    std::string line;
    while (std::getline (infile, line)) {
        std::istringstream iss (line);
        if (iss.peek () == '#')
            continue;
        iss.getline (nameptr, 255, ';');
        if (iss.fail ()) break;
        iss >> std::hex >> uni;
        if (iss.fail ()) break;

        by_name[nameptr] = uni;
    }
    infile.close ();
}

std::string GlyphNameProvider::nameByGid (uint16_t gid) {
    std::string ret = "";

    if (m_cff && !m_cff->cidKeyed ())
        ret = m_cff->glyphName (gid);
    else if (m_post && m_post->version () < 3)
        ret = m_post->glyphName (gid);
    else if (m_enc) {
        std::vector<uint32_t> unis = m_enc->unicode (gid);
        if (unis.size () > 0 ) {
            uint32_t uni = unis[0];

            if (by_uni.count (uni))
                ret = by_uni[uni];
            else {
                std::stringstream ss;
                ss << std::uppercase << std::hex;
                if (uni <= 0xFFFF)
                    ss << "uni" << std::setw (4) << std::setfill ('0') << uni;
                else
                    ss << "u"   << std::setw (6) << std::setfill ('0') << uni;
                ret = ss.str ();
            }
        }
    }
    if (ret.empty ()) {
	std::stringstream ss;
	ss << "glyph" << gid;
	ret = ss.str ();
    }

    return ret;
}

uint32_t GlyphNameProvider::uniByName (std::string &name) {
    uint32_t ret = 0;
    if (by_name.count (name))
        return (by_name[name]);
    else if (name.length () == 7 && sscanf (name.c_str (), "uni%04x", &ret))
	return ret;
    else if (name.length () == 7 && sscanf (name.c_str (), "u%06x", &ret))
	return ret;
    else if (name.length () == 5 && sscanf (name.c_str (), "u%04x", &ret))
	return ret;
    return 0;
}

bool GlyphNameProvider::fontHasGlyphNames () {
    return (
        (m_post && m_post->version () < 3) ||
        (m_cff && !m_cff->cidKeyed () && m_cff->version () < 2));
}

uint16_t GlyphNameProvider::countGlyphs () {
    return m_font.glyph_cnt;
}

uint32_t GlyphNameProvider::glyphNameSource () {
    if (m_cff && !m_cff->cidKeyed () && m_cff->version () < 2)
	return CHR ('C','F','F',' ');
    else if (m_post && m_post->version () < 3)
	return CHR ('p','o','s','t');
    else
	return CHR ('c','m','a','p');
}

CmapEnc* GlyphNameProvider::encoding () {
    return m_enc;
}

void GlyphNameProvider::setGlyphName (uint16_t gid, const std::string &name) {
    if (m_cff && !m_cff->cidKeyed () && m_cff->version () < 2)
	m_cff->addGlyphName (gid, name);
    if (m_post && m_post->version () == 2)
	m_post->setGlyphName (gid, name);
}

sFont &GlyphNameProvider::font () const {
    return m_font;
}

