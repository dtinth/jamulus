/******************************************************************************\
 * Copyright (c) 2026
 *
 * Author(s):
 *  The Jamulus Development Team
 *
 * Licensed under AGPL 3.0 or any later version. See COPYING for details.
 *
\******************************************************************************/

#include <QtTest>
#include "protocol.h"
#include "protocoltester.h"

/* Test cases *****************************************************************/
class CTestProtocol : public QObject
{
    Q_OBJECT

private slots:
    // On-wire compatibility contract: fixed hex literals of what production
    // emits TODAY for a representative message of each shape (a plain
    // message, a connection-based one with a short body, one with a
    // variable-length body, and a connection-less one). If any of these
    // ever fail, the wire format changed -- that must never happen silently,
    // so the literal only gets updated as a deliberate, reviewed decision.
    void GoldenFrameJitBufSize();
    void GoldenFrameClientID();
    void GoldenFrameChatText();
    void GoldenFrameCLPing();

    // ParseMessageFrame contract
    void ParseValidFrame();
    void RejectInvalidFrame_data();
    void RejectInvalidFrame();
    void RejectTruncatedFrame();

    // regression test: a well formed ACKN frame with an empty body must be
    // ignored and must not crash (fixed in commit 024ebb47)
    void IgnoreAcknWithEmptyBody();

    // round trips through two connected CProtocol instances
    void RoundTripJitBufSize_data();
    void RoundTripJitBufSize();
    void RoundTripClientID_data();
    void RoundTripClientID();
    void RoundTripChanGain_data();
    void RoundTripChanGain();
    void RoundTripChanPan_data();
    void RoundTripChanPan();
    void RoundTripMuteState_data();
    void RoundTripMuteState();
    void RoundTripChatText_data();
    void RoundTripChatText();
    void RoundTripNetwTranspProps();
    void RoundTripLicenceRequired_data();
    void RoundTripLicenceRequired();
    void RoundTripRecorderState_data();
    void RoundTripRecorderState();
    void RoundTripCLPing();

    // invalid message bodies inside well formed frames must not emit signals
    void RejectInvalidMessageBody_data();
    void RejectInvalidMessageBody();
};

void CTestProtocol::GoldenFrameJitBufSize()
{
    const CVector<uint8_t> vecbyFrame = SendAndCaptureFrame ( [] ( CProtocol& p ) { p.CreateJitBufMes ( 5 ); } );

    QCOMPARE ( ToByteArray ( vecbyFrame ).toHex ( ' ' ), QByteArray ( "00 00 0a 00 00 02 00 05 00 5e 06" ) );
}

void CTestProtocol::GoldenFrameClientID()
{
    const CVector<uint8_t> vecbyFrame = SendAndCaptureFrame ( [] ( CProtocol& p ) { p.CreateClientIDMes ( 7 ); } );

    QCOMPARE ( ToByteArray ( vecbyFrame ).toHex ( ' ' ), QByteArray ( "00 00 20 00 00 01 00 07 1e bc" ) );
}

void CTestProtocol::GoldenFrameChatText()
{
    const CVector<uint8_t> vecbyFrame = SendAndCaptureFrame ( [] ( CProtocol& p ) { p.CreateChatTextMes ( QStringLiteral ( "Hi" ) ); } );

    QCOMPARE ( ToByteArray ( vecbyFrame ).toHex ( ' ' ), QByteArray ( "00 00 12 00 00 04 00 02 00 48 69 4a 2c" ) );
}

void CTestProtocol::GoldenFrameCLPing()
{
    CProtocol        Sender;
    CVector<uint8_t> vecbyFrame;

    QObject::connect ( &Sender, &CProtocol::CLMessReadyForSending, [&vecbyFrame] ( CHostAddress, CVector<uint8_t> vecMessage ) {
        vecbyFrame = vecMessage;
    } );

    // connection less messages don't depend on a CProtocol instance's send
    // counter (it's zero by definition, see CreateAndImmSendConLessMessage()
    // in protocol.cpp), so unlike the other three golden frames above, a
    // freshly constructed CProtocol isn't actually load-bearing for
    // determinism here -- kept anyway for consistency with the others.
    Sender.CreateCLPingMes ( CHostAddress ( QHostAddress ( "203.0.113.42" ), 22124 ), 12345 );

    QCOMPARE ( ToByteArray ( vecbyFrame ).toHex ( ' ' ), QByteArray ( "00 00 e9 03 00 04 00 39 30 00 00 54 7a" ) );
}

void CTestProtocol::ParseValidFrame()
{
    // pure ParseMessageFrame() header-decode contract: does it recover the
    // counter (0 -- a fresh CProtocol's very first message) and ID of a
    // genuine, valid production frame. Whether the ID's *body* subsequently
    // evaluates to the right value is the round trip family's job; whether
    // the frame's exact *bytes* are the ones production is contractually
    // required to keep emitting is the golden frame tests' job above.
    const CVector<uint8_t> vecbyFrame = SendAndCaptureFrame ( [] ( CProtocol& p ) { p.CreateClientIDMes ( 42 ); } );

    CVector<uint8_t> vecbyMesBodyData;
    int              iRecCounter = -1;
    int              iRecID      = -1;

    QVERIFY ( ParseFrame ( vecbyFrame, vecbyMesBodyData, iRecCounter, iRecID ) );
    QCOMPARE ( iRecCounter, 0 );
    QCOMPARE ( iRecID, PROTMESSID_CLIENT_ID );
}

void CTestProtocol::RejectInvalidFrame_data()
{
    QTest::addColumn<QByteArray> ( "baFrame" );

    // a real, production generated frame to mutate below (CProtocolTester's
    // own validFrame()); its actual body length (rather than a hardcoded
    // constant) drives the length-field mutations further down so this stays
    // correct regardless of which message validFrame() happens to use
    const QByteArray baValidFrame = ToByteArray ( CProtocolTester().validFrame().frame() );
    const int        iBodyLen     = baValidFrame.size() - MESS_LEN_WITHOUT_DATA_BYTE;

    QTest::newRow ( "empty input" ) << QByteArray();

    QTest::newRow ( "shorter than minimum frame length" ) << baValidFrame.left ( MESS_LEN_WITHOUT_DATA_BYTE - 1 );

    // no dedicated mutation helper for this one -- it's a single, one-off
    // bit flip of the first header byte, not worth naming
    QByteArray baBadTag = baValidFrame;
    baBadTag[0]         = static_cast<char> ( baBadTag[0] ^ 0xFF );
    QTest::newRow ( "invalid tag" ) << baBadTag;

    // the rest reuse CProtocolTester's own frame mutators (withBadCRC() etc.)
    // via frame(), rather than duplicating that logic here
    QTest::newRow ( "invalid CRC" ) << ToByteArray ( CProtocolTester().validFrame().withBadCRC().frame() );

    QTest::newRow ( "declared length larger than data" )
        << ToByteArray ( CProtocolTester().validFrame().withDeclaredLength ( iBodyLen + 1 ).frame() );

    QTest::newRow ( "declared length smaller than data" )
        << ToByteArray ( CProtocolTester().validFrame().withDeclaredLength ( iBodyLen - 1 ).frame() );

    QTest::newRow ( "frame truncated on the wire" ) << ToByteArray ( CProtocolTester().validFrame().truncatedBy ( 2 ).frame() );

    // pure junk: no valid frame to mutate, so these two stay raw literals
    QTest::newRow ( "junk data" ) << QByteArray ( 50, static_cast<char> ( 0xA5 ) );

    // oversized junk with a valid tag so that the header decoding is reached
    QByteArray baOversized ( MAX_SIZE_BYTES_NETW_BUF, static_cast<char> ( 0xC3 ) );
    baOversized[0] = 0;
    baOversized[1] = 0;
    QTest::newRow ( "oversized junk data" ) << baOversized;
}

void CTestProtocol::RejectInvalidFrame()
{
    QFETCH ( QByteArray, baFrame );

    const CVector<uint8_t> vecbyFrame = FromByteArray ( baFrame );

    CVector<uint8_t> vecbyMesBodyData;
    int              iRecCounter = 0;
    int              iRecID      = 0;

    // ParseMessageFrame() returns true on error
    QVERIFY ( CProtocol::ParseMessageFrame ( vecbyFrame, vecbyFrame.Size(), vecbyMesBodyData, iRecCounter, iRecID ) );
}

// demonstrates the fluent frame builder for a case that reads better as prose
// than as one more row in RejectInvalidFrame_data() above
void CTestProtocol::RejectTruncatedFrame()
{
    CProtocolTester tester;

    QVERIFY ( tester.validFrame().truncatedBy ( 3 ).isRejected() );
}

void CTestProtocol::IgnoreAcknWithEmptyBody()
{
    // production always fills an ACKN frame's body with the 2 byte
    // acknowledged message ID (see CProtocol::CreateAndImmSendAcknMess() in
    // protocol.cpp), so there is no direct "call this and capture it" path to
    // a real, empty bodied ACKN frame -- capture a real one (by letting a
    // Peer instance auto-ACKN a received message, same as CChannel would),
    // then reshape its body to empty. That is the actual edge case this
    // regression targets: a well formed frame production never emits by
    // itself (fixed in commit 024ebb47).
    CVector<uint8_t> vecbyRealAcknFrame;
    {
        CProtocol Sender;
        CProtocol Peer;

        QObject::connect ( &Peer, &CProtocol::MessReadyForSending, [&vecbyRealAcknFrame] ( CVector<uint8_t> vecMessage ) {
            vecbyRealAcknFrame = vecMessage;
        } );

        QObject::connect ( &Sender, &CProtocol::MessReadyForSending, [&Peer] ( CVector<uint8_t> vecMessage ) {
            CVector<uint8_t> vecbyMesBodyData;
            int              iRecCounter = 0;
            int              iRecID      = 0;

            QVERIFY ( ParseFrame ( vecMessage, vecbyMesBodyData, iRecCounter, iRecID ) );

            Peer.ParseMessageBody ( vecbyMesBodyData, iRecCounter, iRecID );
        } );

        Sender.CreateClientIDMes ( 1 ); // any message triggers Peer's auto-ACKN reply
    }

    CVector<uint8_t> vecbyEmptyBodyAckn = vecbyRealAcknFrame;
    ReplaceIdAndBody ( vecbyEmptyBodyAckn, PROTMESSID_ACKN, CVector<uint8_t> ( 0 ) );

    CProtocol Receiver;

    int iNumSentMess = 0;

    QObject::connect ( &Receiver, &CProtocol::MessReadyForSending, [&iNumSentMess] ( CVector<uint8_t> ) { iNumSentMess++; } );

    CVector<uint8_t> vecbyMesBodyData;
    int              iRecCounter = 0;
    int              iRecID      = 0;

    // the frame itself is well formed and must parse successfully
    QVERIFY ( ParseFrame ( vecbyEmptyBodyAckn, vecbyMesBodyData, iRecCounter, iRecID ) );
    QCOMPARE ( iRecID, PROTMESSID_ACKN );
    QCOMPARE ( vecbyMesBodyData.Size(), 0 );

    // an ACKN message with an empty body must be ignored without a reply
    Receiver.ParseMessageBody ( vecbyMesBodyData, iRecCounter, iRecID );

    QCOMPARE ( iNumSentMess, 0 );
}

void CTestProtocol::RoundTripJitBufSize_data()
{
    QTest::addColumn<int> ( "iJitBufSize" );

    QTest::newRow ( "minimum size" ) << MIN_NET_BUF_SIZE_NUM_BL;
    QTest::newRow ( "maximum size" ) << MAX_NET_BUF_SIZE_NUM_BL;
    QTest::newRow ( "auto setting" ) << AUTO_NET_BUF_SIZE_FOR_PROTOCOL;
}

void CTestProtocol::RoundTripJitBufSize()
{
    QFETCH ( int, iJitBufSize );

    CProtocolTester tester;

    QVERIFY ( tester.jitBufSize ( iJitBufSize ).roundTrips() );
}

void CTestProtocol::RoundTripClientID_data()
{
    QTest::addColumn<int> ( "iChanID" );

    QTest::newRow ( "channel 0" ) << 0;
    QTest::newRow ( "channel 1" ) << 1;
    QTest::newRow ( "channel 250" ) << 250;
}

void CTestProtocol::RoundTripClientID()
{
    QFETCH ( int, iChanID );

    CProtocolTester tester;

    QVERIFY ( tester.clientID ( iChanID ).roundTrips() );
}

void CTestProtocol::RoundTripChanGain_data()
{
    QTest::addColumn<int> ( "iChanID" );
    QTest::addColumn<float> ( "fGain" );

    QTest::newRow ( "zero gain" ) << 0 << 0.0f;
    QTest::newRow ( "half gain" ) << 7 << 0.5f;
    QTest::newRow ( "full gain" ) << 42 << 1.0f;
    QTest::newRow ( "arbitrary gain" ) << 5 << 0.333f;
}

void CTestProtocol::RoundTripChanGain()
{
    QFETCH ( int, iChanID );
    QFETCH ( float, fGain );

    CProtocolTester tester;

    // the gain is quantized to 1 / 2^15 steps on the wire
    QVERIFY ( tester.chanGain ( iChanID, fGain ).roundTripsWithin ( 1.0f / ( 1 << 15 ) ) );
}

void CTestProtocol::RoundTripChanPan_data()
{
    QTest::addColumn<int> ( "iChanID" );
    QTest::addColumn<float> ( "fPan" );

    QTest::newRow ( "pan left" ) << 3 << 0.0f;
    QTest::newRow ( "pan center" ) << 4 << 0.5f;
    QTest::newRow ( "pan right" ) << 5 << 1.0f;
}

void CTestProtocol::RoundTripChanPan()
{
    QFETCH ( int, iChanID );
    QFETCH ( float, fPan );

    CProtocolTester tester;

    // the pan is quantized to 1 / 2^15 steps on the wire
    QVERIFY ( tester.chanPan ( iChanID, fPan ).roundTripsWithin ( 1.0f / ( 1 << 15 ) ) );
}

void CTestProtocol::RoundTripMuteState_data()
{
    QTest::addColumn<int> ( "iChanID" );
    QTest::addColumn<bool> ( "bIsMuted" );

    QTest::newRow ( "muted" ) << 2 << true;
    QTest::newRow ( "not muted" ) << 3 << false;
}

void CTestProtocol::RoundTripMuteState()
{
    QFETCH ( int, iChanID );
    QFETCH ( bool, bIsMuted );

    CProtocolTester tester;

    QVERIFY ( tester.muteState ( iChanID, bIsMuted ).roundTrips() );
}

void CTestProtocol::RoundTripChatText_data()
{
    QTest::addColumn<QString> ( "strChatText" );

    QTest::newRow ( "plain ASCII" ) << QString ( "Hello, Jamulus!" );
    QTest::newRow ( "non ASCII UTF-8" ) << QString::fromUtf8 ( "Grüße aus Tókyō \U0001F3B6" );
}

void CTestProtocol::RoundTripChatText()
{
    QFETCH ( QString, strChatText );

    CProtocolTester tester;

    QVERIFY ( tester.chatText ( strChatText ).roundTrips() );
}

void CTestProtocol::RoundTripNetwTranspProps()
{
    CProtocol Sender;
    CProtocol Receiver;

    ConnectProtocols ( Sender, Receiver );
    ConnectProtocols ( Receiver, Sender );

    CNetworkTransportProps ReceivedProps;
    int                    iNumReceived = 0;

    QObject::connect ( &Receiver, &CProtocol::NetTranspPropsReceived, [&] ( CNetworkTransportProps Props ) {
        ReceivedProps = Props;
        iNumReceived++;
    } );

    const CNetworkTransportProps SentProps ( 166, FRAME_SIZE_FACTOR_PREFERRED, 2, 48000, CT_OPUS, NF_WITH_COUNTER, 42 );

    Sender.CreateNetwTranspPropsMes ( SentProps );

    QCOMPARE ( iNumReceived, 1 );
    QCOMPARE ( ReceivedProps.iBaseNetworkPacketSize, SentProps.iBaseNetworkPacketSize );
    QCOMPARE ( ReceivedProps.iBlockSizeFact, SentProps.iBlockSizeFact );
    QCOMPARE ( ReceivedProps.iNumAudioChannels, SentProps.iNumAudioChannels );
    QCOMPARE ( ReceivedProps.iSampleRate, SentProps.iSampleRate );
    QCOMPARE ( static_cast<int> ( ReceivedProps.eAudioCodingType ), static_cast<int> ( SentProps.eAudioCodingType ) );
    QCOMPARE ( static_cast<int> ( ReceivedProps.eFlags ), static_cast<int> ( SentProps.eFlags ) );
    QCOMPARE ( ReceivedProps.iAudioCodingArg, SentProps.iAudioCodingArg );
}

void CTestProtocol::RoundTripLicenceRequired_data()
{
    QTest::addColumn<int> ( "iLicenceType" );

    QTest::newRow ( "no licence" ) << static_cast<int> ( LT_NO_LICENCE );
    QTest::newRow ( "Creative Commons" ) << static_cast<int> ( LT_CREATIVECOMMONS );
}

void CTestProtocol::RoundTripLicenceRequired()
{
    QFETCH ( int, iLicenceType );

    CProtocolTester tester;

    QVERIFY ( tester.licenceRequired ( static_cast<ELicenceType> ( iLicenceType ) ).roundTrips() );
}

void CTestProtocol::RoundTripRecorderState_data()
{
    QTest::addColumn<int> ( "iRecorderState" );

    QTest::newRow ( "not initialised" ) << static_cast<int> ( RS_NOT_INITIALISED );
    QTest::newRow ( "not enabled" ) << static_cast<int> ( RS_NOT_ENABLED );
    QTest::newRow ( "recording" ) << static_cast<int> ( RS_RECORDING );
}

void CTestProtocol::RoundTripRecorderState()
{
    QFETCH ( int, iRecorderState );

    CProtocolTester tester;

    QVERIFY ( tester.recorderState ( static_cast<ERecorderState> ( iRecorderState ) ).roundTrips() );
}

void CTestProtocol::RoundTripCLPing()
{
    CProtocol Sender;
    CProtocol Receiver;

    // connection less messages are emitted via CLMessReadyForSending, deliver
    // them to the receiver like CSocket does for received network packets
    QObject::connect ( &Sender, &CProtocol::CLMessReadyForSending, [&Receiver] ( CHostAddress InetAddr, CVector<uint8_t> vecMessage ) {
        CVector<uint8_t> vecbyMesBodyData;
        int              iRecCounter = -1;
        int              iRecID      = 0;

        QVERIFY ( ParseFrame ( vecMessage, vecbyMesBodyData, iRecCounter, iRecID ) );

        // the counter of connection less messages is zero by definition
        QCOMPARE ( iRecCounter, 0 );
        QVERIFY ( CProtocol::IsConnectionLessMessageID ( iRecID ) );

        Receiver.ParseConnectionLessMessageBody ( vecbyMesBodyData, iRecID, InetAddr );
    } );

    CHostAddress ReceivedInetAddr;
    int          iReceivedMs  = 0;
    int          iNumReceived = 0;

    QObject::connect ( &Receiver, &CProtocol::CLPingReceived, [&] ( CHostAddress InetAddr, int iMs ) {
        ReceivedInetAddr = InetAddr;
        iReceivedMs      = iMs;
        iNumReceived++;
    } );

    const CHostAddress TestInetAddr ( QHostAddress ( "203.0.113.42" ), 22124 );

    Sender.CreateCLPingMes ( TestInetAddr, 12345 );

    QCOMPARE ( iNumReceived, 1 );
    QCOMPARE ( iReceivedMs, 12345 );
    QVERIFY ( ReceivedInetAddr == TestInetAddr );
}

void CTestProtocol::RejectInvalidMessageBody_data()
{
    QTest::addColumn<int> ( "iID" );
    QTest::addColumn<QByteArray> ( "baBody" );

    // jitter buffer size message with a truncated body (2 bytes expected)
    QTest::newRow ( "jitter buffer size body too short" ) << PROTMESSID_JITT_BUF_SIZE << QByteArray ( 1, 5 );

    // channel gain message with a truncated body (3 bytes expected)
    QTest::newRow ( "channel gain body too short" ) << PROTMESSID_CHANNEL_GAIN << QByteArray ( 2, 0 );

    // chat text message whose string length field claims more bytes than are
    // actually present in the body
    QByteArray baChatTruncated;
    baChatTruncated.append ( static_cast<char> ( 200 ) ); // string length 200 (2 bytes, little endian)
    baChatTruncated.append ( static_cast<char> ( 0 ) );
    baChatTruncated.append ( "abc" ); // but only 3 bytes of string data follow
    QTest::newRow ( "chat text length field beyond data" ) << PROTMESSID_CHAT_TEXT << baChatTruncated;

    // chat text message with trailing bytes after the string
    QByteArray baChatTrailing;
    baChatTrailing.append ( static_cast<char> ( 1 ) ); // string length 1 (2 bytes, little endian)
    baChatTrailing.append ( static_cast<char> ( 0 ) );
    baChatTrailing.append ( 'a' );
    baChatTrailing.append ( static_cast<char> ( 0x77 ) ); // trailing garbage
    QTest::newRow ( "chat text trailing garbage" ) << PROTMESSID_CHAT_TEXT << baChatTrailing;
}

void CTestProtocol::RejectInvalidMessageBody()
{
    QFETCH ( int, iID );
    QFETCH ( QByteArray, baBody );

    CProtocol Receiver;

    QSignalSpy SpyJitBuf ( &Receiver, SIGNAL ( ChangeJittBufSize ( int ) ) );
    QSignalSpy SpyGain ( &Receiver, SIGNAL ( ChangeChanGain ( int, float ) ) );
    QSignalSpy SpyChat ( &Receiver, SIGNAL ( ChatTextReceived ( QString ) ) );

    // build a well formed frame around the invalid body (which production
    // would never generate itself -- that's the point) so that the parsing
    // reaches the body evaluation; the base frame just needs to be *a* real,
    // valid frame, its own message/ID/body are irrelevant since
    // ReplaceIdAndBody() overwrites both
    CVector<uint8_t> vecbyFrame = SendAndCaptureFrame ( [] ( CProtocol& p ) { p.CreateJitBufMes ( 0 ); } );
    ReplaceIdAndBody ( vecbyFrame, iID, FromByteArray ( baBody ) );

    CVector<uint8_t> vecbyMesBodyData;
    int              iRecCounter = 0;
    int              iRecID      = 0;

    QVERIFY ( ParseFrame ( vecbyFrame, vecbyMesBodyData, iRecCounter, iRecID ) );

    // note that an ACKN frame is still sent for a message with an invalid
    // body, the evaluation error only suppresses the receive signal
    Receiver.ParseMessageBody ( vecbyMesBodyData, iRecCounter, iRecID );

    QCOMPARE ( SpyJitBuf.count(), 0 );
    QCOMPARE ( SpyGain.count(), 0 );
    QCOMPARE ( SpyChat.count(), 0 );
}

QTEST_GUILESS_MAIN ( CTestProtocol )

#include "tst_protocol.moc"
