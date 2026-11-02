# A move's own map

Each move that a listing compare judges keeps two files here, named for it and committed with it:

- `<move>.map`, its rename map;
- `<move>.expected`, its expected differences.

They are read against the commit before the move, never appended to an earlier move's: a file moved twice is two maps
and two compares. `tools/loop_listing_compare.py` gives their form:

    loop_listing.py compare BEFORE AFTER tools/moves/<move>.map tools/moves/<move>.expected
