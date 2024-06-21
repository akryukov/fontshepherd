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

#include <QtWidgets>

class TableView;
class TableViewContainer;

class FontShepherdMain : public QMainWindow {
    Q_OBJECT;

public:
    FontShepherdMain (QApplication *app, QString &path);
    ~FontShepherdMain ();
    void tile (const QMainWindow *previous);
    void closeEvent (QCloseEvent *event) override;

public slots:
    void setModified (bool val);
    void enableEditActions (int index, int row);

private slots:
    void open (QString path=QString (""));
    void save ();
    void saveFontAs ();
    void saveCollectionAs ();
    void addToCollection ();
    void removeFromCollection ();
    void updateRecentFileActions ();
    void openRecentFile ();
    void checkSelection (TableView *tv);
    void checkClipboard ();
    void connectEditActions (int index);
    //void quit ();

private:
    static bool hasRecentFiles ();
    void prependToRecentFiles (const QString &fileName);
    void setRecentFilesVisible (bool visible);
    static QString strippedName (const QString &fullFileName);
    void contextMenuEvent (QContextMenuEvent *event);
    bool hasTrueType ();

    QApplication *m_application;
    std::unique_ptr<TableViewContainer> m_tableMatrix;
    bool m_modified;

    enum { MaxRecentFiles = 5 };

    QAction *openAction, *saveAction, *saveFontAsAction, *saveCollAsAction;
    QAction *addFontAction, *removeFontAction;
    QAction *closeAction, *exitAction;
    QAction *cutAction, *copyAction, *pasteAction, *clearAction, *unselectAction, *editAction, *hexEditAction;
    QAction *recentFileActs[MaxRecentFiles], *recentFileSeparator, *recentFileSubMenuAct;
    QAction *undoAction, *redoAction;
    QAction *hdmxAction, *ltshAction, *vdmxAction;

    QPushButton *openButton, *saveButton, *closeButton;

    QMenu *fileMenu, *editMenu, *toolMenu;
};
