# Tarinoi for Unreal Engine

Tarinoi dialogue for Unreal Engine. Sync authored dialogue content from the Tarinoi service
into a local SQLite database, evaluate authored conditions and functions against your game's
own code (C++ or Blueprint), and play dialogue back through a small event-based runtime.

Requires **Unreal Engine 5.8** and a **C++ project**: the plugin ships as source, and the
bindings it generates for your content are C++ classes you can extend in C++ or Blueprint.

> **Status: early development.** Version 0.2.0. The API is not yet stable and will change
> before 1.0. Watch [`CHANGELOG.md`](Tarinoi/CHANGELOG.md).

## Installation

The plugin is the [`Tarinoi/`](Tarinoi) folder. Put it in your project's `Plugins` folder,
either by copying it:

```
YourGame/
  Plugins/
    Tarinoi/
      Tarinoi.uplugin
      Source/...
```

or by cloning this repository there, which lets you pull updates:

```
cd YourGame/Plugins
git clone https://github.com/tarinoi/tarinoi-unreal-plugin.git
```

Then add `"Tarinoi"` to your game module's dependencies in `YourGame.Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Tarinoi" });
```

and rebuild. SQLite comes with the engine (the SQLiteCore plugin), so there is nothing else
to install.

<details>
<summary>Pinning to a specific version</summary>

A clone tracks the default branch. Check out a tag (`git checkout v0.2.0`) or commit to pin it; worth doing while the
plugin is pre-1.0 and the API is still moving.

</details>

## Getting started

**https://tarinoi.app/docs/plugins/unreal.html**: the full guide to configuration,
bindings, events, triggers, shipping a build, and troubleshooting.

In short: set the API path under **Project Settings > Plugins > Tarinoi**, set your token with
**Tools > Tarinoi > Set API Token**, **Sync**, **Regenerate Bindings**, rebuild, then set a
level's GameMode Override to **TarinoiQuickstartGameMode** and press Play.

Related:

- [What the plugins do and don't do](https://tarinoi.app/docs/plugins/)
- [Writing your own integration](https://tarinoi.app/docs/plugins/writing_your_own.html): the engine-agnostic data contract
- [Adapting a plugin](https://tarinoi.app/docs/plugins/adapting.html): forking, porting, and the traps we hit

## Tests

The plugin's automation tests run from **Tools > Session Frontend > Automation** (filter
`Tarinoi`), or headless:

```
UnrealEditor YourGame.uproject -ExecCmds="Automation RunTests Tarinoi" -TestExit="Automation Test Queue Empty" -unattended -nullrhi
```

## License

MIT; see [`LICENSE.md`](LICENSE.md). These plugins are reference implementations, meant to be
built on, modified, and incorporated into your own work.
