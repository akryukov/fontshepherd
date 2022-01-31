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

class sfntFile;
typedef struct ttffont sFont;
class FontTable;

class TableViewModel : public QAbstractTableModel {
    Q_OBJECT;

public:
    TableViewModel (sFont *fnt, int idx, QWidget *parent=nullptr);

    int rowCount (const QModelIndex &parent) const override;
    int columnCount (const QModelIndex &parent) const override;
    QVariant data (const QModelIndex &index, int role) const override;
    bool setData (const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags (const QModelIndex &index) const override;
    QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool removeRows (int row, int count, const QModelIndex &index) override;

    bool insertTable (int row, FontTable *tbl);
    bool pasteTable (int row, FontTable *tbl);

signals:
    void needsSelectionUpdate (int row);

public slots:
    void updateViews (FontTable *tbl);

private:
    sFont *m_font = nullptr;
    int m_index;
    QWidget *m_parent;
};

class TableView : public QTableView {
    Q_OBJECT;

public:
    TableView (sFont *fnt, int idx, QUndoStack *us, QWidget *parent);
    ~TableView ();

    void selectionChanged (const QItemSelection  &selected, const QItemSelection  &deselected);
    QUndoStack *undoStack ();

signals:
    void rowSelected (int tab_idx, int row_num);

public slots:
    void cut ();
    void copy ();
    void paste ();
    void clear ();
    void unselect ();
    void edit ();
    void hexEdit ();
    void doubleClickHandler (const QModelIndex &index);
    void updateSelection (int row);

private:
    void editTable (int row, bool hex);
    int getSelectionIndex ();

    sFont *m_font = nullptr;
    int m_index;
    QUndoStack *m_ustack;
    std::unique_ptr<TableViewModel> m_model;
    QWidget *m_parent;
};

class TableViewContainer : public QTabWidget {
    Q_OBJECT;

public:
    TableViewContainer (QString &path, QWidget *parent);
    ~TableViewContainer ();

    bool hasFont ();
    sfntFile *font ();
    bool loadFont (QString &path);
    void saveFont (bool overwrite, bool ttc=false);
    QAction *undoAction (QObject *parent, const QString &prefix = QString());
    QAction *redoAction (QObject *parent, const QString &prefix = QString());

public slots:
    void setFontModified (int font_idx, const bool val);

signals:
    void fileModified (bool val);

private:
    QString checkPath (QString &path);

    std::unique_ptr<sfntFile> fontFile;
    int curTab;
    bool m_has_font;
    std::unique_ptr<QUndoGroup> m_uGroup;
    QMap<QWidget*, QUndoStack*> m_uStackMap;
};

class AddOrRemoveTableCommand : public QUndoCommand {
public:
    AddOrRemoveTableCommand (TableViewModel *model, sFont *fnt, int row);
    AddOrRemoveTableCommand (TableViewModel *model, sFont *fnt, FontTable *tbl, int row);

    void redo ();
    void undo ();

private:
    TableViewModel *m_model;
    sFont *m_font;
    int m_row;
    bool m_remove;
    QByteArray m_table;
};

class PasteTableCommand : public QUndoCommand {
public:
    PasteTableCommand (TableViewModel *model, sFont *fnt, FontTable *table, int row);

    void redo ();
    void undo ();

private:
    TableViewModel *m_model;
    sFont *m_font;
    int m_row;
    QByteArray m_new;
    QByteArray m_old;
};
