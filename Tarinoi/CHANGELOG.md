# Changelog

All notable changes to this plugin are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] - 2026-09-28

First public release, numbered to match the Godot and Unity plugins.

### Added
- Sync from the Tarinoi documents API into a local SQLite database (the engine's SQLiteCore),
  incrementally, with the two-layer committed/uncommitted merge and a data-format version gate.
  The API token is stored outside the project, owner-readable only.
- `UTarinoiRuntime`: plays dialogue with the same traversal rules as in-app playback, raising
  Blueprint-assignable events for lines, choices, errors and the end of a dialogue.
  `UTarinoiSubsystem` owns and configures it per game instance.
- The Tarinoi expression language, `FTarinoiValue`, and Blueprintable binding collections for
  functions, variables and entities, keyed on case-sensitive collection identifiers.
- Codegen: typed C++ binding classes for the synced declarations (BlueprintNativeEvent per
  function, a UPROPERTY per variable), key constants for lists and entities, and a scaffold of
  the core functions (`Fn.tarinoi.*`) reference implementation, written once for you to edit.
  A validator reports what regenerating would change.
- Editor tooling: Tools > Tarinoi (Sync, Regenerate/Check Bindings, Export Snapshot, Clear Local
  Content, Set API Token), an API token row in Project Settings, the Tarinoi message log, and a
  `-run=Tarinoi` commandlet.
- Offline mode, playing a snapshot exported to `Content/Tarinoi` and staged into the build.
- A quickstart: `ATarinoiQuickstartGameMode` with a C++-built UMG interface (entry-point picker
  and dialogue transcript), `UTarinoiDialogueTrigger` and `ATarinoiDialogueVolume`, and
  `Tarinoi.*` console commands.
- Automation tests for all of the above, including a compiled golden fixture of generated code.
