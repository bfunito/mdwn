`mdwn` is a fast and light Markdown viewer written in C.

It supports different Markdown flavors:

- **Github**, inspired from the official [Primer CSS Markdown sources](https://github.com/primer/css/tree/5e66b1a87905c1ce9f3268676a91e1002d2dcb5e/src/markdown) and [Prettylights syntax tokens](https://github.com/primer/primitives/blob/30cb00c65d789d6ad4850f8a4fd172276e143226/src/tokens/functional/color/syntax.json5)
- **GitLab**, inspired from the official [GitLab Markdown styles](https://gitlab.com/gitlab-org/gitlab/-/blob/776c20ad5675eecea0c2010433a94d98d745e921/app/assets/stylesheets/framework/typography.scss), [Pajamas design tokens](https://gitlab.com/gitlab-org/gitlab-ui/-/tree/8660f9fec8cb516908ea705c6a91d21c74895564/src/tokens), and [light](https://gitlab.com/gitlab-org/gitlab/-/blob/25d1e81ccf1c217cbd8747d2cc9190becc042c04/app/assets/stylesheets/highlight/_white_base.scss) and [dark syntax themes](https://gitlab.com/gitlab-org/gitlab/-/blob/25d1e81ccf1c217cbd8747d2cc9190becc042c04/app/assets/stylesheets/highlight/themes/dark.scss)
- **Codeberg**, inspired from its [Forgejo Markdown styles](https://codeberg.org/Codeberg-Infrastructure/forgejo/src/commit/3675c72d657baaf0454d2fb55b111bb14f512e3f/web_src/css/markup/content.css), [Codeberg themes](https://codeberg.org/Codeberg-Infrastructure/forgejo/src/commit/3675c72d657baaf0454d2fb55b111bb14f512e3f/web_src/css/themes), and [light](https://codeberg.org/Codeberg-Infrastructure/forgejo/src/commit/3675c72d657baaf0454d2fb55b111bb14f512e3f/web_src/css/chroma/light.css) and [dark syntax themes](https://codeberg.org/Codeberg-Infrastructure/forgejo/src/commit/3675c72d657baaf0454d2fb55b111bb14f512e3f/web_src/css/chroma/dark.css)

Dark themes are available for all flavors and can be selected with `--theme dark`.
