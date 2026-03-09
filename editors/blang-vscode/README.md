# BLang for VS Code / Cursor

Syntax highlighting and language support for the BLang programming language.

## Features

- Syntax highlighting for all BLang keywords, types, and operators
- String interpolation highlighting (`"hello {name}"`)
- Annotation highlighting (`@json`, `@grpc("service")`)
- Comment toggling (line and block)
- Bracket matching and auto-closing
- Auto-indentation on `{` / `}`

## Installation

### Quick install (symlink)

Create a symlink from the VS Code / Cursor extensions directory to this folder:

```bash
# For Cursor
ln -s "$(pwd)" ~/.cursor/extensions/blang

# For VS Code
ln -s "$(pwd)" ~/.vscode/extensions/blang
```

Then restart the editor. All `.b` files will automatically use BLang highlighting.

### Manual install (copy)

```bash
# For Cursor
cp -r . ~/.cursor/extensions/blang

# For VS Code
cp -r . ~/.vscode/extensions/blang
```

### Package as VSIX (for distribution)

```bash
npm install -g @vscode/vsce
vsce package
# Produces blang-0.1.0.vsix
# Install: code --install-extension blang-0.1.0.vsix
```

## Highlighted Syntax Elements

| Element | Examples |
|---------|---------|
| Keywords | `fn`, `struct`, `enum`, `protocol`, `impl`, `if`, `else`, `for`, `while`, `match`, `return`, `spawn`, `async`, `await` |
| Types | `int`, `float`, `string`, `bool`, `void`, `Array`, `Map`, `Result`, `Option` |
| Ownership | `own`, `shared`, `sync` |
| Contracts | `requires`, `ensures`, `assert` |
| Queries | `query`, `insert`, `update`, `delete` |
| Operators | `->`, `..`, `\|>`, `?`, `==`, `&&` |
| Annotations | `@json`, `@grpc("service")` |
| Constants | `true`, `false`, `_` |
| Strings | `"hello {name}"` with interpolation |
