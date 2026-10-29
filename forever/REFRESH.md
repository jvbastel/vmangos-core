# Refresh of the Forever spec (stub)

`forever/warrior.tsv` holds the Forever rules of the Warrior (spec jvbastel/WOWAI#204). The generator in
`forever/generator` makes all Forever data from it. Nobody edits the generated files by hand.

## How to refresh

1. Find the newest Forever build. Read the talent page and the class changes page of the sources that
   the spec header names. Use two sources that read the build data, not guides.
2. Compare each row of the spec with the build data: position, ranks, prerequisite, tooltip numbers,
   icon. When the sources disagree, the in-game tooltip of the build wins. Write the disagreement in the
   header.
3. Change the rows, the build number and the date in the header. A surviving talent keeps its 1.12
   talent ID. A new talent or spell takes a free ID from the range in the header.
4. Run the data pipeline test: `cd forever/generator && uv run pytest`.
5. Open the refresh as a PR. Nothing changes on a server until the player merges it.

To be completed by #210 (full spec and refusals): the full column list, how to pick a stock icon, and the
in-game check after a refresh.
