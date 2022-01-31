/* Copyright (C) 2000-2012 by George Williams
 * Copyright (C) 2022 by Alexey Kryukov
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

#include "fs_notify.h"

void FontShepherd::postWarning (QString title, QString text, QWidget *w) {
    QMessageBox::warning (w, title, text);
}

void FontShepherd::postWarning (QString text) {
    qDebug () << "Warning:" << text;
}

void FontShepherd::postError (QString title, QString text, QWidget *w) {
    QMessageBox::critical (w, title, text);
}

void FontShepherd::postError (QString text) {
    qDebug () << "Error:" << text;
}

void FontShepherd::postNotice (QString title, QString text, QWidget *w) {
    QMessageBox::information (w, title, text);
}

void FontShepherd::postNotice (QString text) {
    qDebug () << text;
}

int FontShepherd::postYesNoQuestion (QString title, QString text, QWidget *w) {
    QMessageBox msgBox (w);
    msgBox.setText (text);
    msgBox.setWindowTitle (title);
    msgBox.setIcon (QMessageBox::Question);
    msgBox.setStandardButtons (QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton (QMessageBox::Yes);
    int ret = msgBox.exec();
    return ret;
}

