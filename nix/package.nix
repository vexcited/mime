{
  lib,
  llvmPackages_latest,
  cmake,
  ninja,
  pkg-config,
  sdl3,
  wayland,
  curl,
  systemdLibs,
  vulkan-loader,
  icons,
  audio,
  version ? "unstable",
  sourceDateEpoch ? 315532800,
}:

llvmPackages_latest.stdenv.mkDerivation {
  pname = "mime";
  inherit version;

  src = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ../CMakeLists.txt
      ../data
      ../gen
      ../guest
      ../host
      ../include
      ../scripts
      ../third_party
    ];
  };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    llvmPackages_latest.llvm
    llvmPackages_latest.lld
  ];

  buildInputs = [
    sdl3
    wayland
    curl
    systemdLibs
  ];

  # the guest is built for windows targets, where the wrapper's hardening flags do not exist.
  hardeningDisable = [ "all" ];
  # and the wrapper's --gcc-toolchain is unused there.
  env.NIX_CFLAGS_COMPILE = "-Wno-unused-command-line-argument";

  cmakeBuildType = "RelWithDebInfo";
  cmakeFlags = [
    (lib.cmakeFeature "MIME_ICONS_DIR" "${icons}")
    (lib.cmakeFeature "MIME_AUDIO_DIR" "${audio}")
  ];

  # the rom's files carry the build time, so it moves with the sources instead of nix's 1980.
  env.SOURCE_DATE_EPOCH = toString sourceDateEpoch;

  installPhase = ''
    runHook preInstall
    install -Dm755 mime $out/bin/mime
    runHook postInstall
  '';

  # no wrapper: the desktop entries mime writes start /proc/self/exe.
  postFixup = ''
    patchelf --add-rpath ${lib.makeLibraryPath [ vulkan-loader ]} $out/bin/mime
  '';

  meta = {
    description = "Runs Windows programs on its own Windows-compatible kernel in a virtual machine";
    homepage = "https://code.vexcited.com/mime/mime";
    platforms = [ "x86_64-linux" ];
    mainProgram = "mime";
  };
}
