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

// Same as QUndoAction, internally used by QT
class fsUndoAction : public QAction {
    Q_OBJECT;

public:
    fsUndoAction (const QString &prefix, QObject *parent = 0);

public Q_SLOTS:
    void setPrefixedText (const QString &text);
    void setTextFormat (const QString &textFormat, const QString &defaultText);

private:
    QString m_prefix, m_defaultText;
};

class NonExclusiveUndoGroup : public QObject {
    Q_OBJECT

public:
    explicit NonExclusiveUndoGroup (QObject *parent = 0);

    virtual void addStack (QUndoStack *stack);
    virtual void removeStack (QUndoStack *stack);
    virtual QList<QUndoStack*> stacks () const;
    virtual QUndoStack *activeStack () const;

    virtual QAction *createUndoAction (
        QObject *parent, const QString &prefix = QString ()) const;
    virtual QAction *createRedoAction(
        QObject *parent, const QString &prefix = QString ()) const;
    virtual bool canUndo () const;
    virtual bool canRedo () const;
    virtual QString undoText () const;
    virtual QString redoText () const;
    virtual bool isClean (bool active_only=true) const;
    virtual void setClean (bool active_only=true);

public slots:
    virtual void undo ();
    virtual void redo ();
    virtual void setActiveStack (QUndoStack *stack);

signals:
    void activeStackChanged (QUndoStack *stack);
    void indexChanged (int idx);
    void cleanChanged (bool clean);
    void canUndoChanged (bool canUndo);
    void canRedoChanged (bool canRedo);
    void undoTextChanged (const QString &undoText);
    void redoTextChanged (const QString &redoText);

private:
    Q_DISABLE_COPY (NonExclusiveUndoGroup)
    QList<QUndoStack*> m_stacks;
    QUndoStack *m_active_stack;
};

class UndoGroupContainer : public QObject {
    Q_OBJECT;

public:
    UndoGroupContainer (QObject *parent=0);

    QAction* createUndoAction (QObject *parent, const QString &prefix) const;
    QAction* createRedoAction (QObject *parent, const QString &prefix) const;
    void addGroup (NonExclusiveUndoGroup *group);
    void removeGroup (NonExclusiveUndoGroup *group);
    void setActiveGroup (NonExclusiveUndoGroup *group);
    NonExclusiveUndoGroup* activeGroup () const;
    bool canUndo () const;
    bool canRedo () const;
    QString undoText () const;
    QString redoText () const;

public slots:
    virtual void undo ();
    virtual void redo ();

signals:
    void canUndoChanged (bool);
    void undoTextChanged (QString);
    void canRedoChanged (bool);
    void redoTextChanged (QString);
    void indexChanged (int);
    void cleanChanged (bool);
    void activeGroupChanged (NonExclusiveUndoGroup *);

private:
    NonExclusiveUndoGroup *m_active_ugroup;
    QList<NonExclusiveUndoGroup *> m_undo_groups;
};
