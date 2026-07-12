# Homebrew tap release procedure

Homebrew formulae must fetch an immutable, checksummed source archive. This project therefore publishes the formula from a release tag rather than from a branch.

## One-time tap setup

Create a public GitHub repository named `homebrew-dgramtunneler` under the `MarkoPaul0` account. Copy the generated `Formula/dgramtunneler.rb` file into its `Formula/` directory. Users will then install with:

```sh
brew tap MarkoPaul0/dgramtunneler
brew install dgramtunneler
```

## Per-release update

1. Update `VERSION`, commit it, and push a matching Git tag such as `v0.1.0`.
2. Download the source archive and calculate its checksum:

   ```sh
   curl -L -o dgramtunneler-v0.1.0.tar.gz \
     https://github.com/MarkoPaul0/DatagramTunneler/archive/refs/tags/v0.1.0.tar.gz
   shasum -a 256 dgramtunneler-v0.1.0.tar.gz
   ```

3. Render the formula with the tag and checksum:

   ```sh
   sh packaging/homebrew/render-formula.sh v0.1.0 <sha256>
   ```

4. Copy `packaging/homebrew/Formula/dgramtunneler.rb` to the tap, then verify it:

   ```sh
   brew install --build-from-source MarkoPaul0/dgramtunneler/dgramtunneler
   brew test MarkoPaul0/dgramtunneler/dgramtunneler
   ```

The generated formula is intentionally not committed to this repository: it belongs in the separate tap and is tied to one release tag and checksum.
