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

// Runs Action on a fresh CProtocol instance and returns the single raw frame
// it hands to MessReadyForSending. "Fresh" matters: the frame counter (cnt)
// starts at 0 and increments per message sent on an instance, so this is what
// makes the result byte-for-byte deterministic (relied on by the golden frame
// tests in tst_protocol.cpp).
template<typename ActionT>
inline CVector<uint8_t> SendAndCaptureFrame ( ActionT Action )
{
    CProtocol        Sender;
    CVector<uint8_t> vecbyFrame;

    QObject::connect ( &Sender, &CProtocol::MessReadyForSending, [&vecbyFrame] ( CVector<uint8_t> vecMessage ) { vecbyFrame = vecMessage; } );

    Action ( Sender );

    return vecbyFrame;
}

// keeps vecbyFrame's TAG/CNT bytes but overwrites its ID and body (rewriting
// the length field and recomputing the CRC so the result is still a well
// formed *frame* even though the *body* deliberately might not be) -- for
// testing how CProtocol::ParseMessageBody() reacts to malformed bodies, as
// opposed to malformed frames (see RejectInvalidMessageBody()/
// IgnoreAcknWithEmptyBody() in tst_protocol.cpp)
inline void ReplaceIdAndBody ( CVector<uint8_t>& vecbyFrame, const int iID, const CVector<uint8_t>& vecbyNewBody )
{
    const int        iBodyLen = vecbyNewBody.Size();
    CVector<uint8_t> vecbyNewFrame ( MESS_LEN_WITHOUT_DATA_BYTE + iBodyLen );

    vecbyNewFrame[0] = vecbyFrame[0]; // TAG, unchanged
    vecbyNewFrame[1] = vecbyFrame[1];
    vecbyNewFrame[2] = static_cast<uint8_t> ( iID & 0xFF ); // ID, overwritten
    vecbyNewFrame[3] = static_cast<uint8_t> ( ( iID >> 8 ) & 0xFF );
    vecbyNewFrame[4] = vecbyFrame[4]; // CNT, unchanged
    vecbyNewFrame[5] = static_cast<uint8_t> ( iBodyLen & 0xFF );
    vecbyNewFrame[6] = static_cast<uint8_t> ( ( iBodyLen >> 8 ) & 0xFF );

    for ( int i = 0; i < iBodyLen; i++ )
    {
        vecbyNewFrame[MESS_HEADER_LENGTH_BYTE + i] = vecbyNewBody[i];
    }

    // recompute the CRC over the new header+body content
    CCRC CRCObj;

    for ( int i = 0; i < MESS_HEADER_LENGTH_BYTE + iBodyLen; i++ )
    {
        CRCObj.AddByte ( vecbyNewFrame[i] );
    }

    const uint32_t iCRC                                   = CRCObj.GetCRC();
    vecbyNewFrame[MESS_HEADER_LENGTH_BYTE + iBodyLen]     = static_cast<uint8_t> ( iCRC & 0xFF );
    vecbyNewFrame[MESS_HEADER_LENGTH_BYTE + iBodyLen + 1] = static_cast<uint8_t> ( ( iCRC >> 8 ) & 0xFF );

    vecbyFrame = vecbyNewFrame;
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

/* Fluent test driver *********************************************************/

// CProtocolTester wraps a connected pair of CProtocol instances (round trip
// family) and a mutable raw frame (frame contract family) behind a small
// fluent API, e.g.
//
//   QVERIFY ( tester.chatText ( "hello" ).roundTrips() );
//   QVERIFY ( tester.chanGain ( 2, 0.75f ).roundTripsWithin ( 1.0f / 32768 ) );
//   QVERIFY ( tester.validFrame().truncatedBy ( 3 ).isRejected() );
//
// Design note on failure locations: QtTest's QVERIFY/QCOMPARE macros report
// whatever __FILE__/__LINE__ they expand at, so if the terminal methods below
// (roundTrips(), roundTripsWithin(), isRejected(), isAccepted()) asserted
// internally, every failure would point at this header instead of the
// offending test line. To avoid that, none of the methods below call
// QVERIFY/QCOMPARE themselves -- the "arrange" methods (chatText(), chanGain()
// etc.) only record what happened, and the terminal methods only return bool.
// Callers wrap the whole chain in QVERIFY() at the call site, so QtTest's
// failure output (both the source location and the "expression that returned
// FALSE" text) names the real test line, e.g.
//
//   FAIL!  : CTestProtocol::RoundTripChanGain(arbitrary gain) 'tester.chanGain ( iChanID, fGain ).roundTripsWithin ( 1.0f / 32768 )' returned FALSE.
//   Loc: [tst_protocol.cpp(42)]
//
// This costs a QVERIFY ( ... ) wrapper around every fluent expression (the
// ticket's own examples omit it), which was judged a fair trade for keeping
// the failure location honest without resorting to macro tricks (e.g.
// #define-ing roundTrips() to smuggle in __FILE__/__LINE__ at the call site)
// that would otherwise be needed to make a bare `tester.foo().roundTrips();`
// statement self-assert correctly.
class CProtocolTester
{
public:
    CProtocolTester()
    {
        ConnectProtocols ( m_Sender, m_Receiver );
        ConnectProtocols ( m_Receiver, m_Sender );
    }

    /* round trip family -------------------------------------------------- */

    CProtocolTester& jitBufSize ( const int iJitBufSize )
    {
        QSignalSpy Spy ( &m_Receiver, SIGNAL ( ChangeJittBufSize ( int ) ) );
        m_Sender.CreateJitBufMes ( iJitBufSize );
        return record ( Spy, QVariantList() << iJitBufSize );
    }

    CProtocolTester& clientID ( const int iChanID )
    {
        QSignalSpy Spy ( &m_Receiver, SIGNAL ( ClientIDReceived ( int ) ) );
        m_Sender.CreateClientIDMes ( iChanID );
        return record ( Spy, QVariantList() << iChanID );
    }

    CProtocolTester& chanGain ( const int iChanID, const float fGain )
    {
        QSignalSpy Spy ( &m_Receiver, SIGNAL ( ChangeChanGain ( int, float ) ) );
        m_Sender.CreateChanGainMes ( iChanID, fGain );
        return record ( Spy, QVariantList() << iChanID << fGain, /* iToleranceArgIdx = */ 1 );
    }

    CProtocolTester& chanPan ( const int iChanID, const float fPan )
    {
        QSignalSpy Spy ( &m_Receiver, SIGNAL ( ChangeChanPan ( int, float ) ) );
        m_Sender.CreateChanPanMes ( iChanID, fPan );
        return record ( Spy, QVariantList() << iChanID << fPan, /* iToleranceArgIdx = */ 1 );
    }

    CProtocolTester& muteState ( const int iChanID, const bool bIsMuted )
    {
        QSignalSpy Spy ( &m_Receiver, SIGNAL ( MuteStateHasChangedReceived ( int, bool ) ) );
        m_Sender.CreateMuteStateHasChangedMes ( iChanID, bIsMuted );
        return record ( Spy, QVariantList() << iChanID << bIsMuted );
    }

    CProtocolTester& chatText ( const QString& strChatText )
    {
        QSignalSpy Spy ( &m_Receiver, SIGNAL ( ChatTextReceived ( QString ) ) );
        m_Sender.CreateChatTextMes ( strChatText );
        return record ( Spy, QVariantList() << strChatText );
    }

    // ELicenceType/ERecorderState are plain enums (not Q_ENUM/Q_DECLARE_METATYPE),
    // so QSignalSpy cannot snapshot them into a QVariant the way it can for
    // int/float/bool/QString above; connect a plain lambda instead, same as the
    // original hand written test did.
    CProtocolTester& licenceRequired ( const ELicenceType eLicenceType )
    {
        int iReceivedType = -1;
        int iNumReceived  = 0;

        QObject::connect ( &m_Receiver, &CProtocol::LicenceRequired, [&] ( ELicenceType eReceived ) {
            iReceivedType = static_cast<int> ( eReceived );
            iNumReceived++;
        } );

        m_Sender.CreateLicenceRequiredMes ( eLicenceType );

        return record ( iNumReceived, QVariantList() << iReceivedType, QVariantList() << static_cast<int> ( eLicenceType ) );
    }

    CProtocolTester& recorderState ( const ERecorderState eRecorderState )
    {
        int iReceivedState = -1;
        int iNumReceived   = 0;

        QObject::connect ( &m_Receiver, &CProtocol::RecorderStateReceived, [&] ( ERecorderState eReceived ) {
            iReceivedState = static_cast<int> ( eReceived );
            iNumReceived++;
        } );

        m_Sender.CreateRecorderStateMes ( eRecorderState );

        return record ( iNumReceived, QVariantList() << iReceivedState, QVariantList() << static_cast<int> ( eRecorderState ) );
    }

    // asserts the receiver got the message exactly once, with every argument
    // matching the value that was sent
    bool roundTrips() const { return m_iReceivedCount == 1 && argsMatch ( -1.0f ); }

    // like roundTrips(), but the argument flagged by the arrange method above
    // (the gain/pan float) only has to match within fTolerance -- the value is
    // quantized to 1 / 2^15 steps on the wire
    bool roundTripsWithin ( const float fTolerance ) const { return m_iReceivedCount == 1 && argsMatch ( fTolerance ); }

    /* frame contract family ------------------------------------------------ */

    // a real, production generated well formed frame -- see
    // SendAndCaptureFrame() above; which message it is doesn't matter here,
    // only that it is a genuine, valid frame to mutate below
    CProtocolTester& validFrame()
    {
        m_vecbyFrame = SendAndCaptureFrame ( [] ( CProtocol& p ) { p.CreateChatTextMes ( QStringLiteral ( "frame contract test" ) ); } );
        return *this;
    }

    // chops iBytes off the end of the frame built by validFrame(), e.g. to cut
    // into the CRC or the body
    CProtocolTester& truncatedBy ( const int iBytes )
    {
        m_vecbyFrame.resize ( m_vecbyFrame.Size() - iBytes );
        return *this;
    }

    // flips every bit of the trailing CRC byte so the checksum no longer
    // matches the header plus body
    CProtocolTester& withBadCRC()
    {
        m_vecbyFrame[m_vecbyFrame.Size() - 1] = static_cast<uint8_t> ( m_vecbyFrame[m_vecbyFrame.Size() - 1] ^ 0xFF );
        return *this;
    }

    // overwrites the frame's declared body length field, independently of the
    // body bytes actually present
    CProtocolTester& withDeclaredLength ( const int iLen )
    {
        m_vecbyFrame[5] = static_cast<uint8_t> ( iLen & 0xFF );
        m_vecbyFrame[6] = static_cast<uint8_t> ( ( iLen >> 8 ) & 0xFF );
        return *this;
    }

    // ParseMessageFrame() returns true on error; isRejected()/isAccepted() are
    // named for what the test is asserting, not the raw return value
    bool isRejected() const
    {
        CVector<uint8_t> vecbyMesBodyData;
        int              iRecCounter = 0;
        int              iRecID      = 0;

        return CProtocol::ParseMessageFrame ( m_vecbyFrame, m_vecbyFrame.Size(), vecbyMesBodyData, iRecCounter, iRecID );
    }

    bool isAccepted() const { return !isRejected(); }

    // the raw bytes built up by validFrame() and its mutators, for tests that
    // need the bytes themselves (e.g. as a QTest data row) rather than a
    // pass/fail verdict
    const CVector<uint8_t>& frame() const { return m_vecbyFrame; }

private:
    CProtocolTester& record ( const QSignalSpy& Spy, const QVariantList& ExpectedArgs, const int iToleranceArgIdx = -1 )
    {
        return record ( Spy.count(), Spy.isEmpty() ? QVariantList() : Spy.at ( 0 ), ExpectedArgs, iToleranceArgIdx );
    }

    CProtocolTester& record ( const int           iReceivedCount,
                              const QVariantList& ReceivedArgs,
                              const QVariantList& ExpectedArgs,
                              const int           iToleranceArgIdx = -1 )
    {
        m_iReceivedCount   = iReceivedCount;
        m_ReceivedArgs     = ReceivedArgs;
        m_ExpectedArgs     = ExpectedArgs;
        m_iToleranceArgIdx = iToleranceArgIdx;
        return *this;
    }

    bool argsMatch ( const float fTolerance ) const
    {
        if ( m_ReceivedArgs.size() != m_ExpectedArgs.size() )
        {
            return false;
        }

        for ( int i = 0; i < m_ExpectedArgs.size(); i++ )
        {
            if ( fTolerance >= 0.0f && i == m_iToleranceArgIdx )
            {
                if ( qAbs ( m_ReceivedArgs.at ( i ).toFloat() - m_ExpectedArgs.at ( i ).toFloat() ) > fTolerance )
                {
                    return false;
                }
            }
            else if ( m_ReceivedArgs.at ( i ) != m_ExpectedArgs.at ( i ) )
            {
                return false;
            }
        }

        return true;
    }

    CProtocol m_Sender;
    CProtocol m_Receiver;

    int          m_iReceivedCount = 0;
    QVariantList m_ReceivedArgs;
    QVariantList m_ExpectedArgs;
    int          m_iToleranceArgIdx = -1;

    CVector<uint8_t> m_vecbyFrame;
};
