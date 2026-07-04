# Patches applied to external modules via single_version_override in MODULE.bazel.
#
# rules_swift_windows_toolchain.patch fixes rules_swift 2.4.0 (pulled in
# transitively by protobuf, and unused here): its Windows swift-toolchain
# autoconfiguration emits an invalid BUILD file -- target_compatible_with =
# APPLE_PLATFORMS_CONSTRAINTS[arch], names that are only defined on the Apple
# path -- which breaks all toolchain resolution on Windows, including the pure
# C++ addon build.
exports_files(["rules_swift_windows_toolchain.patch"])
