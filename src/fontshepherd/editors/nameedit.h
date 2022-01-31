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

#ifndef _FONSHEPHERD_NAMEEDIT_H
#define _FONSHEPHERD_NAMEEDIT_H
#include <QtWidgets>
#include <QAbstractListModel>
#include "tables.h"

class sfntFile;
typedef struct ttffont sFont;
class FontTable;
class NameTable;
class TableEdit;
struct name_record;

class AddNameDialog : public QDialog {
    Q_OBJECT;

public:
    explicit AddNameDialog (NameTable *name, QWidget *parent = 0);

    int platform () const;
    int encoding () const;
    int language () const;
    int nameType () const;
    QString nameText () const;
    int rowAvailable () const;

public slots:
    void accept () override;
    void setPlatformSpecific (int plat);

private:
    void fillBoxes ();

    NameTable *m_name;
    QComboBox *m_platformBox, *m_encodingBox, *m_languageBox, *m_nameTypeBox;
    QPlainTextEdit *m_editBox;
    int m_row;
};

class AddLangTagDialog : public QDialog {
    Q_OBJECT;

public:
    explicit AddLangTagDialog (NameTable *name, QWidget *parent = 0);
    QString langTag () const;

public slots:
    void accept () override;

private:
    NameTable *m_name;
    QLineEdit *m_editBox;
};

class NameRecordModel : public QAbstractTableModel {
    Q_OBJECT;

public:
    NameRecordModel (NameTable *name, QWidget *parent = nullptr);
    ~NameRecordModel ();
    int rowCount (const QModelIndex &parent) const override;
    int columnCount (const QModelIndex &parent) const override;
    QVariant data (const QModelIndex &index, int role) const override;
    bool setData (const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags (const QModelIndex &index) const override;
    QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool removeRows (int row, int count, const QModelIndex &index) override;

    void beginResetModel ();
    void endResetModel ();

    QModelIndex insertRows (QList<name_record> &input, int row);
    void clearCustomLangTagDependent ();

public slots:
    void setNamesClean (bool clean);

signals:
    void needsSelectionUpdate (int row, int count);
    void needsLabelUpdate ();

private:
    NameTable *m_name;
};

class LangTagModel : public QAbstractTableModel {
    Q_OBJECT;

public:
    LangTagModel (NameTable *name, int shift, QWidget *parent = nullptr);
    ~LangTagModel ();
    int rowCount (const QModelIndex &parent) const override;
    int columnCount (const QModelIndex &parent) const override;
    QVariant data (const QModelIndex &index, int role) const override;
    bool setData (const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags (const QModelIndex &index) const override;
    QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool removeRows (int row, int count, const QModelIndex &index) override;

    QModelIndex insertRows (QList<QString> &input, int row);
    void clearModel ();
    void sortRows ();
    void unSortRows (QList<QString> &order, int row, int count);

public slots:
    void setLanguagesClean (bool clean);

signals:
    void needsSelectionUpdate (int row, int count);
    void needsLabelUpdate ();

private:
    NameTable *m_name;
    // MS spec for the 'name' table format 1 says custom language tags
    // are numbered beginning from 0x8000, while Apple spec for the 'ldef'
    // table says the indexes start from zero. Hence the 'shift' value.
    int m_shift;
};

class NameRecordCommand : public QUndoCommand {
public:
    NameRecordCommand (NameRecordModel *model, int row, int count);
    NameRecordCommand (NameRecordModel *model, const QList<name_record> &input, int row);

    void redo ();
    void undo ();

private:
    NameRecordModel *m_model;
    int m_row, m_count;
    QList<name_record> m_data;
    bool m_remove;
};

class LangTagCommand : public QUndoCommand {
public:
    LangTagCommand (LangTagModel *model, int row, int count);
    LangTagCommand (LangTagModel *model, const QList<QString> &input, int row);

    void redo ();
    void undo ();

private:
    LangTagModel *m_model;
    int m_row, m_count;
    QList<QString> m_data;
    bool m_remove;
};

class SortLangTagsCommand : public QUndoCommand {
public:
    SortLangTagsCommand (LangTagModel *model, int row, int count);

    void redo ();
    void undo ();

private:
    LangTagModel *m_model;
    int m_row, m_count;
    QList<QString> m_data;
};

class SetStringCommand : public QUndoCommand {
public:
    SetStringCommand (QAbstractItemModel *model, const QModelIndex &index, QString text);

    void redo ();
    void undo ();

private:
    QAbstractItemModel *m_model;
    const QModelIndex m_index;
    QString m_old;
    QString m_new;
};

class NameEdit : public TableEdit {
    Q_OBJECT;

public:
    NameEdit (FontTable* tab, sFont* font, QWidget *parent);
    ~NameEdit ();

    static void setEditWidth (QTableView *edit, int height_ratio=10);

    void resetData () override {};
    bool checkUpdate (bool can_cancel) override;
    bool isModified () override;
    bool isValid () override;
    FontTable* table () override;

    void closeEvent (QCloseEvent *event);
    QSize minimumSize () const;
    QSize sizeHint () const override;

public slots:
    void save ();
    void switchTableVersion (int val);
    void onTabChange (int index);
    void customContextMenu (const QPoint &point);

    void addNameRecord ();
    void removeNameRecord ();
    void addLangTag ();
    void removeLangTag ();
    void sortLangTags ();

    void updateTableSelection (int idx, int count);
    void updateLabels ();

private:
    void fillNameTable ();
    void fillLangTable ();
    void setMenuBar ();

    bool m_valid;

    FontTable *m_table;
    NameTable *m_name;
    sFont *m_font;

    std::unique_ptr<QUndoGroup> m_uGroup;
    QUndoStack *m_nameStack, *m_langStack;
    QAction *saveAction, *addAction, *removeAction, *closeAction, *sortAction;
    QAction *undoAction, *redoAction;

    QTabWidget *m_tab;
    QTableView *m_nametab, *m_langtab;
    QComboBox *m_versionBox;
    QPushButton *saveButton, *closeButton, *addButton, *removeButton;

    std::unique_ptr<QAbstractItemModel> m_nameModel, m_langModel;
};
#endif
