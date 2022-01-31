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
class HeadTable;
class TableEdit;

class HeadEdit : public TableEdit {
    Q_OBJECT;

public:
    HeadEdit (FontTable* tab, sFont* font, QWidget *parent);
    ~HeadEdit () {};

    void resetData () override;
    bool checkUpdate (bool can_cancel) override;
    bool isModified () override;
    bool isValid () override;
    FontTable* table () override;

    void closeEvent (QCloseEvent *event);

public slots:
    void save ();

private:
    void fillControls ();

    static QStringList flagDesc;
    static QStringList macStyleDesc;
    static QList<QPair<QString, int>> fontDirHints;

    HeadTable *m_head;
    sFont *m_font;
    bool m_valid;

    QTabWidget *m_tab;
    QDoubleSpinBox *m_versionBox, *m_fontRevisionBox;
    QLineEdit *m_checkSumField, *m_magicField;
    QListWidget *m_flagList;
    QSpinBox *m_unitsPerEmBox;
    QDateTimeEdit *m_createdBox, *m_modifiedBox;
    QSpinBox *m_xMinBox, *m_yMinBox;
    QSpinBox *m_xMaxBox, *m_yMaxBox;
    QListWidget *m_macStyleList;
    QSpinBox *m_lowestRecBox;
    QComboBox  *m_fontDirectionBox;
    QComboBox  *m_indexToLocFormatBox;
    QSpinBox  *m_glyphDataFormatBox;
    QPushButton *saveButton, *closeButton, *helpButton;
};
