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

CServerRpc::CServerRpc ( CServer* pServer, CRpcServer* pRpcServer )
{
    pRpcServer->HandleMethod ( "jamulusserver/getServerInfo", [=] ( const QJsonObject& params, QJsonObject& response ) {
        QJsonObject serverInfo{
            { "name", pServer->GetServerName() },
            { "city", pServer->GetServerCity() },
            { "country", pServer->GetServerCountry() },
            { "welcomeMessage", pServer->GetWelcomeMessage() },
            { "registrationStatus", pServer->GetSvrRegStatus() },
        };
        response["result"] = serverInfo;
    } );

    pRpcServer->HandleMethod ( "jamulusserver/getConnectedClients", [=] ( const QJsonObject& params, QJsonObject& response ) {
        QJsonArray            clients;
        CVector<CHostAddress> vecHostAddresses;
        CVector<QString>      vecsName;
        CVector<int>          veciJitBufNumFrames;
        CVector<int>          veciNetwFrameSizeFact;

        pServer->GetConCliParam ( vecHostAddresses, vecsName, veciJitBufNumFrames, veciNetwFrameSizeFact );

        // we assume that all vectors have the same length
        const int iNumChannels = vecHostAddresses.Size();

        // fill list with connected clients
        for ( int i = 0; i < iNumChannels; i++ )
        {
            if ( !( vecHostAddresses[i].InetAddr == QHostAddress ( static_cast<quint32> ( 0 ) ) ) )
            {
                QJsonObject client{
                    { "address", vecHostAddresses[i].toString ( CHostAddress::SM_IP_PORT ) },
                    { "name", vecsName[i] },
                    { "jitterBufferSize", veciJitBufNumFrames[i] },
                    { "channels", pServer->GetClientNumAudioChannels ( i ) },
                };
                clients.append ( client );
            }
        }

        response["result"] = clients;
    } );

    pRpcServer->HandleMethod ( "jamulusserver/setServerName", [=] ( const QJsonObject& params, QJsonObject& response ) {
        auto jsonServerName = params["serverName"];
        if ( !jsonServerName.isString() )
        {
            response["error"] = CreateJsonRpcError ( -32602, "Invalid params" );
            return;
        }

        pServer->SetServerName ( jsonServerName.toString() );
        response["result"] = "ok";
    } );

    pRpcServer->HandleMethod ( "jamulusserver/setWelcomeMessage", [=] ( const QJsonObject& params, QJsonObject& response ) {
        auto jsonWelcomeMessage = params["welcomeMessage"];
        if ( !jsonWelcomeMessage.isString() )
        {
            response["error"] = CreateJsonRpcError ( -32602, "Invalid params" );
            return;
        }

        pServer->SetWelcomeMessage ( jsonWelcomeMessage.toString() );
        response["result"] = "ok";
    } );
}

CServerRpc::~CServerRpc() {}
