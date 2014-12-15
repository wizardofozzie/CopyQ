/*
    Copyright (c) 2014, Lukas Holecek <hluk@email.cz>

    This file is part of CopyQ.

    CopyQ is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CopyQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CopyQ.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "winplatformclipboard.h"

#include <QTimer>

WinPlatformClipboard::WinPlatformClipboard()
    : DummyClipboard(false)
    , m_lastClipboardSequenceNumber(-1)
    , m_emitChanged(false)
{
    /* Clipboard needs to be checked in intervals since
     * the QClipboard::changed() signal is not emitted in some cases on Windows.
     */
    QTimer *timer = new QTimer(this);
    timer->setInterval(200);
    connect( timer, SIGNAL(timeout()),
             this, SLOT(checkClipboard()) );
    timer->start();
}

void WinPlatformClipboard::checkClipboard()
{
    const DWORD newClipboardSequenceNumber = GetClipboardSequenceNumber();

    if (m_lastClipboardSequenceNumber != newClipboardSequenceNumber) {
        m_lastClipboardSequenceNumber = newClipboardSequenceNumber;
        m_emitChanged = true;
    } else if (m_emitChanged) {
        m_emitChanged = false;
        emit changed(Clipboard);
    }
}
