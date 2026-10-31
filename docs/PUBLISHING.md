# Public release preparation

Target: **v1.3.0**

Title: **SwitchNOW 1.3.0 — Streaming & Experience Update**

Maintainer and NACP author: **Blade-Punisher**.

Release assets: SwitchNOW-1.3.0.nro, SwitchNOW-1.3.0.zip, SwitchNOW-1.3.0-source.zip and SwitchNOW-1.3.0-SHA256SUMS.txt.

Use RELEASE_NOTES_1.3.0_EN.md / RELEASE_NOTES_1.3.0_RU.md for the release body. No credentials or signing secrets are needed. Public protocol client identifiers, upstream copyright notices and trust certificates are required functionality/attribution and are not account secrets. Synthetic test credentials use example domains.

The clean snapshot has no .git directory or history. Publishing it to an existing repository should use a reviewed commit; do not rewrite or force-push the existing repository history. These preparation scripts do not commit, push, tag or publish.

Host tests and native compilation validate software/build behavior; physical Switch testing remains separate. This release changes R73 metadata/branding and public packaging, without retuning its rendering or streaming algorithms.
