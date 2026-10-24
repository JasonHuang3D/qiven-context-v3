"""The lexer of the structure rules: a C++ source's tokens, outside comments
and literals, its literals' values, read with their escapes, ordinary,
prefixed and raw, and its comments. A header name after #include is no
literal. rules.py's rules read what it gives; it reads no regular expression.
"""

# The lexer: tokens (kind, value, line) outside comments, and the comments.
PUNCTUATORS = ("<=>", "->*", "...", "<<=", ">>=", "::", "->", "++", "--", "<<", ">>", "<=", ">=", "==",
               "!=", "&&", "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", ".*", "##")
PREFIXES = frozenset(("u8", "u", "U", "L", "R", "u8R", "uR", "UR", "LR"))
ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "a": "\a", "b": "\b", "f": "\f", "v": "\v", "\\": "\\",
           "'": "'", '"': '"', "?": "?"}


def word_char(c):
    return c.isalnum() or c == "_" or ord(c) > 127


def read_quoted(text, i, quote):
    """A literal's value from text[i], just past its opening quote; the index past its end."""
    out, n = [], len(text)
    while i < n and text[i] != quote:
        c = text[i]
        if c == "\\" and i + 1 < n:
            e = text[i + 1]
            if e in ESCAPES:
                out.append(ESCAPES[e])
                i += 2
            elif e in "xuU":
                j, limit = i + 2, {"x": n, "u": i + 6, "U": i + 10}[e]
                while j < min(n, limit) and text[j] in "0123456789abcdefABCDEF":
                    j += 1
                out.append(chr(int(text[i + 2:j] or "0", 16) % 0x110000))
                i = j
            elif e in "01234567":
                j = i + 1
                while j < min(n, i + 4) and text[j] in "01234567":
                    j += 1
                out.append(chr(int(text[i + 1:j], 8)))
                i = j
            else:
                out.append(e)
                i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out), i + 1


def lex(text):
    tokens, comments = [], []
    i, n, line = 0, len(text), 1
    while i < n:
        c = text[i]
        start = i
        if c == "\n":
            line += 1
            i += 1
            continue
        if c in " \t\r\f\v" or (c == "\\" and text.startswith("\n", i + 1)):
            i += 1
            continue
        if text.startswith("//", i) or text.startswith("/*", i):
            end = text.find("\n" if c == "/" and text[i + 1] == "/" else "*/", i + 2)
            end = n if end < 0 else end
            comments.append((text[i + 2:end], line))
            line += text.count("\n", i, end)
            i = end if text[i + 1] == "/" else end + 2
            continue
        kind = None
        if word_char(c) and not c.isdigit():
            while i < n and word_char(text[i]):
                i += 1
            word = text[start:i]
            if i < n and text[i] in "\"'" and word in PREFIXES:
                if word.endswith("R") and text[i] == '"':
                    open_ = text.find("(", i)
                    delim = ")" + text[i + 1:open_] + '"'
                    end = text.find(delim, open_)
                    end = n if end < 0 else end
                    value, i = text[open_ + 1:end], end + len(delim)
                else:
                    value, i = read_quoted(text, i + 1, text[i])
                kind = "str" if text[i - 1] == '"' else "chr"
            else:
                tokens.append(("id", word, line))
                continue
        elif c.isdigit() or (c == "." and i + 1 < n and text[i + 1].isdigit()):
            while i < n and (word_char(text[i]) or text[i] in ".'" or
                             (text[i] in "+-" and text[i - 1] in "eEpP")):
                i += 1
            tokens.append(("num", text[start:i], line))
            continue
        elif c in "\"'":
            value, i = read_quoted(text, i + 1, c)
            kind = "str" if c == '"' else "chr"
        if kind:
            # A header name after #include is no literal.
            if len(tokens) >= 2 and tokens[-1][1] == "include" and tokens[-2][1] == "#" \
                    and tokens[-2][2] == line:
                kind = "header"
            tokens.append((kind, value, line))
            line += text.count("\n", start, i)
            continue
        punct = next((p for p in PUNCTUATORS if text.startswith(p, i)), c)
        tokens.append(("punct", punct, line))
        i += len(punct)
    return tokens, comments
