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

#ifndef _FONSHEPHERD_INSTREDIT_H
#define _FONSHEPHERD_INSTREDIT_H

#include <set>
#include <QtWidgets>
#include "tables.h" // Have to load it here due to inheritance from TableEdit

class sfntFile;
typedef struct ttffont sFont;
class FontTable;
class TableEdit;

struct instr_def {
    std::string name;
    uint8_t rangeStart;
    uint8_t rangeEnd;
    std::string toolTip;
};

struct instr_data {
    bool isInstr;
    int16_t code;
    uint8_t base;
    uint8_t nPushes;
    std::string repr;
    std::string toolTip;
};

namespace TTFinstrs {
    enum ParseError {
	Parse_OK = 0,
	Parse_WrongInstr = 1,
	Parse_NeedsNumber = 2,
	Parse_NeedsInstr = 3,
	Parse_NeedsBracket = 4,
	Parse_TooLarge = 5,
	Parse_TooLargeByte = 6,
	Parse_TooLargeWord = 7,
	Parse_Unexpected = 8
    };
};

class InstrEdit : public QWidget {
    Q_OBJECT;

public:
    InstrEdit (uint8_t *data, uint16_t len, QWidget *parent);
    ~InstrEdit () {};

    std::vector<uint8_t> data ();
    bool changed ();

    static instr_data byCode (uint8_t code);
    static int byInstr (std::string instr);
    static instr_data invalidCode (uint8_t code);
    static void checkCodeArgs (instr_data &d, std::string &name);

public slots:
    void edit ();
    void discard ();

signals:
    void instrChanged ();

private:
    static std::map<std::string, uint8_t> ByInstr;
    static const std::map<uint8_t, instr_def> InstrSet;
    static const std::map<std::string, uint8_t> ByArg;

    static int getInstrArgs (std::vector<std::string> &args, std::string &edited, size_t &pos, int &start, int &len);
    static int checkInstrArgs (instr_data &d, std::vector<std::string> &args);

    void decode (uint8_t *data, uint16_t len);
    int parse (std::string &edited, std::vector<instr_data> &instr, int &sel_start, int &sel_len);
    void fillTable ();
    void fillEdit ();
    bool m_changed;

    std::vector<instr_data> m_instrs;
    QStackedLayout *m_stack;
    QTextEdit *m_edit;
    QTableWidget *m_instrTab;

    QPushButton *m_editButton, *m_discardButton;
};

// a wrapper for InstrEdit
class InstrTableEdit : public TableEdit {
    Q_OBJECT;

public:
    InstrTableEdit (FontTable* tab, sFont* font, QWidget *parent);
    ~InstrTableEdit () {};

    void resetData () override {};
    bool checkUpdate (bool can_cancel) override;
    bool isModified () override;
    bool isValid () override;
    FontTable* table () override;

    void closeEvent (QCloseEvent *event);

public slots:
    void save ();

private:
    void decode ();
    void fillInstrTab ();

    sFont *m_font;
    FontTable *m_tbl;
    InstrEdit *m_instrEdit;
    bool m_valid;

    QPushButton *m_okButton, *m_cancelButton;
};

#endif
