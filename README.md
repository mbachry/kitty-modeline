# kitty-modeline

Add [starship](https://starship.rs/) modeline to [kitty](https://sw.kovidgoyal.net/kitty/) terminal.

## Introduction

Starship is a nice improvement to bash experience, but can add visual
clutter and performance issues, especially with big git
repositories. Here's a example with emacs git repo:

![emacs](https://raw.githubusercontent.com/mbachry/kitty-modeline/refs/heads/main/media/before.gif)

You can see that the prompt line is too long and noisy and there's a
huge delay after pressing enter. Keeping it pressed for longer chokes
the shell.

`kitty-modeline` adds a one-row bar to the bottom of kitty windows in
a similar fashion to emacs or vim modelines used to display file
information, column/line numbers in those editors. All the starship
clutter is moved away from bash prompt to a less distracting
location. In addition to that, the updates are asynchronous and don't
block the shell:

![after](https://raw.githubusercontent.com/mbachry/kitty-modeline/refs/heads/main/media/after.gif)

The screen recording shows that while the starship update is still
slow, the shell prompt is not laggy anymore.

Asynchronous updates open possibilities you wouldn't consider normally
due to high latency. For example you can call github pull request APIs
for the repo in the current directory using starship's `custom` rule.

## Installation

At the moment patching `kitty` is required.

Clone the repo with `--recurse-submodules` or run `git submodule init`
and `git submodule update` after cloning.

You need C compiler, `meson` and `glib`. Type `make` to build helper
commands and patch `kitty`. Move `build/kitty-modeline` and
`build/kitty-modeline-client` to your favorite location,
eg. `~/.local/bin`.

Go to `kitty` directory and follow [kitty build
instructions](https://sw.kovidgoyal.net/kitty/build/). You can run
patched `kitty` straight from the source tree after building.

## Configuration

Add `modeline_command ~/.local/bin/kitty-modeline` to `kitty.conf`
(use bin location you picked). `modeline_bg_color` setting configures
the background color of the modeline (`#444444` by default).

Comment out `starship init` in bash profile and source `prompt.sh`
file from this repo instead. Note that `PS1` isn't set anymore, so you
may want to configure it so something simple,
eg. `PS1="\[\033[0;32m\]\w ➜\[\033[0m\] "`.
