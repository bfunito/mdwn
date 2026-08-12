# mdwn

`mdwn` is a fast and lightweight graphical Markdown viewer written in C.
It uses [MD4C](https://github.com/mity/md4c) for parsing, [SDL3](https://www.libsdl.org/) and SDL_ttf for rendering, Fontconfig for font discovery, [GNU Source Highlight](https://www.gnu.org/software/src-highlite/) for syntax highlighting, and toml++ for configuration parsing.

## Features

- GitHub, GitLab, and Codeberg Markdown flavors
- Light and dark themes for every flavor
- Syntax highlighting for fenced code blocks
- Keyboard, mouse, and touchpad navigation
- Text selection and clipboard support
- Clickable links

## Installing from source

### Dependencies

In addition to the libraries used by `mdwn`, building requires a C11 compiler, a C++17 compiler, Make, and pkg-config.

#### Arch Linux

```sh
sudo pacman -S --needed base-devel sdl3 sdl3_ttf md4c fontconfig source-highlight tomlplusplus
```

#### Debian

On Debian 13 (Trixie) or later:

```sh
sudo apt install build-essential pkgconf \
    libsdl3-dev libsdl3-ttf-dev libmd4c-dev \
    libfontconfig-dev libsource-highlight-dev libtomlplusplus-dev
```

Older Debian releases do not provide SDL3 in their official repositories.
Debian-based distributions may require a sufficiently recent release for the same reason.

#### Fedora

```sh
sudo dnf install gcc gcc-c++ make pkgconf-pkg-config \
    SDL3-devel SDL3_ttf-devel md4c-devel \
    fontconfig-devel source-highlight-devel tomlplusplus-devel
```

### Build and install

Clone the repository, build `mdwn`, and install it under `/usr/local`:

```sh
git clone https://github.com/bfunito/mdwn.git
cd mdwn
make
sudo make install
```

This installs the executable as `/usr/local/bin/mdwn` and the manual page under `/usr/local/share/man`.

To install only for the current user instead:

```sh
make
make PREFIX="$HOME/.local" install
```

Make sure `$HOME/.local/bin` is included in your `PATH`.

The Makefile uses the system's default C and C++ compilers. To select other
compilers explicitly, set `CC` and `CXX`, for example:

```sh
make CC=gcc CXX=g++
make CC=clang CXX=clang++
```

To uninstall, use the same prefix used during installation:

```sh
sudo make uninstall
```

For a user-local installation:

```sh
make PREFIX="$HOME/.local" uninstall
```

## Usage

Open a Markdown file with the default GitHub flavor and light theme:

```sh
mdwn examples/hello_mdwn.md
```

Select a flavor or theme with `--flavor` and `--theme`:

```sh
mdwn --flavor gitlab --theme dark examples/hello_mdwn.md
```

Supported flavors are `github`, `gitlab`, and `codeberg`. Supported themes are `light` and `dark`.

Run `mdwn --help` for a command-line summary or `man mdwn` for all options, controls, environment variables, and exit statuses.


## Configuration

`mdwn` reads TOML configuration from the following locations, with later
sources overriding earlier ones:

1. `$XDG_CONFIG_DIRS/mdwn/config.toml` (default: `/etc/xdg/mdwn/config.toml`)
2. `$XDG_CONFIG_HOME/mdwn/config.toml` (default: `~/.config/mdwn/config.toml`)
3. The nearest `.mdwn/config.toml` found by searching from the document's
   directory toward the filesystem root
4. Command-line options

Built-in defaults are used when no configuration file exists. Pass
`--no-config` to ignore all configuration files.

```toml
flavor = "github"
theme = "dark"

[viewer]
initial_zoom = 1.0
min_zoom = 1.0
max_zoom = 5.0
wheel_zoom_speed = 1.0
scroll_step = 48.0
```


## Markdown flavors

- **GitHub**, inspired by the official [Primer CSS Markdown sources](https://github.com/primer/css/tree/5e66b1a87905c1ce9f3268676a91e1002d2dcb5e/src/markdown) and [Prettylights syntax tokens](https://github.com/primer/primitives/blob/30cb00c65d789d6ad4850f8a4fd172276e143226/src/tokens/functional/color/syntax.json5)
- **GitLab**, inspired by the official [GitLab Markdown styles](https://gitlab.com/gitlab-org/gitlab/-/blob/776c20ad5675eecea0c2010433a94d98d745e921/app/assets/stylesheets/framework/typography.scss), [Pajamas design tokens](https://gitlab.com/gitlab-org/gitlab-ui/-/tree/8660f9fec8cb516908ea705c6a91d21c74895564/src/tokens), and [light](https://gitlab.com/gitlab-org/gitlab/-/blob/25d1e81ccf1c217cbd8747d2cc9190becc042c04/app/assets/stylesheets/highlight/_white_base.scss) and [dark syntax themes](https://gitlab.com/gitlab-org/gitlab/-/blob/25d1e81ccf1c217cbd8747d2cc9190becc042c04/app/assets/stylesheets/highlight/themes/dark.scss)
- **Codeberg**, inspired by its [Forgejo Markdown styles](https://codeberg.org/Codeberg-Infrastructure/forgejo/src/commit/3675c72d657baaf0454d2fb55b111bb14f512e3f/web_src/css/markup/content.css), [Codeberg themes](https://codeberg.org/Codeberg-Infrastructure/forgejo/src/commit/3675c72d657baaf0454d2fb55b111bb14f512e3f/web_src/css/themes), and [light](https://codeberg.org/Codeberg-Infrastructure/forgejo/src/commit/3675c72d657baaf0454d2fb55b111bb14f512e3f/web_src/css/chroma/light.css) and [dark syntax themes](https://codeberg.org/Codeberg-Infrastructure/forgejo/src/commit/3675c72d657baaf0454d2fb55b111bb14f512e3f/web_src/css/chroma/dark.css)

## Current limitations

- Raw HTML is rendered literally.
- Images are not displayed; their alternative text is rendered instead.
- LaTeX math is displayed as text rather than typeset.

## License

`mdwn` is distributed under the [GNU General Public License, version 3](LICENSE).
