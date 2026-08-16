# Shadow64 R12 textured/playable milestone

R12 moves past the R11 wireframe map-core test and renders directly from the user's own Shadow Warrior ART/palette data.

Implemented in this milestone:
- real `$bullet.map` (Seppuku Station) loading;
- compact texture bank generated locally from map-referenced `TILES###.ART` resources;
- wall, current-sector floor/ceiling, sprite and sword-HUD texture drawing;
- portal-sector visibility traversal instead of drawing all walls at once;
- collision/sliding and sector transitions;
- sword attack animation and short-range hit detection;
- simple Ninja/Coolie enemy activation/chase/contact damage;
- basic health pickup handling;
- HP, kills and pickup counters;
- overhead map toggle and reset.

This is a first playable N64-native gameplay slice, not yet a full JFSW port. Doors/elevators, slopes, masked/rotated walls, full Build visibility/palookups, exact Shadow Warrior actor state machines, projectile weapons, sound/music and scripted level logic remain follow-up work.
