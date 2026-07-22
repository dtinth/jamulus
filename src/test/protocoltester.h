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

// Renders a QVariantList the way lastError() below wants to show them, e.g.
// QVariantList() << 2 << 0.75f  ->  "(2, 0.75)"
inline QString DescribeArgs ( const QVariantList& Args )
{
    QStringList strParts;

    for ( const QVariant& Arg : Args )
    {
        strParts << Arg.toString();
    }

    return QStringLiteral ( "(" ) + strParts.join ( QStringLiteral ( ", " ) ) + QStringLiteral ( ")" );
}

/* Fluent test driver *********************************************************/

// CProtocolTester wraps a connected pair of CProtocol instances (round trip
// family) and a mutable raw frame (frame contract family) behind a small
// fluent API, e.g.
//
//   QVERIFY2 ( tester.chatText ( "hello" ).roundTrips(), qPrintable ( tester.lastError() ) );
//   QVERIFY2 ( tester.chanGain ( 2, 0.75f ).roundTripsWithin ( 1.0f / 32768 ), qPrintable ( tester.lastError() ) );
//   QVERIFY2 ( tester.validFrame().truncatedBy ( 3 ).isRejected(), qPrintable ( tester.lastError() ) );
//
// Design note on failure locations and messages: QtTest's QVERIFY/QCOMPARE
// macros report whatever __FILE__/__LINE__ they expand at, so if the
// terminal methods below (roundTrips(), roundTripsWithin(), isRejected(),
// isAccepted()) asserted internally, every failure would point at this
// header instead of the offending test line. None of them call QVERIFY/
// QCOMPARE themselves -- they only return bool, so callers wrap the whole
// chain in QVERIFY2() at the call site: QtTest's source location then names
// the real test line, and the *message* comes from lastError(), which every
// terminal method fills in with what was sent, what was actually received
// (values and, for frames, a space separated hex dump), and the tolerance if
// one applied -- e.g.
//
//   FAIL!  : CTestProtocol::RoundTripChanGain(arbitrary gain) 'tester.chanGain ( iChanID, fGain ).roundTripsWithin ( ... )' returned FALSE.
//   (chanGain ( 5, 0.333 ): signal args (5, 0.332977294921875) did not match expected (5, 0.3330000042915344) (tolerance 1e-07 on arg 1))
//      Loc: [tst_protocol.cpp(303)]
//
// so a failure is diagnosable from the log alone. Every arrange method
// (chatText(), validFrame(), ...) clears lastError() as it starts a fresh
// chain, and every terminal method unconditionally overwrites it (to empty
// on success, to a description on failure) -- so a stale message from an
// earlier chain on the same CProtocolTester instance can never be mistaken
// for the current one.
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
        return record ( QStringLiteral ( "jitBufSize ( %1 )" ).arg ( iJitBufSize ), Spy, QVariantList() << iJitBufSize );
    }

    CProtocolTester& clientID ( const int iChanID )
    {
        QSignalSpy Spy ( &m_Receiver, SIGNAL ( ClientIDReceived ( int ) ) );
        m_Sender.CreateClientIDMes ( iChanID );
        return record ( QStringLiteral ( "clientID ( %1 )" ).arg ( iChanID ), Spy, QVariantList() << iChanID );
    }

    CProtocolTester& chanGain ( const int iChanID, const float fGain )
    {
        QSignalSpy Spy ( &m_Receiver, SIGNAL ( ChangeChanGain ( int, float ) ) );
        m_Sender.CreateChanGainMes ( iChanID, fGain );
        return record ( QStringLiteral ( "chanGain ( %1, %2 )" ).arg ( iChanID ).arg ( fGain ),
                        Spy,
                        QVariantList() << iChanID << fGain,
                        /* iToleranceArgIdx = */ 1 );
    }

    CProtocolTester& chanPan ( const int iChanID, const float fPan )
    {
        QSignalSpy Spy ( &m_Receiver, SIGNAL ( ChangeChanPan ( int, float ) ) );
        m_Sender.CreateChanPanMes ( iChanID, fPan );
        return record ( QStringLiteral ( "chanPan ( %1, %2 )" ).arg ( iChanID ).arg ( fPan ),
                        Spy,
                        QVariantList() << iChanID << fPan,
                        /* iToleranceArgIdx = */ 1 );
    }

    CProtocolTester& muteState ( const int iChanID, const bool bIsMuted )
    {
        QSignalSpy Spy ( &m_Receiver, SIGNAL ( MuteStateHasChangedReceived ( int, bool ) ) );
        m_Sender.CreateMuteStateHasChangedMes ( iChanID, bIsMuted );
        return record ( QStringLiteral ( "muteState ( %1, %2 )" ).arg ( iChanID ).arg ( bIsMuted ), Spy, QVariantList() << iChanID << bIsMuted );
    }

    CProtocolTester& chatText ( const QString& strChatText )
    {
        QSignalSpy Spy ( &m_Receiver, SIGNAL ( ChatTextReceived ( QString ) ) );
        m_Sender.CreateChatTextMes ( strChatText );
        return record ( QStringLiteral ( "chatText ( \"%1\" )" ).arg ( strChatText ), Spy, QVariantList() << strChatText );
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

        return record ( QStringLiteral ( "licenceRequired ( %1 )" ).arg ( static_cast<int> ( eLicenceType ) ),
                        iNumReceived,
                        QVariantList() << iReceivedType,
                        QVariantList() << static_cast<int> ( eLicenceType ) );
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

        return record ( QStringLiteral ( "recorderState ( %1 )" ).arg ( static_cast<int> ( eRecorderState ) ),
                        iNumReceived,
                        QVariantList() << iReceivedState,
                        QVariantList() << static_cast<int> ( eRecorderState ) );
    }

    // asserts the receiver got the message exactly once, with every argument
    // matching the value that was sent
    bool roundTrips() const { return verifyRoundTrip ( -1.0f ); }

    // like roundTrips(), but the argument flagged by the arrange method above
    // (the gain/pan float) only has to match within fTolerance -- the value is
    // quantized to 1 / 2^15 steps on the wire
    bool roundTripsWithin ( const float fTolerance ) const { return verifyRoundTrip ( fTolerance ); }

    /* frame contract family ------------------------------------------------ */

    // a real, production generated well formed frame -- see
    // SendAndCaptureFrame() above; which message it is doesn't matter here,
    // only that it is a genuine, valid frame to mutate below
    CProtocolTester& validFrame()
    {
        m_vecbyFrame     = SendAndCaptureFrame ( [] ( CProtocol& p ) { p.CreateChatTextMes ( QStringLiteral ( "frame contract test" ) ); } );
        m_strDescription = QStringLiteral ( "validFrame()" );
        m_strLastError.clear();
        return *this;
    }

    // chops iBytes off the end of the frame built by validFrame(), e.g. to cut
    // into the CRC or the body
    CProtocolTester& truncatedBy ( const int iBytes )
    {
        m_vecbyFrame.resize ( m_vecbyFrame.Size() - iBytes );
        m_strDescription += QStringLiteral ( ".truncatedBy ( %1 )" ).arg ( iBytes );
        return *this;
    }

    // flips every bit of the trailing CRC byte so the checksum no longer
    // matches the header plus body
    CProtocolTester& withBadCRC()
    {
        m_vecbyFrame[m_vecbyFrame.Size() - 1] = static_cast<uint8_t> ( m_vecbyFrame[m_vecbyFrame.Size() - 1] ^ 0xFF );
        m_strDescription += QStringLiteral ( ".withBadCRC()" );
        return *this;
    }

    // overwrites the frame's declared body length field, independently of the
    // body bytes actually present
    CProtocolTester& withDeclaredLength ( const int iLen )
    {
        m_vecbyFrame[5] = static_cast<uint8_t> ( iLen & 0xFF );
        m_vecbyFrame[6] = static_cast<uint8_t> ( ( iLen >> 8 ) & 0xFF );
        m_strDescription += QStringLiteral ( ".withDeclaredLength ( %1 )" ).arg ( iLen );
        return *this;
    }

    // ParseMessageFrame() returns true on error; isRejected()/isAccepted() are
    // named for what the test is asserting, not the raw return value
    bool isRejected() const
    {
        const bool bWasRejected = parseCurrentFrame();

        m_strLastError =
            bWasRejected ? QString() : QStringLiteral ( "%1.isRejected(): frame was accepted -- frame: %2" ).arg ( m_strDescription, frameHex() );

        return bWasRejected;
    }

    bool isAccepted() const
    {
        const bool bWasRejected = parseCurrentFrame();

        m_strLastError =
            bWasRejected ? QStringLiteral ( "%1.isAccepted(): frame was rejected -- frame: %2" ).arg ( m_strDescription, frameHex() ) : QString();

        return !bWasRejected;
    }

    // the raw bytes built up by validFrame() and its mutators, for tests that
    // need the bytes themselves (e.g. as a QTest data row) rather than a
    // pass/fail verdict
    const CVector<uint8_t>& frame() const { return m_vecbyFrame; }

    // a human readable description of what went wrong in the last terminal
    // call (roundTrips()/roundTripsWithin()/isRejected()/isAccepted()) on
    // this instance, empty if that call passed -- pass to QVERIFY2()
    QString lastError() const { return m_strLastError; }

private:
    CProtocolTester& record ( const QString&      strDescription,
                              const QSignalSpy&   Spy,
                              const QVariantList& ExpectedArgs,
                              const int           iToleranceArgIdx = -1 )
    {
        return record ( strDescription, Spy.count(), Spy.isEmpty() ? QVariantList() : Spy.at ( 0 ), ExpectedArgs, iToleranceArgIdx );
    }

    CProtocolTester& record ( const QString&      strDescription,
                              const int           iReceivedCount,
                              const QVariantList& ReceivedArgs,
                              const QVariantList& ExpectedArgs,
                              const int           iToleranceArgIdx = -1 )
    {
        m_strDescription   = strDescription;
        m_iReceivedCount   = iReceivedCount;
        m_ReceivedArgs     = ReceivedArgs;
        m_ExpectedArgs     = ExpectedArgs;
        m_iToleranceArgIdx = iToleranceArgIdx;
        m_strLastError.clear(); // fresh chain, no verdict yet -- set by the terminal method
        return *this;
    }

    bool verifyRoundTrip ( const float fTolerance ) const
    {
        if ( m_iReceivedCount != 1 )
        {
            m_strLastError =
                QStringLiteral ( "%1: expected exactly 1 signal emission, receiver got %2" ).arg ( m_strDescription ).arg ( m_iReceivedCount );
            return false;
        }

        if ( !argsMatch ( fTolerance ) )
        {
            m_strLastError =
                QStringLiteral ( "%1: signal args %2 did not match expected %3%4" )
                    .arg ( m_strDescription,
                           DescribeArgs ( m_ReceivedArgs ),
                           DescribeArgs ( m_ExpectedArgs ),
                           fTolerance >= 0.0f ? QStringLiteral ( " (tolerance %1 on arg %2)" ).arg ( fTolerance ).arg ( m_iToleranceArgIdx )
                                              : QString() );
            return false;
        }

        m_strLastError.clear();
        return true;
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

    bool parseCurrentFrame() const
    {
        CVector<uint8_t> vecbyMesBodyData;
        int              iRecCounter = 0;
        int              iRecID      = 0;

        // ParseMessageFrame() returns true on error, i.e. true means rejected
        return CProtocol::ParseMessageFrame ( m_vecbyFrame, m_vecbyFrame.Size(), vecbyMesBodyData, iRecCounter, iRecID );
    }

    QString frameHex() const { return QString::fromLatin1 ( ToByteArray ( m_vecbyFrame ).toHex ( ' ' ) ); }

    CProtocol m_Sender;
    CProtocol m_Receiver;

    QString      m_strDescription;
    int          m_iReceivedCount = 0;
    QVariantList m_ReceivedArgs;
    QVariantList m_ExpectedArgs;
    int          m_iToleranceArgIdx = -1;

    CVector<uint8_t> m_vecbyFrame;

    mutable QString m_strLastError;
};
