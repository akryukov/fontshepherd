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
#include "tables.h"
#include "tables/glyphcontainer.h"
#include "tables/cff.h"
#include "tables/cmap.h"
#include "editors/cffedit.h"
#include "editors/postedit.h"
#include "tables/glyphnames.h"

#include "fs_notify.h"
#include "icuwrapper.h"
#include "commondelegates.h"
#include "exceptions.h"

static bool dict_entry_editable (const int op) {
    // Exclude dict operators which are either deprecated, or need
    // an offset to some table location, so that there is no reason
    // to edit them manually
    switch (op) {
      case cff::charset:
      case cff::Encoding:
      case cff::CharStrings:
      case cff::Private:
      case cff::Subrs:
      case cff::vsindex: // this one is only relevant for CFF2
      case cff::vstore:
      case cff::PaintType:
      case cff::CharstringType:
      case cff::ForceBoldThreshold:
      case cff::SyntheticBase:
      case cff::BaseFontBlend:
      case cff::FDArray:
      case cff::FDSelect:
        return false;
      default:
        return true;
      }
}

static void adjust_item_data_top (QAbstractItemModel *model, int row, int item_data) {
    QModelIndex data_idx = model->index (row, 1);
    switch (item_data) {
      case cff::UniqueID:
      case cff::CIDFontVersion:
      case cff::CIDFontRevision:
      case cff::CIDFontType:
      case cff::CIDCount:
	model->setData (data_idx, 0, Qt::EditRole);
	model->setData (data_idx, dt_uint, Qt::UserRole);
	break;
      case cff::isFixedPitch:
	model->setData (data_idx, "false", Qt::EditRole);
	model->setData (data_idx, dt_bool, Qt::UserRole);
	break;
      case cff::version:
      case cff::Notice:
      case cff::FullName:
      case cff::FamilyName:
      case cff::Weight:
      case cff::Copyright:
      case cff::BaseFontName:
      case cff::FontName:
	model->setData (data_idx, "", Qt::EditRole);
	model->setData (data_idx, dt_sid, Qt::UserRole);
	break;
      case cff::FontBBox:
	model->setData (data_idx, "[0, 0, 0, 0]", Qt::EditRole);
	model->setData (data_idx, dt_list, Qt::UserRole);
	break;
      case cff::XUID:
	model->setData (data_idx, "[]", Qt::EditRole);
	model->setData (data_idx, dt_list, Qt::UserRole);
	break;
      case cff::FontMatrix:
	model->setData (data_idx, "[0.001, 0, 0, 0.001, 0, 0]", Qt::EditRole);
	model->setData (data_idx, dt_list, Qt::UserRole);
	break;
      case cff::ItalicAngle:
      case cff::UnderlinePosition:
      case cff::UnderlineThickness:
      case cff::StrokeWidth:
	model->setData (data_idx, 0, Qt::EditRole);
	model->setData (data_idx, dt_float, Qt::UserRole);
	break;
      case cff::ROS:
	model->setData (data_idx, "Adobe-Identity-0", Qt::EditRole);
	model->setData (data_idx, dt_ros, Qt::UserRole);
    }
}

static void adjust_item_data_private (QAbstractItemModel *model, int row, int item_data) {
    QModelIndex data_idx = model->index (row, 1);
    switch (item_data) {
      case cff::Subrs:
      case cff::LanguageGroup:
	model->setData (data_idx, 0, Qt::EditRole);
	model->setData (data_idx, pt_uint, Qt::UserRole);
	break;
      case cff::ForceBold:
	model->setData (data_idx, "false", Qt::EditRole);
	model->setData (data_idx, pt_bool, Qt::UserRole);
	break;
      case cff::StdHW:
      case cff::StdVW:
      case cff::defaultWidthX:
      case cff::nominalWidthX:
      case cff::BlueScale:
      case cff::BlueShift:
      case cff::BlueFuzz:
      case cff::ForceBoldThreshold: // (obsolete)
      case cff::ExpansionFactor:
      case cff::initialRandomSeed:
	model->setData (data_idx, "0", Qt::EditRole);
	model->setData (data_idx, pt_blend, Qt::UserRole);
	break;
      case cff::BlueValues:
      case cff::OtherBlues:
      case cff::FamilyBlues:
      case cff::FamilyOtherBlues:
      case cff::StemSnapH:
      case cff::StemSnapV:
	model->setData (data_idx, "[]", Qt::EditRole);
	model->setData (data_idx, pt_blend_list, Qt::UserRole);
    }
}

static bool check_blend (QString &s, blend *b) {
    QRegularExpression re ("^(-?\\d+\\.?\\d*)\\s*(<(.*)>)?");
    QRegularExpressionMatch match = re.match (s);
    if (match.hasMatch ()) {
	b->base = match.captured (1).toDouble ();
	QString s_blend = match.captured (3);
	QStringList lst = s_blend.split (QRegularExpression (",\\s*"), Qt::SkipEmptyParts);
	re.setPattern ("^(-?\\d+(\\.\\d*)?)$");
	b->deltas.reserve (lst.size ());
	for (auto snum : lst) {
	    match = re.match (snum);
	    if (match.hasMatch ())
		b->deltas.push_back (match.captured (1).toDouble ());
	    else
		return false;
	}
	b->valid = true;
	return true;
    }
    return false;
}

static bool check_blend_list (QString &s, private_entry *pe) {
    QRegularExpression re ("^\\s*\\[(.*)\\]\\s*$");
    QRegularExpressionMatch match = re.match (s);
    if (match.hasMatch ()) {
	pe->setType (pt_blend_list);
	QString slist = match.captured (1);
	re.setPattern ("-?\\d+(\\.\\d*)?(\\s*<[^<>]*>)?\\s*,?");
	QRegularExpressionMatchIterator it = re.globalMatch (slist);
	int i = 0;
	while (it.hasNext () && i<14) {
	    match = it.next ();
	    QString sblend = match.captured (0);
	    struct blend b;
	    if (check_blend (sblend, &b))
		pe->list[i] = b;
	    else
		return false;
	    i++;
	}
	return true;
    }
    return false;
}

static bool check_float_list (const QString &s, top_dict_entry &de, uint8_t size) {
    QRegularExpression re ("^\\s*\\[(.*)\\]\\s*$");
    QRegularExpressionMatch match = re.match (s);
    if (match.hasMatch ()) {
	std::vector<double> check_list {};
	check_list.reserve (size*2);
	de.setType (dt_list);
	QString slist = match.captured (1);
	re.setPattern ("\\s*(-?\\d+(\\.\\d+)?),?\\s*");
	QRegularExpressionMatchIterator it = re.globalMatch (slist);
	while (it.hasNext ()) {
	    match = it.next ();
	    QString s = match.captured (1);
	    bool ok;
	    double val = s.toFloat (&ok);
	    if (ok)
		check_list.push_back (val);
	    else
		return false;
	}
	if (check_list.size () == size)
	    de.list = check_list;
	return true;
    }
    return false;
}

static bool check_ros (const QString &s, top_dict_entry &de) {
    QRegularExpression re ("^\\s*(\\S+)-(\\S+)-(\\d+)\\s*$");
    QRegularExpressionMatch match = re.match (s);
    if (match.hasMatch ()) {
	de.setType (dt_ros);
	de.ros.registry.str = match.captured (1).toStdString ();
	de.ros.order.str = match.captured (2).toStdString ();
	de.ros.supplement = match.captured (3).toInt ();
	return true;
    }
    return false;
}

CffDialog::CffDialog (sFont *fnt, CffTable *cff, QWidget *parent) :
    QDialog (parent), m_font (fnt), m_cff (cff) {
    setWindowTitle (QString ("PS Private - ").append (m_font->fontname));

    m_tab = new QTabWidget (this);
    m_topTab = new QTableWidget (m_tab);
    m_privTab = new QTabWidget (m_tab);
    m_gnTab = new QTableWidget (m_tab);
    m_fdSelTab = new QTableWidget (m_tab);
    m_tab->addTab (m_topTab, QWidget::tr ("PS &Top dict"));
    m_tab->addTab (m_privTab, QWidget::tr ("PS &Private"));
    m_tab->addTab (m_gnTab, QWidget::tr ("&Glyph names"));
    m_tab->setTabVisible (2, !cff->cidKeyed () && cff->version () < 2);
    m_tab->addTab (m_fdSelTab, QWidget::tr ("&FD select"));
    m_tab->setTabVisible (3, cff->topDict ()->has_key (cff::FDSelect));

    m_privateTabs.reserve (m_cff->numSubFonts ());
    if (cff->cidKeyed () || cff->version () > 1) {
	for (int i=0; i<m_cff->numSubFonts (); i++) {
	    auto tab = new QTableWidget ();
	    fillPrivateTab (tab, m_cff->privateDict (i));
	    m_privTab->addTab (tab, QString::fromStdString (m_cff->subFontName (i)));
	}
    } else {
        auto tab = new QTableWidget ();
        fillPrivateTab (tab, m_cff->privateDict ());
        m_privTab->addTab (tab, QString::fromStdString (m_cff->fontName ()));
    }
    fillTopTab (m_topTab, m_cff->topDict ());
    if (!cff->cidKeyed () && cff->version () < 2)
	fillGlyphTab (m_gnTab);
    if (cff->topDict ()->has_key (cff::FDSelect))
	fillFdSelTab (m_fdSelTab);

    m_okButton = new QPushButton (QWidget::tr ("OK"));
    connect (m_okButton, &QPushButton::clicked, this, &QDialog::accept);
    m_cancelButton = new QPushButton (QWidget::tr ("&Cancel"));
    connect (m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    m_removeButton = new QPushButton (QWidget::tr ("&Remove entry"));
    connect (m_removeButton, &QPushButton::clicked, this, &CffDialog::removeEntry);
    m_addButton = new QPushButton (QWidget::tr ("&Add entry"));
    connect (m_addButton, &QPushButton::clicked, this, &CffDialog::addEntry);

    QGridLayout *layout = new QGridLayout ();

    layout->addWidget (new QLabel (tr ("Table version:")), 0, 0);
    m_versionBox = new QComboBox ();
    m_versionBox->addItem ("1.0: 'CFF ' font table", 1.0);
    m_versionBox->addItem ("2.0: 'CFF2' font table", 2.0);
    layout->addWidget (m_versionBox, 0, 1);
    m_versionBox->setCurrentIndex
	(m_versionBox->findData (m_cff->version (), Qt::UserRole));
    connect (m_versionBox, static_cast<void (QComboBox::*)(int)> (&QComboBox::currentIndexChanged),
	this, &CffDialog::setTableVersion);

    layout->addWidget (m_tab, 1, 0, 1, 2);

    QHBoxLayout *buttLayout;
    buttLayout = new QHBoxLayout ();
    buttLayout->addWidget (m_okButton);
    buttLayout->addWidget (m_addButton);
    buttLayout->addWidget (m_removeButton);
    buttLayout->addWidget (m_cancelButton);
    layout->addLayout (buttLayout, 2, 0, 1, 2);

    setLayout (layout);

    connect (m_tab, &QTabWidget::currentChanged, this, &CffDialog::onTabChange);
    onTabChange (0);
}

static void updateTopTab (QTableWidget *tab, TopDict *td) {
    tab->setRowCount (td->size ());

    for (size_t i=0; i<td->size (); i++) {
	auto pair = td->by_idx (i);
	int op = pair.first;
	const std::string &sop = cff::psTopDictEntries.at (op);
        auto key_item = new QTableWidgetItem (QString::fromStdString (sop));
        auto val_item = new QTableWidgetItem (QString::fromStdString (pair.second.toString ()));
	key_item->setData (Qt::UserRole, op);
	val_item->setData (Qt::UserRole, pair.second.type ());
        tab->setItem (i, 0, key_item);
        tab->setItem (i, 1, val_item);
	if (!dict_entry_editable (op)) {
	    key_item->setFlags (key_item->flags () & ~Qt::ItemIsEnabled);
	    val_item->setFlags (val_item->flags () & ~Qt::ItemIsEnabled);
	}
    }
}

void CffDialog::fillTopTab (QTableWidget *tab, TopDict *td) {
    tab->setColumnCount (2);
    updateTopTab (tab, td);

    QFontMetrics fm = tab->fontMetrics ();
    int w0 = fm.boundingRect ("~UnderlineThickness~").width ();
    tab->setHorizontalHeaderLabels (QStringList () << tr ("Key") << tr ("Value"));
    tab->setColumnWidth (0, w0);
    tab->horizontalHeader ()->setStretchLastSection (true);
    tab->setSelectionBehavior (QAbstractItemView::SelectRows);
    tab->setSelectionMode (QAbstractItemView::SingleSelection);
    tab->resize (w0*4, tab->rowHeight (0) * 12);
    tab->selectRow (0);
    tab->setItemDelegateForColumn (0, new CffDictDelegate (false, this));
    tab->setItemDelegateForColumn (1, new TopDelegate (this));
}

static void updatePrivateTab (QTableWidget *tab, PrivateDict *pd) {
    tab->setRowCount (pd->size ());

    for (size_t i=0; i<pd->size (); i++) {
	auto pair = pd->by_idx (i);
	int op = pair.first;
	const std::string &sop = cff::psPrivateEntries.at (op);
        auto key_item = new QTableWidgetItem (QString::fromStdString (sop));
        auto val_item = new QTableWidgetItem (QString::fromStdString (pair.second.toString ()));
	key_item->setData (Qt::UserRole, op);
	val_item->setData (Qt::UserRole, pair.second.type ());
        tab->setItem (i, 0, key_item);
        tab->setItem (i, 1, val_item);
	if (op == cff::Subrs) {
	    key_item->setFlags (key_item->flags () & ~Qt::ItemIsEnabled);
	    val_item->setFlags (val_item->flags () & ~Qt::ItemIsEnabled);
	}
    }
}

void CffDialog::fillPrivateTab (QTableWidget *tab, PrivateDict *pd) {
    tab->setColumnCount (2);
    updatePrivateTab (tab, pd);

    QFontMetrics fm = tab->fontMetrics ();
    int w0 = fm.boundingRect ("~FamilyOtherBlues~").width ();
    tab->setHorizontalHeaderLabels (QStringList () << tr ("Key") << tr ("Value"));
    tab->setColumnWidth (0, w0);
    tab->horizontalHeader ()->setStretchLastSection (true);
    tab->setSelectionBehavior (QAbstractItemView::SelectRows);
    tab->setSelectionMode (QAbstractItemView::SingleSelection);
    tab->selectRow (0);
    tab->setItemDelegateForColumn (0, new CffDictDelegate (true, this));
    tab->setItemDelegateForColumn (1, new PrivateDelegate (this));
}

static void updateGlyphTab (QTableWidget *tab, uint16_t cnt, CmapEnc *enc, CffTable *cff) {
    tab->setRowCount (cnt);

    for (uint16_t i=0; i<cnt; i++) {
        auto gid_item = new QTableWidgetItem
	    (QString ("%1 (0x%2)").arg (i).arg (i, 2, 16, QLatin1Char ('0')));
	gid_item->setFlags (gid_item->flags() & ~Qt::ItemIsEditable);
	gid_item->setData (Qt::UserRole, i);
	QString repr = enc ? enc->gidCodeRepr (i) : "<unencoded>";
	auto uni_item = new QTableWidgetItem (repr);
	uni_item->setFlags (uni_item->flags() & ~Qt::ItemIsEditable);
	if (enc && enc->isUnicode ()) {
	    auto uni = enc->unicode (i);
	    if (!uni.empty ())
		uni_item->setToolTip (QString::fromStdString (IcuWrapper::unicodeCharName (uni[0])));
	}
	auto name_item = new QTableWidgetItem
	    (QString::fromStdString (cff->glyphName (i)));
        tab->setItem (i, 0, gid_item);
        tab->setItem (i, 1, uni_item);
        tab->setItem (i, 2, name_item);
    }
}

void CffDialog::fillGlyphTab (QTableWidget *tab) {
    tab->setColumnCount (3);
    CmapEnc *enc = m_font->enc;

    updateGlyphTab (tab, m_font->glyph_cnt, enc, m_cff);
    QString enc_title = (enc && enc->isUnicode ()) ?
	QWidget::tr ("Unicode") : QWidget::tr ("Encoded");
    tab->setHorizontalHeaderLabels (QStringList () << tr ("GID") << enc_title << tr ("Glyph name"));
    tab->horizontalHeader ()->setSectionResizeMode (QHeaderView::Stretch);
    tab->horizontalHeader ()->setStretchLastSection (true);
    tab->setSelectionBehavior (QAbstractItemView::SelectRows);
    tab->setSelectionMode (QAbstractItemView::SingleSelection);
}

void CffDialog::fillFdSelTab (QTableWidget *tab) {
    tab->setRowCount (m_font->glyph_cnt);
    tab->setColumnCount (3);
    CmapEnc *enc = m_font->enc;
    QStringList sflist;

    sflist.reserve (m_cff->numSubFonts ());
    for (int i=0; i<m_cff->numSubFonts (); i++)
	sflist << m_cff->subFontName (i).c_str ();
    QAbstractItemDelegate *dlg = new FdSelectDelegate (sflist);
    tab->setItemDelegateForColumn (2, dlg);

    for (uint16_t i=0; i<m_font->glyph_cnt; i++) {
        auto gid_item = new QTableWidgetItem
	    (QString ("%1 (0x%2)").arg (i).arg (i, 2, 16, QLatin1Char ('0')));
	gid_item->setFlags (gid_item->flags() & ~Qt::ItemIsEditable);
	gid_item->setData (Qt::UserRole, i);
	QString repr = enc ? enc->gidCodeRepr (i) : "<unencoded>";
	auto uni_item = new QTableWidgetItem (repr);
	uni_item->setFlags (uni_item->flags() & ~Qt::ItemIsEditable);
	if (enc && enc->isUnicode ()) {
	    auto uni = enc->unicode (i);
	    if (!uni.empty ())
		uni_item->setToolTip (QString::fromStdString (IcuWrapper::unicodeCharName (uni[0])));
	}
	uint16_t fds = m_cff->fdSelect (i);
	auto fds_item = new QTableWidgetItem ();
	fds_item->setData (Qt::UserRole, fds);
	fds_item->setData (Qt::DisplayRole, QString ("%1: %2").arg (fds).arg (sflist[fds]));
        tab->setItem (i, 0, gid_item);
        tab->setItem (i, 1, uni_item);
        tab->setItem (i, 2, fds_item);
    }

    QString enc_title = (enc && enc->isUnicode ()) ?
	QWidget::tr ("Unicode") : QWidget::tr ("Encoded");
    tab->setHorizontalHeaderLabels (QStringList () << tr ("GID") << enc_title << tr ("FD Select"));
    tab->horizontalHeader ()->setSectionResizeMode (QHeaderView::Stretch);
    tab->horizontalHeader ()->setStretchLastSection (true);
    tab->setSelectionBehavior (QAbstractItemView::SelectRows);
    tab->setSelectionMode (QAbstractItemView::SingleSelection);
}

void CffDialog::accept () {
    TopDict *td = m_cff->topDict ();
    td->clear ();
    m_cff->clearStrings ();

    for (int j=0; j<m_topTab->rowCount (); j++) {
        auto key_item = m_topTab->item (j, 0);
        auto val_item = m_topTab->item (j, 1);
        const QString &val = val_item->text ();
	const std::string sval = val.toStdString ();
        em_dict_entry_type v_type = (em_dict_entry_type) val_item->data (Qt::UserRole).toInt ();
	int op = key_item->data (Qt::UserRole).toInt ();
	uint8_t size;

        struct top_dict_entry de;
        de.setType (v_type);
        switch (v_type) {
          case dt_uint:
	    de.i = val.toInt ();
	    break;
          case dt_bool:
	    de.b = !val.compare ("true");
	    break;
          case dt_float:
	    de.f = val.toFloat ();
	    break;
          case dt_list:
	    size = (op == 5) ? 4 : (op == 14) ? 20 : 6;
	    check_float_list (val, de, size);
	    break;
          case dt_sid:
	    de.sid.str = sval;
	    de.sid.sid = m_cff->addString (sval);
	    break;
          case dt_size_off:
	    // do nothing, will be recalculated on write anyway
	    break;
          case dt_ros:
	    check_ros (val, de);
	    break;
        }
        (*td)[op] = de;
    }

    for (int i=0; i<m_privTab->count (); i++) {
	PrivateDict *pd = m_cff->privateDict (i);
	pd->clear ();
	QWidget *w = m_privTab->widget (i);
	QTableWidget *tw = qobject_cast<QTableWidget *> (w);

	for (int j=0; j<tw->rowCount (); j++) {
	    auto key_item = tw->item (j, 0);
	    auto val_item = tw->item (j, 1);
	    QString val = val_item->text ();
	    int op = (int) key_item->data (Qt::UserRole).toInt ();
	    em_private_type v_type = (em_private_type) val_item->data (Qt::UserRole).toInt ();

	    struct private_entry pe;
	    pe.setType (v_type);
	    switch (v_type) {
	      case pt_uint:
		pe.i = val.toInt ();
		break;
	      case pt_bool:
		pe.b = !val.compare ("true");
		break;
	      case pt_blend:
		check_blend (val, &pe.n);
		break;
	      case pt_blend_list:
		check_blend_list (val, &pe);
	    }
	    (*pd)[op] = pe;
	}
    }
    for (int i=0; i<m_gnTab->rowCount (); i++) {
        auto name_item = m_gnTab->item (i, 2);
	std::string name = name_item->text ().toStdString ();
	m_cff->addGlyphName (i, name);
    }
    for (int i=0; i<m_fdSelTab->rowCount (); i++) {
        auto fds_item = m_gnTab->item (i, 2);
	uint16_t fds = fds_item->data (Qt::UserRole).toUInt ();
	m_cff->setFdSelect (i, fds);
    }
    emit glyphNamesChanged ();
    QDialog::accept ();
}

void CffDialog::addEntry () {
    QWidget *w = m_tab->currentWidget ();
    QTabWidget *tab = qobject_cast<QTabWidget *> (w);
    if (tab) w = tab->currentWidget ();
    QTableWidget *tw = qobject_cast<QTableWidget *> (w);
    if (!tw)
	return;
    bool is_top = (tw == m_topTab);
    const std::map<int, std::string> &dict = is_top ?
	cff::psTopDictEntries : cff::psPrivateEntries;

    QString key;
    int key_id = -1;

    for (auto &pair: dict) {
        if (!dict_entry_editable (pair.first))
	    continue;
        uint16_t i;
        for (i=0; i<tw->rowCount (); i++) {
	    auto item = tw->item (i, 0);
	    int testop = item->data (Qt::UserRole).toInt ();
	    if (testop == pair.first)
		break;
        }
        if (i == tw->rowCount ()) {
	    key_id = pair.first;
	    key = QString::fromStdString (pair.second);
	    break;
        }
    }
    if (key_id >=0) {
        QAbstractItemModel *mod = tw->model ();
        tw->setRowCount (tw->rowCount () + 1);
        int row = tw->rowCount () - 1;

        QTableWidgetItem *key_item = new QTableWidgetItem (key);
	key_item->setData (Qt::UserRole, key_id);
        tw->setItem (row, 0, key_item);
	if (is_top)
	    adjust_item_data_top (mod, row, key_id);
	else
	    adjust_item_data_private (mod, row, key_id);

        tw->selectRow (row);
        tw->edit (mod->index (row, 0, QModelIndex ()));
    } else {
        FontShepherd::postError (
	    tr ("Can't add a new DICT entry"),
	    tr ("All possible entries are already present in the dictionary."),
    	this);
    }
}

void CffDialog::removeEntry () {
    QWidget *w = m_tab->currentWidget ();
    QTabWidget *tab = qobject_cast<QTabWidget *> (w);
    if (tab) w = tab->currentWidget ();
    QTableWidget *tw = qobject_cast<QTableWidget *> (w);

    if (tw) {
	QItemSelectionModel *sel_mod = tw->selectionModel ();
	QModelIndexList row_lst = sel_mod->selectedRows ();
	if (row_lst.size ()) {
	    QModelIndex rowidx = row_lst.first ();
	    tw->removeRow (rowidx.row ());
	}
    }
}

void CffDialog::onTabChange (int index) {
    QWidget *w = m_tab->widget (index);
    if (w == m_gnTab || w == m_fdSelTab) {
	m_addButton->setEnabled (false);
	m_removeButton->setEnabled (false);
    } else if (w == m_topTab) {
	m_addButton->setEnabled (m_cff->version () < 2);
	m_removeButton->setEnabled (m_cff->version () < 2);
    } else {
	m_addButton->setEnabled (true);
	m_removeButton->setEnabled (true);
    }
}

void CffDialog::setTableVersion (int idx) {
    double newver = m_versionBox->itemData (idx, Qt::UserRole).toFloat ();
    bool update_post = false;
    int choice;
    if (newver == m_cff->version ())
	return;
    PostTable *post = dynamic_cast<PostTable *> (m_font->table (CHR ('p','o','s','t')));
    GlyphNameProvider gnp (*m_font);

    if (newver == 2.0 && !m_cff->cidKeyed () && post->version () == 3.0) {
        choice = FontShepherd::postYesNoQuestion (
	    QCoreApplication::tr ("Switching to 'CFF2'"),
	    QCoreApplication::tr (
		"You have chosen to convert your CFF table to the CFF2 format. "
		"This format doesn't support storing glyph names in the table. "
		"Would you like to move them to the 'post' table?"),
	    this);
        if (choice == QMessageBox::Yes)
	    update_post = true;
    } else if (newver == 1.0 && post->version () == 2.0) {
        choice = FontShepherd::postYesNoQuestion (
	    QCoreApplication::tr ("Switching to 'CFF ' v. 1.0"),
	    QCoreApplication::tr (
		"Are you sure to convert your CFF2 table to the older CFF format? "
		"You will lose all variable font data currently stored in the table."),
	    this);
        if (choice == QMessageBox::No) {
	    m_versionBox->setCurrentIndex
		(m_versionBox->findData (m_cff->version (), Qt::UserRole));
	    return;
	}
        choice = FontShepherd::postYesNoQuestion (
	    QCoreApplication::tr ("Switching to 'CFF ' v. 1.0"),
	    QCoreApplication::tr (
		"Would you like to also remove glyph names from the 'post' "
		"table after copying them to the 'CFF ' table?"),
	    this);
        if (choice == QMessageBox::Yes)
	    update_post = true;
    }
    m_topTab->clearContents ();
    m_topTab->setRowCount (0);
    m_gnTab->clearContents ();
    m_gnTab->setRowCount (0);
    m_tab->setTabVisible (2, !m_cff->cidKeyed () && newver < 2);
    // before actually changing CFF table version, when glyph names are still there
    if (update_post && newver == 2.0) {
	post->setVersion (2.0, &gnp);
	post->packData ();
    }
    try {
	m_cff->setVersion (newver, m_font, gnp);
    } catch (TableDataCompileException& e) {
        FontShepherd::postError (
	    tr ("Can't convert to CFF2"),
	    QString (e.what ()),
	    this);
	return;
    }
    // after actually changing CFF table version, as glyph names have already been imported
    if (update_post && newver == 1.0) {
	post->setVersion (3.0, &gnp);
	post->packData ();
    }
    for (int i=0; i<m_privTab->count (); i++) {
        QWidget *w = m_privTab->widget (i);
	QTableWidget *tw = qobject_cast<QTableWidget *> (w);
	tw->clearContents ();
	tw->setRowCount (0);
        updatePrivateTab (tw, m_cff->privateDict (i));
	m_privTab->setTabText (i, QString::fromStdString (m_cff->numSubFonts () ?
	    m_cff->subFontName (i) : m_cff->fontName ()));
    }
    updateTopTab (m_topTab, m_cff->topDict ());
    if (!m_cff->cidKeyed () && newver < 2)
	fillGlyphTab (m_gnTab);
    if (update_post) {
	TableEdit *ed = post->editor ();
	if (ed) {
	    PostEdit *pe = qobject_cast<PostEdit *> (ed);
	    if (pe) pe->resetData ();
	}
    }
}

QSize CffDialog::minimumSize () const {
    QWidget *w = m_tab->currentWidget ();
    if (w) {
	QSize size = w->size ();

	size += QSize (2, 2);
	return size;
    }
    return QSize ();
}

QSize CffDialog::sizeHint () const {
    return minimumSize ();
}

TopDelegate::TopDelegate (QObject *parent) : QStyledItemDelegate (parent) {
}

QWidget* TopDelegate::createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (option);
    int ptype = index.model ()->data (index, Qt::UserRole).toUInt ();
    QString item_text;
    switch (ptype) {
      case dt_uint:
	return new QSpinBox (parent);
      case dt_float:
	return new QDoubleSpinBox (parent);
      case dt_bool:
	{
	    QComboBox *combo = new QComboBox (parent);
	    combo->addItem ("true");
	    combo->addItem ("false");
	    return combo;
	}
      case dt_sid:
	item_text = index.model ()->data (index, Qt::EditRole).toString ();
	if (item_text.contains (QChar::LineFeed) || item_text.contains (QChar::CarriageReturn)) {
	    return new MultilineInputDialog (
		tr ("Edit multiline name string"),
		tr ("Edit multiline name string:"),
		parent);
	} else {
	    return new QLineEdit (parent);
	}
      default:
	return new QLineEdit (parent);
    }
}

void TopDelegate::setEditorData (QWidget *editor, const QModelIndex &index) const {
    QString ed_type = editor->metaObject ()->className ();
    auto value = index.model ()->data (index, Qt::DisplayRole);
    if (!ed_type.compare ("QComboBox")) {
	QComboBox *combo = qobject_cast<QComboBox *> (editor);
	combo->setCurrentIndex (combo->findText (value.toString ()));
    } else if (!ed_type.compare ("QSpinBox")) {
	QSpinBox *spin = qobject_cast<QSpinBox *> (editor);
	spin->setValue (value.toUInt ());
    } else if (!ed_type.compare ("QDoubleSpinBox")) {
	QDoubleSpinBox *spin = qobject_cast<QDoubleSpinBox *> (editor);
	spin->setMinimum (-10000);
	spin->setMaximum (10000);
	spin->setValue (value.toFloat ());
    } else if (editor->isWindow ()) {
	MultilineInputDialog *mdlg = qobject_cast<MultilineInputDialog *> (editor);
	mdlg->setText (value.toString ());
	mdlg->open ();
	// See comment to MultilineInputDialog::ensureFocus () for explanation
	mdlg->ensureFocus ();
    } else {
	QLineEdit *le = qobject_cast<QLineEdit *> (editor);
	le->setText (value.toString ());
    }
}

void TopDelegate::setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
    int ptype = model->data (index, Qt::UserRole).toUInt ();
    const QModelIndex &opidx = index.siblingAtColumn (0);
    int op = model->data (opidx, Qt::UserRole).toInt ();

    switch (ptype) {
      case dt_uint:
	{
	    QSpinBox *spin = qobject_cast<QSpinBox *> (editor);
	    int val = spin->value ();
	    model->setData (index, val, Qt::EditRole);
	}
	break;
      case dt_bool:
	{
	    QComboBox *combo = qobject_cast<QComboBox *> (editor);
	    QString sval = combo->currentText ();
	    model->setData (index, sval, Qt::EditRole);
	}
	break;
      case dt_float:
	{
	    QDoubleSpinBox *spin = qobject_cast<QDoubleSpinBox *> (editor);
	    double val = spin->value ();
	    model->setData (index, val, Qt::EditRole);
	}
	break;
      case dt_list:
	{
	    QLineEdit *le = qobject_cast<QLineEdit *> (editor);
	    QString txt = le->text ();
	    struct top_dict_entry de;
	    uint8_t size = (op == 5) ? 4 : (op == 14) ? 20 : 6;
	    if (check_float_list (txt, de, size))
		model->setData (index, QString::fromStdString (de.toString ()), Qt::EditRole);
	}
	break;
      case dt_sid:
	{
	    QString txt;
	    bool accepted = false;
	    if (editor->isWindow ()) {
		MultilineInputDialog *mdlg = qobject_cast<MultilineInputDialog *> (editor);
		if (mdlg->result () == QDialog::Accepted) {
		    txt = mdlg->text ();
		    accepted = true;
		}
	    } else {
		QLineEdit *le = qobject_cast<QLineEdit *> (editor);
		txt = le->text ();
		accepted = true;
	    }
	    if (accepted)
		model->setData (index, txt, Qt::EditRole);
	}
	break;
      case dt_ros:
	{
	    QLineEdit *le = qobject_cast<QLineEdit *> (editor);
	    QString txt = le->text ();
	    struct top_dict_entry de;
	    if (check_ros (txt, de))
		model->setData (index, QString::fromStdString (de.toString ()), Qt::EditRole);
	}
	break;
      // This one is used for PS Private and not supposed to be set via the GUI
      case dt_size_off:
	;
    }
}

void TopDelegate::updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (index);
    editor->setGeometry (option.rect);
}

PrivateDelegate::PrivateDelegate (QObject *parent) : QStyledItemDelegate (parent) {
}

QWidget* PrivateDelegate::createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (option);
    int ptype = index.model ()->data (index, Qt::UserRole).toUInt ();
    switch (ptype) {
      case pt_uint:
	return new QSpinBox (parent);
      case pt_bool:
	{
	    QComboBox *combo = new QComboBox (parent);
	    combo->addItem ("true");
	    combo->addItem ("false");
	    return combo;
	}
      default:
	return new QLineEdit (parent);
    }
}

void PrivateDelegate::setEditorData (QWidget *editor, const QModelIndex &index) const {
    QString ed_type = editor->metaObject ()->className ();
    auto value = index.model ()->data (index, Qt::DisplayRole);
    if (!ed_type.compare ("QComboBox")) {
	QComboBox *combo = qobject_cast<QComboBox *> (editor);
	combo->setCurrentIndex (combo->findText (value.toString ()));
    } else if (!ed_type.compare ("QSpinBox")) {
	QSpinBox *spin = qobject_cast<QSpinBox *> (editor);
	spin->setValue (value.toUInt ());
    } else {
	QLineEdit *le = qobject_cast<QLineEdit *> (editor);
	le->setText (value.toString ());
    }
}

void PrivateDelegate::setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
    int ptype = model->data (index, Qt::UserRole).toUInt ();
    switch (ptype) {
      case pt_uint:
	{
	    QSpinBox *spin = qobject_cast<QSpinBox *> (editor);
	    int val = spin->value ();
	    model->setData (index, val, Qt::EditRole);
	}
	break;
      case pt_bool:
	{
	    QComboBox *combo = qobject_cast<QComboBox *> (editor);
	    QString sval = combo->currentText ();
	    model->setData (index, sval, Qt::EditRole);
	}
	break;
      case pt_blend:
	{
	    QLineEdit *le = qobject_cast<QLineEdit *> (editor);
	    QString txt = le->text ();
	    struct blend b;
	    if (check_blend (txt, &b))
		model->setData (index, QString::fromStdString (b.toString ()), Qt::EditRole);
	}
	break;
      case pt_blend_list:
	{
	    QLineEdit *le = qobject_cast<QLineEdit *> (editor);
	    QString txt = le->text ();
	    struct private_entry pe;
	    if (check_blend_list (txt, &pe))
		model->setData (index, QString::fromStdString (pe.toString ()), Qt::EditRole);
	}
    }
}

void PrivateDelegate::updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (index);
    editor->setGeometry (option.rect);
}

CffDictDelegate::CffDictDelegate (bool is_priv, QObject *parent) :
    QStyledItemDelegate (parent), m_private (is_priv) {
}

QWidget* CffDictDelegate::createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (option);
    Q_UNUSED (index);
    QComboBox* combo = new QComboBox (parent);
    const QAbstractItemModel *mod = index.model ();
    auto &dict = m_private ? cff::psPrivateEntries : cff::psTopDictEntries;
    uint16_t i=0;
    for (auto &pair: dict) {
	if (!dict_entry_editable (pair.first))
	    continue;
	combo->addItem (QString::fromStdString (pair.second), pair.first);
	for (uint16_t j=0; j<mod->rowCount (); j++) {
	    auto stest = mod->data (mod->index (j, 0, QModelIndex ()), Qt::DisplayRole).toString ();
	    if (pair.second == stest.toStdString ()) {
		auto boxmod = qobject_cast<QStandardItemModel *> (combo->model ());
		auto item = boxmod->item (i);
		item->setFlags (item->flags () & ~Qt::ItemIsEnabled);
		break;
	    }
	}
	i++;
    }
    return combo;
}

void CffDictDelegate::setEditorData (QWidget *editor, const QModelIndex &index) const {
    QString value = index.model ()->data (index, Qt::DisplayRole).toString ();
    QComboBox* combo = qobject_cast<QComboBox*> (editor);
    combo->view ()->setVerticalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    int idx = combo->findText (value);
    combo->setCurrentIndex (idx);
    combo->view ()->scrollTo (combo->model ()->index (idx, 0));
}

void CffDictDelegate::setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
    QComboBox* comboBox = qobject_cast<QComboBox*> (editor);
    uint16_t value = comboBox->currentIndex ();
    QString combo_text = comboBox->itemText (value);
    int item_data = comboBox->itemData (value, Qt::UserRole).toInt ();
    QString table_text = model->data (index, Qt::EditRole).toString ();

    if (!combo_text.compare (table_text))
	return;

    model->setData (index, combo_text, Qt::EditRole);
    if (m_private)
	adjust_item_data_private (model, index.row (), item_data);
    else
	adjust_item_data_top (model, index.row (), item_data);
}

void CffDictDelegate::updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (index);
    editor->setGeometry (option.rect);
}

FdSelectDelegate::FdSelectDelegate (const QStringList &sflist, QObject *parent) :
    QStyledItemDelegate (parent), m_sflist (sflist) {
}

QWidget* FdSelectDelegate::createEditor (QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (option);
    Q_UNUSED (index);
    QSpinBox *box = new QSpinBox (parent);

    box->setFrame (false);
    box->setMinimum (0);
    box->setMaximum (m_sflist.size () - 1);

    return box;
}

void FdSelectDelegate::setEditorData (QWidget *editor, const QModelIndex &index) const {
    uint32_t value = index.model ()->data (index, Qt::UserRole).toUInt ();
    QSpinBox *box = qobject_cast<QSpinBox*> (editor);
    box->setValue (value);
}

void FdSelectDelegate::setModelData (QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
    QSpinBox* box = qobject_cast<QSpinBox*> (editor);
    uint16_t val = box->value ();
    model->setData (index, val, Qt::UserRole);
    model->setData (index, QString ("%1: %2").arg (val).arg (m_sflist[val]), Qt::DisplayRole);
}

void FdSelectDelegate::updateEditorGeometry (QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED (index);
    editor->setGeometry (option.rect);
}
