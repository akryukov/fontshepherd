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

#include "fs_undo.h"

// Same as QUndoAction, internally used by QT
fsUndoAction::fsUndoAction (const QString &prefix, QObject *parent) : QAction(parent) {
     m_prefix = prefix;
}

void fsUndoAction::setPrefixedText (const QString &text) {
    if (m_defaultText.isEmpty ()) {
        QString s = m_prefix;
        if (!m_prefix.isEmpty () && !text.isEmpty ())
            s.append (QLatin1Char (' '));
        s.append (text);
        setText (s);
    } else {
        if (text.isEmpty ())
            setText (m_defaultText);
        else
            setText (m_prefix.arg (text));
    }
}

void fsUndoAction::setTextFormat (const QString &textFormat, const QString &defaultText) {
    m_prefix = textFormat;
    m_defaultText = defaultText;
}

UndoGroupContainer::UndoGroupContainer (QObject *parent) :
    QObject (parent), m_active_ugroup (nullptr) {}

QAction *UndoGroupContainer::createUndoAction (QObject *parent, const QString &prefix) const {
     QString pref = prefix.isEmpty () ? tr ("Undo") : prefix;
     fsUndoAction *result = new fsUndoAction (pref, parent);
     result->setEnabled (canUndo ());
     result->setPrefixedText (undoText ());
     connect (this, &UndoGroupContainer::canUndoChanged,
            result, &fsUndoAction::setEnabled);
     connect (this, &UndoGroupContainer::undoTextChanged,
	    result, &fsUndoAction::setPrefixedText);
     connect (result, &fsUndoAction::triggered,
	    this, &UndoGroupContainer::undo);
     return result;
}

QAction *UndoGroupContainer::createRedoAction (QObject *parent, const QString &prefix) const {
     QString pref = prefix.isEmpty() ? tr ("Redo") : prefix;
     fsUndoAction *result = new fsUndoAction (pref, parent);
     result->setEnabled (canRedo ());
     result->setPrefixedText (redoText ());
     connect (this, &UndoGroupContainer::canRedoChanged,
            result, &fsUndoAction::setEnabled);
     connect (this, &UndoGroupContainer::redoTextChanged,
            result, &fsUndoAction::setPrefixedText);
     connect (result, &fsUndoAction::triggered,
	    this, &UndoGroupContainer::redo);
     return result;
}

void UndoGroupContainer::addGroup (NonExclusiveUndoGroup *group) {
    if (m_undo_groups.contains (group))
        return;
    m_undo_groups.append (group);
}

void UndoGroupContainer::removeGroup (NonExclusiveUndoGroup *group) {
    if (m_undo_groups.removeAll (group) == 0)
        return;
    if (m_active_ugroup == group)
        setActiveGroup (nullptr);
}

void UndoGroupContainer::setActiveGroup (NonExclusiveUndoGroup *group) {
    if (m_active_ugroup == group)
        return;

    if (m_active_ugroup != 0) {
        disconnect (m_active_ugroup, &NonExclusiveUndoGroup::canUndoChanged,
                    this, &UndoGroupContainer::canUndoChanged);
        disconnect (m_active_ugroup, &NonExclusiveUndoGroup::undoTextChanged,
                    this, &UndoGroupContainer::undoTextChanged);
        disconnect (m_active_ugroup, &NonExclusiveUndoGroup::canRedoChanged,
                    this, &UndoGroupContainer::canRedoChanged);
        disconnect (m_active_ugroup, &NonExclusiveUndoGroup::redoTextChanged,
                    this, &UndoGroupContainer::redoTextChanged);
        disconnect (m_active_ugroup, &NonExclusiveUndoGroup::indexChanged,
                    this, &UndoGroupContainer::indexChanged);
        disconnect (m_active_ugroup, &NonExclusiveUndoGroup::cleanChanged,
                    this, &UndoGroupContainer::cleanChanged);
    }

    m_active_ugroup = group;

    if (m_active_ugroup == 0) {
        emit canUndoChanged (false);
        emit undoTextChanged (QString());
        emit canRedoChanged (false);
        emit redoTextChanged (QString());
        emit cleanChanged (true);
        emit indexChanged (0);
    } else {
        connect (m_active_ugroup, &NonExclusiveUndoGroup::canUndoChanged,
                this, &UndoGroupContainer::canUndoChanged);
        connect (m_active_ugroup, &NonExclusiveUndoGroup::undoTextChanged,
                this, &UndoGroupContainer::undoTextChanged);
        connect (m_active_ugroup, &NonExclusiveUndoGroup::canRedoChanged,
                this, &UndoGroupContainer::canRedoChanged);
        connect (m_active_ugroup, &NonExclusiveUndoGroup::redoTextChanged,
                this, &UndoGroupContainer::redoTextChanged);
        connect (m_active_ugroup, &NonExclusiveUndoGroup::indexChanged,
                this, &UndoGroupContainer::indexChanged);
        connect (m_active_ugroup, &NonExclusiveUndoGroup::cleanChanged,
                this, &UndoGroupContainer::cleanChanged);
        emit canUndoChanged (m_active_ugroup->canUndo ());
        emit undoTextChanged (m_active_ugroup->undoText ());
        emit canRedoChanged (m_active_ugroup->canRedo ());
        emit redoTextChanged (m_active_ugroup->redoText ());
        emit cleanChanged (m_active_ugroup->isClean ());
	if (m_active_ugroup->activeStack ())
	    emit indexChanged (m_active_ugroup->activeStack ()->index ());
	else
	    emit indexChanged (0);
    }

    emit activeGroupChanged (m_active_ugroup);
}

void UndoGroupContainer::undo () {
    if (m_active_ugroup != 0)
        m_active_ugroup->undo ();
}

void UndoGroupContainer::redo () {
    if (m_active_ugroup != 0)
        m_active_ugroup->redo ();
}

NonExclusiveUndoGroup *UndoGroupContainer::activeGroup () const {
    return m_active_ugroup;
}

bool UndoGroupContainer::canUndo () const {
    return m_active_ugroup != 0 && m_active_ugroup->canUndo ();
}

bool UndoGroupContainer::canRedo () const {
    return m_active_ugroup != 0 && m_active_ugroup->canRedo ();
}

QString UndoGroupContainer::undoText () const {
    return m_active_ugroup == 0 ? QString () : m_active_ugroup->undoText ();
}

QString UndoGroupContainer::redoText () const {
    return m_active_ugroup == 0 ? QString () : m_active_ugroup->redoText ();
}

NonExclusiveUndoGroup::NonExclusiveUndoGroup (QObject *parent) :
    QObject (parent), m_active_stack (nullptr) {}

void NonExclusiveUndoGroup::addStack (QUndoStack *stack) {
    if (m_stacks.contains(stack))
        return;
    m_stacks.append (stack);
}

void NonExclusiveUndoGroup::removeStack (QUndoStack *stack) {
    if (m_stacks.removeAll (stack) == 0)
        return;
    if (stack == m_active_stack)
        setActiveStack (0);
}

QList<QUndoStack*> NonExclusiveUndoGroup::stacks () const {
    return m_stacks;
}

void NonExclusiveUndoGroup::setActiveStack (QUndoStack *stack) {
    if (m_active_stack == stack)
        return;

    if (m_active_stack != 0) {
        disconnect (m_active_stack, &QUndoStack::canUndoChanged,
                    this, &NonExclusiveUndoGroup::canUndoChanged);
        disconnect (m_active_stack, &QUndoStack::undoTextChanged,
                    this, &NonExclusiveUndoGroup::undoTextChanged);
        disconnect (m_active_stack, &QUndoStack::canRedoChanged,
                    this, &NonExclusiveUndoGroup::canRedoChanged);
        disconnect (m_active_stack, &QUndoStack::redoTextChanged,
                    this, &NonExclusiveUndoGroup::redoTextChanged);
        disconnect (m_active_stack, &QUndoStack::indexChanged,
                    this, &NonExclusiveUndoGroup::indexChanged);
        disconnect (m_active_stack, &QUndoStack::cleanChanged,
                    this, &NonExclusiveUndoGroup::cleanChanged);
    }

    m_active_stack = stack;

    if (m_active_stack == 0) {
        emit canUndoChanged (false);
        emit undoTextChanged (QString ());
        emit canRedoChanged (false);
        emit redoTextChanged (QString ());
        emit cleanChanged (true);
        emit indexChanged (0);
    } else {
        connect (m_active_stack, &QUndoStack::canUndoChanged,
                this, &NonExclusiveUndoGroup::canUndoChanged);
        connect (m_active_stack, &QUndoStack::undoTextChanged,
                this, &NonExclusiveUndoGroup::undoTextChanged);
        connect (m_active_stack, &QUndoStack::canRedoChanged,
                this, &NonExclusiveUndoGroup::canRedoChanged);
        connect (m_active_stack, &QUndoStack::redoTextChanged,
                this, &NonExclusiveUndoGroup::redoTextChanged);
        connect (m_active_stack, &QUndoStack::indexChanged,
                this, &NonExclusiveUndoGroup::indexChanged);
        connect (m_active_stack, &QUndoStack::cleanChanged,
                this, &NonExclusiveUndoGroup::cleanChanged);
        emit canUndoChanged (m_active_stack->canUndo ());
        emit undoTextChanged (m_active_stack->undoText ());
        emit canRedoChanged (m_active_stack->canRedo ());
        emit redoTextChanged (m_active_stack->redoText ());
        emit cleanChanged (m_active_stack->isClean ());
        emit indexChanged (m_active_stack->index ());
    }

    emit activeStackChanged (m_active_stack);
}

QUndoStack *NonExclusiveUndoGroup::activeStack () const {
    return m_active_stack;
}

void NonExclusiveUndoGroup::undo () {
    if (m_active_stack != 0)
        m_active_stack->undo ();
}

void NonExclusiveUndoGroup::redo () {
    if (m_active_stack != 0)
        m_active_stack->redo ();
}

bool NonExclusiveUndoGroup::canUndo () const {
    return m_active_stack != 0 && m_active_stack->canUndo ();
}

bool NonExclusiveUndoGroup::canRedo () const {
    return m_active_stack != 0 && m_active_stack->canRedo ();
}

QString NonExclusiveUndoGroup::undoText () const {
    return m_active_stack == 0 ? QString() : m_active_stack->undoText ();
}

QString NonExclusiveUndoGroup::redoText () const {
    return m_active_stack == 0 ? QString () : m_active_stack->redoText ();
}

bool NonExclusiveUndoGroup::isClean (bool active_only) const {
    if (active_only) {
        return m_active_stack == 0 || m_active_stack->isClean ();
    } else {
        for (const QUndoStack *stack : m_stacks) {
            if (!stack->isClean ())
                return (false);
        }
    }
    return (true);
}

void NonExclusiveUndoGroup::setClean (bool active_only) {
    if (active_only && m_active_stack) {
        m_active_stack->setClean ();
    } else {
        for (QUndoStack *stack : m_stacks) {
	    stack->setClean ();
        }
    }
}

QAction *NonExclusiveUndoGroup::createUndoAction (QObject *parent, const QString &prefix) const {
    fsUndoAction *result = new fsUndoAction (prefix, parent);
    if (prefix.isEmpty ())
        result->setTextFormat (tr ("Undo %1"), tr ("Undo", "Default text for undo action"));

    result->setEnabled (canUndo ());
    result->setPrefixedText (undoText ());
    connect (this, &NonExclusiveUndoGroup::canUndoChanged,
	    result, &fsUndoAction::setEnabled);
    connect (this, &NonExclusiveUndoGroup::undoTextChanged,
	    result, &fsUndoAction::setPrefixedText);
    connect (result, &fsUndoAction::triggered,
	    this, &NonExclusiveUndoGroup::undo);
    return result;
}

QAction *NonExclusiveUndoGroup::createRedoAction (QObject *parent, const QString &prefix) const {
    fsUndoAction *result = new fsUndoAction (prefix, parent);
    if (prefix.isEmpty ())
        result->setTextFormat (tr ("Redo %1"), tr ("Redo", "Default text for redo action"));

    result->setEnabled (canRedo ());
    result->setPrefixedText (redoText ());
    connect (this, &NonExclusiveUndoGroup::canRedoChanged,
	    result, &fsUndoAction::setEnabled);
    connect (this, &NonExclusiveUndoGroup::redoTextChanged,
	    result, &fsUndoAction::setPrefixedText);
    connect (result, &fsUndoAction::triggered,
	    this, &NonExclusiveUndoGroup::redo);
    return result;
}
