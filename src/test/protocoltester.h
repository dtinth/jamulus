/******************************************************************************\
 * Copyright (c) 2026
 *
 * Author(s):
 *  The Jamulus Development Team
 *
 * Licensed under AGPL 3.0 or any later version. See COPYING for details.
 *
\******************************************************************************/

#pragma once

#include <QtTest>
#include "protocol.h"

/* Protocol test helpers ******************************************************/

// Builds a raw protocol frame byte by byte, independently of the production
// code in GenMessageFrame, so that the tests pin the on-wire format:
// TAG (2), ID (2), cnt (1), length (2), data (n), CRC (2), values little endian
inline CVector<uint8_t> GenTestFrame ( const int iCnt, const int iID, const CVector<uint8_t>& vecbyBody )
{
    const int iBodyLen = vecbyBody.Size();

    CVector<uint8_t> vecbyFrame ( MESS_LEN_WITHOUT_DATA_BYTE + iBodyLen );

    vecbyFrame[0] = 0; // TAG (2 bytes, all zero)
    vecbyFrame[1] = 0;
    vecbyFrame[2] = static_cast<uint8_t> ( iID & 0xFF ); // ID (2 bytes)
    vecbyFrame[3] = static_cast<uint8_t> ( ( iID >> 8 ) & 0xFF );
    vecbyFrame[4] = static_cast<uint8_t> ( iCnt & 0xFF );     // cnt (1 byte)
    vecbyFrame[5] = static_cast<uint8_t> ( iBodyLen & 0xFF ); // length (2 bytes)
    vecbyFrame[6] = static_cast<uint8_t> ( ( iBodyLen >> 8 ) & 0xFF );

    for ( int i = 0; i < iBodyLen; i++ )
    {
        vecbyFrame[MESS_HEADER_LENGTH_BYTE + i] = vecbyBody[i];
    }

    // CRC (2 bytes) over header plus body
    CCRC CRCObj;

    for ( int i = 0; i < MESS_HEADER_LENGTH_BYTE + iBodyLen; i++ )
    {
        CRCObj.AddByte ( vecbyFrame[i] );
    }

    const uint32_t iCRC = CRCObj.GetCRC();

    vecbyFrame[MESS_HEADER_LENGTH_BYTE + iBodyLen]     = static_cast<uint8_t> ( iCRC & 0xFF );
    vecbyFrame[MESS_HEADER_LENGTH_BYTE + iBodyLen + 1] = static_cast<uint8_t> ( ( iCRC >> 8 ) & 0xFF );

    return vecbyFrame;
}

inline QByteArray ToByteArray ( const CVector<uint8_t>& vecbyData )
{
    return QByteArray ( reinterpret_cast<const char*> ( vecbyData.data() ), vecbyData.Size() );
}

inline CVector<uint8_t> FromByteArray ( const QByteArray& baData )
{
    const int iSize = static_cast<int> ( baData.size() );

    CVector<uint8_t> vecbyData ( iSize );

    for ( int i = 0; i < iSize; i++ )
    {
        vecbyData[i] = static_cast<uint8_t> ( baData[i] );
    }

    return vecbyData;
}

// note that CProtocol::ParseMessageFrame() returns true on error, this helper
// returns true on success to make the test code easier to read
inline bool ParseFrame ( const CVector<uint8_t>& vecbyFrame, CVector<uint8_t>& vecbyMesBodyData, int& iRecCounter, int& iRecID )
{
    return !CProtocol::ParseMessageFrame ( vecbyFrame, vecbyFrame.Size(), vecbyMesBodyData, iRecCounter, iRecID );
}

// Delivers every frame emitted by From to To through the public parsing
// interface, emulating what CChannel does with received network packets.
// Connecting both directions also routes the acknowledgements back to the
// sender so that its send message queue advances.
inline void ConnectProtocols ( CProtocol& From, CProtocol& To )
{
    QObject::connect ( &From, &CProtocol::MessReadyForSending, &To, [&To] ( CVector<uint8_t> vecMessage ) {
        CVector<uint8_t> vecbyMesBodyData;
        int              iRecCounter = 0;
        int              iRecID      = 0;

        QVERIFY ( ParseFrame ( vecMessage, vecbyMesBodyData, iRecCounter, iRecID ) );

        To.ParseMessageBody ( vecbyMesBodyData, iRecCounter, iRecID );
    } );
}
