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

#include "sfnt.h"
#include "splineglyph.h"
#include "editors/fontview.h" // also includes tables.h
#include "editors/glyphview.h"
#include "editors/glyphcontext.h"
#include "editors/gvundo.h"
#include "editors/figurepalette.h"
#include "editors/instredit.h"
#include "fs_math.h"
#include "fs_undo.h"
#include "cffstuff.h"

bool GlyphViewContainer::m_settingsDone = false;
bool GlyphViewContainer::m_showPoints = true;
bool GlyphViewContainer::m_showControlPoints = false;
bool GlyphViewContainer::m_showPointNumbering = false;
bool GlyphViewContainer::m_showExtrema = true;
bool GlyphViewContainer::m_showFill = false;
bool GlyphViewContainer::m_showHints = true;
bool GlyphViewContainer::m_showBlues = true;
bool GlyphViewContainer::m_showFamilyBlues = true;
bool GlyphViewContainer::m_showGridFit = false;

bool GlyphViewContainer::showPoints () {
    return m_showPoints;
}

bool GlyphViewContainer::showControlPoints () {
    return m_showControlPoints;
}

bool GlyphViewContainer::showPointNumbering () {
    return m_showPointNumbering;
}

bool GlyphViewContainer::showExtrema () {
    return m_showExtrema;
}

bool GlyphViewContainer::showFill () {
    return m_showFill;
}

bool GlyphViewContainer::showHints () {
    return m_showHints;
}

bool GlyphViewContainer::showBlues () {
    return m_showBlues;
}

bool GlyphViewContainer::showFamilyBlues () {
    return m_showFamilyBlues;
}

bool GlyphViewContainer::showGridFit () {
    return m_showGridFit;
}

GlyphViewContainer::GlyphViewContainer (FontView *fv, sFont &fnt, GlyphContainer *tab) :
    QMainWindow (fv, Qt::Window), m_fv (fv), m_font (fnt), m_tab (tab), tfp (&fnt, this) {

    setAttribute (Qt::WA_DeleteOnClose);
    m_ug_container = new UndoGroupContainer (this);

    QSettings settings (QCoreApplication::organizationName (), QCoreApplication::applicationName ());
    m_width = settings.value ("glyphview/width", 800).toInt ();
    m_height = settings.value ("glyphview/height", 600).toInt ();
    setBaseSize (m_width, m_height);
    resize (m_width, m_height);

    if (!m_settingsDone) {
        m_showPoints = settings.value ("glyphview/showPoints", m_showPoints).toBool ();
        m_showControlPoints = settings.value ("glyphview/showControlPoints", m_showControlPoints).toBool ();
        m_showPointNumbering = settings.value ("glyphview/showPointNumbering", m_showPointNumbering).toBool ();
        m_showExtrema = settings.value ("glyphview/showExtrema", m_showExtrema).toBool ();
        m_showFill = settings.value ("glyphview/showFill", m_showFill).toBool ();

        m_showHints = settings.value ("glyphview/showHints", m_showHints).toBool ();
        m_showBlues = settings.value ("glyphview/showBlues", m_showBlues).toBool ();
        m_showFamilyBlues = settings.value ("glyphview/showFamilyBlues", m_showFamilyBlues).toBool ();
        m_showGridFit = settings.value ("glyphview/showGridFit", m_showGridFit).toBool ();
        m_settingsDone = true;
    }

    m_glyphAreaContainer = new QTabWidget (this);
    m_glyphAreaContainer->setTabsClosable (true);
    connect (m_glyphAreaContainer, &QTabWidget::currentChanged, this, &GlyphViewContainer::switchToTab);
    connect (m_glyphAreaContainer, &QTabWidget::tabCloseRequested, this, &GlyphViewContainer::closeGlyphTab);
    setCentralWidget (m_glyphAreaContainer);

    setStatusBar ();
    setToolsPalette ();
    setFigPalette (settings);
    setInstrPalette (settings);
    setGridFitPalette (settings);
    setMenuBar ();
}

GlyphViewContainer::~GlyphViewContainer () {
}

void GlyphViewContainer::setStatusBar () {
    QStatusBar *sb = this->statusBar ();
    QFontMetrics fm = sb->fontMetrics ();

    QLabel *pointer_lbl = new QLabel (this);
    pointer_lbl->setPixmap (QPixmap (":/pixmaps/palette-pointer.png"));
    sb->addWidget (pointer_lbl);

    m_pos_lbl = new QLabel (this);
    m_pos_lbl->setAlignment (Qt::AlignVCenter | Qt::AlignLeft);
    m_pos_lbl->setFrameStyle (QFrame::Panel | QFrame::Sunken);
    m_pos_lbl->setFixedWidth (fm.boundingRect ("~~1000, 1000~~").width ());
    sb->addWidget (m_pos_lbl);
}

void GlyphViewContainer::setMenuBar () {
    QMenuBar *mb = this->menuBar ();
    QMenu *fileMenu, *editMenu, *elementMenu, *pointMenu, *viewMenu, *hintMenu;

    saveAction = new QAction (tr ("&Save"), this);
    closeAction = new QAction (tr ("C&lose"), this);

    saveAction->setEnabled (false);
    connect (saveAction, &QAction::triggered, this, &GlyphViewContainer::save);
    connect (closeAction, &QAction::triggered, this, &GlyphViewContainer::close);

    saveAction->setShortcut (QKeySequence::Save);
    closeAction->setShortcut (QKeySequence::Close);

    cutAction = new QAction (tr ("C&ut"), this);
    copyAction = new QAction (tr ("&Copy"), this);
    pasteAction = new QAction (tr ("&Paste"), this);
    clearAction = new QAction (tr ("&Delete"), this);
    mergeAction = new QAction (tr ("&Merge"), this);
    joinAction = new QAction (tr ("&Join"), this);
    selectAllAction = new QAction (tr ("Select &all"), this);
    unselectAction = new QAction (tr ("Clea&r selection"), this);

    cutAction->setShortcut (QKeySequence::Cut);
    copyAction->setShortcut (QKeySequence::Copy);
    pasteAction->setShortcut (QKeySequence::Paste);
    clearAction->setShortcut (QKeySequence (Qt::Key_Delete));
    mergeAction->setShortcut (QKeySequence (Qt::CTRL | Qt::Key_M));
    joinAction->setShortcut (QKeySequence (Qt::CTRL | Qt::SHIFT | Qt::Key_J));
    selectAllAction->setShortcut (QKeySequence (Qt::CTRL | Qt::Key_A));
    unselectAction->setShortcut (QKeySequence (Qt::Key_Escape));

    connect (QApplication::clipboard (), &QClipboard::dataChanged, this, &GlyphViewContainer::checkSelection);

    connect (copyAction, &QAction::triggered, this, &GlyphViewContainer::copyRequest);
    connect (cutAction, &QAction::triggered, this, &GlyphViewContainer::cutRequest);
    connect (pasteAction, &QAction::triggered, this, &GlyphViewContainer::pasteRequest);
    connect (clearAction, &QAction::triggered, this, &GlyphViewContainer::clearRequest);
    connect (mergeAction, &QAction::triggered, this, &GlyphViewContainer::mergeRequest);
    connect (joinAction, &QAction::triggered, this, &GlyphViewContainer::joinRequest);
    connect (selectAllAction, &QAction::triggered, this, &GlyphViewContainer::selectAllRequest);
    connect (unselectAction, &QAction::triggered, this, &GlyphViewContainer::clearSelectionRequest);

    addExtremaAction = new QAction (tr ("Add e&xtrema"), this);
    simplifyAction = new QAction (tr ("&Simplify"), this);
    roundAction = new QAction (tr ("Round to &integer"), this);
    overlapAction = new QAction (tr ("Remove &overlap"), this);
    overlapAction->setVisible (false);
    corrDirAction = new QAction (tr ("Correct &direction"), this);
    reverseAction = new QAction (tr ("&Reverse direction"), this);
    unlinkAction = new QAction (tr ("Unlink re&ferences"), this);

    makePtCornerAction = new QAction (tr ("Make Point &Corner"), this);
    makePtCurvedAction = new QAction (tr ("Make Point C&urved"), this);
    makePtTangentAction = new QAction (tr ("Make Point &Tangent"), this);
    makePtFirstAction = new QAction (tr ("Make Point &First"), this);

    addExtremaAction->setShortcut (QKeySequence (Qt::CTRL | Qt::SHIFT | Qt::Key_X));
    simplifyAction->setShortcut (QKeySequence (Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    roundAction->setShortcut (QKeySequence (Qt::CTRL | Qt::SHIFT | Qt::Key_Underscore));
    overlapAction->setShortcut (QKeySequence (Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    corrDirAction->setShortcut (QKeySequence (Qt::CTRL | Qt::SHIFT | Qt::Key_D));
    unlinkAction->setShortcut (QKeySequence (Qt::CTRL | Qt::Key_U));

    makePtCornerAction->setShortcut (QKeySequence (Qt::CTRL | Qt::Key_2));
    makePtCurvedAction->setShortcut (QKeySequence (Qt::CTRL | Qt::Key_3));
    makePtTangentAction->setShortcut (QKeySequence (Qt::CTRL | Qt::Key_4));
    makePtFirstAction->setShortcut (QKeySequence (Qt::CTRL | Qt::Key_1));

    connect (addExtremaAction, &QAction::triggered, this, &GlyphViewContainer::addExtremaRequest);
    connect (simplifyAction, &QAction::triggered, this, &GlyphViewContainer::simplifyRequest);
    connect (roundAction, &QAction::triggered, this, &GlyphViewContainer::roundRequest);
    connect (overlapAction, &QAction::triggered, this, &GlyphViewContainer::overlapRequest);
    connect (corrDirAction, &QAction::triggered, this, &GlyphViewContainer::corrDirRequest);
    connect (reverseAction, &QAction::triggered, this, &GlyphViewContainer::reverseRequest);
    connect (unlinkAction, &QAction::triggered, this, &GlyphViewContainer::unlinkRequest);

    connect (makePtCornerAction, &QAction::triggered, this, &GlyphViewContainer::ptCornerRequest);
    connect (makePtCurvedAction, &QAction::triggered, this, &GlyphViewContainer::ptCurvedRequest);
    connect (makePtTangentAction, &QAction::triggered, this, &GlyphViewContainer::ptTangentRequest);
    connect (makePtFirstAction, &QAction::triggered, this, &GlyphViewContainer::ptFirstRequest);

    ttSwitchAction = new QAction (tr ("Show TrueType Outlines"), this);
    psSwitchAction = new QAction (tr ("Show PostScript Outlines"), this);
    svgSwitchAction = new QAction (tr ("Show SVG Outlines"), this);
    colrSwitchAction = new QAction (tr ("Show Colored Outlines"), this);

    ttSwitchAction->setData (QVariant (static_cast<uint> (OutlinesType::TT)));
    ttSwitchAction->setCheckable (true);
    ttSwitchAction->setEnabled (false);
    psSwitchAction->setData (QVariant (static_cast<uint> (OutlinesType::PS)));
    psSwitchAction->setCheckable (true);
    psSwitchAction->setEnabled (false);
    svgSwitchAction->setData (QVariant (static_cast<uint> (OutlinesType::SVG)));
    svgSwitchAction->setCheckable (true);
    svgSwitchAction->setEnabled (false);
    colrSwitchAction->setData (QVariant (static_cast<uint> (OutlinesType::COLR)));
    colrSwitchAction->setCheckable (true);
    colrSwitchAction->setEnabled (false);

    m_switchOutlineActions = new QActionGroup (this);
    m_switchOutlineActions->addAction (ttSwitchAction);
    m_switchOutlineActions->addAction (psSwitchAction);
    m_switchOutlineActions->addAction (svgSwitchAction);
    m_switchOutlineActions->addAction (colrSwitchAction);

    //NB: don't check any of the actions from this group, as there is no glyph yet

    QAction *figPalAction = m_figDock->toggleViewAction ();
    QAction *instrEditAction = m_instrDock->toggleViewAction ();

    zoomInAction = new QAction (tr ("&Zoom in"), this);
    zoomOutAction = new QAction (tr ("Z&oom out"), this);
    showPtsAction = new QAction (tr ("Show &Points"), this);
    showCtlPtsAction = new QAction (tr ("Show &Control Points (Always)"), this);
    showPtNumAction = new QAction (tr ("Show Point &Numbering"), this);
    showExtremaAction = new QAction (tr ("Show E&xtrema"), this);
    showFillAction = new QAction (tr ("Show &Fill"), this);
    showHintsAction = new QAction (tr ("Show &Hints"), this);
    showBluesAction = new QAction (tr ("Show &Blues"), this);
    showFamilyBluesAction = new QAction (tr ("Show Fa&mily Blues"), this);
    showGridFitAction = new QAction (tr ("Show &Grid Fit"), this);

    zoomInAction->setShortcut (QKeySequence (Qt::CTRL | Qt::Key_Plus));
    zoomOutAction->setShortcut (QKeySequence (Qt::CTRL | Qt::Key_Minus));

    showPtsAction->setCheckable (true);
    showPtsAction->setChecked (m_showPoints);
    showCtlPtsAction->setCheckable (true);
    showCtlPtsAction->setChecked (m_showControlPoints);
    showPtNumAction->setCheckable (true);
    showPtNumAction->setChecked (m_showPointNumbering);
    showExtremaAction->setCheckable (true);
    showExtremaAction->setChecked (m_showExtrema);
    showFillAction->setCheckable (true);
    showFillAction->setChecked (m_showFill);

    showHintsAction->setCheckable (true);
    showHintsAction->setChecked (m_showHints);
    showBluesAction->setCheckable (true);
    showBluesAction->setChecked (m_showBlues);
    showFamilyBluesAction->setCheckable (true);
    showFamilyBluesAction->setChecked (m_showFamilyBlues);
    showGridFitAction->setCheckable (true);
    showGridFitAction->setChecked (m_showGridFit);

    connect (zoomInAction, &QAction::triggered, this, &GlyphViewContainer::zoomIn);
    connect (zoomOutAction, &QAction::triggered, this, &GlyphViewContainer::zoomOut);
    connect (showPtsAction, &QAction::triggered, this, &GlyphViewContainer::slot_showPoints);
    connect (showCtlPtsAction, &QAction::triggered, this, &GlyphViewContainer::slot_showControlPoints);
    connect (showPtNumAction, &QAction::triggered, this, &GlyphViewContainer::slot_showPointNumbering);
    connect (showExtremaAction, &QAction::triggered, this, &GlyphViewContainer::slot_showExtrema);
    connect (showFillAction, &QAction::triggered, this, &GlyphViewContainer::slot_showFill);

    connect (showBluesAction, &QAction::triggered, this, &GlyphViewContainer::slot_showBlues);
    connect (showHintsAction, &QAction::triggered, this, &GlyphViewContainer::slot_showHints);
    connect (showFamilyBluesAction, &QAction::triggered, this, &GlyphViewContainer::slot_showFamilyBlues);
    connect (showGridFitAction, &QAction::triggered, this, &GlyphViewContainer::slot_showGridFit);

    autoHintAction = new QAction (tr ("Auto&hint"), this);
    hmUpdateAction = new QAction (tr ("Update hint &masks"), this);
    clearHintsAction = new QAction (tr ("&Clear hints"), this);
    autoHintAction->setShortcut (QKeySequence (Qt::CTRL | Qt::SHIFT | Qt::Key_H));

    connect (autoHintAction, &QAction::triggered, this, &GlyphViewContainer::autoHintRequest);
    connect (hmUpdateAction, &QAction::triggered, this, &GlyphViewContainer::hmUpdateRequest);
    connect (clearHintsAction, &QAction::triggered, this, &GlyphViewContainer::clearHintsRequest);

    undoAction = m_ug_container->createUndoAction (this, tr ("&Undo"));
    redoAction = m_ug_container->createRedoAction (this, tr ("Re&do"));
    undoAction->setShortcut (QKeySequence::Undo);
    redoAction->setShortcut (QKeySequence::Redo);

    fileMenu = mb->addMenu (tr ("&File"));
    fileMenu->addAction (saveAction);
    fileMenu->addSeparator ();
    fileMenu->addAction (closeAction);

    editMenu = mb->addMenu (tr ("&Edit"));
    editMenu->addAction (undoAction);
    editMenu->addAction (redoAction);
    editMenu->addSeparator ();
    editMenu->addAction (cutAction);
    editMenu->addAction (copyAction);
    editMenu->addAction (pasteAction);
    editMenu->addSeparator ();
    editMenu->addAction (clearAction);
    editMenu->addAction (mergeAction);
    editMenu->addAction (joinAction);
    editMenu->addSeparator ();
    editMenu->addAction (selectAllAction);
    editMenu->addAction (unselectAction);
    connect (editMenu, &QMenu::aboutToShow, this, &GlyphViewContainer::checkSelection);

    elementMenu = mb->addMenu (tr ("&Elements"));
    elementMenu->addAction (addExtremaAction);
    elementMenu->addAction (simplifyAction);
    elementMenu->addAction (roundAction);
    elementMenu->addAction (overlapAction);
    elementMenu->addAction (corrDirAction);
    elementMenu->addAction (reverseAction);
    elementMenu->addSeparator ();
    elementMenu->addAction (unlinkAction);
    connect (elementMenu, &QMenu::aboutToShow, this, &GlyphViewContainer::checkSelection);

    pointMenu = mb->addMenu (tr ("&Point"));
    pointMenu->addAction (makePtCornerAction);
    pointMenu->addAction (makePtCurvedAction);
    pointMenu->addAction (makePtTangentAction);
    pointMenu->addSeparator ();
    pointMenu->addAction (makePtFirstAction);
    connect (pointMenu, &QMenu::aboutToShow, this, &GlyphViewContainer::checkSelection);

    hintMenu = mb->addMenu (tr ("&Hints"));
    hintMenu->addAction (autoHintAction);
    hintMenu->addAction (hmUpdateAction);
    hintMenu->addAction (clearHintsAction);

    viewMenu = mb->addMenu (tr ("&View"));
    viewMenu->addAction (ttSwitchAction);
    viewMenu->addAction (psSwitchAction);
    viewMenu->addAction (svgSwitchAction);
    viewMenu->addAction (colrSwitchAction);
    viewMenu->addSeparator ();
    viewMenu->addAction (figPalAction);
    viewMenu->addAction (instrEditAction);
    viewMenu->addSeparator ();
    viewMenu->addAction (zoomInAction);
    viewMenu->addAction (zoomOutAction);
    viewMenu->addSeparator ();
    viewMenu->addAction (showPtsAction);
    viewMenu->addAction (showCtlPtsAction);
    viewMenu->addAction (showPtNumAction);
    viewMenu->addAction (showExtremaAction);
    viewMenu->addAction (showFillAction);
    viewMenu->addSeparator ();
    viewMenu->addAction (showHintsAction);
    viewMenu->addAction (showBluesAction);
    viewMenu->addAction (showFamilyBluesAction);
    viewMenu->addAction (showGridFitAction);
}

void GlyphViewContainer::setToolsPalette () {
    QToolBar *gvToolbar = new QToolBar (this);
    QAction *palPointerAction = gvToolbar->addAction (
        QIcon (":/pixmaps/palette-pointer.png"), tr ("Pointer"));
    QAction *palHandAction = gvToolbar->addAction (
        QIcon (":/pixmaps/palette-hand.png"), tr ("Scroll"));
    QAction *palKnifeAction = gvToolbar->addAction (
        QIcon (":/pixmaps/palette-knife.png"), tr ("Cut splines in two"));
    QAction *palZoomAction = gvToolbar->addAction (
        QIcon (":/pixmaps/palette-magnify.png"), tr ("Zoom In (with Ctrl - Zoom Out)"));
    QAction *palCornerAction = gvToolbar->addAction (
        QIcon (":/pixmaps/palette-corner.png"), tr ("Add Corner Point"));
    QAction *palCurveAction = gvToolbar->addAction (
        QIcon (":/pixmaps/palette-curve.png"), tr ("Add Curve Point"));
    QAction *palTangentAction = gvToolbar->addAction (
        QIcon (":/pixmaps/palette-tangent.png"), tr ("Add Tangent Point"));

    QAction *palEllipseAction = gvToolbar->addAction (
        QIcon (":/pixmaps/palette-ellipse.png"), tr ("Draw Ellipse"));
    QAction *palRectAction = gvToolbar->addAction (
        QIcon (":/pixmaps/palette-rect.png"), tr ("Draw Rectangle"));

    m_defaultPaletteToolAction = palPointerAction;

    palPointerAction->setCheckable (true);
    palPointerAction->setData (QVariant (static_cast<uint> (GVPaletteTool::pointer)));
    palHandAction->setCheckable (true);
    palHandAction->setData (QVariant (static_cast<uint> (GVPaletteTool::hand)));
    palKnifeAction->setCheckable (true);
    palKnifeAction->setData (QVariant (static_cast<uint> (GVPaletteTool::knife)));
    palZoomAction->setCheckable (true);
    palZoomAction->setData (QVariant (static_cast<uint> (GVPaletteTool::zoom)));
    palCornerAction->setCheckable (true);
    palCornerAction->setData (QVariant (static_cast<uint> (GVPaletteTool::corner)));
    palCurveAction->setCheckable (true);
    palCurveAction->setData (QVariant (static_cast<uint> (GVPaletteTool::curve)));
    palTangentAction->setCheckable (true);
    palTangentAction->setData (QVariant (static_cast<uint> (GVPaletteTool::tangent)));

    palEllipseAction->setCheckable (true);
    palEllipseAction->setData (QVariant (static_cast<uint> (GVPaletteTool::ellipse)));
    palRectAction->setCheckable (true);
    palRectAction->setData (QVariant (static_cast<uint> (GVPaletteTool::rect)));

    m_palActions = new QActionGroup (this);
    m_palActions->addAction (palPointerAction);
    m_palActions->addAction (palHandAction);
    m_palActions->addAction (palKnifeAction);
    m_palActions->addAction (palZoomAction);
    m_palActions->addAction (palCornerAction);
    m_palActions->addAction (palCurveAction);
    m_palActions->addAction (palTangentAction);
    m_palActions->addAction (palEllipseAction);
    m_palActions->addAction (palRectAction);

    palPointerAction->setChecked (true);

    gvToolbar->setOrientation (Qt::Vertical);
    gvToolbar->setAllowedAreas (Qt::LeftToolBarArea);
    this->addToolBar (Qt::LeftToolBarArea, gvToolbar);
}

void GlyphViewContainer::setGridFitPalette (QSettings &settings) {
    m_gfToolbar = new QToolBar (this);
    bool mono = settings.value ("glyphview/GridFit/monochrome", false).toBool ();
    bool same = settings.value ("glyphview/GridFit/sameXY", true).toBool ();
    unsigned int ppemX = settings.value ("glyphview/GridFit/ppemX", 22).toUInt ();
    unsigned int ppemY = settings.value ("glyphview/GridFit/ppemY", 22).toUInt ();

    m_gfToolbar->setOrientation (Qt::Horizontal);
    m_gfToolbar->setAllowedAreas (Qt::TopToolBarArea | Qt::BottomToolBarArea);

    QCheckBox *monoBox = new QCheckBox (tr ("Monochrome rendering"));
    monoBox->setCheckState (mono ? Qt::Checked : Qt::Unchecked);
    m_gfToolbar->addWidget (monoBox);
    connect (monoBox, &QCheckBox::clicked, this, &GlyphViewContainer::slot_monoBoxClicked);

    QCheckBox *sameXYBox = new QCheckBox (tr ("Scale X/Y the same"));
    sameXYBox->setCheckState (same ? Qt::Checked : Qt::Unchecked);
    m_gfToolbar->addWidget (sameXYBox);
    connect (sameXYBox, &QCheckBox::clicked, this, &GlyphViewContainer::slot_sameXYBoxClicked);

    m_gfToolbar->addSeparator ();

    m_xPpemLabel = new QLabel (QString ("X PPEM: %1").arg (ppemX));
    m_gfToolbar->addWidget (m_xPpemLabel);
    m_xPpemSlider = new QSlider (Qt::Horizontal);
    m_xPpemSlider->setTickPosition (QSlider::TicksBothSides);
    m_xPpemSlider->setTickInterval (4);
    m_xPpemSlider->setRange (8, 48);
    m_xPpemSlider->setValue (ppemX);
    m_gfToolbar->addWidget (m_xPpemSlider);
    connect (m_xPpemSlider, &QSlider::valueChanged, this, &GlyphViewContainer::slot_xPpemChanged);

    m_gfToolbar->addSeparator ();

    m_yPpemLabel = new QLabel (QString ("X PPEM: %1").arg (ppemX));
    m_yPpemLabel->setEnabled (!same);
    m_gfToolbar->addWidget (m_yPpemLabel);
    m_yPpemSlider = new QSlider (Qt::Horizontal);
    m_yPpemSlider->setTickPosition (QSlider::TicksBothSides);
    m_yPpemSlider->setTickInterval (4);
    m_yPpemSlider->setRange (8, 48);
    m_yPpemSlider->setValue (ppemY);
    m_yPpemSlider->setEnabled (!same);
    m_gfToolbar->addWidget (m_yPpemSlider);
    connect (m_yPpemSlider, &QSlider::valueChanged, this, &GlyphViewContainer::slot_yPpemChanged);

    this->addToolBar (Qt::TopToolBarArea, m_gfToolbar);
}

void GlyphViewContainer::setFigPalette (QSettings &settings) {
    m_figPalContainer = new QStackedWidget (this);
    m_figDock = new QDockWidget (this);
    m_figDock->setAllowedAreas (Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    m_figDock->setWidget (m_figPalContainer);
    m_figDock->setWindowTitle (tr ("SVG Figures"));

    addDockWidget (Qt::BottomDockWidgetArea, m_figDock);

    int xpos = settings.value ("glyphview/FigurePalette/xPos", 0).toInt ();
    int ypos = settings.value ("glyphview/FigurePalette/yPos", 0).toInt ();
    int w = settings.value ("glyphview/FigurePalette/width", m_figDock->width ()).toInt ();
    int h = settings.value ("glyphview/FigurePalette/height", m_figDock->height ()).toInt ();
    bool visible = settings.value ("glyphview/FigurePalette/isVisible", true).toBool ();
    bool docked = settings.value ("glyphview/FigurePalette/isDocked", false).toBool ();

    QRect r = geometry ();
    m_figDock->setFloating (!docked);
    m_figDock->setVisible (visible);
    m_figDock->move (r.x () + xpos, r.y () + ypos);
    m_figDock->resize (w, h);
}

void GlyphViewContainer::setInstrPalette (QSettings &settings) {
    m_instrEditContainer = new QStackedWidget (this);
    m_instrDock = new QDockWidget (this);
    m_instrDock->setAllowedAreas (Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_instrDock->setWidget (m_instrEditContainer);
    m_instrDock->setWindowTitle (tr ("TTF Instructions"));

    addDockWidget (Qt::RightDockWidgetArea, m_instrDock);

    int xpos = settings.value ("glyphview/TTFInstrPalette/xPos", 0).toInt ();
    int ypos = settings.value ("glyphview/TTFInstrPalette/yPos", 0).toInt ();
    int w = settings.value ("glyphview/TTFInstrPalette/width", m_instrDock->width ()).toInt ();
    int h = settings.value ("glyphview/TTFInstrPalette/height", m_instrDock->height ()).toInt ();
    bool visible = settings.value ("glyphview/TTFInstrPalette/isVisible", true).toBool ();
    bool docked = settings.value ("glyphview/TTFInstrPalette/isDocked", true).toBool ();

    QRect r = geometry ();
    m_instrDock->setFloating (!docked);
    m_instrDock->setVisible (visible);
    m_instrDock->move (r.x () + xpos, r.y () + ypos);
    m_instrDock->resize (w, h);
}

void GlyphViewContainer::addGlyph (GlyphContext &gctx, OutlinesType content_type) {
    setWindowTitle (QString ("%1 - %2").arg (m_font.fontname).arg (gctx.name ()));

    gctx.switchOutlinesType (content_type, true);

    ConicGlyph *g = gctx.glyph (content_type);

    NonExclusiveUndoGroup *ug = gctx.undoGroup (true);
    m_ug_container->addGroup (ug);
    //NB: no need to set this group active right here, as switchToTab () will take care of it

    if (tfp.valid ()) {
	tfp.appendOrReloadGlyph (gctx.gid ());
	tfp.compile ();
	ftWrapper.init (&tfp);
    }
    GlyphScene *scene = new GlyphScene (m_font, ftWrapper, gctx, content_type);
    gctx.appendScene (scene);

    GlyphView *view = new GlyphView (scene, m_figPalContainer, m_instrEditContainer, gctx, this);
    int idx = m_glyphAreaContainer->addTab (view, gctx.name ());
    m_glyphAreaContainer->setCurrentIndex (idx);
    m_tabmap[gctx.gid ()] = idx;

    int base_w = g->isEmpty () ? g->advanceWidth () : g->bb.maxx - g->bb.minx;
    int base_h = g->isEmpty () ? g->upm () : g->bb.maxy - g->bb.miny;
    view->fitInView (QRectF (
	    g->bb.minx - g->upm ()/10,
	    g->bb.miny - g->upm ()/10,
	    base_w + g->upm ()/5,
	    base_h + g->upm ()/5
        ), Qt::KeepAspectRatio);

    // NB: don't call view->doZoom () here, as it would scale the viewable area once again
    QPoint zero_pos = view->mapFromScene (QPointF (0, 0));
    view->setRulerOrigin (zero_pos.x (), true);
    view->setRulerOrigin (zero_pos.y (), false);
    view->setRulerZoom (view->transform ().m11 (), true);
    view->setRulerZoom (view->transform ().m22 (), false);

    uint16_t i;
    for (i=0; i<idx; i++) {
        GlyphView *gv = qobject_cast<GlyphView*> (m_glyphAreaContainer->widget (i));
	GlyphScene *gscene = qobject_cast<GlyphScene *> (gv->scene ());
        disconnect (gscene, &QGraphicsScene::selectionChanged, this, &GlyphViewContainer::checkSelection);
	disconnect (gscene, &GlyphScene::mousePointerMoved, this, &GlyphViewContainer::showMousePointerPos);
        disconnect (m_palActions, &QActionGroup::triggered, gv, &GlyphView::toolSelected);
        disconnect (m_switchOutlineActions, &QActionGroup::triggered, gv, &GlyphView::on_switchOutlines);
	disconnect (gv, &GlyphView::requestUpdateGridFit, this, &GlyphViewContainer::updateGridFitActive);
    }
    connect (scene, &QGraphicsScene::selectionChanged, this, &GlyphViewContainer::checkSelection);
    connect (scene, &GlyphScene::mousePointerMoved, this, &GlyphViewContainer::showMousePointerPos);
    connect (m_palActions, &QActionGroup::triggered, view, &GlyphView::toolSelected);
    connect (m_switchOutlineActions, &QActionGroup::triggered, view, &GlyphView::on_switchOutlines);
    connect (view, &GlyphView::requestUpdateGridFit, this, &GlyphViewContainer::updateGridFitActive);

    scene->installEventFilter (this);

    m_defaultPaletteToolAction->setChecked (true);

    ttSwitchAction->setEnabled (gctx.hasOutlinesType (OutlinesType::TT));
    psSwitchAction->setEnabled (gctx.hasOutlinesType (OutlinesType::PS));
    svgSwitchAction->setEnabled (gctx.hasOutlinesType (OutlinesType::SVG));
    colrSwitchAction->setEnabled (gctx.hasOutlinesType (OutlinesType::COLR));

    autoHintAction->setEnabled (gctx.hasOutlinesType (OutlinesType::PS));
    hmUpdateAction->setEnabled (gctx.hasOutlinesType (OutlinesType::PS));
    clearHintsAction->setEnabled (gctx.hasOutlinesType (OutlinesType::PS));

    if (content_type == OutlinesType::TT)
        ttSwitchAction->setChecked (true);
    else if (content_type == OutlinesType::PS)
        psSwitchAction->setChecked (true);
    else if (content_type == OutlinesType::SVG)
        svgSwitchAction->setChecked (true);

    m_gfToolbar->setVisible (m_showGridFit && content_type == OutlinesType::TT);

    gctx.updatePoints ();
    checkSelection ();
}

bool GlyphViewContainer::hasGlyph (const uint16_t gid) const {
    return (m_tabmap.count (gid) > 0);
}

int GlyphViewContainer::glyphTabIndex (const uint16_t gid) const {
    if (m_tabmap.count (gid) > 0) {
	int ret = m_tabmap.at (gid);
	return ret;
    }
    return -1;
}

void GlyphViewContainer::switchToTab (int index) {
    uint16_t i;

    if (index < 0)
        return;
    GlyphView *active = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *act_scene = qobject_cast<GlyphScene *> (active->scene ());
    GlyphContext &gctx = active->glyphContext ();
    OutlinesType ctype = active->outlinesType ();

    m_ug_container->setActiveGroup (active->undoGroup ());
    setWindowTitle (QString ("%1 - %2").arg (m_font.fontname).arg (active->glyphName ()));

    for (i=0; i<m_glyphAreaContainer->count (); i++) {
        GlyphView *gv = qobject_cast<GlyphView*> (m_glyphAreaContainer->widget (i));
	GlyphScene *gscene = qobject_cast<GlyphScene *> (gv->scene ());
        disconnect (gscene, &QGraphicsScene::selectionChanged, this, &GlyphViewContainer::checkSelection);
	disconnect (gscene, &GlyphScene::mousePointerMoved, this, &GlyphViewContainer::showMousePointerPos);
        disconnect (m_palActions, &QActionGroup::triggered, gv, &GlyphView::toolSelected);
        disconnect (m_switchOutlineActions, &QActionGroup::triggered, gv, &GlyphView::on_switchOutlines);
	disconnect (gv, &GlyphView::requestUpdateGridFit, this, &GlyphViewContainer::updateGridFitActive);
    }
    connect (act_scene, &QGraphicsScene::selectionChanged, this, &GlyphViewContainer::checkSelection);
    connect (act_scene, &GlyphScene::mousePointerMoved, this, &GlyphViewContainer::showMousePointerPos);
    connect (m_palActions, &QActionGroup::triggered, active, &GlyphView::toolSelected);
    connect (m_switchOutlineActions, &QActionGroup::triggered, active, &GlyphView::on_switchOutlines);
    connect (active, &GlyphView::requestUpdateGridFit, this, &GlyphViewContainer::updateGridFitActive);

    QAction *vAction = active->activeAction ();
    if (!vAction)
        vAction = m_defaultPaletteToolAction;
    vAction->setChecked (true);

    ttSwitchAction->setEnabled (gctx.hasOutlinesType (OutlinesType::TT));
    psSwitchAction->setEnabled (gctx.hasOutlinesType (OutlinesType::PS));
    svgSwitchAction->setEnabled (gctx.hasOutlinesType (OutlinesType::SVG));
    colrSwitchAction->setEnabled (gctx.hasOutlinesType (OutlinesType::COLR));
    if (ctype == OutlinesType::TT)
        ttSwitchAction->setChecked (true);
    else if (ctype == OutlinesType::PS)
        psSwitchAction->setChecked (true);
    else if (ctype == OutlinesType::SVG)
        svgSwitchAction->setChecked (true);
    else if (ctype == OutlinesType::COLR)
        colrSwitchAction->setChecked (true);

    autoHintAction->setEnabled (gctx.hasOutlinesType (OutlinesType::PS));
    hmUpdateAction->setEnabled (gctx.hasOutlinesType (OutlinesType::PS));
    clearHintsAction->setEnabled (gctx.hasOutlinesType (OutlinesType::PS));

    showGridFitAction->setEnabled (gctx.hasOutlinesType (OutlinesType::TT));

    m_figPalContainer->setCurrentIndex (index);
    m_instrEditContainer->setCurrentIndex (index);

    m_pos_lbl->setText ("");
    checkSelection ();
}

void GlyphViewContainer::showMousePointerPos (QPointF pos) {
    m_pos_lbl->setText (QString ("%1, %2")
	.arg (QString::number (pos.x (), 'f', 2))
	.arg (QString::number (pos.y (), 'f', 2)));
}

bool GlyphViewContainer::eventFilter (QObject *watched, QEvent *event) {
    QGraphicsSceneMouseEvent *me;
    QString obj_type = watched->metaObject ()->className ();

    if (!obj_type.compare ("GlyphScene")) {
	switch (event->type ()) {
	  case QEvent::GraphicsSceneMouseMove:
	    me = static_cast<QGraphicsSceneMouseEvent *> (event);
	    showMousePointerPos (me->scenePos ());
	    break;
	  case QEvent::Leave:
	    m_pos_lbl->setText ("");
	    break;
	  default:
	    ;
	}
    }
    return false;
}

// This one is called when a click in fontview activates a previously
// opened, but currently inactive glyph view tab
void GlyphViewContainer::switchToGlyph (const uint16_t gid, OutlinesType ctype) {
    if (m_tabmap.count (gid))
        m_glyphAreaContainer->setCurrentIndex (m_tabmap[gid]);

    if (ctype == OutlinesType::TT)
        ttSwitchAction->trigger ();
    else if (ctype == OutlinesType::PS)
        psSwitchAction->trigger ();
    else if (ctype == OutlinesType::SVG)
        svgSwitchAction->trigger ();
}

void GlyphViewContainer::updateGridFit () {
    if (tfp.valid ()) {
	tfp.reloadGlyphs ();
	tfp.compile ();
	ftWrapper.init (&tfp);
    }
}

void GlyphViewContainer::updateGridFitActive () {
    GlyphView *active = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    if (active->outlinesType () == OutlinesType::TT) {
	tfp.appendOrReloadGlyph (active->gid ());
	tfp.compile ();
	ftWrapper.init (&tfp);
    }
}

void GlyphViewContainer::reallyCloseGlyphTab (int idx) {
    GlyphView *gv = qobject_cast<GlyphView*> (m_glyphAreaContainer->widget (idx));
    GlyphScene *gscene = qobject_cast<GlyphScene *> (gv->scene ());
    GlyphContext &ctx = gv->glyphContext ();
    m_ug_container->removeGroup (gv->undoGroup ());
    disconnect (gscene, &GlyphScene::selectionChanged, this, &GlyphViewContainer::checkSelection);
    disconnect (gscene, &GlyphScene::mousePointerMoved, this, &GlyphViewContainer::showMousePointerPos);
    disconnect (m_palActions, &QActionGroup::triggered, gv, &GlyphView::toolSelected);
    disconnect (m_switchOutlineActions, &QActionGroup::triggered, gv, &GlyphView::on_switchOutlines);
    disconnect (gv, &GlyphView::requestUpdateGridFit, this, &GlyphViewContainer::updateGridFitActive);

    QWidget *pal_tab = m_figPalContainer->widget (idx);
    m_figPalContainer->removeWidget (pal_tab);
    pal_tab->deleteLater ();

    QWidget *instr_tab = m_instrEditContainer->widget (idx);
    m_instrEditContainer->removeWidget (instr_tab);
    instr_tab->deleteLater ();

    ctx.deleteScene ();
    m_tabmap.erase (gv->gid ());
    m_glyphAreaContainer->removeTab (idx);
    gv->deleteLater ();
}

void GlyphViewContainer::closeEvent (QCloseEvent *) {
    QSettings settings (QCoreApplication::organizationName (), QCoreApplication::applicationName ());

    settings.setValue ("glyphview/width", width ());
    settings.setValue ("glyphview/height", height ());

    QRect r = geometry ();
    QRect pr = m_figDock->geometry ();

    settings.setValue ("glyphview/FigurePalette/isDocked", !m_figDock->isFloating ());
    settings.setValue ("glyphview/FigurePalette/isVisible", m_figDock->isVisible ());
    settings.setValue ("glyphview/FigurePalette/xPos", pr.x () - r.x ());
    settings.setValue ("glyphview/FigurePalette/yPos", pr.y () - r.y ());
    settings.setValue ("glyphview/FigurePalette/width", m_figDock->width ());
    settings.setValue ("glyphview/FigurePalette/height", m_figDock->height ());

    settings.setValue ("glyphview/TTFInstrPalette/isDocked", !m_instrDock->isFloating ());
    settings.setValue ("glyphview/TTFInstrPalette/isVisible", m_instrDock->isVisible ());
    settings.setValue ("glyphview/TTFInstrPalette/xPos", pr.x () - r.x ());
    settings.setValue ("glyphview/TTFInstrPalette/yPos", pr.y () - r.y ());
    settings.setValue ("glyphview/TTFInstrPalette/width", m_instrDock->width ());
    settings.setValue ("glyphview/TTFInstrPalette/height", m_instrDock->height ());

    m_fv->clearGV ();
    // Disconnect signals and delete glyph scenes on close event to prevent
    // a situation when a scene object still exists while the tab widget is not
    // (this may lead to a crash if say selectionChanged signal is triggered).
    // But don't delete glyph tabs themselves, as they are owned by the
    // tab widget and supposed to be destroyed automatically.
    for (int i=m_glyphAreaContainer->count ()-1; i>=0; i--)
	reallyCloseGlyphTab (i);
}

void GlyphViewContainer::closeGlyphTab (int idx) {
    reallyCloseGlyphTab (idx);
    if (!m_glyphAreaContainer->count ())
        close ();
}

void GlyphViewContainer::checkSelection () {
    GlyphView *view = qobject_cast<GlyphView *> (m_glyphAreaContainer->currentWidget ());
    int num_pts = view->numSelectedPoints ();
    int num_refs = view->numSelectedRefs ();
    int num_figs = view->numSelectedFigs ();

    makePtCornerAction->setEnabled (num_pts);
    makePtCurvedAction->setEnabled (num_pts);
    makePtTangentAction->setEnabled (num_pts);
    makePtFirstAction->setEnabled (num_pts == 1);

    cutAction->setEnabled (num_pts + num_refs + num_figs);
    copyAction->setEnabled (num_pts + num_refs + num_figs);
    clearAction->setEnabled (num_pts + num_refs + num_figs);
    mergeAction->setEnabled (num_pts);
    unselectAction->setEnabled (num_pts + num_refs + num_figs);

    QClipboard *clipboard = QApplication::clipboard ();
    const QMimeData *md = clipboard->mimeData ();
    pasteAction->setEnabled (md->hasFormat ("image/svg+xml"));

    reverseAction->setEnabled (num_pts);
}

void GlyphViewContainer::save () {
    m_fv->save ();
}

void GlyphViewContainer::close () {
    QMainWindow::close ();
}

void GlyphViewContainer::copyRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->doCopyClear (true, false);
    //pasteAction->setEnabled (true);
}

void GlyphViewContainer::cutRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->doCopyClear (true, true);
    //pasteAction->setEnabled (true);
}

void GlyphViewContainer::pasteRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->doPaste ();
}

void GlyphViewContainer::clearRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->doCopyClear (false, true);
}

void GlyphViewContainer::mergeRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->doMerge ();
}

void GlyphViewContainer::joinRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->doJoin ();
}

void GlyphViewContainer::selectAllRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->selectAll ();
}

void GlyphViewContainer::clearSelectionRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->clearSelection ();
}

void GlyphViewContainer::addExtremaRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->doExtrema ();
}

void GlyphViewContainer::simplifyRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->doSimplify ();
}

void GlyphViewContainer::roundRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->doRound ();
}

void GlyphViewContainer::overlapRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->doOverlap ();
}

void GlyphViewContainer::corrDirRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->doDirection ();
}

void GlyphViewContainer::reverseRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->doReverse ();
}

void GlyphViewContainer::unlinkRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->doUnlinkRefs ();
}

void GlyphViewContainer::ptCornerRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->setSelPointsType (pt_corner);
}

void GlyphViewContainer::ptCurvedRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->setSelPointsType (pt_curve);
}

void GlyphViewContainer::ptTangentRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->setSelPointsType (pt_tangent);
}

void GlyphViewContainer::ptFirstRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->setSelPointFirst ();
}

void GlyphViewContainer::autoHintRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->doAutoHint (m_font);
    QWidget * viewport = view->viewport ();
    viewport->update ();
}

void GlyphViewContainer::hmUpdateRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->doHintMasksUpdate (m_font);
    QWidget * viewport = view->viewport ();
    viewport->update ();
}

void GlyphViewContainer::clearHintsRequest () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (view->scene ());
    gsc->doClearHints ();
    QWidget * viewport = view->viewport ();
    viewport->update ();
}

void GlyphViewContainer::zoomIn () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    view->doZoom (1.25);
}

void GlyphViewContainer::zoomOut () {
    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    view->doZoom (.8);
}

void GlyphViewContainer::updateViewSetting (const QString key, const bool val) {
    QSettings settings (QCoreApplication::organizationName (), QCoreApplication::applicationName ());
    settings.setValue ("glyphview/" + key, val);

    for (int i=0; i<m_glyphAreaContainer->count (); i++) {
	GlyphView *gv = qobject_cast<GlyphView*> (m_glyphAreaContainer->widget (i));
	if (key.compare ("showFill") == 0)
	    gv->updateFill ();
	else
	    gv->updatePoints ();
    }

    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    QWidget * viewport = view->viewport ();
    viewport->update ();
}

void GlyphViewContainer::slot_showPoints (const bool val) {
    m_showPoints = val;
    updateViewSetting ("showPoints", val);
}

void GlyphViewContainer::slot_showControlPoints (const bool val) {
    m_showControlPoints = val;
    updateViewSetting ("showControlPoints", val);
}

void GlyphViewContainer::slot_showPointNumbering (const bool val) {
    m_showPointNumbering = val;
    updateViewSetting ("showPointNumbering", val);
}

void GlyphViewContainer::slot_showExtrema (const bool val) {
    m_showExtrema = val;
    updateViewSetting ("showExtrema", val);
}

void GlyphViewContainer::slot_showFill (const bool val) {
    m_showFill = val;
    updateViewSetting ("showFill", val);
}

void GlyphViewContainer::slot_showHints (const bool val) {
    m_showHints = val;
    updateViewSetting ("showHints", val);
}

void GlyphViewContainer::slot_showBlues (const bool val) {
    m_showBlues = val;
    updateViewSetting ("showBlues", val);
}

void GlyphViewContainer::slot_showFamilyBlues (const bool val) {
    m_showFamilyBlues = val;
    updateViewSetting ("showFamilyBlues", val);
}

void GlyphViewContainer::slot_showGridFit (const bool val) {
    m_showGridFit = val;

    GlyphView *active = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    OutlinesType ctype = active->outlinesType ();

    m_gfToolbar->setVisible (val && ctype == OutlinesType::TT);
    updateViewSetting ("showGridFit", val);
}

void GlyphViewContainer::slot_monoBoxClicked (const bool val) {
    QSettings settings (QCoreApplication::organizationName (), QCoreApplication::applicationName ());
    settings.setValue ("glyphview/GridFit/monochrome", val);

    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    QWidget * viewport = view->viewport ();
    viewport->update ();
}

void GlyphViewContainer::slot_sameXYBoxClicked (const bool val) {
    QSettings settings (QCoreApplication::organizationName (), QCoreApplication::applicationName ());
    settings.setValue ("glyphview/GridFit/sameXY", val);
    if (val) {
	int xval = m_xPpemSlider->value ();
	m_yPpemSlider->setValue (xval);
	m_yPpemLabel->setText (QString ("Y PPEM: %1").arg (xval));
	settings.setValue ("glyphview/GridFit/ppemY", m_xPpemSlider->value ());

	GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
	QWidget * viewport = view->viewport ();
	viewport->update ();
    }
    m_yPpemLabel->setEnabled (!val);
    m_yPpemSlider->setEnabled (!val);
}

void GlyphViewContainer::slot_xPpemChanged (const int val) {
    QSettings settings (QCoreApplication::organizationName (), QCoreApplication::applicationName ());
    bool same = settings.value ("glyphview/GridFit/sameXY", true).toBool ();
    settings.setValue ("glyphview/GridFit/ppemX", val);
    m_xPpemLabel->setText (QString ("X PPEM: %1").arg (val));
    QToolTip::showText (QCursor::pos (), QString ("%1").arg (val), nullptr);
    if (same) {
	// viewport is updated by the Y slider
	m_yPpemSlider->setValue (val);
    } else {
	GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
	QWidget * viewport = view->viewport ();
	viewport->update ();
    }
}

void GlyphViewContainer::slot_yPpemChanged (const int val) {
    QSettings settings (QCoreApplication::organizationName (), QCoreApplication::applicationName ());
    settings.setValue ("glyphview/GridFit/ppemY", val);
    m_yPpemLabel->setText (QString ("Y PPEM: %1").arg (val));
    QToolTip::showText (QCursor::pos (), QString ("%1").arg (val), nullptr);

    GlyphView *view = qobject_cast<GlyphView*> (m_glyphAreaContainer->currentWidget ());
    QWidget * viewport = view->viewport ();
    viewport->update ();
}

FTWrapper* GlyphViewContainer::freeTypeWrapper () {
    return &ftWrapper;
}

GlyphView::GlyphView (GlyphScene *scene, QStackedWidget *figPalContainer,
    QStackedWidget *instrEditContainer, GlyphContext &gctx, QWidget *parent) :
    QGraphicsView (scene, parent), m_context (gctx) {
    setViewportMargins (RULER_BREADTH, RULER_BREADTH, 0, 0);

    QGridLayout* gridLayout = new QGridLayout ();
    gridLayout->setSpacing (0);
    gridLayout->setContentsMargins (0, 0, 0, 0);

    m_HorzRuler = new QDRuler (QDRuler::Horizontal, this);
    m_VertRuler = new QDRuler (QDRuler::Vertical, this);

    QWidget* fake = new QWidget ();
    fake->setBackgroundRole (QPalette::Window);
    fake->setFixedSize (RULER_BREADTH, RULER_BREADTH);
    gridLayout->addWidget (fake, 0, 0);
    gridLayout->addWidget (m_HorzRuler, 0, 1);
    gridLayout->addWidget (m_VertRuler, 1, 0);
    gridLayout->addWidget (viewport (), 1, 1);

    setLayout (gridLayout);
    setTransform (QTransform (1, 0, 0, -1, 0, 0));
    setRenderHints (QPainter::Antialiasing);

    QScrollBar *hs = horizontalScrollBar ();
    QScrollBar *vs = verticalScrollBar ();
    connect (hs, &QScrollBar::valueChanged, this, &GlyphView::scrolledHorizontally);
    connect (vs, &QScrollBar::valueChanged, this, &GlyphView::scrolledVertically);

    setDragMode (QGraphicsView::NoDrag);
    m_activeAction = nullptr;

    // For some reason it is necessary to explicitly activate scene at
    // this point, so that setting activePanel would have an immediate effect
    // (otherwise remains inactive up to the very end of the caller function).
    // See André Vautour's comment at https://bugreports.qt.io/browse/QTBUG-85728
    QEvent event (QEvent::WindowActivate);
    QCoreApplication::sendEvent (scene, &event);
    ConicGlyph *g = m_context.glyph (outlinesType ());
    m_context.drawGlyph (g, g->gradients);

    m_figMod = std::unique_ptr<FigureModel>
	(new FigureModel (m_context.topItem (), m_context.glyph (outlinesType ())));
    m_figPal = new FigurePalette (m_context, m_figMod.get (), outlinesType (), parent, figPalContainer);
    figPalContainer->addWidget (m_figPal);
    figPalContainer->setCurrentWidget (m_figPal);

    m_figPal->setEnabled (outlinesType () == OutlinesType::SVG);
    m_figPal->selectRow (m_figMod->rowCount () - (scene->activePanelIndex () + 1));

    connect (m_figPal->selectionModel (), &QItemSelectionModel::selectionChanged, this, &GlyphView::setActiveFigure);
    connect (m_figMod.get (), &QAbstractItemModel::dataChanged, this, &GlyphView::onFigurePaletteUpdate);

    connect (scene, &GlyphScene::activePanelChanged, this, &GlyphView::onActiveFigureChange);
    connect (scene, &GlyphScene::panelAdded, this, &GlyphView::onAddFigure);
    connect (scene, &GlyphScene::panelRemoved, this, &GlyphView::onRemoveFigure);
    connect (scene, &GlyphScene::glyphRedrawn, this, &GlyphView::glyphRedrawn);
    connect (scene, &GlyphScene::panelsSwapped, this, &GlyphView::onSwapPanels);
    connect (scene, &GlyphScene::figurePropsChanged, this, &GlyphView::figurePropsChanged);

    m_instrEdit = new InstrEdit (g->instructions.data (), g->instructions.size (), instrEditContainer);
    instrEditContainer->addWidget (m_instrEdit);
    instrEditContainer->setCurrentWidget (m_instrEdit);

    m_instrEdit->setEnabled (outlinesType () == OutlinesType::TT);
    connect (m_instrEdit, &InstrEdit::instrChanged, this, &GlyphView::on_instrChanged);
}

GlyphView::~GlyphView () {
    m_context.deleteScene ();
}

void GlyphView::setViewportMargins (int left, int top, int right, int bottom) {
    QGraphicsView::setViewportMargins (left, top, right, bottom);
}

void GlyphView::setViewportMargins (const QMargins & margins) {
    QGraphicsView::setViewportMargins (margins);
}

void GlyphView::keyPressEvent (QKeyEvent * event) {
    GlyphScene *gsc = qobject_cast<GlyphScene *> (scene ());
    GVPaletteTool tool = gsc->activeTool ();

    if (event->key () & Qt::Key_Control && tool == GVPaletteTool::zoom) {
        QApplication::setOverrideCursor (QCursor (QPixmap (":/pixmaps/cursor-zoom-out.png")));
        return;
    }
    if (event->modifiers () & Qt::ControlModifier && !(event->modifiers () & Qt::ShiftModifier)) {
        uint16_t upm = m_context.glyph (outlinesType ())->upm ();
        switch (event->key ()) {
          case Qt::Key_Equal: case Qt::Key_Plus:
            doZoom (1.25);
            break;
          case Qt::Key_Minus:
            doZoom (.8);
            break;
          case Qt::Key_Left:
            doScroll ((int) -upm/20, true);
            break;
          case Qt::Key_Right:
            doScroll ((int) upm/20, true);
            break;
          case Qt::Key_Up:
            doScroll ((int) upm/20, false);
            break;
          case Qt::Key_Down:
            doScroll ((int) -upm/20, false);
            break;
          default:
            QGraphicsView::keyPressEvent (event);
        }
    } else
        QGraphicsView::keyPressEvent (event);
}

void GlyphView::keyReleaseEvent (QKeyEvent * event) {
    GlyphScene *gsc = qobject_cast<GlyphScene *> (scene ());
    GVPaletteTool tool = gsc->activeTool ();

    if (event->key () & Qt::Key_Control && tool == GVPaletteTool::zoom) {
        QApplication::restoreOverrideCursor ();
        return;
    }
}

void GlyphView::mousePressEvent (QMouseEvent *event) {
    GlyphScene *gsc = qobject_cast<GlyphScene *> (scene ());
    GVPaletteTool tool = gsc->activeTool ();
    bool has_ctrl = event->modifiers () & Qt::ControlModifier;

    if (tool == GVPaletteTool::zoom && event->button () == Qt::LeftButton)
        doZoom (has_ctrl ? 0.8 : 1.25);
    else
        QGraphicsView::mousePressEvent (event);
}

void GlyphView::toolSelected (QAction *action) {
    GVPaletteTool val = static_cast<GVPaletteTool> (action->data ().toUInt());
    GlyphScene *gsc = qobject_cast<GlyphScene *> (scene ());
    m_activeAction = action;
    gsc->setActiveTool (val);

    switch (val) {
      case (GVPaletteTool::pointer):
        QApplication::restoreOverrideCursor ();
        setDragMode (QGraphicsView::NoDrag);
        setCursor (QCursor (Qt::ArrowCursor));
      break;
      case (GVPaletteTool::hand):
        QApplication::restoreOverrideCursor ();
        setDragMode (QGraphicsView::ScrollHandDrag);
        setCursor (QCursor (Qt::OpenHandCursor));
      break;
      case (GVPaletteTool::knife):
        QApplication::restoreOverrideCursor ();
        setDragMode (QGraphicsView::NoDrag);
        setCursor (QCursor (QPixmap (":/pixmaps/palette-knife.png"), 5, 22));
      break;
      case (GVPaletteTool::zoom):
        setDragMode (QGraphicsView::NoDrag);
        setCursor (QCursor (QPixmap (":/pixmaps/cursor-zoom-in.png")));
      break;
      case (GVPaletteTool::corner):
        setDragMode (QGraphicsView::NoDrag);
        setCursor (QCursor (QPixmap (":/pixmaps/cursor-corner.png"), 7, 1));
      break;
      case (GVPaletteTool::curve):
        setDragMode (QGraphicsView::NoDrag);
        setCursor (QCursor (QPixmap (":/pixmaps/cursor-curve.png"), 7, 1));
      break;
      case (GVPaletteTool::tangent):
        setDragMode (QGraphicsView::NoDrag);
        setCursor (QCursor (QPixmap (":/pixmaps/cursor-tangent.png"), 7, 1));
      break;
      case (GVPaletteTool::ellipse):
      case (GVPaletteTool::rect):
        QApplication::restoreOverrideCursor ();
        setDragMode (QGraphicsView::NoDrag);
        setCursor (QCursor (Qt::ArrowCursor));
    }
}

void GlyphView::doScroll (int val, const bool is_x) {
    QScrollBar *sb = is_x ? horizontalScrollBar () : verticalScrollBar ();
    val *= is_x ? transform ().m11 () : transform ().m22 ();
    sb->setValue (sb->value () + val);
}

void GlyphView::doZoom (qreal val) {
    scale (val, val);
    QPoint zero_pos = mapFromScene (QPointF (0, 0));
    m_HorzRuler->setOrigin (zero_pos.x ());
    m_VertRuler->setOrigin (zero_pos.y ());
    m_HorzRuler->setRulerZoom (transform ().m11 ());
    m_VertRuler->setRulerZoom (transform ().m22 ());
}

void GlyphView::setRulerOrigin (const qreal pos, const bool is_x) {
    QDRuler *ruler = is_x ? m_HorzRuler : m_VertRuler;
    ruler->setOrigin (pos);
}

void GlyphView::setRulerZoom (const qreal val, const bool is_x) {
    QDRuler *ruler = is_x ? m_HorzRuler : m_VertRuler;
    ruler->setRulerZoom (val);
}

void GlyphView::scrolledHorizontally (int) {
    if (scene ()) {
	QPoint zero_pos = mapFromScene (QPointF (0, 0));
	m_HorzRuler->setOrigin (zero_pos.x ());
	scene ()->update ();
    }
    viewport ()->update ();
}

void GlyphView::scrolledVertically (int) {
    if (scene ()) {
	QPoint zero_pos = mapFromScene (QPointF (0, 0));
	m_VertRuler->setOrigin (zero_pos.y ());
	scene ()->update ();
    }
    viewport ()->update ();
}

uint16_t GlyphView::gid () {
    return m_context.gid ();
}

QString GlyphView::glyphName () {
    return m_context.name ();
}

GlyphContext& GlyphView::glyphContext () {
    return m_context;
}

uint16_t GlyphView::numSelectedPoints () {
    return m_context.numSelectedPoints ();
}

uint16_t GlyphView::numSelectedRefs () {
    QList<QGraphicsItem *> sellist = scene ()->selectedItems ();
    uint16_t i, ret=0;

    for (i=0; i<sellist.size (); i++) {
        if (sellist[i]->type () == RefItem::Type)
            ret++;
    }
    return ret;
}

uint16_t GlyphView::numSelectedFigs () {
    GlyphScene *gsc = qobject_cast<GlyphScene *> (scene ());
    QGraphicsItem *root = gsc->rootItem ();
    uint16_t ret=0;

    for (QGraphicsItem *child : root->childItems ()) {
	if (child->isPanel () && child->isSelected ())
            ret++;
    }
    return ret;
}

void GlyphView::updatePoints () {
    m_context.updatePoints ();
}

void GlyphView::updateFill () {
    m_context.updateFill ();
}

void GlyphView::switchOutlines (OutlinesType val) {
    GlyphScene *gsc = qobject_cast<GlyphScene *> (scene ());
    gsc->switchOutlines (val);
    m_figPal->setOutlinesType (val);
    m_context.switchOutlinesType (gsc->outlinesType (), true);
    m_context.clearScene ();
    ConicGlyph *g = m_context.glyph (gsc->outlinesType ());
    m_context.drawGlyph (g, g->gradients);
    m_figPal->setEnabled (outlinesType () == OutlinesType::SVG);
}

void GlyphView::on_switchOutlines (QAction *action) {
    OutlinesType val = static_cast<OutlinesType> (action->data ().toUInt ());
    switchOutlines (val);
}

void GlyphView::on_instrChanged () {
    if (outlinesType () == OutlinesType::TT) {
	ConicGlyph *g = m_context.glyph (OutlinesType::TT);
	g->instructions = m_instrEdit->data ();
	m_context.setGlyphChanged (true);
	emit (requestUpdateGridFit ());
	scene ()->update ();
    }
}

OutlinesType GlyphView::outlinesType () {
    GlyphScene *gsc = qobject_cast<GlyphScene *> (scene ());
    return gsc->outlinesType ();
}

// This one is triggered if user selects a figure in the palette
void GlyphView::setActiveFigure (const QItemSelection &selected, const QItemSelection &deselected) {
    Q_UNUSED (deselected);
    GlyphScene *gsc = qobject_cast<GlyphScene *> (scene ());
    disconnect (gsc, &GlyphScene::activePanelChanged, this, &GlyphView::onActiveFigureChange);
    if (!selected.indexes ().empty ()) {
	// NB: using reference here leads to crashes, as the list may be
	// already freed at some point
	const QModelIndex idx = selected.indexes ().at (0);
	int row = m_figMod->rowCount () - (idx.row () + 1);
	gsc->setActiveFigure (row);
	updatePoints ();
    }
    connect (gsc, &GlyphScene::activePanelChanged, this, &GlyphView::onActiveFigureChange);
}

// This one is triggered if user selects a figure on the scene
void GlyphView::onActiveFigureChange (const int idx) {
    disconnect (m_figPal->selectionModel (), &QItemSelectionModel::selectionChanged,
	this, &GlyphView::setActiveFigure);
    int inv_idx = m_figMod->rowCount () - (idx + 1);
    m_figPal->selectRow (inv_idx);
    updatePoints ();
    connect (m_figPal->selectionModel (), &QItemSelectionModel::selectionChanged,
	this, &GlyphView::setActiveFigure);
}

void GlyphView::onAddFigure (QGraphicsItem *item, const int pos) {
    // NB: when inserting items into stl containers iterator
    // should point to the next position
    int inv_pos = m_figMod->rowCount () - pos;
    m_figMod->addFigure (item, inv_pos);
    m_figPal->selectRow (inv_pos);
}

void GlyphView::onRemoveFigure (const int pos) {
    GlyphScene *gscene = qobject_cast<GlyphScene *> (scene ());
    int selpos = gscene->activePanelIndex ();
    int inv_pos = m_figMod->rowCount () - (pos + 1);
    int inv_selpos = m_figMod->rowCount () - (selpos + 1);
    m_figMod->removeFigure (inv_pos);
    if (inv_selpos >= inv_pos) {
	if (m_figMod->rowCount () > 0)
	    m_figPal->selectRow (inv_pos - 1);
    }
}

void GlyphView::onSwapPanels (const int pos1, const int pos2) {
    int inv_pos1 = m_figMod->rowCount () - (pos1 + 1);
    int inv_pos2 = m_figMod->rowCount () - (pos2 + 1);
    m_figMod->swapFigures (inv_pos2, inv_pos1);
}

void GlyphView::glyphRedrawn (OutlinesType otype, const int pidx) {
    m_figMod->reset (m_context.topItem (), m_context.glyph (otype));
    int inv_pidx = m_figMod->rowCount () - (pidx + 1);
    m_figPal->selectRow (inv_pidx);
}

void GlyphView::figurePropsChanged (QGraphicsItem *panel, const int pidx) {
    disconnect (m_figMod.get (), &QAbstractItemModel::dataChanged, this, &GlyphView::onFigurePaletteUpdate);
    int inv_pidx = m_figMod->rowCount () - (pidx + 1);
    m_figPal->selectRow (inv_pidx);
    FigureItem *ctrItem = dynamic_cast<FigureItem *> (panel);
    if (ctrItem) {
	DrawableFigure &fig = ctrItem->svgFigure ();
	m_figMod->setRowState (inv_pidx, fig.svgState);
	connect (m_figMod.get (), &QAbstractItemModel::dataChanged, this, &GlyphView::onFigurePaletteUpdate);
    }
}

void GlyphView::onFigurePaletteUpdate (const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles) {
    Q_UNUSED (bottomRight);
    Q_UNUSED (roles);
    QVariant rowdata = m_figMod->data (topLeft.siblingAtColumn (0), Qt::UserRole);
    SvgState state = rowdata.value<SvgState> ();
    int row = m_figMod->rowCount () - (topLeft.row () + 1);
    GlyphScene *gsc = qobject_cast<GlyphScene *> (scene ());

    disconnect (gsc, &GlyphScene::figurePropsChanged, this, &GlyphView::figurePropsChanged);
    FigurePropsChangeCommand *ucmd =
	new FigurePropsChangeCommand (m_context, outlinesType (), state, row);

    ConicGlyph *g = m_context.glyph (outlinesType ());
    auto it = g->figures.begin ();
    std::advance (it, row);
    auto &fig = *it;
    fig.svgState = state;

    m_context.updateFill ();
    m_context.render (outlinesType ());
    m_context.update (outlinesType ());
    m_context.undoGroup (true)->activeStack ()->push (ucmd);
    connect (gsc, &GlyphScene::figurePropsChanged, this, &GlyphView::figurePropsChanged);
}

NonExclusiveUndoGroup* GlyphView::undoGroup () {
    return m_context.undoGroup (true);
}

QAction* GlyphView::activeAction () {
    return m_activeAction;
}

GlyphScene::GlyphScene (sFont &fnt, FTWrapper &ftw, GlyphContext &gctx, OutlinesType gtype, QObject *parent) :
    QGraphicsScene (parent),
    m_font (fnt),
    m_ftWrapper (ftw),
    m_context (gctx),
    m_outlines_type (gtype),
    m_dragValid (false),
    m_grabber (nullptr),
    m_contextGrabber (nullptr),
    m_hasChanges (false),
    m_undoCmd (nullptr),
    m_rootItem (nullptr)
{
    m_activeTool = GVPaletteTool::pointer;
    m_knifeLine.setPen (QPen (Qt::darkGreen, 3, Qt::SolidLine));
    m_selectionRect.setPen (QPen (Qt::darkBlue, 3, Qt::DotLine));
    setSceneRect (QRectF (GV_MIN_X, GV_MIN_Y, GV_MAX_X, GV_MAX_Y));

    QFont awfnt = QFont ();
    awfnt.setStyleHint (QFont::SansSerif);
    awfnt.setPointSize (8);
    m_awValueItem = new QGraphicsSimpleTextItem ();
    m_awValueItem->setFont (awfnt);
    m_awValueItem->setFlag (QGraphicsItem::ItemIgnoresTransformations);
    addItem (m_awValueItem);

    scene_makePtCornerAction = new QAction (tr ("Make Point &Corner"), this);
    scene_makePtCurvedAction = new QAction (tr ("Make Point C&urved"), this);
    scene_makePtTangentAction = new QAction (tr ("Make Point &Tangent"), this);
    scene_makePtFirstAction = new QAction (tr ("Make Point &First"), this);
    scene_cutAction = new QAction (tr ("C&ut"), this);
    scene_copyAction = new QAction (tr ("&Copy"), this);
    scene_pasteAction = new QAction (tr ("&Paste"), this);
    scene_clearAction = new QAction (tr ("&Delete"), this);
    scene_mergeAction = new QAction (tr ("&Merge"), this);
    scene_pointPropsAction = new QAction (tr ("Point p&roperties"), this);
    scene_pointPropsAction->setEnabled (false);
    scene_refPropsAction = new QAction (tr ("Reference p&roperties"), this);
    scene_refPropsAction->setEnabled (false);

    connect (scene_makePtCornerAction, &QAction::triggered, this, &GlyphScene::ptCornerRequest);
    connect (scene_makePtCurvedAction, &QAction::triggered, this, &GlyphScene::ptCurvedRequest);
    connect (scene_makePtTangentAction, &QAction::triggered, this, &GlyphScene::ptTangentRequest);
    connect (scene_makePtFirstAction, &QAction::triggered, this, &GlyphScene::setSelPointFirst);
    connect (scene_copyAction, &QAction::triggered, this, &GlyphScene::copyRequest);
    connect (scene_cutAction, &QAction::triggered, this, &GlyphScene::cutRequest);
    connect (scene_pasteAction, &QAction::triggered, this, &GlyphScene::doPaste);
    connect (scene_clearAction, &QAction::triggered, this, &GlyphScene::clearRequest);
    connect (scene_mergeAction, &QAction::triggered, this, &GlyphScene::doMerge);
}

static void show_hint (QPainter *painter, const QRectF &exposed, const StemInfo &stem, bool is_v) {
    static QColor v_color (127, 127, 255, 127);
    static QColor h_color (140, 190, 140, 127);
    static QColor v_fill_color (190, 190, 255, 127);
    static QColor h_fill_color (160, 210, 160, 127);
    QPen s_pen (is_v ? v_color : h_color, 3, Qt::DashLine);
    QPen e_pen (is_v ? v_color : h_color, 3, Qt::DotLine);
    double l = exposed.right () - exposed.left ();
    double h = exposed.bottom () - exposed.top ();
    double start = (stem.width == -21) ? stem.start + stem.width : stem.start;
    double end = (stem.width == -21) ? stem.start : stem.start + stem.width;
    static const int pad = 2;

    QPointF grad_start (is_v ? stem.start : 0, is_v ? 0 : stem.start);
    QPointF grad_stop  (is_v ? stem.start+stem.width : 0, is_v ? 0 : stem.start+stem.width);
    QLinearGradient grad (grad_start, grad_stop);

    QFont fnt = QFont ();
    fnt.setStyleHint (QFont::SansSerif);
    fnt.setPointSize (12);
    QFontMetrics fm (fnt, painter->device ());
    int fh = fm.boundingRect ("9999").height () + pad;
    int fw;

    if (stem.width > 0) {
	grad.setColorAt (0, is_v ? v_fill_color : h_fill_color);
	grad.setColorAt (0.5, QColor (255, 255, 255, 0));
	grad.setColorAt (1, is_v ? v_color : h_color);
    } else if (stem.width == -20) {
	grad.setColorAt (0, is_v ? v_fill_color : h_fill_color);
	grad.setColorAt (1, QColor (255, 255, 255, 0));
    } else if (stem.width == -21) {
	grad.setColorAt (1, is_v ? v_fill_color : h_fill_color);
	grad.setColorAt (0, QColor (255, 255, 255, 0));
    }

    if (is_v) {
	painter->setPen (s_pen);
	painter->drawLine (QLineF (start, exposed.top (), start, exposed.bottom ()));
    	painter->fillRect (stem.start, exposed.top (), stem.width, h, grad);
	if (stem.width > 0) {
	    painter->setPen (e_pen);
	    painter->drawLine (QLineF (end, exposed.top (), end, exposed.bottom ()));
	}

	painter->scale (1, -1);
	painter->setFont (fnt);
	fw = fm.boundingRect (QString ("%1").arg (stem.start)).width () + pad;
	QPointF startpos (stem.start - fw - pad, -exposed.top () - pad);
	painter->drawText (startpos, QString ("%1").arg (stem.start));
	QPointF wpos (stem.start + stem.width + pad, -exposed.bottom () + RULER_BREADTH);
	painter->drawText (wpos, QString ("%1").arg (stem.width));
	painter->scale (1, -1);
    } else {
	painter->setPen (s_pen);
	painter->drawLine (QLineF (exposed.left (), start, exposed.right (), start));
    	painter->fillRect (exposed.left (), stem.start, l, stem.width, grad);
	if (stem.width > 0) {
	    painter->setPen (e_pen);
	    painter->drawLine (QLineF (exposed.left (), end, exposed.right (), end));
	}

	painter->scale (1, -1);
	painter->setFont (fnt);
	QPointF startpos (exposed.left () + RULER_BREADTH, -stem.start + fh);
	painter->drawText (startpos, QString ("%1").arg (stem.start));
	fw = fm.boundingRect (QString ("%1").arg (stem.width)).width () + pad;
	QPointF wpos (exposed.right () - fw, -stem.start - stem.width - fh/2);
	painter->drawText (wpos, QString ("%1").arg (stem.width));
	painter->scale (1, -1);
    }
}

static void draw_gridFittedBitmap (QPainter *p, ConicGlyph *g, freetype_raster &r, int ppemX, int ppemY) {
    static QPen whitePen (Qt::white, 1);
    static QPen melrosePen (QColor (0xb0, 0xb0, 0xff), 3);
    if (!r.bitmap.empty ()) {
	p->setPen (whitePen);
	double px_size_x = static_cast<double> (g->upm ())/ppemX;
	double px_size_y = static_cast<double> (g->upm ())/ppemY;
	int start_x = r.lb * px_size_x;
	int start_y = (r.as-1) * px_size_y;
	int pos = 0;

	QVector<QBrush> grays;
	grays.reserve (r.num_grays);
	unsigned int bgr=127, bgg=127, bgb=127;
	unsigned int shift=0, mask=0, rem_grays=r.num_grays;
	while (rem_grays>1) {
	    rem_grays /= 2;
	    shift++;
	    mask = mask << 1;
	    mask |= 1;
	}

        for (int i=0; i<r.num_grays; i++) {
            grays.append (QBrush (QColor (
		255 - i*bgr/(r.num_grays-1),
		255 - i*bgg/(r.num_grays-1),
		255 - i*bgb/(r.num_grays-1)
            ), Qt::SolidPattern));
        }

	for (int i=0; i<r.rows; i++) {
	    pos = r.bytes_per_row*i;
	    int next = pos + r.bytes_per_row;
	    int idx = 0;
	    while (idx < r.cols && pos < next) {
		uint8_t b = r.bitmap[pos];
		uint8_t bits_rem = 8;
		while (idx < r.cols && bits_rem > 0) {
		    bits_rem -= shift;
		    uint8_t ccode = (b>>bits_rem)&mask;
		    p->setBrush (grays[ccode]);
		    p->drawRect (start_x + (px_size_x*idx), start_y - (px_size_y*i), px_size_x, px_size_y);
		    idx++;
		}
		pos++;
	    }
	}
	// Mark centres of the pixels
	p->setPen (melrosePen);
	for (int i=0; i<r.rows; i++) {
	    for (int j=1; j<=r.cols; j++) {
		int cx = start_x + (px_size_x*j) - (px_size_x/2);
		int cy = start_y - (px_size_y*i) + (px_size_y/2);
		p->drawLine (cx-6, cy, cx+6, cy);
		p->drawLine (cx, cy-6, cx, cy+6);
	    }
	}
    }
}

void GlyphScene::drawBackground (QPainter *painter, const QRectF &exposed) {
    static QPen blackPen (Qt::black, 1);
    static QPen bluePen (Qt::blue, 1);
    static QPen greenPen (Qt::darkGreen, 1);
    static QPen noPen (Qt::NoPen);

    if (m_outlines_type == OutlinesType::TT && GlyphViewContainer::showGridFit () && m_ftWrapper.hasFace ()) {
	QPainterPath p;
	QSettings settings (QCoreApplication::organizationName (), QCoreApplication::applicationName ());
	bool mono = settings.value ("glyphview/GridFit/monochrome", false).toBool ();
	unsigned int ppemX = settings.value ("glyphview/GridFit/ppemX", 22).toUInt ();
	unsigned int ppemY = settings.value ("glyphview/GridFit/ppemY", 22).toUInt ();

	uint16_t ft_flags = FT_LOAD_RENDER | FT_LOAD_NO_BITMAP | FT_LOAD_NO_AUTOHINT;
	if (mono) {
	    ft_flags |= FT_LOAD_MONOCHROME;
	    ft_flags |= FT_LOAD_TARGET_MONO;
	} else {
	    ft_flags |= FT_LOAD_TARGET_NORMAL;
	}

	if (!m_ftWrapper.setPixelSize (ppemX, ppemY)) {
	    freetype_raster r = m_ftWrapper.gridFitGlyph (m_context.gid (), ft_flags, &p);

	    if (r.valid) {
		draw_gridFittedBitmap (painter, m_context.glyph (m_outlines_type), r, ppemX, ppemY);

		painter->setPen (greenPen);
		painter->setBrush (QBrush (Qt::NoBrush));
		double upm = m_context.glyph (m_outlines_type)->upm ();
		double xscale = upm/(ppemX*64);
		double yscale = upm/(ppemY*64);
		QTransform cur_trans = painter->worldTransform ();
		QTransform save_trans = cur_trans;
		painter->scale (xscale, yscale);
		painter->drawPath (p);
		painter->setWorldTransform (save_trans);

		double aw_scaled = r.advance * xscale;
		painter->drawLine (QLineF (aw_scaled, exposed.top (), aw_scaled, exposed.bottom ()));
	    }
	}
    }

    painter->setPen (blackPen);
    painter->drawLine (QLineF (exposed.left (), 0, exposed.right (), 0));
    painter->drawLine (QLineF (0, exposed.top (), 0, exposed.bottom ()));

    painter->setPen (bluePen);
    painter->drawLine (QLineF (exposed.left (), m_font.ascent, exposed.right (), m_font.ascent));
    painter->drawLine (QLineF (exposed.left (), m_font.descent, exposed.right (), m_font.descent));

    if (m_outlines_type == OutlinesType::PS) {
	const PrivateDict *pd = m_context.glyph (m_outlines_type)->privateDict ();
	double l = exposed.right () - exposed.left ();
	painter->setPen (noPen);

	if (GlyphViewContainer::showBlues ()) {
	    static QBrush blueBrush (QColor (127, 127, 255, 64), Qt::Dense5Pattern);
	    painter->setBrush (blueBrush);
	    if (pd->has_key (cff::BlueValues)) {
		const private_entry &blues = pd->get (cff::BlueValues);
		for (int i=1; i<16 && blues.list[i].valid; i+=2) {
		    double h = blues.list[i].base - blues.list[i-1].base;
		    painter->drawRect (exposed.left (), blues.list[i-1].base, l, h);
		}
	    }
	    if (pd->has_key (cff::OtherBlues)) {
		const private_entry &blues = pd->get (cff::OtherBlues);
		for (int i=1; i<16 && blues.list[i].valid; i+=2) {
		    double h = blues.list[i].base - blues.list[i-1].base;
		    painter->drawRect (exposed.left (), blues.list[i-1].base, l, h);
		}
	    }
	}
	if (GlyphViewContainer::showFamilyBlues ()) {
	    static QBrush familyBrush (QColor (255, 112, 112, 64), Qt::Dense5Pattern);
	    painter->setBrush (familyBrush);
	    if (pd->has_key (cff::FamilyBlues)) {
		const private_entry &blues = pd->get (cff::FamilyBlues);
		for (int i=1; i<16 && blues.list[i].valid; i+=2) {
		    double h = blues.list[i].base - blues.list[i-1].base;
		    painter->drawRect (exposed.left (), blues.list[i-1].base, l, h);
		}
	    }
	    if (pd->has_key (cff::FamilyOtherBlues)) {
		const private_entry &blues = pd->get (cff::FamilyOtherBlues);
		for (int i=1; i<16 && blues.list[i].valid; i+=2) {
		    double h = blues.list[i].base - blues.list[i-1].base;
		    painter->drawRect (exposed.left (), blues.list[i-1].base, l, h);
		}
	    }
	}
	if (GlyphViewContainer::showHints ()) {
	    for (auto &stem: m_context.glyph (m_outlines_type)->hstem) {
		show_hint (painter, exposed, stem, false);
	    }
	    for (auto &stem: m_context.glyph (m_outlines_type)->vstem) {
		show_hint (painter, exposed, stem, true);
	    }
	}
    }
}

void GlyphScene::drawForeground (QPainter *, const QRectF &exposed) {
    ConicGlyph *g = m_context.glyph (m_outlines_type);
#if 0
    const QFont &f = m_awValueItem->font ();
    QFontMetrics fm (f);
    int h = fm.boundingRect ("000").height ();
#endif
    QPointF pos (g->advanceWidth () + 4, exposed.bottom () - 4);

    // NB: need an update at this point (otherwise artefacts are produced when
    // scrolling), but calling update () or invalidate () here will
    // hang everything if there are more than one GlyphView windows. Calling
    // viewport ()->update () from view->scrolled{Horizontally|Vertically} instead
    // seems to fix the problem

    // Don't take reference shift into account here, as label is shifted together
    // with the whole group
    m_awValueItem->setText (QString::number (g->advanceWidth ()));
    m_awValueItem->setPos (pos);
}

void GlyphScene::setRootItem (QGraphicsItem *root) {
    addItem (root);
    m_rootItem = root;
}

QGraphicsItem *GlyphScene::rootItem () {
    return m_rootItem;
}

void GlyphScene::notifyPanelAdded (QGraphicsItem *item) {
    int pos = 0;
    for (QGraphicsItem *testItem : m_rootItem->childItems ()) {
	if (testItem->isPanel ()) {
	    if (testItem == item)
		break;
	    pos++;
	}
    }
    emit (panelAdded (item, pos));
}

void GlyphScene::notifyPanelRemoved (QGraphicsItem *item) {
    int pos = 0;
    for (QGraphicsItem *testItem : m_rootItem->childItems ()) {
	if (testItem->isPanel ()) {
	    if (testItem == item)
		break;
	    pos++;
	}
    }
    emit (panelRemoved (pos));
}

void GlyphScene::notifyPanelsSwapped (int pos1, int pos2) {
    emit (panelsSwapped (pos1, pos2));
}

void GlyphScene::notifyGlyphRedrawn () {
    emit (glyphRedrawn (m_outlines_type, activePanelIndex ()));
}

void GlyphScene::notifyFigurePropsChanged (int pidx) {
    int pos = 0;
    QGraphicsItem *item = nullptr;
    for (QGraphicsItem *testItem : m_rootItem->childItems ()) {
	if (testItem->isPanel ()) {
	    if (pos == pidx) {
		item = testItem;
		break;
	    }
	    pos++;
	}
    }
    emit (figurePropsChanged (item, pidx));
}

int GlyphScene::activePanelIndex () {
    int pos = 0;
    for (QGraphicsItem *testItem : m_rootItem->childItems ()) {
	if (testItem->isPanel ()) {
	    if (testItem->isActive ())
		return pos;
	    pos++;
	}
    }
    return -1;
}

void GlyphScene::setActiveFigure (QGraphicsItem *item) {
    int pos = 0;

    for (QGraphicsItem *testItem : m_rootItem->childItems ()) {
        if (testItem->isPanel ()) {
	    if (testItem == item) {
		setActivePanel (item);
		emit activePanelChanged (pos);
		break;
	    }
	    pos++;
        }
    }
    update ();
}

void GlyphScene::setActiveFigure (const int idx) {
    int pos = 0;
    for (QGraphicsItem *testItem : m_rootItem->childItems ()) {
        if (testItem->isPanel ()) {
	    if (pos == idx) {
		emit activePanelChanged (pos);
		setActivePanel (testItem);
		break;
	    }
	    pos++;
        }
    }
    update ();
}

void GlyphScene::checkMovable (GlyphChangeCommand *ucmd) {
    QList<QGraphicsItem *> sellist = selectedItems ();
    int pt_cnt=0, ref_cnt=0, fig_cnt=0;
    bool has_aw=false;

    for (const QGraphicsItem *item : sellist) {
	switch (item->type ()) {
	  case OnCurvePointItem::Type:
            pt_cnt++;
	    break;
	  case RefItem::Type:
            ref_cnt++;
	    break;
	  case FigureEllipseItem::Type:
	  case FigureRectItem::Type:
	  case FigurePathItem::Type:
	    fig_cnt++;
	    break;
          case AdvanceWidthItem::Type:
            has_aw = true;
	}
    }

    if (pt_cnt && !ref_cnt & !fig_cnt && !has_aw)
        ucmd->setText (pt_cnt > 1 ? tr ("Move Points") : tr ("Move Point"));
    else if (!pt_cnt && !fig_cnt && ref_cnt && !has_aw)
        ucmd->setText (ref_cnt > 1 ? tr ("Move References") : tr ("Move Reference"));
    else if (!pt_cnt && !ref_cnt && fig_cnt && !has_aw)
        ucmd->setText (ref_cnt > 1 ? tr ("Move Figures") : tr ("Move Figure"));
    else if (!pt_cnt && !ref_cnt & !fig_cnt && has_aw)
        ucmd->setText (tr ("Change Advance Width"));
    else
        ucmd->setText (tr ("Move Elements"));
}

void GlyphScene::mouseDoubleClickEvent (QGraphicsSceneMouseEvent *event) {
    QGraphicsScene::mouseDoubleClickEvent (event);
    switch (m_activeTool) {
      case (GVPaletteTool::pointer): {
        m_grabber = mouseGrabberItem ();
        if (event->button () == Qt::LeftButton && m_grabber) {
            if (m_grabber->type () == OnCurvePointItem::Type ||
		m_grabber->type () == OffCurvePointItem::Type) {
                auto *baseItem = qgraphicsitem_cast<ConicPointItem *> (m_grabber->parentItem ());
		m_context.selectPointContour (baseItem);

		m_dragValid = true;
		m_hasChanges = false;
		GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, m_outlines_type);

		// NB: don't set m_startDragPos to the position of the clicked item:
		// this leads to tiny positioning errors when dragging
		m_startDragPos = event->scenePos ();
		m_prevDragPos = m_startDragPos;
		m_origItemPos = QPointF (0, 0);
		checkMovable (ucmd);
		emit mousePointerMoved (m_grabber->scenePos ());
		m_undoCmd = ucmd;
	    }
	}
      }
      default:
	;
    }
}

void GlyphScene::mousePressEvent (QGraphicsSceneMouseEvent *event) {
    QGraphicsView *view = event->widget () ?
	qobject_cast<QGraphicsView *> (event->widget ()->parentWidget ()) : nullptr;

    // Basically the right click is handled in ::contextMenuEvent, but need to
    // know the item under mouse. However, can't call the default implementation,
    // as it sets the mouseGrabberItem, but also unselects everything selected.
    // Also, the mouse should then be ungrabbed, as otherwise the mouseGrabberItem
    // may interfere with subsequent left mouse clicks, which produces really strange
    // results. So use a class variable (separare from the normal m_grabber)
    // especially to store the last item clicked with the right mouse button
    if (event->button () == Qt::RightButton) {
	m_contextGrabber = itemAt (event->scenePos (), view ? view->transform () : QTransform ());
	if (m_contextGrabber) {
	    QGraphicsItem *parent = m_contextGrabber;
	    // for some reason QGraphicsItem::group always returns nullptr
	    while (parent->parentItem ()) {
		parent = parent->parentItem ();
		if (parent->type () == RefItem::Type) {
		    m_contextGrabber = parent;
		    break;
		}
	    }
	}
	return;
    }
    switch (m_activeTool) {
      case (GVPaletteTool::pointer): {
        // Call the standard implementation first, as otherwise grabber would be nullptr
        QGraphicsScene::mousePressEvent (event);

        m_grabber = mouseGrabberItem ();
	if (m_grabber && m_grabber->isActive () && m_grabber->type () == FigurePathItem::Type)
	    m_grabber = nullptr;
        if (event->button () == Qt::LeftButton && m_grabber) {
	    // Default implementation does the following on mouse release, but, unfortunately,
	    // it also unselects everything we may have selected with a double click
	    // previously. So let's do it here.
	    if (event->modifiers () & Qt::ControlModifier && !m_grabber->isSelected ())
		m_grabber->setSelected (true);
            m_dragValid = true;
            m_hasChanges = false;
            GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, m_outlines_type);

            // NB: don't set m_startDragPos to the position of the clicked item:
            // this leads to tiny positioning errors when dragging
            m_startDragPos = event->scenePos ();
            m_prevDragPos = m_startDragPos;
            m_origItemPos = QPointF (0, 0);
            if (m_grabber->type () == OffCurvePointItem::Type) {
                m_origItemPos = m_grabber->pos ();
                ucmd->setText (tr ("Move Control Point"));
            } else {
                QList<QGraphicsItem *> sellist = selectedItems ();
		checkMovable (ucmd);
            }

	    emit mousePointerMoved (m_grabber->scenePos ());
            m_undoCmd = ucmd;
        } else if (event->button () == Qt::LeftButton) {
	    m_selectionRect.setPos (event->scenePos ()),
	    m_selectionRect.setRect (0, 0, 0, 0);
	    m_dragValid = true;
	    addItem (&m_selectionRect);
	}
      } break;
      case (GVPaletteTool::corner):
      case (GVPaletteTool::curve):
      case (GVPaletteTool::tangent): {
        if (event->button () == Qt::LeftButton) {
            GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, m_outlines_type);
            ucmd->setText (tr ("Add Point"));
            m_undoCmd = ucmd;

            enum pointtype ptype = (m_activeTool == GVPaletteTool::corner) ? pt_corner :
                (m_activeTool == GVPaletteTool::curve) ? pt_curve : pt_tangent;
            QPointF pos = event->scenePos ();
            OnCurvePointItem *newItem = m_context.addPoint (pos, ptype);

            m_dragValid = true;
            m_hasChanges = true;
            m_startDragPos = m_prevDragPos = pos;
            m_prevDragPos = m_startDragPos;
            m_origItemPos = QPointF (0, 0);

            clearSelection ();
            newItem->setSelected (true);
            newItem->grabMouse ();
	    emit mousePointerMoved (pos);
        }
      } break;
      case (GVPaletteTool::knife):
        if (event->button () == Qt::LeftButton) {
            GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, m_outlines_type);
            ucmd->setText (tr ("Cut splines in two"));
            m_undoCmd = ucmd;

            m_startDragPos = event->scenePos ();
	    m_knifeLine.setPos (m_startDragPos);
	    addItem (&m_knifeLine);
	}
      break;
      case (GVPaletteTool::ellipse):
        if (event->button () == Qt::LeftButton) {
            GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, m_outlines_type);
            ucmd->setText (tr ("Add ellipse"));
            m_undoCmd = ucmd;

            m_startDragPos = event->scenePos ();
	    m_addEllipse = std::unique_ptr<QGraphicsEllipseItem> (new QGraphicsEllipseItem ());
	    m_addEllipse->setPos (m_startDragPos);
	    m_addEllipse->setRect (0, 0, 0, 0);

	    addItem (m_addEllipse.get ());
	}
      break;
      case (GVPaletteTool::rect):
        if (event->button () == Qt::LeftButton) {
            GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, m_outlines_type);
            ucmd->setText (tr ("Add rectangle"));
            m_undoCmd = ucmd;

            m_startDragPos = event->scenePos ();
	    m_addRect = std::unique_ptr<QGraphicsRectItem> (new QGraphicsRectItem ());
	    m_addRect->setPos (m_startDragPos);
	    m_addRect->setRect (0, 0, 0, 0);

	    addItem (m_addRect.get ());
	}
      break;
      // Remaining tools (i.e. hand and zoom) are handled by GlyphView
      default:
          ;
    }
}

static int signnum_typical (double x) {
    if (x > 0.0) return 1;
    if (x < 0.0) return -1;
    return 0;
}

void GlyphScene::mouseMoveEvent (QGraphicsSceneMouseEvent *event) {
    // Used to check mouse grabber item inside this routine, but this
    // may return a recently selected item which we have no intention to move.
    // So use a class level variable instead, set in mousePressEvent and
    // unset in mouseReleaseEvent

    switch (m_activeTool) {
      case (GVPaletteTool::pointer):
      case (GVPaletteTool::corner):
      case (GVPaletteTool::curve):
      case (GVPaletteTool::tangent): {
        if (!m_dragValid) {
            QGraphicsScene::mouseMoveEvent (event);
            return;
        } else if (!m_grabber) {
	    QPointF pos = m_selectionRect.scenePos ();
	    QRectF area = QRectF (0, 0,
		event->scenePos ().x () - pos.x (), event->scenePos ().y () - pos.y ()
	    ).normalized ();
	    m_selectionRect.setRect (area);
	    QList<QGraphicsItem *> selectable =
		items (m_selectionRect.mapToScene (area), Qt::ContainsItemShape);
	    for (auto item: items ()) {
		if (item->type () == OnCurvePointItem::Type ||
		    item->type () == RefItem::Type ||
		    //item->type () == FigurePathItem::Type ||
		    item->type () == FigureEllipseItem::Type ||
		    item->type () == FigureRectItem::Type) {
		    item->setSelected (selectable.contains (item));
		}
	    }
	    // Need this to avoid undesired artefacts left by the selection rectangle
	    // on the background items (e. g. blue zones).
	    update ();
	    return;
	}

        QPointF corr = QPointF (0, 0), shift;
        // NB: m_origItemPos is (0, 0) unless the grabber item is an offcurve point
        if (event->modifiers () & Qt::ShiftModifier) {
            corr = m_startDragPos - m_prevDragPos;
            m_prevDragPos = event->scenePos ();
            qreal dx = fabs (m_startDragPos.x () - m_origItemPos.x () - m_prevDragPos.x ());
            qreal dy = fabs (m_startDragPos.y () - m_origItemPos.y () - m_prevDragPos.y ());
            if (dx > dy)
                m_prevDragPos.setY (m_startDragPos.y () - m_origItemPos.y ());
            else
                m_prevDragPos.setX (m_startDragPos.x () - m_origItemPos.x ());
            shift = m_prevDragPos - m_startDragPos;
        } else {
            shift = event->scenePos () - m_prevDragPos;
            m_prevDragPos = event->scenePos ();
        }
        m_hasChanges = true;

        // Check if there is an offcurve point in focus: if so, then move just this
        // point relatively to its base and return. It would be too difficult (and probably
        // impossible) to resolve all situations which might occur if we have started
        // moving both offcurve and oncurve points or several offcurve points at one operation
        if (m_grabber->type () == OffCurvePointItem::Type) {
            OffCurvePointItem *grabItem = qgraphicsitem_cast<OffCurvePointItem *> (m_grabber);
            ConicPointItem *baseItem = qgraphicsitem_cast<ConicPointItem *> (m_grabber->parentItem ());
            QPointF move = baseItem->pos () + grabItem->pos () + corr + shift;
            baseItem->controlPointMoved (move, grabItem->isNextCP ());
            return;
        } else if (m_grabber->type () == ManipulatorItem::Type) {
            ManipulatorItem *grabItem = qgraphicsitem_cast<ManipulatorItem *> (m_grabber);
            QGraphicsItem *baseItem = m_grabber->parentItem ();
	    if (baseItem->type () == FigureEllipseItem::Type) {
		QPointF move = corr + shift;
		FigureEllipseItem *ellipse = qgraphicsitem_cast<FigureEllipseItem *> (baseItem);
		ellipse->manipulatorMoved (move, grabItem);
	    } else if (baseItem->type () == FigureRectItem::Type) {
		QPointF move = corr + shift;
		FigureRectItem *rect = qgraphicsitem_cast<FigureRectItem *> (baseItem);
		rect->manipulatorMoved (move, grabItem);
	    }
            update ();
	    return;
	}

        // If no undo command present, then we are probably selecting rather than draggig items
	if (m_undoCmd) {
	    // Proceed to oncurve points: if there are several selected, move them all at once
	    QList<QGraphicsItem *> sellist = selectedItems ();

	    for (int i=0; i<sellist.size (); i++) {
		switch (sellist[i]->type ()) {
		  case OnCurvePointItem::Type: {
		    OnCurvePointItem *curItem = qgraphicsitem_cast<OnCurvePointItem *> (sellist[i]);
		    ConicPointItem *baseItem = qgraphicsitem_cast<ConicPointItem *> (curItem->parentItem ());
		    baseItem->basePointMoved (baseItem->pos () + corr + shift);
		  } break;
		  case RefItem::Type: {
		    RefItem *refItem = qgraphicsitem_cast<RefItem *> (sellist[i]);
		    refItem->refMoved (corr + shift);
		  } break;
		  case FigureEllipseItem::Type:
		  case FigureRectItem::Type:
		  case FigurePathItem::Type: {
		    FigureItem *elItem = dynamic_cast<FigureItem *> (sellist[i]);
		    elItem->moved (corr + shift);
		  } break;
		  case AdvanceWidthItem::Type: {
		    qreal newx = sellist[i]->pos ().x () + shift.x ();
		    sellist[i]->setPos (QPointF (newx, 0));
		    m_context.setAdvanceWidth (round (newx));
		  }
		}
	    }
	    m_context.joinSplines (true, 3.5);
	}
      } break;
      case (GVPaletteTool::knife):
	if (event->buttons () & Qt::LeftButton) {
	    m_knifeLine.setLine (0, 0,
		event->scenePos ().x () - m_startDragPos.x (),
		event->scenePos ().y () - m_startDragPos.y ()
	    );
	}
      break;
      case (GVPaletteTool::ellipse):
      case (GVPaletteTool::rect):
	if (m_undoCmd && event->buttons () & Qt::LeftButton) {
	    QPointF shift = event->scenePos () - m_startDragPos;
	    if (event->modifiers () & Qt::ShiftModifier) {
		if (std::abs (shift.x ()) > std::abs (shift.y ()))
		    shift.setY (signnum_typical (shift.y ()) * std::abs (shift.x ()));
		else if (std::abs (shift.y ()) > std::abs (shift.x ()))
		    shift.setX (signnum_typical (shift.x ()) * std::abs (shift.y ()));
	    }
	    if (m_activeTool == GVPaletteTool::ellipse)
		m_addEllipse->setRect (0, 0, shift.x (), shift.y ());
	    else
		m_addRect->setRect (0, 0, shift.x (), shift.y ());
	}
      break;
      default:
        ;
    }
    update ();
}

void GlyphScene::mouseReleaseEvent (QGraphicsSceneMouseEvent *event) {
    m_grabber = nullptr;
    switch (m_activeTool) {
      case (GVPaletteTool::pointer):
        if (event->button () == Qt::LeftButton && m_dragValid) {
            m_dragValid = false;
	    if (m_selectionRect.scene ()) {
		removeItem (&m_selectionRect);
		m_selectionRect.setRect (QRectF ());
	    }
	    update ();
	}
      break;
      case (GVPaletteTool::corner):
      case (GVPaletteTool::curve):
      case (GVPaletteTool::tangent):
        if (event->button () == Qt::LeftButton && m_dragValid) {
            mouseGrabberItem ()->ungrabMouse ();
            m_dragValid = false;
        }
      break;
      case (GVPaletteTool::knife):
        if (event->button () == Qt::LeftButton) {
	    m_hasChanges = m_context.cutSplines (
		m_knifeLine.mapToScene (m_knifeLine.line ().p1 ()),
		m_knifeLine.mapToScene (m_knifeLine.line ().p2 ())
	    );
	    removeItem (&m_knifeLine);
	    m_knifeLine.setLine (QLineF ());
	}
      break;
      case (GVPaletteTool::ellipse):
	if (m_addEllipse) {
	    removeItem (m_addEllipse.get ());
	    QPointF tl = m_addEllipse->mapToScene (m_addEllipse->rect ().topLeft ());
	    QPointF br = m_addEllipse->mapToScene (m_addEllipse->rect ().bottomRight ());
	    m_context.addEllipse (QRectF (tl, br));
	    m_addEllipse.reset ();
	    m_hasChanges = true;
	}
      break;
      case (GVPaletteTool::rect):
	if (m_addRect) {
	    removeItem (m_addRect.get ());
	    QPointF tl = m_addRect->mapToScene (m_addRect->rect ().topLeft ());
	    QPointF br = m_addRect->mapToScene (m_addRect->rect ().bottomRight ());
	    m_context.addRect (QRectF (tl, br));
	    m_addRect.reset ();
	    m_hasChanges = true;
	}
      break;
      default:
	// NB: the default implementation causes everything but items in the
	// selection area to be unselected. We don't want this if we have
	// for example just selected an entire contour with a double click.
	if (event->button () == Qt::LeftButton)
            QGraphicsScene::mouseReleaseEvent (event);
    }
    if (m_undoCmd) {
        if (m_hasChanges) {
            m_context.render (m_outlines_type);
            m_context.undoGroup (true)->activeStack ()->push (m_undoCmd);
            m_context.update (m_outlines_type);
            m_hasChanges = false;
        } else {
	    delete m_undoCmd;
	}
        m_undoCmd = nullptr;
    }
}

void GlyphScene::keyPressEvent (QKeyEvent * event) {
    switch (event->key ()) {
      case Qt::Key_Left:
        moveSelected (QPointF (-1, 0));
        break;
      case Qt::Key_Right:
        moveSelected (QPointF (1, 0));
        break;
      case Qt::Key_Up:
        moveSelected (QPointF (0, 1));
        break;
      case Qt::Key_Down:
        moveSelected (QPointF (0, -1));
        break;
      default:
        QGraphicsScene::keyPressEvent (event);
    }
}

void GlyphScene::checkSelection () {
    int num_pts = m_context.numSelectedPoints ();
    int num_refs = numSelectedRefs ();
    int num_figs = numSelectedFigs ();

    scene_makePtCornerAction->setEnabled (num_pts);
    scene_makePtCurvedAction->setEnabled (num_pts);
    scene_makePtTangentAction->setEnabled (num_pts);
    scene_makePtFirstAction->setEnabled (num_pts == 1);

    scene_cutAction->setEnabled (num_pts + num_refs + num_figs);
    scene_copyAction->setEnabled (num_pts + num_refs + num_figs);
    scene_clearAction->setEnabled (num_pts + num_refs + num_figs);
    scene_mergeAction->setEnabled (num_pts);

    QClipboard *clipboard = QApplication::clipboard ();
    const QMimeData *md = clipboard->mimeData ();
    scene_pasteAction->setEnabled (md->hasFormat ("image/svg+xml"));
}

void GlyphScene::contextMenuEvent (QGraphicsSceneContextMenuEvent *event) {
    QMenu menu (event->widget ());
    QObject::connect (&menu, &QMenu::aboutToShow, this, &GlyphScene::checkSelection);

    menu.addAction (scene_makePtCornerAction);
    menu.addAction (scene_makePtCurvedAction);
    menu.addAction (scene_makePtTangentAction);
    menu.addAction (scene_makePtFirstAction);
    menu.addSeparator ();
    menu.addAction (scene_cutAction);
    menu.addAction (scene_copyAction);
    menu.addAction (scene_pasteAction);
    menu.addAction (scene_mergeAction);

    if (m_contextGrabber) {
	switch (m_contextGrabber->type ()) {
	  case OnCurvePointItem::Type:
	    menu.addSeparator ();
	    menu.addAction (scene_pointPropsAction);
	    break;
	  case RefItem::Type:
	    menu.addSeparator ();
	    menu.addAction (scene_refPropsAction);
	    break;
	}
    }
    menu.exec (event->screenPos ());
}

void GlyphScene::setActiveTool (GVPaletteTool active) {
    m_activeTool = active;
}

GVPaletteTool GlyphScene::activeTool () {
    return m_activeTool;
}

void GlyphScene::moveSelected (QPointF move) {
    QList<QGraphicsItem *> sellist = selectedItems ();
    int i;
    MoveCommand *moveCmd = new MoveCommand (move, m_context, m_outlines_type);
    bool cp_moved = false, changed=false;

    for (i=0; i<sellist.size (); i++) {
        if (sellist[i]->type () == OffCurvePointItem::Type) {
            OffCurvePointItem *item = qgraphicsitem_cast<OffCurvePointItem *> (sellist[i]);
            ConicPointItem *baseItem = qgraphicsitem_cast<ConicPointItem *> (item->parentItem ());
            QPointF newPos = baseItem->pos () + item->pos () + move;

            baseItem->controlPointMoved (newPos, item->isNextCP ());
            moveCmd->appendOffCurvePoint (
                newPos, baseItem->ttfindex (), baseItem->nextcpindex (), item->isNextCP ());
            cp_moved = true;
            changed = true;
            break;
        } else if (sellist[i]->type () == ManipulatorItem::Type) {
            ManipulatorItem *item = qgraphicsitem_cast<ManipulatorItem *> (sellist[i]);
	    QGraphicsItem *baseItem = item->parentItem ();

	    if (baseItem->type () == FigureEllipseItem::Type) {
		FigureEllipseItem *baseFigItem = qgraphicsitem_cast<FigureEllipseItem *> (baseItem);
		baseFigItem->manipulatorMoved (move, item);
		moveCmd->appendManipulator (baseFigItem->svgFigure (), item->edge ());
	    } else if (baseItem->type () == FigureRectItem::Type) {
		FigureRectItem *baseFigItem = qgraphicsitem_cast<FigureRectItem *> (baseItem);
		baseFigItem->manipulatorMoved (move, item);
		moveCmd->appendManipulator (baseFigItem->svgFigure (), item->edge ());
	    }
            cp_moved = true;
            changed = true;
            break;
	}
    }

    if (!cp_moved) {
        for (i=0; i<sellist.size (); i++) {
	    switch (sellist[i]->type ()) {
	      case OnCurvePointItem::Type: {
                ConicPointItem *baseItem = qgraphicsitem_cast<ConicPointItem*> (sellist[i]->parentItem ());
                QPointF newPos = baseItem->pos () + move;
                baseItem->basePointMoved (newPos);
                moveCmd->appendOnCurvePoint (newPos, baseItem->ttfindex (), baseItem->nextcpindex ());
                changed = true;
	      } break;
	      case FigureEllipseItem::Type:
	      case FigureRectItem::Type:
	      case FigurePathItem::Type: {
                FigureItem *figItem = dynamic_cast<FigureItem *> (sellist[i]);
                figItem->moved (move);
                moveCmd->appendFigure (figItem->svgFigure ());
                changed = true;
	      } break;
	      case RefItem::Type: {
                RefItem *refItem = qgraphicsitem_cast<RefItem *> (sellist[i]);
                refItem->refMoved (move);
                moveCmd->appendRef (refItem->transform (), refItem->idx (), refItem->gid ());
                changed = true;
	      } break;
	      case AdvanceWidthItem::Type: {
                AdvanceWidthItem *awItem = qgraphicsitem_cast<AdvanceWidthItem*> (sellist[i]);
                qreal newx = awItem->pos ().x () + move.x ();
                awItem->setPos (QPointF (newx, 0));
                m_context.setAdvanceWidth (round (newx));
                moveCmd->appendAdvanceWidth (newx);
                changed = true;
	      }
            }
        }
	changed |= m_context.joinSplines (true, .5);
    }
    if (changed)
        m_context.undoGroup (true)->activeStack ()->push (moveCmd);
    else
        delete moveCmd;
}

void GlyphScene::switchOutlines (OutlinesType gtype) {
    m_outlines_type = gtype;
}

OutlinesType GlyphScene::outlinesType () {
    return (m_outlines_type);
}

void GlyphScene::selectAll () {
    for (auto item : items ()) {
        // don't select offcurve points, as we cannot handle them in a group
        switch (item->type ()) {
          case OnCurvePointItem::Type:
          case RefItem::Type:
            item->setSelected (true);
          break;
          default:
            ;
        }
    }
}

void GlyphScene::clearSelection () {
    for (auto item : selectedItems ())
        item->setSelected (false);
}

void GlyphScene::ptCornerRequest () {
    setSelPointsType (pt_corner);
}

void GlyphScene::ptCurvedRequest () {
    setSelPointsType (pt_curve);
}

void GlyphScene::ptTangentRequest () {
    setSelPointsType (pt_tangent);
}

void GlyphScene::setSelPointFirst () {
    OnCurvePointItem *pt_item = nullptr;
    ConicPointItem *parent = nullptr;
    for (auto item : selectedItems ()) {
        if (item->type () == OnCurvePointItem::Type) {
	    pt_item = qgraphicsitem_cast<OnCurvePointItem *> (item);
	    parent = qgraphicsitem_cast<ConicPointItem *> (pt_item->parentItem ());
	    break;
        }
    }
    if (parent) {
	ConicPoint *nst = parent->conicPoint ();
	QGraphicsItem *panel = activePanel ();
	FigurePathItem *ctrItem = qgraphicsitem_cast<FigurePathItem *> (panel);
	DrawableFigure *fig = ctrItem ? &ctrItem->svgFigure () : nullptr;
        GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, outlinesType ());
        ucmd->setText (tr ("Set First Point"));
        if (fig->startToPoint (nst)) {
	    m_context.updatePointNumbers ();
	    m_context.undoGroup (true)->activeStack ()->push (ucmd);
	    update ();
	} else {
	    delete ucmd;
	}
    }
}

void GlyphScene::copyRequest () {
    doCopyClear (true, false);
    //pasteAction->setEnabled (true);
}

void GlyphScene::cutRequest () {
    doCopyClear (true, true);
    //pasteAction->setEnabled (true);
}

void GlyphScene::clearRequest () {
    doCopyClear (false, true);
}

void GlyphScene::setSelPointsType (enum pointtype ptype) {
    QList<QGraphicsItem *> sellist = selectedItems ();
    uint16_t i;

    for (i=0; i<sellist.size (); i++) {
        if (sellist[i]->type () == OnCurvePointItem::Type) {
            OnCurvePointItem* item = qgraphicsitem_cast<OnCurvePointItem*> (sellist[i]);
            item->setPointType (ptype);
        }
    }
}

void GlyphScene::doCopyClear (bool copy, bool clear) {
    m_context.checkSelected ();
    if (copy) {
        QList<QUrl> urls;
        urls.append (QUrl (QString ("#glyph%1").arg (m_context.gid ())));
        ConicGlyph *g = m_context.glyph (outlinesType ());
	uint8_t opts = SVGOptions::dumpHeader | SVGOptions::doExtras | SVGOptions::doAppSpecific | SVGOptions::onlySelected;
        std::string svg_str = g->toSVG (nullptr, opts);
        QClipboard *clipboard = QApplication::clipboard ();
        QMimeData *md = new QMimeData;
        md->setData ("image/svg+xml", svg_str.c_str ());
        md->setUrls (urls);
        clipboard->setMimeData (md);
    }
    if (clear) {
        GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, outlinesType ());
        ucmd->setText (copy ? tr ("Cut Glyph Data") : tr ("Delete Glyph Data"));
        if (m_context.clearSelected (false)) {
	    m_context.render (outlinesType ());
	    m_context.update (outlinesType ());
	    m_context.undoGroup (true)->activeStack ()->push (ucmd);
	} else {
	    delete ucmd;
	}
    }
}

void GlyphScene::doPaste () {
    QClipboard *clipboard = QApplication::clipboard ();
    const QMimeData *md = clipboard->mimeData ();
    if (!md->hasFormat ("image/svg+xml"))
        return;

    QByteArray svg_data = md->data ("image/svg+xml");
    BoostIn buf (svg_data.constData (), svg_data.size ());
    ConicGlyph *g = m_context.glyph (outlinesType ());

    QGraphicsItem *panel = activePanel ();
    FigurePathItem *ctrItem = qgraphicsitem_cast<FigurePathItem *> (panel);
    DrawableFigure *target = ctrItem ? &ctrItem->svgFigure () : nullptr;

    GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, outlinesType ());
    ucmd->setText (tr ("Paste Glyph Data"));

    m_context.clearScene ();
    g->fromSVG (buf, 0, target);
    bool refs_ok = m_context.resolveRefs (outlinesType ());
    if (refs_ok) {
        m_context.render (outlinesType ());
        m_context.drawGlyph (g, g->gradients);
        m_context.undoGroup (true)->activeStack ()->push (ucmd);
        m_context.update (outlinesType ());
    } else {
        ucmd->undoInvalid ();
        delete ucmd;
    }
}

void GlyphScene::doMerge () {
    m_context.checkSelected ();
    GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, outlinesType ());
    ucmd->setText (tr ("Merge"));
    if (m_context.clearSelected (true)) {
	m_context.render (outlinesType ());
	m_context.update (outlinesType ());
	m_context.undoGroup (true)->activeStack ()->push (ucmd);
    } else {
	delete ucmd;
    }
}

void GlyphScene::doJoin () {
    m_context.checkSelected ();
    GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, outlinesType ());
    ucmd->setText (tr ("Join contours"));
    if (m_context.joinSplines ()) {
	m_context.render (outlinesType ());
	m_context.update (outlinesType ());
	m_context.undoGroup (true)->activeStack ()->push (ucmd);
    } else {
	delete ucmd;
    }
}

void GlyphScene::doUnlinkRefs () {
    m_context.checkSelected ();
    GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, outlinesType ());
    ucmd->setText ("Unlink references");
    if (m_context.unlinkSelectedRefs ()) {
        m_context.render (outlinesType ());
        m_context.update (outlinesType ());
        m_context.undoGroup (true)->activeStack ()->push (ucmd);
    } else {
        delete ucmd;
    }
}

void GlyphScene::undoableCommand (bool (ConicGlyph::*fn)(bool), const char *undo_lbl) {
    bool selected = (m_context.numSelectedPoints () > 0);
    m_context.checkSelected ();
    GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, outlinesType ());
    ucmd->setText (undo_lbl);
    ConicGlyph *g = m_context.glyph (outlinesType ());
    if ((g->*fn) (selected)) {
	m_context.clearScene ();
	m_context.drawGlyph (g, g->gradients);
        m_context.render (outlinesType ());
        m_context.update (outlinesType ());
        m_context.undoGroup (true)->activeStack ()->push (ucmd);
    } else {
	ucmd->undoInvalid ();
        delete ucmd;
    }
}

void GlyphScene::doExtrema () {
    undoableCommand (&ConicGlyph::addExtrema, "Add extrema");
}

void GlyphScene::doSimplify () {
    undoableCommand (&ConicGlyph::simplify, "Simplify outlines");
}

void GlyphScene::doRound () {
    undoableCommand (&ConicGlyph::roundToInt, "Round to int");
}

void GlyphScene::doOverlap () {
}

void GlyphScene::doDirection () {
    undoableCommand (&ConicGlyph::correctDirection, "Correct direction");
}

void GlyphScene::doReverse () {
    if (!m_context.numSelectedPoints ())
	return;
    m_context.checkSelected ();
    GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, outlinesType ());
    ucmd->setText ("Reverse direction");
    ConicGlyph *g = m_context.glyph (outlinesType ());
    g->reverseSelected ();
    m_context.render (outlinesType ());
    m_context.update (outlinesType ());
    m_context.undoGroup (true)->activeStack ()->push (ucmd);
}

void GlyphScene::doAutoHint (sFont &fnt) {
    GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, outlinesType ());
    ucmd->setText ("Autohint");
    ConicGlyph *g = m_context.glyph (outlinesType ());
    if (g->autoHint (fnt))
	m_context.undoGroup (true)->activeStack ()->push (ucmd);
    else
	delete ucmd;
}

void GlyphScene::doHintMasksUpdate (sFont &fnt) {
    GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, outlinesType ());
    ucmd->setText ("Update hint masks");
    ConicGlyph *g = m_context.glyph (outlinesType ());
    if (g->hmUpdate (fnt))
	m_context.undoGroup (true)->activeStack ()->push (ucmd);
    else
	delete ucmd;
}

void GlyphScene::doClearHints () {
    GlyphChangeCommand *ucmd = new GlyphChangeCommand (m_context, outlinesType ());
    ucmd->setText ("Autohint");
    ConicGlyph *g = m_context.glyph (outlinesType ());
    if (g->clearHints ())
	m_context.undoGroup (true)->activeStack ()->push (ucmd);
    else
	delete ucmd;
}

uint16_t GlyphScene::numSelectedRefs () {
    QList<QGraphicsItem *> sellist = selectedItems ();
    uint16_t i, ret=0;

    for (i=0; i<sellist.size (); i++) {
        if (sellist[i]->type () == RefItem::Type)
            ret++;
    }
    return ret;
}

uint16_t GlyphScene::numSelectedFigs () {
    uint16_t ret=0;

    for (QGraphicsItem *child : m_rootItem->childItems ()) {
	if (child->isPanel () && child->isSelected ())
            ret++;
    }
    return ret;
}
