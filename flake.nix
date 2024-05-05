{
  inputs.nixpkgs.url = "nixpkgs/nixpkgs-unstable";

  outputs = { self, nixpkgs }: {
    devShell.x86_64-linux = with nixpkgs.legacyPackages.x86_64-linux; mkShell {
      packages = [
        gcc-arm-embedded-9
        cmake
        ninja
        ccls
        python3
        openocd-rp2040
        picotool
        inetutils # For telnet
        elf2uf2-rs
      ];
    };
  };
}
