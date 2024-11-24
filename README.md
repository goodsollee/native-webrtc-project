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
./out/Default/peerconnection_client --server=your.server.com

# Example outputs
Server: your.server.com
Room: test_room_1
Peers: 2
[INFO] Starting peer connection client...
[INFO] Connected to signaling server
[INFO] Joined room test_room_1
[INFO] Establishing peer connection...
[INFO] Connection established
```

##### Common Options
- `--server`: Signaling server address (default: localhost)
- `--port`: Server port (default: 8888)
- `--name`: Client name (default: auto-generated)
- `--room-id`: Room identifier (default: auto-generated)

