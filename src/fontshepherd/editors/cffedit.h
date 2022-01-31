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
#include <sstream>
#include "cffstuff.h"

typedef struct ttffont sFont;
class CffTable;

class CffDictDelegate : public QStyledItemDelegate {
    Q_OBJECT;

public:
    explicit CffDictDelegate (bool is_priv, QObject *parent = nullptr);

    QWidget* createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void setEditorData (QWidget *editor, const QModelIndex &index) const override;
    void setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;
    void updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    bool m_private;
};

class PrivateDelegate : public QStyledItemDelegate {
    Q_OBJECT;

public:
    explicit PrivateDelegate (QObject *parent = nullptr);

    QWidget* createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void setEditorData (QWidget *editor, const QModelIndex &index) const override;
    void setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;
    void updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

class TopDelegate : public QStyledItemDelegate {
    Q_OBJECT;

public:
    explicit TopDelegate (QObject *parent = nullptr);

    QWidget* createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void setEditorData (QWidget *editor, const QModelIndex &index) const override;
    void setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;
    void updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

class CffDialog : public QDialog {
    Q_OBJECT;

public:
    explicit CffDialog (sFont *fnt, CffTable *cff, QWidget *parent = 0);
    QSize sizeHint () const override;

signals:
    void glyphNamesChanged ();

public slots:
    void accept () override;
    void addEntry ();
    void removeEntry ();
    void onTabChange (int index);
    void setTableVersion (int idx);

private:
    void fillTopTab (QTableWidget *tab, TopDict *td);
    void fillPrivateTab (QTableWidget *tab, PrivateDict *pd);
    void fillGlyphTab (QTableWidget *tab);
    void fillFdSelTab (QTableWidget *tab);
    QSize minimumSize () const;

    sFont *m_font;
    CffTable *m_cff;
    QComboBox  *m_versionBox;
    QTabWidget *m_tab, *m_privTab;
    QTableWidget *m_topTab, *m_gnTab, *m_fdSelTab;
    std::vector<QTableWidget *> m_privateTabs;

    QAction *m_addAction, *m_removeAction;
    QAction *m_undoAction, *m_redoAction;
    QPushButton *m_okButton, *m_cancelButton, *m_addButton, *m_removeButton;
};

class FdSelectDelegate : public QStyledItemDelegate {
    Q_OBJECT;

public:
    explicit FdSelectDelegate (const QStringList &sflist, QObject *parent = nullptr);

    QWidget* createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void setEditorData (QWidget *editor, const QModelIndex &index) const override;
    void setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;
    void updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    const QStringList m_sflist;
};
