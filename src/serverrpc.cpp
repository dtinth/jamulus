/******************************************************************************\
 * Copyright (c) 2021-2022
 *
 * Author(s):
 *  dtinth
 *  Christian Hoffmann
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

CServerRpc::CServerRpc ( CServer* pServer, CRpcServer* pRpcServer, QObject* parent ) : QObject ( parent )
{
    // API doc already part of CClientRpc
    pRpcServer->HandleMethod ( "jamulus/getMode", [=] ( const QJsonObject& params, QJsonObject& response ) {
        QJsonObject result{ { "mode", "server" } };
        response["result"] = result;
        Q_UNUSED ( params );
    } );

    /// @rpc_method jamulusserver/getRecorderStatus
    /// @brief Returns the recorder state.
    /// @param {object} params - No parameters (empty object).
    /// @result {boolean} result.initialised - True if the recorder is initialised.
    /// @result {string} result.errorMessage - The recorder error message, if any.
    /// @result {boolean} result.enabled - True if the recorder is enabled.
    /// @result {string} result.recordingDirectory - The recorder recording directory.
    pRpcServer->HandleMethod ( "jamulusserver/getRecorderStatus", [=] ( const QJsonObject& params, QJsonObject& response ) {
        QJsonObject result{
            { "initialised", pServer->GetRecorderInitialised() },
            { "errorMessage", pServer->GetRecorderErrMsg() },
            { "enabled", pServer->GetRecordingEnabled() },
            { "recordingDirectory", pServer->GetRecordingDir() },
        };

        response["result"] = result;
        Q_UNUSED ( params );
    } );

    /// @rpc_method jamulusserver/getClients
    /// @brief Returns the list of connected clients along with details about them.
    /// @param {object} params - No parameters (empty object).
    /// @result {array} result.clients - The list of connected clients.
    /// @result {number} result.clients[*].id - The client’s channel id.
    /// @result {string} result.clients[*].address - The client’s address (ip:port).
    /// @result {string} result.clients[*].name - The client’s name.
    /// @result {number} result.clients[*].jitterBufferSize - The client’s jitter buffer size.
    /// @result {number} result.clients[*].channels - The number of audio channels of the client.
    pRpcServer->HandleMethod ( "jamulusserver/getClients", [=] ( const QJsonObject& params, QJsonObject& response ) {
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
            if ( vecHostAddresses[i].InetAddr == QHostAddress ( static_cast<quint32> ( 0 ) ) )
            {
                continue;
            }
            QJsonObject client{
                { "id", i },
                { "address", vecHostAddresses[i].toString ( CHostAddress::SM_IP_PORT ) },
                { "name", vecsName[i] },
                { "jitterBufferSize", veciJitBufNumFrames[i] },
                { "channels", pServer->GetClientNumAudioChannels ( i ) },
            };
            clients.append ( client );
        }

        // create result object
        QJsonObject result{
            { "clients", clients },
        };
        response["result"] = result;
        Q_UNUSED ( params );
    } );

    /// @rpc_method jamulusserver/getServerProfile
    /// @brief Returns the server registration profile and status.
    /// @param {object} params - No parameters (empty object).
    /// @result {string} result.name - The server name.
    /// @result {string} result.city - The server city.
    /// @result {number} result.countryId - The server country ID (see QLocale::Country).
    /// @result {string} result.welcomeMessage - The server welcome message.
    /// @result {string} result.directoryServer - The directory server to which this server requested registration, or blank if none.
    /// @result {string} result.registrationStatus - The server registration status as string (see ESvrRegStatus and SerializeRegistrationStatus).
    pRpcServer->HandleMethod ( "jamulusserver/getServerProfile", [=] ( const QJsonObject& params, QJsonObject& response ) {
        QString dsName = "";

        if ( AT_NONE != pServer->GetDirectoryType() )
            dsName = NetworkUtil::GetDirectoryAddress ( pServer->GetDirectoryType(), pServer->GetDirectoryAddress() );

        QJsonObject result{
            { "name", pServer->GetServerName() },
            { "city", pServer->GetServerCity() },
            { "countryId", pServer->GetServerCountry() },
            { "welcomeMessage", pServer->GetWelcomeMessage() },
            { "directoryServer", dsName },
            { "registrationStatus", SerializeRegistrationStatus ( pServer->GetSvrRegStatus() ) },
        };
        response["result"] = result;
        Q_UNUSED ( params );
    } );

    /// @rpc_method jamulusserver/setServerName
    /// @brief Sets the server name.
    /// @param {string} params.serverName - The new server name.
    /// @result {string} result - Always "ok".
    pRpcServer->HandleMethod ( "jamulusserver/setServerName", [=] ( const QJsonObject& params, QJsonObject& response ) {
        auto jsonServerName = params["serverName"];
        if ( !jsonServerName.isString() )
        {
            response["error"] = CRpcServer::CreateJsonRpcError ( CRpcServer::iErrInvalidParams, "Invalid params: serverName is not a string" );
            return;
        }

        pServer->SetServerName ( jsonServerName.toString() );
        response["result"] = "ok";
    } );

    /// @rpc_method jamulusserver/setWelcomeMessage
    /// @brief Sets the server welcome message.
    /// @param {string} params.welcomeMessage - The new welcome message.
    /// @result {string} result - Always "ok".
    pRpcServer->HandleMethod ( "jamulusserver/setWelcomeMessage", [=] ( const QJsonObject& params, QJsonObject& response ) {
        auto jsonWelcomeMessage = params["welcomeMessage"];
        if ( !jsonWelcomeMessage.isString() )
        {
            response["error"] = CRpcServer::CreateJsonRpcError ( CRpcServer::iErrInvalidParams, "Invalid params: welcomeMessage is not a string" );
            return;
        }

        pServer->SetWelcomeMessage ( jsonWelcomeMessage.toString() );
        response["result"] = "ok";
    } );

    /// @rpc_method jamulusserver/setRecordingDirectory
    /// @brief Sets the server recording directory.
    /// @param {string} params.recordingDirectory - The new recording directory.
    /// @result {string} result - Always "acknowledged".
    ///  To check if the directory was changed, call `jamulusserver/getRecorderStatus` again.
    pRpcServer->HandleMethod ( "jamulusserver/setRecordingDirectory", [=] ( const QJsonObject& params, QJsonObject& response ) {
        auto jsonRecordingDirectory = params["recordingDirectory"];
        if ( !jsonRecordingDirectory.isString() )
        {
            response["error"] =
                CRpcServer::CreateJsonRpcError ( CRpcServer::iErrInvalidParams, "Invalid params: recordingDirectory is not a string" );
            return;
        }

        pServer->SetRecordingDir ( jsonRecordingDirectory.toString() );
        response["result"] = "acknowledged";
    } );

    /// @rpc_method jamulusserver/startRecording
    /// @brief Starts the server recording.
    /// @param {object} params - No parameters (empty object).
    /// @result {string} result - Always "acknowledged".
    ///  To check if the recording was enabled, call `jamulusserver/getRecorderStatus` again.
    pRpcServer->HandleMethod ( "jamulusserver/startRecording", [=] ( const QJsonObject& params, QJsonObject& response ) {
        pServer->SetEnableRecording ( true );
        response["result"] = "acknowledged";
        Q_UNUSED ( params );
    } );

    /// @rpc_method jamulusserver/stopRecording
    /// @brief Stops the server recording.
    /// @param {object} params - No parameters (empty object).
    /// @result {string} result - Always "acknowledged".
    ///  To check if the recording was disabled, call `jamulusserver/getRecorderStatus` again.
    pRpcServer->HandleMethod ( "jamulusserver/stopRecording", [=] ( const QJsonObject& params, QJsonObject& response ) {
        pServer->SetEnableRecording ( false );
        response["result"] = "acknowledged";
        Q_UNUSED ( params );
    } );

    /// @rpc_method jamulusserver/restartRecording
    /// @brief Restarts the recording into a new directory.
    /// @param {object} params - No parameters (empty object).
    /// @result {string} result - Always "acknowledged".
    ///  To check if the recording was restarted or if there is any error, call `jamulusserver/getRecorderStatus` again.
    pRpcServer->HandleMethod ( "jamulusserver/restartRecording", [=] ( const QJsonObject& params, QJsonObject& response ) {
        pServer->RequestNewRecording();
        response["result"] = "acknowledged";
        Q_UNUSED ( params );
    } );

    /// @rpc_notification jamulusserver/chatTextReceived
    /// @brief Emitted when a client sends chat text to the server.
    /// @param {number} params.channel.id - The channel ID.
    /// @param {string} params.channel.name - The channel name.
    /// @param {string} params.chatText - The chat text (not HTML-escaped).
    connect ( pServer, &CServer::ChatTextReceived, [=] ( const int iChanNum, const QString& strName, const QString& strChatText ) {
        QJsonObject channel{
            { "id", iChanNum },
            { "name", strName },
        };
        pRpcServer->BroadcastNotification ( "jamulusserver/chatTextReceived",
                                            QJsonObject{
                                                { "channel", channel },
                                                { "chatText", strChatText },
                                            } );
    } );

    /// @rpc_method jamulusserver/broadcastChatText
    /// @brief Broadcasts a chat text to all clients.
    /// @param {string} params.chatTextHtml - The chat text to send (HTML is allowed).
    /// @result {string} result - Always "ok".
    pRpcServer->HandleMethod ( "jamulusserver/broadcastChatText", [=] ( const QJsonObject& params, QJsonObject& response ) {
        auto jsonChatTextHtml = params["chatTextHtml"];
        if ( !jsonChatTextHtml.isString() )
        {
            response["error"] = CRpcServer::CreateJsonRpcError ( -32602, "Invalid params: chatTextHtml is not a string" );
            return;
        }

        pServer->BroadcastChatText ( jsonChatTextHtml.toString() );
        response["result"] = "ok";
    } );

    /// @rpc_method jamulusserver/sendChatText
    /// @brief Sends a chat text to a specific client.
    /// @param {number} params.channelId - The channel ID.
    /// @param {string} params.chatTextHtml - The chat text to send (HTML is allowed).
    /// @result {string} result - Always "ok".
    pRpcServer->HandleMethod ( "jamulusserver/sendChatText", [=] ( const QJsonObject& params, QJsonObject& response ) {
        auto jsonChatTextHtml = params["chatTextHtml"];
        if ( !jsonChatTextHtml.isString() )
        {
            response["error"] = CRpcServer::CreateJsonRpcError ( -32602, "Invalid params: chatTextHtml is not a string" );
            return;
        }

        auto jsonChannelId = params["channelId"];
        if ( !jsonChannelId.isDouble() )
        {
            response["error"] = CRpcServer::CreateJsonRpcError ( -32602, "Invalid params: channelId is not a number" );
            return;
        }

        auto iChannelId = jsonChannelId.toInt();
        pServer->SendChatText ( iChannelId, jsonChatTextHtml.toString() );
        response["result"] = "ok";
    } );
}

QJsonValue CServerRpc::SerializeRegistrationStatus ( ESvrRegStatus eSvrRegStatus )
{
    switch ( eSvrRegStatus )
    {
    case SRS_NOT_REGISTERED:
        return "not_registered";

    case SRS_BAD_ADDRESS:
        return "bad_address";

    case SRS_REQUESTED:
        return "requested";

    case SRS_TIME_OUT:
        return "time_out";

    case SRS_UNKNOWN_RESP:
        return "unknown_resp";

    case SRS_REGISTERED:
        return "registered";

    case SRS_SERVER_LIST_FULL:
        return "directory_server_full";

    case SRS_VERSION_TOO_OLD:
        return "server_version_too_old";

    case SRS_NOT_FULFILL_REQUIREMENTS:
        return "requirements_not_fulfilled";
    }

    return QString ( "unknown(%1)" ).arg ( eSvrRegStatus );
}
