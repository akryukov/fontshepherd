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

#ifndef _FONSHEPHERD_NAME_H
#define _FONSHEPHERD_NAME_H
#include <QtWidgets>
#include "commonlists.h"

typedef void* iconv_t;
class sfntFile;
class FontTable;

struct lang_tag_record {
    QString language;
    uint16_t ID;
};

struct name_record {
    uint16_t platformID, encodingID, languageID, nameID;
    QString name;
    // The following is needed when the table is going to be compiled
    int encoded_idx;

    std::string strPlatform ();
    std::string strEncoding ();
    std::string strLanguage ();
    std::string nameDescription ();

    bool samePos (const name_record &rhs) const;
    bool operator> (const name_record &rhs) const;
    bool operator< (const name_record &rhs) const;
};

class NameTable : public FontTable {
    friend class NameEdit;

public:
    NameTable (sfntFile* fontfile, const TableHeader &props);
    ~NameTable ();
    void unpackData (sFont *font);
    void packData ();
    void edit (sFont* fnt, std::shared_ptr<FontTable> tptr, QWidget* caller) override;
    uint16_t version ();

    uint16_t numNameRecords () const;
    name_record *nameRecord (int index);
    void removeNameRecord (int index);
    void setNameString (int index, QString text);
    int insertNameRecord (name_record rec);
    bool checkNameRecord (const name_record rec, int *pos);
    QString bestName (uint16_t nameID, QString name_dflt = "<nameless>");
    virtual const std::vector<FontShepherd::numbered_string> &nameList () const;

    uint16_t numLangTags () const;
    QString langTagRecord (int index) const;
    void clearLangTags ();
    void clearCustomLangTagDependent ();
    void setLangTag (uint16_t idx, QString name);
    void removeLangTag (uint16_t idx);
    int insertLangTag (QString name, int row);
    bool checkLangTag (QString name) const;
    void sortLangTags ();
    // Makes it possible to undo the sort operation
    void setLangTagOrder (QList<QString> &order);

    void setNamesModified (bool val);
    void setLangTagsModified (bool val);
    bool namesModified () const;
    bool langTagsModified () const;

protected:
    std::vector<name_record> name_records;

private:
    QString decodeString (std::string &uni_enc, uint16_t platformID, uint16_t encodingID, char *strptr, uint16_t len);
    std::string encodeString (bool big_endian, uint16_t platformID, uint16_t encodingID, QString &uni_str);
    void updateCustomLangIDs (int row);

    uint16_t m_version;
    uint16_t m_string_off;
    std::vector<lang_tag_record> lang_tag_records;
    bool m_names_changed, m_lang_tags_changed;
};

// Used to extract a set of names and edit separately
class NameProxy : public NameTable {
public:
    NameProxy (NameTable *name);
    void update (std::vector<FontShepherd::numbered_string> name_list);
    void flush ();
    virtual const std::vector<FontShepherd::numbered_string> &nameList () const;

private:
    NameTable *m_name;
    std::vector<FontShepherd::numbered_string> m_name_list;
};
#endif
