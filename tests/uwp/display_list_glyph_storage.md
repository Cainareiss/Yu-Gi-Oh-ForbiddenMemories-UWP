The installed UWP 1.0.25.0 executable's DWARF records `GlyphSprite` as 0x78
bytes, with `sourceGlyph` at 0x50, `frame` at 0x68, `textBoxSlot` at 0x6F,
and `sequence` at 0x72. The actual display pool has a 0x70-byte stride.
Consequently the real `NameEntry_SpawnGlyphSprite` sequence assignment writes
into the following object's `next` link. The captured frozen pool contained
object 21 linked to itself and repeatedly executing its fade/release callback.

The upstream `DuelEffectEntry *G32 sourceGlyph` keeps the member four bytes
wide and restores the verified offsets: 0x4C, 0x60, 0x67, and 0x6A, with total
size 0x6C. `display_list_glyph_storage.py` compiles the real allocator,
configuration, release, and glyph-spawn code with x64 host pointers. It checks
every available nonterminal general slot, all 256 sequence values, and all
four text-box slots against the following slot's complete contents. It also
checks the live list and release behavior for the observed 20/21 setup.

The negative control recompiles the same production spawner with the former
view, independently represented from the baseline's DWARF. It must report a
neighbor overwrite. No live game process, key input, or cycle-breaking guard
is used by this test.
