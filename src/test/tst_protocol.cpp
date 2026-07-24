/******************************************************************************\
 * Copyright (c) 2026
 *
 * Author(s):
 *  dtinth
 *
 * As of Jamulus 3.12.1dev (commit eb172d47): All new source code contributions must be licensed
 * under AGPL 3.0 or any later version.
 *
 ******************************************************************************
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
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
    // On-wire compatibility contract: pinned bytes production emits TODAY.
    // A mismatch means the wire format changed -- update the literal only as
    // a deliberate, reviewed decision.
    void GoldenFrameJitBufSize();
    void GoldenFrameClientID();
    void GoldenFrameChatText();
    void GoldenFrameCLPing();

    // frame acceptance contract
    void AcceptValidFrame();
    void RejectInvalidFrame_data();
    void RejectInvalidFrame();
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
    // Arrange
    CProtocolTester Tester;

    // Act
    Tester.Sender.CreateJitBufMes ( 5 );

    // Assert
    QString strExpectedFrame;
    strExpectedFrame += "00 00 "; // TAG
    strExpectedFrame += "0a 00 "; // message ID: PROTMESSID_JITT_BUF_SIZE (10)
    strExpectedFrame += "00 ";    // sequence counter (fresh instance -> 0)
    strExpectedFrame += "02 00 "; // data length (2 bytes)
    strExpectedFrame += "05 00 "; // data: jitter buffer size 5
    strExpectedFrame += "5e 06";  // CRC

    QCOMPARE ( Tester.SentFrames(), QStringList{ strExpectedFrame } );
}

void CTestProtocol::GoldenFrameClientID()
{
    // Arrange
    CProtocolTester Tester;

    // Act
    Tester.Sender.CreateClientIDMes ( 7 );

    // Assert
    QString strExpectedFrame;
    strExpectedFrame += "00 00 "; // TAG
    strExpectedFrame += "20 00 "; // message ID: PROTMESSID_CLIENT_ID (32)
    strExpectedFrame += "00 ";    // sequence counter (fresh instance -> 0)
    strExpectedFrame += "01 00 "; // data length (1 byte)
    strExpectedFrame += "07 ";    // data: client ID 7
    strExpectedFrame += "1e bc";  // CRC

    QCOMPARE ( Tester.SentFrames(), QStringList{ strExpectedFrame } );
}

void CTestProtocol::GoldenFrameChatText()
{
    // Arrange
    CProtocolTester Tester;

    // Act
    Tester.Sender.CreateChatTextMes ( QStringLiteral ( "Hi" ) );

    // Assert
    QString strExpectedFrame;
    strExpectedFrame += "00 00 "; // TAG
    strExpectedFrame += "12 00 "; // message ID: PROTMESSID_CHAT_TEXT (18)
    strExpectedFrame += "00 ";    // sequence counter (fresh instance -> 0)
    strExpectedFrame += "04 00 "; // data length (4 bytes)
    strExpectedFrame += "02 00 "; // data: UTF-8 string length (2 bytes)
    strExpectedFrame += "48 69 "; // data: UTF-8 bytes ("Hi")
    strExpectedFrame += "4a 2c";  // CRC

    QCOMPARE ( Tester.SentFrames(), QStringList{ strExpectedFrame } );
}

void CTestProtocol::GoldenFrameCLPing()
{
    // Arrange
    CProtocolTester Tester;

    // Act -- connection-less messages go via CLSentFrames(), not SentFrames()
    Tester.Sender.CreateCLPingMes ( CHostAddress ( QHostAddress ( "203.0.113.42" ), 22124 ), 12345 );

    // Assert
    QString strExpectedFrame;
    strExpectedFrame += "00 00 ";       // TAG
    strExpectedFrame += "e9 03 ";       // message ID: PROTMESSID_CLM_PING_MS (1001)
    strExpectedFrame += "00 ";          // sequence counter (connection-less messages are always 0)
    strExpectedFrame += "04 00 ";       // data length (4 bytes)
    strExpectedFrame += "39 30 00 00 "; // data: ping timestamp 12345 (uint32 LE)
    strExpectedFrame += "54 7a";        // CRC

    QCOMPARE ( Tester.CLSentFrames(), QStringList{ strExpectedFrame } );
}

void CTestProtocol::AcceptValidFrame()
{
    // A genuine, valid production frame must reach ParseMessageBody() on the
    // receiving side.
    CProtocolTester Tester;
    Tester.Sender.CreateChatTextMes ( QStringLiteral ( "frame contract test" ) );

    QCOMPARE ( Tester.ReceivedAndAcceptedMessageCount(), 1 );
}

void CTestProtocol::RejectInvalidFrame_data()
{
    QTest::addColumn<QByteArray> ( "baFrame" );

    // a real, production generated frame to mutate below
    CProtocolTester Tester;
    Tester.Sender.CreateChatTextMes ( QStringLiteral ( "frame contract test" ) );

    const CVector<uint8_t> vecbyValidFrame = Tester.LastSentFrame();
    const QByteArray       baValidFrame    = CProtocolTester::ToByteArray ( vecbyValidFrame );
    const int              iBodyLen        = baValidFrame.size() - MESS_LEN_WITHOUT_DATA_BYTE;

    QTest::newRow ( "empty input" ) << QByteArray();

    QTest::newRow ( "shorter than minimum frame length" ) << baValidFrame.left ( MESS_LEN_WITHOUT_DATA_BYTE - 1 );

    // one-off bit flip of the first header byte
    QByteArray baBadTag = baValidFrame;
    baBadTag[0]         = static_cast<char> ( baBadTag[0] ^ 0xFF );
    QTest::newRow ( "invalid tag" ) << baBadTag;

    CVector<uint8_t> vecbyBadCRC = vecbyValidFrame;
    CProtocolTester::CorruptCRC ( vecbyBadCRC );
    QTest::newRow ( "invalid CRC" ) << CProtocolTester::ToByteArray ( vecbyBadCRC );

    CVector<uint8_t> vecbyLenTooLarge = vecbyValidFrame;
    CProtocolTester::SetDeclaredLength ( vecbyLenTooLarge, iBodyLen + 1 );
    QTest::newRow ( "declared length larger than data" ) << CProtocolTester::ToByteArray ( vecbyLenTooLarge );

    CVector<uint8_t> vecbyLenTooSmall = vecbyValidFrame;
    CProtocolTester::SetDeclaredLength ( vecbyLenTooSmall, iBodyLen - 1 );
    QTest::newRow ( "declared length smaller than data" ) << CProtocolTester::ToByteArray ( vecbyLenTooSmall );

    CVector<uint8_t> vecbyTruncated = vecbyValidFrame;
    CProtocolTester::TruncateBy ( vecbyTruncated, 2 );
    QTest::newRow ( "frame truncated on the wire" ) << CProtocolTester::ToByteArray ( vecbyTruncated );

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

    CProtocolTester Tester;
    Tester.SendRawBytes ( baFrame );

    QCOMPARE ( Tester.ReceivedAndAcceptedMessageCount(), 0 );
}

void CTestProtocol::IgnoreAcknWithEmptyBody()
{
    // Regression test for https://github.com/jamulussoftware/jamulus/issues/302
    // (fixed in 024ebb47): an ACKN with a valid checksum but no data caused an
    // out-of-bounds read. Still well-formed at the frame level, so it's
    // accepted there; ParseMessageBody()'s size check must silently drop it
    // -- the ASan/UBSan job is what gives "no OOB" its teeth.
    CProtocolTester Seed;
    Seed.Sender.CreateChatTextMes ( QStringLiteral ( "frame contract test" ) );

    CVector<uint8_t> vecbyFrame = Seed.LastSentFrame();
    CProtocolTester::ReplaceIdAndBody ( vecbyFrame, PROTMESSID_ACKN, CVector<uint8_t>() );

    CProtocolTester Tester;
    Tester.SendRawBytes ( CProtocolTester::ToByteArray ( vecbyFrame ) );

    QCOMPARE ( Tester.ReceivedAndAcceptedMessageCount(), 1 ); // accepted at the frame level
    QVERIFY ( Tester.ReceivedLog().isEmpty() );               // but silently dropped, no signal fired
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

    // Arrange
    CProtocolTester Tester;

    // Act
    Tester.Sender.CreateJitBufMes ( iJitBufSize );

    // Assert
    QCOMPARE ( Tester.ReceivedLog(), QStringList{ QStringLiteral ( "ChangeJittBufSize(%1)" ).arg ( iJitBufSize ) } );
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

    // Arrange
    CProtocolTester Tester;

    // Act
    Tester.Sender.CreateClientIDMes ( iChanID );

    // Assert
    QCOMPARE ( Tester.ReceivedLog(), QStringList{ QStringLiteral ( "ClientIDReceived(%1)" ).arg ( iChanID ) } );
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

    // Arrange
    CProtocolTester Tester;

    // Act
    Tester.Sender.CreateChanGainMes ( iChanID, fGain );

    // Assert -- also proves no OTHER wired signal fired
    QCOMPARE ( Tester.ReceivedLog(),
               QStringList{ QStringLiteral ( "ChangeChanGain(%1, %2)" ).arg ( iChanID ).arg ( CProtocolTester::FormatFloat ( fGain ) ) } );
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

    // Arrange
    CProtocolTester Tester;

    // Act
    Tester.Sender.CreateChanPanMes ( iChanID, fPan );

    // Assert -- also proves no OTHER wired signal fired
    QCOMPARE ( Tester.ReceivedLog(),
               QStringList{ QStringLiteral ( "ChangeChanPan(%1, %2)" ).arg ( iChanID ).arg ( CProtocolTester::FormatFloat ( fPan ) ) } );
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

    // Arrange
    CProtocolTester Tester;

    // Act
    Tester.Sender.CreateMuteStateHasChangedMes ( iChanID, bIsMuted );

    // Assert
    QCOMPARE ( Tester.ReceivedLog(),
               QStringList{ QStringLiteral ( "MuteStateHasChangedReceived(%1, %2)" ).arg ( iChanID ).arg ( bIsMuted ? "true" : "false" ) } );
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

    // Arrange
    CProtocolTester Tester;

    // Act
    Tester.Sender.CreateChatTextMes ( strChatText );

    // Assert
    QCOMPARE ( Tester.ReceivedLog(), QStringList{ QStringLiteral ( "ChatTextReceived(%1)" ).arg ( strChatText ) } );
}

void CTestProtocol::RoundTripNetwTranspProps()
{
    // Arrange
    CProtocolTester Tester;

    const CNetworkTransportProps SentProps ( 166, FRAME_SIZE_FACTOR_PREFERRED, 2, 48000, CT_OPUS, NF_WITH_COUNTER, 42 );

    // Act
    Tester.Sender.CreateNetwTranspPropsMes ( SentProps );

    // Assert
    QCOMPARE ( Tester.ReceivedLog(),
               QStringList{ QStringLiteral ( "NetTranspPropsReceived(%1, %2, %3, %4, %5, %6, %7)" )
                                .arg ( SentProps.iBaseNetworkPacketSize )
                                .arg ( SentProps.iBlockSizeFact )
                                .arg ( SentProps.iNumAudioChannels )
                                .arg ( SentProps.iSampleRate )
                                .arg ( static_cast<int> ( SentProps.eAudioCodingType ) )
                                .arg ( static_cast<int> ( SentProps.eFlags ) )
                                .arg ( SentProps.iAudioCodingArg ) } );
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

    // Arrange
    CProtocolTester Tester;

    // Act
    Tester.Sender.CreateLicenceRequiredMes ( static_cast<ELicenceType> ( iLicenceType ) );

    // Assert
    QCOMPARE ( Tester.ReceivedLog(), QStringList{ QStringLiteral ( "LicenceRequired(%1)" ).arg ( iLicenceType ) } );
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

    // Arrange
    CProtocolTester Tester;

    // Act
    Tester.Sender.CreateRecorderStateMes ( static_cast<ERecorderState> ( iRecorderState ) );

    // Assert
    QCOMPARE ( Tester.ReceivedLog(), QStringList{ QStringLiteral ( "RecorderStateReceived(%1)" ).arg ( iRecorderState ) } );
}

void CTestProtocol::RoundTripCLPing()
{
    // Arrange
    CProtocolTester    Tester;
    const CHostAddress TestInetAddr ( QHostAddress ( "203.0.113.42" ), 22124 );

    // Act -- CLPing is delivered via the CL wiring (see WireCLMessages()),
    // not the regular Sender/Receiver connection above
    Tester.Sender.CreateCLPingMes ( TestInetAddr, 12345 );

    // Assert
    QCOMPARE ( Tester.ReceivedLog(), QStringList{ QStringLiteral ( "CLPingReceived(%1, %2)" ).arg ( TestInetAddr.toString() ).arg ( 12345 ) } );
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

    // a real, production generated frame to rebuild around the invalid body --
    // its own message/ID/body are irrelevant, ReplaceIdAndBody() overwrites both
    CProtocolTester Seed;
    Seed.Sender.CreateChatTextMes ( QStringLiteral ( "frame contract test" ) );

    CVector<uint8_t> vecbyFrame = Seed.LastSentFrame();
    CProtocolTester::ReplaceIdAndBody ( vecbyFrame, iID, CProtocolTester::FromByteArray ( baBody ) );

    CProtocolTester Tester;
    Tester.SendRawBytes ( CProtocolTester::ToByteArray ( vecbyFrame ) );

    // note that an ACKN frame is still sent for a message with an invalid
    // body (frame-level parsing succeeds), the evaluation error only
    // suppresses the receive signal
    QCOMPARE ( Tester.ReceivedAndAcceptedMessageCount(), 1 );
    QVERIFY ( Tester.ReceivedLog().isEmpty() );
}

QTEST_GUILESS_MAIN ( CTestProtocol )

#include "tst_protocol.moc"
