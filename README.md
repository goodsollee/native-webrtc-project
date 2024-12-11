**WebRTC is a free, open software project** that provides browsers and mobile
applications with Real-Time Communications (RTC) capabilities via simple APIs.
The WebRTC components have been optimized to best serve this purpose.

**Our mission:** To enable rich, high-quality RTC applications to be
developed for the browser, mobile platforms, and IoT devices, and allow them
all to communicate via a common set of protocols.

The WebRTC initiative is a project supported by Google, Mozilla and Opera,
amongst others.

### Development

See [here][native-dev] for instructions on how to get started
developing with the native code.

[Authoritative list](native-api.md) of directories that contain the
native API header files.

### More info

 * Official web site: http://www.webrtc.org
 * Master source code repo: https://webrtc.googlesource.com/src
 * Samples and reference apps: https://github.com/webrtc
 * Mailing list: http://groups.google.com/group/discuss-webrtc
 * Continuous build: https://ci.chromium.org/p/webrtc/g/ci/console
 * [Coding style guide](g3doc/style-guide.md)
 * [Code of conduct](CODE_OF_CONDUCT.md)
 * [Reporting bugs](docs/bug-reporting.md)
 * [Documentation](g3doc/sitemap.md)

[native-dev]: https://webrtc.googlesource.com/src/+/main/docs/native-code/


-----------------------------------------------------------------------------

### (Our contribution) Testing Frameworks and Tools
#### Automated WebRTC Testbed (Native C++)
A complete testing framework for WebRTC applications built using native C++ code. This testbed enables:
- Automated peer connection testing between multiple endpoints
- Network condition simulation and testing
- Performance benchmarking and metrics collection

##### Building from Source
```bash
# Install depot tools (Linuxx)
https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html#_setting_up

# Config
gclient config --name src https://webrtc.googlesource.com/src

# Sync dependencies
gclient sync

# Generate build files
gn gen out/Default

# Build peer connection client
ninja -C out/Default peerconnection_client
```

##### Running Tests
```bash
# Basic connection test
./out/Default/peerconnection_client --server={signaling_server} --room_id={room_id} --experiment_mode=real (or emulation), --y4m_path={your_file.y4m}

##### Common Options
- `--server`: Signaling server address (default: localhost)
- `--room-id`: Room identifier (default: auto-generated)
- `--experiment_mode`: Real environment or emulation (default: real)
- `--y4m_path`: Test video path (should be y4m file) (default: square test video)

