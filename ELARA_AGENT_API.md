# Elara Agent API Reference

This document is written for code-generating and code-modifying AI agents working inside an Elara-based project.

It is intentionally biased toward:
- include paths
- namespaces
- ownership rules
- stable entry points
- legacy traps that should be avoided unless a human explicitly asks for them

## Global Rules

### Namespaces

- Most modernized headers use `namespace elara`.
- `libelaradata/*` still exposes `namespace nrcore`.
- `libelaraserializer/*` still exposes `namespace nrcore`.
- `libelaracore/serializers/*` also still exposes `namespace nrcore`.
- Do not assume namespace consistency across all libraries.

### Ownership Rules

- `elara::Ref<T>` from `<libelaracore/memory/Ref.h>` is a non-thread-safe shared ownership wrapper.
- `elara::threading::memory::Ref<T>` from `<libelarathreads/memory/Ref.h>` is the thread-safe shared ownership wrapper for cross-thread lifetime management.
- Prefer pointer-like usage:
  - `ref->method()`
  - `*ref`
  - `ref.getPtr()` only for raw interop
- Do not assume `elara::Ref<T>` itself makes object state thread-safe.

### Strings And Bytes

- Prefer `elara::String`, `elara::Memory`, and `elara::ByteArray` for internal Elara-facing APIs.
- Convert to raw buffers only at OS or third-party library boundaries.
- `String` is mutable and not `std::string`-compatible by design.

### Linting Policy

- Generated Elara C++ projects are expected to expose a `make lint` target.
- The default linter binary for generated projects is `../build/bin/elara.cpp-lint`.
- If a local framework build is not available, the system fallback is `/usr/local/bin/elara.cpp-lint`.
- Current lint policy is intentionally strict:
  - allowed direct value types are primitives such as `int`, `char`, `bool`, `long`, `float`, `double`, and practical signed/unsigned variants
  - allowed direct Elara safe value types are:
    - `elara::String`
    - `elara::Memory`
    - `elara::ByteArray`
  - allowed borrowed parameter forms include:
    - `const elara::String&`
    - `const elara::Memory&`
    - `const elara::ByteArray&`
    - `const elara::Ref<T>&`
    - `elara::Ref<T>&`
  - all other declared object types should use:
    - `elara::Ref<T>`
    - `elara::RefArray<T>`
    - `elara::threading::memory::Ref<T>`
- If the linter rejects a legitimate framework pattern, treat that as a policy-gap candidate and update the allowed rules deliberately rather than bypassing the lint target casually.
- Do not introduce STL ownership containers into Elara-facing code unless a human explicitly asks for that policy change.

### Build/Link Rules

- Generated `config.h` headers are build artifacts, not stable public API.
- Root-local staged headers live under `build/include`.
- Root-local staged libraries live under `build/lib`.
- Normal local builds stage under the repository-local `build/` tree.
- Framework install targets `/usr/local` by default.
- Common link order:
  - core first
  - threads before debug
  - io before debug
  - event before sockets

### Build/Install Workflow

- Prefer `./build.sh` for local framework builds.
- Prefer `./install.sh` only to install already-built framework artifacts into `/usr/local`.
- `./install.sh --remove` removes installed framework files using the install manifest at `/usr/local/share/elara.cpp/install-manifest.txt` when present.
- Do not assume privileged install steps should also compile or reconfigure code.
- Generated projects follow the same split:
  - `./build.sh` builds locally
  - `./install.sh` installs the built binary
  - `./install.sh --remove` removes it using a per-project manifest under `PREFIX/share/<target>/install-manifest.txt`

### Project Builder And Generated Project Rules

- The canonical bundled agent doc for the installed builder lives at:
  - `/usr/local/share/elara-project-builder/ELARA_AGENT_API.md`
- `elara-project-builder` copies that document into each generated project root as `ELARA_AGENT_API.md`.
- `elara-project-builder` supports both interactive and non-interactive generation.
- Important CLI flags:
  - `--help`
  - `--interactive`
  - `--non-interactive`
  - `--name`
  - `--target`
  - `--output`
  - `--repl`
  - `--thread-pool`
  - `--worker`
  - `--worker-name`
  - `--indexed-data-store`
  - `--store-path`
  - `--store-bank-map-redundancy`
  - `--socket-mode`
  - `--address`
  - `--port`
- `--address` and `--port` are only valid with `--socket-mode server` or `--socket-mode client`.
- `--store-path` and `--store-bank-map-redundancy` imply `--indexed-data-store yes`.
- Generated project defaults:
  - use Elara staged at `../build` for includes, libraries, and tools
  - lint with `../build/bin/elara.cpp-lint`
  - install binaries into `/usr/local/bin` by default
- Generated project install manifests live under:
  - `/usr/local/share/<target>/install-manifest.txt`
- Generated socket projects support both:
  - server mode
  - client mode
- Socket-enabled generated projects automatically enable the thread pool.
- Storage-enabled generated projects scaffold a minimal REPL around `IndexedDataStore` with:
  - `initstore`
  - `put <key> <value>`
  - `get <key>`
- Generated storage projects use explicit store creation:
  - `IndexedDataStore(path, redundancy_count)` for initialization
  - `IndexedDataStore(path)` to reopen an existing store

### Elara UI RPC JSON Contract

This section is the current source-of-truth contract for implementing an Elara UI head client in another language such as Python.

Scope:
- transport is framed JSON-RPC over a duplex socket
- the UI head is the authoritative widget tree and event source
- non-C++ clients should treat JSON-RPC as the wire protocol only
- logic-side code should still model higher-level typed operations locally

#### Transport Model

- Method names are fully qualified JSON-RPC method strings such as:
  - `ui.loadDocument`
  - `ui.snapshot`
  - `ui.setText`
- The current C++ peer uses request/response calls and notifications on the same connection.
- The UI head may also call back into the logic client using outbound notifications such as:
  - `ui.event`

Practical Python guidance:
- keep one long-lived connection
- run a background reader loop so outbound UI events are not blocked by synchronous request handling
- treat every inbound `ui.event` as asynchronous

#### Root Concepts

- Widget ids are stable string handles such as `demo.tabs` or `demo.widgets.input`.
- `root.content` is the stable mounted application subtree.
- `root.popups` is a z-ordered popup stack at the root level.
- Dynamic UI mutation is expected to happen by replacing subtrees below an existing widget id, not by replacing the root itself.

#### Layout Loading

Method:
- `ui.loadDocument`

Params:
```json
{
  "document": "{ ... full elara_ui_protocol JSON document as a string ... }"
}
```

Result:
```json
{
  "loaded": true
}
```

Notes:
- `document` must be a JSON string literal containing the full layout document.
- This is a full load of the UI description, not an incremental patch.
- The layout format itself is documented in `libElaraUI/docs/ELARA_UI_JSON_PROTOCOL_DRAFT.md`.

#### Flat Client Builder API

For C++ client-side code, prefer `libElaraUiRpc/libelarauirpc/ElaraUiDocumentBuilder.h` instead of hand-assembling layout JSON strings.

It provides a flat stateful API that caches the document and serializes valid `elara_ui_protocol` JSON:
- `createWindow(title, width, height, backend_id)`
- `setThemeMode(mode)`
- `setRootContent(widget_id)`
- `createTabs(id)`
- `createGrid(id)`
- `createButton(id, text, action)`
- `createTextInput(id, placeholder, text)`
- `addTab(tabs_id, title, child_id)`
- `placeGridChild(grid_id, child_id, column, row, column_span, row_span)`
- `setPropertyString/Number/Bool(...)`
- `setSectionJson(...)` for raw widget sections such as `demo_data`
- `toJson()`

Design rule for Python and other non-C++ bindings:
- mirror this builder shape locally as a simple stateful object model
- keep JSON-RPC as a transport detail, not the primary programming interface
- emit the same final document shape as `ElaraUiDocumentBuilder::toJson()`

#### Runtime Mutation Methods

Method:
- `ui.setText`

Params:
```json
{
  "target": "demo.widgets.input",
  "value": "hello"
}
```

Supported widgets:
- button
- label
- text input

Result:
```json
{
  "updated": true
}
```

Method:
- `ui.setVisible`

Params:
```json
{
  "target": "demo.popup",
  "visible": true
}
```

Result:
```json
{
  "updated": true
}
```

Method:
- `ui.setEnabled`

Params:
```json
{
  "target": "demo.widgets.button",
  "enabled": false
}
```

Currently supported widgets:
- button
- text input

Method:
- `ui.setBounds`

Params:
```json
{
  "target": "demo.popup",
  "x": 40,
  "y": 60,
  "width": 180,
  "height": 120
}
```

Method:
- `ui.setFocus`

Params:
```json
{
  "target": "demo.widgets.input"
}
```

Method:
- `ui.clearChildren`

Purpose:
- clear the subtree immediately below a target widget

Params:
```json
{
  "target": "demo.widgets.grid"
}
```

Result:
```json
{
  "updated": true
}
```

Method:
- `ui.replaceChildren`

Purpose:
- clear the subtree below a target widget and replace it in a single command

Params for generic containers and grid layouts:
```json
{
  "target": "demo.widgets.grid",
  "document": "[{\"id\":\"demo.new.label\",\"type\":\"elara.widgets.label\",\"cell\":{\"column\":1,\"row\":1},\"properties\":{\"text\":\"Replacement\"}}]"
}
```

Params for tabs:
```json
{
  "target": "demo.tabs",
  "document": "{\"tabs\":[{\"title\":\"New Tab\",\"widget\":{\"id\":\"demo.new.page\",\"type\":\"elara.widgets.label\",\"properties\":{\"text\":\"Hello\"}}}]}"
}
```

Rules:
- `document` is always a JSON string literal on the wire
- if the string starts with `[` it is interpreted as a `children` array
- otherwise it is interpreted as an object such as `{\"children\":[...]}` or `{\"tabs\":[...]}`
- current container-aware rebuild behavior exists for:
  - generic child containers
  - `elara.layouts.grid`
  - `elara.widgets.tabs`

#### Event Subscription Methods

Method:
- `ui.enableEvent`
- `ui.disableEvent`

Params:
```json
{
  "action": "clicked"
}
```

Supported outbound action names:
- `mouseMove`
- `mouseDown`
- `mouseUp`
- `clicked`
- `hoverChanged`
- `keyDown`
- `keyUp`
- `keysTyped`
- `valueChanged`

#### Input Dispatch Methods

Method:
- `ui.dispatchMouseMove`

Params:
```json
{
  "x": 420,
  "y": 180
}
```

Method:
- `ui.dispatchMouseDown`
- `ui.dispatchMouseUp`

Params:
```json
{
  "button": 1,
  "x": 420,
  "y": 180
}
```

Method:
- `ui.clickWidget`

Purpose:
- helper that computes a widget-local click center on the server side

Params:
```json
{
  "target": "demo.widgets.button",
  "button": 1
}
```

Method:
- `ui.dispatchKeyDown`
- `ui.dispatchKeyUp`

Params:
```json
{
  "keyval": 65
}
```

Method:
- `ui.typeWidget`

Purpose:
- focus a target widget and synthesize key down/up events for each byte in `text`

Params:
```json
{
  "target": "demo.widgets.input",
  "text": "hello"
}
```

Notes:
- this is currently byte-oriented, not a full Unicode IME contract
- Python implementations should preserve the exact string supplied by the caller and let the UI head handle the generated key events

#### Introspection Methods

Method:
- `ui.snapshot`

Result shape:
```json
{
  "content": { "... widget snapshot or null ..." },
  "popup": { "... first popup snapshot or null ..." },
  "popups": [
    { "... widget snapshot ..." }
  ],
  "focus": "demo.widgets.input"
}
```

Method:
- `ui.snapshotWidget`

Params:
```json
{
  "target": "demo.widgets.input"
}
```

Result shape:
```json
{
  "id": "demo.widgets.input",
  "type": "ElaraTextInputWidget",
  "visible": true,
  "bounds": {
    "x": 0,
    "y": 0,
    "width": 160,
    "height": 38
  },
  "absoluteBounds": {
    "x": 512,
    "y": 104,
    "width": 160,
    "height": 38
  },
  "state": {
    "text": "41",
    "placeholder": "type here later",
    "enabled": true
  },
  "children": []
}
```

Widget snapshot fields:
- `id`: widget handle string
- `type`: runtime widget class name
- `visible`: boolean
- `bounds`: widget-local bounds assigned by parent layout
- `absoluteBounds`: root-space bounds
- `state`: sparse widget-specific state object
- `children`: recursive child snapshots

Current sparse `state` keys may include:
- common logical keys:
  - `text`
  - `enabled`
  - `visible`
  - `placeholder`
  - `action`
  - `group`
  - `checked`
- tab state:
  - `activeTab`
  - `tabCount`
- list/tree state:
  - `selectedId`
  - `itemCount`
  - `expandedCount`
- slider/spinner state:
  - `orientation`
  - `min`
  - `max`
  - `value`
  - `step`
- chart state:
  - `axisCount`
  - `seriesCount`
- editor state:
  - `scrollX`
  - `scrollY`

Contract note:
- `state` is intentionally sparse
- absent keys mean “not applicable for this widget type”, not `null`

#### Outbound Event Notification Contract

Outbound notification method:
- `ui.event`

Notification params shape:
```json
{
  "target": "demo.widgets.button",
  "action": "clicked",
  "payload": {
    "button": 1,
    "x": 80,
    "y": 19
  }
}
```

Current payload schemas by action:
- `mouseMove`
```json
{ "x": 80, "y": 19 }
```
- `mouseDown`
- `mouseUp`
- `clicked`
```json
{ "button": 1, "x": 80, "y": 19 }
```
- `hoverChanged`
```json
{ "hovered": true }
```
- `keyDown`
- `keyUp`
```json
{ "keyval": 65 }
```
- `keysTyped`
```json
{ "text": "a" }
```
- `valueChanged`
```json
{ "value": 42.0 }
```

Notes:
- `target` is always the widget handle that emitted the event
- `action` is the subscribed event name
- `payload` is a JSON object for known actions and may be `null` for future cases

#### Error Contract

The current C++ implementation returns string error codes and messages rather than a large typed error hierarchy.

Common error codes:
- `missing_target`
- `widget_not_found`
- `missing_action`
- `missing_document`
- `unsupported_widget`
- `unsupported_operation`
- `replace_failed`
- `method_not_found`
- `load_failed`

Practical Python guidance:
- map JSON-RPC transport errors separately from Elara UI semantic errors
- preserve both `error_code` and `error_message` in any Python exception type
- do not rely on message text for logic

#### Minimal Python Client Shape

Recommended abstraction layers:
- `ElaraUiRpcConnection`
  - owns socket
  - handles JSON-RPC ids
  - runs background receive loop
- `ElaraUiClient`
  - exposes typed wrappers such as:
    - `load_document(document_text)`
    - `snapshot()`
    - `snapshot_widget(widget_id)`
    - `set_text(widget_id, value)`
    - `clear_children(widget_id)`
    - `replace_children(widget_id, subtree_json_text)`
    - `enable_event(name)`
- `ElaraUiEventRouter`
  - dispatches inbound `ui.event` notifications by widget id and action

Do not expose raw JSON assembly at every call site in Python. Keep the raw JSON-RPC contract contained in one module and present typed helpers everywhere else.

### Smoke Validation

- The standard framework smoke target is `make smoke`.
- `./build.sh --smoke` is the wrapper entrypoint that configures the framework and then runs the smoke target.
- Smoke validation currently covers:
  - `elara-project-builder` CLI contract checks
  - plain generated project
  - worker generated project
  - socket server generated project
  - socket client generated project
- If changing builder templates, install flow, lint policy, or generated project structure, run the smoke path before considering the change complete.

### Legacy Risk Flags

Treat these as legacy or partially broken until reviewed:
- no current entries

## libElaraCore

Primary role: string, memory, byte manipulation, lightweight containers, utility encoding.

### `<libelaracore/addressing/DynamicIdentifier.h>`

Namespace: `elara`

Type: `class DynamicIdentifier`

Purpose:
- hierarchical identifier with byte-encoded and dot-delimited forms

Key API:
- `DynamicIdentifier(const Memory &id)`
- `DynamicIdentifier(String id)`
- `int componentCount()`
- `int get(int index)`
- `Array<int> getIntArray()`
- `Memory getBytes()`
- `String getDelimitedStrting()`
- `void increment()`

Agent notes:
- the first component is stored as a single byte
- subsequent components use the existing 7-bit continuation encoding
- dot-delimited strings such as `1.2130437.6` round-trip through `getBytes()`

### `<libelaracore/memory/String.h>`

Namespace: `elara`

Type: `class String`

Key API:
- constructors from `const char *`, `char`, integral types, floating types, and copy-construction
- `ssize_t length()`
- `operator char *()`
- `String &operator=(String)`
- `String &operator+=(String)`
- `String operator+(String) const`
- `bool operator==(const String &) const`
- `void append(const String &)`
- `int indexOf(String search, int start=0)`
- `int occuranceCount(String search)`
- `String substr(int offset, int length=0)`
- `String &insert(int index, String ins)`
- `String &replace(String search, String replace, int offset=0, int maxcnt=0)`
- `bool equals(String str, bool case_insensitive)`
- `bool startsWith(String str)`
- `bool endsWith(String str)`
- `String &arg(String arg, const char *replace_marker=0)`
- `String &escape()`
- `String extract(String start, String end, int *offset=0)`
- `String trim(const char *tchrs=0)`
- `String toUpperCase()`
- `String toLowerCase()`
- `static String urlDecode(String str)`

Agent notes:
- use `trim()`, `startsWith()`, `endsWith()`, `replace()`, and `substr()` heavily in generated code
- prefer explicit `String("literal")` comparisons in conditionals

### `<libelaracore/memory/Memory.h>`

Namespace: `elara`

Type: `class Memory`

Key API:
- constructors for empty, sized, copied buffer, slice-copy, and copy-construction
- `Memory getMemory() const`
- `operator char *()`
- `operator String()`
- `char &operator[](size_t index)`
- `size_t length() const`
- `char *getPtr()`
- `char getByte(off_t index) const`
- `String toHex(bool uppercase)`
- `static Memory getRandomBytes(int count)`
- `void crop(int size)`
- `bool operator==(const Memory &)`

Agent notes:
- use for raw byte ownership
- equality is value-based
- `operator String()` treats memory as a C string boundary-sensitive conversion

### `<libelaracore/memory/ByteArray.h>`

Namespace: `elara`

Type: `class ByteArray`

Key API:
- constructors from raw bytes, `Memory`, and copy-construction
- `ssize_t length() const`
- conversion operators to `char *`, `const char *`, and `Memory`
- `append(Memory)`
- `append(const ByteArray &)`
- `append(const void *bytes, int len)`
- `append(int len)`
- `clear()`
- `int indexOf(ByteArray search, int start=0)`
- `int occuranceCount(ByteArray search)`
- `void shift(long bits)`
- `ByteArray subBytes(int offset, int length=0)`
- `ByteArray &insert(int index, const char *ins, size_t len)`
- `ByteArray &insert(int index, ByteArray ins)`
- `ByteArray &replace(ByteArray search, ByteArray replace, int offset=0, int maxcnt=0)`
- `String toHex()`
- `static ByteArray fromHex(String hex)`

### `<libelaracore/memory/Ref.h>`

Namespace: `elara`

Type: `template<class T> class Ref`

Key API:
- `explicit Ref(T *ptr)`
- `explicit Ref(T *ptr, bool array)`
- copy and move construction
- `operator*`, `operator->`, `explicit operator bool`
- `T &get()`
- `const T &get() const`
- `T *getPtr()`
- `const T *getPtr() const`
- copy and move assignment
- `void release()`

Agent notes:
- non-thread-safe
- for cross-thread ownership use `elara::threading::memory::Ref<T>`

### `<libelaracore/memory/RefArray.h>`

Namespace: `elara`

Type: `template<class T> class RefArray : public Ref<T>`

Purpose:
- same semantics as `Ref<T>` but deletes with `delete[]`

### `<libelaracore/memory/Array.h>`

Namespace: `elara`

Type: `template<class T> class Array`

Key API:
- `Array(size_t _size=0)`
- copy-construction and assignment
- `void insert(int index, T &obj)`
- `void remove(int index)`
- `void remove(T &obj)`
- `T &get(unsigned int index) const`
- `void push(const T &obj)`
- `T pop()`
- `void clear()`
- `int indexOf(T &obj)`
- `size_t length() const`
- `T &operator[](unsigned int index) const`

Agent notes:
- dynamically grows in chunks of 16
- `size()` implementation is suspect in current header and should not be relied on

### `<libelaracore/memory/LinkedList.h>`

Namespace: `elara`

Types:
- `template<class T> class LinkedList`
- `template<class T> class LinkedListState`

Key `LinkedList` API:
- `LINKEDLIST_NODE_HANDLE add(const T obj)`
- `LINKEDLIST_NODE_HANDLE firstNode() const`
- `LINKEDLIST_NODE_HANDLE lastNode() const`
- `LINKEDLIST_NODE_HANDLE nextNode(LINKEDLIST_NODE_HANDLE) const`
- `LINKEDLIST_NODE_HANDLE prevNode(LINKEDLIST_NODE_HANDLE) const`
- `T &get(LINKEDLIST_NODE_HANDLE) const`
- `T &get(int index) const`
- `void set(...)`
- `void remove(int index)`
- `void removeNode(LINKEDLIST_NODE_HANDLE node)`
- `void remove(const T &obj)`
- `void clear()`
- `void copy(...)`
- `void append(...)`
- `void insert(LINKEDLIST_NODE_HANDLE node, const T obj)`
- `void swap(...)`
- `int length() const`
- `Array<T> toArray()`

Agent notes:
- used heavily across older code
- node handles are opaque `void *`

### `<libelaracore/memory/StringList.h>`

Namespace: `elara`

Type: `class StringList`

Key API:
- default constructor
- split constructor `StringList(String &str, String delimiter, int limit=0)`
- copy constructor
- `String &operator[](unsigned int index) const`
- `size_t length() const`
- `void removeEmptyStrings()`
- `void append(String str)`

### `<libelaracore/memory/HashMap.h>`

Namespace: `elara`

Type: `template<class T> class HashMap`

Key API:
- trie-like map keyed by bytes or strings
- `void set(Memory map_key, Ref<T> newobj)`
- `void set(String map_key, T newobj)`
- `Ref<T> get(Memory &map_key) const`
- `void remove(Memory &map_key)`
- `void clear()`
- `LinkedList< Ref<MAPENTRY> > getEntries(Memory key_prefix) const`
- `LinkedList< Ref<T> > getObjects(Memory key_prefix) const`

Agent notes:
- string `set()` copies into `new T(newobj)`
- `get()` returns `Ref<T>`

### `<libelaracore/memory/RingBuffer.h>`

Namespace: `elara`

Type: `class RingBuffer`

Key API:
- `RingBuffer(size_t size)`
- `size_t size()`
- `size_t length()`
- `size_t freeSpace()`
- `size_t append(const char *data, size_t len)`
- `Memory fetch(size_t len)`
- `Memory getDataUntilEnd()`
- `void drop(int len)`

### `<libelaracore/parsing/RegularExpression.h>`

Namespace: `elara`

Type: `class RegularExpression`

Key API:
- constructor from expression string
- copy constructor
- `String &getExpression()`
- `bool match(String str)`

### `<libelaracore/encoding/Base58.h>`

Namespace: `elara`

Type: `class Base58`

Key API:
- `static Memory encode(Memory mem)`
- `static Memory decode(Memory mem)`

### `<libelaracore/encoding/Base64.h>`

Namespace: `elara`

Type: `class Base64`

Key API:
- `static Memory encode(Memory mem)`
- `static Memory decode(Memory mem)`

### `<libelaracore/utils/ByteUtils.h>`

Namespace: `elara`

Type: `class ByteUtils`

Key API:
- `static bool isNumber(char *str, ssize_t len=-1)`
- `static Ref<char> getRandomBytes(int length)`

### `<libelaracore/exception/Exception.h>`

Global namespace, not `elara`

Type: `class Exception`

Key API:
- `Exception(int err, const char *message)`
- `const char *getMessage()`
- `int getErrorCode()`

### `<libelaracore/types.h>`

Purpose:
- platform typedef aliases
- `thread_t`, `thread_mutex_t`, `thread_cond_t`, `thread_key_t`

### `<libelaracore/serializers/Serializable.h>`
### `<libelaracore/serializers/SerializableString.h>`

Namespace: `nrcore`

Purpose:
- explicit manual serialization declarations

Key `Serializable` API:
- `ByteArray serialize()`
- `void unserialize(const Memory &bytes)`
- protected declaration helpers:
  - `declareInt8`
  - `declareInt16`
  - `declareInt32`
  - `declareInt64`
  - `declareByteArray`
  - `declareSerializable`
  - `declareOther`
  - `setObjectLength`
  - `clearSerializationDeclarations`

Key `SerializableString` API:
- string subclass with serializable behavior

### Other Core Headers

These are installed but should be treated as legacy/specialized:
- `<libelaracore/memory/StaticArray.h>`
  - fixed-size `set` and `operator[]`

## libElaraThreads

Primary role: thread pool, task queue, locks, wait conditions, thread-safe shared refs.

### `<libelarathreads/Task.h>`

Namespace: `elara`

Type: `class Task`

Key API:
- constructors `Task()` and `Task(bool dynamicly_allocated)`
- static queue/pool helpers:
  - `taskExists`
  - `queueTask`
  - `removeTasks`
  - `getNextTask`
  - `getQueuedTaskCount`
  - `shuttingDown`
  - `staticInit`
  - `staticCleanup`
- `Thread *getAquiredThread()`
- `bool isFinished()`
- override point: `virtual void run() = 0`
- protected helpers:
  - `finished()`
  - `reset()`
  - `bool isDynamiclyAllocated()`
  - `unsigned long getThreadId()`

Agent notes:
- if you allocate a task with `new` and intend the runtime to own it, use the dynamic constructor variant

### `<libelarathreads/Thread.h>`

Namespace: `elara`

Type: `class Thread`

Key API:
- global pool knobs:
  - `static bool pool`
  - `static int max_threads`
- `static void init(int thread_count)`
- `static Thread *addThread()`
- `static Thread *runTask(elara::threading::memory::Ref<Task> task)`
- `static Thread *getWaitingThread()`
- `THREAD_STATUS getStatus()`
- `static void stopAllThreads()`
- `static void staticCleanUp()`
- `static int getCount()`
- `static int getWaitCount()`
- `void waitUntilFinished()`
- `void signal(int sig)`
- `static void waitForAnyAvailableThread()`
- `static Thread *getThreadInstance()`
- `static void getThreadPoolState(int *total, int *active)`
- `void queueTaskToCurrentThread(elara::threading::memory::Ref<Task> task);`
- `void wake()`

Agent notes:
- this is one of Elara’s flagship APIs
- pooled mode is controlled through static initialization rather than separate pool objects

### `<libelarathreads/Mutex.h>`

Namespace: `elara`

Type: `class Mutex`

Key API:
- nested RAII helper `Mutex::Lock`
- `Mutex(const char *tag=0, bool manage=true)`
- `bool lock(long timeout=0, const char *lock_tag=0)`
- `void wait(ThreadWaitCondition *cond, int usecs=0)`
- `bool tryLock(const char *lock_tag=0)`
- `void release()`
- `bool isLocked()`
- `bool isLockedByMe()`
- `thread_t getOwner()`
- `const char *tag()`
- `const char *lockTag()`
- `bool isManaged()`

### `<libelarathreads/ThreadWaitCondition.h>`

Namespace: `elara`

Type: `class ThreadWaitCondition`

Key API:
- `trigger()`
- `broadcast()`

### `<libelarathreads/Semaphore.h>`

Namespace: `elara`

Type: `class Semaphore`

Purpose:
- counting semaphore wrapper for thread coordination

### `<libelarathreads/TaskMutex.h>`

Namespace: `elara`

Type: `class TaskMutex : public Mutex`

Purpose:
- mutex variant tied into task/thread usage patterns

### `<libelarathreads/ThreadSafeObject.h>`

Namespace: `elara`

Purpose:
- mix-in style helper for encapsulated mutex-protected object state

### `<libelarathreads/HighResTimer.h>`

Namespace: `elara`

Type: `class HighResTimer : Task`

Purpose:
- task-based high-resolution timing helper

### `<libelarathreads/memory/Ref.h>`

Namespace: `elara::threading::memory`

Type: `template<class T> class Ref`

Key API:
- explicit constructors from raw pointer and optional array mode
- copy and move construction
- `operator*`, `operator->`, `explicit operator bool`
- `T &get()`

### `<libelarathreads/memory/InstancePool.h>`

Namespace: `elara::threading::memory`

Type: `template<class T> class InstancePool`

Purpose:
- thread-safe pool for scarce reusable server-side instances

Key API:
- `add(const T &obj)`
- `HANDLE acquire(bool block, long timeout_ms=0)`
- `const T &getInstance(HANDLE handle) const`
- `release(HANDLE handle)`
- `remove(const T &obj)`
- `const T &get() const`
- `T *getPtr()`
- `const T *getPtr() const`
- copy and move assignment
- `void release()`

Agent notes:
- atomic refcount
- safe for shared lifetime across threads
- does not protect the pointee’s internal mutable state

## libElaraIO

Primary role: low-level file and fd-based IO utilities.

### `<libelaraio/Stream.h>`

Namespace: `elara`

Type: `class Stream`

Key API:
- `Stream(int fd)`
- copy constructor
- `virtual ssize_t write(const char *buf, size_t sz)`
- `virtual ssize_t read(char *buf, size_t sz)`
- `virtual void close()`
- `int getFd()`
- `bool isValid()`

### `<libelaraio/File.h>`

Namespace: `elara`

Type: `class File : public Memory`

Key API:
- `File(const char *path)`
- `char &operator[](size_t index)`
- `Memory getMemory() const`
- `Memory getSubBytes(size_t offset, size_t length) const`
- `void write(size_t offset, const char *data, size_t length)`
- `Memory read(size_t offset, size_t length) const`
- `size_t length() const`
- `void setFileUpdating(bool val)`
- `void grow(size_t size)`
- `void truncate()`
- `int fileno()`

### `<libelaraio/FileStream.h>`

Namespace: `elara`

Type: `class FileStream : public Stream`

Key API:
- constructors from fd, filename, and copy-construction
- `void seek(off_t position)`
- `void seekEOF()`
- `off_t position()`
- `ssize_t write(const char *buf, size_t sz)`
- `ssize_t read(char *buf, size_t sz)`
- `off_t getfileSize()`
- `void flush()`
- `void close()`

### `<libelaraio/StringStreamReader.h>`

Namespace: `elara`

Type: `class StringStreamReader : public Task`

Key API:
- `StringStreamReader(Stream *stream)`
- `void runBlockingMode()`
- `void close()`
- override point: `virtual void onLineRead(const char *line) = 0`

### `<libelaraio/TextStream.h>`

Namespace: `elara`

Type: `class TextStream : public Stream`

Agent note:
- header itself marks this as incomplete

### `<libelaraio/IndexedDataStore.h>`

Namespace: `elara`

Type: `class IndexedDataStore`

Purpose:
- persistent indexed file store
- index topology durability is prioritized over payload-level validation

Construction rules:
- `IndexedDataStore(String path)` opens an existing store only
- `IndexedDataStore(String path, unsigned int bank_map_redundancy_count)` explicitly creates or resets a store
- do not assume opening a missing path will implicitly initialize a store anymore

Format notes:
- stores begin with a `STORE_HEADER`
- bank maps are stored as replicated `BANK_MAP_RECORD` entries
- each bank-map record contains:
  - `BANK_MAP`
  - `crc32`
- on load, the first valid bank-map replica by CRC is trusted
- bank-map redundancy count is stored in the header and controls how many sequential replicas are reserved and written

Key API:
- `IndexedDataStore(String path)`
- `IndexedDataStore(String path, unsigned int bank_map_redundancy_count)`
- `Ref<LOADED_FILE_DESCRIPTOR> createFile(Memory key, unsigned int block_size)`
- `Ref<LOADED_FILE_DESCRIPTOR> getFile(Memory key)`
- `Ref<LOADED_FILE_DESCRIPTOR> getOrCreateFile(Memory key, unsigned int block_size)`
- `bool writeToFile(Ref<LOADED_FILE_DESCRIPTOR> file, Memory data, unsigned long long offset, unsigned long long length)`
- `unsigned long long getFileSize(Ref<LOADED_FILE_DESCRIPTOR> file)`
- `Memory readFromFile(Ref<LOADED_FILE_DESCRIPTOR> file, unsigned long long offset, unsigned long long length)`
- `void set(Memory key, Memory value)`
- `Memory read(Memory key, unsigned int length)`
- `Memory readOrSet(Memory key, Memory default_value)`
- `bool convertDescriptorListToBankMap(Memory key)`
- `RefArray<int> getChildIndexes(Memory key)`
- `unsigned int getBankMapRedundancyCount() const`

Agent notes:
- if creating a brand-new store, create parent directories first
- if storage is layered, validate or checksum payload semantics one level above `IndexedDataStore`
- the bank-map replicas protect index readability; payload blocks are not currently CRC-protected at this layer

Purpose:
- indexed file-backed storage abstraction

Agent note:
- current implementation emits at least one serious compiler warning and should be used cautiously until reviewed

## libElaraDebug

Primary role: logging, test orchestration, test artifacts.

### `<libelaradebug/Log.h>`

Namespace: `elara`

Type: `class Log`

Key API:
- macros:
  - `LOG(level, format, ...)`
  - `C(x)`
- `addStream(Stream *stream, const char *format, const char *time_format, int log_level)`
- `removeStream(Stream *stream)`
- `setTimeStrFormat(const char *format)`
- `log(int log_level, const char *format, ...)`
- `va_log(int log_level, const char *format, va_list vars)`
- `log(const char *format, ...)`
- `va_log(const char *format, va_list vars)`
- `static void staticCleanUp()`
- global logger instance:
  - `extern Log logger`

### `<libelaradebug/UnitTests.h>`

Namespace: `elara`

Types:
- `typedef bool (*UNITTEST)()`
- `UNITTEST_ENTRY`
- `class UnitTests`

Key API:
- `UnitTests()`
- `void addTest(String name, UNITTEST cb)`
- `void addTests(RefArray< Ref<UNITTEST_ENTRY> > tests, int count)`
- `bool run()`
- `void setRunMode(String mode)`
- `void addRunMetadata(String key, String value)`
- `String getArtifactDirectory() const`
- `static bool fail(const char *msg)`

### `<libelaradebug/TestArtifactBuilder.h>`

Namespace: `elara`

Type: `class TestArtifactBuilder`

Purpose:
- structured artifact output for unit, stress, and valgrind runs

Key API:
- set root path and run mode
- add metadata
- start run
- record per-test result
- finish run
- get run directory

## libElaraEvent

Primary role: libevent-backed event loop integration.

### `<libelaraevent/EventBase.h>`

Namespace: `elara`

Type: `class EventBase : public Task`

Key API:
- `EventBase()`
- `void runEventLoop(bool create_task=false)`
- `void breakEventLoop()`
- `event_base *getEventBase()`
- `Thread *getThread()`

### `<libelaraevent/Timer.h>`

Namespace: `elara`

Type: `class Timer`

Key API:
- `Timer(EventBase *event_base)`
- `void start(int secs, int usecs)`
- `void stop()`
- override point: `virtual void onTick() = 0`

## libElaraFormat

Primary role: CSV and JSON parsing, optional XML support.

### `<libelaraformat/Csv.h>`

Namespace: `elara`

Type: `class Csv`

Key API:
- `bool loadData(String data)`
- `bool loadFile(String path)`
- `bool eof()`
- `StringList getEntry()`

### `<libelaraformat/json/Json.h>`

Namespace: `elara`

Type: `class Json`

Key API:
- constructors from JSON text, `JsonValue::TYPE`, existing `Ref<JsonValue>`, and copy-construction
- `JsonValue::TYPE getType() const`
- `Ref<JsonValue> getValue() const`
- `Ref<JsonValue> getJsonValue(String path) const`
- `String getStringValue(String path) const`
- `int getIntValue(String path) const`
- `Array< Ref<JsonValue> > getArray(String path) const`
- `bool setValue(String path, Ref<JsonValue> value)`
- `String toString()`

### `<libelaraformat/json/types/JsonValue.h>`

Namespace: `elara`

Type: `class JsonValue`

Key API:
- type enum:
  - `OBJECT`
  - `ARRAY`
  - `STRING`
  - `VALUE`
  - `INVALID`
- `virtual TYPE getType() const`
- `virtual String toString() const`
- `static TYPE getJsonType(String json)`
- `static Ref<JsonValue> getJsonValue(String json)`

### `<libelaraformat/json/types/JsonObject.h>`

Namespace: `elara`

Type: `class JsonObject : public JsonValue`

Key API:
- `bool parse(String json)`
- `Ref<JsonValue> getValue(String name)`
- `void addValue(String name, Ref<JsonValue> value)`
- `HashMap<JsonValue> getValues()`
- `String toString() const`

### `<libelaraformat/json/types/JsonArray.h>`

Namespace: `elara`

Type: `class JsonArray : public JsonValue`

Key API:
- `bool parse(String json)`
- `Array< Ref<JsonValue> > getArray()`
- `void addValue(Ref<JsonValue> value)`
- `String toString() const`

### Additional JSON Value Types

Headers:
- `<libelaraformat/json/types/JsonString.h>`
- `<libelaraformat/json/types/JsonNumber.h>`
- `<libelaraformat/json/types/JsonInvalid.h>`

Purpose:
- concrete scalar and invalid-node implementations of `JsonValue`

### Optional XML

Header:
- `<libelaraformat/Xml.h>`

Agent note:
- only available when XML support was enabled at configure time

## libElaraEncryption

Primary role: cipher wrappers, hashes, and signers.

### `<libelaraencryption/base/Cipher.h>`

Namespace: `elara`

Type: `class Cipher`

Key API:
- `virtual void setKey(const Memory &key, const Memory &iv)`
- `virtual Memory encrypt(const char *buf, int len) = 0`
- `virtual Memory decrypt(const char *buf, int len) = 0`
- `virtual int getBlockSize() = 0`

### `<libelaraencryption/base/Signer.h>`

Namespace: `elara`

Type: `class Signer`

Key API:
- `virtual Memory sign(Memory hash) = 0`
- `virtual bool verify(Memory hash, Memory signiture) = 0`
- `virtual int getBlockSize() = 0`

### `<libelaraencryption/Hash.h>`

Namespace: `elara`

Type: `class Hash`

Key API:
- `virtual size_t length() = 0`
- `virtual unsigned char *get() = 0`

### `<libelaraencryption/Aes.h>`

Namespace: `elara`

Type: `class Aes : public Cipher`

Key API:
- `Aes(const Memory &key, const Memory &iv)`
- `void setKey(const Memory &key, const Memory &iv)`
- `Memory encrypt(const char *buf, int len)`
- `Memory decrypt(const char *buf, int len)`
- `void encryptBlock(const char *dat_in, char *dat_out)`
- `void decryptBlock(const char *dat_in, char *dat_out)`
- `int getBlockSize()`
- `void reset()`

### `<libelaraencryption/Rsa.h>`

Namespace: `elara`

Type: `class Rsa : public Cipher`

Key API:
- constructors from certificate and optional private key in `PEM` or `DER`
- `Memory encrypt(const char *buf, int len)`
- `Memory decrypt(const char *buf, int len)`
- `int getBlockSize()`
- `Memory getCertificateBytes()`
- `bool validate()`

Agent notes:
- OpenSSL 3 deprecation warnings may appear in current builds

### `<libelaraencryption/Sha256.h>`
### `<libelaraencryption/Ripemd160.h>`
### `<libelaraencryption/Keccak256.h>`

Namespace: `elara`

Pattern:
- hash object with `update`, `final`, `length`, `get`, and `reset`

### `<libelaraencryption/Secp256k1Signer.h>`

Namespace: `elara`

Type: `class Secp256k1Signer : public Signer`

Key API:
- default constructor and constructor from private key
- `bool setPublicKey(Memory &public_key)`
- `Memory getPublicKey(bool compressed)`
- `Memory sign(Memory hash)`
- `bool verify(Memory hash, Memory signiture)`
- `int getBlockSize()`

### Additional Cipher Headers

Headers:
- `<libelaraencryption/Serpent.h>`
- `<libelaraencryption/twofish.h>`
- `<libelaraencryption/twofish/twofish2.h>`

Purpose:
- alternative cipher implementations

Agent note:
- use AES unless a human explicitly requests a different cipher

## libElaraSockets

Primary role: event-driven sockets, listeners, SOCKS5 components.

### `<libelarasockets/memory/DescriptorInstanceMap.h>`

Namespace: `elara::sockets::memory`

Type: `template<class T> class DescriptorInstanceMap`

Purpose:
- descriptor-indexed storage for socket-side instance lookup

Key API:
- `set(unsigned long fd, T instance)`
- `equals(unsigned long fd, T instance)`
- `get(unsigned long fd)`
- `getMaxDescriptors()`

### `<libelarasockets/Address.h>`

Namespace: `elara`

Type: `class Address`

Key API:
- `Address(ADDRESS_TYPE type, const char *bytes)`
- copy constructor
- `ADDRESS_TYPE getType()`
- `int getAddrSize()`
- `const char *getAddr()`
- `static ADDRESS_TYPE getType(const char *addr)`

Address types:
- `IPV4`
- `IPV6`
- `ADDR`
- `DOMAIN`
- `MAC`

### `<libelarasockets/Socket.h>`

Namespace: `elara`

Type: `class Socket`

Key API:
- constructors from fd or callback interface
- `bool connect(Address address, unsigned short port)`
- `void poll()`
- `size_t available()`
- `Memory read(int max)`
- `size_t writeBufferSpace()`
- `int send(Memory data)`
- `int send(const char *buffer, size_t len)`
- `void setCallbackInterface(CallbackInterface *cb)`
- `void close()`
- `static void init(EventBase *event_base)`

Subclass requirements:
- implement `onReceive()`
- implement `onWriteReady()`

Callback interface hooks:
- `onConnected`
- `onClosed`
- `onDestroyed`

### `<libelarasockets/Listener.h>`

Namespace: `elara`

Type: `class Listener : public Task`

Key API:
- constructors for default or bound listener creation
- `void listen(int listen_port, int opts, unsigned int interface=INADDR_ANY, const in6_addr *ipv6_interface=&in6addr_any, EventBase *event_base=0)`
- `void stop()`
- `void runEventLoop(bool create_task=false)`
- `void breakEventLoop()`
- `EventBase *getEventBase()`

Subclass requirement:
- implement `onNewConnection(EventBase *event_base, int fd, unsigned char *addr, int addr_sz)`

Flags:
- `LISTENER_OPTS_IPV4`
- `LISTENER_OPTS_IPV4_REQUIRED`
- `LISTENER_OPTS_IPV6`
- `LISTENER_OPTS_IPV6_REQUIRED`

### SOCKS5 Headers

Headers:
- `<libelarasockets/socks5/Socks5Server.h>`
- `<libelarasockets/socks5/ClientSocket.h>`
- `<libelarasockets/socks5/Scoks5Client.h>`
- `<libelarasockets/socks5/Structures.h>`
- `<libelarasockets/socks5/BlockLoaders/UsernamePasswordLoader.h>`
- `<libelarasockets/utils/BlockLoaderBase.h>`

Purpose:
- prebuilt SOCKS5 protocol server/client pieces

Agent note:
- prefer plain `Socket` and `Listener` for new protocol work unless SOCKS5 is explicitly required

## libElaraData

Primary role: SQL connector abstraction, query results, schema/migration helpers.

Namespace warning:
- public headers still use `namespace nrcore`

Feature warning:
- MySQL and SQLite support are individually optional at configure time

### `<libelaradata/connectors/Connector.h>`

Namespace: `nrcore`

Type: `class Connector`

Key API:
- `void setConnection(void *connection)`
- `void *getConnection()`
- `Ref<Schemas> schemas()`
- `virtual Ref<Builder> getBuilder(String table) = 0`
- `virtual void createDatabase(String name) = 0`
- `virtual void dropDatabase(String name) = 0`
- `virtual bool tableExists(String table) = 0`
- `virtual void execute(String sql) = 0`
- `virtual ResultSet query(String sql) = 0`
- `virtual unsigned int lastInsertId() = 0`

### `<libelaradata/result/ResultSet.h>`

Namespace: `nrcore`

Type: `class ResultSet`

Key API:
- `void addRow(Array<Memory> fields)`
- `Row row(unsigned int offset)`
- `Row first()`
- `Row last()`
- `Row *next()`
- `int getColumnIndex(String name)`
- `size_t length()`

### `<libelaradata/result/Row.h>`

Namespace: `nrcore`

Type: `class Row`

Key API:
- typed getters by index or column name:
  - `getString`
  - `getInteger`
  - `getUnsignedInteger`
  - `getDouble`
  - `getBlob`

### `<libelaradata/models/Model.h>`

Namespace: `nrcore`

Type: `class Model`

Purpose:
- base model with revisioned migration flow

Key protected API:
- override `revision()`
- override `migrate(int revision)`
- `loadRevision()`
- `runMigration()`
- `Builder *getBuilder()`
- `int lastInsertId()`

### `<libelaradata/models/Schemas.h>`

Namespace: `nrcore`

Purpose:
- schema inspection/management attached to a `Connector`

### SQL Builder Headers

Headers:
- `<libelaradata/sql/builders/Builder.h>`
- `<libelaradata/sql/sections/sql/Clause.h>`
- `<libelaradata/sql/sections/sql/ClauseGroup.h>`
- `<libelaradata/sql/sections/sql/ClauseValue.h>`
- `<libelaradata/sql/sections/sql/FieldDescriptor.h>`
- `<libelaradata/sql/sections/sql/Fields.h>`
- `<libelaradata/sql/sections/sql/Join.h>`
- `<libelaradata/sql/sections/sql/OffsetLimit.h>`
- `<libelaradata/sql/sections/sql/Order.h>`
- `<libelaradata/sql/sections/sql/Values.h>`

Purpose:
- explicit SQL AST/builder layer

Agent note:
- use these only when a human wants fluent SQL generation instead of raw SQL strings

## libElaraSerializer

Primary role: explicit object serialization helpers separate from core.

Namespace warning:
- public headers still use `namespace nrcore`

Headers:
- `<libelaraserializer/Serializable.h>`
- `<libelaraserializer/SerializableString.h>`
- `<libelaraserializer/LinkedList.h>`

Agent note:
- current staged headers mirror the older serializer API rather than a modernized `elara` namespace layer
- if you need explicit serialization today, verify whether the project wants `libElaraSerializer` or the older `libElaraCore/serializers` headers

## Quick Recommendations For Agents

Prefer these building blocks first:
- `elara::String`
- `elara::Memory`
- `elara::ByteArray`
- `elara::Array<T>`
- `elara::LinkedList<T>`
- `elara::Ref<T>`
- `elara::threading::memory::Ref<T>` for cross-thread ownership
- `elara::Task`, `elara::Thread`, `elara::Mutex`
- `elara::File`, `elara::Stream`, `elara::FileStream`
- `elara::Json`
- `elara::Socket`, `elara::Listener`

Avoid in new code unless explicitly requested:
- legacy object/pool headers with missing dependencies
- serializer/data namespace assumptions
- implicit raw pointer ownership without `Ref`
- `std::string` in public Elara-facing APIs

## Minimal Include Cheat Sheet

- strings: `<libelaracore/memory/String.h>`
- raw bytes: `<libelaracore/memory/Memory.h>`
- byte builder: `<libelaracore/memory/ByteArray.h>`
- shared ref: `<libelaracore/memory/Ref.h>`
- thread-safe shared ref: `<libelarathreads/memory/Ref.h>`
- task/thread pool: `<libelarathreads/Task.h>`, `<libelarathreads/Thread.h>`
- locking: `<libelarathreads/Mutex.h>`, `<libelarathreads/ThreadWaitCondition.h>`
- files/fds: `<libelaraio/File.h>`, `<libelaraio/Stream.h>`, `<libelaraio/FileStream.h>`
- logging: `<libelaradebug/Log.h>`
- event loop: `<libelaraevent/EventBase.h>`, `<libelaraevent/Timer.h>`
- json: `<libelaraformat/json/Json.h>`
- sockets: `<libelarasockets/Address.h>`, `<libelarasockets/Socket.h>`, `<libelarasockets/Listener.h>`
