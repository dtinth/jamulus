# Jamulus Development Guidelines for AI Assistants

## Build/Test Commands
- Build: `qmake Jamulus.pro && make` (standard) or `qmake "CONFIG+=headless serveronly" && make` (server-only)
- Format code: `make clang_format` 
- Test: No built-in test suite, implement manual testing for your changes

## Build Configurations
- Client: Default build
- Server only: `CONFIG+=serveronly`
- Headless mode: `CONFIG+=headless`
- Disable JSON-RPC: `CONFIG+=nojsonrpc`
- Use JACK: `CONFIG+=jackonwindows` or `CONFIG+=jackonmac`
- Version check disabled: `CONFIG+=disable_version_check`

## Code Style
- Use clang-format before committing - run `make clang_format` (version defined in GitHub CI)
- Tab size = 4 spaces, insert spaces not tabs
- Braces on separate lines for if/else/while/for blocks
- Use QString substitutions: `QString(tr("Hello, %1")).arg(getName())` not string concatenation
- Follow conditional compilation patterns with defines like HEADLESS, SERVER_ONLY, NO_JSON_RPC

## Project Philosophy
- Stability is the highest priority - avoid changes that risk instability
- Follow KISS principle - avoid adding features that can be done externally
- Error handling should be thorough and robust
- C++11 compatibility required throughout the codebase
- Minimum Qt version: 5.12.2
- Update ChangeLog for new features or bug fixes

## PR Process
- Include `CHANGELOG: Brief description of the change` in PR descriptions
- First-time contributors should add their name to contributors list in `src/util.cpp`
- PRs require at least two reviews by main developers before merging