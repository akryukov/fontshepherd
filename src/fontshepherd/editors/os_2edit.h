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
#include "tables.h" // Have to load it here due to inheritance from TableEdit

class sfntFile;
typedef struct ttffont sFont;
class FontTable;
class OS_2Table;
class TableEdit;
class UniSpinBox;

struct uni_range {
    QString rangeName;
    uint32_t first;
    uint32_t last;
};

class OS_2Edit : public TableEdit {
    Q_OBJECT;

public:
    OS_2Edit (std::shared_ptr<FontTable> tptr, sFont* font, QWidget *parent);
    ~OS_2Edit () {};

    void resetData () override {};
    bool checkUpdate (bool can_cancel) override;
    bool isModified () override;
    bool isValid () override;
    std::shared_ptr<FontTable> table () override;

    void closeEvent (QCloseEvent *event);
    QSize sizeHint () const override;

public slots:
    void save ();

private slots:
    void setFamilyClass (int family);
    void setPanoseFamily (int family);
    void setTableVersion (int version);

private:
    void fillControls ();

    static QVector<QPair<QString, int>> fsRestrictionsList;

    static QVector<QPair<QString, int>> usWeightList;
    static QVector<QPair<QString, int>> usWidthList;
    static QVector<QPair<QString, int>> ibmFamList;
    static QVector<QPair<QString, int>> ibmSubFamListDefault;
    static QMap<int, QVector<QPair<QString, int>>> ibmSubFamLists;
    static QVector<QPair<QString, int>> selectionFlags;

    static QStringList panoseFam;
    static QMap<int, QVector<QPair<QString, QVector<QString>>>> panose;

    static QVector<QVector <uni_range>> uniRangeList;
    static QVector<QPair<QString, int>> codepageList;

    std::shared_ptr<OS_2Table> m_os_2;
    sFont *m_font;
    bool m_valid;

    QTabWidget *m_tab;

    QSpinBox *m_versionBox;
    QComboBox *m_licenseBox;
    QCheckBox *m_noSubsettingBox, *m_bitmapsBox;
    QLineEdit *m_vendorIDBox;
    UniSpinBox *m_firstCharBox, *m_lastCharBox, *m_defaultCharBox, *m_breakCharBox;
    QSpinBox *m_maxContextBox, *m_lowerOptSizeBox, *m_upperOptSizeBox;

    QSpinBox *m_avgCharWidthBox;
    QSpinBox *m_typoAscenderBox, *m_typoDescenderBox, *m_typoLineGapBox;
    QSpinBox *m_winAscentBox, *m_winDescentBox;
    QSpinBox *m_xHeightBox, *m_capHeightBox;

    QSpinBox *m_ySubscriptXSizeBox, *m_ySubscriptYSizeBox;
    QSpinBox *m_ySubscriptXOffsetBox, *m_ySubscriptYOffsetBox;
    QSpinBox *m_ySuperscriptXSizeBox, *m_ySuperscriptYSizeBox;
    QSpinBox *m_ySuperscriptXOffsetBox, *m_ySuperscriptYOffsetBox;
    QSpinBox *m_yStrikeoutSizeBox, *m_yStrikeoutPositionBox;

    QComboBox *m_weightClassBox, *m_widthClassBox;
    QComboBox *m_FamilyClassBox, *m_FamilySubClassBox;
    QListWidget *m_selectionWidget;

    QComboBox *m_panoseBox[10];
    QLabel *m_panoseLabel[10];

    QListWidget *m_uniWidget, *m_cpWidget;

    QPushButton *saveButton, *closeButton, *helpButton;
};
