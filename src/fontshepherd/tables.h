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

#ifndef _FONSHEPHERD_TABLES_H
#define _FONSHEPHERD_TABLES_H
#include <stdint.h>
// otherwise getting "incomplete type error" on array declarations
#include <array>
#include <QtWidgets>
#include "qhexedit.h"

class sfntFile;
typedef struct ttffont sFont;
class TableEdit;

struct TableHeader {
    QFile *file;
    uint32_t iname, checksum, off, length;
};

/* GWW: The EBDT and bdat tags could reasonable point to the same table
 * as could EBLC and bloc. Why haven't Apple and MS used the same tag?
 * they use the same formats...
 * The same table may be used by several fonts */
class FontTable {
    Q_DECLARE_TR_FUNCTIONS (FontTable)

    friend class TableViewModel;
    friend class sfntFile;
    friend class HexTableEdit;

public:
    FontTable (sfntFile* fontfile, const TableHeader &props);
    FontTable (FontTable* table);
    FontTable (QByteArray storage);
    virtual ~FontTable ();

    static void putushort (char *data, uint16_t val);
    static void putlong (char *data, uint32_t val);
    static void putfixed (char *data, double val);
    static void putvfixed (char *data, double val);

    static void putushort (std::ostream &os, uint16_t val);
    static void put3bytes (std::ostream &os, uint32_t val);
    static void putlong (std::ostream &os, uint32_t val);
    static void putfixed (std::ostream &os, double val);
    static void putvfixed (std::ostream &os, double val);

    static void put2dot14 (QDataStream &os, double dval);

    void fillup ();
    void copyData (FontTable* table);
    std::string stringName (int index=0) const;
    QByteArray serialize ();
    sfntFile *containerFile ();
    uint32_t iName (int index=0) const;

    virtual void unpackData (sFont*) {};
    virtual void edit (sFont* fnt, QWidget* caller);
    void hexEdit (sFont* fnt, QWidget* caller);

    void setModified (bool val);
    bool modified () const;
    void setContainer (sfntFile *cont_file);
    void clearEditor ();
    void setEditor (TableEdit *editor);
    TableEdit *editor ();
    bool loaded () const;
    bool interpreted () const;
    void clearData ();
    int orderingVal ();

    static uint16_t getushort (char *bdata, uint32_t pos);
    static uint32_t getlong (char *bdata, uint32_t pos);
    static double get2dot14 (char *bdata, uint32_t pos);

protected:
    uint16_t getushort (uint32_t pos);
    uint32_t get3bytes (uint32_t pos);
    uint32_t getlong (uint32_t pos);
    double getfixed (uint32_t pos);
    double getvfixed (uint32_t pos);
    double getversion (uint32_t pos);
    double get2dot14 (uint32_t pos);
    uint32_t getoffset (uint32_t pos, uint8_t size);

    sfntFile *container;
    /* No pointer to the font, because a given table may be part of several */
    /*  different fonts in a ttc */

    QFile *infile = nullptr;
    // May be referenced more than once (bdat/EBDT etc.), hence the list of names
    std::array<uint32_t, 4> m_tags;
    uint32_t oldchecksum;
    uint32_t start;
    uint32_t len;

    uint32_t newchecksum;	/* used during saving */
    uint32_t newstart;		/* used during saving */
    uint32_t newlen;		/* actual length, but data will be */
    char *data;			/*  padded out to 32bit boundary with 0*/
    bool changed: 1;		/* someone has changed either data or table_data */
    bool td_changed: 1;		/* it's table_data that has changed */
    bool required: 1;
    bool is_new: 1;		/* table is new, nothing to revert to */
    bool freeing: 1;		/* table has been put on list of tables to be freed */
    bool inserted: 1;		/* table has been inserted into ordered table list (for save) */
    bool processed: 1;
    TableEdit *tv;
    bool m_loaded, m_usable;
};

class TableEdit : public QMainWindow {
    Q_OBJECT;

public:
    TableEdit (QWidget* parent, Qt::WindowType type) : QMainWindow (parent, type) {}

    virtual void resetData () = 0;
    virtual bool checkUpdate (bool can_cancel) = 0;
    virtual bool isModified () = 0;
    virtual bool isValid () = 0;
    virtual FontTable* table () = 0;

signals:
    void update (FontTable *ft);
};

class HexTableEdit : public TableEdit {
    Q_OBJECT;

public:
    HexTableEdit (FontTable* tab, QWidget* parent);
    ~HexTableEdit ();

    void setData (char *data, int len);
    void resetData () override;
    bool checkUpdate (bool can_cancel) override;
    bool isModified () override;
    bool isValid () override;
    FontTable* table () override;
    void closeEvent (QCloseEvent *event);

private slots:
    void edited ();
    void save ();
    void toggleReadOnly (bool val);
    void toggleOverwrite (bool val);

private:
    FontTable *m_table;
    QHexEdit *m_hexedit;
    bool m_edited, m_valid;

    QAction *saveAction, *closeAction;
    QAction *undoAction, *redoAction, *toggleReadOnlyAction, *toggleOverwriteAction;

    QMenu *fileMenu, *editMenu;
};
#endif
