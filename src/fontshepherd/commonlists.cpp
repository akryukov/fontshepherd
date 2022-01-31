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

#include <algorithm>
#include "commonlists.h"

static bool rcomp_by_string
    (const FontShepherd::numbered_string ns1, const FontShepherd::numbered_string ns2) {
    return (ns1.name < ns2.name);
}

const std::vector<FontShepherd::numbered_string>& FontShepherd::specificList (int platform) {
    switch (platform) {
      case FontShepherd::plt_unicode:
	return FontShepherd::unicodeEncodings;
      case FontShepherd::plt_mac:
	return FontShepherd::macEncodings;
      case FontShepherd::plt_iso:
	return FontShepherd::isoEncodings;
      case FontShepherd::plt_windows:
	return FontShepherd::windowsEncodings;
      case FontShepherd::plt_custom:
	return FontShepherd::windowsCustomEncodings;
    }
    return noEncodings;
}

const std::string FontShepherd::iconvCharsetName (int platform, int charset) {
    switch (platform) {
      case 0: // Unicode
	switch (charset) {
	  case 0:
	  case 1:
	  case 2:
	  case 3:
	    return "UTF-16BE";
	  // 5 is for Variation Selectors, so can't be used to encode some text
	  case 4:
	  case 6:
	    return "UTF-32BE";
	}
      break;
      // Macintosh. Most part of encodings is unsupported, while some others
      // (like MacArabic) are not guaranteed to be supported by your version of iconv.
      // (MacArabic occurs e.g. in Monaco.ttf from Mac OS X distrbution, although the
      // strings themselves are actually ASCII)
      case 1:
	switch (charset) {
	  case 0:
	    return "MACINTOSH";
	  case 1:
	    return "SHIFT_JISX0213";
	  case 2:
	    return "BIG5-HKSCS";
	  case 3:
	    return "EUC-KR";
	  case 4:
	    return "MACARABIC";
	  case 5:
	    return "MACHEBREW";
	  case 6:
	    return "MACGREEK";
	  case 7:
	    return "MAC-UK";
	  case 21:
	    return "MACTHAI";
	  case 25:
	    return "GB18030";
	}
      break;
      // Obsolete ISO-10646
      case 2:
	switch (charset) {
	  case 0:
	    return "US-ASCII";
	  case 1:
	    return "UTF-16BE";
	  case 2:
	    return "ISO-8859-1";
	}
      break;
      // Windows
      case 3:
	switch (charset) {
	  case 0: // This one is not handled by iconv, but return the name for reference
	    return "SYMBOL";
	  case 1:
	    return "UTF-16BE";
	  case 2:
	    return "SHIFT_JISX0213";
	  case 3:
	    return "GB18030";
	  case 4:
	    return "BIG5-HKSCS";
	  case 5:
	    return "EUC-KR";
	  case 6:
	    return "JOHAB";
	  case 10:
	    return "UTF-32BE";
	}
      break;
      // Windows "Custom"
      case 4:
	switch (charset) {
	  case 161:
	    return "WINDOWS-1253";
	  case 162:
	    return "WINDOWS-1254";
	  case 163:
	    return "WINDOWS-1258";
	  case 177:
	    return "WINDOWS-1255";
	  case 178:
	    return "WINDOWS-1256";
	  case 186:
	    return "WINDOWS-1257";
	  case 204:
	    return "WINDOWS-1251";
	  case 238:
	    return "WINDOWS-1250";
	}
    }
    return "UNSUPPORTED";
}

const std::vector<FontShepherd::numbered_string> FontShepherd::sortedMacLanguages () {
    std::vector<numbered_string> ret (macLanguages);
    std::sort (ret.begin (), ret.end (), rcomp_by_string);
    return ret;
}

