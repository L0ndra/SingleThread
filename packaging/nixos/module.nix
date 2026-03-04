# NixOS module for SingleThread compositor
# Usage in configuration.nix:
#   imports = [ singlethread.nixosModules.default ];
#   programs.singlethread.enable = true;

flake:
{ config, lib, pkgs, ... }:

let
  cfg = config.programs.singlethread;
  singlethread = flake.packages.${pkgs.system}.singlethread;
in
{
  options.programs.singlethread = {
    enable = lib.mkEnableOption "SingleThread task-centric Wayland compositor";

    package = lib.mkOption {
      type = lib.types.package;
      default = singlethread;
      description = "The SingleThread package to use.";
    };

    xwayland = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = "Whether to enable XWayland support.";
    };

    extraConfig = lib.mkOption {
      type = lib.types.lines;
      default = "";
      description = ''
        Extra configuration to append to the SingleThread config file.
        Written to ~/.config/singlethread/config.toml.
      '';
    };

    focusMode = {
      breakInterval = lib.mkOption {
        type = lib.types.int;
        default = 25;
        description = "Break reminder interval in minutes (Pomodoro-style).";
      };

      breakDuration = lib.mkOption {
        type = lib.types.int;
        default = 5;
        description = "Break duration in minutes.";
      };

      suppressNotifications = lib.mkOption {
        type = lib.types.bool;
        default = true;
        description = "Suppress notifications during focus mode.";
      };
    };
  };

  config = lib.mkIf cfg.enable {
    # Install the package
    environment.systemPackages = [ cfg.package ];

    # Add wayland-session entry for display managers
    services.displayManager.sessionPackages = [ cfg.package ];

    # Required system services
    security.polkit.enable = true;
    hardware.graphics.enable = true;

    # XWayland support
    programs.xwayland.enable = lib.mkDefault cfg.xwayland;

    # Seatd for unprivileged access to input/graphics
    services.seatd.enable = lib.mkDefault true;

    # Default fonts for the UI shell
    fonts.packages = with pkgs; [
      inter
      jetbrains-mono
      noto-fonts
      noto-fonts-emoji
    ];

    # Portal support for screen sharing etc
    xdg.portal = {
      enable = true;
      wlr.enable = true;
      extraPortals = [ pkgs.xdg-desktop-portal-gtk ];
    };

    # Environment variables
    environment.sessionVariables = {
      XDG_SESSION_TYPE = "wayland";
      XDG_CURRENT_DESKTOP = "singlethread";
      MOZ_ENABLE_WAYLAND = "1";
      QT_QPA_PLATFORM = "wayland";
      SDL_VIDEODRIVER = "wayland";
      _JAVA_AWT_WM_NONREPARENTING = "1";
    };

    # Default config file
    environment.etc."singlethread/config.toml.default" = {
      text = ''
        # SingleThread - Default NixOS Configuration
        # Override in ~/.config/singlethread/config.toml

        [general]
        default_layout = "master-stack"
        master_count = 1
        master_ratio = 0.55
        gaps_inner = 6
        gaps_outer = 6
        focus_follows_mouse = true

        [tasks]
        create_default = true
        default_name = "General"
        restore_focus = true

        [focus]
        break_reminders = true
        break_interval = ${toString cfg.focusMode.breakInterval}
        break_duration = ${toString cfg.focusMode.breakDuration}
        suppress_notifications = ${lib.boolToString cfg.focusMode.suppressNotifications}

        [appearance]
        theme = "tokyo-night"
        border_width = 2

        ${cfg.extraConfig}
      '';
    };
  };
}
