{
  description = "Running Windows programs on Linux by reimplementing Windows.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      llvm = pkgs.llvmPackages_latest;
      mime = pkgs.callPackage ./nix/package.nix { };
    in
    {
      packages.${system} = {
        inherit mime;
        default = mime;
      };

      nixosModules.default = import ./nix/module.nix self;

      devShells.${system}.default = pkgs.mkShell.override { stdenv = llvm.stdenv; } {
        nativeBuildInputs = [
          pkgs.cmake
          pkgs.ninja
          pkgs.pkg-config
          llvm.llvm
          llvm.lld
        ];
        buildInputs = [
          pkgs.sdl3
          pkgs.wayland
          pkgs.curl
          pkgs.systemdLibs
        ];
        hardeningDisable = [ "all" ];
        NIX_CFLAGS_COMPILE = "-Wno-unused-command-line-argument";
        LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath [ pkgs.vulkan-loader ];
      };

      formatter.${system} = pkgs.nixfmt;
    };
}
