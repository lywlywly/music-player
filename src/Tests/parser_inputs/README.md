Place parser test audio files in this directory (or subdirectories).

The parser test (`tst_parser`) reads a config file:
- `src/Tests/parser_inputs/parser_cases.json`

Config format:

```json
{
  "cases": [
    {
      "file": "relative/path/to/song.mp3",
      "expect": {
        "title": "Expected Title",
        "artist": "Expected Artist",
        "tracknumber": "3"
      }
    }
  ]
}
```

Rules:
- `file` is relative to `src/Tests/parser_inputs`.
- `expect` maps field id -> expected string value.
- `filepath` is always validated automatically by the test.
- The test fails if `parser_cases.json` is missing, has no cases, or referenced
  audio files are missing.

Audio fixtures are intentionally not tracked in git.
