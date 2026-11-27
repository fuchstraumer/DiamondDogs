# EditorUi (`dd::ui`)

`EditorUi` provides reusable immediate-mode layout primitives, telemetry styling tokens, and low-level custom controls over Dear ImGui (`imgui_module`) with zero dependency on `RenderGraph` or Vulkan/WebGPU headers.

## Components

- **`UiTheme`:** Packed ABGR role color tokens, spacing metrics, `HeaderFontMode` typography selection (`Blinker` and `Michroma`), and ImGui style application (`MakeTelemetryTheme`, `ApplyImGuiTheme`, `ResolveFont`).
- **`UiLayout`:**
  - Headless track and rect solvers (`SolveTrackSizes`, `SolveHorizontalRects`, `SolveGridCellRect`) usable without an ImGui context.
  - Non-copyable RAII layout scopes (`VBox`, `HBox`, `Grid`, `Card`, `PropertyRow`) backed by a compact `foundation::SmallVector` scope stack so nested containers inherit active slot bounds without heap allocations on normal UI depths.
  - Each `Slot()`, `Next()`, or `NextCell()` sets the ImGui cursor position, pushes item width for stock widgets, and returns a `Rect` for custom `ImDrawList` widgets.
- **`UiControls`:** Telemetry-styled headers, key-value readouts, toggles, segmented selectors, artist sliders (with logarithmic scale and Ctrl-click text entry), and snapped angle sliders.
- **`EditProposal`:** Deferred UI mutation records (`CommitKind`, `ProposalOp`, `EditProposal`, `EditProposalSink`, `ProposalBuffer`, and `ApplyEditProposal`) so inspector controls never write runtime structs directly.
- **`Inspector` & `WidgetRegistry`:** Read-only reflection walker over `foundation::reflect::TypeInfo` and `FieldUiMetadata`, with extensible widget tag / type name overrides and optional `FieldDecorator` readouts for derived runtime values.
