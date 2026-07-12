# Homebrew tap release procedure

Homebrew formulae must fetch an immutable, checksummed source archive. This project therefore publishes the formula from a release tag rather than from a branch.

## Official tap

The public formula is maintained in [MarkoPaul0/homebrew-dgramtunneler](https://github.com/MarkoPaul0/homebrew-dgramtunneler). Users install it with:

```sh
brew tap MarkoPaul0/dgramtunneler
brew install dgramtunneler
```

## Per-release update

1. Update `VERSION`, commit it, and publish the matching Git tag such as `v1.1.0`. The tag-triggered release workflow validates the version and creates the GitHub Release.
2. Download the source archive and calculate its checksum:

   ```sh
   curl -L -o dgramtunneler-v1.1.0.tar.gz \
     https://github.com/MarkoPaul0/DatagramTunneler/archive/refs/tags/v1.1.0.tar.gz
   shasum -a 256 dgramtunneler-v1.1.0.tar.gz
   ```

3. Render the formula with the tag and checksum:

   ```sh
   sh packaging/homebrew/render-formula.sh v1.1.0 <sha256>
   ```

4. Copy the rendered `packaging/homebrew/Formula/dgramtunneler.rb` to the tap's `Formula/` directory, commit and push it, then verify it:

   ```sh
   brew install --build-from-source MarkoPaul0/dgramtunneler/dgramtunneler
   brew test MarkoPaul0/dgramtunneler/dgramtunneler
   ```

The generated formula is intentionally not committed to this repository: it belongs in the separate tap and is tied to one release tag and checksum.
