# Todo App — BLang integration example

A small but complete web app written entirely in BLang: a REST API backed by
SQLite with an embedded single-page frontend. It exercises the data layer
end-to-end and doubles as an integration test for these features:

- **`table struct`** stored in SQLite (`Todo`)
- **`insert` / `update` / `delete`** with bound parameters
- **`query Todo |> order_by { .id }`** mapped back into an `Array<Todo>`
- **`query Todo |> where { .id == id } |> first`** returning `Option<Todo>` —
  the item routes look the todo up once and `match` on it, so an unknown id is
  a real 404 (`none`) instead of a blind update/delete of zero rows
- **`@json`** serialization (`to_json` / `Todo_from_json`) over the wire
- **HTTP stdlib** (`import net;`) serving both the JSON API and static HTML
- **`[database]`** configuration in `blang.toml` (the default connection is
  opened automatically at startup)

## Run

```sh
cd examples/todo_app
bcc build              # compile the app
bcc migrate --apply    # create the SQLite schema from the Todo table struct
./todo_app
# open http://localhost:8080
```

`bcc build` reads `blang.toml`, combines `main.b` with the `net` stdlib module,
and links the SQLite-backed database runtime. `bcc migrate --apply` derives the
schema directly from the `table struct Todo` and creates the table in `todos.db`
— there is **no hand-written `CREATE TABLE`** in the app. Use
`bcc migrate --preview` to see the SQL it would run. Todos persist in `todos.db`.

## REST API

| Method | Path           | Body                  | Description              |
|--------|----------------|-----------------------|--------------------------|
| GET    | `/`            | —                     | Serves the frontend HTML |
| GET    | `/todos`       | —                     | List all todos (JSON)    |
| POST   | `/todos`       | `{"title": "..."}`    | Create a todo            |
| PUT    | `/todos/{id}`  | `{"done": true}`      | Update done state        |
| DELETE | `/todos/{id}`  | —                     | Delete a todo            |

All mutating endpoints return the full, updated todo list as JSON.

## Test

```sh
./test_todo_app.sh
```

Builds the app, starts the server, drives the full CRUD cycle with `curl`,
checks 404s (unknown route and unknown item ids on GET/PUT/DELETE, via the
`|> first` lookup) and the served frontend, and verifies the data survives a
restart (SQLite persistence).

## Notes

- The router matches exact paths, so `/todos/{id}` is parsed in the catch-all
  handler. Path-parameter routing is a natural future stdlib addition.
- `Todo` is both a `table struct` and `@json`, so the same type is the database
  row and the JSON wire format.
