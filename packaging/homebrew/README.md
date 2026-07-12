# Homebrew tap release procedure

Homebrew formulae must fetch an immutable, checksummed source archive. This project therefore publishes the formula from a release tag rather than from a branch.

## Official tap

The public formula is maintained in [MarkoPaul0/homebrew-dgramtunneler](https://github.com/MarkoPaul0/homebrew-dgramtunneler). Users install it with:

```sh
brew tap MarkoPaul0/dgramtunneler
brew install dgramtunneler
```

## Automation setup

The main repository's `Update Homebrew Formula` workflow opens a reviewed pull request in the tap whenever a GitHub Release is published. Create a fine-grained personal access token with access limited to `MarkoPaul0/homebrew-dgramtunneler` and these permissions:

- **Contents:** read and write, to push the generated formula branch.
- **Pull requests:** read and write, to open the reviewable update PR.

Store the token as the `HOMEBREW_TAP_TOKEN` Actions secret in the `MarkoPaul0/DatagramTunneler` repository. The release remains successful if this workflow is not configured; the separate formula workflow reports the missing secret clearly. Use its manual-dispatch input with an existing tag to retry a formula update without making another release.

## Per-release update

1. Update `VERSION`, commit it, and publish the matching Git tag such as `v1.1.0`. The tag-triggered release workflow validates the version and creates the GitHub Release.
2. Review and merge the formula-update PR automatically opened in the tap.
3. Verify the published formula:

   ```sh
   brew install --build-from-source MarkoPaul0/dgramtunneler/dgramtunneler
   brew test MarkoPaul0/dgramtunneler/dgramtunneler
   ```

The generated formula is intentionally not committed to this repository: it belongs in the separate tap and is tied to one release tag and checksum.
