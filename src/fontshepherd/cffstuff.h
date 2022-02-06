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

#ifndef _FONSHEPHERD_CFF_STUFF_H
#define _FONSHEPHERD_CFF_STUFF_H
#include <stdint.h>
#include <array>
#include <map>
#include <vector>
#include <QtGlobal>

#include "tables/variations.h"

namespace cff {
    const int version = 0;
    const int Notice = 1;
    const int Copyright = (12<<8);
    const int FullName = 2;
    const int FamilyName = 3;
    const int Weight = 4;
    const int isFixedPitch = (12<<8)+1;
    const int ItalicAngle = (12<<8)+2;
    const int UnderlinePosition = (12<<8)+3;
    const int UnderlineThickness = (12<<8)+4;
    const int PaintType = (12<<8)+5;
    const int CharstringType = (12<<8)+6;
    const int FontMatrix = (12<<8)+7;
    const int UniqueID = 13;
    const int FontBBox = 5;
    const int StrokeWidth = (12<<8)+8;
    const int XUID = 14;
    const int charset = 15;
    const int Encoding = 16;
    const int CharStrings = 17;
    const int Private = 18;
    const int vsindex = 22;
    const int vstore = 24;
    const int SyntheticBase = (12<<8)+20;
    const int PostScript = (12<<8)+21;
    const int BaseFontName = (12<<8)+22;
    const int BaseFontBlend = (12<<8)+23;

    const int ROS = (12<<8)+30;
    const int CIDFontVersion = (12<<8)+31;
    const int CIDFontRevision = (12<<8)+32;
    const int CIDFontType = (12<<8)+33;
    const int CIDCount = (12<<8)+34;
    const int UIDBase = (12<<8)+35;
    const int FDArray = (12<<8)+36;
    const int FDSelect = (12<<8)+37;
    const int FontName = (12<<8)+38;

    const int BlueValues = 6;
    const int OtherBlues = 7;
    const int FamilyBlues = 8;
    const int FamilyOtherBlues = 9;
    const int BlueScale = (12<<8)+9;
    const int BlueShift = (12<<8)+10;
    const int BlueFuzz = (12<<8)+11;
    const int StdHW = 10;
    const int StdVW = 11;
    const int StemSnapH = (12<<8)+12;
    const int StemSnapV = (12<<8)+13;
    const int ForceBold = (12<<8)+14;
    const int ForceBoldThreshold  = (12<<8)+15; //obsolete
    const int lenIV = (12<<8)+16; // obsolete
    const int LanguageGroup = (12<<8)+17;
    const int ExpansionFactor = (12<<8)+18;
    const int initialRandomSeed = (12<<8)+19;
    const int Subrs = 19;
    const int defaultWidthX = 20;
    const int nominalWidthX = 21;
    const int blend = 23;
    const int T2 = 31;

    namespace cs {
	const int vmoveto = 0x04;
	const int hmoveto = 0x16;
	const int rmoveto = 0x15;
	const int closepath = 0x09;

	const int rlineto = 0x05;
	const int hlineto = 0x06;
	const int vlineto = 0x07;
	const int rrcurveto = 0x08;
	const int hhcurveto = 0x1b;
	const int vvcurveto = 0x1a;
	const int hvcurveto = 0x1f;
	const int vhcurveto = 0x1e;
	const int rcurveline = 0x18;
	const int rlinecurve = 0x19;

	const int hstem = 0x01;
	const int hstemhm = 0x12;
	const int vstem = 0x03;
	const int vstemhm = 0x17;
	const int hintmask = 0x13;
	const int cntrmask = 0x14;
	const int hflex = 0xc22;
	const int flex = 0xc23;
	const int hflex1 = 0xc24;
	const int flex1 = 0xc25;

	const int callsubr = 0x0a;
	const int callgsubr = 0x1d;
	const int op_return = 0x0b;
	const int endchar = 0x0e;

	const int vsindex = 0x0f;
	const int blend = 0x10;
    };

    extern const std::map<int, std::string> psPrivateEntries;
    extern const std::map<int, std::string> psTopDictEntries;
    extern const std::vector<std::string> names;
};

// Structures needed for reading/writing CFF data,
// but not directly related to our CFF table representation
// as a glyph container

enum em_private_type {
    pt_uint, pt_bool, pt_blend, pt_blend_list
};

struct private_entry {
    private_entry ();
    private_entry (const private_entry &pe);
    ~private_entry ();
    void operator = (const private_entry &pe);
    void setType (em_private_type pt);
    em_private_type type () const;
    const std::string toString () const;

    union {
	uint32_t i;
	bool b;
	struct blend n;
	std::array<blend, 16> list;
    };

private:
    em_private_type ptype;
};

struct cff_sid {
    uint32_t sid;
    std::string str;
};

struct size_off {
    uint32_t size, offset;
};

struct ros_info {
    cff_sid registry, order;
    uint16_t supplement;
};

enum em_dict_entry_type {
    dt_uint, dt_bool, dt_float, dt_list, dt_sid, dt_size_off, dt_ros
};

struct top_dict_entry {
    top_dict_entry ();
    top_dict_entry (const top_dict_entry &de);
    ~top_dict_entry ();
    void operator = (const top_dict_entry &de);
    void setType (em_dict_entry_type dt);
    em_dict_entry_type type () const;
    const std::string toString () const;

    union {
	uint32_t i;
	bool b;
	double f;
	std::vector<double> list;
	cff_sid sid;
	struct size_off so;
	struct ros_info ros;
    };

private:
    em_dict_entry_type de_type;
};

template <class T1, class T2>
class PseudoMap {
public:
    PseudoMap ();
    PseudoMap (const PseudoMap &other);

    size_t size ();
    bool has_key (T1 key) const;
    void reserve (size_t cap);
    const T2 & get (T1 key) const;
    void set_value (T1 key, T2 val);
    std::pair<T1, T2> &by_idx (size_t idx);
    void erase (T1 key);
    void clear ();

    const T2 & operator [](T1 key) const;
    T2 & operator [](T1 key);
    PseudoMap & operator = (const PseudoMap &other);

private:
    std::vector<std::pair<T1, T2>> m_list;
};

typedef PseudoMap<int, private_entry> PrivateDict;
typedef PseudoMap<int, top_dict_entry> TopDict;

struct charstring {
    std::string sdata;
    uint8_t hintcnt;

    charstring () : hintcnt (0) {};
    charstring (const std::string &s) : sdata (s), hintcnt (0) {};
    charstring (char *data, int len) : sdata (data, len), hintcnt (0) {};
};

struct pschars {
    int cnt, bias;
    std::vector<struct charstring> css;
};

typedef struct cffcontext {
    double version;
    int painttype;
    int hint_cnt;
    struct variation_store &vstore;
    const struct pschars &gsubrs;
    const struct pschars &lsubrs;
    const PrivateDict &pdict;
} PSContext;

struct cff_font {
    std::string fontname;
    TopDict top_dict;
    PrivateDict private_dict;
    std::vector<std::string> strings;
    struct pschars glyphs, local_subrs;
    uint8_t csformat;
    std::vector<uint16_t> charset;
    std::vector<uint16_t> fdselect;
    struct variation_store vstore;

    std::vector<struct cff_font> subfonts;
};

#endif
