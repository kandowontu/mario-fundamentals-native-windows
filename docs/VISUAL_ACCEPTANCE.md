# Visual acceptance procedure

Automated fidelity is necessary but does not authorize publication. The release remains withdrawn
until the current executable is reviewed by a person at normal play speed and the corrected scenes
are explicitly accepted.

Run the full hidden gate first, then generate the lossless review packet:

```powershell
.\tools\build_release.ps1
python .\tools\build_visual_acceptance_packet.py
```

Open `work/qa/visual-acceptance/index.html`. Its fifteen sheets cover both shells, every sampled
game-intro instant, every deterministic opening state, representative gameplay/outcome states, and
all Yacht actor/cup/dice/score frames. Together they expose every one of the 477 pinned corpus frames
at full resolution, and each local checkbox persists in the browser without sending data anywhere.
The builder fails if any frame is absent or duplicated, if either corpus differs from its pinned
digest, or if the candidate does not match `dist/SHA256SUMS.txt`. Static-sheet and live-test
checkmarks are namespaced by that candidate SHA-256, so a rebuilt executable cannot inherit an
older candidate's acceptance.

The live executable must additionally be checked for the timing/audio aspects a static packet
cannot prove perceptually:

1. Both startup sequences play their correct sounds/music without a hang or orphaned background
   process.
2. Macintosh title/easel clicks and the selected Yacht-on-water intro skip immediately; Yacht's
   later visible-scorecard “Good luck / I go first” opening remains input-locked like vanilla.
3. Menus and gameplay remain spatially stable in windowed and `Alt+Enter` fullscreen modes, with
   no black repaint flicker.
4. All five intros in both editions remain aligned and solid, with no duplicated heads or latent
   board/menu layers.
5. Backgammon setup, Dominoes deal/draw/drag, Checkers play, Go Fish's seven-card deal and requests,
   and Yacht rolls/scoring remain playable through a complete result/replay path.
6. Yacht shows no large cup before a roll, exactly one large cup during movie 6020, and no large cup
   after that movie ends; the authored three/two small remaining-roll markers stay visible.

Record any rejection with the edition, sheet/frame name or gameplay step, and a screenshot. Do not
publish either GitHub draft until every item is explicitly accepted against the executable checksum
recorded in the generated packet.
