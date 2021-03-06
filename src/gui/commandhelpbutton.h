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

#ifndef COMMANDHELPBUTTON_H
#define COMMANDHELPBUTTON_H

#include <QWidget>

class QPushButton;
class QTextBrowser;

class CommandHelpButton : public QWidget
{
    Q_OBJECT
public:
    explicit CommandHelpButton(QWidget *parent = NULL);

public slots:
    void setHelpVisible(bool visible);

signals:
    void hidden();

private:
    QPushButton *m_button;
    QTextBrowser *m_help;
};

#endif // COMMANDHELPBUTTON_H
