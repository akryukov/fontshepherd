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

class MultilineInputDialog : public QDialog {
    Q_OBJECT;

public:
    explicit MultilineInputDialog (QString title, QString prompt, QWidget *parent = 0);
    void setText (const QString text);
    QString text () const;
    QSize sizeHint () const override;
    void ensureFocus ();

private:
    QPlainTextEdit *m_editBox;
    bool m_accepted;
};

class TextDelegate : public QStyledItemDelegate {
    Q_OBJECT;

public:
    explicit TextDelegate (QUndoStack *us = nullptr, QObject *parent = nullptr);

    QWidget* createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void setEditorData (QWidget *editor, const QModelIndex &index) const override;
    void setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;
    void updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    QUndoStack *m_ustack;
};

class SpinBoxDelegate : public QStyledItemDelegate {
    Q_OBJECT;

public:
    explicit SpinBoxDelegate (int min, int max, QObject *parent = nullptr);

    QWidget* createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void setEditorData (QWidget *editor, const QModelIndex &index) const override;
    void setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;
    void updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    int m_min, m_max;
};

// This one checks the next and previous row to make sure the value doesn't
// get out of the range
class SortedSpinBoxDelegate : public SpinBoxDelegate {
    Q_OBJECT;

public:
    explicit SortedSpinBoxDelegate (QObject *parent = nullptr);

    void setEditorData (QWidget *editor, const QModelIndex &index) const override;
};

class TrueFalseDelegate : public QStyledItemDelegate {
    Q_OBJECT;

public:
    explicit TrueFalseDelegate (QObject *parent = nullptr, QString false_str = "false", QString true_str = "true");

    QWidget* createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void setEditorData (QWidget *editor, const QModelIndex &index) const override;
    void setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;
    void updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    QString byVal (bool val) const;
private:
    QString m_false, m_true;
};
