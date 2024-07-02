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
#include <stack>
#include <QtWidgets>
#include "tables.h" // Have to load it here due to inheritance from TableEdit

class sfntFile;
typedef struct ttffont sFont;
class FontTable;
class TableEdit;

#ifndef _FS_STRUCT_BASEPOINT_DEFINED
#define _FS_STRUCT_BASEPOINT_DEFINED
typedef struct ipoint {
    int32_t x = 0;
    int32_t y = 0;
} IPoint;

typedef struct basepoint {
    double x;
    double y;

    void transform (basepoint *from, const std::array<double, 6> &transform);
} BasePoint;
#endif

struct instr_def {
    std::string name;
    uint8_t rangeStart;
    uint8_t rangeEnd;
    int nPops;
    int nPushes;
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

class ConicGlyph;

struct instr_props {
    uint16_t maxTwilight;
    uint16_t maxStackDepth;
    uint16_t maxStorage;
    uint16_t rBearingPointNum;
    uint16_t numIdefs;
    bool z0used;
    bool rBearingTouched;
    std::vector<std::vector<uint8_t>> fdefs;
};

struct GraphicsState {
    uint8_t size;
    uint16_t upm;
    size_t nloop;
    BasePoint projVector;
    BasePoint freeVector;
    BasePoint dualVector;
    std::array<uint16_t, 3> zp;
    std::array<uint16_t, 3> rp;
    bool flip;
    int errorCode;
    uint32_t errorPos;
    std::vector<int32_t> istack;
    std::vector<int32_t> storage;
    std::vector<int16_t> cvt;
    std::vector<IPoint> twilightPts;
    ConicGlyph *g;

    GraphicsState () :
	nloop (1), projVector { 1.0, 0.0 }, freeVector { 1.0, 0.0 },
	zp { 1, 1, 1 }, rp { 0, 0, 0 }, flip (true), errorCode (0), g (nullptr) {};
    bool getPoint (uint32_t num, int zp_num, IPoint &pt);
    bool setZonePointer (instr_props &props, int idx, int val);
    int16_t readCvt (int idx);
    bool writeCvt (int idx, int16_t val);
    int32_t readStorage (size_t idx);
    void writeStorage (size_t idx, int32_t val);
    bool pop (int32_t &val);
    bool pop2 (int32_t &val1, int32_t &val2);
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
	Parse_Unexpected = 8,

	Parse_WrongZone,
	Parse_WrongPointNumber,
	Parse_WrongTwilightPointNumber,
	Parse_WrongFunctionNumber,
	Parse_WrongCvtIndex,
	Parse_WrongStorageIndex,
	Parse_StackExceeded,
	Parse_UnexpectedEnd,
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

    static int quickExecute (std::vector<uint8_t> &bytecode, GraphicsState &state, instr_props &props, int level=0);
    static void reportError (GraphicsState &state, uint32_t table, uint16_t gid);

public slots:
    void edit ();
    void discard ();

signals:
    void instrChanged ();

private:
    static std::map<std::string, uint8_t> ByInstr;
    static const std::map<uint8_t, instr_def> InstrSet;
    static const std::map<std::string, uint8_t> ByArg;
    static int skipBranch (std::vector<uint8_t> &bytecode, uint32_t &pos, bool def, int level);

    static int getInstrArgs (std::vector<std::string> &args, std::string &edited, size_t &pos, int &start, int &len);
    static int checkInstrArgs (instr_data &d, std::vector<std::string> &args);

    void decode (uint8_t *data, uint16_t len);
    int parse (std::string &edited, std::vector<instr_data> &instr, int &sel_start, int &sel_len);
    void fillTable ();
    void fillEdit ();

    int skipBranch (std::vector<uint8_t> &bytecode, uint16_t &pos);

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
    InstrTableEdit (std::shared_ptr<FontTable> tptr, sFont* font, QWidget *parent);
    ~InstrTableEdit () {};

    void resetData () override {};
    bool checkUpdate (bool can_cancel) override;
    bool isModified () override;
    bool isValid () override;
    std::shared_ptr<FontTable> table () override;

    void closeEvent (QCloseEvent *event);

public slots:
    void save ();

private:
    void decode ();
    void fillInstrTab ();

    sFont *m_font;
    std::shared_ptr<FontTable> m_table;
    InstrEdit *m_instrEdit;
    bool m_valid;

    QPushButton *m_okButton, *m_cancelButton;
};

#endif
