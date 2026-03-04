{
  description = "SingleThread - Task-Centric Wayland Compositor for ADHD-Friendly Computing";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        singlethread = pkgs.stdenv.mkDerivation {
          pname = "singlethread";
          version = "0.1.0";

          src = ./.;

          nativeBuildInputs = with pkgs; [
            meson
            ninja
            pkg-config
            wayland-scanner
          ];

          buildInputs = with pkgs; [
            wayland
            wayland-protocols
            wlroots_0_18
            libinput
            xkbcommon
            pixman
            json_c
            gtk4
            gtk4-layer-shell
            libxcb
            xcb-util-errors
            libnotify
          ];

          mesonFlags = [
            "-Dxwayland=enabled"
            "-Dshell=enabled"
          ];

          meta = with pkgs.lib; {
            description = "Task-centric Wayland compositor with ADHD-friendly features";
            homepage = "https://github.com/L0ndra/SingleThread";
            license = licenses.mit;
            platforms = platforms.linux;
            mainProgram = "singlethread";
          };
        };
      in
      {
        packages = {
          default = singlethread;
          singlethread = singlethread;
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ singlethread ];
          packages = with pkgs; [
            gdb
            valgrind
            wayland-utils
            wlr-randr
          ];
        };
      }
    ) // {
      # NixOS module
      nixosModules.default = import ./packaging/nixos/module.nix self;

      # Overlay for use in other flakes
      overlays.default = final: prev: {
        singlethread = self.packages.${final.system}.singlethread;
      };
    };
}
