Look at CLAUDE.md, README.md, 00-roadmap.md, and 07-clone-architecture.md

The goal is to create the toolchain spike. This includes:
- having a working build system that can generate packaged artifacts for redistribution via Github releases
- having an extractor system that first asks for a path to the original game directory, verifies the validity of the selected directory, and then performs extraction steps to a known location next to the game EXE. 
- having a game that loads some extracted data and assets and renders a sprite, and receives clicks and keypresses.

---

Once the spike is created, the full spec for the game program must be fleshed out (in OpenTE/spec/ directory). This should cover the categories of rendering, audio, input, ui, data-model, world-and-maps, simulation, entities, opponent AI, and modding, and any other relevant categories. Each category probably deserves a separate markdown file in the spec. The spec should cover implementation in detail, and highlight any areas where we still don't know enough about the original implementation of the game to recreate it faithfully.

The spec should not reference anything about the implementation of the original game that isn't relevant to the implementation of the clone. The spec is about how to build the clone cleanly in a modern way - not the results of the reverse engineering.

If the implementation of the original game uses an old method that is superseded by a new method, we should likely use the new method to acheive cleaner, more readable, maintainable, friendly code.

The spec shouldn't detail implementation ORDER. That can be covered in one or more docs placed into the OpenTE/implementation/ directory. Any notes about stages of implementation, milestones, intermediate goals, or temp/placeholders can go there.