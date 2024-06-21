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
#include <QAbstractListModel>
#include "tables.h" // Have to load it here due to inheritance from TableEdit

class sfntFile;
typedef struct ttffont sFont;
class FontTable;
class ColrTable;
class CpalTable;
class NameTable;
class NameProxy;
class NameRecordModel;
class TableEdit;
class GlyphNameProvider;
struct cpal_palette;

class ColorModel : public QAbstractTableModel {
    Q_OBJECT;

public:
    ColorModel (cpal_palette *pal, QVector<QString> &en_list, QWidget *parent = nullptr);
    ~ColorModel ();
    int rowCount (const QModelIndex &parent) const override;
    int columnCount (const QModelIndex &parent) const override;
    QVariant data (const QModelIndex &index, int role) const override;
    bool setData (const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags (const QModelIndex &index) const override;
    QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void truncate (int new_count);
    void expand (int new_count);

signals:
    void needsSelectionUpdate (int row);

private:
    cpal_palette *m_pal;
    QVector<QString> &m_entryNames;
    QWidget *m_parent;
};

class ListEntryIdModel : public QAbstractTableModel {
    Q_OBJECT;

public:
    ListEntryIdModel (std::vector<uint16_t> &idxList, QWidget *parent = nullptr);
    ~ListEntryIdModel ();
    int rowCount (const QModelIndex &parent) const override;
    int columnCount (const QModelIndex &parent) const override;
    QVariant data (const QModelIndex &index, int role) const override;
    bool setData (const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags (const QModelIndex &index) const override;
    QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void truncate (int new_count);
    void expand (int new_count);

private:
    std::vector<uint16_t> &m_idxList;
    QWidget *m_parent;
};

class PaletteTab : public QWidget {
    Q_OBJECT;

public:
    PaletteTab (cpal_palette *pal, NameTable *name, int idx, QVector<QString> &en_list, QTabWidget *parent=nullptr);
    ~PaletteTab ();
    void setTableVersion (int version);
    void setColorCount (int count);
    QString label ();
    bool checkNameSelection ();
    void flush ();

public slots:
    void startColorEditor (const QModelIndex &index);
    void addNameRecord ();
    void removeSelectedNameRecord ();

private slots:
    void onNameIdChange (int val);

signals:
    void tableModified (bool val);
    void needsLabelUpdate (int pal_idx);
    void fwdNameSelectionChanged (const QItemSelection &newSelection, const QItemSelection &oldSelection);

private:
    void fillControls ();

    cpal_palette *m_pal;
    int m_idx;
    QVector<QString> &m_entryNames;
    std::unique_ptr<NameProxy> m_nameProxy;
    std::unique_ptr<NameRecordModel> m_nameModel;

    QSpinBox *m_nameIdBox;
    QListWidget *m_flagList;
    QTableView *m_nameView;
    QTableView *m_colorList;

    std::unique_ptr<ColorModel> m_color_model;
};

class CpalEdit : public TableEdit {
    Q_OBJECT;

public:
    CpalEdit (std::shared_ptr<FontTable> tptr, sFont* font, QWidget *parent);
    ~CpalEdit ();

    void resetData () override {};
    bool checkUpdate (bool can_cancel) override;
    bool isModified () override;
    bool isValid () override;
    std::shared_ptr<FontTable> table () override;

    void closeEvent (QCloseEvent *event);
    QSize minimumSize () const;
    QSize sizeHint () const;

public slots:
    void setTableVersion (int version);
    void setPalettesNumber (int value);
    void setEntriesNumber (int value);
    void updatePaletteLabel (int pal_idx);
    void updateEntryList (const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles);
    void updateNameModel ();
    void onTabChange (int index);
    void onPaletteChange (int index);
    void checkNameSelection (const QItemSelection &newSelection, const QItemSelection &oldSelection);
    void addNameRecord ();
    void removeSelectedNameRecord ();
    void save ();

private:
    QString entryLabel (int idx);
    void fillControls ();

    bool m_valid;

    std::shared_ptr<CpalTable> m_cpal;
    std::shared_ptr<NameTable> m_name;
    sFont *m_font;
    std::unique_ptr<ListEntryIdModel> m_entry_id_model;
    std::unique_ptr<NameProxy> m_nameProxy;
    std::unique_ptr<NameRecordModel> m_nameModel;
    QVector<QString> m_entryNames;

    QAction *saveAction, *addAction, *removeAction, *closeAction;
    QTabWidget *m_tab;
    QWidget *m_cpalTab;
    QSpinBox *m_cpalVersionBox, *m_numPalettesBox, *m_numEntriesBox;
    QTableView *m_entryIdList, *m_entryNameView;
    QTabWidget *m_palContainer;

    QPushButton *saveButton, *closeButton, *addButton, *removeButton;
};
