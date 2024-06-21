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
#include <unicode/uchar.h>

#include "sfnt.h"
#include "editors/heaedit.h" // also includes tables.h
#include "tables/hea.h"

#include "fs_notify.h"

QStringList HeaEdit::hLabels = {
    QWidget::tr ("Version number of the table"),
    QWidget::tr ("Typographic ascender"),
    QWidget::tr ("Typographic descender"),
    QWidget::tr ("Typographic line gap"),
    QWidget::tr ("Maximum advance width"),
    QWidget::tr ("Minimum left sidebearing"),
    QWidget::tr ("Minimum right sidebearing"),
    QWidget::tr ("Maximum x-extent"),
    QWidget::tr ("Caret slope rise"),
    QWidget::tr ("Caret slope run"),
    QWidget::tr ("Caret offset"),
    QWidget::tr ("Metric data format"),
    QWidget::tr ("Number of advance widths in 'hmtx' table"),
};

QStringList HeaEdit::vLabels = {
    QWidget::tr ("Version number of the table"),
    QWidget::tr ("Vertical typographic ascender"),
    QWidget::tr ("Vertical typographic descender"),
    QWidget::tr ("Vertical typographic line gap"),
    QWidget::tr ("Maximum advance height"),
    QWidget::tr ("Minimum top sidebearing"),
    QWidget::tr ("Minimum bottom sidebearing"),
    QWidget::tr ("Maximum y-extent"),
    QWidget::tr ("Caret slope rise"),
    QWidget::tr ("Caret slope run"),
    QWidget::tr ("Caret offset"),
    QWidget::tr ("Metric data format"),
    QWidget::tr ("Number of advance heights in 'vmtx' table"),
};

HeaEdit::HeaEdit (std::shared_ptr<FontTable> tptr, sFont* font, QWidget *parent) :
    TableEdit (parent, Qt::Window), m_font (font) {
    m_hea = std::dynamic_pointer_cast<HeaTable> (tptr);

    setAttribute (Qt::WA_DeleteOnClose);
    setWindowTitle (QString ("%1 - %2").arg
	(QString::fromStdString (m_hea->stringName ())).arg (m_font->fontname));

    QWidget *window = new QWidget (this);
    QGridLayout *hea_layout = new QGridLayout ();

    QStringList &lst = m_hea->isVertical () ? vLabels : hLabels;
    for (int i=0; i<lst.size (); i++)
	hea_layout->addWidget (new QLabel (lst[i]), i, 0);

    m_versionBox = new QDoubleSpinBox ();
    hea_layout->addWidget (m_versionBox, 0, 1);

    m_ascentBox = new QSpinBox ();
    m_ascentBox->setMinimum (-32767);
    m_ascentBox->setMaximum (32767);
    hea_layout->addWidget (m_ascentBox, 1, 1);

    m_descentBox = new QSpinBox ();
    m_descentBox->setMinimum (-32767);
    m_descentBox->setMaximum (32767);
    hea_layout->addWidget (m_descentBox, 2, 1);

    m_lineGapBox = new QSpinBox ();
    m_lineGapBox->setMinimum (-32767);
    m_lineGapBox->setMaximum (32767);
    hea_layout->addWidget (m_lineGapBox, 3, 1);

    m_advanceMaxBox = new QSpinBox ();
    m_advanceMaxBox->setMinimum (-32767);
    m_advanceMaxBox->setMaximum (32767);
    hea_layout->addWidget (m_advanceMaxBox, 4, 1);

    m_minStartSideBearingBox = new QSpinBox ();
    m_minStartSideBearingBox->setMinimum (-32767);
    m_minStartSideBearingBox->setMaximum (32767);
    hea_layout->addWidget (m_minStartSideBearingBox, 5, 1);

    m_minEndSideBearingBox = new QSpinBox ();
    m_minEndSideBearingBox->setMinimum (-32767);
    m_minEndSideBearingBox->setMaximum (32767);
    hea_layout->addWidget (m_minEndSideBearingBox, 6, 1);

    m_maxExtentBox = new QSpinBox ();
    m_maxExtentBox->setMinimum (-32767);
    m_maxExtentBox->setMaximum (32767);
    hea_layout->addWidget (m_maxExtentBox, 7, 1);

    m_caretSlopeRiseBox = new QSpinBox ();
    m_caretSlopeRiseBox->setMinimum (-32767);
    m_caretSlopeRiseBox->setMaximum (32767);
    hea_layout->addWidget (m_caretSlopeRiseBox, 8, 1);

    m_caretSlopeRunBox = new QSpinBox ();
    m_caretSlopeRunBox->setMinimum (-32767);
    m_caretSlopeRunBox->setMaximum (32767);
    hea_layout->addWidget (m_caretSlopeRunBox, 9, 1);

    m_caretOffsetBox = new QSpinBox ();
    m_caretOffsetBox->setMinimum (-32767);
    m_caretOffsetBox->setMaximum (32767);
    hea_layout->addWidget (m_caretOffsetBox, 10, 1);

    m_metricDataFormatBox = new QSpinBox ();
    hea_layout->addWidget (m_metricDataFormatBox, 11, 1);

    m_numOfMetricsBox = new QSpinBox ();
    m_numOfMetricsBox->setMaximum (0xffff);
    m_numOfMetricsBox->setEnabled (false);
    hea_layout->addWidget (m_numOfMetricsBox, 12, 1);

    QVBoxLayout *layout = new QVBoxLayout ();
    layout->addLayout (hea_layout);

    saveButton = new QPushButton (tr ("&Compile table"));
    closeButton = new QPushButton (tr ("C&lose"));

    QHBoxLayout *buttLayout = new QHBoxLayout ();
    buttLayout->addWidget (saveButton);
    buttLayout->addWidget (closeButton);
    layout->addLayout (buttLayout);

    connect (saveButton, &QPushButton::clicked, this, &HeaEdit::save);
    connect (closeButton, &QPushButton::clicked, this, &HeaEdit::close);

    window->setLayout (layout);
    setCentralWidget (window);
    fillControls ();

    m_valid = true;
}

void HeaEdit::fillControls () {
    m_versionBox->setValue (m_hea->version ());
    m_ascentBox->setValue (m_hea->ascent ());
    m_descentBox->setValue (m_hea->descent ());
    m_lineGapBox->setValue (m_hea->lineGap ());
    m_advanceMaxBox->setValue (m_hea->advanceMax ());
    m_minStartSideBearingBox->setValue (m_hea->minStartSideBearing ());
    m_minEndSideBearingBox->setValue (m_hea->minEndSideBearing ());
    m_maxExtentBox->setValue (m_hea->maxExtent ());
    m_caretSlopeRiseBox->setValue (m_hea->caretSlopeRise ());
    m_caretSlopeRunBox->setValue (m_hea->caretSlopeRun ());
    m_caretOffsetBox->setValue (m_hea->caretOffset ());
    m_metricDataFormatBox->setValue (m_hea->metricDataFormat ());
    m_numOfMetricsBox->setValue (m_hea->numOfMetrics ());
}

bool HeaEdit::checkUpdate (bool) {
    return true;
}

bool HeaEdit::isModified () {
    return m_hea->modified ();
}

bool HeaEdit::isValid () {
    return m_valid;
}

std::shared_ptr<FontTable> HeaEdit::table () {
    return m_hea;
}

void HeaEdit::closeEvent (QCloseEvent *event) {
    // If we are going to delete the font, ignore changes in table edits
    if (!isModified () || checkUpdate (true))
        m_hea->clearEditor ();
    else
        event->ignore ();
}

void HeaEdit::save () {
    hea_data &hd = m_hea->contents;
    hd.version = m_versionBox->value ();
    hd.ascent = m_ascentBox->value ();
    hd.descent = m_descentBox->value ();
    hd.lineGap = m_lineGapBox->value ();
    hd.advanceMax = m_advanceMaxBox->value ();
    hd.minStartSideBearing = m_minStartSideBearingBox->value ();
    hd.minEndSideBearing = m_minEndSideBearingBox->value ();
    hd.maxExtent = m_maxExtentBox->value ();
    hd.caretSlopeRise = m_caretSlopeRiseBox->value ();
    hd.caretSlopeRun = m_caretSlopeRunBox->value ();
    hd.caretOffset = m_caretOffsetBox->value ();
    hd.metricDataFormat = m_metricDataFormatBox->value ();
    hd.numOfMetrics = m_numOfMetricsBox->value ();

    m_hea->packData ();
    emit (update (m_hea));
    close ();
}
