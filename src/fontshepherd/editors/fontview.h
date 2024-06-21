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

#include <stdint.h>
#include <deque>

#include <QtWidgets>
#include "tables.h" // Have to load it here due to inheritance from TableEdit
#include "charbuffer.h"
#include "tables/glyphnames.h"

class LocaTable;
class ConicGlyph;
class UndoGroupContainer;
class GlyphContainer;
typedef struct ttffont sFont;

class FVLayout : public QLayout {

public:
   FVLayout (QWidget *parent, int margin = -1, int hSpacing = -1, int vSpacing = -1);
   FVLayout (int margin = -1, int hSpacing = -1, int vSpacing = -1);
   ~FVLayout ();

   void addItem (QLayoutItem *item) override;
   int horizontalSpacing () const;
   int verticalSpacing () const;
   Qt::Orientations expandingDirections () const override;
   bool hasHeightForWidth () const override;
   int heightForWidth (int) const override;
   int count () const override;
   QLayoutItem *itemAt (int index) const override;
   QSize minimumSize () const override;
   void setGeometry (const QRect &rect) override;
   QSize sizeHint () const override;
   QLayoutItem *takeAt (int index) override;

   void setPixelSize (int size);

private:
   int doLayout (const QRect &rect, bool testOnly) const;
   int smartSpacing (QStyle::PixelMetric pm) const;

   QList<QLayoutItem *> itemList;
   int m_hSpace;
   int m_vSpace;
};

class GlyphContext;

class GlyphBox : public QGroupBox {
    Q_OBJECT;

public:
    GlyphBox (QWidget *parent, uint32_t pos, int size);
    ~GlyphBox ();

    void attachGlyph (GlyphContext *gctx, int64_t uni);
    void detachGlyph ();
    void mousePressEvent (QMouseEvent* ev) override;
    void mouseDoubleClickEvent (QMouseEvent* ev) override;
    void paintEvent (QPaintEvent* event) override;
    void renderGlyph ();
    void displayTitle (int style);
    int gid () const;
    uint32_t position () const;
    int64_t unicode () const;
    void setUnicode (int64_t uni);
    void resizeCell (int size);

    static QString styleSheet;

signals:
    void selected (GlyphBox *gb, Qt::KeyboardModifiers flags, bool val);
    void editRequest (GlyphBox *gb);

public slots:
    void select (bool val);
    void setClean (bool val);
    void updatePixmap (int idx);

private:
    bool m_rendered;
    bool m_selected;
    QLabel *m_g_label;
    int64_t m_uni;
    uint32_t m_pos;
    GlyphContext *m_context;
};

class ColrTable;
class CpalTable;
class GlyphNameProvider;
class GlyphViewContainer;
enum class OutlinesType;

class FontView : /*public QMainWindow, virtual */ public TableEdit {
    Q_OBJECT;

public:
    FontView (std::shared_ptr<FontTable> tptr, sFont* font, QWidget *parent);
    ~FontView ();

    void resetData () override {};
    bool checkUpdate (bool can_cancel) override;
    bool isModified () override;
    bool isValid () override;
    void clearGV ();
    std::shared_ptr<FontTable> table () override;
    void setTable (std::shared_ptr<FontTable> tbl);

    bool eventFilter (QObject *object, QEvent *event) override;

    static QString normalColor;
    static QString selectedColor;

public slots:
    //void close ();
    void save ();
    void clear ();
    void cut ();
    void copy ();
    void copyRef ();
    void paste ();
    void pasteInto ();
    void undo ();
    void redo ();
    void addExtrema ();
    void simplify ();
    void roundToInt ();
    void removeOverlap ();
    void correctDirection ();
    void unlinkRefs ();
    void autoHint ();
    void clearHints ();
    void svgCopy ();
    void clearSelection ();
    void selectAllGlyphs ();
    void checkSelection ();
    void resize8x2 ();
    void resize16x4 ();
    void resize16x8 ();
    void resize32x8 ();
    void resizeCells (QAction *action);
    void setOrderList ();
    void changeGlyphOrder (int idx);
    void addGlyph ();
    void clearSvgGlyph ();
    void switchPalette (int idx);
    void updateGlyphNames ();

signals:
    void selectAll (bool);

protected:
    void closeEvent (QCloseEvent *event);
    void keyPressEvent (QKeyEvent * event);
    void mouseMoveEvent (QMouseEvent* ev);

private slots:
    void edited ();
    void glyphSelected (GlyphBox *gb, Qt::KeyboardModifiers flags, bool val);
    void glyphEdit (GlyphBox *gb);
    void glyphEditCurrent ();
    void showGlyphProps ();
    void editCFF ();
    void switchOutlinesByAction (QAction *action);

private:
    void loadTables (uint32_t tag);
    bool loadGlyphs ();
    //bool ftRenderGlyphs ();
    bool switchGlyphOutlines ();
    void switchOutlines (OutlinesType val);
    void ensureGlyphOutlinesLoaded (uint16_t gid);

    void selectCellLR (bool left, bool expand);
    void selectCellHE (bool home, bool expand);
    void selectCellTB (int inrow, bool top, bool expand);
    void selectToCell (uint32_t idx);

    void selectAllCells (bool val);
    void selectCell (uint32_t idx, bool val);
    void selectCell (GlyphBox *cell, bool val);
    void setStatusBar ();
    // For convenience reasons the following function also sets active undo stack
    void updateStatusBar (GlyphBox *cell);
    void setMenuBar ();
    void setToolBar ();
    void prepareGlyphCells ();
    void displayEncodedGlyphs (CmapEnc *enc, bool by_enc);
    void resetGlyphs (bool do_resize);

    void copyCell (bool cut, bool as_ref);
    void pasteCell (BoostIn &buf, uint32_t cell_idx, uint32_t clipb_idx, bool replace);
    void pasteRange (bool replace);

    void resizeXY (int x_factor, int y_factor);
    int actualHeight (int factor);
    int actualWidth (int factor);

    void contextMenuEvent (QContextMenuEvent *event);
    void undoableCommand (bool (ConicGlyph::*fn)(bool), const char *prog_lbl, const char *undo_lbl);

    QAction *saveAction, *closeAction, *cffAction;
    QAction *undoAction, *redoAction;
    QAction *cutAction, *copyAction, *svgCopyAction, *pasteAction, *clearAction;
    QAction *unselectAction, *selectAllAction, *editAction, *glyphPropsAction;
    QAction *copyRefAction, *pasteIntoAction;
    QAction *addExtremaAction, *simplifyAction, *roundAction, *overlapAction, *corrDirAction;
    QAction *unlinkAction;
    QAction *view8x2Action, *view16x4Action, *view16x8Action, *view32x8Action;
    QAction *cell36Action, *cell48Action, *cell72Action, *cell96Action, *cell128Action;
    QAction *ttSwitchAction, *psSwitchAction, *svgSwitchAction, *colrSwitchAction;
    QActionGroup *m_switchOutlineActions, *m_cellSizeActions;
    QAction *addGlyphAction, *clearSvgGlyphAction;
    QAction *autoHintAction, *clearHintsAction;

    std::shared_ptr<FontTable> m_table;
    std::shared_ptr<GlyphContainer> m_gc_table, m_glyf_table, m_svg_table, m_cff_table;
    std::shared_ptr<ColrTable> m_colr;
    std::shared_ptr<CpalTable> m_cpal;
    sFont *m_font;
    GlyphNameProvider m_gnp;
    bool m_edited, m_valid;
    bool m_post_changed, m_cmap_changed, m_gcount_changed, m_gdef_changed;
    FVLayout *m_layout;
    QScrollArea *m_scroll;
    int m_cell_size, m_h_mult, m_v_mult;
    std::deque<GlyphContext> m_glyphs;
    std::vector<GlyphBox *> m_cells;
    GlyphBox *m_current_cell;
    std::vector<uint32_t> m_selected;
    QLabel *m_sb_enc_lbl, *m_sb_gid_lbl, *m_sb_name_lbl, *m_sb_uniname_lbl, *m_sb_uni_lbl;
    OutlinesType m_content_type;
    uint8_t m_outlines_avail;
    uint16_t m_paletteIdx;

    GlyphViewContainer *m_gv;
    UndoGroupContainer *m_ug_container;
    QComboBox *m_orderBox, *m_paletteBox;
    QAction *m_palLabelAction, *m_palBoxAction;
};
