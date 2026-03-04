# Example NixOS configuration for a SingleThread ADHD-friendly desktop
#
# Usage:
#   1. Add the SingleThread flake to your flake inputs
#   2. Import this module or use it as reference
#   3. Run: sudo nixos-rebuild switch
#
# This gives you an immutable OS base with reproducible builds,
# perfect for an ADHD-friendly setup where the system "just works"
# and you never have to debug broken packages.

{ config, pkgs, ... }:

{
  # Import the SingleThread NixOS module
  # (In your flake.nix, add singlethread as an input and import its module)

  programs.singlethread = {
    enable = true;
    xwayland = true;

    focusMode = {
      breakInterval = 25;     # Pomodoro: 25 min focus
      breakDuration = 5;      # 5 min break
      suppressNotifications = true;
    };

    extraConfig = ''
      [keybindings]
      # Quick launch favorites
      "Super+b" = "spawn:firefox"
      "Super+e" = "spawn:nautilus"
      "Super+Shift+Return" = "spawn:foot"

      # ADHD shortcuts
      "Super+F12" = "focus:toggle"
      "Super+Shift+F12" = "focus:start:25"
      "Super+n" = "quicknote:open"
    '';
  };

  # ─── Core system ──────────────────────────────────────────────
  boot.loader.systemd-boot.enable = true;
  boot.loader.efi.canTouchEfiVariables = true;

  # ─── Networking ───────────────────────────────────────────────
  networking.networkmanager.enable = true;

  # ─── Sound (PipeWire for low-latency audio) ──────────────────
  security.rtkit.enable = true;
  services.pipewire = {
    enable = true;
    alsa.enable = true;
    alsa.support32Bit = true;
    pulse.enable = true;
  };

  # ─── Fonts (essential for the beautiful UI) ──────────────────
  fonts = {
    packages = with pkgs; [
      inter                    # Primary UI font
      jetbrains-mono           # Monospace font
      noto-fonts
      noto-fonts-cjk-sans
      noto-fonts-emoji
      font-awesome             # Icons
      (nerdfonts.override { fonts = [ "JetBrainsMono" ]; })
    ];
    fontconfig.defaultFonts = {
      sansSerif = [ "Inter" "Noto Sans" ];
      monospace = [ "JetBrains Mono" ];
    };
  };

  # ─── Essential packages for daily use ─────────────────────────
  environment.systemPackages = with pkgs; [
    # Terminal
    foot                       # Fast Wayland-native terminal

    # File management
    nautilus
    file-roller

    # Web
    firefox-wayland

    # Media
    mpv
    imv                        # Image viewer for Wayland

    # Utilities
    wl-clipboard               # Clipboard for Wayland
    grim                       # Screenshot
    slurp                      # Screen region selector
    mako                       # Notification daemon (fallback)
    brightnessctl              # Screen brightness
    pavucontrol                # Audio control

    # Development
    git
    neovim

    # System monitoring
    btop
  ];

  # ─── Auto-login (no friction) ────────────────────────────────
  # Uncomment to auto-login (reduces friction for ADHD users)
  # services.greetd = {
  #   enable = true;
  #   settings = {
  #     default_session = {
  #       command = "singlethread";
  #       user = "your-username";
  #     };
  #   };
  # };

  # ─── Locale ──────────────────────────────────────────────────
  time.timeZone = "America/New_York";  # Change to your timezone
  i18n.defaultLocale = "en_US.UTF-8";

  system.stateVersion = "24.11";
}
