# Shadow64 R13 Duke64-control gameplay milestone

R13A builds on the R12 textured slice and moves the first level toward an actual game loop.

Implemented in this milestone:
- Duke Nukem 64-style default layout: Control Stick look, C-buttons move/strafe, Z fire, A use, R jump, B crouch, D-pad left/right weapon select, D-pad up/down inventory selection placeholder, L inventory-use placeholder, Start pause;
- sword plus Uzi selection, ammo, HUD fire frames and hitscan damage;
- Ninja/Coolie pain/death states and basic line-of-sight attacks;
- health, armor, Uzi/ammo and four key families;
- A-button nearby-use path for map switches and tagged sector operators;
- matched-tag activation for vators/rotators/slidors/door-tag sectors;
- simple animated vator floor movement;
- floor/ceiling slope-aware player height and rendering approximations;
- level-exit switch/sector detection;
- portal-aware visibility retained from R12.

Still compatibility-layer behavior, not a byte-for-byte JFSW sector engine: rotating/sliding door geometry is currently represented by opening/closing the relevant portal rather than reproducing the original moving geometry, complex sector objects/triggers are incomplete, inventory items are placeholders, and enemy/weapon behavior is simplified. Hardware testing on the real first level determines the next blocker.
