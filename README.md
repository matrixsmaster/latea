# Latea

**Latea** is a small FLTK text editor with a deliberately nonexistent feature set, and only one suspiciously useful trick: inline autocomplete.

It is meant to be a truly lightweight, no-fuss editor for people who want to edit text without being recruited into an ecosystem. No plugins. No cloud sync. No account. No telemetry. No update circus. No extension marketplace. No startup screen asking what kind of productivity journey you are on.

Just an editor, editing text. And you don't even need to memorize 16384 hotkey combos. A truly revolutionary concept for 21st century!

**Latea** is designed for Linux and BSD systems, builds with a simple `Makefile`, and avoids extra dependencies beyond FLTK and the C++ standard library. It uses FLTK 1.3, so it looks a little retro, but that is intentional. The point is not to chase whatever the current desktop fashion is this week, but to feel simple, readable, fast, and comfortably old-school.

## What Latea is

Latea is a plain text editor for writing, programming, note-taking, and other everyday editing jobs.

It does not try to be a full IDE. It does not try to be a document processor. It does not want to become a plugin host, project manager, terminal emulator, browser tab, kitchen appliance, team collaboration portal, or lifestyle coach.

It opens text files, lets you edit them, helps you find and replace things, remembers undo and redo properly, and can suggest what you might want to type next.

That last part is the feature. The only feature.

## What it does

- Open, edit, and save plain text files
- Find, replace, and replace all
- Show inline autocomplete suggestions in several modes:
  - disabled
  - dictionary file
  - current file
  - Markov model
  - AI connector
  - embedded AI engine
- Keep multiple autocomplete candidates and let you move through them back and forth
- Generate Markov autocomplete suggestions from the current document itself
- Accept suggestions inline while staying in the normal editing flow
- Run AI autocomplete through either a local `llama.cpp`-style server or the in-process embedded inference engine
- Launch and manage a local `llama.cpp` server automatically if configured
- Show embedded AI generation progress and approximate token/context usage (for embedded AI engine)
- Has prompt caching for embedded inference where possible
- Customize font, colors, and other editor appearance settings
- Save named preference presets for different editing and AI setups
- Optionally create `.bak` backups before overwriting existing loaded files
- Provide desktop installation
- Stay small, local, and boring in the best possible way

## Autocomplete

Autocomplete appears as inline ghost text at the caret instead of a popup. It is designed to stay out of the way: visible when useful, gone when ignored.

You can move between alternatives back and forth, accept the whole thing, or accept one word and keep the remaining tail alive.

Latea's autocomplete is meant for both programmers and writers, those two famously incompatible categories who somehow keep ending up at the same keyboard.

For programmers, autocomplete can help with:

- names already present in the current file
- words from a dictionary or keyword list
- alternate completion candidates when several words fit
- navigating those candidates fluently, without opening a ceremonial popup altar
- source code continuation from a local AI model
- short inline completions without breaking the editing flow
- switching between simple lexical completion and actual model-backed continuation without joining a productivity cult

For writers, it can act more like a quiet co-pilot:

- suggesting the next word or phrase
- helping continue a sentence
- offering a possible direction when the cursor is blinking with theatrical judgment
- speeding up drafting without taking over the text

And for people who both write and program, Latea may be especially comfortable: simple enough to stay focused, but helpful enough to keep momentum.

## Autocomplete modes

Latea currently supports multiple autocomplete modes. Each mode can work both in a single-shot manual mode, as well as providing continuously adapting suggestions.

### Disabled

No autocomplete. Just plain editing.

### Dictionary file

Suggestions are taken from a user-supplied word list. This is useful for keywords, domain-specific terms, names, or any vocabulary you want Latea to know about.

### Current file

Suggestions are based on words already present in the open document. This is useful for programming identifiers, repeated names, project-specific words, and general consistency.

### Markov model

Suggestions are generated from a small Markov model trained from the current document text. No cloud, no model file, no wizard. It simply looks at local token transitions in the text you already wrote and guesses what usually follows what.

This mode can generate multiple alternative continuations, so `Up` and `Down` work the same way as in other multi-candidate modes.

The `Stop on punctuation` preference decides whether Markov suggestions politely stop at punctuation instead of trying to finish the entire paragraph like a caffeinated parrot.

### AI connector

Suggestions come from a local `llama.cpp`-style server. This allows Latea to generate longer completions, including prose continuation or source code.

The AI connector is local-first. Latea talks directly to a local server over HTTP using simple bespoke socket code. No external HTTP library or JSON library is required, because apparently an editor can still open a socket without summoning a dependency monster.

Latea can also launch and manage the local server itself if you configure a `llama.cpp` server path. It can start the backend, poll until it's ready, retry when the model is still busy rubbing its silicon temples, and shut the server down when autocomplete is reset or the editor exits.

So yes, the editor can now babysit llama.cpp server instead of demanding that you start it manually in another terminal.

### Embedded AI engine

Suggestions come from an embedded local inference engine built into Latea.

This mode uses an in-tree AI engine based on [LiGGUF](https://github.com/matrixsmaster/ligguf), allowing Latea to generate completions from a local model file without requiring a separate `llama.cpp` server to be running in the background.

This is useful when you want:

- fewer moving parts
- no server process to babysit
- a tiny editor with a tiny local model whispering at the cursor

The embedded path includes prompt caching support, so repeated completion requests can reuse context where possible. That matters because autocomplete should feel snappy and helpful.

While embedded generation is running, Latea can show partial progress in the status area and track exact token/context usage. This is mostly so you know whether the model is thinking, sulking, or eating the entire context window with a tiny spoon.

## Keyboard and editing flow

- `Ctrl+Space` to trigger autocomplete
- `Esc` to clear the current suggestion
- `Up` and `Down` to move through alternate suggestions
- `Right` to accept the full suggestion
- `Ctrl+Right` to accept the next suggested word

Autocomplete appears inline as ghost text at the caret, not as a popup menu. If continuous mode is enabled, suggestions are refreshed after a short debounce.

The intent is that autocomplete feels like part of typing, not a separate dialog with opinions. Even when there are multiple candidates, they stay in the typing flow instead of turning text editing into a tiny popup show.

## Preferences and status

Latea supports named preference presets. Instead of one sacred global configuration carved into stone, you can keep multiple setups and quickly switch between them.

## Saving files

Saving files is still meant to be boring. Boring is good. Boring means your text survives.

If `Save backup` is enabled, saving an existing loaded file first writes the previously loaded or saved contents to `<filename>.bak`. The old known-good version gets a tiny cardboard helmet before the new one marches in.

After a successful save, Latea remembers the newly saved contents as the backup baseline for the next save.

## Build

Requirements:

- FLTK 1.3 dev package

Yes, that's it. Build and install it with one command:

```
make install
```

No configuration or a 100MB build system required. Only the tools present in every NIX or BSD system.

You're welcome.

## Run

Start the editor with `./latea`

Or open a file immediately: `./latea path/to/file.txt`

## AI autocomplete

All autocomplete modes are local and offline. Latea is not trying to upload your code, notes, unfinished novel, grocery list, or alarming collection of TODO comments to a mysterious cloud endpoint.

Use your own `llama.cpp` server if you want fancy GPU acceleration and big models. Or configure Latea to launch and manage that local server for you.

Use the built-in AI engine with a small model, and forget about headaches.

And if you're frequently writing long and twisted AI prompts, you're probably already cloning the repo :)

## Philosophy

Latea is intentionally simple.

It is not trying to replace Emacs, Vim, VS Code, Eclipse, LibreOffice, Chrome, or your operating system.

Latea was built in memory of the endangered plain text editor with GUI: a small program that opens a file, lets you type in it, and does not have a marketplace, social feed, login prompt, onboarding wizard, 10-mile long hotkey list, or animated assistant asking whether you are ready to be productive.

This may sound primitive. Possibly even reckless.

Where is the account system? Where can I buy plugins? What if I want to edit each 6th column on each 4th row? Where is the premium AI writing plan for only $99.99 a month? Where is the video tour explaining the concept of a cursor?

Nowhere, mercifully.

The slightly retro FLTK look is part of the charm: simple widgets, simple menus, 0ms startup time, and a feeling that the program is there to serve the text rather than trying to pretend it's a coffee maker and a video player. And the crazy part is that Latea doesn't require the newest graphics card consuming over 9000W just to draw "fancy widgets" via OpenGL. Who might have thought that it's possible to edit text files without graphics accelerator?

Oh, and building from source doesn't require you to pull in absurd amount of dependencies. Only FLTK. Done.

Latea edits text files. That's it! It has optional autocomplete because autocomplete is useful. Everything else has to justify its existence before being allowed through the door.

---

## Credits

Created by **Dmitry 'sciloaf' Solovyev** aka **MatrixS_Master**, in 2026.

## License

MIT
