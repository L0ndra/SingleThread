{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    meson
    ninja
    pkg-config
    gcc
    gdb
    valgrind
    wayland-scanner
  ];

  buildInputs = with pkgs; [
    wayland
    wayland-protocols
    wlroots_0_18
    libinput
    libxkbcommon
    pixman
    json_c
    gtk4
    gtk4-layer-shell
    xorg.libxcb
  ];
}
