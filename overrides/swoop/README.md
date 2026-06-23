# Swoop accelerator-pad widening (optional, NOT auto-shipped)

Reproducible record of the manual Override experiment that widened the Tatooine
swoop accelerator pads. Kept here so the change is understood and reversible — it
is **not** bundled by the installer or the `.kpatch`; it is applied/removed by
hand in the game's `Override/` folder.

## What it is — exactly

`m17mg.are.widened` is the vanilla Tatooine swoop area (`m17mg.are`, also present
in `modules/tat_m17mg.rim`) with **one** change:

- Every accelerator pad's hit size — the `MiniGame → Enemies[*] → Sphere_Radius`
  float — raised from vanilla **2.0** to **5.0**. 30 pads, 30 floats, nothing
  else changed (verified by a full `gff2xml` diff).

Effect in-engine: the accelpad hit test is a swept sphere of
`player.Sphere_Radius + pad.Sphere_Radius`. Player is 3.0, so this widens the
catch window from vanilla **3.0 + 2.0 = 5.0u** to **3.0 + 5.0 = 8.0u**.

## Apply (re-add the widening)

Copy `m17mg.are.widened` to the game `Override/` as `m17mg.are`:

    cp overrides/swoop/m17mg.are.widened "<install>/Override/m17mg.are"

## Remove (back to vanilla)

The vanilla `.are` lives in `modules/tat_m17mg.rim`, so either delete the Override
file, or — safer, because it can't depend on rim fallback — overwrite it with the
vanilla copy:

    cp build/swoop-rim/m17mg.are "<install>/Override/m17mg.are"   # vanilla 2.0 pads

(`build/swoop-rim/m17mg.are` is the extracted vanilla; regenerate with
`unrim e modules/tat_m17mg.rim` if missing.)

## Regenerate the widened file from scratch

1. `gff2xml` vanilla `m17mg.are` → XML.
2. Replace every `<float label="Sphere_Radius">2.000000</float>` inside the
   `Enemies` list with `5.000000`.
3. `xml2gff` back to `m17mg.are`.

## Status

As of 2026-06-23 the widening is **removed** (game uses vanilla 5.0u combined
radius); the magnet aim-assist is the single source of catch forgiveness.
