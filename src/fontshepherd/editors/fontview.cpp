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

#include <cstdio>
#include <sstream>
#include <iostream>
#include <assert.h>
#include <stdint.h>
#include <limits>

#include "sfnt.h"
#include "editors/fontview.h" // also includes tables.h
#include "tables/glyphcontainer.h" // also includes splineglyph.h
#include "tables/cmap.h"
#include "tables/gdef.h"
#include "tables/glyf.h"
#include "tables/cff.h"
#include "editors/cffedit.h"
#include "tables/colr.h"
#include "tables/svg.h"
#include "tables/name.h"
#include "tables/maxp.h"
#include "tables/mtx.h"
#include "tables/glyphnames.h"
#include "editors/postedit.h"
#include "editors/glyphcontext.h"
#include "editors/glyphview.h"
#include "fs_undo.h"
#include "editors/gvundo.h"
#include "editors/unispinbox.h"

#include "fs_notify.h"
#include "icuwrapper.h"

FVLayout::FVLayout (QWidget *parent, int margin, int hSpacing, int vSpacing)
     : QLayout (parent), m_hSpace (hSpacing), m_vSpace (vSpacing) {

     setContentsMargins (margin, margin, margin, margin);
     setSizeConstraint (QLayout::SetMinAndMaxSize);
}

FVLayout::FVLayout (int margin, int hSpacing, int vSpacing)
     : m_hSpace (hSpacing), m_vSpace (vSpacing) {

     setContentsMargins (margin, margin, margin, margin);
}

FVLayout::~FVLayout () {
     QLayoutItem *item;
     while ((item = takeAt (0)))
         delete item;
}

void FVLayout::addItem (QLayoutItem *item) {
     itemList.append (item);
}

int FVLayout::horizontalSpacing () const {
     if (m_hSpace >= 0) {
         return m_hSpace;
     } else {
         return smartSpacing (QStyle::PM_LayoutHorizontalSpacing);
     }
}

int FVLayout::verticalSpacing () const {
     if (m_vSpace >= 0) {
         return m_vSpace;
     } else {
         return smartSpacing (QStyle::PM_LayoutVerticalSpacing);
     }
}

int FVLayout::count () const {
     return itemList.size ();
}

QLayoutItem *FVLayout::itemAt (int index) const {
     return itemList.value (index);
}

QLayoutItem *FVLayout::takeAt (int index) {
     if (index >= 0 && index < itemList.size ())
         return itemList.takeAt (index);
     else
         return 0;
}

Qt::Orientations FVLayout::expandingDirections () const {
     return Qt::Orientations ();
}

bool FVLayout::hasHeightForWidth() const {
     return true;
}

int FVLayout::heightForWidth(int width) const {
     int height = doLayout (QRect(0, 0, width, 0), true);
     return height;
}

void FVLayout::setGeometry (const QRect &rect) {
     QLayout::setGeometry (rect);
     doLayout (rect, false);
}

QSize FVLayout::sizeHint () const {
     return minimumSize ();
}

QSize FVLayout::minimumSize () const {
     QSize size;
     for (auto item: itemList)
         size = size.expandedTo (item->minimumSize ());

     size += QSize (2*margin (), 2*margin ());
     return size;
}

int FVLayout::doLayout (const QRect &rect, bool testOnly) const {
     int left, top, right, bottom;
     getContentsMargins (&left, &top, &right, &bottom);
     QRect effectiveRect = rect.adjusted (+left, +top, -right, -bottom);
     int x = effectiveRect.x ();
     int y = effectiveRect.y ();
     int lineHeight = 0;

     for (auto item: itemList) {
         QWidget *wid = item->widget ();
         int spaceX = horizontalSpacing ();
         if (spaceX == -1)
             spaceX = wid->style ()->layoutSpacing (
                 QSizePolicy::Frame, QSizePolicy::Frame, Qt::Horizontal);
         int spaceY = verticalSpacing ();
         if (spaceY == -1)
             spaceY = wid->style ()->layoutSpacing (
                 QSizePolicy::Frame, QSizePolicy::Frame, Qt::Vertical);
         int nextX = x + item->sizeHint().width() + spaceX;
         if (nextX - spaceX > effectiveRect.right() && lineHeight > 0) {
             x = effectiveRect.x ();
             y = y + lineHeight + spaceY;
             nextX = x + item->sizeHint ().width () + spaceX;
             lineHeight = 0;
         }

         if (!testOnly)
             item->setGeometry (QRect (QPoint (x, y), item->sizeHint ()));

         x = nextX;
         lineHeight = qMax (lineHeight, item->sizeHint ().height ());
     }
     return y + lineHeight - rect.y () + bottom;
}

int FVLayout::smartSpacing (QStyle::PixelMetric pm) const {
     QObject *parent = this->parent ();

     if (!parent) {
         return -1;
     } else if (parent->isWidgetType ()) {
         QWidget *pw = static_cast<QWidget *> (parent);
         return pw->style ()->pixelMetric (pm, 0, pw);
     } else {
         return static_cast<QLayout*> (parent)->spacing ();
     }
}

void FVLayout::setPixelSize (int size) {
    for (auto item: itemList) {
        GlyphBox *gb = qobject_cast<GlyphBox *> (item->widget ());
	gb->resizeCell (size);
    }
}

FontView::FontView (FontTable* tbl, sFont *fnt, QWidget *parent) :
    TableEdit (parent, Qt::Window), m_table (tbl), m_font (fnt),
    m_gnp (GlyphNameProvider (*fnt)),
    m_post_changed (false), m_cmap_changed (false),
    m_gcount_changed (false), m_gdef_changed (false),
    m_paletteIdx (0), m_gv (nullptr) {

    setAttribute (Qt::WA_DeleteOnClose);
    m_gc_table = nullptr; m_colr = nullptr; m_cpal = nullptr;
    m_glyf_table = m_cff_table = m_svg_table = nullptr;
    m_ug_container = new UndoGroupContainer (this);

    QSettings settings (QCoreApplication::organizationName (), QCoreApplication::applicationName ());
    m_edited = m_valid = false;
    m_cell_size = settings.value ("fontview/cellSize", 72).toInt ();
    m_h_mult = settings.value ("fontview/horzFactor", 16).toInt ();
    m_v_mult = settings.value ("fontview/vertFactor", 16).toInt ();
    m_current_cell = nullptr;

    m_outlines_init  = 0;
    m_outlines_avail = 0;
    m_content_type = 0;

    loadTables (m_table ? m_table->iName () : 0);
    if (!m_gc_table) {
        QMessageBox::critical (this,
            tr ("No glyph data"),
            tr ("Error: this font doesn't contain 'glyf', 'CFF ', 'CFF2' or 'SVG ' tables "
                "(or they are so badly corrupted that I can't use them)."));
        return;
    } else if (m_colr && !m_cpal) {
        QMessageBox::critical (this,
            tr ("No CPAL table"),
            tr ("Error: this font doesn't contain a 'CPAL' table, "
                "which is necessary do display colored glyphs."));
        return;
    }

    switch (m_gc_table->iName ()) {
      case CHR('g','l','y','f'):
        m_content_type = (uint8_t) OutlinesType::TT;
      break;
      case CHR('C','F','F',' '):
      case CHR('C','F','F','2'):
        m_content_type = (uint8_t) OutlinesType::PS;
      break;
      case CHR('S','V','G',' '):
        m_content_type = (uint8_t) OutlinesType::SVG;
      break;
      default:
        ;
    }
    if (m_table->iName () == CHR('C','O','L','R'))
        m_content_type |= (uint8_t) OutlinesType::COLR;

    if (!loadGlyphs ())
        return;
    addColorData ();
    m_layout = new FVLayout (0, 0, 0);

    m_scroll = new QScrollArea (this);
    m_scroll->installEventFilter (this);
    m_scroll->setWidgetResizable (true);
    m_scroll->setStyleSheet ("QScrollArea {margin: 0; padding: 2; border: 0}");

    setMinimumSize (actualWidth (1), actualHeight (1));
    setBaseSize (actualWidth (1), actualHeight (1));
    resize (actualWidth (m_h_mult), actualHeight (m_v_mult));
    setSizeIncrement (m_cell_size+4, m_cell_size+26);

    setCentralWidget (m_scroll);

    setStatusBar ();
    setMenuBar ();
    setToolBar ();

    setWindowTitle (tr ("Glyph Set - ").append (m_font->fontname));

    prepareGlyphCells ();
    displayEncodedGlyphs (m_font->enc, false);
    m_valid = true;
}

FontView::~FontView () {}

void FontView::setStatusBar () {
    QStatusBar *sb = this->statusBar ();
    QFontMetrics hexmetr = sb->fontMetrics ();
    QString line;

    m_sb_gid_lbl = new QLabel (this);
    m_sb_gid_lbl->setAlignment (Qt::AlignVCenter | Qt::AlignLeft);
    m_sb_gid_lbl->setFrameStyle (QFrame::Panel | QFrame::Sunken);
    line = QString ("GID: 000000 (0x0000)");
    m_sb_gid_lbl->setFixedWidth (hexmetr.boundingRect (line).width ());
    sb->addWidget (m_sb_gid_lbl);

    m_sb_enc_lbl = new QLabel (this);
    m_sb_enc_lbl->setAlignment (Qt::AlignVCenter | Qt::AlignLeft);
    m_sb_enc_lbl->setFrameStyle (QFrame::Panel | QFrame::Sunken);
    line = QString ("0x0000");
    m_sb_enc_lbl->setFixedWidth (hexmetr.boundingRect (line).width ());
    sb->addWidget (m_sb_enc_lbl);

    m_sb_name_lbl = new QLabel (this);
    m_sb_name_lbl->setAlignment (Qt::AlignVCenter | Qt::AlignLeft);
    m_sb_name_lbl->setFrameStyle (QFrame::Panel | QFrame::Sunken);
    line = QString ("upsilondieresistonos");
    m_sb_name_lbl->setFixedWidth (hexmetr.boundingRect (line).width ());
    if (!m_gnp.fontHasGlyphNames ()) {
        QPalette pal;
        pal.setColor (QPalette::WindowText, QColor (0x55, 0x55, 0x55, 0xFF));
        m_sb_name_lbl->setPalette (pal);
    }
    sb->addWidget (m_sb_name_lbl);

    m_sb_uni_lbl = new QLabel (this);
    m_sb_uni_lbl->setAlignment (Qt::AlignVCenter | Qt::AlignLeft);
    m_sb_uni_lbl->setFrameStyle (QFrame::Panel | QFrame::Sunken);
    line = QString ("U+0000000");
    m_sb_uni_lbl->setFixedWidth (hexmetr.boundingRect (line).width ());
    sb->addWidget (m_sb_uni_lbl);

    m_sb_uniname_lbl = new QLabel (this);
    m_sb_uniname_lbl->setAlignment (Qt::AlignVCenter | Qt::AlignLeft);
    m_sb_uniname_lbl->setFrameStyle (QFrame::Panel | QFrame::Sunken);
    sb->addWidget (m_sb_uniname_lbl);
}

void FontView::setMenuBar () {
    QMenuBar *mb = this->menuBar ();
    QMenu *fileMenu, *editMenu, *elementMenu, *hintMenu, *viewMenu;

    cffAction = new QAction (tr ("CFF &properties..."), this);
    saveAction = new QAction (tr ("&Compile tables"), this);
    closeAction = new QAction (tr ("C&lose"), this);

    cffAction->setEnabled (m_content_type & (uint8_t) OutlinesType::PS);
    connect (cffAction, &QAction::triggered, this, &FontView::editCFF);
    connect (saveAction, &QAction::triggered, this, &FontView::save);
    connect (closeAction, &QAction::triggered, this, &FontView::close);

    saveAction->setShortcut (QKeySequence::Save);
    closeAction->setShortcut (QKeySequence::Close);

    undoAction = m_ug_container->createUndoAction (this, tr ("&Undo"));
    redoAction = m_ug_container->createRedoAction (this, tr ("Re&do"));
    disconnect (undoAction, &QAction::triggered, m_ug_container, &UndoGroupContainer::undo);
    disconnect (redoAction, &QAction::triggered, m_ug_container, &UndoGroupContainer::redo);

    undoAction->setShortcut (QKeySequence::Undo);
    redoAction->setShortcut (QKeySequence::Redo);

    cutAction = new QAction (tr ("C&ut"), this);
    copyAction = new QAction (tr ("&Copy"), this);
    copyRefAction = new QAction (tr ("Copy &Reference"), this);
    svgCopyAction = new QAction (tr ("Copy S&VG as text"), this);
    pasteAction = new QAction (tr ("&Paste"), this);
    pasteIntoAction = new QAction (tr ("Paste &Into"), this);
    clearAction = new QAction (tr ("&Delete"), this);
    unselectAction = new QAction (tr ("Clear &selection"), this);
    selectAllAction = new QAction (tr ("Se&lect all"), this);
    editAction = new QAction (tr ("&Edit glyph..."), this);
    addGlyphAction = new QAction (tr ("&Add glyph..."), this);
    clearSvgGlyphAction = new QAction (tr ("Clear SVG &glyph"), this);
    clearSvgGlyphAction->setVisible (m_content_type & (uint8_t) OutlinesType::SVG);

    cutAction->setShortcut (QKeySequence::Cut);
    copyAction->setShortcut (QKeySequence::Copy);
    copyRefAction->setShortcut (QKeySequence (Qt::CTRL + Qt::Key_G));
    pasteAction->setShortcut (QKeySequence::Paste);
    pasteIntoAction->setShortcut (QKeySequence (Qt::CTRL + Qt::SHIFT + Qt::Key_V));
    clearAction->setShortcut (QKeySequence (Qt::Key_Delete));
    unselectAction->setShortcut (QKeySequence (Qt::Key_Escape));
    selectAllAction->setShortcut (QKeySequence (Qt::CTRL + Qt::Key_A));

    connect (undoAction, &QAction::triggered, this, &FontView::undo);
    connect (redoAction, &QAction::triggered, this, &FontView::redo);
    connect (clearAction, &QAction::triggered, this, &FontView::clear);
    connect (cutAction, &QAction::triggered, this, &FontView::cut);
    connect (copyAction, &QAction::triggered, this, &FontView::copy);
    connect (copyRefAction, &QAction::triggered, this, &FontView::copyRef);
    connect (svgCopyAction, &QAction::triggered, this, &FontView::svgCopy);
    connect (pasteAction, &QAction::triggered, this, &FontView::paste);
    connect (pasteIntoAction, &QAction::triggered, this, &FontView::pasteInto);
    connect (unselectAction, &QAction::triggered, this, &FontView::clearSelection);
    connect (selectAllAction, &QAction::triggered, this, &FontView::selectAllGlyphs);
    connect (editAction, &QAction::triggered, this, &FontView::glyphEditCurrent);
    connect (addGlyphAction, &QAction::triggered, this, &FontView::addGlyph);
    connect (clearSvgGlyphAction, &QAction::triggered, this, &FontView::clearSvgGlyph);

    addExtremaAction = new QAction (tr ("Add e&xtrema"), this);
    simplifyAction = new QAction (tr ("&Simplify"), this);
    roundAction = new QAction (tr ("Round to &integer"), this);
    overlapAction = new QAction (tr ("Remove &overlap"), this);
    overlapAction->setVisible (false);
    corrDirAction = new QAction (tr ("Correct &direction"), this);
    unlinkAction = new QAction (tr ("&Unlink references"), this);

    addExtremaAction->setShortcut (QKeySequence (Qt::CTRL + Qt::SHIFT + Qt::Key_X));
    simplifyAction->setShortcut (QKeySequence (Qt::CTRL + Qt::SHIFT + Qt::Key_M));
    roundAction->setShortcut (QKeySequence (Qt::CTRL + Qt::SHIFT + Qt::Key_Underscore));
    overlapAction->setShortcut (QKeySequence (Qt::CTRL + Qt::SHIFT + Qt::Key_O));
    corrDirAction->setShortcut (QKeySequence (Qt::CTRL + Qt::SHIFT + Qt::Key_D));
    unlinkAction->setShortcut (QKeySequence (Qt::CTRL + Qt::Key_U));

    connect (addExtremaAction, &QAction::triggered, this, &FontView::addExtrema);
    connect (simplifyAction, &QAction::triggered, this, &FontView::simplify);
    connect (roundAction, &QAction::triggered, this, &FontView::roundToInt);
    connect (overlapAction, &QAction::triggered, this, &FontView::removeOverlap);
    connect (corrDirAction, &QAction::triggered, this, &FontView::correctDirection);
    connect (unlinkAction, &QAction::triggered, this, &FontView::unlinkRefs);

    autoHintAction = new QAction (tr ("Autohint"), this);
    clearHintsAction = new QAction (tr ("Clear hints"), this);
    autoHintAction->setShortcut (QKeySequence (Qt::CTRL + Qt::SHIFT + Qt::Key_H));

    connect (autoHintAction, &QAction::triggered, this, &FontView::autoHint);
    connect (clearHintsAction, &QAction::triggered, this, &FontView::clearHints);

    autoHintAction->setEnabled (m_content_type & (uint8_t) OutlinesType::PS);
    clearHintsAction->setEnabled (m_content_type & (uint8_t) OutlinesType::PS);

    checkSelection ();
    connect (QApplication::clipboard (), &QClipboard::dataChanged, this, &FontView::checkSelection);

    ttSwitchAction = new QAction (tr ("Show TrueType Outlines"), this);
    psSwitchAction = new QAction (tr ("Show PostScript Outlines"), this);
    svgSwitchAction = new QAction (tr ("Show SVG Outlines"), this);
    colrSwitchAction = new QAction (tr ("Show Colored Outlines"), this);

    ttSwitchAction->setData (QVariant (static_cast<uint> (OutlinesType::TT)));
    ttSwitchAction->setCheckable (true);
    ttSwitchAction->setEnabled (m_outlines_avail & (uint8_t) OutlinesType::TT);
    psSwitchAction->setData (QVariant (static_cast<uint> (OutlinesType::PS)));
    psSwitchAction->setCheckable (true);
    psSwitchAction->setEnabled (m_outlines_avail & (uint8_t) OutlinesType::PS);
    svgSwitchAction->setData (QVariant (static_cast<uint> (OutlinesType::SVG)));
    svgSwitchAction->setCheckable (true);
    svgSwitchAction->setEnabled (m_outlines_avail & (uint8_t) OutlinesType::SVG);
    colrSwitchAction->setData (QVariant (static_cast<uint> (OutlinesType::COLR)));
    colrSwitchAction->setCheckable (true);
    colrSwitchAction->setEnabled (m_outlines_avail & (uint8_t) OutlinesType::COLR);

    m_switchOutlineActions = new QActionGroup (this);
    m_switchOutlineActions->addAction (ttSwitchAction);
    m_switchOutlineActions->addAction (psSwitchAction);
    m_switchOutlineActions->addAction (svgSwitchAction);
    m_switchOutlineActions->addAction (colrSwitchAction);

    connect (m_switchOutlineActions, &QActionGroup::triggered, this, &FontView::switchOutlinesByAction);
    if (m_content_type & (uint8_t) OutlinesType::COLR)
        colrSwitchAction->setChecked (true);
    else if (m_content_type & (uint8_t) OutlinesType::TT)
        ttSwitchAction->setChecked (true);
    else if (m_content_type & (uint8_t) OutlinesType::PS)
        psSwitchAction->setChecked (true);
    else if (m_content_type & (uint8_t) OutlinesType::SVG)
        svgSwitchAction->setChecked (true);

    view8x2Action =  new QAction (tr ("8x2 cell window"), this);
    view16x4Action = new QAction (tr ("16x4 cell window"), this);
    view16x8Action = new QAction (tr ("16x8 cell window"), this);
    view32x8Action = new QAction (tr ("32x8 cell window"), this);

    connect (view8x2Action, &QAction::triggered, this, &FontView::resize8x2);
    connect (view16x4Action, &QAction::triggered, this, &FontView::resize16x4);
    connect (view16x8Action, &QAction::triggered, this, &FontView::resize16x8);
    connect (view32x8Action, &QAction::triggered, this, &FontView::resize32x8);

    cell36Action = new QAction (tr ("36 pixel outline"), this);
    cell48Action = new QAction (tr ("48 pixel outline"), this);
    cell72Action = new QAction (tr ("72 pixel outline"), this);
    cell96Action = new QAction (tr ("96 pixel outline"), this);
    cell128Action = new QAction (tr ("128 pixel outline"), this);

    m_cellSizeActions = new QActionGroup (this);
    m_cellSizeActions->addAction (cell36Action);
    m_cellSizeActions->addAction (cell48Action);
    m_cellSizeActions->addAction (cell72Action);
    m_cellSizeActions->addAction (cell96Action);
    m_cellSizeActions->addAction (cell128Action);

    cell36Action->setData (QVariant (36));
    cell36Action->setCheckable (true);
    cell36Action->setChecked (m_cell_size == 36);
    cell48Action->setData (QVariant (48));
    cell48Action->setCheckable (true);
    cell48Action->setChecked (m_cell_size == 48);
    cell72Action->setData (QVariant (72));
    cell72Action->setCheckable (true);
    cell72Action->setChecked (m_cell_size == 72);
    cell96Action->setData (QVariant (96));
    cell96Action->setCheckable (true);
    cell96Action->setChecked (m_cell_size == 96);
    cell128Action->setData (QVariant (128));
    cell128Action->setCheckable (true);
    cell128Action->setChecked (m_cell_size == 128);

    connect (m_cellSizeActions, &QActionGroup::triggered, this, &FontView::resizeCells);

    fileMenu = mb->addMenu (tr ("&File"));
    fileMenu->addAction (cffAction);
    fileMenu->addSeparator ();
    fileMenu->addAction (saveAction);
    fileMenu->addAction (closeAction);

    editMenu = mb->addMenu (tr ("&Edit"));
    editMenu->addAction (undoAction);
    editMenu->addAction (redoAction);
    editMenu->addSeparator ();
    editMenu->addAction (cutAction);
    editMenu->addAction (copyAction);
    editMenu->addAction (copyRefAction);
    editMenu->addAction (svgCopyAction);
    editMenu->addAction (pasteAction);
    editMenu->addAction (pasteIntoAction);
    editMenu->addAction (clearAction);
    editMenu->addSeparator ();
    editMenu->addAction (selectAllAction);
    editMenu->addAction (unselectAction);
    editMenu->addSeparator ();
    editMenu->addAction (editAction);
    editMenu->addAction (addGlyphAction);
    editMenu->addAction (clearSvgGlyphAction);
    connect (editMenu, &QMenu::aboutToShow, this, &FontView::checkSelection);

    elementMenu = mb->addMenu (tr ("&Elements"));
    elementMenu->addAction (addExtremaAction);
    elementMenu->addAction (simplifyAction);
    elementMenu->addAction (roundAction);
    elementMenu->addAction (overlapAction);
    elementMenu->addAction (corrDirAction);
    elementMenu->addSeparator ();
    elementMenu->addAction (unlinkAction);

    hintMenu = mb->addMenu (tr ("&Hints"));
    hintMenu->addAction (autoHintAction);
    hintMenu->addAction (clearHintsAction);
    connect (hintMenu, &QMenu::aboutToShow, this, &FontView::checkSelection);

    viewMenu = mb->addMenu (tr ("&View"));
    viewMenu->addAction (ttSwitchAction);
    viewMenu->addAction (psSwitchAction);
    viewMenu->addAction (svgSwitchAction);
    viewMenu->addAction (colrSwitchAction);
    viewMenu->addSeparator ();
    viewMenu->addAction (view8x2Action);
    viewMenu->addAction (view16x4Action);
    viewMenu->addAction (view16x8Action);
    viewMenu->addAction (view32x8Action);
    viewMenu->addSeparator ();
    viewMenu->addAction (cell36Action);
    viewMenu->addAction (cell48Action);
    viewMenu->addAction (cell72Action);
    viewMenu->addAction (cell96Action);
    viewMenu->addAction (cell128Action);
}

void FontView::setToolBar () {
    QToolBar *tb = new QToolBar ();
    tb->setStyleSheet ("QToolBar {spacing: 6px; padding: 6px}");
    tb->setMovable (false);
    tb->addWidget (new QLabel (tr ("Order glyphs by:")));
    m_orderBox = new QComboBox ();
    setOrderList ();
    tb->addWidget (m_orderBox);
    addToolBar (Qt::TopToolBarArea, tb);

    m_palLabelAction = tb->addWidget (new QLabel (tr ("Color palette:")));
    m_palLabelAction->setVisible (m_content_type & (uint8_t) OutlinesType::COLR);
    m_paletteBox = new QComboBox ();
    if (m_cpal) {
	NameTable *name = dynamic_cast<NameTable *> (m_font->table (CHR ('n','a','m','e')));
	m_paletteBox->addItems (m_cpal->paletteList (name));
	m_paletteBox->setCurrentIndex (0);
    }
    connect (m_paletteBox, static_cast<void (QComboBox::*)(int)> (&QComboBox::currentIndexChanged),
	this, &FontView::switchPalette);
    m_palBoxAction = tb->addWidget (m_paletteBox);
    m_palBoxAction->setVisible (m_content_type & (uint8_t) OutlinesType::COLR);
    addToolBar (Qt::TopToolBarArea, tb);
}

void FontView::setOrderList () {
    m_orderBox->addItem (tr ("Glyph ID"), -1);
    m_orderBox->setCurrentIndex (0);
    CmapTable *cmap = dynamic_cast<CmapTable *> (m_font->table (CHR ('c','m','a','p')));
    if (!cmap)
	return;
    for (uint16_t i=0; i<cmap->numSubTables (); i++) {
	CmapEnc *enc = cmap->getSubTable (i);
	// Variation sequences are irrelevant in our context
	if (enc->format () != 14) {
	    std::string enc_name = enc->stringName ();
	    m_orderBox->addItem (QString::fromStdString (enc_name), i);
	}
    }
    connect (m_orderBox, static_cast<void (QComboBox::*)(int)> (&QComboBox::currentIndexChanged),
	this, &FontView::changeGlyphOrder);
}

void FontView::changeGlyphOrder (int idx) {
    QVariant cur = m_orderBox->itemData (idx);
    int cur_idx = cur.toInt ();
    if (cur_idx < 0) {
	displayEncodedGlyphs (m_font->enc, false);
    } else {
	CmapTable *cmap = dynamic_cast<CmapTable *> (m_font->table (CHR ('c','m','a','p')));
	if (cmap && cur_idx < cmap->numSubTables ()) {
	    CmapEnc *enc = cmap->getSubTable (cur_idx);
	    displayEncodedGlyphs (enc, true);
	}
    }
}

void FontView::switchPalette (int idx) {
    m_paletteIdx = idx;
    addColorData ();
    switchGlyphOutlines ();
    resetGlyphs (false);
}

void FontView::updateGlyphNames () {
    for (auto &gc: m_glyphs)
	gc.setName (m_gnp.nameByGid (gc.gid ()));
}

void FontView::contextMenuEvent (QContextMenuEvent *event) {
    QMenu menu (this);
    connect (&menu, &QMenu::aboutToShow, this, &FontView::checkSelection);

    menu.addAction (cutAction);
    menu.addAction (copyAction);
    menu.addAction (copyRefAction);
    menu.addAction (svgCopyAction);
    menu.addAction (pasteAction);
    menu.addAction (clearAction);
    menu.addSeparator ();
    menu.addAction (unlinkAction);
    menu.addSeparator ();
    menu.addAction (editAction);
    menu.addAction (clearSvgGlyphAction);

    menu.exec (event->globalPos());
}

void FontView::prepareGlyphCells () {
    QWidget *window = new QWidget (this);
    window->setLayout (m_layout);
    m_scroll->setWidget (window);

    m_cells.reserve (m_font->glyph_cnt);
    QProgressDialog progress (tr ("Preparing glyph cells..."), tr ("Abort"), 0, m_font->glyph_cnt, this);
    progress.setWindowModality (Qt::WindowModal);
    progress.show ();

    for (uint16_t i=0; i<m_font->glyph_cnt; i++) {
        GlyphBox *gb = new GlyphBox (window, i, m_cell_size);
        connect (gb, &GlyphBox::selected, this, &FontView::glyphSelected);
        connect (gb, &GlyphBox::editRequest, this, &FontView::glyphEdit);

        m_layout->addWidget (gb);
        m_cells.push_back (gb);

        qApp->instance ()->processEvents ();
        if (progress.wasCanceled ())
            return;
        progress.setValue (i);
    }
    progress.setValue (m_font->glyph_cnt);
}

void FontView::displayEncodedGlyphs (CmapEnc *enc, bool by_enc) {
    uint32_t num_glyphs = (by_enc) ? enc->count () : m_font->glyph_cnt;
    std::vector<uint32_t> unencoded;
    if (enc && by_enc)
        unencoded = enc->unencoded (m_font->glyph_cnt);
    uint32_t num_glyphs_full = num_glyphs + unencoded.size ();
    uint32_t max_num = num_glyphs_full >= m_cells.size () ? num_glyphs_full : m_cells.size ();

    m_cells.reserve (num_glyphs_full);
    centralWidget ()->setUpdatesEnabled (false);
    QProgressDialog progress (tr ("Displaying glyph images..."), tr ("Abort"), 0, max_num, this);
    progress.setCancelButton (nullptr);
    progress.setWindowModality (Qt::WindowModal);
    progress.show ();

    for (uint32_t i=0; i<num_glyphs_full; i++) {
        int64_t uni = -1;
        int gid = 0;
        GlyphBox *gb;
	if (i<m_cells.size ()) {
	    gb = m_cells[i];
	} else {
	    gb = new GlyphBox (nullptr, i, m_cell_size);
	    connect (gb, &GlyphBox::selected, this, &FontView::glyphSelected);
	    connect (gb, &GlyphBox::editRequest, this, &FontView::glyphEdit);
	    m_layout->addWidget (gb);
	    m_cells.push_back (gb);
	}

        if (i<num_glyphs) {
            if (enc) {
		if (by_enc)
		    uni = enc->unicodeByPos (i);
		else {
		    std::vector<uint32_t> unis = enc->unicode (i);
		    if (unis.size () > 0) uni = unis[0];
		}
                gid = by_enc ? enc->gidByUnicode (uni) : i;
            } else {
                gid = i;
		uni = -1;
            }
        } else {
            gid = unencoded[i - num_glyphs];
	    uni = -1;
	}
        assert ((uint16_t) gid < m_glyphs.size ());

	gb->attachGlyph (&m_glyphs[gid], uni);
        gb->select (false);
        gb->setClean (m_glyphs[gid].undoGroup ()->isClean ());

        if (progress.wasCanceled ())
            return;
        progress.setValue (i);
    }

    if (num_glyphs_full < m_cells.size ()) {
	uint32_t progress_cnt = num_glyphs_full;
	for (uint32_t i=m_cells.size ()-1; i>=num_glyphs_full; i--) {
	    GlyphBox *gb = m_cells[i];
	    m_layout->removeWidget (gb);
	    m_cells.erase (m_cells.begin () + i);
	    delete gb;

	    if (progress.wasCanceled ())
		return;
	    progress.setValue (progress_cnt++);
	}
    }
    progress.setValue (max_num);
    centralWidget ()->setUpdatesEnabled (true);
}

void FontView::addGlyph () {
    AddGlyphDialog dlg (m_font->enc, m_gc_table, this);
    switch (dlg.exec ()) {
      case QDialog::Accepted:
	break;
      case QDialog::Rejected:
	return;
    }
    int64_t uni = dlg.unicode ();
    std::string gname = dlg.glyphName ();
    uint8_t subf = dlg.subFont ();
    uint16_t gid = m_font->glyph_cnt;
    m_font->glyph_cnt++;

    GlyphBox *gb = new GlyphBox (nullptr, gid, m_cell_size);
    gb->setClean (false);

    m_gcount_changed = true;
    if (uni >= 0) {
	CmapTable *cmap = dynamic_cast<CmapTable *> (m_font->table (CHR ('c','m','a','p')));
	if (cmap) cmap->addCommonMapping (uni, gid);
	m_cmap_changed = true;
    }
    if (!gname.empty ()) {
	m_gnp.setGlyphName (gid, gname);
	if (m_gnp.glyphNameSource () == CHR ('p','o','s','t'))
	    m_post_changed = true;
    }

    m_glyphs.emplace_back (gid, m_gnp, m_glyphs);
    GlyphContext &gctx = m_glyphs[gid];

    if (m_glyf_table) {
        m_glyf_table->addGlyph (m_font);
	ConicGlyph *g = m_glyf_table->glyph (m_font, gid);
	g->setModified (true);
        gctx.setGlyph ((uint8_t) OutlinesType::TT, g);
    }
    if (m_cff_table) {
        m_cff_table->addGlyph (m_font, subf);
	ConicGlyph *g = m_cff_table->glyph (m_font, gid);
	g->setModified (true);
        gctx.setGlyph ((uint8_t) OutlinesType::PS, g);
    }
    // No glyph for SVG by default, even if the table is available
    // and displayed in fontview
    gctx.switchOutlinesType (m_content_type, false);

    connect (gb, &GlyphBox::selected, this, &FontView::glyphSelected);
    connect (gb, &GlyphBox::editRequest, this, &FontView::glyphEdit);

    m_layout->addWidget (gb);
    m_cells.push_back (gb);
    gb->attachGlyph (&gctx, uni);
    gb->select (true);
}

void FontView::clearSvgGlyph () {
    if (!m_selected.size () || m_content_type != (uint8_t) OutlinesType::SVG)
        return;
    bool plural = m_selected.size () > 1;
    SvgTable *svgt = dynamic_cast<SvgTable *> (m_svg_table);
    QMessageBox::StandardButton ask;
    ask = QMessageBox::question (this,
        tr (plural ? "Clear SVG glyphs" : "Clear SVG glyph"),
        tr ("Are you sure to clear the selected SVG glyphs? "
            "This operation cannot be undone."),
        (QMessageBox::Yes|QMessageBox::No));
    if (ask != QMessageBox::Yes)
        return;

    for (uint32_t sel : m_selected) {
	GlyphBox *cell = m_cells[sel];
        int gid = cell->gid ();
	if (m_gv) {
	    int tab_idx = m_gv->glyphTabIndex (gid);
	    if (tab_idx >= 0)
		m_gv->closeGlyphTab (tab_idx);
	}
        GlyphContext &gctx = m_glyphs[gid];
	if (svgt->hasGlyph (sel)) {
	    gctx.clearSvgGlyph ();
	    svgt->clearGlyph (gid);
	    cell->setClean (true);
	}
    }
}

void FontView::resetGlyphs (bool do_resize) {
    uint32_t i;

    // NB: works much faster without a progress dialog and processing events
    centralWidget ()->setUpdatesEnabled (false);
    for (i=0; i<m_cells.size (); i++) {
        GlyphBox *gb = m_cells[i];
	if (do_resize)
	    gb->resizeCell (m_cell_size);
	else
	    gb->renderGlyph ();
    }
    centralWidget ()->setUpdatesEnabled (true);
}

void FontView::loadTables (uint32_t tag) {
    for (auto &tbl : m_font->tbls) {
	switch (tbl->iName ()) {
          case CHR('g','l','y','f'):
            m_glyf_table = dynamic_cast<GlyphContainer *> (tbl.get ());
            m_glyf_table->fillup ();
            m_glyf_table->unpackData (m_font);
            m_outlines_avail |= (uint8_t) OutlinesType::TT;
            if (!m_gc_table || tag == CHR('g','l','y','f'))
                m_gc_table = m_glyf_table;
	    break;
          case CHR('C','F','F',' '):
          case CHR('C','F','F','2'):
            m_cff_table = dynamic_cast<GlyphContainer *> (tbl.get ());
            m_cff_table->fillup ();
            m_cff_table->unpackData (m_font);
            m_outlines_avail |= (uint8_t) OutlinesType::PS;
            if (!m_gc_table || tag == CHR('C','F','F',' ') || tag == CHR('C','F','F','2'))
                m_gc_table = m_cff_table;
	    break;
          case CHR('S','V','G',' '):
            m_svg_table = dynamic_cast<GlyphContainer *> (tbl.get ());
            m_svg_table->fillup ();
            m_svg_table->unpackData (m_font);
            m_outlines_avail |= (uint8_t) OutlinesType::SVG;
            if (tag == CHR('S','V','G',' ') || (!m_gc_table && tag != CHR('C','O','L','R')))
                m_gc_table = m_svg_table;
	    break;
          case CHR('C','O','L','R'):
            m_colr = dynamic_cast<ColrTable *> (tbl.get ());
            m_colr->fillup ();
            m_colr->unpackData (m_font);
	    break;
          case CHR('C','P','A','L'):
            m_cpal = dynamic_cast<CpalTable *> (tbl.get ());
            m_cpal->fillup ();
            m_cpal->unpackData (m_font);
	    break;
	  case CHR('G','D','E','F'):
            GdefTable *gdef = dynamic_cast<GdefTable *> (tbl.get ());
            gdef->fillup ();
            gdef->unpackData (m_font);
	    break;
	}
    }
    if (m_colr && m_cpal)
        m_outlines_avail |= (uint8_t) OutlinesType::COLR;
}

QString FontView::normalColor = QString ("#F2F3F4");

QString FontView::selectedColor = QString ("#FFBF00");

void FontView::edited () {
    m_edited = true;
}

void FontView::save () {
    uint16_t gcnt = m_glyphs.size ();
    MaxpTable *maxp = dynamic_cast<MaxpTable*> (m_font->table (CHR ('m','a','x','p')));
    HmtxTable *hmtx = dynamic_cast<HmtxTable*> (m_font->table (CHR ('h','m','t','x')));
    CmapTable *cmap = dynamic_cast<CmapTable *> (m_font->table (CHR ('c','m','a','p')));
    PostTable *post = dynamic_cast<PostTable*> (m_font->table (CHR ('p','o','s','t')));

    if (m_gcount_changed) {
	if (maxp) {
	    maxp->setGlyphCount (gcnt);
	}
	if (hmtx) {
	    hmtx->setNumGlyphs (gcnt);
	}
    }

    if (cmap && m_cmap_changed) {
	cmap->packData ();
	m_cmap_changed = false;
    }

    if (post && m_post_changed) {
	post->packData ();
	TableEdit *ed = post->editor ();
	if (ed) {
	    PostEdit *pe = qobject_cast<PostEdit *> (ed);
	    if (pe) pe->resetData ();
	}
	m_post_changed = false;
    }

    // If we have added any glyphs while the contents of the SVG table was displayed
    // and then going to save the table, then the main glyph container table
    // (glyf or CFF/CFF2) should also be saved to match the new glyph count.
    // This means we have to ensure all its glyphs have already been loaded.
    // Note that the opposite is not necessary, as it is OK to have some glyphs
    // missing in the SVG table
    if (m_gcount_changed && m_gc_table == m_svg_table) {
	GlyphContainer *other_cnt = m_glyf_table ? m_glyf_table : m_cff_table;
	uint8_t other_type = (uint8_t) other_cnt->outlinesType ();

	QProgressDialog progress (tr ("Loading glyphs..."), tr ("Abort"), 0, gcnt, this);
	progress.setCancelButton (nullptr);
	progress.setWindowModality (Qt::WindowModal);
	progress.show ();

	for (size_t i=0; i<gcnt; i++) {
	    GlyphContext &gctx = m_glyphs[i];
	    if (!gctx.hasOutlinesType (other_type)) {
		ConicGlyph *g = other_cnt->glyph (m_font, i);
		gctx.setGlyph (other_type, g);
	    }
	    progress.setValue (i);
	}
	progress.setValue (gcnt);
	other_cnt->packData ();
    }

    m_gc_table->packData ();
    for (auto &gc : m_glyphs) {
	NonExclusiveUndoGroup *ug = gc.undoGroup ();
	ug->setClean (true);
    }

    // While compiling the CFF/glyf/SVG tables, glyph metrics may be stored
    // into the hmtx table. And maxp is always changed when TTF glyphs are
    // compiled. So these two tables should be compiled after the main
    // glyph container
    if (maxp && maxp->modified ()) {
	maxp->packData ();
    }
    if (hmtx && hmtx->modified ()) {
	hmtx->packData ();
    }
    m_gcount_changed = false;
    emit update (m_gc_table);
}

bool FontView::checkUpdate (bool can_cancel) {
    if (isModified ()) {
        QMessageBox::StandardButton ask;
        ask = QMessageBox::question (this,
            tr ("Unsaved Changes"),
            tr ("Some glyphs have been modified. "
                "Would you like to export the changes back into the font?"),
            can_cancel ?  (QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel) :
                          (QMessageBox::Yes|QMessageBox::No));
        if (ask == QMessageBox::Cancel) {
            return false;
        } else if (ask == QMessageBox::Yes) {
            save ();
        }
    }
    return true;
}

bool FontView::isValid () {
    return m_valid;
}

bool FontView::isModified () {
    bool ret = false;
    for (auto &gc : m_glyphs) {
	NonExclusiveUndoGroup *ug = gc.undoGroup ();
	if (!ug->isClean ()) {
	    ret = true;
	    break;
	}
    }
    return ret;
}

FontTable* FontView::table () {
    return m_table;
}

void FontView::setTable (FontTable* tbl) {
    if (m_table != tbl) {
        OutlinesType val;
        if (m_table)
	    m_table->setEditor (this);
        switch (tbl->iName ()) {
          case CHR('g','l','y','f'):
            val = OutlinesType::TT;
          break;
          case CHR('C','F','F',' '):
          case CHR('C','F','F','2'):
            val = OutlinesType::PS;
          break;
          case CHR('S','V','G',' '):
            val = OutlinesType::SVG;
          break;
          case CHR('C','O','L','R'):
            val = OutlinesType::COLR;
          break;
        }
        switchOutlines (val);
        m_table = tbl;
    }
}

void FontView::clearGV () {
    m_gv = nullptr;
}

#if 0
void FontView::close () {
    bool modified = false;
    for (GlyphContext &gctx: m_glyphs) {
        NonExclusiveUndoGroup *ugroup = gctx.undoGroup ();
        if (!ugroup->isClean ()) {
            modified = true;
            break;
        }
    }
    if (modified) {
        QMessageBox::StandardButton ask;
        ask = QMessageBox::question (this,
            tr ("Unsaved Changes"),
            tr ("Would you like to save changes?"),
            QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel);
        if (ask == QMessageBox::Cancel)
            return;
        else if (ask == QMessageBox::Yes)
            save ();
    }
    QMainWindow::close ();
}
#endif

void FontView::clear () {
    uint32_t i;

    for (i=0; i<m_selected.size (); i++) {
        uint32_t sel = m_selected[i];
        GlyphBox *cell = m_cells[sel];
        int gid = cell->gid ();
        GlyphContext &gctx = m_glyphs[gid];
        ConicGlyph *g = gctx.glyph (m_content_type);
        GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_glyphs[gid], m_content_type);
        ucmd->setText (tr ("Delete Glyph Data"));

        gctx.clearScene ();
        g->clear ();
        gctx.render (m_content_type, m_cell_size);
        gctx.drawGlyph (g);
        gctx.undoGroup ()->activeStack ()->push (ucmd);
        gctx.update (m_content_type);
    }
}

void FontView::copyCell (bool cut, bool as_ref) {
    if (!m_selected.size ())
        return;
    std::vector<uint32_t> sortsel (m_selected);
    std::sort (sortsel.begin (), sortsel.end ());
    uint32_t i;
    std::ostringstream oss;
    QList<QUrl> urls;

    oss << "<?xml version=\"1.0\" standalone=\"no\"?>\n";
    oss << "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\" >\n";
    oss << "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:fsh=\"http://www.fontsheferd.github.io/svg\">\n";

    for (i=0; i<sortsel.size (); i++) {
        uint32_t idx = sortsel[i];
        GlyphBox *cell = m_cells[idx];
        int gid = cell->gid ();
        GlyphContext &gctx = m_glyphs[gid];
        ConicGlyph *g = gctx.glyph (m_content_type);
	uint8_t opts = SVGOptions::doExtras | SVGOptions::doAppSpecific;
	if (as_ref) opts |= SVGOptions::asReference;
        std::string svg_str = g->toSVG (nullptr, opts);
        oss << svg_str;
        // URL scheme doesn't matter: currently used just to determine the number of glyphs
        // in the clipboard, as urls is the only standard QClipboard attribute which can
        // store a list of values
        urls.append (QUrl (QString ("#glyph%1").arg (gid)));

        if (cut) {
            GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_glyphs[gid], m_content_type);
            ucmd->setText (tr ("Cut Glyph Data"));

            gctx.clearScene ();
            g->clear ();
            gctx.render (m_content_type, m_cell_size);
            gctx.drawGlyph (g);
            gctx.undoGroup ()->activeStack ()->push (ucmd);
            gctx.update (m_content_type);
        }
    }
    oss << "</svg>\n";

    QClipboard *clipboard = QApplication::clipboard ();
    QMimeData *md = new QMimeData;
    md->setData ("image/svg+xml", oss.str ().c_str ());
    md->setUrls (urls);
    clipboard->setMimeData (md);
    //pasteAction->setEnabled (true);
    //pasteIntoAction->setEnabled (true);
}

void FontView::cut () {
    copyCell (true, false);
}

void FontView::copy () {
    copyCell (false, false);
}

void FontView::copyRef () {
    copyCell (false, true);
}

void FontView::pasteCell (BoostIn &buf, uint32_t cell_idx, uint32_t clipb_idx, bool replace) {
    GlyphBox *cell = m_cells[cell_idx];
    int gid = cell->gid ();
    GlyphContext &gctx = m_glyphs[gid];
    ConicGlyph *g = gctx.glyph (m_content_type);
    GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_glyphs[gid], m_content_type);
    ucmd->setText (tr ("Paste Glyph Data"));
    gctx.clearScene ();
    if (replace)
        g->clear ();
    buf.seekg (0);
    g->fromSVG (buf, clipb_idx);
    bool refs_ok = gctx.resolveRefs (m_content_type);
    if (refs_ok) {
        gctx.render (m_content_type, m_cell_size);
        gctx.drawGlyph (g);
        gctx.undoGroup ()->activeStack ()->push (ucmd);
        gctx.update (m_content_type);
    } else {
        ucmd->undoInvalid ();
        delete ucmd;
    }
}

void FontView::pasteRange (bool replace) {
    QClipboard *clipboard = QApplication::clipboard ();
    const QMimeData *md = clipboard->mimeData ();
    uint32_t i;
    if (!md->hasFormat ("image/svg+xml"))
        return;

    QByteArray svg_data = md->data ("image/svg+xml");
    BoostIn buf (svg_data.constData (), svg_data.size ());
    uint32_t num = md->hasUrls () ? md->urls ().size () : 1;
    if (m_selected.size () == 1) {
        uint32_t idx = m_selected[0];
        for (i=0; idx < m_cells.size () && i<num; i++) {
            pasteCell (buf, idx, i, replace);
            idx++;
        }
    } else {
        for (i=0; i<m_selected.size () && i<num; i++)
            pasteCell (buf, m_selected[i], i, replace);
    }
}

void FontView::paste () {
    pasteRange (true);
}

void FontView::pasteInto () {
    pasteRange (false);
}

void FontView::svgCopy () {
    if (!m_selected.size ())
        return;
    uint32_t sel = m_selected.back ();
    GlyphBox *cell = m_cells[sel];
    int gid = cell->gid ();
    ConicGlyph *g = m_glyphs[gid].glyph (m_content_type);
    std::string svg_str = g->toSVG ();
    QClipboard *clipboard = QApplication::clipboard ();
    QMimeData *md = new QMimeData;
    md->setData ("text/plain", svg_str.c_str ());
    clipboard->setMimeData (md);
}

void FontView::checkSelection () {
    bool has_sel = (m_selected.size () > 0);
    cutAction->setEnabled (has_sel);
    copyAction->setEnabled (has_sel);
    copyRefAction->setEnabled (has_sel);
    svgCopyAction->setEnabled (has_sel);
    clearAction->setEnabled (has_sel);
    unselectAction->setEnabled (has_sel);
    editAction->setEnabled (has_sel);
    clearSvgGlyphAction->setEnabled (has_sel);

    QClipboard *clipboard = QApplication::clipboard ();
    const QMimeData *md = clipboard->mimeData ();
    pasteAction->setEnabled (md->hasFormat ("image/svg+xml"));
    pasteIntoAction->setEnabled (md->hasFormat ("image/svg+xml"));

    addExtremaAction->setEnabled (has_sel);
    simplifyAction->setEnabled (has_sel);
    roundAction->setEnabled (has_sel);
    overlapAction->setEnabled (has_sel);
    corrDirAction->setEnabled (has_sel);
    unlinkAction->setEnabled (has_sel);

    if (m_content_type & (uint8_t) OutlinesType::PS) {
	autoHintAction->setEnabled (has_sel);
	clearHintsAction->setEnabled (has_sel);
    }
}

void FontView::clearSelection () {
    selectAllCells (false);
    cutAction->setEnabled (false);
    copyAction->setEnabled (false);
    copyRefAction->setEnabled (false);
    svgCopyAction->setEnabled (false);
    clearAction->setEnabled (false);
    unlinkAction->setEnabled (false);
    unselectAction->setEnabled (false);
    editAction->setEnabled (false);
    clearSvgGlyphAction->setEnabled (false);
}

void FontView::selectAllGlyphs () {
    selectAllCells (true);
    cutAction->setEnabled (true);
    copyAction->setEnabled (true);
    copyRefAction->setEnabled (true);
    svgCopyAction->setEnabled (true);
    clearAction->setEnabled (true);
    unlinkAction->setEnabled (true);
    unselectAction->setEnabled (true);
    editAction->setEnabled (true);
    clearSvgGlyphAction->setEnabled (true);
}

void FontView::unlinkRefs () {
    uint32_t i;

    for (i=0; i<m_selected.size (); i++) {
        uint32_t sel = m_selected[i];
        GlyphBox *cell = m_cells[sel];
        int gid = cell->gid ();
        GlyphContext &gctx = m_glyphs[gid];
        ConicGlyph *g = gctx.glyph (m_content_type);
        GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_glyphs[gid], m_content_type);
        ucmd->setText (tr ("Unlink References"));

        gctx.clearScene ();
        g->unlinkRefs (false);
	for (uint16_t refgid: g->refersTo ()) {
	    GlyphContext &depctx = m_glyphs[refgid];
	    depctx.removeDependent (gid);
	}
        gctx.render (m_content_type, m_cell_size);
        gctx.drawGlyph (g);
        gctx.undoGroup ()->activeStack ()->push (ucmd);
        gctx.update (m_content_type);
    }
}

int FontView::actualHeight (int factor) {
    static const int cell_header_height = 26;
    static const int scroll_padding = 6;
    return ((m_cell_size + cell_header_height)*factor +
        scroll_padding + menuBar ()->height () + statusBar ()->height ());
}

int FontView::actualWidth (int factor) {
    static const int scroll_padding = 6;
    static const int cell_padding = 4;
    int sb_w = qApp->style ()->pixelMetric (QStyle::PM_ScrollBarExtent); // 16 on my system
    return (scroll_padding + (m_cell_size+cell_padding)*factor + sb_w);
}

void FontView::resizeXY (int x_factor, int y_factor) {
    QSettings settings (QCoreApplication::organizationName (), QCoreApplication::applicationName ());
    m_h_mult = x_factor;
    m_v_mult = y_factor;
    settings.setValue ("fontview/horzFactor", m_h_mult);
    settings.setValue ("fontview/vertFactor", m_v_mult);

    resize (actualWidth (x_factor), actualHeight (y_factor));
}

void FontView::resize8x2 () {
    resizeXY (8, 2);
}

void FontView::resize16x4 () {
    resizeXY (16, 4);
}

void FontView::resize16x8 () {
    resizeXY (16, 8);
}

void FontView::resize32x8 () {
    resizeXY (32, 16);
}

void FontView::resizeCells (QAction *action) {
    m_cell_size = action->data ().toInt();
    QSettings settings (QCoreApplication::organizationName (), QCoreApplication::applicationName ());
    settings.setValue ("fontview/cellSize", m_cell_size);

    setMinimumSize (actualWidth (1), actualHeight (1));
    setBaseSize (actualWidth (1), actualHeight (1));
    resize (actualWidth (m_h_mult), actualHeight (m_v_mult));
    setSizeIncrement (m_cell_size+4, m_cell_size+26);

    if (!switchGlyphOutlines ())
        return;
    //resetGlyphs (true);
    m_layout->setPixelSize (m_cell_size);

    cell36Action->setChecked (m_cell_size == 36);
    cell48Action->setChecked (m_cell_size == 48);
    cell72Action->setChecked (m_cell_size == 72);
    cell96Action->setChecked (m_cell_size == 96);
    cell128Action->setChecked (m_cell_size == 128);
}

void FontView::glyphSelected (GlyphBox *gb, Qt::KeyboardModifiers flags, bool val) {
    if (flags & Qt::ShiftModifier && val) {
	selectToCell (gb->position ());
    } else if (flags & Qt::ControlModifier && val) {
	selectCell (gb, val);
	m_current_cell = gb;
    } else {
        selectAllCells (false);
	selectCell (gb, val);
	m_current_cell = val ? gb : nullptr;
    }
    checkSelection ();
}

void FontView::glyphEdit (GlyphBox *gb) {
    uint16_t gid = gb->gid ();
    ensureGlyphOutlinesLoaded (gid);

    if (!m_gv) {
        m_gv = new GlyphViewContainer (this, *m_font, m_gc_table);
        m_gv->show ();
        m_gv->addGlyph (m_glyphs[gid], m_content_type);

    } else if (m_gv->hasGlyph (gid)) {
        m_gv->raise ();
        m_gv->switchToGlyph (gid, m_content_type);

    } else {
        m_gv->raise ();
        m_gv->addGlyph (m_glyphs[gid], m_content_type);
    }
}

void FontView::glyphEditCurrent () {
    if (m_current_cell)
	glyphEdit (m_current_cell);
}

void FontView::editCFF () {
    if (m_content_type & (uint8_t) OutlinesType::PS) {
	CffTable *cff = dynamic_cast<CffTable *> (m_cff_table);
	CffDialog edit (m_font, cff, this);
	connect (&edit, &CffDialog::glyphNamesChanged, this, &FontView::updateGlyphNames);
	edit.exec ();
    }
}

void FontView::ensureGlyphOutlinesLoaded (uint16_t gid) {
    GlyphContext &gctx = m_glyphs[gid];
    ConicGlyph *g;
    std::string gname = gctx.name ().toStdString();

    if (m_glyf_table && !gctx.hasOutlinesType ((uint8_t) OutlinesType::TT)) {
        g = m_glyf_table->glyph (m_font, gid);
        gctx.setGlyph ((uint8_t) OutlinesType::TT, g);
    }
    if (m_cff_table && !gctx.hasOutlinesType ((uint8_t) OutlinesType::PS)) {
        g = m_cff_table->glyph (m_font, gid);
        gctx.setGlyph ((uint8_t) OutlinesType::PS, g);
    }
    if (m_svg_table && m_content_type == (uint8_t) OutlinesType::SVG &&
	!gctx.hasOutlinesType ((uint8_t) OutlinesType::SVG)) {
	SvgTable *svgt = dynamic_cast<SvgTable *> (m_svg_table);
	if (!svgt->hasGlyph (gid)) {
	    svgt->addGlyphAt (m_font, gid);
	    g = m_svg_table->glyph (m_font, gid);
	    g->setModified (true);
	    gctx.setGlyph ((uint8_t) OutlinesType::SVG, g);
	    gctx.switchOutlinesType ((uint8_t) OutlinesType::SVG);
	} else {
	    g = m_svg_table->glyph (m_font, gid);
	    gctx.setGlyph ((uint8_t) OutlinesType::SVG, g);
	}
    }
}

void FontView::switchOutlines (OutlinesType val) {
    switch (val) {
      case (OutlinesType::TT):
        m_content_type = (uint8_t) val;
        m_gc_table = m_glyf_table;
      break;
      case (OutlinesType::PS):
        m_content_type = (uint8_t) val;
        m_gc_table = m_cff_table;
      break;
      case (OutlinesType::SVG):
        m_content_type = (uint8_t) val;
        m_gc_table = m_svg_table;
      break;
      case (OutlinesType::COLR):
        if (m_content_type & (uint8_t) OutlinesType::SVG) {
            if (m_glyf_table) {
                m_content_type = (uint8_t) OutlinesType::TT;
                m_gc_table = m_glyf_table;
            } else if (m_cff_table) {
                m_content_type = (uint8_t) OutlinesType::PS;
                m_gc_table = m_cff_table;
            }
        } else
            m_content_type |= (uint8_t) val;
      break;
      default:
        ;
    }
    m_palLabelAction->setVisible (m_content_type & (uint8_t) OutlinesType::COLR);
    m_palBoxAction->setVisible (m_content_type & (uint8_t) OutlinesType::COLR);
    cffAction->setEnabled (m_content_type & (uint8_t) OutlinesType::PS);
    clearSvgGlyphAction->setVisible (m_content_type & (uint8_t) OutlinesType::SVG);
    if (!loadGlyphs ())
        return;
    if (!switchGlyphOutlines ())
        return;
    resetGlyphs (false);
}

void FontView::switchOutlinesByAction (QAction *action) {
    OutlinesType val = static_cast<OutlinesType> (action->data ().toUInt());
    switchOutlines (val);
}

void FontView::closeEvent (QCloseEvent *event) {
    // If we are going to delete the font, ignore changes in table edits
    if (checkUpdate (true)) {
        if (m_table)
	    m_table->clearEditor ();
    } else {
        event->ignore ();
        return;
    }

    static const int cell_header_height = 26;
    static const int scroll_padding = 6;
    static const int cell_padding = 4;
    QSettings settings (QCoreApplication::organizationName (), QCoreApplication::applicationName ());

    int sb_w = qApp->style ()->pixelMetric (QStyle::PM_ScrollBarExtent);
    int inrow = (width () - sb_w - scroll_padding)/(m_cell_size + cell_padding);
    int incol = (height () - menuBar ()->height () - statusBar ()->height ()/* - scroll_padding*/) /
                (m_cell_size + cell_header_height);
    settings.setValue ("fontview/horzFactor", inrow);
    settings.setValue ("fontview/vertFactor", incol);

    if (m_gv) {
        m_gv->close ();
        m_gv = nullptr;
    }

    TableEdit::closeEvent (event);
}

bool FontView::eventFilter (QObject *object, QEvent *event) {
    if (object == m_scroll && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (!(keyEvent->modifiers () & Qt::AltModifier) && (
            keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right ||
            keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_Down)) {

            keyPressEvent (keyEvent);
            return true;
        }
    }
    return false;
}

void FontView::selectAllCells (bool val) {
    uint32_t i;
    if (val) {
        for (i=0; i<m_cells.size (); i++) {
            GlyphBox *cell = m_cells[i];
            cell->select (true);
            m_selected.push_back (cell->position ());
        }
	m_current_cell = m_cells.back ();
    } else {
        for (i=0; i<m_selected.size (); i++) {
            uint32_t sel = m_selected[i];
            m_cells[sel]->select (false);
        }
        m_selected.clear ();
	m_current_cell = nullptr;
    }
    updateStatusBar (m_current_cell);
}

void FontView::updateStatusBar (GlyphBox *cell) {
    if (cell && m_font->enc) {
	int64_t uni = cell->unicode ();
	int gid = cell->gid ();
        QString name = m_glyphs[gid].name ();
        m_ug_container->setActiveGroup (m_glyphs[gid].undoGroup ());

        m_sb_gid_lbl->setText (QString ("GID: %1 (0x%2)").arg (gid).arg (gid, 4, 16, QLatin1Char ('0')));
        m_sb_name_lbl->setText (name);

        if (uni >= 0) {
            m_sb_uni_lbl->setText (QString ("U+%1").arg (uni, 4, 16, QLatin1Char ('0')));
	    m_sb_uniname_lbl->setText (QString::fromStdString (IcuWrapper::unicodeCharName (uni)));
        } else {
            m_sb_uni_lbl->setText (QString ("U+????"));
            m_sb_uniname_lbl->setText (QString (""));
        }

    } else {
        m_sb_gid_lbl->setText (QString ());
        m_sb_uni_lbl->setText (QString ());
        m_sb_name_lbl->setText (QString ());
        m_sb_uniname_lbl->setText (QString ());
        m_ug_container->setActiveGroup (nullptr);
    }
}

void FontView::selectCell (uint32_t idx, bool val) {
    int i;
    if (idx >= m_cells.size ())
        return;

    for (i=m_selected.size () - 1; i>=0; i--) {
        if (idx == m_selected[i]) {
            if (!val) m_selected.erase (m_selected.begin() + i);
            break;
        }
    }
    if (i < 0 && val)
        m_selected.push_back (idx);

    GlyphBox *cell = m_cells[idx];
    cell->select (val);
    if (m_selected.size () > 0) {
	updateStatusBar (cell);
	m_current_cell = cell;
    } else
	updateStatusBar (nullptr);
}

void FontView::selectCell (GlyphBox *cell, bool val) {
    int i;

    for (i=m_selected.size () - 1; i>=0; i--) {
        if (m_selected[i] == cell->position ()) {
            if (!val) m_selected.erase (m_selected.begin() + i);
            break;
        }
    }
    if (i < 0 && val)
        m_selected.push_back (cell->position ());

    cell->select (val);
    if (m_selected.size () > 0) {
	updateStatusBar (cell);
	m_current_cell = cell;
    } else
	updateStatusBar (nullptr);
}

void FontView::selectToCell (uint32_t idx) {
    if (idx >= m_cells.size ())
        return;
    else if (!m_current_cell) {
	selectCell (idx, true);
	m_current_cell = m_cells[idx];
        return;
    }

    uint32_t base_pos = m_current_cell->position ();
    uint32_t last = base_pos;
    if (base_pos < idx) {
	for (int i = m_selected.size ()-1; i>=0; i--) {
	    uint32_t sel_pos = m_selected[i];
	    if (sel_pos < base_pos)
		selectCell (sel_pos, false);
	    else if (sel_pos > last && sel_pos > idx)
		selectCell (sel_pos, false);
	    else if (sel_pos > last)
		last = sel_pos;
	}
	for (uint32_t i=last+1; i<=idx; i++)
	    selectCell (i, true);
    // NB: this <= is important, as without it the base cell gets unselected
    // when going upside
    } else if (idx <= base_pos) {
	for (int i = m_selected.size ()-1; i>=0; i--) {
	    uint32_t sel_pos = m_selected[i];
	    if (sel_pos > base_pos)
		selectCell (sel_pos, false);
	    else if (sel_pos < last && sel_pos < idx)
		selectCell (sel_pos, false);
	    else if (sel_pos < last)
		last = sel_pos;
	}
	for (uint32_t i=last-1; i>=idx; i--)
	    selectCell (i, true);
    }
}

void FontView::selectCellLR (bool left, bool expand) {
    GlyphBox *last_sel = m_selected.size () > 0 ? m_cells[m_selected.back ()] : nullptr;
    int target = left ? last_sel->position () - 1 : last_sel->position () + 1;
    uint32_t utarget = (uint32_t) target;

    if (expand) {
        if (target < 0 || utarget >= m_cells.size ())
            return;
        if ((last_sel->position () < utarget && utarget <= m_current_cell->position ()) ||
            (last_sel->position () > utarget && utarget >= m_current_cell->position ())) {
            selectCell (last_sel, false);
        } else if ((last_sel->position () < utarget && last_sel->position () >= m_current_cell->position ()) ||
            (last_sel->position () > utarget && last_sel->position () <= m_current_cell->position ())) {
            selectCell (target, true);
        }

    } else {
        selectAllCells (false);
        if (target < 0 || utarget >= m_cells.size ())
            target = last_sel->position ();
        selectCell (target, true);
    }
}

void FontView::selectCellTB (int inrow, bool top, bool expand) {
    GlyphBox *last_sel = m_selected.size () > 0 ? m_cells[m_selected.back ()] : nullptr;
    int target = top ? last_sel->position () - inrow : last_sel->position () + inrow;
    int incr = top? -1 : 1;
    uint32_t i;

    if (target < 0)
        target = 0;
    else if ((uint32_t) target >= m_cells.size ())
        target = m_cells.size () - 1;
    uint32_t utarget = (uint32_t) target;

    if (expand) {
        if (last_sel->position () == utarget)
            return;

        if ((last_sel->position () <= m_current_cell->position () && top) ||
            (last_sel->position () >= m_current_cell->position () && !top)) {
            for (i=last_sel->position () + incr; i!=utarget; i+=incr)
                selectCell (i, true);
            selectCell (target, true);
        } else if ((last_sel->position () < m_current_cell->position () && !top) ||
            (last_sel->position () > m_current_cell->position () && top)) {
            for (i=last_sel->position (); i!=m_current_cell->position () && i!=utarget; i+=incr)
                selectCell (i, false);
            if ((top && utarget < m_current_cell->position ()) || (!top && utarget > m_current_cell->position ())) {
                for (i=m_current_cell->position () + incr; i!=utarget; i+=incr)
                    selectCell (i, true);
                selectCell (target, true);
            }
        }
    } else {
        selectAllCells (false);
        selectCell (target, true);
    }
    m_scroll->ensureWidgetVisible (m_cells[target]);
}

void FontView::selectCellHE (bool home, bool expand) {
    int target = home ? 0 : m_cells.size () -1;
    int incr = home? -1 : 1;
    uint32_t i;
    uint32_t utarget = (uint32_t) target;
    GlyphBox *last_sel = m_selected.size () > 0 ? m_cells[m_selected.back ()] : nullptr;

    if (expand) {
        if (!last_sel || last_sel->position () == utarget)
            return;

        if ((last_sel->position () <= m_current_cell->position () && home) ||
            (last_sel->position () >= m_current_cell->position () && !home)) {
            for (i=last_sel->position () + incr; i!=utarget; i+=incr)
                selectCell (i, true);
            selectCell (target, true);
        } else if ((last_sel->position () < m_current_cell->position () && !home) ||
            (last_sel->position () > m_current_cell->position () && home)) {
            for (i=last_sel->position (); i!=m_current_cell->position (); i+=incr)
                selectCell (i, false);
            for (i=m_current_cell->position () + incr; i!=utarget; i+=incr)
                selectCell (i, true);
            selectCell (target, true);
        }
    } else {
        selectAllCells (false);
        selectCell (target, true);
    }
    m_scroll->ensureWidgetVisible (m_cells[target]);
}

void FontView::keyPressEvent (QKeyEvent * event) {
    if (m_selected.size () > 0) {
	int sb_w = qApp->style ()->pixelMetric (QStyle::PM_ScrollBarExtent);
	int inrow = (width () - sb_w - 6)/(m_cell_size + 4);
	bool expand = event->modifiers () & Qt::ShiftModifier;

	switch (event->key()) {
	  case Qt::Key_Left:
	    selectCellLR (true, expand);
	    break;
	  case Qt::Key_Right:
	    selectCellLR (false, expand);
	    break;
	  case Qt::Key_Up:
	    selectCellTB (inrow, true, expand);
	    break;
	  case Qt::Key_Down:
	    selectCellTB (inrow, false, expand);
	    break;
	  case Qt::Key_Home:
	    selectCellHE (true, expand);
	    break;
	  case Qt::Key_End:
	    selectCellHE (false, expand);
	    break;
	  default:
	;
	}
    }
}

void FontView::mouseMoveEvent (QMouseEvent* ev) {
    bool add = (ev->modifiers () & Qt::ShiftModifier);
    if (add) {
	const QPoint relative = m_scroll->mapFromGlobal (ev->globalPos ());
	auto item = m_scroll->childAt (relative);
	QString itype = item->metaObject ()->className ();
	if (itype == "GlyphBox") {
	    auto gb = qobject_cast<GlyphBox *> (item);
	    selectToCell (gb->position ());
	}
    }
}

bool FontView::loadGlyphs () {
    uint16_t i;
    ConicGlyph *g;
    std::vector<uint16_t> refs;
    bool needs_ctx_init = ((int) m_glyphs.size () < m_font->glyph_cnt);

    QProgressDialog progress (tr ("Loading glyphs..."), tr ("Abort"), 0, m_font->glyph_cnt, this);
    progress.setWindowModality (Qt::WindowModal);
    progress.show ();

    for (i=0; i<m_font->glyph_cnt; i++) {
        std::string gname;
        if (needs_ctx_init) {
            gname = m_gnp.nameByGid (i);
            m_glyphs.emplace_back (i, m_gnp, m_glyphs);
        }
        GlyphContext &gctx = m_glyphs[i];

        if (needs_ctx_init || !gctx.hasOutlinesType (m_content_type)) {
            g = m_gc_table->glyph (m_font, i);
            gctx.setGlyph (m_content_type, g);
        }
	gctx.setFontViewSize (m_cell_size);
        gctx.switchOutlinesType (m_content_type, false);

        if (needs_ctx_init)
            m_ug_container->addGroup (gctx.undoGroup ());

        qApp->instance ()->processEvents ();
        if (progress.wasCanceled ())
            return false;
        progress.setValue (i);
    }
    progress.setValue (m_font->glyph_cnt);
    if (m_outlines_init & m_content_type)
        return true;

    progress.setLabelText (tr ("Resolving references..."));
    progress.show ();

    for (i=0; i<m_font->glyph_cnt; i++) {
        g = m_glyphs[i].glyph (m_content_type);
        if (g) {
            m_glyphs[i].resolveRefs (m_content_type);
            qApp->instance ()->processEvents ();
        }
        if (progress.wasCanceled ())
            return false;
        progress.setValue (i);
    }
    progress.setValue (m_font->glyph_cnt);
    return true;
}

bool FontView::addColorData () {
    if ((m_outlines_avail & (uint8_t) OutlinesType::COLR) &&
        !(m_content_type & (uint8_t) OutlinesType::SVG)) {
	QProgressDialog progress (tr ("Resolving color layers..."), tr ("Abort"), 0, m_font->glyph_cnt, this);
	progress.setWindowModality (Qt::WindowModal);
	progress.setCancelButton (nullptr);
        progress.show ();

        for (uint16_t i=0; i<m_font->glyph_cnt; i++) {
            ConicGlyph *g = m_glyphs[i].glyph (m_content_type);
            if (g) {
                g->addColorData (m_colr, m_cpal, m_paletteIdx);
                std::vector<uint16_t> refs = g->layerIds ();
                for (uint16_t j=0; j<refs.size (); j++)
                    g->provideLayer (m_glyphs[refs[j]].glyph (m_content_type), j);
                qApp->instance ()->processEvents ();
            }
            progress.setValue (i);
        }
        progress.setValue (m_font->glyph_cnt);
        m_outlines_init |= (uint8_t) OutlinesType::COLR;
    }
    m_outlines_init |= m_content_type;
    return true;
}

bool FontView::switchGlyphOutlines () {
    uint16_t i, gcnt = m_glyphs.size ();

    for (i=0; i<gcnt; i++) {
	m_glyphs[i].setFontViewSize (m_cell_size);
        m_glyphs[i].switchOutlinesType (m_content_type, false);
    }
    return true;
}

#if 0
bool FontView::ftRenderGlyphs () {
    unsigned int i, j;

    if (!FTWrapper::hasContext ()) FTWrapper::init ();

    if (!FTWrapper::hasContext ()) {
        QMessageBox::critical (this,
            tr ("Cannot rasterize font"),
            tr ("Error: rasterization failed. "
                "Please check if freetype is available and properly installed."));
        return false;
    }

    std::vector<struct freetype_raster> bitmaps = FTWrapper::getRaster (
        m_font->container->path ().toStdString().c_str (), m_font->index, m_cell_size);
    if (bitmaps.size () == 0) {
        QMessageBox::critical (this,
            tr ("Cannot rasterize font"),
            tr ("Error: rasterization failed. "
                "Please check if freetype is available and properly installed."));
        return false;
    }

    QProgressDialog progress (tr ("Rasterizing glyphs..."), tr ("Abort"), 0, bitmaps.size (), this);
    progress.setWindowModality (Qt::WindowModal);
    progress.show ();

    unsigned int bgr, bgg, bgb;
    sscanf (normalColor.toStdString ().c_str (), "#%2x%2x%2x", &bgr, &bgg, &bgb);

    for (i=0; i<bitmaps.size (); i++) {
        struct freetype_raster fr = bitmaps[i];
        QImage qi (fr.bitmap, fr.cols, fr.rows, fr.bytes_per_row, QImage::Format_Indexed8);

        if (qi.isNull ()) {
            fr.pixmap = nullptr;
        } else {
            qi.setColorCount (fr.num_grays);
            qi.setColor (0, qRgba (0, 0, 0, 0));
            for (j=1; j<fr.num_grays; ++j) {
                qi.setColor (j, qRgba (
                    bgr - (j*bgr)/(fr.num_grays-1),
                    bgg - (j*bgg)/(fr.num_grays-1),
                    bgb - (j*bgb)/(fr.num_grays-1),
                    j
                ));
            }

            QImage canv (m_cell_size, m_cell_size, QImage::Format_ARGB32_Premultiplied);
            canv.fill (qRgba (0, 0, 0, 0));
            QPainter p;
            p.begin (&canv);
            p.drawImage (QPoint (
                (m_cell_size - fr.cols)/2, m_cell_size - fr.as - (m_cell_size * .2)), qi);
            p.end ();
            QPixmap *pm = new QPixmap ();
            pm->convertFromImage (canv);
            _pixmaps.push_back (pm);
        }
        delete[] fr.bitmap;
        fr.bitmap = nullptr;
        qApp->instance ()->processEvents ();
        if (progress.wasCanceled ())
            return false;
        progress.setValue (i);
    }
    progress.setValue (bitmaps.size ());
    return true;
}
#endif

void FontView::undo () {
    uint32_t i;
    std::vector<uint32_t> sortsel (m_selected);
    std::sort (sortsel.begin (), sortsel.end ());

    for (i=0; i<sortsel.size (); i++) {
        GlyphBox *cell = m_cells[sortsel[i]];
        int gid = cell->gid ();
        NonExclusiveUndoGroup *ugroup = m_glyphs[gid].undoGroup ();
        if (ugroup->canUndo ())
            ugroup->undo ();
    }
}

void FontView::redo () {
    uint32_t i;
    std::vector<uint32_t> sortsel (m_selected);
    std::sort (sortsel.begin (), sortsel.end ());

    for (i=0; i<sortsel.size (); i++) {
        GlyphBox *cell = m_cells[sortsel[i]];
        int gid = cell->gid ();
        NonExclusiveUndoGroup *ugroup = m_glyphs[gid].undoGroup ();
        if (ugroup->canRedo ())
            ugroup->redo ();
    }
}

void FontView::undoableCommand (bool (ConicGlyph::*fn)(bool), const char *prog_lbl, const char *undo_lbl) {
    int i=0;
    std::vector<uint32_t> sortsel (m_selected);
    std::sort (sortsel.begin (), sortsel.end ());
    std::vector<bool> gdone (m_font->glyph_cnt, false);

    QProgressDialog progress (tr (prog_lbl), tr ("Abort"), 0, sortsel.size (), this);
    progress.setWindowModality (Qt::WindowModal);
    progress.show ();
    for (uint32_t sel : sortsel) {
        GlyphBox *cell = m_cells[sel];
        int gid = cell->gid ();
	if (!gdone[gid]) {
	    GlyphContext &gctx = m_glyphs[gid];
	    ConicGlyph *g = gctx.glyph (m_content_type);
	    GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_glyphs[gid], m_content_type);
	    ucmd->setText (tr (undo_lbl));
	    if ((g->*fn) (false)) {
		gctx.render (m_content_type, m_cell_size);
		gctx.drawGlyph (g);
		gctx.undoGroup ()->activeStack ()->push (ucmd);
	    } else
		delete ucmd;
	    gdone[gid] = true;
	}
	progress.setValue (i++);
    }
    progress.setValue (sortsel.size ());
}

void FontView::addExtrema () {
    undoableCommand (&ConicGlyph::addExtrema, "Adding extrema...", "Add extrema");
}

void FontView::simplify () {
    undoableCommand (&ConicGlyph::simplify, "Simplifying outlines...", "Simplify outlines");
}

void FontView::roundToInt () {
    undoableCommand (&ConicGlyph::roundToInt, "Rounding to integer...", "Round to int");
}

void FontView::removeOverlap () {
}

void FontView::correctDirection () {
    undoableCommand (&ConicGlyph::correctDirection, "Correcting direction of splines...", "Correct direction");
}

void FontView::autoHint () {
    int i=0;
    std::vector<uint32_t> sortsel (m_selected);
    std::sort (sortsel.begin (), sortsel.end ());
    std::vector<bool> gdone (m_font->glyph_cnt, false);

    QProgressDialog progress (tr ("Autohinting glyphs..."), tr ("Abort"), 0, sortsel.size (), this);
    progress.setWindowModality (Qt::WindowModal);
    progress.show ();
    for (uint32_t sel : sortsel) {
        GlyphBox *cell = m_cells[sel];
        int gid = cell->gid ();
	if (!gdone[gid]) {
	    GlyphContext &gctx = m_glyphs[gid];
	    ConicGlyph *g = gctx.glyph (m_content_type);
	    GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_glyphs[gid], m_content_type);
	    ucmd->setText (tr ("Autohint"));
	    if (g->autoHint (*m_font)) {
		gctx.drawGlyph (g);
		gctx.undoGroup ()->activeStack ()->push (ucmd);
	    } else
		delete ucmd;
	    gdone[gid] = true;
	}
	progress.setValue (i++);
    }
    progress.setValue (sortsel.size ());
}

void FontView::clearHints () {
    std::vector<uint32_t> sortsel (m_selected);
    std::sort (sortsel.begin (), sortsel.end ());
    std::vector<bool> gdone (m_font->glyph_cnt, false);

    for (uint32_t sel : sortsel) {
        GlyphBox *cell = m_cells[sel];
        int gid = cell->gid ();
	if (!gdone[gid]) {
	    GlyphContext &gctx = m_glyphs[gid];
	    ConicGlyph *g = gctx.glyph (m_content_type);
	    GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_glyphs[gid], m_content_type);
	    ucmd->setText (tr ("Clear hints"));
	    if (g->clearHints ()) {
		gctx.drawGlyph (g);
		gctx.undoGroup ()->activeStack ()->push (ucmd);
	    } else
		delete ucmd;
	    gdone[gid] = true;
	}
    }
}

QString GlyphBox::styleSheet = QString (
    "QGroupBox {"
    "   padding: 24px 1 1 1;"
    "   margin: 0;"
    "   border: 1px solid;"
    "   border-top-color: gray; border-left-color: gray;"
    "   border-right-color: black; border-bottom-color: black;"
    "   background-color: %1;"
    "}"
    "QGroupBox::title {"
    "   color: %2;"
    "   padding: 0; margin: 0;"
    "   subcontrol-origin: padding; subcontrol-position: top center;"
    "}"
);


GlyphBox::GlyphBox (QWidget *parent, uint32_t pos, int size) :
    QGroupBox (parent), m_rendered (false), m_selected (false),
    m_uni (-1), m_pos (pos), m_context (nullptr) {

    setAlignment (Qt::AlignHCenter);
    setMinimumSize (size + 4, size + 26);
    setMaximumSize (size + 4, size + 26);

    QColor defaultColor = palette ().color (QWidget::backgroundRole ());
    setStyleSheet (styleSheet.arg (defaultColor.name ()).arg ("#000000"));

    QVBoxLayout *g_layout = new QVBoxLayout ();
    g_layout->setContentsMargins (0, 0, 0, 0);

    m_g_label = new QLabel (this);
    m_g_label->setAlignment (Qt::AlignVCenter | Qt::AlignHCenter);
    m_g_label->setFixedSize (size, size);
    m_g_label->setFocusPolicy (Qt::ClickFocus);
    //m_g_label->setFrameStyle (QFrame::Panel | QFrame::Sunken);
    //m_g_label->setContentsMargins (0, 0, 0, 0);
    displayTitle (0);

    // NB: no rendering by default (do when requested by a PaintEvent)
    m_selected = false;
    g_layout->addWidget (m_g_label);
    setLayout (g_layout);
}

GlyphBox::~GlyphBox () {
}

// NB: can't take unicode from glyph context, as glyph context
// knows only its GID
void GlyphBox::attachGlyph (GlyphContext *gctx, int64_t uni) {
    m_context = gctx;
    connect (m_context->undoGroup (), &NonExclusiveUndoGroup::cleanChanged, this, &GlyphBox::setClean);
    connect (m_context->undoGroup (), &NonExclusiveUndoGroup::indexChanged, this, &GlyphBox::updatePixmap);
    m_uni = uni;
    m_context->addCell (this);
    displayTitle (0);
    m_g_label->setPixmap (QPixmap ());
    m_rendered = false;
}

void GlyphBox::detachGlyph () {
    disconnect (m_context->undoGroup (), &NonExclusiveUndoGroup::cleanChanged, this, &GlyphBox::setClean);
    disconnect (m_context->undoGroup (), &NonExclusiveUndoGroup::indexChanged, this, &GlyphBox::updatePixmap);
    m_context = nullptr;
    m_uni = -1;
    m_g_label->setPixmap (QPixmap ());
    m_rendered = false;
}

void GlyphBox::renderGlyph () {
    if (m_context && m_context->gid () >= 0) {
	QPixmap &pm = m_context->pixmap ();
	m_g_label->setPixmap (pm);
	m_rendered = true;
    }
}

void GlyphBox::paintEvent (QPaintEvent* event) {
    auto vr = visibleRegion ();
    if (!vr.isEmpty () && !m_rendered)
	renderGlyph ();
    QGroupBox::paintEvent (event);
}

void GlyphBox::resizeCell (int size) {
    resize (size + 4, size + 26);
    setMinimumSize (size + 4, size + 26);
    setMaximumSize (size + 4, size + 26);
    m_g_label->setFixedSize (size, size);
    m_rendered = false;
}

void GlyphBox::displayTitle (int) {
    if (m_uni < 0) {
        setTitle ("???");

    } else if (m_uni == 0x26) {
        setTitle ("&&");

    /* ASCII control characters */
    } else if (m_uni < 0x20) {
        uint32_t uni[] = {(uint32_t) m_uni + 0x2400, 0};
        setTitle (QString::fromUcs4 (uni));

    /* Control characters, non-characters, PUA */
    } else if (m_uni == 0 || (m_uni >= 0x80 && m_uni <= 0x9F) ||
        (m_uni >= 0xE000 && m_uni <= 0xF8FF) ||
        (m_uni >= 0xFDD0 && m_uni <= 0xFDEF) ||
        (m_uni >= 0xF0000 && m_uni <= 0xFFFFD) ||
        (m_uni >= 0x100000 && m_uni <= 0x10FFFD) ||
        (m_uni & 0xFFFE) == 0xFFFE || (m_uni & 0xFFFF) == 0xFFFF) {
        setTitle (QString ("%1").arg (m_uni, 4, 16, QLatin1Char('0')));

    /* Combining marks */
    } else if (m_uni <= 0xFFFF && QChar ((uint16_t) m_uni).isMark ()) {
        uint32_t uni[] = {(uint32_t) m_uni, 0};
        setTitle (QString ("\u25CC%1").arg (QString::fromUcs4 (uni)));

    } else {
        uint32_t uni[] = {(uint32_t) m_uni, 0};
        setTitle (QString::fromUcs4 (uni));
    }
}

void GlyphBox::mousePressEvent (QMouseEvent* ev) {
    if (ev->button () == Qt::LeftButton) {
        /* Only emit a signal, actual selection commands are always executed
         * by the container window, as it should also adjust status bar and other things */
        emit selected (this, ev->modifiers (), !m_selected);
    }
}

void GlyphBox::mouseDoubleClickEvent (QMouseEvent* ev) {
    if (ev->button () == Qt::LeftButton)
        emit editRequest (this);
}

void GlyphBox::select (bool val) {
    m_g_label->setStyleSheet (QString ("QLabel { background-color: %1; }").arg (
        val ? FontView::selectedColor : FontView::normalColor));
    m_selected = val;
}

void GlyphBox::updatePixmap (int idx) {
    Q_UNUSED (idx);
    auto vr = visibleRegion ();
    if (!vr.isEmpty ())
	renderGlyph ();
}

void GlyphBox::setClean (bool clean) {
    static QColor defaultColor = palette ().color (QWidget::backgroundRole ());
    setStyleSheet (styleSheet
        .arg (clean ? defaultColor.name () : "#000060")
        .arg (clean ? "#000000" : "#FFFFFF")
    );
    updatePixmap (0);
}

int GlyphBox::gid () const {
    if (m_context)
	return m_context->gid ();
    return -1;
}

uint32_t GlyphBox::position () const {
    return m_pos;
}

int64_t GlyphBox::unicode () const {
    return m_uni;
}

AddGlyphDialog::AddGlyphDialog (CmapEnc *enc, GlyphContainer *gc, QWidget *parent) :
    QDialog (parent), m_enc (enc) {
    int fcnt = 0;
    if (CffTable *cff = dynamic_cast<CffTable *> (gc))
	fcnt = cff->numSubFonts ();

    setWindowTitle (tr ("Add a new glyph to the font"));

    QVBoxLayout *layout = new QVBoxLayout ();
    QGridLayout *glay = new QGridLayout;
    layout->addLayout (glay);

    glay->addWidget (new QLabel ("Unicode"), 0, 0);
    m_uniBox = new UniSpinBox ();
    m_uniBox->setMinimum (-1);
    m_uniBox->setMaximum (0xffffff);
    m_uniBox->setValue (-1);
    glay->addWidget (m_uniBox, 0, 1);

    glay->addWidget (new QLabel ("Glyph name"), 1, 0);
    m_glyphNameField = new QLineEdit ();
    glay->addWidget (m_glyphNameField, 1, 1);
    m_glyphNameField->setValidator (new QRegExpValidator (QRegExp ("[A-Za-z0-9_.]*"), this));

    QLabel *subLabel = new QLabel ("CFF subfont");
    glay->addWidget (subLabel, 2, 0);
    m_subFontBox = new QSpinBox ();
    glay->addWidget (m_subFontBox, 2, 1);

    if (!fcnt) {
	subLabel->setVisible (false);
	m_subFontBox->setVisible (false);
    } else {
	m_subFontBox->setMaximum (fcnt-1);
    }

    QHBoxLayout *butt_layout = new QHBoxLayout ();
    QPushButton* okBtn = new QPushButton ("OK");
    connect (okBtn, &QPushButton::clicked, this, &QDialog::accept);
    butt_layout->addWidget (okBtn);

    QPushButton* cancelBtn = new QPushButton (tr ("Cancel"));
    connect (cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    butt_layout->addWidget (cancelBtn);
    layout->addLayout (butt_layout);

    setLayout (layout);
}

int64_t AddGlyphDialog::unicode () const {
    return m_uniBox->value ();
}

std::string AddGlyphDialog::glyphName () const {
    return m_glyphNameField->text ().toStdString ();
}

uint8_t AddGlyphDialog::subFont () const {
    return m_subFontBox->value ();
}

void AddGlyphDialog::accept () {
    uint32_t uni = unicode ();
    if (m_enc->gidByUnicode (uni))
        FontShepherd::postError (
	    QCoreApplication::tr ("Can't insert glyph"),
            QCoreApplication::tr (
    	    "There is already a glyph mapped to U+%1.")
		.arg (uni, uni <= 0xFFFF ? 4 : 6, 16, QLatin1Char ('0')),
            this);
    else
	QDialog::accept ();
}
