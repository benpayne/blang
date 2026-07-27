// Todo web app — a REST API backed by SQLite, with an embedded static frontend.
//
// Showcases the data layer end to end: a `table struct` stored in SQLite,
// `insert`/`update`/`delete` with bound parameters, and `query Todo` mapped
// back into an `Array<Todo>`, all served over HTTP and serialized with @json.
//
// Build & run:
//   cd examples/todo_app && bcc build && bcc migrate --apply && ./todo_app
// then open http://localhost:8080
//
// `bcc migrate --apply` creates the SQLite schema directly from the `Todo`
// table struct below — no hand-written CREATE TABLE needed.

import net;

// A single struct used both as the SQLite-backed table (`table`) and as the
// JSON wire format (`@json`). `bcc migrate` derives the schema from this.
@json
table struct Todo {
	int id;
	string title;
	bool done;
}

// The single-page frontend, served as static text/html. Uses single-quoted
// attributes/strings so the BLang string literal needs no escaping, and avoids
// bare {identifier} braces so string interpolation leaves the markup intact.
fn index_html() -> string {
	return "<!doctype html>\n"
		+ "<html lang='en'>\n"
		+ "<head>\n"
		+ "<meta charset='utf-8'>\n"
		+ "<meta name='viewport' content='width=device-width, initial-scale=1'>\n"
		+ "<title>BLang Todos</title>\n"
		+ "<style>\n"
		+ "  * { box-sizing: border-box; }\n"
		+ "  body { font-family: system-ui, sans-serif; max-width: 32rem; margin: 3rem auto; padding: 0 1rem; color: #1a1a2e; }\n"
		+ "  h1 { font-size: 1.6rem; }\n"
		+ "  form { display: flex; gap: .5rem; margin-bottom: 1.5rem; }\n"
		+ "  input[type=text] { flex: 1; padding: .6rem .8rem; border: 1px solid #ccc; border-radius: .5rem; font-size: 1rem; }\n"
		+ "  button { padding: .6rem 1rem; border: 0; border-radius: .5rem; background: #4361ee; color: #fff; font-size: 1rem; cursor: pointer; }\n"
		+ "  button.secondary { background: #e0e0e0; color: #333; padding: .3rem .6rem; }\n"
		+ "  ul { list-style: none; padding: 0; }\n"
		+ "  li { display: flex; align-items: center; gap: .6rem; padding: .6rem .2rem; border-bottom: 1px solid #eee; }\n"
		+ "  li span { flex: 1; }\n"
		+ "  li.done span { text-decoration: line-through; color: #999; }\n"
		+ "  .empty { color: #999; font-style: italic; }\n"
		+ "</style>\n"
		+ "</head>\n"
		+ "<body>\n"
		+ "  <h1>BLang Todos</h1>\n"
		+ "  <form id='add-form'>\n"
		+ "    <input id='title' type='text' placeholder='What needs doing?' autocomplete='off' required>\n"
		+ "    <button type='submit'>Add</button>\n"
		+ "  </form>\n"
		+ "  <ul id='list'></ul>\n"
		+ "<script>\n"
		+ "const list = document.getElementById('list');\n"
		+ "const form = document.getElementById('add-form');\n"
		+ "const titleInput = document.getElementById('title');\n"
		+ "async function load() {\n"
		+ "  const res = await fetch('/todos');\n"
		+ "  render(await res.json());\n"
		+ "}\n"
		+ "function render(todos) {\n"
		+ "  list.innerHTML = '';\n"
		+ "  if (!todos.length) {\n"
		+ "    const li = document.createElement('li');\n"
		+ "    li.className = 'empty';\n"
		+ "    li.textContent = 'No todos yet.';\n"
		+ "    list.appendChild(li);\n"
		+ "    return;\n"
		+ "  }\n"
		+ "  for (const t of todos) {\n"
		+ "    const li = document.createElement('li');\n"
		+ "    if (t.done) li.className = 'done';\n"
		+ "    const cb = document.createElement('input');\n"
		+ "    cb.type = 'checkbox';\n"
		+ "    cb.checked = t.done;\n"
		+ "    cb.onchange = () => toggle(t);\n"
		+ "    const span = document.createElement('span');\n"
		+ "    span.textContent = t.title;\n"
		+ "    const del = document.createElement('button');\n"
		+ "    del.className = 'secondary';\n"
		+ "    del.textContent = 'Delete';\n"
		+ "    del.onclick = () => remove(t.id);\n"
		+ "    li.appendChild(cb);\n"
		+ "    li.appendChild(span);\n"
		+ "    li.appendChild(del);\n"
		+ "    list.appendChild(li);\n"
		+ "  }\n"
		+ "}\n"
		+ "form.onsubmit = async (e) => {\n"
		+ "  e.preventDefault();\n"
		+ "  const title = titleInput.value.trim();\n"
		+ "  if (!title) return;\n"
		+ "  const res = await fetch('/todos', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ title: title }) });\n"
		+ "  titleInput.value = '';\n"
		+ "  render(await res.json());\n"
		+ "};\n"
		+ "async function toggle(t) {\n"
		+ "  const res = await fetch('/todos/' + t.id, { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ done: !t.done }) });\n"
		+ "  render(await res.json());\n"
		+ "}\n"
		+ "async function remove(id) {\n"
		+ "  const res = await fetch('/todos/' + id, { method: 'DELETE' });\n"
		+ "  render(await res.json());\n"
		+ "}\n"
		+ "load();\n"
		+ "</script>\n"
		+ "</body>\n"
		+ "</html>\n";
}


// Serialize every todo as a JSON array: [ {..}, {..} ].
fn todos_json() -> string {
	Array<Todo> todos = query Todo |> order_by { .id };
	string out = "[";
	int i = 0;
	for t in todos {
		if i > 0 {
			out = out + ",";
		}
		out = out + to_json(t);
		i = i + 1;
	}
	out = out + "]";
	return out;
}

// Extract the trailing id from a path like "/todos/42".
fn path_id(string path) -> int {
	int idx = path.index_of("/todos/");
	if idx < 0 {
		return -1;
	}
	string rest = path.substring(idx + 7, path.length);
	return rest.to_int();
}

// Single catch-all handler that implements the REST routes by inspecting the
// request method and path (the router matches exact paths only, so path
// parameters like /todos/{id} are parsed here).
fn handle(net.HttpRequest req) -> net.HttpResponse {
	string p = req.path;
	string m = req.method;

	// Static frontend.
	if p == "/" {
		return net.http_response(200, "text/html", index_html());
	}

	// Collection: list + create.
	if p == "/todos" {
		if m == "GET" {
			return net.http_json(todos_json());
		}
		if m == "POST" {
			Todo input = Todo_from_json(req.body);
			insert Todo { title: input.title, done: false };
			return net.http_json(todos_json());
		}
	}

	// Item: fetch (GET) + toggle (PUT) + remove (DELETE).
	// The item is looked up once with `|> first`: an unknown id is a real 404
	// instead of a blind update/delete that silently affects zero rows.
	if p.starts_with("/todos/") {
		int id = path_id(p);
		Option<Todo> found = query Todo |> where { .id == id } |> first;
		match found {
			some(t) {
				if m == "GET" {
					return net.http_json(to_json(t));
				}
				if m == "PUT" {
					Todo input = Todo_from_json(req.body);
					bool nd = input.done;
					update Todo |> where { .id == id } |> set { .done = nd };
					return net.http_json(todos_json());
				}
				if m == "DELETE" {
					delete Todo |> where { .id == id };
					return net.http_json(todos_json());
				}
			}
			none {
				return net.http_not_found();
			}
		}
	}

	return net.http_not_found();
}

fn main() -> int {
	// The default connection is opened at startup from blang.toml's [database]
	// section. The schema is created beforehand by `bcc migrate --apply`, so the
	// app contains no table-creation SQL.
	net.HttpServer server = net.http_server("0.0.0.0", 8080);
	server.on_request(handle);
	println("Todo app listening on http://localhost:8080");
	server.join();
	return 0;
}
