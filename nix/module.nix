self:
{
  config,
  lib,
  pkgs,
  ...
}:

let
  cfg = config.programs.mime;
in
{
  options.programs.mime = {
    enable = lib.mkEnableOption "Mime";
    package = lib.mkOption {
      type = lib.types.package;
      default = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
      defaultText = lib.literalExpression "mime.packages.\${system}.default";
      description = "The Mime package to use.";
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ];
    # the vulkan drivers in /run/opengl-driver.
    hardware.graphics.enable = true;
  };
}
