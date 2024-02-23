{
  #
  # Install nix: 
  #  `curl --proto '=https' --tlsv1.2 -sSf -L https://install.determinate.systems/nix | sh -s -- install`
  # Enter dev shell: 
  #  `nix develop`
  # Update flake lock:
  #  `nix flake update --commit-lock-file`
  #
  description = "proof-of-concept implementation of the design proposed in [P2300](http://wg21.link/p2300)";

  inputs = {
    flake-parts.url = "github:hercules-ci/flake-parts";
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    pre-commit-hooks.url = "github:hercules-ci/pre-commit-hooks.nix/flakeModule";
  };

  outputs = inputs@{ self, nixpkgs, flake-parts, pre-commit-hooks, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      imports = [
        pre-commit-hooks.flakeModule
      ];

      # This is the list of architectures that work with this project
      systems = [ "x86_64-linux" "aarch64-linux" "aarch64-darwin" "x86_64-darwin" ];
      perSystem = { config, self', inputs', pkgs, system, ... }: rec {
        # Per-system attributes can be defined here. The self' and inputs'
        # module parameters provide easy access to attributes of the same
        # system.

        # The `callPackage` automatically fills the parameters of the function
        # in package.nix with what's inside the `pkgs` attribute.
        packages = {
          default = pkgs.callPackage ./package.nix { };
          clang = pkgs.callPackage ./package.nix { stdenv = pkgs.clang17Stdenv; };
        };

        # The `config` variable contains our own outputs, so we can reference
        # neighbor attributes like the package we just defined one line earlier.
        devShells = {
          default = packages.default;
          clang = packages.clang;
        };

        pre-commit.settings.hooks = {
          nixpkgs-fmt.enable = true;
          clang-format = {
            enable = false; # true;
            types_or = [ "c" "c++" ];
          };
        };
      };
    };

}
