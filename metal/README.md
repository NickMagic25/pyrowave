
This is an AI assisted port of Pyrowave to Metal on Apple platforms.

This has no Granite code, using native Metal and converting the shaders to MSL.

This is covered by the standard MIT license available in ../LICENSE.

Upstream note: This port may be removed entirely at some point if some effort is spend on a more direct implementation.
This code duplication and AI use is a pragmatic compromise since a quick and dirty port was needed by clients and upstream did not have a setup to do a "proper" port. Upstream is not able to maintain or support this port beyond the absolute minimum and is provided here for convenience for said clients.

Port notes:
* The API has been simplified, removing Vulkanisms and using Metal objects for GPU decode.
* You can set the environment variable PYROWAVE_PRECISION to 0, 1, or 2, to make the same precision/speed tradeoffs as the main library.
* Using FP32 math and FP16 storage in the shaders ended up being the fastest and most accurate combination on Apple hardware.
* This implementation is roughly twice as fast as the Vulkan implementation running on KosmicKrisp on macOS

Decoder upload admission is bounded to four outstanding command buffers. When
all upload slots are occupied, decoding returns `PYROWAVE_ERROR_BUSY` without
waiting for GPU completion or consuming the parsed frame. Commit pending work
and retry after it completes. A caller can keep the wait on a control worker,
away from packet receive and display callbacks.

To run the Metal admission regression on supported Apple hardware:

```sh
cmake -S metal -B build-metal -DPYROWAVE_METAL_BUILD_TESTS=ON
cmake --build build-metal
ctest --test-dir build-metal --output-on-failure
```

The test deliberately leaves four decode commands uncommitted, checks that the
fifth returns busy with its frame intact, then commits and verifies successful
retry and GPU output. It also checks invalid partial-frame sideband values.
