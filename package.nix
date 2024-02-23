# file: package.nix

# The items in the curly brackets are function parameters as this is a Nix
# function that accepts dependency inputs and returns a new package
# description
{ lib
, stdenv
, cmake
, gtest
, ninja
, nixpkgs-fmt
  # Builds are by default Release, but the caller can set "Debug" or "RelWithDebugInfo"
, buildType ? "Release"
  # Tests are by default enabled, but the caller can disable them
, enableTests ? true
  # Examples are by default enabled, but the caller can disable them
, enableExamples ? true
  # Documents are by default disabled, but the caller can enable them
, enableDocuments ? false
  # Extra type checks are by default disabled, but the caller can enable them
, enableExtraTypeChecks ? false
  # TBB is by default disabled, but the caller can enable 
, enableTBB ? false
  # IO_URING is by default detected, but the caller can force-enable 
, enableIoUring ? false
  # CUDA is by default detected, but the caller can force-enable 
, enableCUDA ? false
}:

# stdenv.mkDerivation now accepts a list of named parameters that describe
# the package itself.

stdenv.mkDerivation {
  pname = "stdexec";
  version = "0.0.99";

  packages = [
    # C++ Compiler is already part of stdenv
    stdenv
    cmake
    gtest
    ninja
    nixpkgs-fmt
  ];

  # good source filtering is important for caching of builds.
  # It's easier when subprojects have their own distinct subfolders.
  src = lib.sourceByRegex ./. [
    "^include.*"
    "^test.*"
    "^examples.*"
    "CMakeLists.txt"
  ];

  # Setting up the environment variables you need during
  # development.
  shellHook =
    let
      icon = ""; #"f121";
    in
    ''
      export PS1="$(echo -e '\u${icon}') {\[$(tput sgr0)\]\[\033[38;5;228m\]\w\[$(tput sgr0)\]\[\033[38;5;15m\]} \\$ \[$(tput sgr0)\]"
    '';

  # We now list the dependencies similar to the devShell before.
  # Distinguishing between `nativeBuildInputs` (runnable on the host
  # at compile time) and normal `buildInputs` (runnable on target
  # platform at run time) is an important preparation for cross-compilation.
  nativeBuildInputs = [ cmake ninja ];
  buildInputs = [ ];
  checkInputs = [ gtest ];

  # Instruct the build process to run tests.
  # The generic builder script of `mkDerivation` handles all the default
  # command lines of several build systems, so it knows how to run our tests.
  doCheck = enableTests;

  cmakeBuildType = buildType;
  # Our CMakeLists.txt has several options
  cmakeFlags = [ ]
    ++ lib.optional (!enableTests) "-DSTDEXEC_BUILD_TESTS=off"
    ++ lib.optional (!enableExamples) "-DSTDEXEC_BUILD_EXAMPLES=off"
    ++ lib.optional (enableDocuments) "-DSTDEXEC_BUILD_DOCS=on"
    ++ lib.optional (enableExtraTypeChecks) "-DSTDEXEC_ENABLE_EXTRA_TYPE_CHECKING=on"
    ++ lib.optional (enableTBB) "-DSTDEXEC_ENABLE_TBB=on"
    ++ lib.optional (enableIoUring) "-DSTDEXEC_ENABLE_IO_URING_TESTS=on"
    ++ lib.optional (enableCUDA) "-DSTDEXEC_ENABLE_CUDA=on";

}
