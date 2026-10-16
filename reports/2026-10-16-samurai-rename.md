# SAMURAI rename verification — 2026-10-16

## Delivered

- Game and editor application entries are `SAMURAI` and `Unreal Editor — SAMURAI`.
  Both installed entries pass `desktop-file-validate`; Gio resolves their names
  and icons correctly. The matching old entries for this checkout were removed.
- The text-free custom icon is preserved byte-for-byte as
  `assets/branding/samurai.png`.
- Project metadata, the game-window title configuration, the title-screen label,
  active branding documentation, and the portable archive filename use SAMURAI.
- The story defines Ren as Iori's samurai retainer and Masato as an officer of
  the same household. The Drowned Road remains the campaign name.

## Checks

The launcher migration passed isolated clean installs, repeated installs, both
legacy entry migrations, preservation of another checkout's entries, missing-icon
handling, and desktop-file validation. Shell syntax, Python syntax, project JSON,
and whitespace checks passed.

The Unreal Development editor build compiled the new HUD title and linked
`libUnrealEditor-Ronin.so` successfully. An initial build found a template
deduction error in the in-progress character test: `TestNotNull` received a
`TObjectPtr<UStaticMesh>` from `GetStaticMesh()`. Passing `.Get()` fixed the error.
That one-line correction remains with the other character-test edits in the
working tree; those unfinished changes are not included in the branding commit.

## Runtime limitation

A separate NullRHI Python commandlet intended to inspect the loaded project and
window names crashed before reaching its script. The stack runs through
`URoninAnimInstance` construction, loading a retargeted `UAnimSequence`,
`USkeleton::AccumulateCurveMetaData`, and `UEditorEngine::BeginTransaction`.
The animation import and retarget integration are still in progress.

This establishes successful compilation and installed launcher naming, not a
successful gameplay startup or a rendered title-screen inspection. Complete that
runtime check after the active animation-loading work is stable. No gameplay
behavior was changed for the rename.
