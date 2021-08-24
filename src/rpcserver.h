/******************************************************************************\
 * Copyright (c) 2004-2020
 *
 * Author(s):
 *  Volker Fischer
 *  dtinth
 *
 ******************************************************************************
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 *
\******************************************************************************/

#pragma once

#include <QObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QVector>
#include <memory>

typedef std::function<void ( const QJsonObject&, QJsonObject& )> CRpcHandler;

/* Classes ********************************************************************/
class CRpcServer : public QObject
{
    Q_OBJECT

public:
    CRpcServer ( int iPort );
    virtual ~CRpcServer();

    void Start();
    void HandleMethod ( const QString& strMethod, CRpcHandler pHandler );
    void BroadcastNotification ( const QString& strMethod, const QJsonObject& aParams );

private:
    int         iPort;
    QTcpServer* pTransportServer;

    // A map from method name to handler functions
    QMap<QString, CRpcHandler> mapMethodHandlers;
    QVector<QTcpSocket*>       vecClients;

    void ProcessMessage ( QTcpSocket* pSocket, QJsonObject message, QJsonObject& response );
    void Send ( QTcpSocket* pSocket, const QJsonDocument& aMessage );

protected slots:
    void OnNewConnection();
};

/* Utilities ********************************************************************/
QJsonObject CreateJsonRpcError ( int code, QString message );