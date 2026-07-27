// Example #2 — an arithmetic expression interpreter (calculator).
//
// A classic three-stage interpreter written in BLang:
//   tokenize  -> Array<Token>
//   parse     -> Ast            (a real recursive expression tree)
//   eval_ast  -> Result<int, string>
//
// The Ast is a RECURSIVE enum — variants carry Ast children directly
// (`add(Ast, Ast)`), and match destructuring binds both children
// (`add(l, r)`). It exercises a broad slice of the language: recursive enums
// with multi-binding destructuring, the `?` try operator over Result, a small
// mutable Parser struct threaded by reference, string/char scanning, and
// forward references. The `test` blocks at the bottom run under `bcc test`.
//
// Grammar (standard precedence, left-associative):
//   expr   = term (("+" | "-") term)*
//   term   = factor (("*" | "/") factor)*
//   factor = NUMBER | "(" expr ")" | "-" factor

enum Token {
	num(int),
	plus,
	minus,
	star,
	slash,
	lparen,
	rparen,
	end
}

// The expression tree. Enum-typed payloads are heap-boxed by the compiler,
// so the recursion has finite layout and is managed by ARC.
enum Ast {
	num(int),
	add(Ast, Ast),
	sub(Ast, Ast),
	mul(Ast, Ast),
	div(Ast, Ast),
	neg(Ast)
}

// A mutable cursor over the token stream. Structs are heap references, so
// field writes made through a passed-in Parser persist for the caller — this
// is how `pos` advances (and parse errors propagate) across the recursive
// parse functions. Parse errors live here because the parser returns Ast
// values; the first failure wins.
struct Parser {
	Array<Token> toks;
	int pos;
	bool failed;
	string error;
}

fn main() -> int {
	demo("1 + 2 * 3");
	demo("(1 + 2) * 3");
	demo("10 - 2 - 3");
	demo("-5 + 8");
	demo("2 * (3 + 4) - 1");
	demo("100 / 5 / 2");
	demo("1 / 0");
	demo("2 +");
	demo("(1 + 2");
	return 0;
}

// Evaluate one expression and print the result (or the error).
fn demo(string expr) {
	Result<int, string> r = eval(expr);
	match r {
		ok(v) {
			println("{} = {}", expr, v);
		}
		err(e) {
			println("{} -> error: {}", expr, e);
		}
	}
}

// Tie the stages together: tokenize -> parse to an Ast -> evaluate the tree.
fn eval(string src) -> Result<int, string> {
	Array<Token> toks = tokenize(src)?;
	Parser p = Parser { toks: toks, pos: 0, failed: false, error: "" };
	Ast tree = parse_expr(p);
	if p.failed {
		return Result.err(p.error);
	}

	// After a full expression the only token left should be `end`.
	Token rest = peek(p);
	match rest {
		end {
			return eval_ast(tree);
		}
		_ {
			return Result.err("trailing tokens after expression");
		}
	}
}

// --- Tokenizer -------------------------------------------------------------

fn tokenize(string s) -> Result<Array<Token>, string> {
	Array<Token> toks = [];
	int i = 0;
	int n = s.length;

	for {
		if i >= n {
			toks.push(Token.end);
			return Result.ok(toks);
		}

		char c = s[i];
		if c == ' ' || c == '\t' {
			i = i + 1;
		} else if c >= '0' && c <= '9' {
			// Accumulate a multi-digit integer.
			int value = 0;
			while i < n && s[i] >= '0' && s[i] <= '9' {
				value = value * 10 + (s[i] - '0');
				i = i + 1;
			}
			toks.push(Token.num(value));
		} else if c == '+' {
			toks.push(Token.plus);
			i = i + 1;
		} else if c == '-' {
			toks.push(Token.minus);
			i = i + 1;
		} else if c == '*' {
			toks.push(Token.star);
			i = i + 1;
		} else if c == '/' {
			toks.push(Token.slash);
			i = i + 1;
		} else if c == '(' {
			toks.push(Token.lparen);
			i = i + 1;
		} else if c == ')' {
			toks.push(Token.rparen);
			i = i + 1;
		} else {
			return Result.err("unexpected character");
		}
	}
}

// --- Cursor helpers --------------------------------------------------------

// Current token without consuming it. `end` is always the last token, so this
// is safe to call repeatedly at the end of input.
fn peek(Parser p) -> Token {
	return p.toks[p.pos];
}

// Consume and return the current token, stopping at the final `end`.
fn advance(Parser p) -> Token {
	Token t = p.toks[p.pos];
	if p.pos < p.toks.length - 1 {
		p.pos = p.pos + 1;
	}
	return t;
}

// Record a parse error on the parser and return a placeholder node. The
// caller chain checks p.failed and unwinds; eval() reports p.error.
fn fail(Parser p, string msg) -> Ast {
	p.failed = true;
	p.error = msg;
	return Ast.num(0);
}

// --- Recursive-descent parser (builds the Ast) -----------------------------

fn parse_expr(Parser p) -> Ast {
	Ast left = parse_term(p);
	if p.failed {
		return left;
	}
	for {
		Token t = peek(p);
		match t {
			plus {
				advance(p);
				Ast right = parse_term(p);
				if p.failed {
					return left;
				}
				left = Ast.add(left, right);
			}
			minus {
				advance(p);
				Ast right = parse_term(p);
				if p.failed {
					return left;
				}
				left = Ast.sub(left, right);
			}
			_ {
				return left;
			}
		}
	}
}

fn parse_term(Parser p) -> Ast {
	Ast left = parse_factor(p);
	if p.failed {
		return left;
	}
	for {
		Token t = peek(p);
		match t {
			star {
				advance(p);
				Ast right = parse_factor(p);
				if p.failed {
					return left;
				}
				left = Ast.mul(left, right);
			}
			slash {
				advance(p);
				Ast right = parse_factor(p);
				if p.failed {
					return left;
				}
				left = Ast.div(left, right);
			}
			_ {
				return left;
			}
		}
	}
}

fn parse_factor(Parser p) -> Ast {
	Token t = peek(p);
	match t {
		num(value) {
			advance(p);
			return Ast.num(value);
		}
		minus {
			advance(p);
			Ast inner = parse_factor(p);
			if p.failed {
				return inner;
			}
			return Ast.neg(inner);
		}
		lparen {
			advance(p);
			Ast inner = parse_expr(p);
			if p.failed {
				return inner;
			}
			Token close = peek(p);
			match close {
				rparen {
					advance(p);
					return inner;
				}
				_ {
					return fail(p, "expected closing paren");
				}
			}
		}
		_ {
			return fail(p, "expected number, unary minus, or paren");
		}
	}
	return fail(p, "unreachable");
}

// --- Tree evaluator --------------------------------------------------------

// Walk the Ast. Runtime errors (division by zero) surface as Result.err and
// propagate out of the recursion with `?`.
fn eval_ast(Ast a) -> Result<int, string> {
	match a {
		num(n) {
			return Result.ok(n);
		}
		add(l, r) {
			int lv = eval_ast(l)?;
			int rv = eval_ast(r)?;
			return Result.ok(lv + rv);
		}
		sub(l, r) {
			int lv = eval_ast(l)?;
			int rv = eval_ast(r)?;
			return Result.ok(lv - rv);
		}
		mul(l, r) {
			int lv = eval_ast(l)?;
			int rv = eval_ast(r)?;
			return Result.ok(lv * rv);
		}
		div(l, r) {
			int lv = eval_ast(l)?;
			int rv = eval_ast(r)?;
			if rv == 0 {
				return Result.err("division by zero");
			}
			return Result.ok(lv / rv);
		}
		neg(x) {
			int v = eval_ast(x)?;
			return Result.ok(0 - v);
		}
	}
	return Result.err("unreachable");
}

// --- Tests (run with `bcc test`) -------------------------------------------

// Test helpers: unwrap a Result so assertions stay simple, using
// match-as-expression (each arm yields a single value).
fn eval_equals(string src, int expected) -> bool {
	Result<int, string> r = eval(src);
	return match r {
		ok(v) { v == expected }
		err(e) { false }
	};
}

fn eval_is_error(string src) -> bool {
	Result<int, string> r = eval(src);
	return match r {
		ok(v) { false }
		err(e) { true }
	};
}

test "single number" {
	assert eval_equals("42", 42);
}

test "addition and precedence" {
	assert eval_equals("1 + 2 * 3", 7);
	assert eval_equals("2 * 3 + 1", 7);
}

test "parentheses override precedence" {
	assert eval_equals("(1 + 2) * 3", 9);
	assert eval_equals("2 * (3 + 4) - 1", 13);
}

test "left associative subtraction and division" {
	assert eval_equals("10 - 2 - 3", 5);
	assert eval_equals("100 / 5 / 2", 10);
}

test "unary minus" {
	assert eval_equals("-5 + 8", 3);
	assert eval_equals("-(2 + 3)", -5);
}

test "whitespace is ignored" {
	assert eval_equals("  7   *   6 ", 42);
}

test "multi digit numbers" {
	assert eval_equals("123 + 456", 579);
}

test "division by zero is an error" {
	assert eval_is_error("1 / 0");
}

test "unbalanced parenthesis is an error" {
	assert eval_is_error("(1 + 2");
}

test "trailing operator is an error" {
	assert eval_is_error("2 +");
}

test "empty input is an error" {
	assert eval_is_error("");
}

test "ast built by hand evaluates directly" {
	Ast t = Ast.mul(Ast.add(Ast.num(1), Ast.num(2)), Ast.num(3));
	Result<int, string> r = eval_ast(t);
	assert match r { ok(v) { v == 9 } err(e) { false } };
}
