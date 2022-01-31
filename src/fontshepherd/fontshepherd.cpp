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

#include <clocale>
#include "fontshepherd.h"
#include "sfnt.h"
#include "tables.h"
#include "tableview.h"

FontShepherdMain::FontShepherdMain (QApplication *app, QString &path) {
    setAttribute (Qt::WA_DeleteOnClose);
    m_application = app;
    m_modified = false;

    m_tableMatrix = std::unique_ptr<TableViewContainer> (new TableViewContainer (path, this));
    TableViewContainer *tmptr = dynamic_cast<TableViewContainer *> (m_tableMatrix.get ());
    connect (tmptr, &QTabWidget::currentChanged, this, &FontShepherdMain::connectEditActions);
    if (!(tmptr->hasFont ())) {
        QTimer::singleShot (0, this, SLOT (close()));
        return;
    }

    openAction = new QAction (tr ("&Open"), this);
    saveAction = new QAction (tr ("&Save"), this);
    saveFontAsAction = new QAction (tr ("Save font &as..."), this);
    saveCollAsAction = new QAction (tr ("Sa&ve collection as..."), this);
    saveCollAsAction->setEnabled (m_tableMatrix->count () > 1);
    addFontAction = new QAction (tr ("A&dd font to collection..."), this);
    removeFontAction = new QAction (tr ("&Remove font from collection..."), this);
    removeFontAction->setEnabled (m_tableMatrix->count () > 1);
    closeAction = new QAction (tr ("C&lose"), this);
    exitAction = new QAction (tr ("E&xit"), app);

    connect (openAction, &QAction::triggered, [=](){this->open ();});
    connect (saveAction, &QAction::triggered, this, &FontShepherdMain::save);
    connect (saveFontAsAction, &QAction::triggered, this, &FontShepherdMain::saveFontAs);
    connect (saveCollAsAction, &QAction::triggered, this, &FontShepherdMain::saveCollectionAs);
    connect (addFontAction, &QAction::triggered, this, &FontShepherdMain::addToCollection);
    connect (removeFontAction, &QAction::triggered, this, &FontShepherdMain::removeFromCollection);
    connect (closeAction, &QAction::triggered, this, &FontShepherdMain::close);
    connect (exitAction, &QAction::triggered, app, &QApplication::quit);
    connect (app, &QApplication::aboutToQuit, this, &FontShepherdMain::close);

    openAction->setShortcut (QKeySequence::Open);
    saveAction->setShortcut (QKeySequence::Save);
    closeAction->setShortcut (QKeySequence::Close);
    exitAction->setShortcut (QKeySequence (Qt::CTRL + Qt::Key_Q));

    cutAction = new QAction (tr ("C&ut"), this);
    copyAction = new QAction (tr ("&Copy"), this);
    pasteAction = new QAction (tr ("&Paste"), this);
    clearAction = new QAction (tr ("&Delete"), this);
    unselectAction = new QAction (tr ("Clear &selection"), app);
    editAction = new QAction (tr ("&Edit table..."), app);
    hexEditAction = new QAction (tr ("Edit table as &Hex..."), app);

    undoAction = tmptr->undoAction (this, tr ("&Undo"));
    redoAction = tmptr->redoAction (this, tr ("Re&do"));
    undoAction->setShortcut (QKeySequence::Undo);
    redoAction->setShortcut (QKeySequence::Redo);

    connectEditActions (0);
    connect (QApplication::clipboard (), &QClipboard::dataChanged, this, &FontShepherdMain::checkClipboard);

    cutAction->setShortcut (QKeySequence::Cut);
    copyAction->setShortcut (QKeySequence::Copy);
    pasteAction->setShortcut (QKeySequence::Paste);
    unselectAction->setShortcut (QKeySequence (Qt::Key_Escape));

    cutAction->setEnabled (false);
    copyAction->setEnabled (false);
    pasteAction->setEnabled (false);
    clearAction->setEnabled (false);
    unselectAction->setEnabled (false);
    editAction->setEnabled (false);
    hexEditAction->setEnabled (false);

    fileMenu = menuBar ()->addMenu (tr ("&File"));
    fileMenu->addAction (openAction);
    fileMenu->addAction (saveAction);
    fileMenu->addAction (saveFontAsAction);
    fileMenu->addAction (saveCollAsAction);

    fileMenu->addSeparator ();
    fileMenu->addAction (addFontAction);
    fileMenu->addAction (removeFontAction);

    fileMenu->addSeparator ();
    QMenu *recentMenu = fileMenu->addMenu (tr ("Recent..."));
    recentFileSubMenuAct = recentMenu->menuAction ();
    recentFileSeparator = fileMenu->addSeparator ();

    prependToRecentFiles (path);
    for (int i = 0; i < MaxRecentFiles; ++i) {
        recentFileActs[i] = recentMenu->addAction (QString (), this, SLOT (openRecentFile ()));
        recentFileActs[i]->setVisible (false);
    }
    setRecentFilesVisible (hasRecentFiles ());

    fileMenu->addAction (closeAction);
    fileMenu->addAction (exitAction);

    editMenu = menuBar ()->addMenu (tr ("&Edit"));
    editMenu->addAction (cutAction);
    editMenu->addAction (copyAction);
    editMenu->addAction (pasteAction);
    editMenu->addAction (clearAction);
    editMenu->addSeparator ();
    editMenu->addAction (unselectAction);
    editMenu->addSeparator ();
    editMenu->addAction (editAction);
    editMenu->addSeparator ();
    editMenu->addAction (undoAction);
    editMenu->addAction (redoAction);

    saveButton = new QPushButton (QWidget::tr ("&Save"));
    closeButton = new QPushButton (QWidget::tr ("&Close"));

    connect (saveButton, &QPushButton::clicked, this, &FontShepherdMain::save);
    connect (closeButton, &QPushButton::clicked, this, &FontShepherdMain::close);

    QVBoxLayout *layout;
    layout = new QVBoxLayout ();
    layout->addWidget (tmptr);

    QHBoxLayout *buttLayout;
    buttLayout = new QHBoxLayout ();
    buttLayout->addWidget (saveButton);
    buttLayout->addWidget (closeButton);
    layout->addLayout (buttLayout);

    QWidget *window = new QWidget ();
    window->setLayout (layout);
    setWindowIcon (QIcon (":/icons/fontshepherd-crozier.svg"));
    setCentralWidget (window);
}

FontShepherdMain::~FontShepherdMain () {
    qDebug() << "delete application";
}

void FontShepherdMain::contextMenuEvent (QContextMenuEvent *event) {
    QMenu menu (this);

    menu.addAction (cutAction);
    menu.addAction (copyAction);
    menu.addAction (pasteAction);
    menu.addAction (clearAction);
    menu.addSeparator ();
    menu.addAction (editAction);
    menu.addAction (hexEditAction);

    menu.exec (event->globalPos ());
}

void FontShepherdMain::open (QString path) {
    FontShepherdMain *secondary = new FontShepherdMain (m_application, path);
    secondary->tile (this);
    secondary->show ();
}

void FontShepherdMain::save () {
    m_tableMatrix->saveFont (true, m_tableMatrix->count () > 1);
    unselectAction->trigger ();
}

void FontShepherdMain::saveFontAs () {
    m_tableMatrix->saveFont (false, false);
    unselectAction->trigger ();
}

void FontShepherdMain::saveCollectionAs () {
    m_tableMatrix->saveFont (false, true);
    unselectAction->trigger ();
}

void FontShepherdMain::addToCollection () {
    QString path = QFileDialog::getOpenFileName (
	m_tableMatrix.get (), tr ("Open Font"), "",
        tr ("OpenType Font Files (*.ttf *.otf *.ttc)"
    ));
    if (m_tableMatrix->loadFont (path))
	setModified (true);
    saveCollAsAction->setEnabled (m_tableMatrix->count () > 1);
}

void FontShepherdMain::removeFromCollection () {
    if (m_tableMatrix->count () == 1) {
        QMessageBox::critical (this, tr ("Remove font from collection"),
            tr ("Cannot remove the last and only font from the font file."));
	return;
    }
    QMessageBox::StandardButton ask;
    ask = QMessageBox::question (this,
        tr ("Remove font from collection"),
        tr ("Are you sure to remove a font from the collection? "
	    "This operation cannot be undone!"),
        QMessageBox::Yes|QMessageBox::No);
    if (ask == QMessageBox::Yes) {
	sfntFile *fcont = m_tableMatrix->font ();
	int index = m_tableMatrix->currentIndex ();
	QWidget *w = m_tableMatrix->widget (index);
	m_tableMatrix->removeTab (index);
	delete w;
	fcont->removeFromCollection (index);
	setModified (true);
	saveCollAsAction->setEnabled (m_tableMatrix->count () > 1);
    }
}

void FontShepherdMain::closeEvent (QCloseEvent *event) {
    sfntFile *fcont = m_tableMatrix->font ();
    if (fcont) {
	for (int i=0; i<fcont->fontCount (); i++) {
	    sFont *fnt = fcont->font (i);
	    for (auto &tabptr : fnt->tbls) {
		FontTable *tab = tabptr.get ();
		TableEdit *edit = tab->editor ();
		if (edit) {
		    if (edit->close ()) {
			edit->deleteLater ();
			tab->clearEditor ();
		    } else {
			event->ignore ();
			return;
		    }
		}
	    }
	}
    }
    qApp->instance ()->processEvents ();

    if (m_modified) {
        QMessageBox::StandardButton ask;
        ask = QMessageBox::question (this,
            tr ("Unsaved Changes"),
            tr ("Would you like to save changes?"),
            QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel);
        if (ask == QMessageBox::Cancel)
            event->ignore ();
        else if (ask == QMessageBox::Yes)
            save ();
    }
}

// When user switches to another tab
void FontShepherdMain::checkSelection (TableView *tv) {
    if (tv) {
	QItemSelectionModel *curidx = tv->selectionModel ();
	QModelIndexList rlist = curidx->selectedRows ();
	bool has_selection = !rlist.isEmpty ();

	cutAction->setEnabled (has_selection);
	copyAction->setEnabled (has_selection);
	clearAction->setEnabled (has_selection);
	unselectAction->setEnabled (has_selection);
	editAction->setEnabled (has_selection);
	hexEditAction->setEnabled (has_selection);
    }
}

void FontShepherdMain::checkClipboard () {
    QClipboard *clipboard = QApplication::clipboard ();
    const QMimeData *md = clipboard->mimeData ();
    pasteAction->setEnabled (md->hasFormat ("fontshepherd/x-fonttable"));
}

// When the selection in the current tab is changed
void FontShepherdMain::enableEditActions (int index, int row) {
    bool enabled = (row >= 0);
    if (index == m_tableMatrix->currentIndex ()) {
	cutAction->setEnabled (enabled);
	copyAction->setEnabled (enabled);
	clearAction->setEnabled (enabled);
	unselectAction->setEnabled (enabled);
	editAction->setEnabled (enabled);
	hexEditAction->setEnabled (enabled);
    }
}

void FontShepherdMain::connectEditActions (int index) {
    for (int i=0; i<m_tableMatrix->count (); i++) {
	TableView *tv = qobject_cast<TableView *> (m_tableMatrix->widget (i));
        disconnect (cutAction, &QAction::triggered, tv, &TableView::cut);
        disconnect (copyAction, &QAction::triggered, tv, &TableView::copy);
        disconnect (pasteAction, &QAction::triggered, tv, &TableView::paste);
        disconnect (clearAction, &QAction::triggered, tv, &TableView::clear);
        disconnect (unselectAction, &QAction::triggered, tv, &TableView::unselect);
        disconnect (editAction, &QAction::triggered, tv, &TableView::edit);
        disconnect (hexEditAction, &QAction::triggered, tv, &TableView::hexEdit);
    }
    if (index >= 0) {
	QWidget *w = m_tableMatrix->widget (index);
	if (w) {
	    TableView *tv = qobject_cast<TableView *> (w);
	    QUndoStack *us = tv->undoStack ();
	    us->setActive ();

	    connect (cutAction, &QAction::triggered, tv, &TableView::cut);
	    connect (copyAction, &QAction::triggered, tv, &TableView::copy);
	    connect (pasteAction, &QAction::triggered, tv, &TableView::paste);
	    connect (clearAction, &QAction::triggered, tv, &TableView::clear);
	    connect (unselectAction, &QAction::triggered, tv, &TableView::unselect);
	    connect (editAction, &QAction::triggered, tv, &TableView::edit);
	    connect (hexEditAction, &QAction::triggered, tv, &TableView::hexEdit);

	    checkSelection (tv);
	}
    }
}

void FontShepherdMain::setModified (bool val) {
    QString title = windowTitle ();
    bool has_asterisk = title.startsWith (QChar ('*'));

    if (has_asterisk && !val) {
        setWindowTitle (title.remove (0, 1));
    } else if (!has_asterisk && val) {
        setWindowTitle (title.prepend ('*'));
    }
    m_modified = val;
}

void FontShepherdMain::tile (const QMainWindow *previous) {
    if (!previous)
        return;
    int topFrameWidth = previous->geometry ().top () - previous->pos ().y ();
    if (!topFrameWidth)
        topFrameWidth = 40;
    const QPoint pos = previous->pos () + 2 * QPoint (topFrameWidth, topFrameWidth);
    if (QApplication::desktop ()->availableGeometry (this).contains (rect ().bottomRight () + pos))
        move (pos);
}

void FontShepherdMain::setRecentFilesVisible (bool visible) {
    recentFileSubMenuAct->setVisible (visible);
    recentFileSeparator->setVisible (visible);
}

static inline QString recentFilesKey() { return QString ("recentFileList"); }
static inline QString fileKey () { return QString ("file"); }

static QStringList readRecentFiles (QSettings &settings) {
    QStringList result;
    const int count = settings.beginReadArray (recentFilesKey ());
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex (i);
        result.append (settings.value (fileKey ()).toString());
    }
    settings.endArray ();
    return result;
}

static void writeRecentFiles (const QStringList &files, QSettings &settings) {
    const int count = files.size ();
    settings.beginWriteArray (recentFilesKey ());
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex (i);
        settings.setValue (fileKey (), files.at (i));
    }
    settings.endArray ();
}

QString FontShepherdMain::strippedName (const QString &fullFileName) {
    return QFileInfo (fullFileName).fileName ();
}

bool FontShepherdMain::hasRecentFiles () {
    QSettings settings(QCoreApplication::organizationName (), QCoreApplication::applicationName ());
    const int count = settings.beginReadArray (recentFilesKey ());
    settings.endArray ();
    return count > 0;
}

void FontShepherdMain::prependToRecentFiles (const QString &fileName) {
    QSettings settings (QCoreApplication::organizationName (), QCoreApplication::applicationName ());

    const QStringList oldRecentFiles = readRecentFiles (settings);
    QStringList recentFiles = oldRecentFiles;
    recentFiles.removeAll (fileName);
    recentFiles.prepend (fileName);
    if (oldRecentFiles != recentFiles)
        writeRecentFiles (recentFiles, settings);

    setRecentFilesVisible (!recentFiles.isEmpty ());
}

void FontShepherdMain::updateRecentFileActions () {
    QSettings settings (QCoreApplication::organizationName (), QCoreApplication::applicationName ());

    const QStringList recentFiles = readRecentFiles (settings);
    const int count = qMin (int (MaxRecentFiles), recentFiles.size ());

    int i = 0;
    for ( ; i < count; ++i) {
        const QString fileName = strippedName (recentFiles.at (i));
        recentFileActs[i]->setText (tr ("&%1 %2").arg (i + 1).arg (fileName));
        recentFileActs[i]->setData (recentFiles.at (i));
        recentFileActs[i]->setVisible (true);
    }
    for ( ; i < MaxRecentFiles; ++i)
        recentFileActs[i]->setVisible(false);
}

void FontShepherdMain::openRecentFile () {
    if (const QAction *action = qobject_cast<const QAction *> (sender ()))
        open (action->data ().toString ());
}

int main (int argc, char **argv) {
    QString path ("");

    for (int i=1; i<argc; ++i) {
        char *pt = argv[i];
	if (pt[0] == '-')
            i++;
        else {
            path = QString (argv[i]);
        }
    }

    setlocale (LC_ALL,"");
    QApplication app (argc, argv);
    QCoreApplication::setApplicationName ("FontShepherd");
    QCoreApplication::setOrganizationName ("ru.anagnost96");
    FontShepherdMain* fontshepherd = new FontShepherdMain (&app, path);

    fontshepherd->show ();

    return app.exec ();
}
