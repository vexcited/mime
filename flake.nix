{
  description = "Running Windows programs on Linux by reimplementing Windows.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      inherit (nixpkgs) lib;
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      # the asset repositories at the commits CMakeLists.txt pins, so a bump there is all both builds need.
      cmakeSet =
        variable:
        let
          matches = map (builtins.match "set\\(${variable} ([^)]+)\\)") (
            lib.splitString "\n" (builtins.readFile ./CMakeLists.txt)
          );
        in
        lib.head (lib.findFirst (m: m != null) (throw "CMakeLists.txt sets no ${variable}") matches);
      asset =
        name:
        builtins.fetchGit {
          url = cmakeSet "MIME_${name}_REPOSITORY";
          rev = cmakeSet "MIME_${name}_COMMIT";
          allRefs = true;
        };
      icons = asset "ICONS";
      audio = asset "AUDIO";
      lastModified = builtins.foldl' lib.max 0 (
        map (source: source.lastModified or 0) [
          self
          icons
          audio
        ]
      );
      mime = pkgs.callPackage ./nix/package.nix {
        inherit icons audio;
        version =
          let
            date = self.lastModifiedDate or "19700101";
          in
          "0-unstable-${builtins.substring 0 4 date}-${builtins.substring 4 2 date}-${builtins.substring 6 2 date}";
        sourceDateEpoch = lastModified;
      };
    in
    {
      packages.${system} = {
        inherit mime;
        default = mime;
      };

      nixosModules.default = import ./nix/module.nix self;

      devShells.${system}.default = pkgs.mkShell.override { stdenv = mime.stdenv; } {
        inputsFrom = [ mime ];
        hardeningDisable = [ "all" ];
        LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath [ pkgs.vulkan-loader ];
      };

      formatter.${system} = pkgs.nixfmt;
    };
}
