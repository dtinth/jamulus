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

#include "rpcserver.h"

CRpcServer::CRpcServer ( int iPort ) : iPort ( iPort ), pTransportServer ( new QTcpServer ( this ) )
{
    connect ( pTransportServer, &QTcpServer::newConnection, this, &CRpcServer::OnNewConnection );
}

CRpcServer::~CRpcServer()
{
    if ( pTransportServer->isListening() )
    {
        qInfo() << "- stopping RPC server";
        pTransportServer->close();
    }
}

void CRpcServer::Start()
{
    if ( iPort < 0 )
    {
        return;
    }
    if ( pTransportServer->listen ( QHostAddress ( "127.0.0.1" ), iPort ) )
    {
        qInfo() << "- RPC server started";
    }
    else
    {
        qInfo() << "- unable to start RPC server:" << pTransportServer->errorString();
    }
}

static QJsonObject CreateJsonRpcError ( int code, QString message )
{
    QJsonObject error;
    error["code"]    = QJsonValue ( code );
    error["message"] = QJsonValue ( message );
    return error;
}

static QJsonObject CreateJsonRpcErrorReply ( int code, QString message )
{
    QJsonObject object;
    object["jsonrpc"] = QJsonValue ( "2.0" );
    object["error"]   = CreateJsonRpcError ( code, message );
    return object;
}

void CRpcServer::OnNewConnection()
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
                    Send ( pSocket, QJsonDocument ( CreateJsonRpcErrorReply ( -32700, "Parse error" ) ) );
                }
                else
                {
                    Send ( pSocket, QJsonDocument ( CreateJsonRpcErrorReply ( -32600, "Invalid Request" ) ) );
                }
            }
            else if ( data.isArray() )
            {
                QJsonArray output;
                for ( auto item : data.array() )
                {
                    if ( item.isObject() )
                    {
                        auto        object = item.toObject();
                        QJsonObject response;
                        response["jsonrpc"] = QJsonValue ( "2.0" );
                        response["id"]      = object["id"];
                        ProcessMessage ( pSocket, object, response );
                        output.append ( response );
                    }
                    else
                    {
                        output.append ( CreateJsonRpcErrorReply ( -32600, "Invalid Request" ) );
                    }
                }
                if ( output.size() > 0 )
                {
                    Send ( pSocket, QJsonDocument ( output ) );
                }
                else
                {
                    Send ( pSocket, QJsonDocument ( CreateJsonRpcErrorReply ( -32600, "Invalid Request" ) ) );
                }
            }
            else if ( data.isObject() )
            {
                auto        object = data.object();
                QJsonObject response;
                response["jsonrpc"] = QJsonValue ( "2.0" );
                response["id"]      = object["id"];
                ProcessMessage ( pSocket, object, response );
                Send ( pSocket, QJsonDocument ( response ) );
            }
            else
            {
                Send ( pSocket, QJsonDocument ( CreateJsonRpcErrorReply ( -32600, "Invalid Request" ) ) );
            }
        }
    } );
}

void CRpcServer::Send ( QTcpSocket* pSocket, const QJsonDocument& aMessage ) { pSocket->write ( aMessage.toJson ( QJsonDocument::Compact ) + "\n" ); }

void CRpcServer::ProcessMessage ( QTcpSocket* pSocket, QJsonObject message, QJsonObject& response )
{
    qInfo() << "- message from:" << pSocket->peerAddress() << message;
    response["error"] = CreateJsonRpcError ( -32601, "Method not found" );
}

void CRpcServer::HandleMethod ( const QString& strMethod, CRpcHandler pHandler ) { mapMethodHandlers[strMethod] = pHandler; }