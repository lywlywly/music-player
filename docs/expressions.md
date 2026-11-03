# Expressions

One expression language (`libraryexpression_*`) is used for Library Search,
autoplaylists, Playlist Search, computed fields, and the display expressions
that build the status bar text and window title. This file covers the
language, how it is parsed and evaluated, and the display expressions. See
[architecture.md](architecture.md) for the rest of the app.

## Language and modules

* `libraryexpression_*` modules
  * Tokenization, parsing, AST, type inference, and operator evaluation.
  * Supports boolean expressions, comparisons, lists/ranges, and `IF ... THEN ... ELSE ...`.
  * Supports string interpolation with backticks and `${...}` placeholders.
  * Resolves names through `ExprSymbolResolver` into canonical namespaced ids:
    * runtime: `status:<name>`
    * built-in: `builtin:<name>`
    * dynamic tag: `attr:<key>`
    * computed: `computed:<key>`
  * Symbol providers export both unqualified aliases and fully-qualified names.
  * Unqualified resolution precedence is: `status > builtin > attr > computed`.
  * Precedence is implemented by first-match lookup in
    `ExprSymbolResolver::lookup(...)` over merged symbol order:
    display contexts prepend runtime symbols, while registry symbols are emitted
    in `builtin`, then `attr`, then `computed` order.
  * `HAS` supports multi-value text split by comma separators.
* `LibraryExprEvalContext`
  * Evaluation boundary for field lookup.
  * Provides `FieldValue` instances to the expression runtime.
* `parseSongFilter` (songlibrary.h)
  * Song filters must be conditions: it parses with
    `SongLibrary::parseLibraryExpression` and rejects an expression whose
    static type is not boolean ("Expression must be a condition"). A bare
    word like `davis` parses as a text literal, so it is rejected.
  * Used by Library Search, autoplaylists (`PlaylistCollection`,
    `AutoPlaylistDialog`) and `SongMatcher`.
* `LibrarySearchDialog` + `LibrarySearchResultsModel`
  * Parse the query with `parseSongFilter` (conditions only), evaluate it over
    the monitored library (`SongLibrary::search(expr, candidatePks)`), then
    present the matching songs in `filepath` order.
* `SongMatcher` (Playlist Search)
  * Uses the query as a condition when `parseSongFilter` accepts it, and as
    plain words otherwise. See "Playlist Search" in
    [monitored-library.md](monitored-library.md).

## Parsing

* `QString` / `std::string` boundary:
  * `QString`: tokenizer/parser input, token text, and parse errors.
  * `std::string`: AST/runtime/eval/storage data (`ExprValue`,
    `ExprFieldRef`, runtime values, interpolated parts, `FieldValue::text`,
    eval lookup key `std::string_view`).
  * Main conversion happens during parse, where token text is normalized and
    resolved into `std::string` AST/runtime payloads.

* Comparison AST/eval shape is unified as `leftExpr op rightExpr`.
  * The comparison type is resolved from the left expression:
    field refs use field value type; non-field expressions use static type.
  * Right side is validated against that left-side type (either literal/list/range
    value form or another expression).
* `ExprValueExpr` wraps RHS value-form literals (scalar/list/range) parsed by
  `parseValue(...)`, so comparisons keep one `leftExpr op rightExpr` shape.
* Field resolution/evaluation split:
  * Parse resolves field tokens into canonical namespaced `resolvedId`s.
  * Unqualified names are canonicalized to fully-qualified IDs at parse time
    using precedence; evaluation does not re-resolve by precedence.
  * Eval always reads by canonical ID
    (`context.fieldValue(field.resolvedColumnId)`).
  * In display context, canonical `status:*` fields are read from runtime
    symbols; other canonical fields are read from the current song.
* Parser flow (high level):
  * `parseOr(...)` is the parse entry point, then `parseAnd(...)`,
    `parseUnary(...)`, and `parsePrimary(...)` by precedence.
  * `parsePrimary(...)` parses an atom first, then optional comparison suffix.
    If atom-start is invalid, it falls back to field-comparison parsing for
    clearer diagnostics.
  * Field-led (`field op value`) and generic suffix (`expr op value`)
    comparisons share the same comparison-tail parser path for consistency:
    operator parse in `parseComparisonTail(...)`,
    RHS parse+validation in `parseComparisonRightExpr(...)`,
    then `ComparisonExpr` construction.

## Evaluation contexts

* Song filters (Library Search, autoplaylists, Playlist Search):
  * Resolver source: `ColumnRegistry::expressionSymbols()`
  * Eval context: song-only (`SongLibraryExprEvalContext`)
  * Eval lookup maps canonical IDs to song map keys:
    * `builtin:* -> <builtin key>`
    * `attr:* -> attr:*`
    * `computed:* -> computed:*`
* Display expressions (status bar + window title):
  * Resolver source: runtime symbols merged first, then registry symbols
  * Eval context: runtime-first, then current song (`DisplayExpressionEvalContext`)
  * Collisions prefer runtime symbols; fully-qualified names always disambiguate.
  * Runtime names shadow song fields: `duration` and `bitrate` are the
    playback values (`status:*`); `builtin:duration` / `builtin:bitrate`
    read the song's tags.
  * Interpolation is supported in backtick strings only.
  * In interpolation, `${field}` display comes from `FieldValue::display()`.
  * A `${...}` holding one known field name becomes a field reference;
    anything else is parsed as a full expression, where an unknown bare
    name is literal text. So `${mood}` with no `mood` field shows `mood`
    instead of failing; outside `${}`, an unknown field is a parse error.
  * Known issue: a bare field used as the whole expression is not formatted.
    `evaluateDisplayExpression` (status bar, window title and the
    `SettingsDialog` preview) turns the result into text with
    `runtimeValueToQString(evaluateValue(...))`,
    which gives the raw typed value, while interpolation parts use
    `evaluateDisplayText(...)`, and `FieldRefExpr::evaluateDisplayText` returns
    `FieldValue::display()`. So `duration` shows `125` but `` `${duration}` ``
    shows `02:05`, and `channels` shows `2` but `` `${channels}` `` shows
    `stereo`. A field returned from an `IF` branch is raw in both forms,
    because `IfExpr` uses the base `evaluateDisplayText`, which converts
    `evaluateValue`. Possible fix: have `evaluateDisplayExpression` use
    `QString::fromStdString(expr.evaluateDisplayText(context))`; only bare-field
    expressions would change, and no test covers that case yet.
* Song field lookup for expressions (`builtin:<name>` stored as `<name>`) is
  `findSongFieldValue` (`songlibrary.h`), shared by
  `SongLibraryExprEvalContext` and `DisplayExpressionEvalContext`.

## Display expressions (status bar and window title)

### Classes

* `NowPlayingDisplay` (no widgets, owned by `NowPlayingPresenter`)
  * Builds the status bar text and window title from the display
    expressions, the playback status and the current song;
    `NowPlayingPresenter` only applies the strings. Nothing playing: no
    status text and the window's original title.
  * Owns the parsed expressions (`setExpression(kind, text)` with the text
    each Settings signal carries; `loadExpressions()` re-parses both from
    settings after custom columns load or change, since field names are
    resolved at parse time) and the values behind the `status:*`
    symbols: position, duration and the bitrate chosen for the song (see
    "Bitrate display behavior").
  * Playback status and the current song are passed in on each call, not
    stored, so they cannot go stale; `PlaybackController` owns them.
* `NowPlayingPresenter` (frontend)
  * Connects to `PlaybackController`'s `songAboutToStart`, `songStarted`,
    state, position, duration and bitrate signals, updates
    `NowPlayingDisplay`, and sets the window's status bar message and title.
  * `MainWindow` passes it the expressions from each Settings signal
    (`setExpression`), calls `reloadExpressions()` after custom columns load
    or change, and on `refreshed()` updates an open Settings dialog's preview
    context with `runtimeSymbols()` and `PlaybackController::currentSong()`.
* `displayexpression.{h,cpp}`
  * The rules both `NowPlayingDisplay` and `SettingsDialog` use: settings
    keys, the default expressions, "blank means default", parsing with
    runtime symbols first, and evaluation (its eval context is private to
    `displayexpression.cpp`).

### Settings

* Status bar expression is persisted as `status_bar/expression`.
* Window title expression is persisted as `window_title/expression`.
* Settings dialog shows a live preview for the currently active display
  expression editor, using current playback/runtime song context when
  available (fallback sample values otherwise).
* Default expressions come from `defaultDisplayExpression(kind)`.
* A blank expression is stored and used as the default one; a stored
  expression that does not parse is used as the default one (with a
  warning), while the Settings preview shows the parse error.
* Custom (`attr:`) columns load after the expressions are first parsed, so
  they are parsed again once the columns are in `ColumnRegistry` (see
  "Startup loading" in [architecture.md](architecture.md)).

### Runtime symbols

* `StatusRuntimeSymbolTable` also registers its runtime field definitions into
  `FieldTypePool`, so runtime fields (for example `playback_time` and
  `duration`) use the same `FieldValue::display()` formatting pipeline.
* Runtime symbols are exposed as `status:*` fields
  (`status:isplaying`, `status:ispaused`, `status:playback_time`,
  `status:duration`, `status:bitrate`) and also as unqualified aliases.
* Runtime symbols are only available in display-expression parsing/evaluation
  contexts; song filter parsing uses registry symbols only.

### Bitrate display behavior

* `NowPlayingDisplay` evaluates both status bar and window title from display
  expressions (same expression engine + runtime symbols).
* `${bitrate}` in display expressions reads runtime symbol `status:bitrate`.
* `status:bitrate` comes from `NowPlayingDisplay` (`startSong` reads the
  tag bitrate and CBR detection, `setStreamBitrateBps` the backend's):
  * if track is treated as CBR and parsed tag bitrate is available, use tag bitrate
  * otherwise use runtime playback bitrate when available
  * otherwise fall back to parsed tag bitrate (or `0`)
* `GstAudioPlayer` provides playback-time bitrate:
  * hooks `playbin` `element-setup`,
  * attaches one pad probe to the first `Decoder/Audio` sink pad,
  * accumulates buffer bytes in an atomic counter,
  * emits `bitrateChanged` about once per second from `updatePosition()`.
* For tracks treated as CBR (`mp3` and `wav`, detected via TagLib file type),
  status display prefers parsed tag bitrate from song metadata.
* `QTAudioPlayer` does not currently emit runtime bitrate updates, so UI falls
  back to tag bitrate when present.
