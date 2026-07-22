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

void CTestProtocol::ParseValidFrame()
{
    CVector<uint8_t> vecbyBody ( 4 );
    vecbyBody[0] = 0x11;
    vecbyBody[1] = 0x22;
    vecbyBody[2] = 0x33;
    vecbyBody[3] = 0x44;

    const CVector<uint8_t> vecbyFrame = GenTestFrame ( 33, PROTMESSID_CHAT_TEXT, vecbyBody );

    CVector<uint8_t> vecbyMesBodyData;
    int              iRecCounter = 0;
    int              iRecID      = 0;

    QVERIFY ( ParseFrame ( vecbyFrame, vecbyMesBodyData, iRecCounter, iRecID ) );
    QCOMPARE ( iRecCounter, 33 );
    QCOMPARE ( iRecID, PROTMESSID_CHAT_TEXT );
    QVERIFY ( vecbyMesBodyData == vecbyBody );
}

void CTestProtocol::RejectInvalidFrame_data()
{
    QTest::addColumn<QByteArray> ( "baFrame" );

    CVector<uint8_t> vecbyBody ( 4 );
    vecbyBody[0] = 1;
    vecbyBody[1] = 2;
    vecbyBody[2] = 3;
    vecbyBody[3] = 4;

    const QByteArray baValidFrame = ToByteArray ( GenTestFrame ( 0, PROTMESSID_CHAT_TEXT, vecbyBody ) );

    QTest::newRow ( "empty input" ) << QByteArray();

    QTest::newRow ( "shorter than minimum frame length" ) << baValidFrame.left ( MESS_LEN_WITHOUT_DATA_BYTE - 1 );

    QByteArray baBadTag = baValidFrame;
    baBadTag[0]         = static_cast<char> ( 0xFF );
    QTest::newRow ( "invalid tag" ) << baBadTag;

    QByteArray baBadCRC           = baValidFrame;
    baBadCRC[baBadCRC.size() - 1] = static_cast<char> ( baBadCRC[baBadCRC.size() - 1] ^ 0xFF );
    QTest::newRow ( "invalid CRC" ) << baBadCRC;

    QByteArray baLenTooLarge = baValidFrame;
    baLenTooLarge[5]         = 5; // body is actually 4 bytes long
    QTest::newRow ( "declared length larger than data" ) << baLenTooLarge;

    QByteArray baLenTooSmall = baValidFrame;
    baLenTooSmall[5]         = 3; // body is actually 4 bytes long
    QTest::newRow ( "declared length smaller than data" ) << baLenTooSmall;

    QTest::newRow ( "frame truncated on the wire" ) << baValidFrame.left ( baValidFrame.size() - 2 );

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
    CProtocol Receiver;

    int iNumSentMess = 0;

    QObject::connect ( &Receiver, &CProtocol::MessReadyForSending, [&iNumSentMess] ( CVector<uint8_t> ) { iNumSentMess++; } );

    const CVector<uint8_t> vecbyFrame = GenTestFrame ( 5, PROTMESSID_ACKN, CVector<uint8_t> ( 0 ) );

    CVector<uint8_t> vecbyMesBodyData;
    int              iRecCounter = 0;
    int              iRecID      = 0;

    // the frame itself is well formed and must parse successfully
    QVERIFY ( ParseFrame ( vecbyFrame, vecbyMesBodyData, iRecCounter, iRecID ) );
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

    // build a well formed frame around the invalid body so that the parsing
    // reaches the body evaluation
    const CVector<uint8_t> vecbyFrame = GenTestFrame ( 0, iID, FromByteArray ( baBody ) );

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
