# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Building and Development

### Quick Build Commands

**Linux/macOS - Desktop Client:**
```bash
make distclean  # Optional: clean previous build artifacts
qmake           # Generate build files
make            # Compile
sudo make install  # Optional: install to /usr/local/bin
```

**Linux - Headless Server:**
```bash
make distclean
qmake "CONFIG+=headless serveronly"
make
sudo make install
```

**macOS - Generate Xcode Project:**
```bash
qmake -spec macx-xcode Jamulus.pro
xcodebuild build  # Build from command line
```

**Windows:**
```powershell
# In PowerShell, navigate to jamulus directory
.\windows\deploy_windows.ps1 "C:\Qt\<path32>" "C:\Qt\<path64>"
# Installer will be in .\deploy directory
```

### Compile-Time Configuration

Use `qmake "CONFIG+=<option>"` to customize builds:

| Option | Description |
|--------|-------------|
| `serveronly` | Server-only build (no client features) |
| `headless` | Disable GUI (works with client or server) |
| `nojsonrpc` | Disable JSON-RPC interface |
| `jackonwindows` | Use JACK instead of ASIO on Windows |
| `noupcasename` | Build as lowercase "jamulus" instead of "Jamulus" |
| `disable_version_check` | Skip version update checks |

### Code Style and Quality

**Before committing, run clang-format:**
```bash
make clang_format  # After running qmake
# Or manually:
clang-format -i <path/to/changed/files>
```

**clang-format requirements:**
- The CI uses clang-format version 14
- Configuration is in `.clang-format` file
- Style: 4-space tabs (insert spaces), specific brace and space rules
- Use string substitutions for i18n: `tr("Hello %1").arg(name)` not `tr("Hello ") + name`

**Shell scripts and Python:**
```bash
# Bash: shellcheck and shfmt
shellcheck *.sh
shfmt -d .

# Python: pylint
pylint tools/*.py  # Uses .pylintrc config
```

## Codebase Architecture

### High-Level Overview

Jamulus is a Qt-based client-server application for real-time collaborative music. The core design:

- **Server** collects audio from all clients, mixes it, and sends mixed streams back
- **Client** captures local audio, sends to server, receives and plays mixed audio from others
- Uses **UDP** for low-latency connectionless communication
- Uses **OPUS codec** for audio compression (from `libs/opus/`)
- Custom **binary protocol** for all network messages (see `docs/JAMULUS_PROTOCOL.md`)

### Directory Structure

```
src/
  ├── client.h/cpp              # Main client logic
  ├── server.h/cpp              # Main server logic
  ├── channel.h/cpp             # Per-connection state (used by both)
  ├── protocol.h/cpp            # Network message encoding/decoding
  ├── socket.h/cpp              # UDP socket layer
  ├── buffer.h/cpp              # Ring/jitter buffers for audio
  ├── recorder/                 # Session recording (WAV, Reaper format)
  ├── sound/                    # Platform-specific audio drivers
  │   ├── asio/                 # Windows ASIO
  │   ├── jack/                 # JACK (Linux, macOS, Windows)
  │   ├── coreaudio-mac/        # macOS Core Audio
  │   ├── coreaudio-ios/        # iOS Core Audio
  │   └── oboe/                 # Android
  ├── plugins/                  # Audio processing (reverb effects)
  ├── android/, ios/, mac/      # Platform-specific UI code
  ├── translation/              # i18n translation files (.ts)
  └── res/                      # Resources (icons, sounds, instruments, flags)

libs/
  ├── opus/                     # OPUS audio codec source
  └── ...

docs/
  ├── JAMULUS_PROTOCOL.md       # Network protocol specification
  └── JSON-RPC.md               # JSON-RPC API documentation
```

### Core Components

| Class | File | Responsibility |
|-------|------|-----------------|
| `CServer` | `server.h/cpp` | Main server loop, client management, mixing logic |
| `CClient` | `client.h/cpp` | Audio capture/playback, server communication |
| `CChannel` | `channel.h/cpp` | Individual client connection state and buffers |
| `CProtocol` | `protocol.h/cpp` | Message encoding/decoding, ack tracking |
| `CSocket` | `socket.h/cpp` | UDP transmission/reception |
| `CNetBuf` | `buffer.h/cpp` | Jitter buffer with auto-sizing |
| `CSettings` | `settings.h/cpp` | Persistent configuration (INI files) |
| `CRpcServer`/`CClientRpc` | JSON-RPC implementation | Automation/control interface |

### Architecture Relationships

```
CClient/CServer (main logic)
    ↓
CChannel (per-connection state)
    ↓
CProtocol (message handling)
    ↓
CSocket (UDP)
    ↑ ↓ (audio codec)
CNetBuf/OPUS (buffering and compression)
    ↑ ↓
Sound drivers (platform-specific audio I/O)
```

### Audio Flow

**Client:**
1. Sound driver captures → `CClient` buffer → OPUS encode → UDP send
2. UDP receive → Jitter buffer → OPUS decode → Mix calculation → Sound driver playback

**Server:**
1. UDP receive from all clients → Per-channel buffers → Extract audio frames
2. Mix all frames → OPUS encode per-client → UDP send to each client

## Key Development Guidelines

### From CONTRIBUTING.md

**Before starting work:**
- Check if a GitHub issue exists for your feature/bug
- Post on GitHub Discussions to discuss spec before coding
- Get agreement before implementing (avoids rejected PRs)

**Code submission:**
- Run `make clang_format` before committing (clang-format v14)
- Update ChangeLog with single-sentence summary (keyword: `CHANGELOG: `)
- First-time contributors: add name to `src/util.cpp` in `CAboutDlg` constructor
- Maintain C++11 compatibility throughout
- Qt minimum version: 5.12.2

**Code principles:**
- **Stability first** - Live performances (WorldJam) must work reliably
- **KISS principle** - Keep it simple; prefer interfaces over feature bloat
- **Do One Thing Well** - Focus on core; use JSON-RPC API for extensions

**Platform support:**
- Windows 10+, macOS 10.10+, Ubuntu 20.04+, Debian 11+
- Avoid platform-specific code; test all platforms via CI
- Check Qt calls against 5.12.2 minimum

**Code quality:**
- No space before `(` in function calls; space after `if`/`for`/`while`
- All `if`/`else`/`while`/`for` bodies in braces on separate lines
- String formatting via `tr("text %1").arg(value)` for translation support

## Build Artifacts and Testing

### GitHub Actions (Autobuild)

The CI pipeline builds and tests all platforms:
- **Branches starting with `autobuild/`** - Full build+CodeQL on each push
- **Pull requests to main** - Full platform build+CodeQL
- **Tags starting with `r`** - Release builds (prerelease if suffix, release if no suffix)

To test locally before PR: use an `autobuild/` branch name to trigger full CI build.

### Verify Before Committing

```bash
# 1. Code style
make clang_format

# 2. Build (platform you're on)
make distclean && qmake && make

# 3. Check Git status - all changes committed
git status
```

## JSON-RPC Interface

Jamulus exposes control via JSON-RPC 2.0 (see `docs/JSON-RPC.md`). This allows:
- Remote automation of client/server
- Control via external tools
- CI integration for testing

Key endpoints documented in `docs/JSON-RPC.md` - useful for understanding API design patterns.

## Network Protocol

The binary protocol is documented in `docs/JAMULUS_PROTOCOL.md`:
- UDP-based, connectionless
- Custom message types for audio, control, chat
- CRC validation
- Message fragmentation for large payloads
- Acknowledgment mechanism (400ms timeout)

When modifying protocol: update docs and check backward compatibility.

## Important Notes

- **Qt version compatibility**: Code may contain Qt 4.x style in some files for legacy reasons, but maintain Qt 5.12.2 compatibility
- **Deprecated functions**: `QT_NO_DEPRECATED_WARNINGS` defined to allow building with older Qt versions
- **Audio is real-time critical**: Audio processing code must be deterministic and minimal latency
- **Translation system**: Uses Qt `.ts` files embedded at compile time (via `embed_translations`)
- **No hardcoded paths**: Use Qt functions for platform-specific paths
