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

#ifndef COMMANDTESTER_H
#define COMMANDTESTER_H

#include "common/command.h"

#include <QObject>
#include <QVariant>

Q_DECLARE_METATYPE(Command)

class Action;

class CommandTester : public QObject
{
    Q_OBJECT
public:
    explicit CommandTester(QObject *parent = NULL);

    /// Stop current processing and clear commands and data.
    void abort();

    /// Abort current processing set new commands and data.
    void setCommands(const QList<Command> &commands, const QVariantMap &data);

    bool isCompleted() const;

    bool hasCommands() const;

    const QVariantMap &data() const;

public slots:
    void start();

signals:
    void commandPassed(const Command &command, bool passed);

private slots:
    void actionFinished();

private:
    void startNext();
    void commandPassed(bool passed);
    bool maybeFinish();

    QList<Command> m_commands;
    QVariantMap m_data;
    Action *m_action;
    bool m_abort;
};

#endif // COMMANDTESTER_H
