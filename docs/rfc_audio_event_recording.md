# RFC: Audio Event Streaming for High-Fidelity Mix Reconstruction

## Status
Proposed

## Summary
This RFC proposes a new TCP-based audio event streaming system for Jamulus that captures and streams audio events at a lower level than the current WAV recording system. By streaming the original Opus packets before decoding, along with exact mixing metadata, this system enables higher quality post-jam reconstructions and more flexible processing options.

## Motivation
Jamulus currently supports recording sessions via the `JamController` class, which stores decoded audio in multitrack WAV format. While functional, this approach has several limitations:

1. WAV files are large and inefficient for long jam sessions
2. Packets that arrive too late for the jitter buffer create audio gaps in the recording
3. Audio data is stored post-decoder, losing the original compressed packets
4. The WAV recording quality is limited by real-time constraints of the live server

By streaming the raw Opus packets and mixing events instead of storing decoded audio, we can:

1. Preserve data more efficiently since Opus is already compressed
2. Retroactively include late packets in post-processing
3. Apply custom gain/panning settings during post-processing
4. Create higher quality mixes by using all available audio data, including packets that arrived too late for real-time mixing
5. Analyze network performance and audio quality metrics after the session
6. Support development of specialized external tools without modifying the Jamulus core

## Design

### Event Types
The system will record two primary types of events:

1. **Opus Packet Received Event**: Captures the original compressed audio packets from clients
2. **Audio Frame Mixed Event**: Records the exact mixing parameters used for each frame

### Binary Format
The TCP stream consists of a series of timestamped events sent sequentially without any header. Clients can connect at any time and begin receiving events immediately:

```
Each Event:
- Timestamp: uint64_t (8 bytes, microseconds since server start)
- Event Type: uint8_t (1 byte)
- Event Data Length: uint32_t (4 bytes)
- Event Data: Variable length byte array
```

Note: Essential server parameters like frame size, sample rate, and server start time will be included in each Audio Frame Mixed Event, making a separate header unnecessary.

#### Opus Packet Received Event (Type 0x01):
```
- Channel ID: uint32_t (4 bytes)
- Packet Sequence Number: uint64_t (8 bytes, monotonically increasing per channel)
- Audio Compression Type: uint8_t (1 byte, CT_OPUS=0, CT_OPUS64=1)
- Number of Audio Channels: uint8_t (1 byte, 1=mono, 2=stereo)
- Opus Coded Bytes: uint16_t (2 bytes, size of Opus packet)
- Opus Data: Variable length raw opus packet
```

#### Audio Frame Mixed Event (Type 0x02):
```
- Frame Sequence Number: uint64_t (8 bytes, monotonically increasing)
- Server Frame Size Samples: uint16_t (2 bytes)
- Sample Rate: uint32_t (4 bytes)
- Number of Channels in Mix: uint16_t (2 bytes)
For each channel:
  - Channel ID: uint32_t (4 bytes)
  - Packet Sequence Number: uint64_t (8 bytes, references which packet was used)
  - Decoder Parameters:
    - Audio Compression Type: uint8_t (1 byte)
    - Number of Audio Channels: uint8_t (1 byte)
    - Frame Size Samples: uint16_t (2 bytes)
  - Musician Profile:
    - Name Length: uint8_t (1 byte)
    - Name: UTF-8 string (variable length)
    - City Length: uint8_t (1 byte)
    - City: UTF-8 string (variable length)
    - Country Code: uint16_t (2 bytes, as QLocale::Country value)
    - Skill Level: uint8_t (1 byte)
    - Instrument: uint8_t (1 byte, instrument ID)
```

### Implementation

#### New Class

`CAudioEventRecorder` - Manages the audio event streaming system
  - Maintains a list of connected TCP clients
  - Formats and streams events to connected clients
  - Provides methods to record different event types
  - Handles client disconnections and synchronization

#### Server Modifications

1. Add the `CAudioEventRecorder` class as a member of `CServer`
2. Add a TCP server component to stream audio events
3. Hook into `PutAudioData` to capture incoming Opus packets
4. Hook into `MixEncodeTransmitData` to record mixing decisions
5. Add command-line option to specify the TCP port for event streaming

### Integration

The new recording system will be available through a TCP stream server that clients can connect to:

```cpp
// In server constructor
void CServer::InitAudioEventStreaming(const quint16 iEventStreamPort)
{
    if (iEventStreamPort > 0)
    {
        // Create audio event recorder and TCP server
        pAudioEventRecorder = std::make_unique<CAudioEventRecorder>();
        
        // Set up TCP server on the specified port
        pEventStreamServer = std::make_unique<QTcpServer>();
        
        if (!pEventStreamServer->listen(QHostAddress::LocalHost, iEventStreamPort))
        {
            qCritical() << "Failed to start Audio Event Stream server on port" << iEventStreamPort;
            return;
        }
        
        // Set up connection handler
        connect(pEventStreamServer.get(), &QTcpServer::newConnection, this, &CServer::OnNewEventStreamConnection);
        
        qInfo() << "Audio Event Stream server listening on port" << iEventStreamPort;
    }
}

// Connection handler
void CServer::OnNewEventStreamConnection()
{
    // New client connected to receive the event stream
    QTcpSocket* pSocket = pEventStreamServer->nextPendingConnection();
    pAudioEventRecorder->AddStreamClient(pSocket);
    
    // Client starts receiving events immediately, no header needed
}
```

## Backward Compatibility
This feature adds a new recording format without affecting the existing WAV recording system. Older clients will not be affected.

## Security Considerations
The event streaming system will only be accessible to those with network access to the server port. By default, the TCP server will only listen on localhost (127.0.0.1), meaning only local connections are accepted. For remote access, server operators would need to explicitly configure firewall rules and modify the listening address.

Since the system uses a TCP stream rather than writing to disk, there are no file permission concerns. However, server operators should be aware that the stream contains the raw audio data from all participants and treat access to the stream with appropriate privacy considerations.

## Implementation Timeline

1. Implement the `CAudioEventRecorder` class with TCP streaming capability
2. Modify the server to capture events at key points in the audio pipeline
3. Add command-line option to enable event streaming and specify port
4. Create simple example client to demonstrate consuming the event stream
5. Add documentation for the new feature

## Future Extensions

1. **Selective Channel Streaming**: Allow streaming only specific channels
2. **WebSocket Support**: Add WebSocket protocol option for web-based clients
3. **Network Performance Metrics**: Add additional event types to track network conditions
4. **Multiple Stream Formats**: Support both binary and JSON formats for different client needs
5. **Authentication**: Add simple authentication mechanism for the TCP server
6. **External Playback and Processing Tools**: The binary format is designed to be consumed by external tools for playback, analysis, and conversion

## Conclusion
The Audio Event Streaming system provides a powerful new way to capture Jamulus sessions at the packet level, enabling higher quality post-processing and more flexible use of the audio data. The TCP streaming approach simplifies implementation by removing file I/O concerns while still providing all the information needed to recreate high-quality mixes. This system enables capabilities not possible with the current WAV recording system and allows for innovative external processing tools to be developed independently.