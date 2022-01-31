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
class HeaTable;
class TableEdit;

class HeaEdit : public TableEdit {
    Q_OBJECT;

public:
    HeaEdit (FontTable* tab, sFont* font, QWidget *parent);
    ~HeaEdit () {};

    void resetData () override {};
    bool checkUpdate (bool can_cancel) override;
    bool isModified () override;
    bool isValid () override;
    FontTable* table () override;

    void closeEvent (QCloseEvent *event);

public slots:
    void save ();

private:
    void fillControls ();

    static QStringList vLabels;
    static QStringList hLabels;

    HeaTable *m_hea;
    sFont *m_font;
    bool m_valid;

    QDoubleSpinBox *m_versionBox;
    QSpinBox *m_ascentBox, *m_descentBox, *m_lineGapBox;
    QSpinBox *m_advanceMaxBox;
    QSpinBox *m_minStartSideBearingBox, *m_minEndSideBearingBox;
    QSpinBox *m_maxExtentBox;
    QSpinBox *m_caretSlopeRiseBox, *m_caretSlopeRunBox, *m_caretOffsetBox;
    QSpinBox *m_metricDataFormatBox, *m_numOfMetricsBox;

    QPushButton *saveButton, *closeButton, *helpButton;
};
