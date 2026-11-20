# UI font

- `font.ttf` — trimmed DejaVu Sans embedded into the ELF (via `embed.cmake`)
- `DejaVu.ttf` — full original DejaVu Sans
- `LICENSE` — Bitstream Vera / DejaVu license

## Regenerating `font.ttf`

Requires [fontTools](https://github.com/fonttools/fonttools) (`pyftsubset`)

```sh
pyftsubset DejaVu.ttf \
  --output-file=font.ttf \
  --unicodes="U+0020-007E,U+00A0-00FF,U+0100-017F,U+0180-024F,U+0370-03FF,U+0400-052F,U+2000-206F,U+20A0-20CF,U+2100-214F,U+2190-21FF,U+2500-257F,U+25A0-25FF" \
  --layout-features='*' \
  --drop-tables+=MATH,FFTM \
  --name-IDs='*' \
  --notdef-outline \
  --recommended-glyphs
```

Coverage: Basic Latin, Latin-1, Latin Extended A/B, Greek, Cyrillic, and common punctuation/symbols.
