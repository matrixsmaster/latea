# Latea

Latea is the GUI text editor without any features. Yes, no features!

Well... Only one feature :)

Latea is a small FLTK text editor with a deliberately nonexistent feature set, and only one suspiciously useful trick: inline autocomplete.

It is meant to be a no-fuss editor for people who want to edit text without being recruited into an ecosystem. No plugins. No cloud sync. No account. No telemetry. No update circus. No extension marketplace. No startup screen asking what kind of productivity journey you are on.

Just an editor, editing text. And you don't even need to memorize 16384 hotkey combos. A truly revolutionary concept for 21st century!

Latea is designed for Linux and *BSD systems, builds with a simple `Makefile`, and avoids extra dependencies beyond FLTK and the C++ standard library. It uses FLTK 1.3, so it looks a little retro, but that is intentional. The point is not to chase whatever the current desktop fashion is this week, but to feel simple, readable, fast, and comfortably old-school.

## What Latea is

Latea is a plain text editor for writing, programming, note-taking, and others everyday editing jobs.

It does not try to be a full IDE. It does not try to be a document processor. It does not want to become a plugin host, project manager, terminal emulator, browser tab, kitchen appliance, team collaboration portal, or lifestyle coach.

It opens text files, lets you edit them, helps you find and replace things, remembers undo and redo properly, and can suggest what you might want to type next.

That last part is the feature. The only feature.

## What it does

- Open, edit, and save plain text files
- Keep custom undo and redo history outside FLTK's limited built-in undo support
- Find, replace, and replace all
- Show inline autocomplete suggestions in four modes:
  - disabled
  - dictionary file
  - current file
  - AI connector
- Accept suggestions inline while staying in the normal editing flow
- Store preferences in `~/.latea/latea.cfg`
- Customize font, colors, word wrap, and line numbers
- Stay small, local, and boring in the best possible way

## Autocomplete

Autocomplete appears as inline ghost text at the caret instead of a popup. It is designed to stay out of the way: visible when useful, gone when ignored.

Latea's autocomplete is meant for both programmers and writers, those two famously incompatible categories who somehow keep ending up at the same keyboard.

For programmers, autocomplete can help with:

- names already present in the current file
- words from a dictionary or keyword list
- source code continuation from a local AI model
- short inline completions without breaking the editing flow

For writers, it can act more like a quiet co-pilot:

- suggesting the next word or phrase
- helping continue a sentence
- offering a possible direction when the cursor is blinking with theatrical judgment
- speeding up drafting without taking over the text

And for people who both write and program, Latea may be especially comfortable: simple enough to stay focused, but helpful enough to keep momentum.

## Autocomplete modes

Latea currently supports four autocomplete modes.

### Disabled

No autocomplete. Just plain editing.

### Dictionary file

Suggestions are taken from a word list. This is useful for keywords, domain-specific terms, names, or any vocabulary you want Latea to know about.

### Current file

Suggestions are based on words already present in the open document. This is useful for programming identifiers, repeated names, project-specific words, and general consistency.

### AI connector

Suggestions come from a local `llama.cpp` server. This allows Latea to generate longer completions, including prose continuation or source code.

The AI connector is local-first. Latea talks directly to a local server over HTTP using simple bespoke socket code. No external HTTP library or JSON library is required.

## Keyboard and editing flow

- `Ctrl+N`, `Ctrl+O`, `Ctrl+S`, `Ctrl+Q` for the basic file actions
- `Ctrl+Z` and `Ctrl+Shift+Z` for undo and redo
- `Ctrl+F` and `Ctrl+H` for find and replace
- `Ctrl+Space` to trigger autocomplete
- `Esc` to clear the current suggestion
- `Right` to accept the full suggestion
- `Ctrl+Right` to accept the next suggested word

Autocomplete appears inline as ghost text at the caret, not as a popup menu. If continuous mode is enabled, suggestions are refreshed after a short debounce.

The intent is that autocomplete feels like part of typing, not a separate dialog with opinions.

## Build

Requirements:

- FLTK 1.3 dev package

Yes, that's it. Build with `make` now. No configuration or a 100MB build system required. Only the tools present in every NIX or BSD system.

You're welcome.

## Run

Start the editor with:

```
./latea
```

Or open a file immediately:

```
./latea path/to/file.txt
```

## AI autocomplete

The AI mode talks directly to a local `llama.cpp` server over HTTP.

Preferences let you configure:

- host and port
- completion vs. preferred infill endpoint mode
- prompt prefix and suffix window sizes
- debounce delay and request timeout
- system prompt
- cache reuse flag
- slot id
- maximum suggestion length

The connector tries to use infill mode when requested and when a suffix is available. Otherwise, it falls back to normal completion.

This makes it suitable for both code and prose: it can continue a line of source code, fill in part of a sentence, or suggest the next chunk of text based on the surrounding context.

And if you're frequently writing long and twisted AI prompts, you're probably already cloning the repo :)

## Philosophy

Latea is intentionally simple.

It is not trying to replace Emacs, Vim, VS Code, Eclipse, LibreOffice, Chrome, or your operating system.

Latea was built in memory of the endangered plain text editor with GUI: a small program that opens a file, lets you type in it, and does not have a marketplace, social feed, login prompt, onboarding wizard, 10-mile long hotkey list, or animated assistant asking whether you are ready to be productive.

This may sound primitive. Possibly even reckless.

Where is the account system? Where is the plugin marketplace? What if I want to edit each 6th column on each 4th row? Where is the premium AI writing plan for only $99.99 a month? Where is the video tour explaining the concept of a cursor?

Nowhere, mercifully.

The slightly retro FLTK look is part of the charm: simple widgets, simple menus, 0ms startup time, and a feeling that the program is there to serve the text rather than trying to pretend it's a coffee maker and a video player. And the crazy part is that Latea doesn't require the newest graphics card consuming over 9000W just to draw "fancy widgets" via OpenGL. Who might have thought that it's possible to edit text files without graphics accelerator?

Oh, and building from source doesn't require you to pull in absurd amount of dependencies. Only FLTK. Done.

Latea edits text files. That's it! It has optional autocomplete because autocomplete is useful. Everything else has to justify its existence before being allowed through the door.

---

## Credits

Created by **Dmitry 'sciloaf' Solovyev** aka **MatrixS_Master**, in 2026.

## License

MIT
