{ config, pkgs, ... }:

{
  imports = [ ./hardware-configuration.nix ];

  # Boot
  boot.loader.systemd-boot.enable = true;
  boot.loader.efi.canTouchEfiVariables = true;

  # Networking
  networking.hostName = "singlethread-dev";
  networking.networkmanager.enable = true;

  # User account
  users.users.dev = {
    isNormalUser = true;
    extraGroups = [ "wheel" "video" "input" "seat" ];
    initialPassword = "changeme";
  };

  # Enable seat management (required for Wayland compositors)
  services.seatd.enable = true;
  security.polkit.enable = true;
  hardware.graphics.enable = true;

  # Development tools
  environment.systemPackages = with pkgs; [
    git
    neovim
    foot              # Wayland terminal
    firefox
    meson
    ninja
    pkg-config
    gcc
    gdb
    valgrind

    # SingleThread build dependencies (include .dev outputs for headers/pkg-config)
    wayland
    wayland.dev
    wayland-protocols
    wayland-scanner
    wlroots_0_18
    wlroots_0_18.dev
    libinput
    libinput.dev
    libxkbcommon
    libxkbcommon.dev
    pixman
    pixman.dev
    json_c
    json_c.dev
    gtk4
    gtk4.dev
    gtk4-layer-shell
    gtk4-layer-shell.dev
    xorg.libxcb
    xorg.libxcb.dev

    # Wayland utilities
    wl-clipboard
    grim
    slurp
    wlr-randr

    # Clipboard sharing with UTM/SPICE
    spice-vdagent
  ];

  # Fonts for the UI
  fonts.packages = with pkgs; [
    inter
    jetbrains-mono
    noto-fonts
    noto-fonts-color-emoji
    font-awesome
  ];
  fonts.fontconfig.defaultFonts = {
    sansSerif = [ "Inter" "Noto Sans" ];
    monospace = [ "JetBrains Mono" ];
  };

  # Portal support
  xdg.portal = {
    enable = true;
    wlr.enable = true;
    extraPortals = [ pkgs.xdg-desktop-portal-gtk ];
  };

  # Wayland environment
  environment.sessionVariables = {
    XDG_SESSION_TYPE = "wayland";
    MOZ_ENABLE_WAYLAND = "1";
    QT_QPA_PLATFORM = "wayland";
  };

  # Auto-login to TTY (optional, reduces friction)
  services.getty.autologinUser = "dev";

  system.stateVersion = "24.11";
}
