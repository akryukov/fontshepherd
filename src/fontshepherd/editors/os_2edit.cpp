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

#include <cstdio>
#include <sstream>
#include <iostream>
#include <assert.h>
#include <stdint.h>
#include <limits>
#include <unicode/uchar.h>

#include "sfnt.h"
#include "editors/os_2edit.h" // also includes tables.h
#include "tables/os_2.h"
#include "editors/unispinbox.h"

#include "fs_notify.h"

QVector<QPair<QString, int>> OS_2Edit::usWeightList = {
    { "Thin", 100 },
    { "Extra-light", 200 },
    { "Light", 300 },
    { "Regular", 400 },
    { "Medium", 500 },
    { "Semi-bold", 600 },
    { "Bold", 700 },
    { "Extra-bold", 800 },
    { "Black", 900 },
};

QVector<QPair<QString, int>> OS_2Edit::usWidthList = {
    { "Ultra-condensed", 1 },
    { "Extra-condensed", 2 },
    { "Condensed", 3 },
    { "Semi-condensed", 4 },
    { "Medium", 5 },
    { "Semi-expanded", 6 },
    { "Expanded", 7 },
    { "Extra-expanded", 8 },
    { "Ultra-expanded", 9 },
};

QVector<QPair<QString, int>> OS_2Edit::fsRestrictionsList = {
    { "Installable embedding", 0 },
    { "Restricted License embedding", 2 },
    { "Preview & Print embedding", 4 },
    { "Editable embedding", 8 },
};

QVector<QPair<QString, int>> OS_2Edit::ibmFamList = {
    { "No Classification", 0 },
    { "Oldstyle Serifs", 1 },
    { "Transitional Serifs", 2 },
    { "Modern Serifs", 3 },
    { "Clarendon Serifs", 4 },
    { "Slab Serifs", 5 },
    { "(Reserved for future use)", 6 },
    { "Freeform Serifs", 7 },
    { "Sans Serif", 8 },
    { "Ornamentals", 9 },
    { "Scripts", 10 },
    { "(Reserved for future use)", 11 },
    { "Symbolic", 12 },
    { "(Reserved for future use)", 13 },
    { "(Reserved for future use)", 14 },
    { "(Reserved for future use)", 15 },
};

QVector<QPair<QString, int>> OS_2Edit::ibmSubFamListDefault = {
    { " 0: No Classification", 0 },
    { " 1: Reserved for future use", 1 },
    { " 2: Reserved for future use", 2 },
    { " 3: Reserved for future use", 3 },
    { " 4: Reserved for future use", 4 },
    { " 5: Reserved for future use", 5 },
    { " 6: Reserved for future use", 6 },
    { " 7: Reserved for future use", 7 },
    { " 8: Reserved for future use", 8 },
    { " 9: Reserved for future use", 9 },
    { "10: Reserved for future use", 10 },
    { "11: Reserved for future use", 11 },
    { "12: Reserved for future use", 12 },
    { "13: Reserved for future use", 13 },
    { "14: Reserved for future use", 14 },
    { "15: Miscellaneous", 15 },
};

QMap<int, QVector<QPair<QString, int>>> OS_2Edit::ibmSubFamLists = {
    {1, {
	{ "IBM Rounded Legibility", 1 },
	{ "Garalde", 2 },
	{ "Venetian", 3 },
	{ "Modified Venetian", 4 },
	{ "Dutch Modern", 5 },
	{ "Dutch Traditional", 6 },
	{ "Contemporary", 7 },
	{ "Calligraphic", 8 },
    }},
    {2, {
	{ "Direct Line", 1 },
	{ "Script", 2 },
    }},
    {3, {
	{ "Italian", 1 },
	{ "Script", 2 },
    }},
    {4, {
	{ "Clarendon", 1 },
	{ "Modern", 2 },
	{ "Traditional", 3 },
	{ "Newspaper", 4 },
	{ "Stub Serif", 5 },
	{ "Monotone", 6 },
	{ "Typewriter", 7 },
    }},
    {5, {
	{ "Monotone", 1 },
	{ "Humanist", 2 },
	{ "Geometric", 3 },
	{ "Swiss", 4 },
	{ "Typewriter", 5 },
	{ "Monotone", 6 },
	{ "Typewriter", 7 },
    }},
    {7, {
	{ "Modern", 1 },
    }},
    {8, {
	{ "IBM Neo-grotesque Gothic", 1 },
	{ "Humanist", 2 },
	{ "Low-x Round Geometric", 3 },
	{ "High-x Round Geometric", 4 },
	{ "Neo-grotesque Gothic", 5 },
	{ "Modified Neo-grotesque Gothic", 5 },
	{ "Typewriter Gothic", 9 },
	{ "Matrix", 10 },
    }},
    {9, {
	{ "Engraver", 1 },
	{ "Black Letter", 2 },
	{ "Decorative", 3 },
	{ "Three Dimensional", 4 },
    }},
    {10,{
	{ "Uncial", 1 },
	{ "Brush Joined", 2 },
	{ "Formal Joined", 3 },
	{ "Monotone Joined", 4 },
	{ "Calligraphic", 5 },
	{ "Brush Unjoined", 2 },
	{ "Formal Unjoined", 3 },
	{ "Monotone Unjoined", 4 },
    }},
    {12,{
	{ "Mixed Serif", 3 },
	{ "Oldstyle Serif", 6 },
	{ "Neo-grotesque Sans Serif", 7 },
    }},
};

QVector<QPair<QString, int>> OS_2Edit::selectionFlags = {
    { "Italic", 0 },
    { "Underscore", 1 },
    { "Negative", 2 },
    { "Outlined", 3 },
    { "Strikeout", 5 },
    { "Bold", 6 },
    { "Regular", 7 },
    { "Use typo metrics", 8 },
    { "WWS", 9 },
    { "Oblique", 10 },
    { "11: Reserved", 11 },
    { "12: Reserved", 12 },
    { "13: Reserved", 13 },
    { "14: Reserved", 14 },
    { "15: Reserved", 15 },
};

QStringList OS_2Edit::panoseFam = {
    " 0: Any",
    " 1: No Fit",
    " 2: Latin Text",
    " 3: Latin Hand Written",
    " 4: Latin Decorative",
    " 5: Latin Pictoral",
};

QMap<int, QVector<QPair<QString, QVector<QString>>>> OS_2Edit::panose = {
    {2, {
	{QWidget::tr ("Serif Style"), {
	    " 2: Cove",
	    " 3: Obtuse Cove",
	    " 4: Square Cove",
	    " 5: Obtuse Square Cove",
	    " 6: Square",
	    " 7: Thin",
	    " 8: Oval",
	    " 9: Exaggerated",
	    "10: Triangle",
	    "11: Normal Sans",
	    "12: Obtuse Sans",
	    "13: Perpendicular Sans",
	    "14: Flared",
	    "15: Rounded"
	}},
	{QWidget::tr ("Weight"), {
	    " 2: Very Light",
	    " 3: Light",
	    " 4: Thin",
	    " 5: Book",
	    " 6: Medium",
	    " 7: Demi",
	    " 8: Bold",
	    " 9: Heavy",
	    "10: Black",
	    "11: Extra Black",
	}},
	{QWidget::tr ("Contrast"), {
	    " 2: Old Style",
	    " 3: Modern",
	    " 4: Even Width",
	    " 5: Extended",
	    " 6: Condensed",
	    " 7: Very Extended",
	    " 8: Very Condensed",
	    " 9: Monospaced",
	}},
	{QWidget::tr ("Proportion"), {
	    " 2: None",
	    " 3: Very Low",
	    " 4: Low",
	    " 5: Medium Low",
	    " 6: Medium",
	    " 7: Medium High",
	    " 8: High",
	    " 9: Very High",
	}},
	{QWidget::tr ("Stroke Variation"), {
	    " 2: No Variation",
	    " 3: Gradual/Diagonal",
	    " 4: Gradual/Transitional",
	    " 5: Gradual/Vertical",
	    " 6: Gradual/Horizontal",
	    " 7: Rapid/Vertical",
	    " 8: Rapid/Horizontal",
	    " 9: Instant/Vertical",
	    "10: Instant/Horizontal",
	}},
	{QWidget::tr ("Arm Style"), {
	    " 2: Straight Arms/Horizontal",
	    " 3: Straight Arms/Wedge",
	    " 4: Straight Arms/Vertical",
	    " 5: Straight Arms/Single Serif",
	    " 6: Straight Arms/Double Serif",
	    " 7: Non-Straight/Horizontal",
	    " 8: Non-Straight/Wedge",
	    " 9: Non-Straight/Vertical",
	    "10: Non-Straight/Single Serif",
	    "11: Non-Straight/Double Serif",
	}},
	{QWidget::tr ("Letterform"), {
	    " 2: Normal/Contact",
	    " 3: Normal/Weighted",
	    " 4: Normal/Boxed",
	    " 5: Normal/Flattened",
	    " 6: Normal/Rounded",
	    " 7: Normal/Off Center",
	    " 8: Normal/Square",
	    " 9: Oblique/Contact",
	    "10: Oblique/Weighted",
	    "11: Oblique/Boxed",
	    "12: Oblique/Flattened",
	    "13: Oblique/Rounded",
	    "14: Oblique/Off Center",
	    "15: Oblique/Square",
	}},
	{QWidget::tr ("Midline"), {
	    " 2: Standard/Trimmed",
	    " 3: Standard/Pointed",
	    " 4: Standard/Serifed",
	    " 5: High/Trimmed",
	    " 6: High/Pointed",
	    " 7: High/Serifed",
	    " 8: Constant/Trimmed",
	    " 9: Constant/Pointed",
	    "10: Constant/Serifed",
	    "11: Low/Trimmed",
	    "12: Low/Pointed",
	    "13: Low/Serifed",
	}},
	{QWidget::tr ("X-height"), {
	    " 2: Constant/Small",
	    " 3: Constant/Standard",
	    " 4: Constant/Large",
	    " 5: Ducking/Small",
	    " 6: Ducking/Standard",
	    " 7: Ducking/Large",
	}}
    }},
    {3, {
	{QWidget::tr ("Tool kind"), {
	    " 2: Flat Nib",
	    " 3: Pressure Point",
	    " 4: Engraved",
	    " 5: Ball (Round Cap)",
	    " 6: Brush",
	    " 7: Rough",
	    " 8: Felt Pen/Brush Tip",
	    " 9: Wild Brush - Drips a lot",
	}},
	{QWidget::tr ("Weight"), {
	    " 2: Very Light",
	    " 3: Light",
	    " 4: Thin",
	    " 5: Book",
	    " 6: Medium",
	    " 7: Demi",
	    " 8: Bold",
	    " 9: Heavy",
	    "10: Black",
	    "11: Extra Black (Nord)",
	}},
	{QWidget::tr ("Spacing"), {
	    " 2: Proportional Spaced",
	    " 3: Monospaced",
	}},
	{QWidget::tr ("Aspect Ratio"), {
	    " 2: Very Condensed",
	    " 3: Condensed",
	    " 4: Normal",
	    " 5: Expanded",
	    " 6: Very Expanded",
	}},
	{QWidget::tr ("Contrast"), {
	    " 2: None",
	    " 3: Very Low",
	    " 4: Low",
	    " 5: Medium Low",
	    " 6: Medium",
	    " 7: Medium High",
	    " 8: High",
	    " 9: Very High",
	}},
	{QWidget::tr ("Topology"), {
	    " 2: Roman Disconnected",
	    " 3: Roman Trailing",
	    " 4: Roman Connected",
	    " 5: Cursive Disconnected",
	    " 6: Cursive Trailing",
	    " 7: Cursive Connected",
	    " 8: Blackletter Disconnected",
	    " 9: Blackletter Trailing",
	    "10: Blackletter Connected",
	}},
	{QWidget::tr ("Form"), {
	    " 2: Upright / No Wrapping",
	    " 3: Upright / Some Wrapping",
	    " 4: Upright / More Wrapping",
	    " 5: Upright / Extreme Wrapping",
	    " 6: Oblique / No Wrapping",
	    " 7: Oblique / Some Wrapping",
	    " 8: Oblique / More Wrapping",
	    " 9: Oblique / Extreme Wrapping",
	    "10: Exaggerated / No Wrapping",
	    "11: Exaggerated / Some Wrapping",
	    "12: Exaggerated / More Wrapping",
	    "13: Exaggerated / Extreme Wrapping",
	}},
	{QWidget::tr ("Finals"), {
	    " 2: None / No loops",
	    " 3: None / Closed loops",
	    " 4: None / Open loops",
	    " 5: Sharp / No loops",
	    " 6: Sharp / Closed loops",
	    " 7: Sharp / Open loops",
	    " 8: Tapered / No loops",
	    " 9: Tapered / Closed loops",
	    "10: Tapered / Open loops",
	    "11: Round / No loops",
	    "12: Round / Closed loops",
	    "13: Round / Open loops",
	}},
	{QWidget::tr ("X-Ascent"), {
	    " 2: Very Low",
	    " 3: Low",
	    " 4: Medium",
	    " 5: High",
	    " 6: Very High",
	}}
    }},
    {4, {
	{QWidget::tr ("Class"), {
	    " 2: Derivative",
	    " 3: Non-standard Topology",
	    " 4: Non-standard Elements",
	    " 5: Non-standard Aspect",
	    " 6: Initials",
	    " 7: Cartoon",
	    " 8: Picture Stems",
	    " 9: Ornamented",
	    "10: Text and Background",
	    "11: Collage",
	    "12: Montage",
	}},
	{QWidget::tr ("Weigth"), {
	    " 2: Very Light",
	    " 3: Light",
	    " 4: Thin",
	    " 5: Book",
	    " 6: Medium",
	    " 7: Demi",
	    " 8: Bold",
	    " 9: Heavy",
	    "10: Black",
	    "11: Extra Black",
	}},
	{QWidget::tr ("Aspect"), {
	    " 2: Super Condensed",
	    " 3: Very Condensed",
	    " 4: Condensed",
	    " 5: Normal",
	    " 6: Extended",
	    " 7: Very Extended",
	    " 8: Super Extended",
	    " 9: Monospaced",
	}},
	{QWidget::tr ("Contrast"), {
	    " 2: None",
	    " 3: Very Low",
	    " 4: Low",
	    " 5: Medium Low",
	    " 6: Medium",
	    " 7: Medium High",
	    " 8: High",
	    " 9: Very High",
	    "10: Horizontal Low",
	    "11: Horizontal Medium",
	    "12: Horizontal High",
	    "13: Broken",
	}},
	{QWidget::tr ("Serif Variant"), {
	    " 2: Cove",
	    " 3: Obtuse Cove",
	    " 4: Square Cove",
	    " 5: Obtuse Square Cove",
	    " 6: Square",
	    " 7: Thin",
	    " 8: Oval",
	    " 9: Exaggerated",
	    "10: Triangle",
	    "11: Normal Sans",
	    "12: Obtuse Sans",
	    "13: Perpendicular Sans",
	    "14: Flared",
	    "15: Rounded",
	    "16: Script",
	}},
	{QWidget::tr ("Treatment"), {
	    " 2: None - Standard Solid Fill",
	    " 3: White / No Fill",
	    " 4: Patterned Fill",
	    " 5: Complex Fill",
	    " 6: Shaped Fill",
	    " 7: Drawn / Distressed",
	}},
	{QWidget::tr ("Lining"), {
	    " 2: None",
	    " 3: Inline",
	    " 4: Outline",
	    " 5: Engraved (Multiple Lines)",
	    " 6: Shadow",
	    " 7: Relief",
	    " 8: Backdrop",
	}},
	{QWidget::tr ("Topology"), {
	    " 2: Standard",
	    " 3: Square",
	    " 4: Multiple Segment",
	    " 5: Deco (E,M,S) Waco midlines",
	    " 6: Uneven Weighting",
	    " 7: Diverse Arms",
	    " 8: Diverse Forms",
	    " 9: Lombardic Forms",
	    "10: Upper Case in Lower Case",
	    "11: Implied Topology",
	    "12: Horseshoe E and A",
	    "13: Cursive",
	    "14: Blackletter",
	    "15: Swash Variance",
	}},
	{QWidget::tr ("Range of Characters"), {
	    " 2: Extended Collection",
	    " 3: Litterals",
	    " 4: No Lower Case",
	    " 5: Small Caps",
	}},
    }},
    {5, {
	{QWidget::tr ("Kind"), {
	    " 2: Montages",
	    " 3: Pictures",
	    " 4: Shapes",
	    " 5: Scientific",
	    " 6: Music",
	    " 7: Expert",
	    " 8: Patterns",
	    " 9: Boarders",
	    "10: Icons",
	    "11: Logos",
	    "12: Industry specific",
	}},
	{QWidget::tr ("Weight"), {
	}},
	{QWidget::tr ("Spacing"), {
	    " 0: Any",
	    " 1: No fit",
	    " 2: Proportional Spaced",
	    " 3: Monospaced",
	}},
	{QWidget::tr ("Aspect ratio & contrast"), {
	}},
	{QWidget::tr ("Aspect ratio of character 94"), {
	    " 0: Any",
	    " 1: No Fit",
	    " 2: No Width",
	    " 3: Exceptionally Wide",
	    " 4: Super Wide",
	    " 5: Very Wide",
	    " 6: Wide",
	    " 7: Normal",
	    " 8: Narrow",
	    " 9: Very Narrow",
	}},
	{QWidget::tr ("Aspect ratio of character 119"), {
	    " 0: Any",
	    " 1: No Fit",
	    " 2: No Width",
	    " 3: Exceptionally Wide",
	    " 4: Super Wide",
	    " 5: Very Wide",
	    " 6: Wide",
	    " 7: Normal",
	    " 8: Narrow",
	    " 9: Very Narrow",
	}},
	{QWidget::tr ("Aspect ratio of character 157"), {
	    " 0: Any",
	    " 1: No Fit",
	    " 2: No Width",
	    " 3: Exceptionally Wide",
	    " 4: Super Wide",
	    " 5: Very Wide",
	    " 6: Wide",
	    " 7: Normal",
	    " 8: Narrow",
	    " 9: Very Narrow",
	}},
	{QWidget::tr ("Aspect ratio of character 163"), {
	    " 0: Any",
	    " 1: No Fit",
	    " 2: No Width",
	    " 3: Exceptionally Wide",
	    " 4: Super Wide",
	    " 5: Very Wide",
	    " 6: Wide",
	    " 7: Normal",
	    " 8: Narrow",
	    " 9: Very Narrow",
	}},
	{QWidget::tr ("Aspect ratio of character 211"), {
	    " 0: Any",
	    " 1: No Fit",
	    " 2: No Width",
	    " 3: Exceptionally Wide",
	    " 4: Super Wide",
	    " 5: Very Wide",
	    " 6: Wide",
	    " 7: Normal",
	    " 8: Narrow",
	    " 9: Very Narrow",
	}},
    }},
};

QVector<QPair<QString, int>> OS_2Edit::codepageList = {
    { "1252: Latin 1", 0 },
    { "1250: Latin 2: Eastern Europe", 1 },
    { "1251: Cyrillic", 2 },
    { "1253: Greek", 3 },
    { "1254: Turkish", 4 },
    { "1255: Hebrew", 5 },
    { "1256: Arabic", 6 },
    { "1257: Windows Baltic", 7 },
    { "1258: Vietnamese", 8 },
    { "(Reserved for alternate ANSI)", 9 },
    { "(Reserved for alternate ANSI)", 10 },
    { "(Reserved for alternate ANSI)", 11 },
    { "(Reserved for alternate ANSI)", 12 },
    { "(Reserved for alternate ANSI)", 13 },
    { "(Reserved for alternate ANSI)", 14 },
    { "(Reserved for alternate ANSI)", 15 },
    { " 874: Thai", 16 },
    { " 932: JIS/Japan", 17 },
    { " 936: Chinese: Simplified chars—PRC and Singapore", 18 },
    { " 949: Korean Wansung", 19 },
    { " 950: Chinese: Traditional chars—Taiwan and Hong Kong", 20 },
    { "1361: Korean Johab", 21 },
    { "(Reserved for alternate ANSI or OEM)", 22 },
    { "(Reserved for alternate ANSI or OEM)", 23 },
    { "(Reserved for alternate ANSI or OEM)", 24 },
    { "(Reserved for alternate ANSI or OEM)", 25 },
    { "(Reserved for alternate ANSI or OEM)", 26 },
    { "(Reserved for alternate ANSI or OEM)", 27 },
    { "(Reserved for alternate ANSI or OEM)", 28 },
    { "Macintosh Character Set (US Roman)", 29 },
    { "OEM Character Set", 30 },
    { "Symbol Character Set", 31 },
    { "(Reserved for alternate OEM)", 32 },
    { "(Reserved for alternate OEM)", 33 },
    { "(Reserved for alternate OEM)", 34 },
    { "(Reserved for alternate OEM)", 35 },
    { "(Reserved for alternate OEM)", 36 },
    { "(Reserved for alternate OEM)", 37 },
    { "(Reserved for alternate OEM)", 38 },
    { "(Reserved for alternate OEM)", 39 },
    { "(Reserved for alternate OEM)", 40 },
    { "(Reserved for alternate OEM)", 41 },
    { "(Reserved for alternate OEM)", 42 },
    { "(Reserved for alternate OEM)", 43 },
    { "(Reserved for alternate OEM)", 44 },
    { "(Reserved for alternate OEM)", 45 },
    { "(Reserved for alternate OEM)", 46 },
    { "(Reserved for alternate OEM)", 47 },
    { " 869 IBM Greek", 48 },
    { " 866 MS-DOS Russian", 49 },
    { " 865 MS-DOS Nordic", 50 },
    { " 864 Arabic", 51 },
    { " 863 MS-DOS Canadian French", 52 },
    { " 862 Hebrew", 53 },
    { " 861 MS-DOS Icelandic", 54 },
    { " 860 MS-DOS Portuguese", 55 },
    { " 857 IBM Turkish", 56 },
    { " 855 IBM Cyrillic; primarily Russian", 57 },
    { " 852 Latin 2", 58 },
    { " 775 MS-DOS Baltic", 59 },
    { " 737 Greek; former 437 G", 60 },
    { " 708 Arabic; ASMO 708", 61 },
    { " 850 WE/Latin 1", 62 },
    { " 437 US", 63 },
};

QVector<QVector <uni_range>> OS_2Edit::uniRangeList = {
	{   { QWidget::tr ("Basic Latin"), 0x0000, 0x007F }},
	{   { QWidget::tr ("Latin-1 Supplement"), 0x0080, 0x00FF }},
	{   { QWidget::tr ("Latin Extended-A"), 0x0100, 0x017F }},
	{   { QWidget::tr ("Latin Extended-B"), 0x0180, 0x024F }},
	{   { QWidget::tr ("IPA Extensions"), 0x0250, 0x02AF },
	    { QWidget::tr ("Phonetic Extensions"), 0x1D00, 0x1D7F },
	    { QWidget::tr ("Phonetic Extensions Supplement"), 0x1D80, 0x1DBF }},
	{   { QWidget::tr ("Spacing Modifier Letters"), 0x02B0, 0x02FF },
	    { QWidget::tr ("Modifier Tone Letters"), 0xA700, 0xA71F }},
	{   { QWidget::tr ("Combining Diacritical Marks"), 0x0300, 0x036F },
	    { QWidget::tr ("Combining Diacritical Marks Supplement"), 0x1DC0, 0x1DFF }},
	{   { QWidget::tr ("Greek and Coptic"), 0x0370, 0x03FF }},
	{   { QWidget::tr ("Coptic"), 0x2C80, 0x2CFF }},
	{   { QWidget::tr ("Cyrillic"), 0x0400, 0x04FF },
	    { QWidget::tr ("Cyrillic Supplement"), 0x0500, 0x052F },
	    { QWidget::tr ("Cyrillic Extended-A"), 0x2DE0, 0x2DFF },
	    { QWidget::tr ("Cyrillic Extended-B"), 0xA640, 0xA69F }},
	{   { QWidget::tr ("Armenian"), 0x0530, 0x058F }},
	{   { QWidget::tr ("Hebrew"), 0x0590, 0x05FF }},
	{   { QWidget::tr ("Vai"), 0xA500, 0xA63F }},
	{   { QWidget::tr ("Arabic"), 0x0600, 0x06FF },
	    { QWidget::tr ("Arabic Supplement"), 0x0750, 0x077F }},
	{   { QWidget::tr ("NKo"), 0x07C0, 0x07FF }},
	{   { QWidget::tr ("Devanagari"), 0x0900, 0x097F }},
	{   { QWidget::tr ("Bengali"), 0x0980, 0x09FF }},
	{   { QWidget::tr ("Gurmukhi"), 0x0A00, 0x0A7F }},
	{   { QWidget::tr ("Gujarati"), 0x0A80, 0x0AFF }},
	{   { QWidget::tr ("Oriya"), 0x0B00, 0x0B7F }},
	{   { QWidget::tr ("Tamil"), 0x0B80, 0x0BFF }},
	{   { QWidget::tr ("Telugu"), 0x0C00, 0x0C7F }},
	{   { QWidget::tr ("Kannada"), 0x0C80, 0x0CFF }},
	{   { QWidget::tr ("Malayalam"), 0x0D00, 0x0D7F }},
	{   { QWidget::tr ("Thai"), 0x0E00, 0x0E7F }},
	{   { QWidget::tr ("Lao"), 0x0E80, 0x0EFF }},
	{   { QWidget::tr ("Georgian"), 0x10A0, 0x10FF },
	    { QWidget::tr ("Georgian Supplement"), 0x2D00, 0x2D2F }},
	{   { QWidget::tr ("Balinese"), 0x1B00, 0x1B7F }},
	{   { QWidget::tr ("Hangul Jamo"), 0x1100, 0x11FF }},
	{   { QWidget::tr ("Latin Extended Additional"), 0x1E00, 0x1EFF },
	    { QWidget::tr ("Latin Extended-C"), 0x2C60, 0x2C7F },
	    { QWidget::tr ("Latin Extended-D"), 0xA720, 0xA7FF }},
	{   { QWidget::tr ("Greek Extended"), 0x1F00, 0x1FFF }},
	{   { QWidget::tr ("General Punctuation"), 0x2000, 0x206F },
	    { QWidget::tr ("Supplemental Punctuation"), 0x2E00, 0x2E7F }},
	{   { QWidget::tr ("Superscripts And Subscripts"), 0x2070, 0x209F }},
	{   { QWidget::tr ("Currency Symbols"), 0x20A0, 0x20CF }},
	{   { QWidget::tr ("Combining Diacritical Marks For Symbols"), 0x20D0, 0x20FF }},
	{   { QWidget::tr ("Letterlike Symbols"), 0x2100, 0x214F }},
	{   { QWidget::tr ("Number Forms"), 0x2150, 0x218F }},
	{   { QWidget::tr ("Arrows"), 0x2190, 0x21FF },
	    { QWidget::tr ("Supplemental Arrows-A"), 0x27F0, 0x27FF },
	    { QWidget::tr ("Supplemental Arrows-B"), 0x2900, 0x297F },
	    { QWidget::tr ("Miscellaneous Symbols and Arrows"), 0x2B00, 0x2BFF }},
	{   { QWidget::tr ("Mathematical Operators"), 0x2200, 0x22FF },
	    { QWidget::tr ("Supplemental Mathematical Operators"), 0x2A00, 0x2AFF },
	    { QWidget::tr ("Miscellaneous Mathematical Symbols-A"), 0x27C0, 0x27EF },
	    { QWidget::tr ("Miscellaneous Mathematical Symbols-B"), 0x2980, 0x29FF }},
	{   { QWidget::tr ("Miscellaneous Technical"), 0x2300, 0x23FF }},
	{   { QWidget::tr ("Control Pictures"), 0x2400, 0x243F }},
	{   { QWidget::tr ("Optical Character Recognition"), 0x2440, 0x245F }},
	{   { QWidget::tr ("Enclosed Alphanumerics"), 0x2460, 0x24FF }},
	{   { QWidget::tr ("Box Drawing"), 0x2500, 0x257F }},
	{   { QWidget::tr ("Block Elements"), 0x2580, 0x259F }},
	{   { QWidget::tr ("Geometric Shapes"), 0x25A0, 0x25FF }},
	{   { QWidget::tr ("Miscellaneous Symbols"), 0x2600, 0x26FF }},
	{   { QWidget::tr ("Dingbats"), 0x2700, 0x27BF }},
	{   { QWidget::tr ("CJK Symbols And Punctuation"), 0x3000, 0x303F }},
	{   { QWidget::tr ("Hiragana"), 0x3040, 0x309F }},
	{   { QWidget::tr ("Katakana"), 0x30A0, 0x30FF },
	    { QWidget::tr ("Katakana Phonetic Extensions"), 0x31F0, 0x31FF }},
	{   { QWidget::tr ("Bopomofo"), 0x3100, 0x312F },
	    { QWidget::tr ("Bopomofo Extended"), 0x31A0, 0x31BF }},
	{   { QWidget::tr ("Hangul Compatibility Jamo"), 0x3130, 0x318F }},
	{   { QWidget::tr ("Phags-pa"), 0xA840, 0xA87F }},
	{   { QWidget::tr ("Enclosed CJK Letters And Months"), 0x3200, 0x32FF }},
	{   { QWidget::tr ("CJK Compatibility"), 0x3300, 0x33FF }},
	{   { QWidget::tr ("Hangul Syllables"), 0xAC00, 0xD7AF }},
	{   { QWidget::tr ("Non-Plane 0"), 0x10000, 0x10FFFF }},
	{   { QWidget::tr ("Phoenician"), 0x10900, 0x1091F }},
	{   { QWidget::tr ("CJK Unified Ideographs"), 0x4E00, 0x9FFF },
	    { QWidget::tr ("CJK Radicals Supplement"), 0x2E80, 0x2EFF },
	    { QWidget::tr ("Kangxi Radicals"), 0x2F00, 0x2FDF },
	    { QWidget::tr ("Ideographic Description Characters"), 0x2FF0, 0x2FFF },
	    { QWidget::tr ("CJK Unified Ideographs Extension A"), 0x3400, 0x4DBF },
	    { QWidget::tr ("CJK Unified Ideographs Extension B"), 0x20000, 0x2A6DF },
	    { QWidget::tr ("Kanbun"), 0x3190, 0x319F }},
	{   { QWidget::tr ("Private Use Area (plane 0)"), 0xE000, 0xF8FF }},
	{   { QWidget::tr ("CJK Strokes"), 0x31C0, 0x31EF },
	    { QWidget::tr ("CJK Compatibility Ideographs"), 0xF900, 0xFAFF },
	    { QWidget::tr ("CJK Compatibility Ideographs Supplement"), 0x2F800, 0x2FA1F }},
	{   { QWidget::tr ("Alphabetic Presentation Forms"), 0xFB00, 0xFB4F }},
	{   { QWidget::tr ("Arabic Presentation Forms-A"), 0xFB50, 0xFDFF }},
	{   { QWidget::tr ("Combining Half Marks"), 0xFE20, 0xFE2F }},
	{   { QWidget::tr ("Vertical Forms"), 0xFE10, 0xFE1F },
	    { QWidget::tr ("CJK Compatibility Forms"), 0xFE30, 0xFE4F }},
	{   { QWidget::tr ("Small Form Variants"), 0xFE50, 0xFE6F }},
	{   { QWidget::tr ("Arabic Presentation Forms-B"), 0xFE70, 0xFEFF }},
	{   { QWidget::tr ("Halfwidth And Fullwidth Forms"), 0xFF00, 0xFFEF }},
	{   { QWidget::tr ("Specials"), 0xFFF0, 0xFFFF }},
	{   { QWidget::tr ("Tibetan"), 0x0F00, 0x0FFF }},
	{   { QWidget::tr ("Syriac"), 0x0700, 0x074F }},
	{   { QWidget::tr ("Thaana"), 0x0780, 0x07BF }},
	{   { QWidget::tr ("Sinhala"), 0x0D80, 0x0DFF }},
	{   { QWidget::tr ("Myanmar"), 0x1000, 0x109F }},
	{   { QWidget::tr ("Ethiopic"), 0x1200, 0x137F },
	    { QWidget::tr ("Ethiopic Supplement"), 0x1380, 0x139F },
	    { QWidget::tr ("Ethiopic Extended"), 0x2D80, 0x2DDF }},
	{   { QWidget::tr ("Cherokee"), 0x13A0, 0x13FF }},
	{   { QWidget::tr ("Unified Canadian Aboriginal Syllabics"), 0x1400, 0x167F }},
	{   { QWidget::tr ("Ogham"), 0x1680, 0x169F }},
	{   { QWidget::tr ("Runic"), 0x16A0, 0x16FF }},
	{   { QWidget::tr ("Khmer"), 0x1780, 0x17FF },
	    { QWidget::tr ("Khmer Symbols"), 0x19E0, 0x19FF }},
	{   { QWidget::tr ("Mongolian"), 0x1800, 0x18AF }},
	{   { QWidget::tr ("Braille Patterns"), 0x2800, 0x28FF }},
	{   { QWidget::tr ("Yi Syllables"), 0xA000, 0xA48F },
	    { QWidget::tr ("Yi Radicals"), 0xA490, 0xA4CF }},
	{   { QWidget::tr ("Tagalog"), 0x1700, 0x171F },
	    { QWidget::tr ("Hanunoo"), 0x1720, 0x173F },
	    { QWidget::tr ("Buhid"), 0x1740, 0x175F },
	    { QWidget::tr ("Tagbanwa"), 0x1760, 0x177F }},
	{   { QWidget::tr ("Old Italic"), 0x10300, 0x1032F }},
	{   { QWidget::tr ("Gothic"), 0x10330, 0x1034F }},
	{   { QWidget::tr ("Deseret"), 0x10400, 0x1044F }},
	{   { QWidget::tr ("Byzantine Musical Symbols"), 0x1D000, 0x1D0FF },
	    { QWidget::tr ("Musical Symbols"), 0x1D100, 0x1D1FF },
	    { QWidget::tr ("Ancient Greek Musical Notation"), 0x1D200, 0x1D24F }},
	{   { QWidget::tr ("Mathematical Alphanumeric Symbols"), 0x1D400, 0x1D7FF }},
	{   { QWidget::tr ("Private Use (plane 15)"), 0xF0000, 0xFFFFD },
	    { QWidget::tr ("Private Use (plane 16)"), 0x100000, 0x10FFFD }},
	{   { QWidget::tr ("Variation Selectors"), 0xFE00, 0xFE0F },
	    { QWidget::tr ("Variation Selectors Supplement"), 0xE0100, 0xE01EF }},
	{   { QWidget::tr ("Tags"), 0xE0000, 0xE007F }},
	{   { QWidget::tr ("Limbu"), 0x1900, 0x194F }},
	{   { QWidget::tr ("Tai Le"), 0x1950, 0x197F }},
	{   { QWidget::tr ("New Tai Lue"), 0x1980, 0x19DF }},
	{   { QWidget::tr ("Buginese"), 0x1A00, 0x1A1F }},
	{   { QWidget::tr ("Glagolitic"), 0x2C00, 0x2C5F }},
	{   { QWidget::tr ("Tifinagh"), 0x2D30, 0x2D7F }},
	{   { QWidget::tr ("Yijing Hexagram Symbols"), 0x4DC0, 0x4DFF }},
	{   { QWidget::tr ("Syloti Nagri"), 0xA800, 0xA82F }},
	{   { QWidget::tr ("Linear B Syllabary"), 0x10000, 0x1007F },
	    { QWidget::tr ("Linear B Ideograms"), 0x10080, 0x100FF },
	    { QWidget::tr ("Aegean Numbers"), 0x10100, 0x1013F }},
	{   { QWidget::tr ("Ancient Greek Numbers"), 0x10140, 0x1018F }},
	{   { QWidget::tr ("Ugaritic"), 0x10380, 0x1039F }},
	{   { QWidget::tr ("Old Persian"), 0x103A0, 0x103DF }},
	{   { QWidget::tr ("Shavian"), 0x10450, 0x1047F }},
	{   { QWidget::tr ("Osmanya"), 0x10480, 0x104AF }},
	{   { QWidget::tr ("Cypriot Syllabary"), 0x10800, 0x1083F }},
	{   { QWidget::tr ("Kharoshthi"), 0x10A00, 0x10A5F }},
	{   { QWidget::tr ("Tai Xuan Jing Symbols"), 0x1D300, 0x1D35F }},
	{   { QWidget::tr ("Cuneiform"), 0x12000, 0x123FF },
	    { QWidget::tr ("Cuneiform Numbers and Punctuation"), 0x12400, 0x1247F }},
	{   { QWidget::tr ("Counting Rod Numerals"), 0x1D360, 0x1D37F }},
	{   { QWidget::tr ("Sundanese"), 0x1B80, 0x1BBF }},
	{   { QWidget::tr ("Lepcha"), 0x1C00, 0x1C4F }},
	{   { QWidget::tr ("Ol Chiki"), 0x1C50, 0x1C7F }},
	{   { QWidget::tr ("Saurashtra"), 0xA880, 0xA8DF }},
	{   { QWidget::tr ("Kayah Li"), 0xA900, 0xA92F }},
	{   { QWidget::tr ("Rejang"), 0xA930, 0xA95F }},
	{   { QWidget::tr ("Cham"), 0xAA00, 0xAA5F }},
	{   { QWidget::tr ("Ancient Symbols"), 0x10190, 0x101CF }},
	{   { QWidget::tr ("Phaistos Disc"), 0x101D0, 0x101FF }},
	{   { QWidget::tr ("Carian"), 0x102A0, 0x102DF },
	    { QWidget::tr ("Lycian"), 0x10280, 0x1029F },
	    { QWidget::tr ("Lydian"), 0x10920, 0x1093F }},
	{   { QWidget::tr ("Domino Tiles"), 0x1F030, 0x1F09F },
	    { QWidget::tr ("Mahjong Tiles"), 0x1F000, 0x1F02F }},
};

OS_2Edit::OS_2Edit (FontTable* tbl, sFont* font, QWidget *parent) :
    TableEdit (parent, Qt::Window), m_font (font) {
    m_os_2 = dynamic_cast<OS_2Table *> (tbl);

    setAttribute (Qt::WA_DeleteOnClose);
    setWindowTitle (QString ("OS/2 - ").append (m_font->fontname));

    QWidget *window = new QWidget (this);
    m_tab = new QTabWidget (window);

    // General
    QWidget *gen_tab = new QWidget ();

    QGridLayout *gen_layout = new QGridLayout ();
    gen_layout->setAlignment (Qt::AlignTop);
    gen_tab->setLayout (gen_layout);

    gen_layout->addWidget (new QLabel (tr ("OS/2 table version number:")), 0, 0);
    m_versionBox = new QSpinBox ();
    m_versionBox->setMaximum (5);
    gen_layout->addWidget (m_versionBox, 0, 1);
    connect (m_versionBox, static_cast<void (QSpinBox::*)(int)> (&QSpinBox::valueChanged),
	this, &OS_2Edit::setTableVersion);

    gen_layout->addWidget (new QLabel (tr ("Embedding policy:")), 1, 0);
    m_licenseBox = new QComboBox ();
    gen_layout->addWidget (m_licenseBox, 1, 1);

    m_noSubsettingBox = new QCheckBox ();
    m_noSubsettingBox->setText (tr ("No subsetting"));
    gen_layout->addWidget (m_noSubsettingBox, 2, 0);

    m_bitmapsBox = new QCheckBox ();
    m_bitmapsBox->setText (tr ("Only bitmaps"));
    gen_layout->addWidget (m_bitmapsBox, 2, 1);

    gen_layout->addWidget (new QLabel (tr ("Vendor ID:")), 3, 0);
    m_vendorIDBox = new QLineEdit ();
    gen_layout->addWidget (m_vendorIDBox, 3, 1);

    gen_layout->addWidget (new QLabel (tr ("Minimum Unicode index:")), 4, 0);
    m_firstCharBox = new UniSpinBox ();
    m_firstCharBox->setMaximum (0xffffff);
    gen_layout->addWidget (m_firstCharBox, 4, 1);

    gen_layout->addWidget (new QLabel (tr ("Maximum Unicode index:")), 5, 0);
    m_lastCharBox = new UniSpinBox ();
    m_lastCharBox->setMaximum (0xffffff);
    gen_layout->addWidget (m_lastCharBox, 5, 1);

    gen_layout->addWidget (new QLabel (tr ("Default character index:")), 6, 0);
    m_defaultCharBox = new UniSpinBox ();
    m_defaultCharBox->setMaximum (0xffffff);
    gen_layout->addWidget (m_defaultCharBox, 6, 1);

    gen_layout->addWidget (new QLabel (tr ("Default break character index:")), 7, 0);
    m_breakCharBox = new UniSpinBox ();
    m_breakCharBox->setMaximum (0xffffff);
    gen_layout->addWidget (m_breakCharBox, 7, 1);

    gen_layout->addWidget (new QLabel (tr ("Maximum glyph context length:")), 8, 0);
    m_maxContextBox = new QSpinBox ();
    gen_layout->addWidget (m_maxContextBox, 8, 1);

    gen_layout->addWidget (new QLabel (tr ("Minimum optical size:")), 9, 0);
    m_lowerOptSizeBox = new QSpinBox ();
    m_lowerOptSizeBox->setMaximum (0xfffe);
    gen_layout->addWidget (m_lowerOptSizeBox, 9, 1);

    gen_layout->addWidget (new QLabel (tr ("Maximum optical size:")), 10, 0);
    m_upperOptSizeBox = new QSpinBox ();
    m_upperOptSizeBox->setMaximum (0xfffe);
    gen_layout->addWidget (m_upperOptSizeBox, 10, 1);

    m_tab->addTab (gen_tab, QWidget::tr ("&General"));

    // Metrics 1
    QWidget *mtx_tab = new QWidget ();

    QGridLayout *mtx_layout = new QGridLayout ();
    mtx_layout->setAlignment (Qt::AlignTop);
    mtx_tab->setLayout (mtx_layout);

    mtx_layout->addWidget (new QLabel (tr ("Average weighted escapement:")), 0, 0);
    m_avgCharWidthBox = new QSpinBox ();
    m_avgCharWidthBox->setMaximum (16384);
    mtx_layout->addWidget (m_avgCharWidthBox, 0, 1);

    mtx_layout->addWidget (new QLabel (tr ("Typographic ascender:")), 1, 0);
    m_typoAscenderBox = new QSpinBox ();
    m_typoAscenderBox->setMinimum (-32767);
    m_typoAscenderBox->setMaximum (32767);
    mtx_layout->addWidget (m_typoAscenderBox, 1, 1);

    mtx_layout->addWidget (new QLabel (tr ("Typographic descender:")), 2, 0);
    m_typoDescenderBox = new QSpinBox ();
    m_typoDescenderBox->setMinimum (-32767);
    m_typoDescenderBox->setMaximum (32767);
    mtx_layout->addWidget (m_typoDescenderBox, 2, 1);

    mtx_layout->addWidget (new QLabel (tr ("Typographic line gap:")), 3, 0);
    m_typoLineGapBox = new QSpinBox ();
    m_typoLineGapBox->setMinimum (-32767);
    m_typoLineGapBox->setMaximum (32767);
    mtx_layout->addWidget (m_typoLineGapBox, 3, 1);

    mtx_layout->addWidget (new QLabel (tr ("Windows ascender:")), 4, 0);
    m_winAscentBox = new QSpinBox ();
    m_winAscentBox->setMinimum (-32767);
    m_winAscentBox->setMaximum (32767);
    mtx_layout->addWidget (m_winAscentBox, 4, 1);

    mtx_layout->addWidget (new QLabel (tr ("Windows descender:")), 5, 0);
    m_winDescentBox = new QSpinBox ();
    m_winDescentBox->setMinimum (-32767);
    m_winDescentBox->setMaximum (32767);
    mtx_layout->addWidget (m_winDescentBox, 5, 1);

    mtx_layout->addWidget (new QLabel (tr ("x Height:")), 6, 0);
    m_xHeightBox = new QSpinBox ();
    m_xHeightBox->setMinimum (-32767);
    m_xHeightBox->setMaximum (32767);
    mtx_layout->addWidget (m_xHeightBox, 6, 1);

    mtx_layout->addWidget (new QLabel (tr ("Capital Height:")), 7, 0);
    m_capHeightBox = new QSpinBox ();
    m_capHeightBox->setMinimum (-32767);
    m_capHeightBox->setMaximum (32767);
    mtx_layout->addWidget (m_capHeightBox, 7, 1);

    m_tab->addTab (mtx_tab, QWidget::tr ("Metrics &1"));

    // Metrics 2
    QWidget *sss_tab = new QWidget ();

    QGridLayout *sss_layout = new QGridLayout ();
    sss_layout->setAlignment (Qt::AlignTop);
    sss_tab->setLayout (sss_layout);

    sss_layout->addWidget (new QLabel (tr ("Subscript")), 0, 0);
    sss_layout->addWidget (new QLabel (tr ("X")), 0, 1);
    sss_layout->addWidget (new QLabel (tr ("Y")), 0, 2);

    sss_layout->addWidget (new QLabel (tr ("Size:")), 1, 0);
    m_ySubscriptXSizeBox = new QSpinBox ();
    m_ySubscriptXSizeBox->setMinimum (-32767);
    m_ySubscriptXSizeBox->setMaximum (32767);
    sss_layout->addWidget (m_ySubscriptXSizeBox, 1, 1);
    m_ySubscriptYSizeBox = new QSpinBox ();
    m_ySubscriptYSizeBox->setMinimum (-32767);
    m_ySubscriptYSizeBox->setMaximum (32767);
    sss_layout->addWidget (m_ySubscriptYSizeBox, 1, 2);

    sss_layout->addWidget (new QLabel (tr ("Offset:")), 2, 0);
    m_ySubscriptXOffsetBox = new QSpinBox ();
    m_ySubscriptXOffsetBox->setMinimum (-32767);
    m_ySubscriptXOffsetBox->setMaximum (32767);
    sss_layout->addWidget (m_ySubscriptXOffsetBox, 2, 1);
    m_ySubscriptYOffsetBox = new QSpinBox ();
    m_ySubscriptYOffsetBox->setMinimum (-32767);
    m_ySubscriptYOffsetBox->setMaximum (32767);
    sss_layout->addWidget (m_ySubscriptYOffsetBox, 2, 2);

    sss_layout->addWidget (new QLabel (tr ("Superscript")), 3, 0);

    sss_layout->addWidget (new QLabel (tr ("Size:")), 4, 0);
    m_ySuperscriptXSizeBox = new QSpinBox ();
    m_ySuperscriptXSizeBox->setMinimum (-32767);
    m_ySuperscriptXSizeBox->setMaximum (32767);
    sss_layout->addWidget (m_ySuperscriptXSizeBox, 4, 1);
    m_ySuperscriptYSizeBox = new QSpinBox ();
    m_ySuperscriptYSizeBox->setMinimum (-32767);
    m_ySuperscriptYSizeBox->setMaximum (32767);
    sss_layout->addWidget (m_ySuperscriptYSizeBox, 4, 2);

    sss_layout->addWidget (new QLabel (tr ("Offset:")), 5, 0);
    m_ySuperscriptXOffsetBox = new QSpinBox ();
    m_ySuperscriptXOffsetBox->setMinimum (-32767);
    m_ySuperscriptXOffsetBox->setMaximum (32767);
    sss_layout->addWidget (m_ySuperscriptXOffsetBox, 5, 1);
    m_ySuperscriptYOffsetBox = new QSpinBox ();
    m_ySuperscriptYOffsetBox->setMinimum (-32767);
    m_ySuperscriptYOffsetBox->setMaximum (32767);
    sss_layout->addWidget (m_ySuperscriptYOffsetBox, 5, 2);

    sss_layout->addWidget (new QLabel (tr ("Strikeout")), 6, 0);

    sss_layout->addWidget (new QLabel (tr ("Size")), 7, 0);
    m_yStrikeoutSizeBox = new QSpinBox ();
    m_yStrikeoutSizeBox->setMinimum (-32767);
    m_yStrikeoutSizeBox->setMaximum (32767);
    sss_layout->addWidget (m_yStrikeoutSizeBox, 7, 2);

    sss_layout->addWidget (new QLabel (tr ("Position")), 8, 0);
    m_yStrikeoutPositionBox = new QSpinBox ();
    m_yStrikeoutPositionBox->setMinimum (-32767);
    m_yStrikeoutPositionBox->setMaximum (32767);
    sss_layout->addWidget (m_yStrikeoutPositionBox, 8, 2);

    m_tab->addTab (sss_tab, QWidget::tr ("Metrics &2"));

    // Classification
    QWidget *cls_tab = new QWidget ();

    QGridLayout *cls_layout = new QGridLayout ();
    cls_layout->setAlignment (Qt::AlignTop);
    cls_tab->setLayout (cls_layout);

    cls_layout->addWidget (new QLabel (tr ("Weight class:")), 0, 0);
    m_weightClassBox = new QComboBox ();
    cls_layout->addWidget (m_weightClassBox, 0, 1);

    cls_layout->addWidget (new QLabel (tr ("Width class:")), 1, 0);
    m_widthClassBox = new QComboBox ();
    cls_layout->addWidget (m_widthClassBox, 1, 1);

    cls_layout->addWidget (new QLabel (tr ("IBM family class:")), 2, 0);
    m_FamilyClassBox = new QComboBox ();
    cls_layout->addWidget (m_FamilyClassBox, 2, 1);
    connect (m_FamilyClassBox, static_cast<void (QComboBox::*)(int)> (&QComboBox::currentIndexChanged),
	this, &OS_2Edit::setFamilyClass);

    cls_layout->addWidget (new QLabel (tr ("IBM family subclass:")), 3, 0);
    m_FamilySubClassBox = new QComboBox ();
    cls_layout->addWidget (m_FamilySubClassBox, 3, 1);

    cls_layout->addWidget (new QLabel (tr ("Font selection flags:")), 4, 0);
    m_selectionWidget = new QListWidget ();
    cls_layout->addWidget (m_selectionWidget, 5, 0, 1, 2);

    m_tab->addTab (cls_tab, QWidget::tr ("&Classification"));

    //Panose
    QWidget *pan_tab = new QWidget ();

    QGridLayout *pan_layout = new QGridLayout ();
    pan_layout->setAlignment (Qt::AlignTop);
    pan_tab->setLayout (pan_layout);

    m_panoseLabel[0] = new QLabel (tr ("Family Kind:"));
    pan_layout->addWidget (m_panoseLabel[0], 0, 0);
    m_panoseBox[0] = new QComboBox ();
    pan_layout->addWidget (m_panoseBox[0], 0, 1);
    connect (m_panoseBox[0], static_cast<void (QComboBox::*)(int)> (&QComboBox::currentIndexChanged),
	this, &OS_2Edit::setPanoseFamily);

    for (int i=1; i<10; i++) {
	m_panoseLabel[i] = new QLabel (tr ("Panose %1").arg (i+1));
	pan_layout->addWidget (m_panoseLabel[i], i, 0);
	m_panoseBox[i] = new QComboBox ();
	pan_layout->addWidget (m_panoseBox[i], i, 1);
    }

    m_tab->addTab (pan_tab, QWidget::tr ("&Panose"));

    // Charsets
    QWidget *uni_tab = new QWidget ();

    QGridLayout *uni_layout = new QGridLayout ();
    uni_layout->setAlignment (Qt::AlignTop);
    uni_tab->setLayout (uni_layout);

    uni_layout->addWidget (new QLabel (tr ("Supported Unicode ranges:")), 0, 0);
    m_uniWidget = new QListWidget ();
    uni_layout->addWidget (m_uniWidget, 1, 0, 1, 2);

    uni_layout->addWidget (new QLabel (tr ("Supported charsets:")), 2, 0);
    m_cpWidget = new QListWidget ();
    uni_layout->addWidget (m_cpWidget, 3, 0, 1, 2);

    m_tab->addTab (uni_tab, QWidget::tr ("&Charsets"));

    // Buttons
    QVBoxLayout *layout = new QVBoxLayout ();
    layout->addWidget (m_tab);

    saveButton = new QPushButton (tr ("&Compile table"));
    closeButton = new QPushButton (tr ("C&lose"));

    QHBoxLayout *buttLayout = new QHBoxLayout ();
    buttLayout->addWidget (saveButton);
    buttLayout->addWidget (closeButton);
    layout->addLayout (buttLayout);

    connect (saveButton, &QPushButton::clicked, this, &OS_2Edit::save);
    connect (closeButton, &QPushButton::clicked, this, &OS_2Edit::close);

    window->setLayout (layout);
    setCentralWidget (window);
    fillControls ();

    m_valid = true;
}

void OS_2Edit::fillControls () {
    // General
    m_versionBox->setValue (m_os_2->version ());
    for (int i=0; i<fsRestrictionsList.size (); i++)
	m_licenseBox->addItem (fsRestrictionsList[i].first, fsRestrictionsList[i].second);
    m_licenseBox->setCurrentIndex (0);
    for (int i=1; i<5; i++) {
	if (m_os_2->fsType (i)) {
	    m_licenseBox->setCurrentIndex (i);
	    break;
	}
    }
    m_noSubsettingBox->setChecked (m_os_2->fsType (8));
    m_bitmapsBox->setChecked (m_os_2->fsType (9));
    m_vendorIDBox->setText (QString::fromStdString (m_os_2->achVendID ()));
    m_firstCharBox->setValue (m_os_2->usFirstCharIndex ());
    m_lastCharBox->setValue (m_os_2->usLastCharIndex ());
    m_defaultCharBox->setValue (m_os_2->usDefaultChar ());
    m_breakCharBox->setValue (m_os_2->usBreakChar ());
    m_maxContextBox->setValue (m_os_2->usMaxContext ());
    m_lowerOptSizeBox->setValue (m_os_2->usLowerOpticalPointSize ());
    m_upperOptSizeBox->setValue (m_os_2->usUpperOpticalPointSize ());

    // Metrics 1
    m_avgCharWidthBox->setValue (m_os_2->xAvgCharWidth ());
    m_typoAscenderBox->setValue (m_os_2->sTypoAscender ());
    m_typoDescenderBox->setValue (m_os_2->sTypoDescender ());
    m_typoLineGapBox->setValue (m_os_2->sTypoLineGap ());
    m_winAscentBox->setValue (m_os_2->usWinAscent ());
    m_winDescentBox->setValue (m_os_2->usWinDescent ());
    m_xHeightBox->setValue (m_os_2->sxHeight ());
    m_capHeightBox->setValue (m_os_2->sCapHeight ());

    //Metrics 2
    m_ySubscriptXSizeBox->setValue (m_os_2->ySubscriptXSize ());
    m_ySubscriptYSizeBox->setValue (m_os_2->ySubscriptYSize ());
    m_ySubscriptXOffsetBox->setValue (m_os_2->ySubscriptXOffset ());
    m_ySubscriptYOffsetBox->setValue (m_os_2->ySubscriptYOffset ());
    m_ySuperscriptXSizeBox->setValue (m_os_2->ySuperscriptXSize ());
    m_ySuperscriptYSizeBox->setValue (m_os_2->ySuperscriptYSize ());
    m_ySuperscriptXOffsetBox->setValue (m_os_2->ySuperscriptXOffset ());
    m_ySuperscriptYOffsetBox->setValue (m_os_2->ySuperscriptYOffset ());
    m_yStrikeoutSizeBox->setValue (m_os_2->yStrikeoutSize ());
    m_yStrikeoutPositionBox->setValue (m_os_2->yStrikeoutPosition ());

    // Classification
    for (int i=0; i<usWeightList.size (); i++)
	m_weightClassBox->addItem (usWeightList[i].first, usWeightList[i].second);
    m_weightClassBox->setCurrentIndex
	(m_weightClassBox->findData (m_os_2->usWeightClass (), Qt::UserRole));
    for (int i=0; i<usWidthList.size (); i++)
	m_widthClassBox->addItem (usWidthList[i].first, usWidthList[i].second);
    m_widthClassBox->setCurrentIndex
	(m_widthClassBox->findData (m_os_2->usWidthClass (), Qt::UserRole));
    for (int i=0; i<ibmFamList.size (); i++)
	m_FamilyClassBox->addItem (ibmFamList[i].first, ibmFamList[i].second);
    QStandardItemModel *model = qobject_cast<QStandardItemModel *> (m_FamilyClassBox->model ());
    for (int i=0; i<ibmFamList.size (); i++) {
	QStandardItem *item = model->item (i);
	if (item->text ().contains (tr ("Reserved for future use")))
	    item->setFlags (item->flags () & ~Qt::ItemIsEnabled);
    }

    m_FamilyClassBox->setCurrentIndex
	(m_FamilyClassBox->findData (m_os_2->sFamilyClass (), Qt::UserRole));
    m_FamilySubClassBox->setCurrentIndex
	(m_FamilySubClassBox->findData (m_os_2->sFamilySubClass (), Qt::UserRole));
    for (int i=0; i<selectionFlags.size (); i++) {
	auto *item = new QListWidgetItem (selectionFlags[i].first);
	auto iflags = item->flags ();
	iflags |= Qt::ItemIsUserCheckable;
	item->setCheckState (m_os_2->fsSelection (i) ? Qt::Checked : Qt::Unchecked);
	if (selectionFlags[i].first.contains ("Reserved"))
	    iflags &= ~Qt::ItemIsEnabled;
	item->setFlags (iflags);
	m_selectionWidget->addItem (item);
    }

    // Panose
    for (int i=0; i<panoseFam.size (); i++)
	m_panoseBox[0]->addItem (panoseFam[i], i);
    m_panoseBox[0]->setCurrentIndex
	(m_panoseBox[0]->findData (m_os_2->panose (0), Qt::UserRole));
    for (int i=0; i<10; i++)
        m_panoseBox[i]->setCurrentIndex
	    (m_panoseBox[i]->findData (m_os_2->panose (i), Qt::UserRole));

    // Charsets
    for (int i=0; i<123; i++) {
	auto *item = new QListWidgetItem ();
	auto range_lst = uniRangeList[i];
        int flen = range_lst[0].first > 0xffff ? 6 : 4;
        QString label = QString ("%1: 0x%2-0x%3").arg (range_lst[0].rangeName)
	    .arg (range_lst[0].first, flen, 16, QLatin1Char ('0'))
	    .arg (range_lst[0].last, flen, 16, QLatin1Char ('0'));
	QString tip = label;
        for (int j=1; j<range_lst.size (); j++) {
	    flen = range_lst[j].first > 0xffff ? 6 : 4;
	    label = label.append (QString ("; %1: 0x%2-0x%3").arg (range_lst[j].rangeName)
		.arg (range_lst[j].first, flen, 16, QLatin1Char ('0'))
		.arg (range_lst[j].last, flen, 16, QLatin1Char ('0')));
	    tip = tip.append (QString ("\n%1: 0x%2-0x%3").arg (range_lst[j].rangeName)
		.arg (range_lst[j].first, flen, 16, QLatin1Char ('0'))
		.arg (range_lst[j].last, flen, 16, QLatin1Char ('0')));
        }
	item->setFlags (item->flags () | Qt::ItemIsUserCheckable);
	item->setText (label);
	item->setToolTip (tip);
	item->setCheckState (m_os_2->ulUnicodeRange (i) ? Qt::Checked : Qt::Unchecked);
	m_uniWidget->addItem (item);
    }
    for (int i=123; i<128; i++) {
	auto *item = new QListWidgetItem (QString ("Unassigned bit %1").arg (i));
	auto iflags = item->flags ();
	iflags |= Qt::ItemIsUserCheckable;
	item->setCheckState (m_os_2->ulUnicodeRange (i) ? Qt::Checked : Qt::Unchecked);
	iflags &= ~Qt::ItemIsEnabled;
	item->setFlags (iflags);
	m_uniWidget->addItem (item);
    }

    for (int i=0; i<64; i++) {
	auto *item = new QListWidgetItem (codepageList[i].first);
	auto iflags = item->flags ();
	iflags |= Qt::ItemIsUserCheckable;
	item->setCheckState (m_os_2->ulCodePageRange (i) ? Qt::Checked : Qt::Unchecked);
	if (codepageList[i].first.startsWith ("(Reserved"))
	    iflags &= ~Qt::ItemIsEnabled;
	item->setFlags (iflags);
	m_cpWidget->addItem (item);
    }
}

bool OS_2Edit::checkUpdate (bool) {
    return true;
}

bool OS_2Edit::isModified () {
    return m_os_2->modified ();
}

bool OS_2Edit::isValid () {
    return m_valid;
}

FontTable* OS_2Edit::table () {
    return m_os_2;
}

void OS_2Edit::closeEvent (QCloseEvent *event) {
    // If we are going to delete the font, ignore changes in table edits
    if (!isModified () || checkUpdate (true))
        m_os_2->clearEditor ();
    else
        event->ignore ();
}

QSize OS_2Edit::sizeHint () const {
    QFontMetrics fm = m_vendorIDBox->fontMetrics ();
    int w = fm.boundingRect ("Aspect Ratio of character 119: Aspect Ratio of character 119:").width ();
    int h = fm.lineSpacing () * 20;
    return QSize (w, h);
}

void OS_2Edit::save () {
    os_2_data &osd = m_os_2->contents;

    osd.version = m_versionBox->value ();
    osd.xAvgCharWidth = m_avgCharWidthBox->value ();
    osd.usWeightClass = m_weightClassBox->itemData (m_weightClassBox->currentIndex ()).toUInt ();
    osd.usWidthClass = m_widthClassBox->itemData (m_widthClassBox->currentIndex ()).toUInt ();
    osd.fsType = m_licenseBox->itemData (m_licenseBox->currentIndex ()).toInt ();
    if (m_noSubsettingBox->isChecked ())
	osd.fsType.set (8);
    if (m_bitmapsBox->isChecked ())
	osd.fsType.set (9);
    osd.ySubscriptXSize = m_ySubscriptXSizeBox->value ();
    osd.ySubscriptYSize = m_ySubscriptYSizeBox->value ();
    osd.ySubscriptXOffset = m_ySubscriptXOffsetBox->value ();
    osd.ySubscriptYOffset = m_ySubscriptYOffsetBox->value ();
    osd.ySuperscriptXSize = m_ySuperscriptXSizeBox->value ();
    osd.ySuperscriptYSize = m_ySuperscriptYSizeBox->value ();
    osd.ySuperscriptXOffset = m_ySuperscriptXOffsetBox->value ();
    osd.ySuperscriptYOffset = m_ySuperscriptYOffsetBox->value ();
    osd.yStrikeoutSize = m_yStrikeoutSizeBox->value ();
    osd.yStrikeoutPosition = m_yStrikeoutPositionBox->value ();
    osd.sFamilyClass = m_FamilyClassBox->itemData (m_FamilyClassBox->currentIndex ()).toUInt ();
    osd.sFamilySubClass = m_FamilySubClassBox->itemData (m_FamilySubClassBox->currentIndex ()).toUInt ();
    for (int i=0; i<10; i++)
	osd.panose[i] = m_panoseBox[i]->itemData (m_panoseBox[i]->currentIndex ()).toUInt ();
    for (int i=0; i<32; i++) {
	auto item = m_uniWidget->item (i);
	osd.ulUnicodeRange1[i] = (item->checkState () == Qt::Checked);
    }
    for (int i=32; i<64; i++) {
	auto item = m_uniWidget->item (i);
	osd.ulUnicodeRange2[i] = (item->checkState () == Qt::Checked);
    }
    for (int i=64; i<96; i++) {
	auto item = m_uniWidget->item (i);
	osd.ulUnicodeRange3[i] = (item->checkState () == Qt::Checked);
    }
    for (int i=96; i<128; i++) {
	auto item = m_uniWidget->item (i);
	osd.ulUnicodeRange4[i] = (item->checkState () == Qt::Checked);
    }
    std::string cvID = m_vendorIDBox->text ().toStdString ();
    cvID.copy (osd.achVendID.data (), 4);
    for (int i=0; i<selectionFlags.size (); i++) {
	auto item = m_selectionWidget->item (i);
	osd.fsSelection[i] = (item->checkState () == Qt::Checked);
    }
    osd.usFirstCharIndex = m_firstCharBox->value ();
    osd.usLastCharIndex = m_lastCharBox->value ();
    osd.sTypoAscender = m_typoAscenderBox->value ();
    osd.sTypoDescender = m_typoDescenderBox->value ();
    osd.sTypoLineGap = m_typoLineGapBox->value ();
    osd.usWinAscent = m_winAscentBox->value ();
    osd.usWinDescent = m_winDescentBox->value ();
    for (int i=0; i<32; i++) {
	auto item = m_cpWidget->item (i);
	osd.ulCodePageRange1[i] = (item->checkState () == Qt::Checked);
    }
    for (int i=32; i<64; i++) {
	auto item = m_cpWidget->item (i);
	osd.ulCodePageRange2[i] = (item->checkState () == Qt::Checked);
    }
    osd.sxHeight = m_xHeightBox->value ();
    osd.sCapHeight = m_capHeightBox->value ();
    osd.usDefaultChar = m_defaultCharBox->value ();
    osd.usBreakChar = m_breakCharBox->value ();
    osd.usMaxContext = m_maxContextBox->value ();
    osd.usLowerOpticalPointSize = m_lowerOptSizeBox->value ();
    osd.usUpperOpticalPointSize = m_upperOptSizeBox->value ();

    m_os_2->packData ();
    emit (update (m_os_2));
    close ();
}

void OS_2Edit::setFamilyClass (int family) {
    auto lst = ibmSubFamListDefault;
    m_FamilySubClassBox->clear ();
    if (ibmSubFamLists.contains (family)) {
	auto corr_lst = ibmSubFamLists[family];
	int j=0;
	for (int i=0; i<lst.size (); i++) {
	    while (j < corr_lst.size () && corr_lst[j].second < lst[i].second) j++;
	    if (j < corr_lst.size () && corr_lst[j].second == lst[i].second)
		m_FamilySubClassBox->addItem (corr_lst[j].first, corr_lst[j].second);
	    else
		m_FamilySubClassBox->addItem (lst[i].first, lst[i].second);
	}
    } else {
	for (int i=0; i<lst.size (); i++)
	    m_FamilySubClassBox->addItem (lst[i].first, lst[i].second);
    }
    QStandardItemModel *model = qobject_cast<QStandardItemModel *> (m_FamilySubClassBox->model ());
    for (int i=0; i<lst.size (); i++) {
	QStandardItem *item = model->item (i);
	if (item->text ().contains (tr ("Reserved for future use")))
	    item->setFlags (item->flags () & ~Qt::ItemIsEnabled);
    }
    m_FamilySubClassBox->setCurrentIndex (0);
}

void OS_2Edit::setPanoseFamily (int family) {
    for (int i=1; i<10; i++) {
	m_panoseLabel[i]->setText (QString ("Panose %1").arg (i+1));
	m_panoseBox[i]->clear ();
	m_panoseBox[i]->addItem (" 0: Any", 0);
	m_panoseBox[i]->addItem (" 1: No Fit", 1);
	m_panoseBox[i]->setCurrentIndex (0);
    }
    if (family > 1 && family < 6) {
	auto branch = panose[family];
	for (int i=1; i<10; i++) {
	    QString &label = branch[i-1].first;
	    auto lst = branch[i-1].second;
	    m_panoseLabel[i]->setText (label);
	    for (int j=0; j<lst.size (); j++)
		m_panoseBox[i]->addItem (lst[j], j+2);
	}
    }
}

void OS_2Edit::setTableVersion (int version) {
    m_cpWidget->setEnabled (version > 0);
    m_xHeightBox->setEnabled (version > 1);
    m_capHeightBox->setEnabled (version > 1);
    m_defaultCharBox->setEnabled (version > 1);
    m_breakCharBox->setEnabled (version > 1);
    m_maxContextBox->setEnabled (version > 1);
    m_lowerOptSizeBox->setEnabled (version > 4);
    m_upperOptSizeBox->setEnabled (version > 4);
}
