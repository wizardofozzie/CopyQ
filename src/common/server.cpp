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

#include "server.h"

#include "common/arguments.h"
#include "common/clientsocket.h"
#include "common/client_server.h"
#include "common/log.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>

#ifdef Q_OS_WIN
#   include <qt_windows.h>
#endif

namespace {

#ifdef Q_OS_WIN
class SystemWideMutex {
public:
    SystemWideMutex(const QString &name)
        : m_mutex(NULL)
    {
#   ifdef UNICODE
        const QString text = "Global\\" + QString::fromLatin1(name.toLatin1());
#   else
        const QByteArray data = "Global\\" + name.toLatin1();
#   endif

        m_mutex = CreateMutex(
                    NULL, FALSE,
            #   ifdef UNICODE
                    reinterpret_cast<LPCWSTR>(text.utf16())
            #   else
                    reinterpret_cast<LPCSTR>(data.data())
            #   endif
                    );

        m_error = GetLastError();
    }

    operator bool() const
    {
        return GetLastError() != ERROR_ALREADY_EXISTS;
    }

    ~SystemWideMutex()
    {
        if (m_mutex)
            CloseHandle(m_mutex);
    }
private:
    Q_DISABLE_COPY(SystemWideMutex)

    HANDLE m_mutex;
    DWORD m_error;
};
#endif

bool serverIsRunning(const QString &serverName)
{
    QLocalSocket socket;
    socket.connectToServer(serverName);
    return socket.waitForConnected(-1);
}

QLocalServer *newServer(const QString &name, QObject *parent)
{
    COPYQ_LOG( QString("Starting server \"%1\".").arg(name) );

    QLocalServer *server = new QLocalServer(parent);

#ifdef Q_OS_WIN
    // On Windows, it's possible to have multiple local servers listening with same name.
    // This handles race condition when creating new server.
    SystemWideMutex mutex(name);
    if (!mutex)
        return server;
#endif

    if ( !serverIsRunning(name) ) {
        QLocalServer::removeServer(name);
        server->listen(name);
    }

    return server;
}

} // namespace

Server::Server(const QString &name, QObject *parent)
    : QObject(parent)
    , m_server(newServer(name, this))
    , m_socketCount(0)
{
    COPYQ_LOG( QString(isListening()
                       ? "Server \"%1\" started."
                       : "Server \"%1\" already running!").arg(name) );

    qRegisterMetaType<Arguments>("Arguments");
    connect( qApp, SIGNAL(aboutToQuit()), SLOT(close()) );
}

void Server::start()
{
    while (m_server->hasPendingConnections())
        onNewConnection();

    connect( m_server, SIGNAL(newConnection()),
             this, SLOT(onNewConnection()) );
}

bool Server::isListening() const
{
    return m_server->isListening();
}

void Server::onNewConnection()
{
    QLocalSocket* socket = m_server->nextPendingConnection();
    if (!socket) {
        log("No pending client connections!", LogError);
    } else if ( socket->state() != QLocalSocket::ConnectedState ) {
        log("Client is not connected!", LogError);
        socket->deleteLater();
    } else {
        QScopedPointer<ClientSocket> clientSocket( new ClientSocket(socket) );

        const Arguments args = clientSocket->readArguments();
        if ( !args.isEmpty() ) {
            ++m_socketCount;
            connect( clientSocket.data(), SIGNAL(destroyed()),
                     this, SLOT(onSocketClosed()) );
            connect( this, SIGNAL(destroyed()),
                     clientSocket.data(), SLOT(close()) );
            connect( this, SIGNAL(destroyed()),
                     clientSocket.data(), SLOT(deleteAfterDisconnected()) );
            emit newConnection( args, clientSocket.take() );
        }
    }
}

void Server::onSocketClosed()
{
    Q_ASSERT(m_socketCount > 0);
    --m_socketCount;
}

void Server::close()
{
    m_server->close();

    COPYQ_LOG( QString("Sockets open: %1").arg(m_socketCount) );
    while (m_socketCount > 0)
        QCoreApplication::processEvents();

    deleteLater();
}
