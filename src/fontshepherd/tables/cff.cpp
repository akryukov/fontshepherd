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

#include <cstring>
#include <iostream>
#include <iomanip>
#include <assert.h>
#include <ctype.h>
#include <boost/pool/object_pool.hpp>

#include "exceptions.h"
#include "sfnt.h"
#include "editors/fontview.h" // Includes also tables.h
#include "tables/glyphcontainer.h"
#include "tables/cff.h"
#include "tables/cmap.h"
#include "tables/mtx.h"
#include "fs_notify.h"
#include "fs_math.h"
#include "tables/maxp.h"
#include "tables/name.h"
#include "tables/os_2.h"
#include "tables/head.h"

#define CS_DEBUG 0

namespace cff {
    const std::map<int, std::string> psPrivateEntries = {
	{ BlueValues , "BlueValues" },
	{ OtherBlues, "OtherBlues" },
	{ FamilyBlues, "FamilyBlues" },
	{ FamilyOtherBlues, "FamilyOtherBlues" },
	{ StdHW, "StdHW" },
	{ StdVW, "StdVW" },
	{ Subrs, "Subrs" },
	{ defaultWidthX, "defaultWidthX" },
	{ nominalWidthX, "nominalWidthX" },
	{ BlueScale, "BlueScale" },
	{ BlueShift, "BlueShift" },
	{ BlueFuzz, "BlueFuzz" },
	{ StemSnapH, "StemSnapH" },
	{ StemSnapV, "StemSnapV" },
	{ ForceBold, "ForceBold" },
	{ ForceBoldThreshold, "ForceBoldThreshold" },
	{ LanguageGroup, "LanguageGroup" },
	{ ExpansionFactor, "ExpansionFactor" },
	{ initialRandomSeed, "initialRandomSeed" },
    };

    const std::map<int, std::string> psTopDictEntries = {
	{ version, "version" },
	{ Notice, "Notice" },
	{ FullName, "FullName" },
	{ FamilyName, "FamilyName" },
	{ Weight, "Weight" },
	{ FontBBox, "FontBBox" },
	{ UniqueID, "UniqueID" },
	{ XUID, "XUID" },
	{ charset, "charset" },
	{ Encoding, "Encoding" },
	{ CharStrings, "CharStrings" },
	{ Private, "Private" },
	{ vsindex, "vsindex" },
	{ vstore, "vstore" },
	{ Copyright, "Copyright" },
	{ isFixedPitch, "isFixedPitch" },
	{ ItalicAngle, "ItalicAngle" },
	{ UnderlinePosition, "UnderlinePosition" },
	{ UnderlineThickness, "UnderlineThickness" },
	{ PaintType, "PaintType" },
	{ CharstringType, "CharstringType" },
	{ FontMatrix, "FontMatrix" },
	{ StrokeWidth, "StrokeWidth" },
	{ SyntheticBase, "SyntheticBase" },
	{ PostScript, "PostScript" },
	{ BaseFontName, "BaseFontName" },
	{ BaseFontBlend, "BaseFontBlend" },
	{ ROS, "ROS" },
	{ CIDFontVersion, "CIDFontVersion" },
	{ CIDFontRevision, "CIDFontRevision" },
	{ CIDFontType, "CIDFontType" },
	{ CIDCount, "CIDCount" },
	{ UIDBase, "UIDBase" },
	{ FDArray, "FDArray" },
	{ FDSelect, "FDSelect" },
	{ FontName, "FontName" },
    };

    const std::vector<std::string> names {
	".notdef", "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand",
	"quoteright", "parenleft", "parenright", "asterisk", "plus", "comma", "hyphen", "period",
	"slash", "zero", "one", "two", "three", "four", "five", "six",
	"seven", "eight", "nine", "colon", "semicolon", "less", "equal", "greater",
	"question", "at",  "A", "B", "C", "D", "E", "F",
	"G", "H", "I", "J", "K", "L", "M", "N",
	"O", "P", "Q", "R", "S", "T", "U", "V",
	"W", "X", "Y", "Z", "bracketleft", "backslash", "bracketright", "asciicircum",
	"underscore", "quoteleft", "a", "b", "c", "d", "e", "f",
	"g", "h", "i", "j", "k", "l", "m", "n",
	"o", "p", "q", "r", "s", "t", "u", "v",
	"w", "x", "y", "z", "braceleft", "bar", "braceright", "asciitilde",
	"exclamdown", "cent", "sterling", "fraction", "yen", "florin", "section", "currency",
	"quotesingle", "quotedblleft", "guillemotleft", "guilsinglleft", "guilsinglright", "fi", "fl", "endash",
	"dagger", "daggerdbl", "periodcentered", "paragraph", "bullet", "quotesinglbase", "quotedblbase", "quotedblright",
	"guillemotright", "ellipsis", "perthousand", "questiondown", "grave", "acute", "circumflex", "tilde",
	"macron", "breve", "dotaccent", "dieresis", "ring", "cedilla", "hungarumlaut", "ogonek",
	"caron", "emdash", "AE", "ordfeminine", "Lslash", "Oslash", "OE", "ordmasculine",
	"ae", "dotlessi", "lslash", "oslash", "oe", "germandbls", "onesuperior", "logicalnot",
	"mu", "trademark", "Eth", "onehalf", "plusminus", "Thorn", "onequarter", "divide",
	"brokenbar", "degree", "thorn", "threequarters", "twosuperior", "registered", "minus", "eth",
	"multiply", "threesuperior", "copyright", "Aacute", "Acircumflex", "Adieresis", "Agrave", "Aring",
	"Atilde", "Ccedilla", "Eacute", "Ecircumflex", "Edieresis", "Egrave", "Iacute", "Icircumflex",
	"Idieresis", "Igrave", "Ntilde", "Oacute", "Ocircumflex", "Odieresis", "Ograve", "Otilde",
	"Scaron", "Uacute", "Ucircumflex", "Udieresis", "Ugrave", "Yacute", "Ydieresis", "Zcaron",
	"aacute", "acircumflex", "adieresis", "agrave", "aring", "atilde", "ccedilla", "eacute",
	"ecircumflex", "edieresis", "egrave", "iacute", "icircumflex", "idieresis", "igrave", "ntilde",
	"oacute", "ocircumflex", "odieresis", "ograve", "otilde", "scaron", "uacute", "ucircumflex",
	"udieresis", "ugrave", "yacute", "ydieresis", "zcaron", "exclamsmall", "Hungarumlautsmall", "dollaroldstyle",
	"dollarsuperior", "ampersandsmall", "Acutesmall", "parenleftsuperior", "parenrightsuperior", "twodotenleader", "onedotenleader",
	"zerooldstyle", "oneoldstyle", "twooldstyle", "threeoldstyle", "fouroldstyle", "fiveoldstyle", "sixoldstyle", "sevenoldstyle",
	"eightoldstyle", "nineoldstyle", "commasuperior", "threequartersemdash", "periodsuperior", "questionsmall", "asuperior", "bsuperior",
	"centsuperior", "dsuperior", "esuperior", "isuperior", "lsuperior", "msuperior", "nsuperior", "osuperior",
	"rsuperior", "ssuperior", "tsuperior", "ff", "ffi", "ffl", "parenleftinferior", "parenrightinferior",
	"Circumflexsmall", "hyphensuperior", "Gravesmall", "Asmall", "Bsmall", "Csmall", "Dsmall", "Esmall",
	"Fsmall", "Gsmall", "Hsmall", "Ismall", "Jsmall", "Ksmall", "Lsmall", "Msmall",
	"Nsmall", "Osmall", "Psmall", "Qsmall", "Rsmall", "Ssmall", "Tsmall", "Usmall",
	"Vsmall", "Wsmall", "Xsmall", "Ysmall", "Zsmall", "colonmonetary", "onefitted", "rupiah",
	"Tildesmall", "exclamdownsmall", "centoldstyle", "Lslashsmall", "Scaronsmall", "Zcaronsmall", "Dieresissmall", "Brevesmall",
	"Caronsmall", "Dotaccentsmall", "Macronsmall", "figuredash", "hypheninferior", "Ogoneksmall", "Ringsmall", "Cedillasmall",
	"questiondownsmall", "oneeighth", "threeeighths", "fiveeighths", "seveneighths", "onethird", "twothirds", "zerosuperior",
	"foursuperior", "fivesuperior", "sixsuperior", "sevensuperior", "eightsuperior", "ninesuperior", "zeroinferior", "oneinferior",
	"twoinferior", "threeinferior", "fourinferior", "fiveinferior", "sixinferior", "seveninferior", "eightinferior", "nineinferior",
	"centinferior", "dollarinferior", "periodinferior", "commainferior", "Agravesmall", "Aacutesmall", "Acircumflexsmall", "Atildesmall",
	"Adieresissmall", "Aringsmall", "AEsmall", "Ccedillasmall", "Egravesmall", "Eacutesmall", "Ecircumflexsmall", "Edieresissmall",
	"Igravesmall", "Iacutesmall", "Icircumflexsmall", "Idieresissmall", "Ethsmall", "Ntildesmall", "Ogravesmall", "Oacutesmall",
	"Ocircumflexsmall", "Otildesmall", "Odieresissmall", "OEsmall", "Oslashsmall", "Ugravesmall", "Uacutesmall", "Ucircumflexsmall",
	"Udieresissmall", "Yacutesmall", "Thornsmall", "Ydieresissmall",
	"001.000", "001.001", "001.002", "001.003",
	"Black", "Bold", "Book", "Light", "Medium", "Regular", "Roman", "Semibold",
    };

    const std::array<uint16_t, 256> AdobeStandardEncoding {
	0     , 0     , 0     , 0     , 0     , 0     , 0     , 0     ,
	0     , 0     , 0     , 0     , 0     , 0     , 0     , 0     ,
	0     , 0     , 0     , 0     , 0     , 0     , 0     , 0     ,
	0     , 0     , 0     , 0     , 0     , 0     , 0     , 0     ,
	0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x2019,
	0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
	0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
	0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
	0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
	0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
	0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
	0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f,
	0x2018, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
	0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f,
	0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
	0x0078, 0x0079, 0x007a, 0x007b, 0x007c, 0x007d, 0x007e, 0     ,
	0     , 0     , 0     , 0     , 0     , 0     , 0     , 0     ,
	0     , 0     , 0     , 0     , 0     , 0     , 0     , 0     ,
	0     , 0     , 0     , 0     , 0     , 0     , 0     , 0     ,
	0     , 0     , 0     , 0     , 0     , 0     , 0     , 0     ,
	0     , 0x00a1, 0x00a2, 0x00a3, 0x2044, 0x00a5, 0x0192, 0x00a7,
	0x00a4, 0x0027, 0x201c, 0x00ab, 0x2039, 0x203a, 0xfb01, 0xfb02,
	0     , 0x2013, 0x2020, 0x2021, 0x00b7, 0     , 0x00b6, 0x2022,
	0x201a, 0x201e, 0x201d, 0x00bb, 0x2026, 0x2030, 0     , 0x00bf,
	0     , 0x0060, 0x00b4, 0x02c6, 0x02dc, 0x00af, 0x02d8, 0x02d9,
	0x00a8, 0     , 0x02da, 0x00b8, 0     , 0x02dd, 0x02db, 0x02c7,
	0x2014, 0     , 0     , 0     , 0     , 0     , 0     , 0     ,
	0     , 0     , 0     , 0     , 0     , 0     , 0     , 0     ,
	0     , 0x00c6, 0     , 0x00aa, 0     , 0     , 0     , 0     ,
	0x0141, 0x00d8, 0x0152, 0x00ba, 0     , 0     , 0     , 0     ,
	0     , 0x00e6, 0     , 0     , 0     , 0x0131, 0     , 0     ,
	0x0142, 0x00f8, 0x0153, 0x00df, 0     , 0     , 0     , 0
    };
}

#if CS_DEBUG

static std::string print_ps (const std::string &s, struct pschars *gsubrs=nullptr, struct pschars *lsubrs=nullptr, int hint_cnt=0) {
    std::stringstream ss;
    std::string extr;
    int stack_size = 0, prev_num = 0;
    std::ios init (NULL);
    init.copyfmt (std::cout);

    for (size_t i=0; i<s.length (); i++) {
	uint8_t ch = s[i];
	switch (ch) {
          case cff::cs::hstem:
	    ss << "hstem ";
	    hint_cnt += stack_size/2;
	    stack_size = 0;
	    break;
	  case cff::cs::vstem:
	    ss << "vstem ";
	    hint_cnt += stack_size/2;
	    stack_size = 0;
	    break;
	  case cff::cs::vmoveto:
	    ss << "vmoveto ";
	    stack_size = 0;
	    break;
	  case cff::cs::rlineto:
	    ss << "rlineto ";
	    stack_size = 0;
	    break;
	  case cff::cs::hlineto:
	    ss << "hlineto ";
	    stack_size = 0;
	    break;
	  case cff::cs::vlineto:
	    ss << "vlineto ";
	    stack_size = 0;
	    break;
	  case cff::cs::rrcurveto:
	    ss << "rrcurveto ";
	    stack_size = 0;
	    break;
	  case cff::cs::closepath:
	    ss << "closepath ";
	    stack_size = 0;
	    break;
	  case cff::cs::callsubr:
	    if (lsubrs) {
		ss << "callsubr{";
		extr = print_ps (lsubrs->css[prev_num + lsubrs->bias].sdata, gsubrs, lsubrs, hint_cnt);
		ss << extr;
		ss << "} ";
	    } else
		ss << "callsubr ";
	    stack_size = 0;
	    break;
	  case cff::cs::op_return:
	    ss << "return ";
	    stack_size = 0;
	    break;
	  case 12: {
	    uint8_t ch1 = s[++i];
	    stack_size = 0;
	    switch (ch1) {
	      case 22:
		ss << "hflex ";
		break;
	      case 23:
		ss << "flex ";
		break;
	      case 24:
		ss << "hflex1 ";
		break;
	      case 25:
		ss << "flex1 ";
		break;
	      default:
		ss << "composite op: 0x0c";
		ss << std::hex << std::setfill ('0');
		ss << std::setw (2) << (s[++i]&0xff);
		std::cout.copyfmt (init);
	      }
	    } break;
	  case cff::cs::endchar:
	    ss << "endchar ";
	    stack_size = 0;
	    break;
	  case cff::cs::vsindex:
	    ss << "vsindex ";
	    stack_size = 0;
	    break;
	  case cff::cs::blend:
	    ss << "blend ";
	    stack_size = 0;
	    break;
	  case cff::cs::hstemhm:
	    ss << "hstemhm ";
	    hint_cnt += stack_size/2;
	    stack_size = 0;
	    break;
	  case cff::cs::hintmask:
	  case cff::cs::cntrmask: {
	    if (ch==cff::cs::hintmask)
		ss << "hintmask ";
	    else
		ss << "cntrmask ";
	    hint_cnt += stack_size/2;
	    int mask_size = (hint_cnt+7)/8;
	    ss << std::hex << std::setfill ('0');
	    for (int j=0; j<mask_size; j++)
		ss << std::setw (2) << (s[++i]&0xff);
	    std::cout.copyfmt (init);
	    stack_size = 0;
	    } break;
	  case cff::cs::rmoveto:
	    ss << "rmoveto ";
	    stack_size = 0;
	    break;
	  case cff::cs::hmoveto:
	    ss << "hmoveto ";
	    stack_size = 0;
	    break;
	  case cff::cs::vstemhm:
	    ss << "vstemhm ";
	    hint_cnt += stack_size/2;
	    stack_size = 0;
	    break;
	  case cff::cs::rcurveline:
	    ss << "rcurveline ";
	    stack_size = 0;
	    break;
	  case cff::cs::rlinecurve:
	    ss << "rlinecurve ";
	    stack_size = 0;
	    break;
	  case cff::cs::vvcurveto:
	    ss << "vvcurveto ";
	    stack_size = 0;
	    break;
	  case cff::cs::hhcurveto:
	    ss << "hhcurveto ";
	    stack_size = 0;
	    break;
	  case cff::cs::callgsubr:
	    if (gsubrs) {
		ss << "callgsubr{";
		extr = print_ps (gsubrs->css[prev_num + gsubrs->bias].sdata, gsubrs, lsubrs, hint_cnt);
		ss << extr;
		ss << "} ";
	    } else
		ss << "callgsubr ";
	    stack_size = 0;
	    break;
	  case cff::cs::vhcurveto:
	    ss << "vhcurveto ";
	    stack_size = 0;
	    break;
	  case cff::cs::hvcurveto:
	    ss << "hvcurveto ";
	    stack_size = 0;
	    break;
	  default:
	    if (ch>31 && ch<=246) {
		prev_num = ch - 139;
		ss << prev_num << " ";
		stack_size++;
	    } else if (ch>246 && ch<=250) {
		prev_num = (ch-247)*256 + (uint8_t) s[++i] + 108;
		ss << prev_num << " ";
		stack_size++;
	    } else if (ch>250 && ch<=254) {
		prev_num = -(ch-251)*256 - (uint8_t) s[++i] - 108;
		ss << prev_num << " ";
		stack_size++;
	    } else if (ch==28) {
		uint8_t ch1 = s[i+1];
		uint8_t ch2 = s[i+2];
		i+=2;
		prev_num = (ch1<<8)|ch2;
		ss << prev_num << " ";
		stack_size++;
	    } else if (ch==255) {
		uint8_t ch1 = s[i+1];
		uint8_t ch2 = s[i+2];
		uint8_t ch3 = s[i+3];
		uint8_t ch4 = s[i+4];
		uint32_t val = (ch1<<24)|(ch2<<16)|(ch3<<8)|ch4;
		i+=4;
		uint16_t mant = val&0xffff;
		double fval = ((int16_t) (val>>16)) + mant/65536.;
		ss << fval << " (" << std::hex << val << ") " << std::dec;
		stack_size++;
	    } else
		ss << "unhandled: " << +ch << " ";
	}
    }
    return ss.str ();
}

#endif

CffTable::CffTable (sfntFile *fontfile, TableHeader &props) :
    GlyphContainer (fontfile, props) {

    m_loaded = m_bad_cff = false;
    m_version = (props.iname == CHR('C','F','F','2')) ? 2 : 1;
}

CffTable::~CffTable () {
}

static char *addnibble (char *pt, int nib) {
    if (nib<=9)
	*pt++ = nib+'0';
    else if (nib==10)
	*pt++ = '.';
    else if (nib==11)
	*pt++ = 'E';
    else if (nib==12) {
	*pt++ = 'E';
	*pt++ = '-';
    } else if (nib==14)
	*pt++ = '-';
    else if (nib==15)
	*pt++ = '\0';
    return (pt);
}

int CffTable::readcffthing (int *_ival, double *dval, uint16_t *operand) {
    char buffer[50], *pt;
    uint8_t ch;
    int ival;

    ch = data[m_pos++];
    if (ch==12) {
	*operand = (12<<8) | (uint8_t) data[m_pos++];
        return (3);
    // In CFF2: vsindex (22), blend (23), vstore (24)
    } else if (ch<=24) {
	*operand = ch;
        return (3);
    } else if (ch==30) {
	/* fixed format doesn't exist in dict data but does in type2 strings */
	pt = buffer;
	do {
	    ch = data[m_pos++];
	    if (pt<buffer+44 || (ch&0xf)==0xf || (ch&0xf0)==0xf0) {
		pt = addnibble (pt, ch>>4);
		pt = addnibble (pt, ch&0xf);
	    }
	} while (pt[-1]!='\0');
	// NB: need this std::string to make a local copy of the char* array
	// (otherwise may be immediately rewritten at the following setlocale call)
	std::string oldLocale (std::setlocale (LC_NUMERIC, nullptr));
	std::setlocale (LC_NUMERIC, "C");
	char* end;
	*dval = std::strtod (buffer, &end);
	std::setlocale (LC_NUMERIC, oldLocale.c_str ());
        return (2);
    } else if (ch>=32 && ch<=246) {
	*_ival = ch-139;
        return (1);
    } else if (ch>=247 && ch<=250) {
	*_ival = ((ch-247)<<8) + ((uint8_t) data[m_pos++])+108;
        return (1);
    } else if (ch>=251 && ch<=254) {
	*_ival = -((ch-251)<<8) - ((uint8_t) data[m_pos++])-108;
        return (1);
    } else if (ch==28) {
	ival = (uint8_t) data[m_pos++]<<8;
	*_ival = (ival | (uint8_t) data[m_pos++]);
        return (1);
    } else if (ch==29) {
        /* 4 byte integers exist in dict data but not in type2 strings */
	ival  = (uint8_t) data[m_pos++]<<24;
	ival |= (uint8_t) data[m_pos++]<<16;
	ival |= (uint8_t) data[m_pos++]<<8;
	*_ival = (ival | (uint8_t) data[m_pos++]);
        return (1);
    }
    FontShepherd::postError (
        tr ("Bad CFF table"),
        tr ("Unexpected value in dictionary: %1").arg (ch),
        container->parent ());
    m_bad_cff = true;
    *_ival = 0;
    return (0);
}

void CffTable::skipcfft2thing () {
    /* GWW: The old CFF spec allows little type2 programs to live in the CFF dict */
    /*  indices. These are designed to allow interpolation of values for mm */
    /*  fonts. */
    /* The Type2 program is terminated by an "endchar" operator */
    /* I don't support this, but I shall try to skip over them properly */
    /* There's no discussion about how values move from the t2 stack to the */
    /*  cff stack, as there are no examples of this, it's hard to guess */
    int ch;

    /* GT: DICT is a magic term inside CFF fonts, as is INDEX, and I guess CFF and type2 */
    FontShepherd::postWarning (
        tr ("Unsupported data in CFF"),
        tr ("I do not support type2 programs embedded in CFF DICT INDICES."),
        container->parent ());
    while (true) {
	ch = data[m_pos++];
	if (ch>=247 && ch<=254)
	    m_pos++;		/* Two byte number */
	else if (ch==255) {
	    m_pos+=4;
	    /* 16.16 number */
	} else if (ch==28) {
	    m_pos+=2;
	} else if (ch==12) {
	    m_pos++;		/* Two byte operator */
	} else if (ch==14) {
            return;
	}
    }
}

void CffTable::readCffSubrs (struct pschars &subs) {
    uint32_t count;
    int offsize;
    std::vector<uint32_t> offsets;
    uint32_t i, base;
    bool err = false;

    if (m_version == 2) {
	count = getlong (m_pos); m_pos+=4;
    } else {
	count = getushort (m_pos); m_pos+=2;
    }
    if (count==0)
        return;
    subs.cnt = count;
    subs.css.reserve (count);
    offsets.reserve (count+1);
    offsize = data[m_pos++];
    for (i=0; i<=count; ++i) {
	offsets[i] = getoffset (m_pos, offsize);
        m_pos += offsize;
    }
    base = m_pos;
    for (i=0; i<count; ++i) {
	m_pos = base + offsets[i]-1;
	if (offsets[i+1]>offsets[i] && offsets[i+1]-offsets[i]<0x10000) {
	    int len = offsets[i+1]-offsets[i];
	    struct charstring cs {data + m_pos, len};
            subs.css.push_back (cs);
	// In CFF2 may have zero-length data for an empty glyph (as the advance
	// width is obtained from hmtx anyway and the return op is deprecated)
	} else if (m_version == 2 && offsets[i] == offsets[i+1]) {
	    struct charstring cs;
            subs.css.push_back (cs);
	} else {
	    struct charstring cs;
	    if (!err)
                FontShepherd::postError (
                    tr ("Bad CFF table"),
                    tr ("Bad subroutine INDEX in cff font"),
                    container->parent ());
	    m_bad_cff = true;
	    err = true;
	    cs.sdata.push_back (11);		/* return */
            subs.css.push_back (cs);
	}
    }
}

void CffTable::readCffTopDict (TopDict &td, uint32_t size) {
    int ival, sp, ret;
    uint16_t max_stack = m_version == 2 ? 513 : 48;
    double stack[max_stack+2];
    uint16_t oval;
    uint32_t end = m_pos + size;
    size_t list_size;

    /* GWW: Multiple master fonts can have Type2 operators here, particularly */
    /*  blend operators. We're ignoring that */
    while (m_pos<end) {
	sp = 0;
	while ((ret=readcffthing (&ival, &stack[sp], &oval))!=3 && m_pos<end) {
	    if (ret==1)
		stack[sp]=ival;
	    if (ret!=0 && sp<max_stack)
		++sp;
	}
	if (ret==3 && oval==31 /* "T2" operator, can have 0 arguments */ ) {
	    skipcfft2thing ();
	} else if (sp==0) {
            FontShepherd::postError (
                tr ("Bad CFF table"),
                tr ("No argument to operator"),
                container->parent ());
	    m_bad_cff = true;
	} else if (ret==3) {
	    top_dict_entry entry;
	    switch (oval) {
	      case cff::version:
	      case cff::Notice:
	      case cff::FullName:
	      case cff::FamilyName:
	      case cff::Weight:
	      case cff::Copyright:
	      case cff::PostScript:
	      case cff::BaseFontName:
	      case cff::FontName:
		entry.setType (dt_sid);
		entry.sid.sid = stack[sp-1];
		td[oval]=entry;
		break;
	      case cff::isFixedPitch:
		entry.setType (dt_bool);
		entry.b = stack[sp-1];
		td[oval]=entry;
		break;
	      case cff::UniqueID:
	      case cff::charset:
	      case cff::Encoding:
	      case cff::CharStrings:
	      case cff::vsindex:
	      case cff::vstore:
	      case cff::PaintType:
	      case cff::CharstringType:
	      case cff::CIDFontVersion:
	      case cff::CIDFontRevision:
	      case cff::CIDFontType:
	      case cff::CIDCount:
	      case cff::UIDBase:
	      case cff::FDArray:
	      case cff::FDSelect:
		entry.setType (dt_uint);
		entry.i = stack[sp-1];
		td[oval]=entry;
		break;
	      case cff::ItalicAngle:
	      case cff::UnderlinePosition:
	      case cff::UnderlineThickness:
	      case cff::StrokeWidth:
		entry.setType (dt_float);
		entry.f = stack[sp-1];
		td[oval]=entry;
		break;
	      case cff::FontBBox:
	      case cff::XUID:
	      case cff::FontMatrix:
		list_size = (oval == 5) ? 4 : (oval == 14) ? 20 : 6;
		entry.setType (dt_list);
		for (size_t i=0; i<list_size; i++)
		    entry.list.push_back (stack[i]);
		td[oval]=entry;
		break;
	      case cff::BaseFontBlend:
		entry.setType (dt_list);
		for (int i=0; i<sp; i++) {
		    entry.list.push_back (stack[i]);
		    if (i>0)
			entry.list[i] += entry.list[i-1];
		}
		td[oval]=entry;
		break;
	      case cff::Private:
		entry.setType (dt_size_off);
		entry.so.size = stack[0];
		entry.so.offset = stack[1];
		td[oval] = entry;
	      break;
	      case cff::ROS:
		entry.setType (dt_ros);
		entry.ros.registry.sid = stack[0];
		entry.ros.order.sid = stack[1];
		entry.ros.supplement = stack[2];
		td[oval] = entry;
		break;
	      case cff::SyntheticBase:
		FontShepherd::postWarning (
		    tr ("Unsupported data in CFF"),
		    tr ("I do not support synthetic fonts"),
		    container->parent ());
		break;
	      case (12<<8)+24:
	      case (12<<8)+26:
		FontShepherd::postWarning (
		    tr ("Unsupported data in CFF"),
		    tr ("I do not support type2 multiple master fonts"),
		    container->parent ());
		break;
	      case (12<<8)+39:
		FontShepherd::postWarning (
		    tr ("Unsupported data in CFF"),
		    tr ("I do not support Chameleon fonts"),
		    container->parent ());
		break;
	      default:
		FontShepherd::postError (
		    tr ("Bad CFF table"),
		    tr ("Unknown operator in CFF top DICT: %1").arg (oval),
		    container->parent ());
		m_bad_cff = true;
		break;
	    }
	}
    }
}

void CffTable::encodeInt (QDataStream &os, int val) {
    if (val >= -107 && val <= 107) {
	os << (uint8_t) (val + 139);
    } else if (val >= 108 && val <= 1131) {
	val -= 108;
	os << (uint8_t) ((val>>8)+247);
	os << (uint8_t) (val&0xff);
    } else if (val >=- 1131 && val <=- 108) {
	val = -val;
	val -= 108;
	os << (uint8_t) ((val>>8)+251);
	os << (uint8_t) (val&0xff);
    } else if (val >= -32768 && val < 32768) {
	os << (uint8_t) 28;
	os << (uint8_t) (val>>8);
	os << (uint8_t) (val&0xff);
    // GWW: In dict data we have 4 byte ints, in type2 strings we don't
    } else {
	os << (uint8_t) 29;
	os << (uint8_t) ((val>>24)&0xff);
	os << (uint8_t) ((val>>16)&0xff);
	os << (uint8_t) ((val>>8)&0xff);
	os << (uint8_t) (val&0xff);
    }
}

void CffTable::encodeInt (std::ostream &os, int val) {
    if (val >= -107 && val <= 107) {
	os.put (val + 139);
    } else if (val >= 108 && val <= 1131) {
	val -= 108;
	os.put ((char) ((val>>8)+247));
	os.put ((char) (val&0xff));
    } else if (val >=- 1131 && val <=- 108) {
	val = -val;
	val -= 108;
	os.put ((char) ((val>>8)+251));
	os.put ((char) (val&0xff));
    } else if (val >= -32768 && val < 32768) {
	os.put (28);
	os.put ((char) (val>>8));
	os.put ((char) (val&0xff));
    // GWW: In dict data we have 4 byte ints, in type2 strings we don't
    } else {
	os.put (29);
	os.put ((char) ((val>>24)&0xff));
	os.put ((char) ((val>>16)&0xff));
	os.put ((char) ((val>>8)&0xff));
	os.put ((char) (val&0xff));
    }
}

void CffTable::encodeSizedInt (QDataStream &os, uint8_t size, int val) {
    if (size == 2) {
	os << (uint8_t) 28;
	os << (uint8_t) (val>>8);
	os << (uint8_t) (val&0xff);
    // GWW: In dict data we have 4 byte ints, in type2 strings we don't
    } else {
	os << (uint8_t) 29;
	os << (uint8_t) ((val>>24)&0xff);
	os << (uint8_t) ((val>>16)&0xff);
	os << (uint8_t) ((val>>8)&0xff);
	os << (uint8_t) (val&0xff);
    }
}

void CffTable::encodeFixed (QDataStream &os, double val) {
    if (val-rint (val) > -.00001 && val-rint (val) < .00001) {
	encodeInt (os, (int) val);
	return;
    }

    os << (uint8_t) 0xFF;
    int ints = floor (val);
    int mant = (val-ints)*65536.;
    uint32_t ival = (ints<<16) | mant;
    os << ival;
}

void CffTable::encodeFixed (std::ostream &os, double val) {
    if (val-rint (val) > -.00001 && val-rint (val) < .00001) {
	encodeInt (os, (int) val);
	return;
    }

    os.put ((char) 0xFF);
    int ints = floor (val);
    int mant = (val-ints)*65536.;
    uint32_t ival = (ints<<16) | mant;
    os.put ((char) ((ival>>24)&0xff));
    os.put ((char) ((ival>>16)&0xff));
    os.put ((char) ((ival>>8)&0xff));
    os.put ((char) (ival&0xff));
}

void CffTable::encodeFloat (QDataStream &os, double val) {
    if (val-rint (val) > -.00001 && val-rint (val) < .00001) {
	encodeInt (os, (int) val);
	return;
    }
    /* The type2 strings have a fixed format, but the dict data does not */
    uint8_t sofar = 0, n;
    bool odd = true;
    std::string buf = std::to_string (val);
    // Remove trailing and leading zeros
    buf.erase (buf.find_last_not_of ('0') + 1, std::string::npos);
    buf.erase (0, std::min (buf.find_first_not_of ('0'), buf.size () - 1));
    /* Start a double */
    os << (uint8_t) 30;
    for (auto pt = buf.cbegin (); pt != buf.cend (); ++pt) {
        if (std::isdigit (*pt))
	    n = *pt-'0';
        else if (*pt=='.')
	    n = 0xa;
        else if (*pt=='-')
	    n = 0xe;
        else if ((*pt=='E' || *pt=='e') && pt[1]=='-') {
	    n = 0xc;
	    ++pt;
        } else if (*pt=='E' || *pt=='e')
	    n = 0xb;
        /* Should never happen */
	else
	    n = 0;
        if (odd) {
	    sofar = n<<4;
	    odd = false;
        } else {
	    os << (uint8_t) (sofar|n);
	    sofar=0;
	    odd = true;
        }
    }
    if (sofar==0)
        os << (uint8_t) 0xff;
    else
        os << (uint8_t) (sofar|0xf);
}

void CffTable::encodeOper (QDataStream &os, uint16_t oper) {
    if (oper>=256)
        os << (uint8_t) (oper>>8);
    os << (uint8_t) (oper&0xff);
}

void CffTable::encodeOper (std::ostream &os, uint16_t oper) {
    if (oper>=256)
        os.put ((char) (oper>>8));
    os.put ((char) (oper&0xff));
}

void CffTable::encodeOff (QDataStream &os, uint8_t offsize, uint32_t val) {
    if (offsize==1)
	os << (uint8_t) val;
    else if (offsize==2)
	os << (uint16_t) val;
    else if (offsize==3) {
	os << (uint8_t) ((val>>16)&0xff);
	os << (uint8_t) ((val>>8)&0xff);
	os << (uint8_t) (val&0xff);
    } else
	os << val;
}

static void write_string_array (QDataStream &os, QBuffer &buf, std::vector<std::string> slist, double table_v) {
    int len = slist.size ();
    if (table_v > 1)
        os << (uint32_t) len;
    else
        os << (uint16_t) len;
    if (len == 0)
	return;

    uint32_t maxl = 0;
    for (int i=0; i<len; i++)
        maxl += slist[i].length ();
    uint8_t off_size = maxl > 0xFFFFFF ? 4 : maxl > 0xFFFF ? 3 : maxl > 0xFF ? 2 : 1;
    os << off_size;
    CffTable::encodeOff (os, off_size, 1);
    uint32_t cur_off = 1;
    for (int i=0; i<len; i++) {
        cur_off += slist[i].length ();
        CffTable::encodeOff (os, off_size, cur_off);
    }
    for (int i=0; i<len; i++) {
        buf.write (slist[i].c_str (), slist[i].length ());
    }
}

void CffTable::writeCffTopDict (TopDict &td, QDataStream &os, QBuffer &buf, uint16_t off_size) {
    for (size_t i=0; i<td.size (); i++) {
	auto &pair = td.by_idx (i);
	int oper = pair.first;
	top_dict_entry &entry = pair.second;

	// Drop Encoding data if it is present (not needed for CFF in OTF),
	// as well as other types of unsupported data
	if (oper < 0 || oper == 16 || oper == (12<<8)+20 ||
	    oper == (12<<8)+24 || oper == (12<<8)+26 || oper == (12<<8)+39)
	    continue;

	switch (entry.type ()) {
	  case dt_uint:
	  case dt_bool:
	    switch (oper) {
	      // As the following are offsets which aren't known right now,
	      // their size should be predictable, so that later we can return
	      // and fill them. So can't use normal CFF integer encoding here
	      case cff::charset:
	      case cff::CharStrings:
	      case cff::vstore:
	      case cff::FDArray:
	      case cff::FDSelect:
		entry.i = buf.pos ();
		encodeSizedInt (os, off_size, 0);
		break;
	      default:
		encodeInt (os, entry.i);
	    }
	    break;
	  case dt_float:
	    encodeFloat (os, entry.f);
	    break;
	  case dt_list:
	    for (uint16_t j=0; j<entry.list.size (); j++)
		encodeInt (os, entry.list[j]);
	    break;
	  case dt_sid:
	    encodeInt (os, entry.sid.sid);
	    break;
	  case dt_size_off:
	    // two integers: size and offset
	    // Assume actual data size is already known (the only case
	    // this data type is used is for the Private dict, so we need
	    // to have its encoded representation before proceeding
	    // to the Top Dict)
	    encodeInt (os, entry.so.size);
	    entry.so.offset = buf.pos ();
	    encodeSizedInt (os, off_size, 0);
	    break;
	  case dt_ros:
	    encodeInt (os, entry.ros.registry.sid);
	    encodeInt (os, entry.ros.order.sid);
	    encodeInt (os, entry.ros.supplement);
	    break;
	}
	encodeOper (os, oper);
    }
}

void CffTable::readCffPrivate (PrivateDict &pd, uint32_t off, uint32_t size) {
    int ival, sp, ret;
    uint16_t max_stack = m_version == 2 ? 513 : 48;
    double stack[max_stack+2];
    std::vector<blend> blend_list;
    uint16_t oval;
    uint32_t end = off + size;

    m_pos = off;

    while (m_pos<end) {
	sp = 0;
	while ((ret = readcffthing (&ival, &stack[sp], &oval))!=3 && m_pos<end ) {
	    if (ret==1)
		stack[sp]=ival;
	    if (ret!=0 && sp<max_stack)
		++sp;
	}
	if (ret==3) {
	    private_entry entry;
	    switch (oval) {
	      case cff::BlueValues:
	      case cff::OtherBlues:
	      case cff::FamilyBlues:
	      case cff::FamilyOtherBlues:
	      case cff::StemSnapH:
	      case cff::StemSnapV:
		entry.setType (pt_blend_list);
		if (blend_list.size () > 0) {
		    for (size_t i=0; i<blend_list.size () && i<14; ++i)
			entry.list[i] = blend_list[i];
		    blend_list.resize (0);
		} else {
		    for (int i=0; i<sp && i<14; ++i) {
			entry.list[i].base = stack[i];
			entry.list[i].valid = true;
		    }
		}
		for (size_t i=1; i<entry.list.size () && entry.list[i].valid; i++)
		    entry.list[i].base += entry.list[i-1].base;
		pd[oval] = entry;
		break;
	      case cff::StdHW:
	      case cff::StdVW:
	      case cff::defaultWidthX:
	      case cff::nominalWidthX:
	      case cff::BlueScale:
	      case cff::BlueShift:
	      case cff::BlueFuzz:
	      case cff::ForceBoldThreshold: // obsolete
	      case cff::ExpansionFactor:
	      case cff::initialRandomSeed:
		if (sp==0 && !blend_list.size ()) {
		    FontShepherd::postError (
			tr ("Bad CFF table"),
			tr ("No argument to operator %1 in private dict").arg (oval),
			container->parent ());
		    m_bad_cff = true;
		} else {
		    entry.setType (pt_blend);
		    if (blend_list.size ()) {
			entry.n = blend_list[blend_list.size () - 1];
			blend_list.resize (0);
		    } else {
			entry.n.base = stack[sp-1];
		    }
		    entry.n.valid = true;
		    pd[oval] = entry;
		}
		break;
	      case cff::ForceBold:
		if (sp==0) {
		    FontShepherd::postError (
			tr ("Bad CFF table"),
			tr ("No argument to operator %1 in private dict").arg (oval),
			container->parent ());
		    m_bad_cff = true;
		} else {
		    entry.setType (pt_bool);
		    entry.b = stack[sp-1];
		    pd[oval] = entry;
		}
		break;
	      case cff::lenIV:
		/* lenIV. -1 => unencrypted charstrings */
		/* obsolete */
		break;
	      case cff::Subrs:
	      case cff::LanguageGroup:
		if (sp==0) {
		    FontShepherd::postError (
			tr ("Bad CFF table"),
			tr ("No argument to operator %1 in private dict").arg (oval),
			container->parent ());
		    m_bad_cff = true;
		} else {
		    entry.setType (pt_uint);
		    entry.i = stack[sp-1];
		    pd[oval] = entry;
		}
		break;
	      case cff::blend:
		// Unlike other commands, blend preserves n arguments on the stack for the next command
		if (sp==0) {
		    FontShepherd::postError (
			tr ("Bad CFF table"),
			tr ("No argument to operator %1 in private dict").arg (oval),
			container->parent ());
		    m_bad_cff = true;
		} else {
		    int n_base = stack[sp-1];
		    if (m_core_font.vstore.data.size () > m_core_font.vstore.index) {
			blend_list.reserve (n_base);
			int n_regions = m_core_font.vstore.data[m_core_font.vstore.index].regionIndexes.size ();
			for (int i=0; i<n_base; i++) {
			    blend b;
			    b.base = stack[i];
			    b.valid = true;
			    b.deltas.resize (n_regions);
			    for (int j=0; j<n_regions; j++)
				b.deltas[j] = stack[n_base+(i*n_regions)+j];
			    blend_list.push_back (b);
			}
		    } else {
			FontShepherd::postError (
			    tr ("Bad CFF table"),
			    tr ("Blend operator in PS Private dictionary, while no Variation Data available"),
			    container->parent ());
			m_bad_cff = true;
		    }
		    sp = 0;
		}
		break;
	      case cff::T2: /* "T2" operator, can have 0 arguments */
		skipcfft2thing ();
		break;
	      default:
		FontShepherd::postError (
		    tr ("Bad CFF table"),
		    tr ("Unknown operator in Private DICT: %2").arg (oval),
		    container->parent ());
		m_bad_cff = true;
		break;
	    }
	}
    }
}

void CffTable::writeCffPrivate (PrivateDict &pd, QDataStream &os, QBuffer &buf) {
    uint32_t init_pos = buf.pos ();
    for (size_t i=0; i<pd.size (); i++) {
	auto &pair = pd.by_idx (i);
	int oper = pair.first;
	if (oper == cff::Subrs)
	    continue;
	private_entry &entry = pair.second;

	switch (entry.type ()) {
	  case pt_uint:
	  case pt_bool:
	    encodeInt (os, entry.i);
	    break;
	  case pt_blend:
	    encodeFloat (os, entry.n.base);
	    if (entry.n.deltas.size ()) {
		for (size_t j=0; j<entry.n.deltas.size (); j++)
		    encodeFloat (os, entry.n.deltas[j]);
		// count of blends (1)
		encodeInt (os, 1);
		encodeOper (os, cff::blend);
	    }
	    break;
	  case pt_blend_list:
	    if (entry.list[0].valid)
		encodeFloat (os, entry.list[0].base);
	    for (size_t j=1; j<14 && entry.list[j].valid; j++) {
		encodeFloat (os, entry.list[j].base - entry.list[j-1].base);
	    }
	    size_t num_blends = 0;
	    for (size_t j=0; j<14 && entry.list[j].valid; j++) {
		if (entry.list[j].deltas.size ()) {
		    num_blends++;
		    for (size_t k=0; k<entry.list[j].deltas.size (); k++)
			encodeFloat (os, entry.list[j].deltas[k]);
		}
	    }
	    if (num_blends) {
		encodeInt (os, num_blends);
		encodeOper (os, cff::blend);
	    }
	    break;
	}
	encodeOper (os, oper);
    }
    size_t dict_size = buf.pos () - init_pos;
    if (pd.has_key (cff::Subrs)) {
	// One more byte for the Subrs op itself
	dict_size += 1;
	if (dict_size < 107)
	    dict_size += 1;
	else if (dict_size < 1129)
	    dict_size += 2;
	else
	    dict_size += 3;
	encodeInt (os, dict_size);
	encodeOper (os, cff::Subrs);
    }
}

void CffTable::readSubFonts () {
    uint32_t count = (m_version > 1) ? getlong (m_pos) : getushort (m_pos);
    int offsize;
    std::vector<uint32_t> offsets;

    m_pos +=2;
    if (m_version > 1) m_pos +=2;
    if (count==0)
        return;

    offsets.reserve (count+1);
    offsize = data[m_pos++];
    for (uint32_t i=0; i<=count; ++i) {
	offsets[i] = getoffset (m_pos, offsize);
        m_pos+=offsize;
    }

    m_core_font.subfonts.resize (count);
    for (uint32_t i=0; i<count; ++i) {
	cff_font &subfont = m_core_font.subfonts[i];
	readCffTopDict (subfont.top_dict, offsets[i+1]-offsets[i]);
	for (size_t i=0; i<subfont.top_dict.size (); i++) {
	    auto &pair = subfont.top_dict.by_idx (i);
	    if (pair.second.type () == dt_sid)
		pair.second.sid.str = getsid (pair.second.sid.sid, m_core_font.strings);
	}
        if (m_core_font.top_dict.has_key (cff::FontMatrix)) {
	    if (subfont.top_dict.has_key (cff::FontMatrix)) {
		std::vector<double> &parent_matrix = m_core_font.top_dict[cff::FontMatrix].list;
		std::vector<double> &child_matrix = subfont.top_dict[cff::FontMatrix].list;
		FontShepherd::math::matMultiply (parent_matrix.data (), child_matrix.data (), child_matrix.data ());
	    } else {
		subfont.top_dict[cff::FontMatrix] = m_core_font.top_dict[cff::FontMatrix];
	    }
	}
    }

    for (size_t i=0; i<m_core_font.subfonts.size (); i++) {
        struct cff_font &subfont = m_core_font.subfonts[i];
        if (subfont.top_dict.has_key (cff::Private)) {
	    uint32_t p_size = subfont.top_dict[cff::Private].so.size;
	    uint32_t p_off = subfont.top_dict[cff::Private].so.offset;
	    readCffPrivate (subfont.private_dict, p_off, p_size);
	    if (subfont.private_dict.has_key (cff::Subrs)) {
		m_pos = p_off + subfont.private_dict[cff::Subrs].i;
		readCffSubrs (subfont.local_subrs);
		int cstype = subfont.top_dict.has_key (cff::CharstringType) ?
		    subfont.top_dict[cff::CharstringType].i : 2;
		subfont.local_subrs.bias = (cstype==1) ? 0 :
			subfont.local_subrs.cnt < 1240 ? 107 :
			subfont.local_subrs.cnt <33900 ? 1131 : 32768;
	    }
        }
    }
    return;
}

void CffTable::writeSubFonts (QDataStream &os, QBuffer &buf, uint8_t off_size) {
    std::vector<QByteArray> top_dicts;
    std::vector<QByteArray> prv_dicts;
    QBuffer sub_buf;
    QDataStream sub_os (&sub_buf);
    size_t cnt = m_core_font.subfonts.size ();
    auto cs_data_extractor = [](const struct charstring &cs) { return cs.sdata; };

    top_dicts.resize (cnt);
    prv_dicts.resize (cnt);
    if (m_version > 1)
	os << (uint32_t) cnt;
    else
	os << (uint16_t) cnt;

    for (size_t i=0; i<cnt; i++) {
	struct cff_font &subf = m_core_font.subfonts[i];
	sub_buf.setBuffer (&prv_dicts[i]);
	sub_buf.open (QIODevice::WriteOnly);
	writeCffPrivate (subf.private_dict, sub_os, sub_buf);
	subf.top_dict[cff::Private].so.size = prv_dicts[i].size ();
	if (subf.private_dict.has_key (cff::Subrs)) {
	    std::vector<std::string> ls;
	    ls.reserve (subf.local_subrs.cnt);
	    std::transform (subf.local_subrs.css.cbegin (), subf.local_subrs.css.cend (),
		std::back_inserter (ls), cs_data_extractor);
	    write_string_array (sub_os, sub_buf, ls, m_version);
	}
	sub_buf.close ();
    }

    for (size_t i=0; i<cnt; i++) {
	struct cff_font &subf = m_core_font.subfonts[i];
	sub_buf.setBuffer (&top_dicts[i]);
	sub_buf.open (QIODevice::WriteOnly);
	writeCffTopDict (subf.top_dict, sub_os, sub_buf, off_size);
	sub_buf.close ();
    }

    uint32_t top_size = 0;
    for (size_t i=0; i<cnt; i++)
	top_size += top_dicts[i].size ();
    uint8_t td_off_size = top_size > 0xFFFFFF ? 4 : top_size > 0xFFFF ? 3 : top_size > 0xFF ? 2 : 1;

    os << td_off_size;
    uint32_t td_off = 1;
    encodeOff (os, td_off_size, td_off);
    for (size_t i=0; i<cnt; i++) {
	td_off += top_dicts[i].size ();
	encodeOff (os, td_off_size, td_off);
    }
    uint32_t td_pos = buf.pos ();
    for (size_t i=0; i<cnt; i++)
	buf.write (top_dicts[i]);
    for (size_t i=0; i<cnt; i++) {
	struct cff_font &subf = m_core_font.subfonts[i];
	uint32_t cur_pos = buf.pos ();
	buf.seek (subf.top_dict[cff::Private].so.offset + td_pos);
	encodeSizedInt (os, off_size, cur_pos);
	subf.top_dict[cff::Private].so.offset = cur_pos;
	buf.seek (cur_pos);
	buf.write (prv_dicts[i]);
	td_pos += top_dicts[i].size ();
    }
}

std::string CffTable::getsid (int sid, std::vector<std::string> &strings) {
    uint16_t scnt = strings.size ();
    const uint16_t nStdStrings = cff::names.size ();

    if (sid==-1)
        return ("");
    else if (sid<nStdStrings)
        return (cff::names[sid]);
    else if (sid-nStdStrings<scnt)
        return (strings[sid-nStdStrings]);

    FontShepherd::postError (
        tr ("Bad CFF table"),
        tr ("Bad sid: %1 (must be less than %2)").arg (sid).arg (scnt+nStdStrings),
        container->parent ());
    m_bad_cff = true;
    return ("");
}

void CffTable::readCffSet (int off, int len, std::vector<uint16_t> &charset) {
    uint16_t i, j, first, cnt;

    i = 0;
    switch (off) {
      case 0:
	/* ISO Adobe charset */
        charset.resize (len);
	for (i=0; i<len && i<=228; ++i)
	    charset[i] = i;
	break;
      case 1:
	/* Expert charset */
	charset.resize (len<162 ? 162 : len);
	charset[0] = 0;		/* .notdef */
	charset[1] = 1;
	for ( i=2; i<len && i<=238-227; ++i )
	    charset[i] = i+227;
	charset[12] = 13;
	charset[13] = 14;
	charset[14] = 15;
	charset[15] = 99;
	for ( i=16; i<len && i<=248-223; ++i )
	    charset[i] = i+223;
	charset[25] = 27;
	charset[26] = 28;
	for ( i=27; i<len && i<=266-222; ++i )
	    charset[i] = i+222;
	charset[44] = 109;
	charset[45] = 110;
	for ( i=46; i<len && i<=318-221; ++i )
	    charset[i] = i+221;
	charset[96] = 158;
	charset[97] = 155;
	charset[98] = 163;
	for ( i=99; i<len && i<=326-220; ++i )
	    charset[i] = i+220;
	charset[107] = 150;
	charset[108] = 164;
	charset[109] = 169;
	for ( i=110; i<len && i<=378-217; ++i )
	    charset[i] = i+217;
	break;
      case 2:
	/* Expert subset charset */
	charset.resize (len < 130 ? 130 : len);
	charset[0] = 0;		/* .notdef */
	charset[1] = 1;
	for ( i=2; i<len && i<=238-227; ++i )
	    charset[i] = i+227;
	charset[12] = 13;
	charset[13] = 14;
	charset[14] = 15;
	charset[15] = 99;
	for ( i=16; i<len && i<=248-223; ++i )
	    charset[i] = i+223;
	charset[25] = 27;
	charset[26] = 28;
	for ( i=27; i<len && i<=266-222; ++i )
	    charset[i] = i+222;
	charset[44] = 109;
	charset[45] = 110;
	for ( i=46; i<len && i<=272-221; ++i )
	    charset[i] = i+221;
	charset[51] = 300;
	charset[52] = 301;
	charset[53] = 302;
	charset[54] = 305;
	charset[55] = 314;
	charset[56] = 315;
	charset[57] = 158;
	charset[58] = 155;
	charset[59] = 163;
	for ( i=60; i<len && i<=326-260; ++i )
	    charset[i] = i+260;
	charset[67] = 150;
	charset[68] = 164;
	charset[69] = 169;
	for (i=110; i<len && i<=346-217; ++i)
	    charset[i] = i+217;
	break;
      default:
	charset.resize (len);
	charset[0] = 0;		/* .notdef */
	m_pos = off;
	m_core_font.csformat = (uint8_t) data[m_pos++];
	switch (m_core_font.csformat) {
	  case 0:
	    for (i=1; i<len; ++i) {
		charset[i] = getushort (m_pos);
                m_pos+=2;
            }
	    break;
	  case 1:
	    for (i=1; i<len; ) {
		first = charset[i++] = getushort (m_pos);
                m_pos+=2;
		cnt = (uint8_t) data[m_pos++];
		for (j=0; j<cnt; ++j)
		    charset[i++] = ++first;
	    }
	    break;
	  case 2:
	    for (i=1; i<len; ) {
		first = charset[i++] = getushort (m_pos);
		m_pos+=2;
		cnt = getushort (m_pos); m_pos+=2;
		for (j=0; j<cnt; ++j)
		    charset[i++] = ++first;
	    }
	    break;
	  default:
            FontShepherd::postError (
                tr ("Bad CFF table"),
                tr ("Unexpected charset format in cff: %1").arg (m_core_font.csformat),
                container->parent ());
	    m_bad_cff = true;
	}
    }
    while (i<len) charset[i++] = 0;
}

void CffTable::writeCffSet (QDataStream &os, QBuffer &, uint32_t off) {
    int limit, nranges=0, nshort=0;
    uint8_t format=0;
    // Special offset values indicate one of predefined charsets
    if (off < 3)
	return;
    // Check which format is more appropriate
    for (size_t i=1; i<m_core_font.charset.size () - 1; i++) {
	for (int rsize=0; m_core_font.charset[i+1] == m_core_font.charset[i] + 1; rsize++) {
	    i++;
	    if (rsize == 0x100) nshort++;
	}
	nranges++;
    }
    if (nranges < (int) (m_core_font.charset.size ()/2))
	format = (nshort > nranges/1.5) ? 2 : 1;

    os << format;
    switch (format) {
      case 0:
	for (size_t i=1; i<m_core_font.charset.size (); i++)
	    os << m_core_font.charset[i];
	break;
      case 1:
      case 2:
	limit = format == 1 ? 0xFF : 0xFFFF;
	for (size_t i=1; i<m_core_font.charset.size (); i++) {
	    os << m_core_font.charset[i];
	    uint16_t nleft = 0;
	    while (i<(m_core_font.charset.size () - 1) && nleft < limit &&
		m_core_font.charset[i+1] == m_core_font.charset[i] + 1) {
		i++;
		nleft++;
	    }
	    if (format == 1)
		os << (uint8_t) nleft;
	    else
		os << nleft;
	}
	break;
      default:
	;
    }
}

void CffTable::readfdselect (std::vector<uint16_t> &fdselect, uint16_t numglyphs) {
    uint16_t i, j, format, first, end, fd;
    uint32_t nr;

    format = (uint8_t) data[m_pos++];
    fdselect.resize (numglyphs);
    switch (format) {
      case 0:
	for (i=0; i<numglyphs; ++i)
	    fdselect[i] = (uint8_t) data[m_pos++];
	break;
      case 3:
	nr = getushort (m_pos); m_pos+=2;
	first = getushort (m_pos); m_pos+=2;
	for (i=0; i<nr; ++i) {
	    fd = (uint8_t) data[m_pos++];
	    end = getushort (m_pos); m_pos+=2;
	    for (j=first; j<end; ++j) {
		if (j>=numglyphs) {
                    FontShepherd::postError (
                        tr ("Bad CFF table"), tr ("Bad fdselect"), container->parent ());
		    m_bad_cff = true;
		} else
		    fdselect[j] = fd;
	    }
	    first = end;
	}
	break;
      case 4:
	nr = getlong (m_pos); m_pos+=4;
	first = getlong (m_pos); m_pos+=4;
	for (i=0; i<nr; ++i) {
	    fd = getushort (m_pos); m_pos+=2;
	    end = getlong (m_pos); m_pos+=4;
	    for (j=first; j<end; ++j) {
		if (j>=numglyphs) {
                    FontShepherd::postError (
                        tr ("Bad CFF table"), tr ("Bad fdselect"), container->parent ());
		    m_bad_cff = true;
		} else
		    fdselect[j] = fd;
	    }
	    first = end;
	}
	break;
      default:
        FontShepherd::postError (
            tr ("Bad CFF table"),
            tr ("Didn't understand format for fdselect").arg (format),
            container->parent ());
	m_bad_cff = true;
    }
}

void CffTable::writefdselect (QDataStream &os, QBuffer &) {
    size_t nr = 1;
    uint8_t format = 0;
    for (size_t i=1; i<m_core_font.fdselect.size (); i++) {
	if (m_core_font.fdselect[i] != m_core_font.fdselect[i-1])
	    nr++;
    }
    if (m_core_font.fdselect.size () > 0xfffe || m_core_font.subfonts.size () > 256)
	format = 4;
    else if (nr <= (m_core_font.fdselect.size ()/2))
	format = 3;
    os << format;
    switch (format) {
      case 0:
	for (size_t i=0; i<m_core_font.fdselect.size (); i++)
	    os << (uint8_t) m_core_font.fdselect[i];
	break;
      case 3:
      case 4:
	encodeOff (os, format == 3 ? 2 : 4, nr);
	for (size_t i=0; i<m_core_font.fdselect.size (); i++) {
	    encodeOff (os, format == 3 ? 2 : 4, i);
	    encodeOff (os, format == 3 ? 1 : 2, m_core_font.fdselect[i]);
	    while (i<m_core_font.fdselect.size () - 1 &&
		m_core_font.fdselect[i+1] == m_core_font.fdselect[i])
		i++;
	}
	encodeOff (os, format == 3 ? 2 : 4, m_core_font.fdselect.size ());
	break;
      default:
	;
    }
}

void CffTable::readvstore (struct variation_store &vstore) {
    uint32_t start = m_pos;
    /*uint16_t length = */ getushort (m_pos); m_pos +=2;
    vstore.format = getushort (m_pos); m_pos +=2;
    uint32_t reg_offset = getlong (m_pos); m_pos +=4;
    uint16_t data_count = getushort (m_pos); m_pos +=2;

    std::vector<uint32_t> data_off_list;
    data_off_list.resize (data_count);
    for (uint16_t i=0; i<data_count; i++) {
	data_off_list[i] = getlong (m_pos);
	m_pos +=4;
    }
    uint16_t axis_count = getushort (m_pos); m_pos+=2;
    uint16_t region_count = getushort (m_pos); m_pos+=2;
    m_pos = start + reg_offset;
    vstore.regions.reserve (region_count);

    for (uint16_t i=0; i<region_count; i++) {
	std::vector<struct axis_coordinates> region;
	for (uint16_t j=0; j<axis_count; j++) {
	    struct axis_coordinates va;
	    va.startCoord = get2dot14 (m_pos); m_pos+=2;
	    va.peakCoord = get2dot14 (m_pos); m_pos+=2;
	    va.endCoord = get2dot14 (m_pos); m_pos+=2;
	    region.push_back (va);
	}
	vstore.regions.push_back (region);
    }
    for (uint16_t i=0; i<data_count; i++) {
	// this offset is from the start of ItemVariationStore, i. e. VariationStore Data + length field
	m_pos = start + data_off_list[i] + 2;
	struct variation_data vd;
	uint16_t item_count = getushort (m_pos); m_pos+=2;
	uint16_t short_count = getushort (m_pos); m_pos+=2;
	uint16_t reg_count = getushort (m_pos); m_pos+=2;
	vd.shortDeltaCount = short_count;
	vd.regionIndexes.resize (reg_count);
	vstore.data.reserve (item_count);

	for (uint16_t j=0; j<reg_count; j++) {
	    vd.regionIndexes[j] = getushort (m_pos); m_pos+=2;
	}

	std::vector<std::vector<int16_t>> dsets;
	for (uint16_t j=0; j<item_count; j++) {
	    std::vector<int16_t> deltas;
	    deltas.resize (reg_count);
	    for (uint16_t k=0; k<short_count; k++) {
		deltas[k] = getushort (m_pos); m_pos+=2;
	    }
	    for (uint16_t k=short_count; k<reg_count; k++) {
		deltas[k] = data[m_pos]; m_pos++;
	    }
	    dsets.push_back (deltas);
	}
	vstore.data.push_back (vd);
    }
}

void CffTable::writevstore (QDataStream &os, QBuffer &buf) {
    struct variation_store &vstore = m_core_font.vstore;
    int init_pos = buf.pos ();
    int cur_pos;
    // Table size is uint16, while internal offsets, relative to the
    // start of the table, are uint32. Is it OK?
    os << (uint16_t) 0; // placeholder for table size
    os << vstore.format;
    os << (uint32_t) 0; // variationRegionListOffset
    os << (uint16_t) vstore.data.size ();

    for (size_t i=0; i<vstore.data.size (); i++)
	os << (uint32_t) 0;

    cur_pos = buf.pos ();
    buf.seek (init_pos + 4);
    // Table size field is not the part of the table itself,
    // hence subtract 2 from the offset
    os << (uint32_t) (cur_pos - init_pos - 2);
    buf.seek (cur_pos);
    uint16_t axis_cnt = vstore.regions[0].size ();
    uint16_t  reg_cnt = vstore.regions.size ();
    os << axis_cnt;
    os << reg_cnt;
    for (size_t i=0; i<reg_cnt; i++) {
	for (size_t j=0; j<axis_cnt; j++) {
	    put2dot14 (os, vstore.regions[i][j].startCoord);
	    put2dot14 (os, vstore.regions[i][j].peakCoord);
	    put2dot14 (os, vstore.regions[i][j].endCoord);
	}
    }

    for (size_t i=0; i<vstore.data.size (); i++) {
	cur_pos = buf.pos ();
	buf.seek (init_pos + 10 + i*4);
	os << (uint32_t) (cur_pos - init_pos - 2);
	buf.seek (cur_pos);

	os << (uint16_t) vstore.data[i].deltaSets.size ();
	os << (uint16_t) vstore.data[i].shortDeltaCount;
	os << (uint16_t) vstore.data[i].regionIndexes.size ();
	for (size_t j=0; j<vstore.data[i].regionIndexes.size (); j++)
	    os << vstore.data[i].regionIndexes[j];
	for (size_t j=0; j<vstore.data[i].deltaSets.size (); j++) {
	    for (size_t k=0; k<vstore.data[i].regionIndexes.size (); k++) {
		if (k<vstore.data[i].shortDeltaCount)
		    os << vstore.data[i].deltaSets[j][k];
		else
		    os << (int8_t) vstore.data[i].deltaSets[j][k];
	    }
	}
    }
    cur_pos = buf.pos ();
    buf.seek (init_pos);
    os << (uint16_t) (cur_pos - init_pos - 2);
    buf.seek (cur_pos);
}

void CffTable::readCffNames (std::vector<std::string> &names) {
    uint16_t count;
    uint8_t offsize;
    std::vector<uint32_t> offsets;

    count = getushort (m_pos); m_pos +=2;

    if (count==0)
        return;
    offsets.reserve (count+1);
    offsize = data[m_pos++];
    for (uint16_t i=0; i<=count; ++i) {
	offsets.push_back (getoffset (m_pos, offsize));
        m_pos+= offsize;
    }
    names.reserve (count);
    for (uint16_t i=0; i<count; ++i) {
	if (offsets[i+1]<offsets[i]) {
            /* GT: The CFF font type contains a thing called a name INDEX, and that INDEX */
            /* GT: is bad. It is an index of many of the names used in the CFF font. */
            /* GT: We hope the user will never see this. */
            FontShepherd::postError (
                tr ("Bad CFF table"), tr ("Bad CFF name INDEX"), container->parent ());
	    m_bad_cff = true;
	    while (i<count) {
                names.push_back ("");
		++i;
	    }
	    --i;
	} else {
	    std::string name (data+m_pos, offsets[i+1]-offsets[i]);
	    names.push_back (name);
	    m_pos += offsets[i+1]-offsets[i];
	}
    }
}

void CffTable::unpackData (sFont *font) {
    if (m_loaded)
        return;
    // reading PS data may fail in various places, so set this flag here
    // (rather than at the end of the function)
    m_loaded = true;
    GlyphContainer::unpackData (font);

    uint8_t version = data[0];
    if (version!=m_version) {
        FontShepherd::postError (
            tr ("Bad CFF table"),
	    tr ("CFF version mismatch"),
	    container->parent ());
        return;
    }

    uint8_t hdr_size = data[2];

    if (m_version < 2) {
	std::vector<std::string> fontnames;
	m_pos = hdr_size;
	readCffNames (fontnames);
	m_core_font.fontname = fontnames[0];
	/* GWW: More than one? Can that even happen in OpenType? */
	if (fontnames.size () > 1) {
	    FontShepherd::postWarning (
		tr ("Unsupported data in CFF"),
		tr ("This CFF table appears to contain %1 fonts. "
		    "I will attempt to use the first one").arg (fontnames.size ()),
		container->parent ());
	}
	uint16_t fcnt = getushort (m_pos); m_pos+=2;
	uint8_t off_size = data[m_pos++];
	uint32_t td_off = getoffset (m_pos, off_size); m_pos+=off_size;
	uint32_t td_size = getoffset (m_pos, off_size) - td_off; m_pos+=off_size;
	if (fcnt > 1) m_pos += (fcnt-1)*off_size;
	readCffTopDict (m_core_font.top_dict, td_size);
	/* GWW: String index is just the same as fontname index */
	readCffNames (m_core_font.strings);
	for (size_t i=0; i<m_core_font.top_dict.size (); i++) {
	    auto &pair = m_core_font.top_dict.by_idx (i);
	    if (pair.second.type () == dt_sid)
		pair.second.sid.str = getsid (pair.second.sid.sid, m_core_font.strings);
	    else if (pair.second.type () == dt_ros) {
		pair.second.ros.registry.str = getsid (pair.second.ros.registry.sid, m_core_font.strings);
		pair.second.ros.order.str = getsid (pair.second.ros.order.sid, m_core_font.strings);
	    }
	}
    } else {
	m_pos = 3;
	uint16_t td_size = getushort (m_pos); m_pos+=2;
	m_pos = hdr_size;
	readCffTopDict (m_core_font.top_dict, td_size);
	m_pos = hdr_size + td_size;
    }
    m_gsubrs = pschars ();
    readCffSubrs (m_gsubrs);
    int cstype = m_core_font.top_dict.has_key (cff::CharstringType) ?
	m_core_font.top_dict[cff::CharstringType].i : 2;
    m_gsubrs.bias = (cstype==1) ? 0 :
            m_gsubrs.cnt < 1240 ? 107 :
            m_gsubrs.cnt <33900 ? 1131 : 32768;

    if (m_version > 1 && m_core_font.top_dict.has_key (cff::vstore)) {
	m_pos = m_core_font.top_dict[cff::vstore].i;
	readvstore (m_core_font.vstore);
	m_core_font.vstore.index = m_core_font.top_dict[cff::vsindex].i;
    }

    /* GWW: Can be many fonts here. Only decompose the one */
    if (m_core_font.top_dict.has_key (cff::CharStrings)) {
	m_pos = m_core_font.top_dict[cff::CharStrings].i;
	readCffSubrs (m_core_font.glyphs);
    }

    if (m_version < 2) {
	if (m_core_font.top_dict.has_key (cff::Private)) {
	    uint32_t p_size = m_core_font.top_dict[cff::Private].so.size;
	    uint32_t p_off = m_core_font.top_dict[cff::Private].so.offset;
	    readCffPrivate (m_core_font.private_dict, p_off, p_size);
	    if (m_core_font.private_dict.has_key (cff::Subrs)) {
		m_pos = p_off + m_core_font.private_dict[cff::Subrs].i;
		readCffSubrs (m_core_font.local_subrs);
		m_core_font.local_subrs.bias =
			m_core_font.local_subrs.cnt < 1240 ? 107 :
			m_core_font.local_subrs.cnt <33900 ? 1131 : 32768;
	    }
	}
	if (m_core_font.top_dict.has_key (cff::charset))
	    readCffSet (m_core_font.top_dict[cff::charset].i, m_core_font.glyphs.cnt, m_core_font.charset);
    }

    if (m_core_font.top_dict.has_key (cff::FDArray)) {
	m_pos = m_core_font.top_dict[cff::FDArray].i;
	readSubFonts ();
    }
    if (m_core_font.top_dict.has_key (cff::FDSelect)) {
        m_pos = m_core_font.top_dict[cff::FDSelect].i;
	readfdselect (m_core_font.fdselect, m_core_font.glyphs.cnt);
    }
    m_usable = true;
}

void CffTable::updateGlyph (uint16_t gid) {
    QByteArray ga;
    QBuffer gbuf (&ga);
    QDataStream gstream (&gbuf);
    int sub_idx = 0;
    ConicGlyph *g = m_glyphs[gid];

    if (m_core_font.fdselect.size () > gid)
	sub_idx = m_core_font.fdselect[gid];
    struct pschars &lsubrs = (cidKeyed () || m_version > 1) ?
	m_core_font.subfonts[sub_idx].local_subrs : m_core_font.local_subrs;
    PrivateDict &pdict = (cidKeyed () || m_version > 1) ?
	m_core_font.subfonts[sub_idx].private_dict : m_core_font.private_dict;

    struct cffcontext ctx = {
	m_version,
	0,
	0,
	m_core_font.vstore,
	m_gsubrs,
	lsubrs,
	pdict,
    };
    m_hmtx->setaw (g->gid (), g->advanceWidth ());
    m_hmtx->setlsb (g->gid (), g->leftSideBearing ());

    gbuf.open (QIODevice::WriteOnly);
    g->toPS (gbuf, gstream, ctx);
    gbuf.close ();
    m_core_font.glyphs.css[gid].sdata = ga.toStdString ();
}

void CffTable::packData () {
    QByteArray ba, tda, priva;
    QBuffer buf (&ba);
    QBuffer sec_buf;
    buf.open (QIODevice::WriteOnly);
    QDataStream os (&buf);
    QDataStream sec_os (&sec_buf);
    uint8_t off_size = (m_glyphs.size () > 256) ? 4 : 2;
    uint8_t hdr_size = (m_version > 1) ? 5 : 4;
    auto cs_data_extractor = [](const struct charstring &cs) { return cs.sdata; };

    delete[] data; data = nullptr;

    std::vector<uint16_t> gmod;
    gmod.reserve (m_glyphs.size ());
    for (size_t i=0; i<m_glyphs.size (); i++) {
	ConicGlyph *g = m_glyphs[i];
	if (g->isModified ())
	    gmod.push_back (i);
    }

    // One problem with the CFF format is that we have to rebuild
    // the entire set of charstrings in order to figure out which
    // data should be put to subrs. So if the number of modified
    // glyphs doesn't exceed an arbitrary (small) number, then compile
    // only those glyphs, but without subroutines; otherwise update
    // everything
    if (gmod.size () > 5) {
	if (cidKeyed () || m_version > 1) {
	    for (int i=0; i<numSubFonts (); i++)
		updateCharStrings (m_core_font.glyphs, i, m_version);
	} else
	    updateCharStrings (m_core_font.glyphs, 0, m_version);

	// Currently we don't use gsubrs, so it is safe to clear them after
	// rebuilding charstring data
	m_gsubrs.css.clear ();
	m_gsubrs.cnt = 0;
    } else {
	for (uint16_t gid: gmod)
	    updateGlyph (gid);
    }

    os << (uint8_t) m_version;
    os << (uint8_t) 0;
    os << hdr_size;
    if (m_version < 2) {
	os << off_size;
	// Name INDEX
	os << (uint16_t)1; // Exactly one font name in the INDEX
	os << (uint8_t) 1; // One element in offset array
	os << (uint8_t) 1; // OFF size 1, no need to be more
	os << (uint8_t) (m_core_font.fontname.length () + 1);
	buf.write (m_core_font.fontname.c_str (), m_core_font.fontname.length ());
    }
    if (m_core_font.top_dict.has_key (cff::Private)) {
	sec_buf.setBuffer (&priva);
	sec_buf.open (QIODevice::WriteOnly);
	writeCffPrivate (m_core_font.private_dict, sec_os, sec_buf);
	sec_buf.close ();
	m_core_font.top_dict[cff::Private].so.size = priva.size ();
    }
    sec_buf.setBuffer (&tda);
    sec_buf.open (QIODevice::WriteOnly);
    writeCffTopDict (m_core_font.top_dict, sec_os, sec_buf, off_size);
    sec_buf.close ();
    if (m_version < 2) {
	uint8_t td_off = tda.size () > 0xFFFFFF ? 4 : tda.size () > 0xFFFF ? 3 : tda.size () > 0xFF ? 2 : 1;
	os << (uint16_t)1; // Single TOP Dict
	os << td_off;
	encodeOff (os, td_off, 1);
	encodeOff (os, td_off, tda.size () + 1);
    } else {
	os << (uint16_t) tda.size ();
    }
    int pre_td_pos = buf.pos ();
    buf.write (tda);

    if (m_version < 2) {
	write_string_array (os, buf, m_core_font.strings, m_version);
    }
    {
        std::vector<std::string> ls;
        ls.reserve (m_gsubrs.cnt);
        std::transform (m_gsubrs.css.cbegin (), m_gsubrs.css.cend (),
	    std::back_inserter (ls), cs_data_extractor);
        write_string_array (os, buf, ls, m_version);
    }
    if (m_version > 1 && m_core_font.top_dict.has_key (cff::vstore)) {
	uint32_t cur_pos = buf.pos ();
	buf.seek (m_core_font.top_dict[cff::vstore].i + pre_td_pos);
	encodeSizedInt (os, off_size, cur_pos);
	m_core_font.top_dict[cff::vstore].i = cur_pos;
	buf.seek (cur_pos);

	writevstore (os, buf);
    }
    if (m_core_font.top_dict.has_key (cff::CharStrings)) {
	uint32_t cur_pos = buf.pos ();
	buf.seek (m_core_font.top_dict[cff::CharStrings].i + pre_td_pos);
	encodeSizedInt (os, off_size, cur_pos);
	m_core_font.top_dict[cff::CharStrings].i = cur_pos;
	buf.seek (cur_pos);

        std::vector<std::string> ls;
        ls.reserve (m_core_font.glyphs.cnt);
        std::transform (m_core_font.glyphs.css.cbegin (), m_core_font.glyphs.css.cend (),
	    std::back_inserter (ls), cs_data_extractor);
	write_string_array (os, buf, ls, m_version);
    }
    if (m_version < 2 && m_core_font.top_dict.has_key (cff::Private)) {
	uint32_t cur_pos = buf.pos ();
	buf.seek (m_core_font.top_dict[cff::Private].so.offset + pre_td_pos);
	encodeSizedInt (os, off_size, cur_pos);
	m_core_font.top_dict[cff::Private].so.offset = cur_pos;
	buf.seek (cur_pos);

	buf.write (priva);
	if (m_core_font.private_dict.has_key (cff::Subrs)) {
	    std::vector<std::string> ls;
	    ls.reserve (m_core_font.local_subrs.cnt);
	    std::transform (m_core_font.local_subrs.css.cbegin (), m_core_font.local_subrs.css.cend (),
		std::back_inserter (ls), cs_data_extractor);
	    write_string_array (os, buf, ls, m_version);
	}
    }
    if (m_version < 2 && m_core_font.top_dict.has_key (cff::charset)) {
	uint32_t cur_pos = buf.pos ();
	buf.seek (m_core_font.top_dict[cff::charset].i + pre_td_pos);
	encodeSizedInt (os, off_size, cur_pos);
	m_core_font.top_dict[cff::charset].i = cur_pos;
	buf.seek (cur_pos);

	writeCffSet (os, buf, m_core_font.top_dict[cff::charset].i);
    }
    if (m_core_font.top_dict.has_key (cff::FDArray)) {
	uint32_t cur_pos = buf.pos ();
	buf.seek (m_core_font.top_dict[cff::FDArray].i + pre_td_pos);
	encodeSizedInt (os, off_size, cur_pos);
	m_core_font.top_dict[cff::FDArray].i = cur_pos;
	buf.seek (cur_pos);

	writeSubFonts (os, buf, off_size);
    }
    if (m_core_font.top_dict.has_key (cff::FDSelect)) {
	uint32_t cur_pos = buf.pos ();
	buf.seek (m_core_font.top_dict[cff::FDSelect].i + pre_td_pos);
	encodeSizedInt (os, off_size, cur_pos);
	m_core_font.top_dict[cff::FDSelect].i = cur_pos;
	buf.seek (cur_pos);

	writefdselect (os, buf);
    }
    buf.close ();

    changed = false;
    td_changed = true;
    start = 0xffffffff;
    m_tags[0] = m_version > 1 ? CHR('C','F','F','2') : CHR('C','F','F',' ');

    newlen = ba.length ();
    data = new char[newlen];
    std::copy (ba.begin (), ba.end (), data);
}

ConicGlyph* CffTable::glyph (sFont* fnt, uint16_t gid) {
    if (!m_usable || gid >= m_glyphs.size ())
	return nullptr;
    if (m_glyphs[gid])
        return m_glyphs[gid];

    TopDict &dict = m_core_font.top_dict;
    int sub_idx = 0;
    uint16_t emsize = 1000;
    if (dict.has_key (cff::FontMatrix))
	emsize = rint (1/dict[cff::FontMatrix].list[0]);

    if (m_core_font.fdselect.size () > gid)
	sub_idx = m_core_font.fdselect[gid];
    struct pschars &lsubrs = (cidKeyed () || m_version > 1) ?
	m_core_font.subfonts[sub_idx].local_subrs : m_core_font.local_subrs;
    PrivateDict &pdict = (cidKeyed () || m_version > 1) ?
	m_core_font.subfonts[sub_idx].private_dict : m_core_font.private_dict;

    struct cffcontext ctx = {
	m_version,
	0,
	0,
	m_core_font.vstore,
	m_gsubrs,
	lsubrs,
	pdict,
    };

    BaseMetrics gm = {emsize, fnt->ascent, fnt->descent};
    ConicGlyph *g = glyph_pool.construct (gid, gm);
    m_glyphs[gid] = g;
    if (m_hmtx)
        g->setHMetrics (m_hmtx->lsb (gid), m_hmtx->aw (gid));

    BoostIn buf (m_core_font.glyphs.css[gid].sdata.c_str (), m_core_font.glyphs.css[gid].sdata.length ());

    g->fromPS (buf, ctx);
    if (!g->refs.empty () && fnt->enc->isUnicode ()) {
	for (auto &ref: g->refs) {
	    uint16_t uni = cff::AdobeStandardEncoding[ref.adobe_enc];
	    ref.GID = fnt->enc->gidByUnicode (uni);
	}
    }
    g->setModified (false);
    return g;
}

uint16_t CffTable::addGlyph (sFont* fnt, uint8_t subfont) {
    BaseMetrics gm = {fnt->units_per_em, fnt->ascent, fnt->descent};
    uint16_t gid = m_glyphs.size ();
    ConicGlyph *g = glyph_pool.construct (gid, gm);
    int aw = fnt->units_per_em/3;
    g->setHMetrics (aw, aw);
    g->setOutlinesType (OutlinesType::PS);
    m_glyphs.push_back (g);

    if (numSubFonts () > 0 && m_core_font.fdselect.size () > 0) {
	m_core_font.fdselect.resize (gid+1);
	m_core_font.fdselect[gid] = subfont;
    }
    m_core_font.glyphs.css.emplace_back ();
    m_core_font.glyphs.cnt++;
    m_core_font.charset.push_back (0);
    g->m_private = (cidKeyed () || m_version > 1) ?
	&m_core_font.subfonts[subfont].private_dict : &m_core_font.private_dict;

    return (gid);
}

std::string CffTable::glyphName (uint16_t gid) {
    if (m_version > 1 || gid >= m_core_font.charset.size () || cidKeyed ())
        return (std::string ());
    uint16_t idx = m_core_font.charset[gid];
    return getsid (idx, m_core_font.strings);
}

bool CffTable::cidKeyed () const {
    return (m_core_font.fdselect.size () > 0);
}

int CffTable::version () const {
    return m_version;
}

bool CffTable::usable () const {
    return (m_loaded && !m_bad_cff);
}

int CffTable::numSubFonts () const {
    return m_core_font.subfonts.size ();
}

PrivateDict *CffTable::privateDict (uint16_t subidx) {
    if (!cidKeyed () && subidx == 0) {
	if (m_version > 1)
	    return &m_core_font.subfonts[0].private_dict;
	else
	    return &m_core_font.private_dict;
    } else if (subidx < m_core_font.subfonts.size ()) {
	return &m_core_font.subfonts[subidx].private_dict;
    }
    return nullptr;
}

TopDict *CffTable::topDict () {
    return &m_core_font.top_dict;
}

std::string CffTable::fontName () const {
    return m_core_font.fontname;
}

std::string CffTable::subFontName (uint16_t subidx) {
    if (subidx < m_core_font.subfonts.size ()) {
	cff_font &subfont = m_core_font.subfonts[subidx];
	if (subfont.top_dict.has_key (cff::FontName))
	    return subfont.top_dict[cff::FontName].sid.str;
	else {
	    std::stringstream ss;
	    ss << "Subfont " << subidx+1;
	    return ss.str ();
	}
    }
    throw std::out_of_range ("Subfont index is out of range");
}

void CffTable::clearStrings () {
    m_core_font.strings.clear ();
}

// returns sid
int CffTable::addString (const std::string &s) {
    const uint16_t nStdStrings = cff::names.size ();

    for (size_t i=0; i<cff::names.size (); i++) {
	if (s == cff::names[i])
	    return i;
    }
    m_core_font.strings.push_back (s);
    return m_core_font.strings.size () - 1 + nStdStrings;
}

void CffTable::addGlyphName (uint16_t gid, const std::string &name) {
    int sid = addString (name);
    if (m_core_font.charset.size () < (uint16_t) (gid+1))
	m_core_font.charset.resize (gid+1);
    m_core_font.charset[gid] = sid;
}

uint16_t CffTable::fdSelect (uint16_t gid) {
    if (gid < m_core_font.fdselect.size ())
	return m_core_font.fdselect[gid];
    return 0xFFFF;
}

void CffTable::setFdSelect (uint16_t gid, uint16_t val) {
    if (gid > (m_core_font.fdselect.size () + 1))
	m_core_font.fdselect.resize (gid + 1);
    m_core_font.fdselect[gid] = val;
    ConicGlyph *g = m_glyphs[gid];
    g->m_private = &m_core_font.subfonts[val].private_dict;
}

/* Type 2 charstring subroutinizer. Inspired by the code from the OTFCC
 * project and uses the same approach, i. e. SEQUITUR (Nevill-Manning)
 * algorithm (see http://www.eecs.harvard.edu/~michaelm/CS222/sequitur.pdf),
 * but with several modifications intended to improve both consistency
 * and compression ratio. One important difference is that I don't attempt
 * to split subroutines between "global" and "local" (as this difference
 * IMO makes no sense, unless there are multiple subfonts in CFF) and just
 * put everything to local subrs.
 * This version of algorithm gives significantly better compression ratio
 * than one used in FontForge and is only slightly less effective than
 * FontLab and Adobe's makeotf utility. It is also fast enough */

enum class SeqNode {
    GuardNode, GlyphNode, RuleNode, TerminalNode
};

struct seq_node;

struct sub_rule {
    static int rule_cnt;
    static const int min_length = 8;
    static const int min_usecnt = 2;
    static const int max_depth = 10;

    int use_cnt = 0, ID = 0, subrID = -1;
    struct seq_node *head;

    const std::string getCharString (int bias, bool needs_return, bool &endchar, bool pack=true) const;
    int depth () const;
    bool isWrapper () const;
    void handleWrapper ();
};

int sub_rule::rule_cnt = 0;

struct seq_node {
    SeqNode ntype = SeqNode::TerminalNode;
    bool endchar = false;
    int sub_idx = 0, gid = -1;
    std::string sdata, hexdata;
    struct seq_node *prev=nullptr, *next=nullptr;
    struct sub_rule *rule=nullptr, *outer=nullptr;

    void setData (const std::string &s);
    std::string strID () const;
    void insertSingle (seq_node *n);
    // insert two nodes after this node
    void insertDouble (seq_node *n);
    // replace two nodes after this node with the specified node
    void replaceDouble (seq_node *n);
    // replace the node after this node with the specified node (currently unused)
    void replaceSingle (seq_node *n);
    void makeRule (std::map<std::string, struct seq_node*> &dhash,
	boost::object_pool<struct seq_node> &npool, std::deque<struct sub_rule> &rules);
    bool packed ();
};

void seq_node::setData (const std::string &s) {
    std::stringstream ss;
    ss << std::hex << std::setfill ('0');
    for (auto ch: s)
        ss << std::setw (2) << (ch&0xff);

    sdata = s;
    hexdata = ss.str ();
}

// Nodes (both rules and terminal) are identified by their contents,
// while any other details are ignored. So we get identical keys
// even for those segments which have been previously splitted to rules
// by a different way (say (ab)c vs. a(bc)). This is a crucial point
// in order to get same representation for identical glyphs and glyph
// fragments, no matter what was the order of their processing
std::string seq_node::strID () const {
    std::stringstream ss;
    if (ntype == SeqNode::RuleNode) {
	seq_node *subn = rule->head;
	do {
	    if (subn->ntype == SeqNode::RuleNode || subn->ntype == SeqNode::TerminalNode)
		ss << subn->strID ();
	    subn = subn->next;
	} while (subn != rule->head);
    } else {
        ss << "-+-" << hexdata;
    }
    return ss.str ();
}

void seq_node::insertSingle (seq_node *n) {
    n->next = this->next;
    this->next->prev = n;
    this->next = n;
    n->prev = this;
}

// insert two nodes after this node
void seq_node::insertDouble (seq_node *n) {
    n->next->next = this->next;
    this->next->prev = n->next;
    this->next = n;
    n->prev = this;
}

// replace two nodes after this node with the specified node
void seq_node::replaceDouble (seq_node *n) {
    n->next = this->next->next->next;
    n->next->prev = n;
    this->next = n;
    n->prev = this;
}

// replace the node after this node with the specified node
void seq_node::replaceSingle (seq_node *n) {
    n->next = this->next->next;
    n->next->prev = n;
    this->next = n;
    n->prev = this;
}

void seq_node::makeRule (std::map<std::string, struct seq_node*> &dhash,
    boost::object_pool<struct seq_node> &npool,
    std::deque<struct sub_rule> &rules) {
    // first get key, then set node type (otherwise would recur to
    // subnodes which don't exist yet)
    std::string key (this->strID ());
    ntype = SeqNode::RuleNode;

    if (dhash.count (key)) {
        this->rule = dhash[key]->outer;
        this->rule->use_cnt++;
    } else {
        rules.emplace_back ();
        struct sub_rule *r = &rules.back ();
        r->ID = sub_rule::rule_cnt++;
        rule = r;
        this->rule = r;
        this->rule->use_cnt++;

        struct seq_node *guard = npool.construct ();
        guard->ntype = SeqNode::GuardNode;
        struct seq_node *sub = npool.construct ();
        sub->setData (this->sdata);
        guard->next = sub;
        sub->prev = guard;
        sub->next = guard;
        guard->prev = sub;
        r->head = guard;
        sub->outer = r;
        dhash[sub->strID ()] = sub;
    }
    setData (std::string ());
}

bool seq_node::packed () {
    bool ret = (this->prev->ntype == SeqNode::GuardNode &&
    	    this->next->next->ntype == SeqNode::GuardNode);
    return ret;
}

const std::string sub_rule::getCharString (int bias, bool needs_return, bool &endchar, bool pack) const {
    seq_node *n = head->next;
    std::stringstream sout;
    do {
	switch (n->ntype) {
	  case SeqNode::RuleNode:
	    if (pack && n->rule->subrID >= 0) {
		CffTable::encodeInt (sout, n->rule->subrID - bias);
		sout.put (cff::cs::callsubr);
	    } else {
		sout << n->rule->getCharString (bias, false, endchar);
		if (endchar)
		    return sout.str ();
	    }
	    break;
	  case SeqNode::TerminalNode:
	    sout << n->sdata;
	    endchar = n->endchar;
	    if (endchar)
		return sout.str ();
	    break;
	  default:
	    ;
        }
        n = n->next;
    } while (n != head);
    if (needs_return && !endchar)
        sout.put (cff::cs::op_return);
    return sout.str ();
}

int sub_rule::depth () const {
    struct seq_node *tn = this->head;
    int max = 0;
    do {
        int dp = (tn->ntype == SeqNode::RuleNode) ? tn->rule->depth () : 0;
        if (dp > max) max = dp;
        tn = tn->next;
    } while (tn != this->head);
    if (use_cnt >= sub_rule::min_usecnt) max+= 1;
    return max;
}

// Suppose we have a rule which joins either two other rules or a rule
// and a single-byte op without operands (most typically endchar).
// If we convert this rule to a subr it would probably take from 3 to 5 bytes
// ("x callsubr y callsubr return" or "x callsubr endchar") plus an offset to
// the subr, while calling such a subr from another charstring requires
// at least 2 bytes (z callsubr). Thus if there are only two calls to
// a subr, then it would be cheaper in terms of the disk space to place
// the subr contents directly into the corresponding charstrings. On the
// other hand, it is also possible to remove the "inner" subrs placing their
// contents into the "outer" subr. In fact determining which subrs to keep is
// the key point which that be maintained to achieve an effective
// compression instead of making the CFF table even bigger. Now we
// determine this by comparing the use counter and resetting it to zero
// for less commonly used subrs
bool sub_rule::isWrapper () const {
    struct seq_node *tn = this->head;
    bool ret = true;
    do {
	if ((tn->ntype == SeqNode::TerminalNode && !tn->endchar) ||
	    (tn->ntype == SeqNode::RuleNode && tn->rule->use_cnt < sub_rule::min_usecnt))
	    ret = false;
    } while (tn != this->head && ret);
    return ret;
}

void sub_rule::handleWrapper () {
    struct seq_node *tn = this->head;
    int max_used = 0;
    do {
	if (tn->ntype == SeqNode::RuleNode && tn->rule->use_cnt > max_used)
	    max_used = tn->rule->use_cnt;
	tn = tn->next;
    } while (tn != this->head);

    if (max_used <= this->use_cnt) {
	tn = this->head;
	do {
	    if (tn->ntype == SeqNode::RuleNode)
		tn->rule->use_cnt = 0;
	    tn = tn->next;
	} while (tn != this->head);
    } else
	this->use_cnt = 0;
}

#if CS_DEBUG

static void show_graph (struct seq_node *head, int level) {
    struct seq_node *n = head->next;
    while (n != head) {
	for (int i=0; i<level; i++)
	    std::cerr << "\t";
	switch (n->ntype) {
	  case SeqNode::RuleNode:
	    std::cerr << n->strID () << " " << n->rule->subrID
		<< " used: " << n->rule->use_cnt << " depth: " << n->rule->depth () << std::endl;
	    show_graph (n->rule->head, level+1);
	    break;
	  case SeqNode::TerminalNode:
	    std::cerr << print_ps (n->sdata) << std::endl;
	    break;
	  case SeqNode::GlyphNode:
	    std::cerr << "glyph " << n->gid << std::endl;
	    break;
	  default:
	    ;
	}
	n = n->next;
    }
}

#endif

static void add_to_graph (struct seq_node *nhead,
    std::map<std::string, struct seq_node*> &dhash,
    boost::object_pool<struct seq_node> &npool,
    std::deque<struct sub_rule> &rules,
    struct seq_node *node) {

    if (nhead->prev->ntype == SeqNode::TerminalNode || nhead->prev->ntype == SeqNode::RuleNode) {
	std::stringstream skey;
	skey << nhead->prev->strID () << node->strID ();
	if (dhash.count (skey.str ())) {
	    struct seq_node *ins = dhash[skey.str ()];
	    struct sub_rule *r;
	    std::stringstream snextkey, sprevkey;
	    if (!ins->packed ()) {
		sprevkey << ins->prev->strID () << ins->strID ();
		if (dhash.count (sprevkey.str ()) && dhash[sprevkey.str ()] == ins->prev)
		    dhash.erase (sprevkey.str ());
		snextkey << ins->next->strID () << ins->next->next->strID ();
		if (dhash.count (snextkey.str ()) && dhash[snextkey.str ()] == ins->next)
		    dhash.erase (snextkey.str ());

		struct seq_node *repl = npool.construct ();
		ins->prev->replaceDouble (repl);

		rules.emplace_back ();
		r = &rules.back ();
		r->ID = sub_rule::rule_cnt++;
		repl->rule = r;
		repl->ntype = SeqNode::RuleNode;
		r->use_cnt++;

		r->head = npool.construct ();
		r->head->ntype = SeqNode::GuardNode;
		r->head->next = r->head->prev = r->head;
		r->head->insertDouble (ins);

		sprevkey.str (std::string ());
		sprevkey << repl->prev->strID () << repl->strID ();
		if (!dhash.count (sprevkey.str ()))
		    dhash[sprevkey.str ()] = repl->prev;
		snextkey.str (std::string ());
		snextkey << repl->strID () << repl->next->strID ();
		if (!dhash.count (snextkey.str ()))
		    dhash[snextkey.str ()] = repl;

		if (ins->ntype == SeqNode::RuleNode) ins->rule->use_cnt--;
		if (ins->next->ntype == SeqNode::RuleNode) ins->next->rule->use_cnt--;
		ins->outer = r;
	    } else {
		r = ins->outer;
	    }
	    // Remove pair for two previous nodes
	    sprevkey.str (std::string ());
	    sprevkey << nhead->prev->prev->strID () << nhead->prev->strID ();
	    if (dhash.count (sprevkey.str ()) && dhash[sprevkey.str ()] == nhead->prev->prev)
		dhash.erase (sprevkey.str ());

	    // Remove the last node in the list (absorbed by our rule)
	    struct seq_node *del = nhead->prev;
	    nhead->prev->prev->next = nhead;
	    nhead->prev = nhead->prev->prev;
	    npool.free (del);

	    node->ntype = SeqNode::RuleNode;
	    node->setData (std::string ());
	    node->rule = r;
	    r->use_cnt++;

	    // Recursion to check if we can do one more replacement
	    add_to_graph (nhead, dhash, npool, rules, node);
	} else {
	    dhash[skey.str ()] = nhead->prev;
	    nhead->prev->insertSingle (node);
	}
    } else {
        nhead->prev->insertSingle (node);
    }
}

static void make_subrs (struct pschars &chars, struct pschars &subrs,
    struct seq_node *head, std::deque<struct sub_rule> &rules, bool needs_return) {

    int subr_id = 0, gid = 0;
    std::stringstream sout;
    struct seq_node *n = head->next;

    for (auto &rule: rules) {
	if (rule.isWrapper () && rule.use_cnt >= sub_rule::min_usecnt)
	    rule.handleWrapper ();
    }
    for (auto &rule: rules) {
	if (rule.use_cnt >= sub_rule::min_usecnt && rule.depth () < sub_rule::max_depth)
	    rule.subrID = subr_id++;
    }
    subrs.cnt = subr_id;
    subrs.bias = subrs.cnt < 1240 ? 107 : subrs.cnt < 33900 ? 1131 : 32768;
    subrs.css.resize (subrs.cnt);
    for (auto &rule: rules) {
	bool endchar = false;
	if (rule.subrID >= 0)
	    subrs.css[rule.subrID].sdata = rule.getCharString (subrs.bias, needs_return, endchar);
    }

    while (n != head) {
	switch (n->ntype) {
	  case SeqNode::RuleNode:
	    if (n->rule->subrID >= 0) {
		CffTable::encodeInt (sout, n->rule->subrID - subrs.bias);
		sout.put (cff::cs::callsubr);
	    } else {
		bool endchar = false;
		sout << n->rule->getCharString (subrs.bias, false, endchar);
	    }
	    break;
	  case SeqNode::TerminalNode:
	    sout << n->sdata;
	    break;
	  case SeqNode::GlyphNode:
	    if (n->gid > gid) {
		chars.css[gid].sdata = sout.str ();
		sout.str (std::string ());
		gid = n->gid;
	    }
	  default:
	    ;
	}
	n = n->next;
    }
    // last glyph
    chars.css[gid].sdata = sout.str ();
}

void CffTable::updateCharStrings (struct pschars &chars, int sub_idx, double version) {
    std::vector<std::string> st;
    std::map<std::string, struct seq_node*> dhash;
    boost::object_pool<struct seq_node> npool;
    std::deque<struct sub_rule> rules;

    struct pschars &lsubrs = (cidKeyed () || m_version > 1) ?
	m_core_font.subfonts[sub_idx].local_subrs : m_core_font.local_subrs;
    PrivateDict &pdict = (cidKeyed () || m_version > 1) ?
	m_core_font.subfonts[sub_idx].private_dict : m_core_font.private_dict;

    private_entry pe;
    // Charstring builder needs to know defaultWidthX, except in CFF2
    if (m_version < 2 && !pdict.has_key (cff::defaultWidthX)) {
        pe.setType (pt_blend);
        pe.n.base = stdWidth (m_hmtx, sub_idx);
        pdict[cff::defaultWidthX] = pe;
    }
    if (!pdict.has_key (cff::Subrs)) {
        pe.setType (pt_uint);
        pdict[cff::Subrs] = pe;
    }

    struct cffcontext ctx = {
        m_version,
        0,
        0,
        m_core_font.vstore,
        m_gsubrs,
        lsubrs,
        pdict,
    };

    struct seq_node *nhead = npool.construct ();
    nhead->ntype = SeqNode::GuardNode;
    nhead->next = nhead->prev = nhead;

    for (size_t i=0; i<m_glyphs.size (); i++) {
    	uint8_t select = cidKeyed () ? m_core_font.fdselect[i] : 0;
	if (select != sub_idx)
	    continue;
	ConicGlyph *g = m_glyphs[i];
	std::vector<std::pair<int, std::string>> splitted;

	if (pdict.has_key (cff::vsindex))
	    m_core_font.vstore.index = pdict[cff::vsindex].i;
	g->splitToPS (splitted, ctx);

    	// a GlyphNode marks the beginning of a new glyph and stores its gid
	// (charstring index), so that later we know where to output it
	struct seq_node *sep = npool.construct ();
	sep->ntype = SeqNode::GlyphNode;
    	sep->gid = i;
    	nhead->prev->insertSingle (sep);

	for (auto &pair: splitted) {
	    // There is no much difference between endchar and other codes
	    // (especially as in CFF2 it is not used anyway), but we have
	    // to keep track of endchars in order to avoid having both
	    // endchar and return at the end of a subr
	    struct seq_node *node = npool.construct ();
	    node->endchar = (pair.first == cff::cs::endchar);
	    node->setData (pair.second);
	    if (node->sdata.length () > sub_rule::min_length)
		node->makeRule (dhash, npool, rules);
	    add_to_graph (nhead, dhash, npool, rules, node);
	}
    }

    // Don't clear gsubrs at this point, as they may be needed for processing
    // next subfont. Currently we aren't using them anyway, so it's safe to
    // clear them later
    lsubrs.css.clear ();
    lsubrs.cnt = 0;

    make_subrs (chars, lsubrs, nhead, rules, (version < 2));
#if CS_DEBUG
    show_graph (nhead, 0);
#endif
}

// Everything related with conversion from CFF to CFF2 and vice versa

static bool td_op_cff2_compatible (int op) {
    switch (op) {
      case cff::FontMatrix:
      case cff::CharStrings:
      case cff::FDArray:
      case cff::FDSelect:
      case cff::vstore:
	return true;
      default:
	return false;
    }
}

int CffTable::stdWidth (HmtxTable *hmtx, int sub_idx) {
    auto &fdselect = m_core_font.fdselect;
    uint16_t cnt = m_core_font.glyphs.cnt;
    // Sort the array
    std::vector<uint16_t> ws;
    ws.reserve (cnt);
    for (int i=0; i<cnt; i++) {
	if (fdselect.empty () || fdselect[i] == sub_idx)
	    ws.push_back (hmtx->aw (i));
    }
    std::sort (ws.begin (), ws.end ());

    // find the max frequency using linear traversal
    int max_count = 1, res = ws[0], curr_count = 1;
    for (int i=1; i<cnt; i++) {
        if (ws[i] == ws[i-1])
            curr_count++;
        else {
            if (curr_count > max_count) {
                max_count = curr_count;
                res = ws[i-1];
            }
            curr_count = 1;
        }
    }

    // If last element is most frequent
    if (curr_count > max_count) {
        max_count = curr_count;
        res = ws[cnt-1];
    }

    return res;
}

void CffTable::convertToCFF (sFont *fnt, GlyphNameProvider &gnp) {
    auto name = dynamic_cast<NameTable *> (fnt->table (CHR ('n','a','m','e')));
    auto post = dynamic_cast<PostTable *> (fnt->table (CHR ('p','o','s','t')));
    auto os_2 = dynamic_cast<OS_2Table *> (fnt->table (CHR ('O','S','/','2')));
    auto head = dynamic_cast<HeadTable *> (fnt->table (CHR ('h','e','a','d')));

    if (!name || !post || !os_2 || !head || !m_hmtx)
        throw TableDataCompileException ("CFF",
    	"Can't switch to CFF: some required font data not present!");

    m_core_font.fontname = name->bestName (6).toStdString ();
    auto &dict = m_core_font.top_dict;
    dict.clear ();

    top_dict_entry entry;
    if (numSubFonts () > 1) {
        entry.setType (dt_ros);
        entry.ros.registry.str = "Adobe";
        entry.ros.registry.sid = addString (entry.ros.registry.str);
        entry.ros.order.str = "Identity";
        entry.ros.order.sid = addString (entry.ros.registry.str);
        entry.ros.supplement = 0;
        dict[cff::ROS] = entry;
    }

    entry.setType (dt_sid);

    QString qstr = name->bestName (5);
    entry.sid.str = qstr.toStdString ();
    entry.sid.sid = addString (entry.sid.str);
    dict[cff::version] = entry;

    qstr = name->bestName (0);
    entry.sid.str = qstr.toStdString ();
    entry.sid.sid = addString (entry.sid.str);
    dict[cff::Notice] = entry;

    qstr = name->bestName (4);
    entry.sid.str = qstr.toStdString ();
    entry.sid.sid = addString (entry.sid.str);
    dict[cff::FullName] = entry;

    qstr = name->bestName (1);
    entry.sid.str = qstr.toStdString ();
    entry.sid.sid = addString (entry.sid.str);
    dict[cff::FamilyName] = entry;

    uint16_t wc = os_2->usWeightClass ();
    switch (wc) {
      case 100:
        entry.sid.str = "Thin";
        break;
      case 200:
        entry.sid.str = "ExtraLight";
        break;
      case 300:
        entry.sid.str = "Light";
        break;
      case 400:
        entry.sid.str = "Regular";
        break;
      case 500:
        entry.sid.str = "Medium";
        break;
      case 600:
        entry.sid.str = "Semibold";
        break;
      case 700:
        entry.sid.str = "Bold";
        break;
      case 800:
        entry.sid.str = "ExtraBold";
        break;
      case 900:
        entry.sid.str = "Black";
        break;
      default:
        entry.sid.str = "Regular";
    }
    entry.sid.sid = addString (entry.sid.str);
    dict[cff::Weight] = entry;

    entry.setType (dt_float);
    entry.f = post->underlinePosition ();
    dict[cff::UnderlinePosition] = entry;

    entry.f = post->underlineThickness ();
    dict[cff::UnderlineThickness] = entry;

    entry.setType (dt_list);
    entry.list.push_back (head->xMin ());
    entry.list.push_back (head->yMin ());
    entry.list.push_back (head->xMax ());
    entry.list.push_back (head->yMax ());
    dict[cff::FontBBox] = entry;

    entry.setType (dt_uint);
    entry.i = -1;
    dict[cff::charset] = entry;
    dict[cff::CharStrings] = entry;

    if (numSubFonts () > 1) {
        entry.setType (dt_uint);
        entry.i = fnt->glyph_cnt;
        dict[cff::CIDCount] = entry;

        entry.i = -1;
        if (!m_core_font.fdselect.empty ())
    	dict[cff::FDSelect] = entry;
        dict[cff::FDArray] = entry;
    }

    for (int i=0; i<numSubFonts (); i++) {
        auto &pd = m_core_font.subfonts[i].private_dict;
        for (size_t j=0; j<pd.size (); j++) {
	    auto &pair = pd.by_idx (j);
	    auto &entry = pair.second;
	    if (entry.type () == pt_blend) {
		entry.n.deltas.clear ();
	    } else if (entry.type () == pt_blend_list) {
		for (auto &blend: entry.list)
		    blend.deltas.clear ();
	    }
        }
    }

    if (numSubFonts () == 1) {
        top_dict_entry entry;
        entry.setType (dt_size_off);
        entry.so.size = entry.so.offset = -1;
        m_core_font.top_dict[cff::Private] = entry;
        m_core_font.private_dict = m_core_font.subfonts[0].private_dict;
	m_core_font.local_subrs.css = m_core_font.subfonts[0].local_subrs.css;
	m_core_font.local_subrs.cnt = m_core_font.subfonts[0].local_subrs.cnt;
        m_core_font.subfonts[0].local_subrs.css.clear ();
        m_core_font.subfonts.clear ();

        m_core_font.charset.resize (m_core_font.glyphs.cnt);
        for (uint16_t i=0; i<m_core_font.glyphs.cnt; i++) {
	    const std::string &name = gnp.nameByGid (i);
	    addGlyphName (i, name);
        }
    }

    m_core_font.vstore.data.clear ();
    m_core_font.vstore.regions.clear ();
    m_core_font.vstore.format = m_core_font.vstore.index = 0;
}

void CffTable::convertToCFF2 () {
    auto &dict = m_core_font.top_dict;
    for (int i=dict.size () - 1; i>=0; i--) {
        auto &op = dict.by_idx (i).first;
        if (!td_op_cff2_compatible (op))
    	dict.erase (op);
    }
    if (!dict.has_key (cff::FDArray)) {
        top_dict_entry entry;
        entry.setType (dt_uint);
        entry.f = -1;
        dict[cff::FDArray] = entry;
    }
    m_core_font.strings.clear ();
    m_core_font.charset.clear ();

    if (m_core_font.subfonts.empty ()) {
	private_entry pe;

        top_dict_entry entry;
        entry.setType (dt_size_off);
        entry.so.size = entry.so.offset = -1;
        m_core_font.subfonts.emplace_back ();
        m_core_font.subfonts[0].top_dict[cff::Private] = entry;
        m_core_font.subfonts[0].private_dict = m_core_font.private_dict;

        int cnt = m_core_font.local_subrs.cnt;
        m_core_font.subfonts[0].local_subrs.cnt = cnt;
        m_core_font.subfonts[0].local_subrs.css = m_core_font.local_subrs.css;
        m_core_font.subfonts[0].local_subrs.bias = m_core_font.local_subrs.bias;
    }
    m_core_font.local_subrs.cnt = 0;
    m_core_font.local_subrs.bias = 0;
    m_core_font.local_subrs.css.clear ();
    m_core_font.private_dict.clear ();
}


void CffTable::setVersion (double val, sFont *fnt, GlyphNameProvider &gnp) {
    if (val > 1)
	convertToCFF2 ();
    else
	convertToCFF (fnt, gnp);
    // Changing table format invalidates existing glyph charstrings and
    // surbrs, so mark all glyphs as modified
    for (size_t i=0; i<m_glyphs.size (); i++)
	m_glyphs[i]->setModified (true);
    m_version = val;
}

private_entry::private_entry () {
    ptype = pt_uint;
    i = 0;
}

private_entry::private_entry (const private_entry &pe) {
    ptype = pe.ptype;
    switch (ptype) {
      case pt_blend:
        new (&n) blend (pe.n);
        break;
      case pt_blend_list:
        new (&list) std::array<blend, 16> (pe.list);
        break;
      case pt_uint:
        i = pe.i;
        break;
      case pt_bool:
        b = pe.b;
        break;
    }
}

private_entry::~private_entry () {
    switch (ptype) {
      case pt_blend:
        n.~blend ();
        break;
      case pt_blend_list:
        list.~array<blend, 16> ();
        break;
      default:
        ;
    }
}

void private_entry::operator = (const private_entry &pe) {
    if (ptype != pe.ptype)
        setType (pe.ptype);
    switch (ptype) {
      case pt_blend:
        n = pe.n;
        break;
      case pt_blend_list:
        list = pe.list;
        break;
      case pt_uint:
        i = pe.i;
        break;
      case pt_bool:
        b = pe.b;
        break;
    }
}

void private_entry::setType (em_private_type pt) {
    ptype = pt;
    switch (ptype) {
      case pt_blend:
        new (&n) blend ();
        break;
      case pt_blend_list:
        new (&list) std::array<blend, 16> ();
        break;
      case pt_uint:
        i = 0;
        break;
      case pt_bool:
        b = false;
        break;
    }
}

em_private_type private_entry::type () const {
    return ptype;
}

const std::string private_entry::toString () const {
    std::stringstream ss;
    switch (ptype) {
      case pt_blend:
        ss << n.toString ();
        break;
      case pt_blend_list:
        ss << '[';
        for (size_t i=0; i<14 && list[i].valid; i++) {
	    ss << list[i].toString ();
	    if (i<13 && list[i+1].valid)
		ss << ", ";
        }
        ss << ']';
        break;
      case pt_uint:
        ss << i;
        break;
      case pt_bool:
        ss << (b ? "true" : "false");
        break;
    }
    return ss.str ();
}

top_dict_entry::top_dict_entry () {
    de_type = dt_uint;
    i = 0;
}

top_dict_entry::top_dict_entry (const top_dict_entry &de) {
    de_type = de.de_type;
    switch (de_type) {
      case dt_uint:
        i = de.i;
        break;
      case dt_bool:
        b = de.b;
        break;
      case dt_float:
        f = de.f;
        break;
      case dt_list:
        new (&list) std::vector<double> (de.list);
        break;
      case dt_sid:
        new (&sid) cff_sid (de.sid);
        break;
      case dt_size_off:
        new (&so) size_off (de.so);
        break;
      case dt_ros:
        new (&ros) ros_info (de.ros);
        break;
    }
}

top_dict_entry::~top_dict_entry () {
    switch (de_type) {
      case dt_list:
        list.~vector<double> ();
        break;
      case dt_sid:
        sid.~cff_sid ();
        break;
      case dt_ros:
        ros.~ros_info ();
        break;
      default:
	;
    }
}

void top_dict_entry::operator = (const top_dict_entry &de) {
    if (de_type != de.de_type)
        setType (de.de_type);
    switch (de_type) {
      case dt_uint:
        i = de.i;
        break;
      case dt_bool:
        b = de.b;
        break;
      case dt_float:
        f = de.f;
        break;
      case dt_list:
        list = de.list;
        break;
      case dt_sid:
        sid = de.sid;
        break;
      case dt_size_off:
        so = de.so;
        break;
      case dt_ros:
        ros = de.ros;
        break;
    }
}

void top_dict_entry::setType (em_dict_entry_type dt) {
    de_type = dt;
    switch (de_type) {
      case dt_uint:
        i = 0;
        break;
      case dt_float:
        f = 0;
        break;
      case dt_bool:
        b = false;
        break;
      case dt_list:
        new (&list) std::vector<double> ();
	list.reserve (20);
        break;
      case dt_sid:
        new (&sid) cff_sid ();
	sid.sid = 0xffffffff;
        break;
      case dt_size_off:
        new (&so) size_off ({ 0, 0 });
        break;
      case dt_ros:
        new (&ros) ros_info ({ cff_sid (), cff_sid (), 0 });
        break;
    }
}

em_dict_entry_type top_dict_entry::type () const {
    return de_type;
}

const std::string top_dict_entry::toString () const {
    std::stringstream ss;
    switch (de_type) {
      case dt_uint:
        ss << i;
        break;
      case dt_bool:
        ss << (b ? "true" : "false");
        break;
      case dt_float:
        ss << f;
        break;
      case dt_list:
	ss << '[';
	for (size_t i=0; i<list.size (); i++) {
	    ss << list[i];
	    if (i<list.size ()-1) ss << ", ";
	}
	ss << ']';
        break;
      case dt_sid:
        ss << sid.str;
        break;
      case dt_size_off:
	ss << so.size << " bytes at offset " << so.offset;
        break;
      case dt_ros:
	ss << ros.registry.str << "-" << ros.order.str << "-" << ros.supplement;
        break;
    }
    return ss.str ();
}

const std::string blend::toString () const {
    std::stringstream ss;
    ss << base;
    if (deltas.size ()) {
        ss << '<';
        for (size_t i=0; i<deltas.size (); i++) {
	    ss << deltas[i];
	    if (i<deltas.size ()-1)
		ss << ", ";
        }
        ss << '>';
    }
    return ss.str ();
}

template <class T1, class T2>
PseudoMap<T1, T2>::PseudoMap () {}

template <class T1, class T2>
PseudoMap<T1, T2>::PseudoMap (const PseudoMap<T1, T2> &other) {
    this->m_list = other.m_list;
}

template <class T1, class T2>
size_t PseudoMap<T1, T2>::size () {
    return m_list.size ();
}

template <class T1, class T2>
bool PseudoMap<T1, T2>::has_key (T1 key) const {
    for (auto &pair : m_list) {
	if (pair.first == key)
	    return true;
    }
    return false;
}

template <class T1, class T2>
void PseudoMap<T1, T2>::reserve (size_t cap) {
    m_list.reserve (cap);
}

template <class T1, class T2>
const T2 &PseudoMap<T1, T2>::get (T1 key) const {
    for (auto &pair: m_list) {
	if (pair.first == key)
	    return pair.second;
    }
    throw std::out_of_range ("Array subscript is out of range");
}

template <class T1, class T2>
void PseudoMap<T1, T2>::set_value (T1 key, T2 val) {
    for (auto &pair : m_list) {
	if (pair.first == key) {
	    pair.second = val;
	    return;
	}
    }
    m_list.push_back ({ key, val });
}

template <class T1, class T2>
std::pair<T1, T2> &PseudoMap<T1, T2>::by_idx (size_t idx) {
    if (idx < m_list.size ())
	return m_list[idx];
    throw std::out_of_range ("Array subscript is out of range");
}

template <class T1, class T2>
void PseudoMap<T1, T2>::clear () {
    m_list.clear ();
}

template <class T1, class T2>
void PseudoMap<T1, T2>::erase (T1 key) {
    for (size_t i=0; i<m_list.size (); i++) {
	if (m_list[i].first == key) {
	    m_list.erase (m_list.begin () + i);
	    return;
	}
    }
    throw std::out_of_range ("Array subscript is out of range");
}

template <class T1, class T2>
const T2 &PseudoMap<T1, T2>::operator [](T1 key) const {
    for (auto &pair: m_list) {
	if (pair.first == key)
	    return pair.second;
    }
    throw std::out_of_range ("Array subscript is out of range");
}

template <class T1, class T2>
T2 &PseudoMap<T1, T2>::operator [](T1 key) {
    for (auto &pair: m_list) {
	if (pair.first == key)
	    return pair.second;
    }
    m_list.push_back ({ key, T2 () });
    return m_list.back ().second;
}

template <class T1, class T2>
PseudoMap<T1, T2> & PseudoMap<T1, T2>::operator = (const PseudoMap<T1, T2> &other) {
    this->m_list = other.m_list;
    return *this;
}

template class PseudoMap<int, private_entry>;
template class PseudoMap<int, top_dict_entry>;
