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
#include "ftwrapper.h"

#define RULER_BREADTH 24
#define GV_MIN_Y -4096
#define GV_MAX_Y 32767
#define GV_MIN_X -4096
#define GV_MAX_X 32767

#ifndef _FS_ENUM_POINTTYPE_COLOR_DEFINED
#define _FS_ENUM_POINTTYPE_COLOR_DEFINED
enum pointtype { pt_curve, pt_corner, pt_tangent };
#endif

class QDRuler : public QWidget {
    Q_OBJECT
    Q_ENUMS (RulerType)
    Q_PROPERTY (qreal origin READ origin WRITE setOrigin)
    Q_PROPERTY (qreal rulerUnit READ rulerUnit WRITE setRulerUnit)
    Q_PROPERTY (qreal rulerZoom READ rulerZoom WRITE setRulerZoom)

public:
    enum RulerType { Horizontal, Vertical };

    QDRuler (QDRuler::RulerType rulerType, QWidget* parent);
    QSize minimumSizeHint () const;
    QDRuler::RulerType rulerType () const;
    qreal origin () const;
    qreal rulerUnit () const;
    qreal rulerZoom () const;

public slots:
    void setOrigin (const qreal origin);
    void setRulerUnit (const qreal rulerUnit);
    void setRulerZoom (const qreal rulerZoom);
    void setCursorPos (const QPoint cursorPos);
    void setMouseTrack (const bool track);

protected:
    void mouseMoveEvent (QMouseEvent* event);
    void paintEvent (QPaintEvent* event);

private:
    void drawAScaleMeter (QPainter* painter, QRectF rulerRect, qreal scaleMeter, qreal startPositoin);
    void drawFromOriginTo (QPainter* painter, QRectF rulerRect, qreal startMark, qreal endMark,
        int startTickNo, qreal vstep, qreal lstep, qreal startPosition);
    void drawMousePosTick (QPainter* painter);

    RulerType mRulerType;
    qreal mOrigin;
    qreal mRulerUnit;
    qreal mRulerZoom;
    QPoint mCursorPos;
    bool mMouseTracking;
    bool mDrawText;
};

enum class GVPaletteTool {
    pointer, hand, knife, zoom, corner, curve, tangent, ellipse, rect
};

class ConicGlyph;
class DrawableFigure;
struct ttffont;
typedef ttffont sFont;
class GlyphContext;
class NonExclusiveUndoGroup;
enum class OutlinesType;
class GlyphChangeCommand;
class FigurePalette;
class FigureModel;
class FigurePathItem;

class GlyphScene : public QGraphicsScene {
    Q_OBJECT;

public:
    GlyphScene (sFont &fnt, FTWrapper &ftw, GlyphContext &gctx, OutlinesType gtype, QObject *parent = nullptr);

    void drawBackground (QPainter *painter, const QRectF &exposed);
    void drawForeground (QPainter *painter, const QRectF &exposed);

    void setRootItem (QGraphicsItem *root);
    QGraphicsItem *rootItem ();
    int activePanelIndex ();
    void notifyPanelAdded (QGraphicsItem *item);
    void notifyPanelRemoved (QGraphicsItem *item);
    void notifyPanelsSwapped (const int pos1, const int pos2);
    void notifyGlyphRedrawn ();
    void notifyFigurePropsChanged (const int idx);
    void setActiveFigure (QGraphicsItem *item);
    void setActiveFigure (const int idx);

    //void connectToPalette (FigurePalette &figPal, bool link);

    void mouseDoubleClickEvent (QGraphicsSceneMouseEvent *event);
    void mousePressEvent (QGraphicsSceneMouseEvent *event);
    void mouseMoveEvent (QGraphicsSceneMouseEvent *event);
    void mouseReleaseEvent (QGraphicsSceneMouseEvent *event);
    void keyPressEvent (QKeyEvent * keyEvent);

    void setActiveTool (GVPaletteTool active);
    GVPaletteTool activeTool ();

    void switchOutlines (OutlinesType val);
    OutlinesType outlinesType ();

signals:
    void mousePointerMoved (QPointF pos);
    void panelAdded (QGraphicsItem *item, const int pos);
    void panelRemoved (const int pos);
    void panelsSwapped (const int pos1, const int pos2);
    void glyphRedrawn (OutlinesType otype, int pidx);
    void figurePropsChanged (QGraphicsItem *panel, const int pidx);
    void activePanelChanged (const int pos);

private:
    void moveSelected (QPointF move);
    void checkMovable (GlyphChangeCommand *ucmd);

    // Position where the previous mouse move event occured. Comparing this
    // with the current position we can calculate the shift and apply it to
    // selected items
    QPointF m_prevDragPos;
    // Initial position when the dragging is started. We need this for
    // constrainted moving (i. e. when the Shift key is pressed)
    QPointF m_startDragPos;
    // Need this for constrainted moving when the current item is a control point,
    // as their position in this case is not as interesting as the base point
    // position, but should be taken in account to make necessary adjustments
    QPointF m_origItemPos;

    sFont &m_font;
    FTWrapper &m_ftWrapper;
    GlyphContext &m_context;
    OutlinesType m_outlines_type;
    bool m_dragValid;
    QGraphicsItem *m_grabber;
    bool m_hasChanges;
    QUndoCommand *m_undoCmd;
    GVPaletteTool m_activeTool;
    QGraphicsLineItem m_knifeLine;
    QGraphicsRectItem m_selectionRect;
    std::unique_ptr<QGraphicsEllipseItem> m_addEllipse;
    std::unique_ptr<QGraphicsRectItem> m_addRect;
    QGraphicsItem *m_rootItem;
};

class InstrEdit;

class GlyphView : public QGraphicsView {
    Q_OBJECT;

public:
    GlyphView (GlyphScene *scene, QStackedWidget *figPalContainer, QStackedWidget *instrEditContainer, GlyphContext &gctx, QWidget *parent);
    ~GlyphView ();

    void setViewportMargins (int left, int top, int right, int bottom);
    void setViewportMargins (const QMargins & margins);
    void keyPressEvent (QKeyEvent *keyEvent);
    void keyReleaseEvent (QKeyEvent *keyEvent);
    void mousePressEvent (QMouseEvent *event);

    void setRulerOrigin (const qreal pos, const bool is_x);
    void setRulerZoom (const qreal val, const bool is_x);
    void doScroll (int val, const bool is_x);
    void doZoom (qreal val);

    uint16_t gid ();
    QString glyphName ();
    GlyphContext& glyphContext ();
    NonExclusiveUndoGroup* undoGroup ();
    QAction* activeAction ();

    uint16_t numSelectedPoints ();
    uint16_t numSelectedRefs ();
    uint16_t numSelectedFigs ();
    void setSelPointsType (enum pointtype ptype);
    void clearSelection ();
    void selectAll ();
    void updatePoints ();
    void updateFill ();
    void switchOutlines (OutlinesType val);
    OutlinesType outlinesType ();

    void doCopyClear (bool copy, bool clear);
    void doPaste ();
    void doMerge ();
    void doJoin ();
    void doUnlinkRefs ();

    void doExtrema ();
    void doSimplify ();
    void doRound ();
    void doOverlap ();
    void doDirection ();
    void doReverse ();

    void doAutoHint (sFont &fnt);
    void doHintMasksUpdate (sFont &fnt);
    void doClearHints ();

public slots:
    void toolSelected (QAction *action);
    void on_switchOutlines (QAction *action);
    void on_instrChanged ();

private slots:
    void scrolledHorizontally (int val);
    void scrolledVertically (int val);

    void onAddFigure (QGraphicsItem *item, const int pos);
    void onRemoveFigure (const int pos);
    void onSwapPanels (const int pos1, const int pos2);
    void glyphRedrawn (OutlinesType otype, const int pidx);
    void figurePropsChanged (QGraphicsItem *panel, const int pidx);
    void onFigurePaletteUpdate (const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles = QVector<int>());
    void setActiveFigure (const QItemSelection &selected, const QItemSelection &deselected);
    void onActiveFigureChange (const int idx);

private:
    void undoableCommand (bool (ConicGlyph::*fn)(bool), const char *undo_lbl);

    QDRuler *m_HorzRuler, *m_VertRuler;
    FigurePalette *m_figPal;
    InstrEdit *m_instrEdit;
    std::unique_ptr<FigureModel> m_figMod;
    GlyphContext &m_context;
    QAction *m_activeAction;
};

class GlyphContainer;
class FontView;
struct ttffont;
typedef ttffont sFont;
class UndoGroupContainer;

class GlyphViewContainer : public QMainWindow {
    Q_OBJECT;

public:
    GlyphViewContainer (FontView *fv, sFont &font, GlyphContainer *tab);
    ~GlyphViewContainer ();

    void addGlyph (GlyphContext &gctx, OutlinesType content_type);
    void closeEvent (QCloseEvent *event);
    bool hasGlyph (const uint16_t gid) const;
    int glyphTabIndex (const uint16_t gid) const;
    void switchToGlyph (const uint16_t gid, OutlinesType ctype);

    static bool showPoints ();
    static bool showControlPoints ();
    static bool showPointNumbering ();
    static bool showExtrema ();
    static bool showFill ();
    static bool showHints ();
    static bool showBlues ();
    static bool showFamilyBlues ();
    static bool showGridFit ();

    bool eventFilter (QObject *watched, QEvent *event) override;
    FTWrapper *freeTypeWrapper ();

public slots:
    void save ();
    void close ();
    void switchToTab (int index);
    void showMousePointerPos (QPointF pos);
    void closeGlyphTab (int);

private slots:
    void copyRequest ();
    void cutRequest ();
    void pasteRequest ();
    void clearRequest ();
    void mergeRequest ();
    void joinRequest ();

    void addExtremaRequest ();
    void simplifyRequest ();
    void roundRequest ();
    void overlapRequest ();
    void corrDirRequest ();
    void reverseRequest ();
    void unlinkRequest ();

    void autoHintRequest ();
    void hmUpdateRequest ();
    void clearHintsRequest ();

    void checkSelection ();
    void selectAllRequest ();
    void clearSelectionRequest ();
    void ptCornerRequest ();
    void ptCurvedRequest ();
    void ptTangentRequest ();
    void ptFirstRequest ();
    void zoomIn ();
    void zoomOut ();
    void slot_showPoints (const bool val);
    void slot_showControlPoints (const bool val);
    void slot_showPointNumbering (const bool val);
    void slot_showExtrema (const bool val);
    void slot_showFill (const bool val);
    void slot_showHints (const bool val);
    void slot_showBlues (const bool val);
    void slot_showFamilyBlues (const bool val);
    void slot_showGridFit (const bool val);

    void slot_monoBoxClicked (const bool val);
    void slot_sameXYBoxClicked (const bool val);
    void slot_xPpemChanged (const int val);
    void slot_yPpemChanged (const int val);

private:
    void setStatusBar ();
    void setMenuBar ();
    void setToolsPalette ();
    void setGridFitPalette (QSettings &settings);
    void setFigPalette (QSettings &settings);
    void setInstrPalette (QSettings &settings);
    void updateViewSetting (const QString key, const bool val);
    void reallyCloseGlyphTab (int idx);

    QAction *saveAction, *closeAction;
    QAction *undoAction, *redoAction;
    QAction *cutAction, *copyAction, *pasteAction, *clearAction, *mergeAction, *joinAction;
    QAction *selectAllAction, *unselectAction;
    QAction *addExtremaAction, *simplifyAction, *roundAction, *overlapAction;
    QAction *reverseAction, *corrDirAction;
    QAction *unlinkAction;
    QAction *makePtCornerAction, *makePtCurvedAction, *makePtTangentAction, *makePtFirstAction;
    QAction *zoomInAction, *zoomOutAction;
    QAction *showPtsAction, *showCtlPtsAction, *showPtNumAction, *showExtremaAction, *showFillAction;
    QAction *showHintsAction, *showBluesAction, *showFamilyBluesAction, *showGridFitAction;
    QAction *ttSwitchAction, *psSwitchAction, *svgSwitchAction, *colrSwitchAction;
    QAction *autoHintAction, *hmUpdateAction, *clearHintsAction;
    QActionGroup *m_switchOutlineActions;

    QAction *m_defaultPaletteToolAction;
    QActionGroup *m_palActions;

    FontView *m_fv;
    sFont &m_font;
    GlyphContainer *m_tab;
    QLabel *m_pos_lbl;

    int m_width, m_height;
    QTabWidget *m_glyphAreaContainer;
    std::map<uint16_t, int> m_tabmap;
    QDockWidget *m_figDock, *m_instrDock;
    QStackedWidget *m_figPalContainer, *m_instrEditContainer;

    QToolBar *m_gfToolbar;
    QLabel *m_xPpemLabel, *m_yPpemLabel;
    QSlider *m_xPpemSlider, *m_yPpemSlider;

    static bool m_showPoints, m_showControlPoints, m_showPointNumbering, m_showExtrema, m_showFill;
    static bool m_showHints, m_showBlues, m_showFamilyBlues, m_showGridFit;
    static bool m_settingsDone;
    FTWrapper ftWrapper;

    UndoGroupContainer *m_ug_container;
};
