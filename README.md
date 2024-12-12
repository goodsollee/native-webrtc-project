# WebRTC Native Project

WebRTC is a free, open software project that provides browsers and mobile applications with Real-Time Communications (RTC) capabilities via simple APIs. The WebRTC components have been optimized to best serve this purpose.

## Mission
To enable rich, high-quality RTC applications to be developed for the browser, mobile platforms, and IoT devices, and allow them all to communicate via a common set of protocols.

The WebRTC initiative is a project supported by Google, Mozilla, and Opera, amongst others.

## Development
See [here][native-dev] for instructions on how to get started developing with the native code.

For a detailed list of directories containing the native API header files, refer to the [authoritative list](native-api.md).

## More Info

- Official website: [WebRTC.org](http://www.webrtc.org)
- Master source code repo: [WebRTC Source](https://webrtc.googlesource.com/src)
- Samples and reference apps: [GitHub Repository](https://github.com/webrtc)
- Mailing list: [Discuss WebRTC](http://groups.google.com/group/discuss-webrtc)
- Continuous build: [Build Console](https://ci.chromium.org/p/webrtc/g/ci/console)
- [Coding style guide](g3doc/style-guide.md)
- [Code of conduct](CODE_OF_CONDUCT.md)
- [Reporting bugs](docs/bug-reporting.md)
- [Documentation](g3doc/sitemap.md)

[native-dev]: https://webrtc.googlesource.com/src/+/main/docs/native-code/

---

## Testing Frameworks and Tools

### Automated WebRTC Testbed (Native C++)
A complete testing framework for WebRTC applications built using native C++ code. This testbed enables:
- Automated peer connection testing between multiple endpoints
- Network condition simulation and testing
- Performance benchmarking and metrics collection

### Building from Source

1. Install depot tools ([Linux setup guide](https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html#_setting_up))

2. Create `.gclient` file in your root directory:
    ```python
    solutions = [
      {
        "url": "https://github.com/lgs96/native-webrtc-project.git",
        "managed": False,
        "name": "src",
        "deps_file": "DEPS",
        "custom_deps": {},
        "custom_vars": {},
      },
    ]
    target_os = ["linux"]  # Adjust based on your target platform
    ```

3. Sync dependencies:
    ```bash
    gclient sync
    ```

4. Generate build files:
    ```bash
    gn gen out/Default
    ```

5. Build peer connection client:
    ```bash
    ninja -C out/Default peerconnection_client
    ```

### Running Tests

```bash
# Basic connection test
./out/Default/peerconnection_client --server={signaling_server} --room_id={room_id} --experiment_mode=real (or emulation), --y4m_path={your_file.y4m}
```

#### Common Options
- `--server`: Signaling server address (default: localhost)
- `--room_id`: Room identifier (default: auto-generated)
- `--experiment_mode`: Real environment or emulation (default: real)
- `--y4m_path`: Test video path (should be a y4m file) (default: square test video)
