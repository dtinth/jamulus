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

#include "clientrpc.h"

static QJsonValue FormatSkillLevel ( ESkillLevel skillLevel );

CClientRpc::CClientRpc ( CClient* pClient, CRpcServer* pRpcServer )
{
    pRpcServer->HandleMethod ( "jamulusclient/getClientInfo", [=] ( const QJsonObject& params, QJsonObject& response ) {
        QJsonObject clientInfo;
        clientInfo["connected"] = pClient->IsConnected();
        response["result"]      = clientInfo;
    } );

    pRpcServer->HandleMethod ( "jamulusclient/getChannelInfo", [=] ( const QJsonObject& params, QJsonObject& response ) {
        QJsonObject channelInfo;
        channelInfo["name"]       = pClient->ChannelInfo.strName;
        channelInfo["skillLevel"] = FormatSkillLevel ( pClient->ChannelInfo.eSkillLevel );
        response["result"]        = channelInfo;
    } );

    pRpcServer->HandleMethod ( "jamulusclient/setName", [=] ( const QJsonObject& params, QJsonObject& response ) {
        auto jsonName = params["name"];
        if ( !jsonName.isString() )
        {
            response["error"] = CreateJsonRpcError ( -32602, "Invalid params" );
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
            response["error"] = CreateJsonRpcError ( -32602, "Invalid params" );
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
            response["error"] = CreateJsonRpcError ( -32602, "Invalid params" );
            return;
        }

        pClient->SetRemoteInfo();
        response["result"] = "ok";
    } );
}

CClientRpc::~CClientRpc() {}

static QJsonValue FormatSkillLevel ( ESkillLevel eSkillLevel )
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
