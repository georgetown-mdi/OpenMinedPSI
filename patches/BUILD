# Patches applied to external modules via single_version_override in MODULE.bazel.
#
# rules_swift_windows_toolchain.patch fixes rules_swift 2.4.0 (pulled in
# transitively by protobuf, and unused here): its Windows swift-toolchain
# autoconfiguration emits an invalid BUILD file -- target_compatible_with =
# APPLE_PLATFORMS_CONSTRAINTS[arch], names that are only defined on the Apple
# path -- which breaks all toolchain resolution on Windows, including the pure
# C++ addon build.
#
# boringssl_openssl_thread_stop.patch adds the OpenSSL-compatible
# OPENSSL_thread_stop() to BoringSSL, which has no public way to drain its
# per-thread state (error queue, RAND) or unregister the thread-exit destructor
# that frees it. The napi binding calls it from an env cleanup hook so a
# worker_threads teardown -- where Node dlcloses the addon before the OS thread
# exits -- does not fire that destructor against unmapped code (SIGSEGV). See
# private_set_intersection/napi/psi_napi.cpp.
exports_files([
    "boringssl_openssl_thread_stop.patch",
    "rules_swift_windows_toolchain.patch",
])
