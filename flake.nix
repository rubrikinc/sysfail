{
  description = "SysFail dev-shell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      devShells."${system}" = {
        default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            cmake
            gcc
            gnumake
            pkg-config
          ];

          buildInputs = with pkgs; [
            boost
            eigen
            gtest
          ];

          shellHook = ''
            export CC=gcc
            export CXX=g++
          '';
        };
      };
  };
}
