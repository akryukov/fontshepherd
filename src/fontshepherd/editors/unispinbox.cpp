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

#include "editors/unispinbox.h"
#include "icuwrapper.h"

int get_hex_value (const QString *text) {
    QByteArray ba = text->toLocal8Bit ();
    int ret=-1;
    const char *c_str = ba.data();
    sscanf (c_str, "U+%x", &ret);
    return ret;
}

UniSpinBox::UniSpinBox (QWidget *parent) : QSpinBox (parent) {
}

QString UniSpinBox::textFromValue (int val) const {
    if (val < 0)
	return QString ("<unassigned>");
    return QString("U+%1").arg (val, val <= 0xFFFF ? 4 : 6, 16, QLatin1Char ('0')).toUpper ();
}

QValidator::State UniSpinBox::validate (QString &text, int &pos) const {
    Q_UNUSED (pos);
    if (get_hex_value (&text) >= -1)
	return QValidator::State::Acceptable;
    else
	return QValidator::State::Invalid;
}

int UniSpinBox::valueFromText (const QString &text) const {
    int ret = get_hex_value (&text);
    return (ret);
}

void UniSpinBox::setValue (int val) {
    // Seems to be triggered only when the value is changed programmatically.
    // So use this to set the initial value to compare further changes with.
    m_oldval = val;
    QSpinBox::setValue (val);
}

void UniSpinBox::onValueChange (int val) {
    setToolTip (QString::fromStdString (IcuWrapper::unicodeCharName (val)));

    QSpinBox::setValue (val);
    if (val > m_oldval) emit (valueUp ());
    else if (val < m_oldval) emit (valueDown ());
    m_oldval = val;
}

VarSelectorBox::VarSelectorBox (QWidget *parent) :
    UniSpinBox (parent) {
    this->setMinimum (0xfe00);
    this->setMaximum (0xe01ef);
    connect (this, static_cast<void (QSpinBox::*)(int)> (&QSpinBox::valueChanged),
	this, &VarSelectorBox::onValueChange);
}

void VarSelectorBox::onValueChange (int val) {
    if (val > m_oldval) {
	if (val == 0xfe10)
	    val = 0xe0100;
	emit (valueUp ());
    } else if (val < m_oldval) {
	if (val == 0xe00ff)
	    val = 0xfe0f;
	emit (valueDown ());
    }
    QSpinBox::setValue (val);
    setToolTip (QString::fromStdString (IcuWrapper::unicodeCharName (val)));
    m_oldval = val;
}

QValidator::State VarSelectorBox::validate (QString &text, int &pos) const {
    Q_UNUSED (pos);
    int val = get_hex_value (&text);
    if ((val >= 0xfe00 && val <= 0xfe0f) || (val >= 0xe0100 && val <= 0xe01ef))
	return QValidator::State::Acceptable;
    else
	return QValidator::State::Invalid;
}
