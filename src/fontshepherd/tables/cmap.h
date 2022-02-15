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

#include <QtWidgets>

typedef void* iconv_t;
class sfntFile;
class FontTable;
class GlyphNameProvider;

enum charset {
    em_none = -1,
    em_symbol, em_unicode,
    em_shift_jis, em_gbk, em_big5, em_wansung, em_johab,
    mac_roman, mac_cyrillic, mac_ukrainian, mac_ce,
    ms_greek, ms_turkish, ms_vietnamese, ms_hebrew, ms_arabic, ms_baltic, ms_cyrillic, ms_ce
};

enum platform {
    plt_unicode = 0,
    plt_mac = 1,
    plt_iso10646 = 2,
    plt_ms = 3,
    plt_custom = 4
};

/* a sub header in 8/16 cmap table */
struct subhead {
    uint16_t first, cnt, delta, rangeoff;
};

struct enc_range {
    uint32_t first_enc, length;
    uint16_t first_gid;
};

struct enc_range4 {
    uint16_t start_code, end_code, id_range_off;
    int16_t id_delta;
};

struct enc_mapping {
    uint32_t code;
    uint16_t gid;
};

struct vsr_range {
    uint32_t start_uni;
    uint8_t add_count;
};

typedef struct var_selector_record {
    uint32_t selector, default_offset, non_default_offset;
    std::vector<uint32_t> default_vars;
    std::vector<struct vsr_range> default_ranges;
    std::vector<struct enc_mapping> non_default_vars;

    var_selector_record () : selector (0), default_offset (0), non_default_offset (0) {};
} VarSelRecord;

class CmapEnc {
    friend class CmapTable;

public:
    CmapEnc (uint16_t platID, uint16_t encID, FontTable *tbl);
    CmapEnc (std::map<std::string, int> &args, CmapEnc *source, const std::string &encoding, FontTable *tbl);
    CmapEnc (GlyphNameProvider *source, FontTable *tbl);
    CmapEnc (const CmapEnc&) = delete;
    ~CmapEnc ();

    uint32_t offset () const;
    uint32_t length () const;
    uint16_t format () const;
    uint16_t language () const;
    bool isUnicode () const;
    bool hasConverter () const;
    uint8_t numBits () const;
    uint32_t count () const;
    uint32_t index () const;
    std::string stringName () const;
    int charset () const;

    bool isCurrent () const;
    void setCurrent (bool val);
    bool isModified () const;
    void setModified (bool val);

    void addLock ();
    void removeLock ();
    int isLocked ();

    std::vector<uint32_t> encoded (uint16_t gid);
    std::vector<uint32_t> unicode (uint16_t gid);
    uint32_t unicodeByPos (uint32_t pos) const;
    uint32_t encByPos (uint32_t pos) const;
    uint16_t gidByEnc (uint32_t code) const;
    uint16_t gidByUnicode (uint32_t uni) const;
    uint16_t gidByPos (uint32_t pos) const;
    std::vector<uint32_t> unencoded (uint32_t glyph_cnt);
    const QString codeRepr (uint32_t pos);
    const QString gidCodeRepr (uint16_t gid);

    bool deleteMapping (uint32_t code);
    bool deleteMappingsForGid (uint16_t gid);
    bool insertMapping (uint32_t code, uint16_t gid);
    bool insertUniMapping (uint32_t uni, uint16_t gid);
    bool setGidByPos (uint32_t pos, uint16_t gid);
    int firstAvailableCode () const;
    int codeAvailable (uint32_t code) const;

    uint32_t numRanges () const;
    struct enc_range *getRange (uint32_t idx);
    bool deleteRange (uint32_t idx);
    bool insertRange (uint32_t first_enc, uint16_t first_gid, uint32_t length);
    int firstAvailableRange (uint32_t *start, uint32_t *length);
    int rangeAvailable (uint32_t first_enc, uint32_t length);

    VarSelRecord *getVarSelectorRecord (uint32_t idx);
    bool deleteVarSelectorRecord (uint32_t code);
    VarSelRecord *addVariationSequence (uint32_t selector, bool is_dflt, uint32_t code, uint16_t gid);

protected:
    void setIndex (uint32_t);
    void setOffset (uint32_t val);
    void setLength (uint32_t val);
    void setFormat (uint16_t val);
    void setLanguage (uint16_t val);
    void addMapping (uint32_t enc, uint32_t gid, uint32_t len=1);

    // use unique_ptr for variation selectors, as pointers to those
    // are then manipulated by a Tree Model items
    std::vector<std::unique_ptr<VarSelRecord>> var_selectors;
    std::vector<struct enc_mapping> mappings;
    std::vector<struct enc_range> segments;
    uint16_t map[256] = { 0 };

private:
    uint32_t recodeChar (uint32_t code, bool to_uni=true) const;

    uint32_t m_offset;
    uint32_t m_length;
    uint16_t m_format, m_language;
    bool m_current;
    bool m_changed;
    int m_lockCounter;

    iconv_t m_codec;
    iconv_t m_unicodec;
    int m_charset;
    uint32_t m_index;
    FontTable *m_parent;
};

class CmapEncTable {
public:
    CmapEncTable (uint16_t platform, uint16_t specific, uint32_t offset);
    ~CmapEncTable ();

    uint16_t platform ();
    std::string strPlatform ();
    uint16_t specific ();
    std::string strSpecific ();
    uint32_t offset ();
    void setSubTable (CmapEnc *subtable);
    CmapEnc *subtable ();
    static bool isCJK (uint16_t platform, uint16_t specific);

private:
    uint16_t m_platform, m_specific;
    uint32_t m_offset;
    CmapEnc *m_subtable;
};

class CmapTable : public FontTable {
public:
    CmapTable (sfntFile* fontfile, TableHeader &props);
    ~CmapTable ();
    void unpackData (sFont *font);
    void edit (sFont* fnt, QWidget* caller) override;

    uint16_t numTables ();
    CmapEncTable* getTable (uint16_t idx);
    uint16_t addTable (uint16_t platform, uint16_t specific, CmapEnc *subtable);
    void removeTable (uint16_t idx);
    uint16_t numSubTables ();
    CmapEnc* getSubTable (uint16_t idx);
    CmapEnc* addSubTable (std::map<std::string, int> &args, const std::string &encoding, GlyphNameProvider *gnp);
    void removeSubTable (uint16_t idx, sFont *font);
    void sortSubTables ();
    void reorderSubTables (int from, int to);
    void packData ();
    bool tablesModified ();
    bool subTablesModified ();
    void setTablesModified (bool val);
    void setSubTablesModified (bool val);
    void findBestSubTable (sFont *font);
    void clearMappingsForGid (uint16_t gid);
    void addCommonMapping (uint32_t uni, uint16_t gid);

private:
    uint32_t recodeChar (uint32_t ch, const char * name);

    void encodeFormat0 (std::ostream &os, CmapEnc *enc);
    void encodeFormat2 (std::ostream &os, CmapEnc *enc);
    void encodeFormat4 (std::ostream &os, CmapEnc *enc);
    void encodeFormat6 (std::ostream &os, CmapEnc *enc);
    void encodeFormat10 (std::ostream &os, CmapEnc *enc);
    void encodeFormat12 (std::ostream &os, CmapEnc *enc, bool many_to_one);
    void encodeFormat14 (std::ostream &os, CmapEnc *enc);

    uint16_t m_version;
    std::vector<std::unique_ptr<CmapEncTable>> cmap_tables;
    std::vector<std::unique_ptr<CmapEnc>> cmap_subtables;
    bool m_tables_changed, m_subtables_changed;
};
