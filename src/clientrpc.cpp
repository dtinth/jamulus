/******************************************************************************\
 * Copyright (c) 2021
 *
 * Author(s):
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

#include "clientrpc.h"

CClientRpc::CClientRpc ( QObject* parent, CClient* pClient, CRpcServer* pRpcServer ) : QObject ( parent )
{
    connect ( pClient, &CClient::ChatTextReceived, [=] ( QString strChatText ) {
        pRpcServer->BroadcastNotification ( "jamulusclient/chatTextReceived",
                                            QJsonObject{
                                                { "chatText", strChatText },
                                            } );
    } );

    connect ( pClient, &CClient::ClientIDReceived, [=] ( int iChanID ) {
        pRpcServer->BroadcastNotification ( "jamulusclient/connected",
                                            QJsonObject{
                                                { "id", iChanID },
                                            } );
    } );

    connect ( pClient, &CClient::ConClientListMesReceived, [=] ( CVector<CChannelInfo> vecChanInfo ) {
        QJsonArray arrChanInfo;
        for ( const auto& chanInfo : vecChanInfo )
        {
            QJsonObject objChanInfo{
                { "id", chanInfo.iChanID },
                { "name", chanInfo.strName },
                { "skillLevel", SerializeSkillLevel ( chanInfo.eSkillLevel ) },
                { "countryId", chanInfo.eCountry },
                { "city", chanInfo.strCity },
                { "instrumentId", chanInfo.iInstrument },
            };
            arrChanInfo.append ( objChanInfo );
        }
        pRpcServer->BroadcastNotification ( "jamulusclient/clientListReceived",
                                            QJsonObject{
                                                { "clients", arrChanInfo },
                                            } );
        arrStoredChanInfo = arrChanInfo;
    } );

    connect ( pClient, &CClient::CLChannelLevelListReceived, [=] ( CHostAddress /* unused */, CVector<uint16_t> vecLevelList ) {
        QJsonArray arrLevelList;
        for ( const auto& level : vecLevelList )
        {
            arrLevelList.append ( level );
        }
        pRpcServer->BroadcastNotification ( "jamulusclient/channelLevelListReceived",
                                            QJsonObject{
                                                { "channelLevelList", arrLevelList },
                                            } );
    } );

    connect ( pClient, &CClient::Disconnected, [=]() { pRpcServer->BroadcastNotification ( "jamulusclient/disconnected", QJsonObject{} ); } );

    pRpcServer->HandleMethod ( "jamulus/getMode", [=] ( const QJsonObject& params, QJsonObject& response ) {
        QJsonObject result{ { "mode", "client" } };
        response["result"] = result;
    } );

    pRpcServer->HandleMethod ( "jamulusclient/getClientInfo", [=] ( const QJsonObject& params, QJsonObject& response ) {
        QJsonObject result{ { "connected", pClient->IsConnected() } };
        response["result"] = result;
    } );

    pRpcServer->HandleMethod ( "jamulusclient/getChannelInfo", [=] ( const QJsonObject& params, QJsonObject& response ) {
        QJsonObject result{
            { "name", pClient->ChannelInfo.strName },
            { "skillLevel", SerializeSkillLevel ( pClient->ChannelInfo.eSkillLevel ) },
        };
        response["result"] = result;
    } );

    pRpcServer->HandleMethod ( "jamulusclient/getClientList", [=] ( const QJsonObject& params, QJsonObject& response ) {
        QJsonObject result{
            { "clients", arrStoredChanInfo },
        };
        response["result"] = result;
    } );

    pRpcServer->HandleMethod ( "jamulusclient/setName", [=] ( const QJsonObject& params, QJsonObject& response ) {
        auto jsonName = params["name"];
        if ( !jsonName.isString() )
        {
            response["error"] = CRpcServer::CreateJsonRpcError ( -32602, "Invalid params: name is not a string" );
            return;
        }

        pClient->ChannelInfo.strName = jsonName.toString().left ( MAX_LEN_FADER_TAG );
        pClient->SetRemoteInfo();

        response["result"] = "ok";
    } );

    pRpcServer->HandleMethod ( "jamulusclient/setSkillLevel", [=] ( const QJsonObject& params, QJsonObject& response ) {
        auto jsonSkillLevel = params["skillLevel"];
        if ( jsonSkillLevel.isNull() )
        {
            pClient->ChannelInfo.eSkillLevel = SL_NOT_SET;
            pClient->SetRemoteInfo();
            return;
        }

        if ( !jsonSkillLevel.isString() )
        {
            response["error"] = CRpcServer::CreateJsonRpcError ( -32602, "Invalid params: skillLevel is not a string" );
            return;
        }

        auto strSkillLevel = jsonSkillLevel.toString();
        if ( strSkillLevel == "beginner" )
        {
            pClient->ChannelInfo.eSkillLevel = SL_BEGINNER;
        }
        else if ( strSkillLevel == "intermediate" )
        {
            pClient->ChannelInfo.eSkillLevel = SL_INTERMEDIATE;
        }
        else if ( strSkillLevel == "expert" )
        {
            pClient->ChannelInfo.eSkillLevel = SL_PROFESSIONAL;
        }
        else
        {
            response["error"] = CRpcServer::CreateJsonRpcError ( -32602, "Invalid params: skillLevel is not beginner, intermediate or expert" );
            return;
        }

        pClient->SetRemoteInfo();
        response["result"] = "ok";
    } );

    pRpcServer->HandleMethod ( "jamulusclient/sendChatText", [=] ( const QJsonObject& params, QJsonObject& response ) {
        auto jsonMessage = params["chatText"];
        if ( !jsonMessage.isString() )
        {
            response["error"] = CRpcServer::CreateJsonRpcError ( -32602, "Invalid params: chatText is not a string" );
            return;
        }

        pClient->CreateChatTextMes ( jsonMessage.toString() );

        response["result"] = "ok";
    } );
}

QJsonValue CClientRpc::SerializeSkillLevel ( ESkillLevel eSkillLevel )
{
    switch ( eSkillLevel )
    {
    case SL_BEGINNER:
        return QJsonValue ( "beginner" );

    case SL_INTERMEDIATE:
        return QJsonValue ( "intermediate" );

    case SL_PROFESSIONAL:
        return QJsonValue ( "expert" );

    case SL_NOT_SET:
        return QJsonValue ( QJsonValue::Null );
    }
}
