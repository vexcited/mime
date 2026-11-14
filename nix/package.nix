{
  lib,
  stdenv,
  fetchurl,
  autoPatchelfHook,
  sdl3,
  wayland,
  libxkbcommon,
  pipewire,
  curl,
  systemdLibs,
  vulkan-loader,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "mime";
  version = "0.0.2";

  src = fetchurl {
    url = "https://github.com/vexcited/mime/releases/download/${finalAttrs.version}/mime-x86_64-linux.tar.gz";
    hash = "sha256-srabRWKpQfaKqcu5cjgfWMxYxUhCssa+Ed8n3c41BTQ=";
  };
  sourceRoot = ".";

  nativeBuildInputs = [ autoPatchelfHook ];

  buildInputs = [
    # the release above still links it.
    sdl3
    wayland
    libxkbcommon
    curl
    systemdLibs
    (lib.getLib stdenv.cc.cc)
  ];

  runtimeDependencies = [
    vulkan-loader
    pipewire
  ];

  installPhase = ''
    runHook preInstall
    install -Dm755 mime $out/bin/mime
    runHook postInstall
  '';

  meta = {
    description = "Running Windows programs on Linux by reimplementing Windows";
    homepage = "https://code.vexcited.com/mime/mime";
    platforms = [ "x86_64-linux" ];
    sourceProvenance = [ lib.sourceTypes.binaryNativeCode ];
    mainProgram = "mime";
  };
})
