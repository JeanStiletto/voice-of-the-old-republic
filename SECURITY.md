# Security Policy

## Scope

Voice of the Old Republic is a local accessibility mod for Star Wars: Knights of
the Old Republic 1. It:

- Injects a DLL into the game process to read the game's in-memory UI and world
  state and expose it to a screen reader.
- Sends no telemetry, no user data, and no logs anywhere. Logs are written only
  to local files on your own machine.
- Makes exactly one kind of outbound network call: a request to the GitHub
  Releases API to check whether a newer version of the mod is available.

There is no server component, no account system, and no credentials handled by
the mod.

## Reporting a vulnerability

If you find a security issue, please report it privately before posting
publicly, so it can be fixed before bad actors notice:

- Email: fabian@nordwiesen30.de

Use "Voice of the Old Republic security" in the subject. You can expect a first
reply within a few days. I will credit you in the release notes when the fix
ships, unless you prefer to stay anonymous.

## Realistic attack surface

The one place where a security researcher might find something worth reporting is
the **auto-update flow** (F5 from the main menu / a background check when the
main menu loads):

1. The mod queries the GitHub Releases API over HTTPS for the latest published
   version.
2. If a newer version is available, it downloads the installer over HTTPS.
3. It writes a small batch script to the temp folder that waits for the game to
   exit, launches the installer (which requests UAC elevation to deploy files),
   and relaunches the game.

If any of those steps could be tricked into downloading or executing a file that
isn't the legitimate release — a spoofed release response, a hijacked download
URL, a writable temp path an attacker could pre-seed — that would be a real
issue and I want to hear about it.

## Things that are explicitly out of scope

- **Data exfiltration** — the mod contains no code that sends your data
  anywhere. The only outbound request is the version check described above.
- **Anti-cheat / account bypass** — the mod only reads and narrates state the
  game has already computed for a single-player, offline RPG. There is no
  multiplayer or account system to attack.
- **Windows SmartScreen / "unknown publisher" warnings** on the unsigned
  installer and DLL are a code-signing cost issue, not a security
  vulnerability. Code-signing certificates cost hundreds of euros per year,
  which isn't realistic for a free accessibility project. Each release publishes
  SHA-256 checksums so you can verify the files you downloaded match the ones
  published on GitHub:
  - PowerShell: `Get-FileHash <filename> -Algorithm SHA256`
  - Command Prompt: `certutil -hashfile <filename> SHA256`
