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
#include "tables.h" // Have to load it here due to inheritance from TableEdit

class sfntFile;
typedef struct ttffont sFont;
class FontTable;
class CmapEnc;
class CmapEncTable;
class CmapTable;
class TableEdit;
class UniSpinBox;
class VarSelectorBox;
class GlyphNameProvider;

class GidListModel : public QAbstractListModel {
    Q_OBJECT;

public:
    GidListModel (sFont* fnt, bool is_8bit, QObject *parent = nullptr);
    ~GidListModel ();
    virtual QVariant data (const QModelIndex & index, int role = Qt::DisplayRole) const;
    virtual int rowCount (const QModelIndex & parent = QModelIndex ()) const;

    QString getGidStr (const uint32_t gid) const;

private:
    std::unique_ptr<GlyphNameProvider> m_gnp;
    sFont *m_font;
    QStringList m_data;
    bool m_8bit_limit;
};

class AddTableDialog : public QDialog {
    Q_OBJECT;

public:
    explicit AddTableDialog (CmapTable *cmap, QWidget *parent = 0);

    int platform () const;
    int specific () const;
    int subtable () const;

public slots:
    void accept () override;
    void setSpecificList (int plat);

private:
    void fillBoxes ();

    CmapTable *m_cmap;
    QComboBox *m_platformBox, *m_specificBox, *m_subtableBox;
};

class AddSubTableDialog : public QDialog {
    Q_OBJECT;

public:
    explicit AddSubTableDialog (CmapTable *cmap, bool has_glyph_names, QWidget *parent = 0);

    int format () const;
    int language () const;
    QString encoding () const;
    int source () const;
    int minCode () const;
    int maxCode () const;

public slots:
    void fillControls (int idx);
    void setEncoding (int val);

private:
    static QList<QPair<QString, int>> formatList;
    static QList<QPair<QString, QString>> cjkList;
    static QList<QPair<QString, QString>> euList;

    uint16_t m_default_enc;
    CmapTable *m_cmap;
    QComboBox *m_formatBox, *m_encodingBox, *m_sourceBox;
    QComboBox *m_languageBox;
    UniSpinBox *m_minBox, *m_maxBox;
};

class AddMappingDialog : public QDialog {
    Q_OBJECT;

public:
    explicit AddMappingDialog (CmapEnc *enc, GidListModel *model, QWidget *parent = 0);

    uint32_t code () const;
    uint16_t gid () const;

public slots:
    void accept () override;

private:
    CmapEnc *m_enc;
    GidListModel *m_model;
    UniSpinBox *m_codeBox;
    QComboBox *m_gidBox;
};

class AddRangeDialog : public QDialog {
    Q_OBJECT;

public:
    explicit AddRangeDialog (CmapEnc *enc, struct enc_range rng, GidListModel *model, QWidget *parent = 0);

    uint32_t firstCode () const;
    uint32_t lastCode () const;
    uint16_t gid () const;

public slots:
    void accept () override;

private:
    CmapEnc *m_enc;
    GidListModel *m_model;
    UniSpinBox *m_firstBox;
    UniSpinBox *m_lastBox;
    QComboBox *m_gidBox;
};

class AddVariationDialog : public QDialog {
    Q_OBJECT;

public:
    explicit AddVariationDialog (CmapEnc *enc, GidListModel *model, QWidget *parent = 0);

    void init (QModelIndex &index);
    uint32_t selector () const;
    bool isDefault () const;
    uint32_t code () const;
    uint16_t gid () const;

public slots:
    void accept () override;
    void setDefault (int state);

private:
    CmapEnc *m_enc;
    GidListModel *m_model;
    VarSelectorBox *m_vsBox;
    QComboBox *m_gidBox;
    QCheckBox *m_defaultBox;
    UniSpinBox *m_codeBox;
};

struct table_record {
    uint16_t platform, specific, subtable;
};

class CmapTableModel : public QAbstractTableModel {
    Q_OBJECT;

public:
    CmapTableModel (CmapTable *cmap, QWidget *parent = nullptr);
    ~CmapTableModel ();
    int rowCount (const QModelIndex &parent) const override;
    int columnCount (const QModelIndex &parent) const override;
    QVariant data (const QModelIndex &index, int role) const override;
    bool setData (const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags (const QModelIndex &index) const override;
    QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool removeRows (int row, int count, const QModelIndex &index) override;

    QModelIndex insertRows (QList<table_record> &input);

signals:
    void needsSelectionUpdate (int row);

private:
    CmapTable *m_cmap;
    QWidget *m_parent;
};

class EncSubModel : public QAbstractTableModel {
    Q_OBJECT;

public:
    EncSubModel (CmapEnc *enc, GidListModel *lmodel, QWidget *parent = nullptr);

    int rowCount (const QModelIndex &parent) const override;
    int columnCount (const QModelIndex &parent) const override;
    QVariant data (const QModelIndex &index, int role) const override;
    bool setData (const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags (const QModelIndex &index) const override;
    QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool removeRows (int row, int count, const QModelIndex &index) override;

    QModelIndex insertRows (const QList<struct enc_mapping> &input, int row);

public slots:
    void setSubTableModified (bool clean);

signals:
    void needsLabelUpdate (int index);
    void needsSelectionUpdate (uint16_t tab_idx, int row, int count);

private:
    CmapEnc *m_enc;
    GidListModel *m_listmodel;
    QWidget *m_parent;
};

class Enc13SubModel : public QAbstractTableModel {
    Q_OBJECT;

public:
    Enc13SubModel (CmapEnc *enc, GlyphNameProvider *gnp, QObject *parent = nullptr);

    int rowCount (const QModelIndex &parent) const override;
    int columnCount (const QModelIndex &parent) const override;
    QVariant data (const QModelIndex &index, int role) const override;
    bool setData (const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags (const QModelIndex &index) const override;
    QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool removeRows (int row, int count, const QModelIndex &index) override;

    QModelIndex insertRows (QList<struct enc_range> &input, int row);

public slots:
    void setSubTableModified (bool clean);

signals:
    void needsLabelUpdate (int index);
    void needsSelectionUpdate (uint16_t tab_idx, int row, int count);

private:
    CmapEnc *m_enc;
    GlyphNameProvider *m_gnp;
};

enum vsItemType {
    em_varSelector = 1001,
    em_uvsDefaultGroup = 1010,
    em_uvsNonDefaultGroup = 1020,
    em_uvsDefaultRecord = 1011,
    em_uvsNonDefaultRecord = 1021,
};

struct uni_variation {
    uint32_t selector;
    bool is_dflt;
    uint32_t unicode;
    uint16_t gid;
};

class VarSelectorModel : public QAbstractItemModel {
    Q_OBJECT;

public:
    VarSelectorModel (CmapEnc *enc, GidListModel *lmodel, QObject *parent = nullptr);

    QModelIndex index (int row, int column, const QModelIndex &parent) const override;
    QModelIndex parent (const QModelIndex &child) const override;
    int rowCount (const QModelIndex &parent = QModelIndex ()) const override;
    int columnCount (const QModelIndex &parent = QModelIndex ()) const override;
    QVariant data (const QModelIndex &index, int role = Qt::DisplayRole) const override;
    virtual bool setData (const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags (const QModelIndex &index) const override;
    bool removeRows (int row, int count, const QModelIndex &index) override;

    QModelIndex insertRows (QList<struct uni_variation> &input, int type=em_varSelector);

public slots:
    void setSubTableModified (bool clean);

signals:
    void needsLabelUpdate (int index);
    void needsSelectionUpdate (uint16_t tab_idx, int row, int count, QModelIndex parent);

public:
    class VarSelectorItem {
    public:
	VarSelectorItem (VarSelectorItem *parent = nullptr);

	virtual enum vsItemType type () const = 0;
	VarSelectorItem *parent () const;
	virtual VarSelectorItem *getChild (int idx);
	virtual bool removeChildren (int row, int count=1);
	virtual void appendChild (uint32_t code, int row, bool is_dflt);
	virtual QVariant data (int column, int role = Qt::DisplayRole) const = 0;
	virtual bool setData (int column, const QVariant &value, int role = Qt::EditRole);
	virtual int rowCount () const;
	virtual int columnCount () const;
	virtual struct var_selector_record *vsRecord () const = 0;
	virtual uint32_t unicode () const = 0;
	virtual int findRow () const = 0;
	virtual Qt::ItemFlags flags (int column) const;

    protected:
	std::vector<std::unique_ptr<VarSelectorItem>> m_children;

    private:
	VarSelectorItem *m_parent;
    };

private:
    class UvsItem : public VarSelectorItem {
    public:
	UvsItem (GidListModel *lmodel, uint32_t uni, enum vsItemType type, VarSelectorItem *parent);

	enum vsItemType type () const override;
	QVariant data (int column, int role = Qt::DisplayRole) const override;
	bool setData (int column, const QVariant &value, int role = Qt::EditRole) override;
	int columnCount () const override;
	struct var_selector_record *vsRecord () const override;
	uint32_t unicode () const override;
	int findRow () const override;
	virtual Qt::ItemFlags flags (int column) const override;

    private:
	GidListModel *m_glyph_desc_provider;
	uint32_t m_unicode;
	enum vsItemType m_type;
    };

    class UvsItemGroup : public VarSelectorItem {
    public:
	UvsItemGroup (GidListModel *lmodel, enum vsItemType type, VarSelectorItem *parent);

	virtual enum vsItemType type () const override;
	VarSelectorItem *getChild (int idx) override;
	bool removeChildren (int row, int count=1) override;
	virtual void appendChild (uint32_t code, int row, bool is_dflt);
	QVariant data (int column, int role = Qt::DisplayRole) const override;
	int columnCount () const override;
	struct var_selector_record *vsRecord () const override;
	uint32_t unicode () const override;
	int findRow () const override;

    private:
	GidListModel *m_glyph_desc_provider;
	enum vsItemType m_type;
    };

    class VarSelectorRoot : public VarSelectorItem {
    public:
	VarSelectorRoot (CmapEnc *enc, struct var_selector_record *vsr, GidListModel *lmodel, VarSelectorItem *parent = nullptr);

	virtual enum vsItemType type () const override;
	VarSelectorItem *getChild (int idx) override;
	bool removeChildren (int row, int count=1) override;
	virtual void appendChild (uint32_t code, int row, bool is_dflt);
	virtual QVariant data (int column, int role = Qt::DisplayRole) const override;
	struct var_selector_record *vsRecord () const override;
	uint32_t unicode () const override;
	int findRow () const override;

	void update (uint16_t i);

    private:
	CmapEnc *m_enc;
	struct var_selector_record *m_vsr;
	GidListModel *m_glyph_desc_provider;
    };

    QModelIndex insertRow (uint32_t selector, bool is_dflt, uint32_t code, uint16_t gid);
    bool removeRootItems (int row, int count=1);

    CmapEnc *m_enc;
    GidListModel *m_lmodel;
    std::vector<std::unique_ptr<VarSelectorRoot>> m_root;
};

class TableRecordCommand : public QUndoCommand {
public:
    TableRecordCommand (CmapTableModel *model, int row);
    TableRecordCommand (CmapTableModel *model, const QList<table_record> &input);

    void redo ();
    void undo ();

private:
    CmapTableModel *m_model;
    int m_row;
    QList<table_record> m_data;
    bool m_remove;
};

class MappingCommand : public QUndoCommand {
public:
    MappingCommand (EncSubModel *model, int row, int count);
    MappingCommand (EncSubModel *model, const QList<struct enc_mapping> &input, int row);

    void redo ();
    void undo ();

private:
    EncSubModel *m_model;
    int m_row;
    int m_count;
    QList<struct enc_mapping> m_data;
    bool m_remove;
};

class RangeCommand : public QUndoCommand {

public:
    RangeCommand (Enc13SubModel *model, int row, int count);
    RangeCommand (Enc13SubModel *model, const QList<struct enc_range> &input, int row);

    void redo ();
    void undo ();

private:
    Enc13SubModel *m_model;
    int m_row;
    int m_count;
    QList<struct enc_range> m_data;
    bool m_remove;
};

class VariationCommand : public QUndoCommand {

public:
    VariationCommand (VarSelectorModel *model, QModelIndex parent, int row, int count);
    VariationCommand (VarSelectorModel *model, const QList<struct uni_variation> &input);

    void redo ();
    void undo ();

private:
    void readSequences (QModelIndex &parent, uint32_t selector, int row, int count);

    VarSelectorModel *m_model;
    QModelIndex m_parent;
    int m_row;
    int m_count;
    QList<struct uni_variation> m_data;
    int m_type;
    bool m_remove;
};

class ChangeCellCommand : public QUndoCommand {
public:
    ChangeCellCommand (QAbstractItemModel *model, const QModelIndex &index, uint32_t new_val);

    void redo ();
    void undo ();

private:
    QAbstractItemModel *m_model;
    const QModelIndex m_index;
    uint16_t m_old;
    uint16_t m_new;
};

class SubtableSelectorDelegate : public QStyledItemDelegate {
    Q_OBJECT;

public:
    explicit SubtableSelectorDelegate (CmapTable *cmap, QUndoStack *us, QObject *parent = nullptr);

    QWidget* createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const;
    void setEditorData (QWidget *editor, const QModelIndex &index) const;
    void setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const;
    void updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const;

private:
    CmapTable *m_cmap;
    QUndoStack *m_ustack;
};

class ComboDelegate : public QStyledItemDelegate {
    Q_OBJECT;

public:
    explicit ComboDelegate (GidListModel *model, QUndoStack *us, QObject *parent = nullptr);

    QWidget* createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const;
    void setEditorData (QWidget *editor, const QModelIndex &index) const;
    void setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const;
    void updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const;

private:
    GidListModel *m_model;
    QUndoStack *m_ustack;
};

class UnicodeDelegate : public QStyledItemDelegate {
    Q_OBJECT;

public:
    explicit UnicodeDelegate (QUndoStack *us, QObject *parent = nullptr);

    QWidget* createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const;
    void setEditorData (QWidget *editor, const QModelIndex &index) const;
    void setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const;
    void updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const;

private:
    QUndoStack *m_ustack;
};

class CmapEdit : public TableEdit {
    Q_OBJECT;

public:
    CmapEdit (FontTable* tab, sFont* font, QWidget *parent);
    ~CmapEdit ();

    void resetData () override {};
    bool checkUpdate (bool can_cancel) override;
    bool isModified () override;
    bool isValid () override;
    FontTable* table () override;

    void closeEvent (QCloseEvent *event);
    QSize minimumSize () const;
    QSize sizeHint () const;

signals:
    void fwdRemoveTableRow (int index);

public slots:
    void save ();

    void removeEncodingRecord ();
    void addEncodingRecord ();
    void removeSelectedSubTable ();
    void removeSubTable (int idx);
    void addSubTable ();
    void removeSubTableMapping ();
    void addSubTableMapping ();
    void removeSubTableRange ();
    void addSubTableRange ();
    void removeVariationSequence ();
    void addVariationSequence ();

    void onTabChange (int index);
    void onEncTabChange (int index);
    void setTablesClean (bool clean);
    void updateSubTableLabel (int index);
    void changeSubTableOrder (int from, int to);

    void updateTableSelection (int idx);
    void updateMappingSelection (uint16_t idx, int row, int count);
    void updateVariationSelection (uint16_t tab_idx, int row, int count, QModelIndex parent);

    void showEditMenu ();
    void onTablesContextMenu (const QPoint &point);
    void onMappingsContextMenu (const QPoint &point);
    void onRangesContextMenu (const QPoint &point);
    void onVarSelectorsContextMenu (const QPoint &point);

private:
    void setMenuBar ();
    void fillTables ();
    void fillSubTable (CmapEnc *cur_enc);
    void showStandard (QTabWidget *tab, CmapEnc *sub, GidListModel *model);
    void showRanges13 (QTabWidget *tab, CmapEnc *sub);
    void showVariations (QTabWidget *tab, CmapEnc *sub, GidListModel *model);
    void updateSubTableLabels ();
    void setEditMenuTexts (VarSelectorModel::VarSelectorItem* item);

    void setTablesModified (bool val);
    void setSubTablesModified (bool val);

    bool m_valid;

    FontTable *m_table;
    CmapTable *m_cmap;
    sFont *m_font;
    std::unique_ptr<GidListModel> m_model;
    std::unique_ptr<GidListModel> m_model8;
    std::unique_ptr<GlyphNameProvider> m_gnp;

    std::unique_ptr<QUndoGroup> m_uGroup;
    QMap<QWidget*, QUndoStack*> m_uStackMap;
    QAction *saveAction, *addAction, *removeAction, *closeAction;
    QAction *deleteMappingAction, *addMappingAction;
    QAction *deleteRangeAction, *addRangeAction;
    QAction *deleteVarSequenceAction, *addVarSequenceAction;
    QAction *undoAction, *redoAction;

    QTabWidget *m_maptab;
    QTableView *m_tabtab;
    QTabWidget *m_enctab;
    QPushButton *saveButton, *closeButton, *addButton, *removeButton;

    QStringList m_enc_list;
    std::vector<std::unique_ptr<QAbstractItemModel>> m_model_storage;
};
