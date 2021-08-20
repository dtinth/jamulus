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

#include "serverrpc.h"

CServerRpc::CServerRpc ( CServer* pServer ) : pServer ( pServer ), pTransportServer ( new QTcpServer ( this ) )
{
    connect ( pTransportServer, &QTcpServer::newConnection, this, &CServerRpc::OnNewConnection );
}

CServerRpc::~CServerRpc() { qInfo() << "- server destr0y!!"; }

void CServerRpc::Start()
{
    QString listenPath ( QDir::tempPath() + "/jams.sock" );
    qInfo() << "- listen to " << listenPath;
    if ( pTransportServer->listen ( QHostAddress ( "127.0.0.1" ), 22123 ) )
    {
        qInfo() << "- server start!!";
    }
    else
    {
        qInfo() << "- server cannot start:" << pTransportServer->errorString();
    }
}

static QJsonObject CreateJsonRpcError ( int code, QString message )
{
    QJsonObject error;
    error["code"]    = QJsonValue ( code );
    error["message"] = QJsonValue ( message );
    return error;
}

static void ReplyError ( QTcpSocket* pSocket, int code, QString message )
{
    QJsonObject object;
    object["jsonrpc"] = QJsonValue ( "2.0" );
    object["error"]   = CreateJsonRpcError ( code, message );
    pSocket->write ( QJsonDocument ( object ).toJson ( QJsonDocument::Compact ) + "\n" );
}

void CServerRpc::OnNewConnection()
{
    QTcpSocket* pSocket = pTransportServer->nextPendingConnection();
    if ( !pSocket )
    {
        return;
    }
    qInfo() << "- accept connection from:" << pSocket->peerAddress().toString();
    connect ( pSocket, &QTcpSocket::readyRead, [this, pSocket]() {
        while ( pSocket->canReadLine() )
        {
            QByteArray      line = pSocket->readLine();
            QJsonParseError parseError;
            QJsonDocument   data = QJsonDocument::fromJson ( line, &parseError );

            if ( data.isNull() )
            {
                if ( parseError.error != QJsonParseError::NoError )
                {
                    ReplyError ( pSocket, -32700, "Parse error" );
                }
                else
                {
                    ReplyError ( pSocket, -32600, "Invalid Request" );
                }
            }
            else if ( data.isArray() )
            {
                int count = 0;
                for ( auto item : data.array() )
                {
                    count++;
                    if ( item.isObject() )
                    {
                        ProcessMessage ( pSocket, item.toObject() );
                    }
                    else
                    {
                        ReplyError ( pSocket, -32600, "Invalid Request" );
                    }
                }
                if ( count == 0 )
                {
                    ReplyError ( pSocket, -32600, "Invalid Request" );
                }
            }
            else if ( data.isObject() )
            {
                ProcessMessage ( pSocket, data.object() );
            }
            else
            {
                ReplyError ( pSocket, -32600, "Invalid Request" );
            }
        }
    } );
}

void CServerRpc::ProcessMessage ( QTcpSocket* pSocket, QJsonObject message )
{
    qInfo() << "- message from:" << pSocket->peerAddress() << message;
    QJsonObject result;
    result["jsonrpc"] = QJsonValue ( "2.0" );
    result["error"]   = CreateJsonRpcError ( -32601, "Method not found" );
    pSocket->write ( QJsonDocument ( result ).toJson ( QJsonDocument::Compact ) + "\n" );
}
