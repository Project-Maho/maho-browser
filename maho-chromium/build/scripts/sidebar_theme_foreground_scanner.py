from dataclasses import dataclass
from pathlib import Path
import re
from typing import Final


_SEMANTIC: Final = re.compile(
    r"ui::kColorSysOnSurfaceSubtle|ui::kColorSysOnSurface|"
    r"kMahoColorSidebar(?:Primary|Secondary|Tertiary)Text"
)
_FIXED: Final = re.compile(
    r"SK_ColorWHITE|SK_ColorBLACK|SK_ColorGRAY|SkColorSetRGB|SkColorSetARGB"
)
_CONTROL_PREFIXES: Final = (
    "if ", "if(", "for ", "for(", "while ", "while(", "switch ",
    "switch(", "catch ", "catch(", "else", "do ", "try ",
)


@dataclass(frozen=True, slots=True)
class SourceFile:
    path: str
    text: str


@dataclass(frozen=True, slots=True)
class Hit:
    path: str
    symbol: str
    anchor: str
    line: int


@dataclass(frozen=True, slots=True)
class _Scope:
    kind: str
    name: str
    start: int
    end: int


def _mask_range(masked: list[str], source: str, start: int, end: int) -> None:
    for index in range(start, end):
        if source[index] != "\n":
            masked[index] = " "


def _raw_literal_end(source: str, start: int) -> int | None:
    if source[start:start + 2] != 'R"':
        return None
    delimiter_end = source.find("(", start + 2, start + 19)
    if delimiter_end < 0:
        return None
    delimiter = source[start + 2:delimiter_end]
    if any(character.isspace() or character in "\\()" for character in delimiter):
        return None
    terminator = ")" + delimiter + '"'
    closing = source.find(terminator, delimiter_end + 1)
    if closing < 0:
        raise RuntimeError("unterminated C++ raw string while scanning foregrounds")
    return closing + len(terminator)


def _mask_cpp(source: str) -> str:
    masked = list(source)
    index = 0
    state = "code"
    while index < len(source):
        pair = source[index:index + 2]
        character = source[index]
        if state == "code":
            raw_end = _raw_literal_end(source, index)
            if raw_end is not None:
                _mask_range(masked, source, index, raw_end)
                index = raw_end
                continue
            if pair in {"//", "/*"}:
                masked[index:index + 2] = "  "
                index += 2
                state = "line_comment" if pair == "//" else "block_comment"
                continue
            if character in {'"', "'"}:
                masked[index] = " "
                state = "string" if character == '"' else "character"
        elif state == "line_comment":
            if character == "\n":
                state = "code"
            else:
                masked[index] = " "
        elif state == "block_comment":
            if pair == "*/":
                masked[index:index + 2] = "  "
                index += 2
                state = "code"
                continue
            if character != "\n":
                masked[index] = " "
        else:
            quote = '"' if state == "string" else "'"
            if character == "\\" and index + 1 < len(source):
                _mask_range(masked, source, index, index + 2)
                index += 2
                continue
            if character == quote:
                state = "code"
            if character != "\n":
                masked[index] = " "
        index += 1
    if state in {"block_comment", "string", "character"}:
        raise RuntimeError(f"unterminated C++ {state.replace('_', ' ')} while scanning foregrounds")
    return "".join(masked)


def _normalize(value: str) -> str:
    return re.sub(r"\s+", " ", value).strip()


def _declaration_before(masked: str, brace: int) -> str:
    parens = 0
    brackets = 0
    index = brace - 1
    while index >= 0:
        character = masked[index]
        if character == ")":
            parens += 1
        elif character == "(":
            parens -= 1
        elif character == "]":
            brackets += 1
        elif character == "[":
            brackets -= 1
        elif parens == brackets == 0 and character in ";{}":
            break
        index -= 1
    return _normalize(masked[index + 1:brace])


def _matching_paren(value: str, opening: int) -> int | None:
    depth = 0
    for index in range(opening, len(value)):
        depth += value[index] == "("
        depth -= value[index] == ")"
        if depth == 0:
            return index
    return None


def _function_identity(declaration: str, classes: tuple[str, ...]) -> str | None:
    if not declaration or declaration.startswith(_CONTROL_PREFIXES) or "[" in declaration:
        return None
    for opening, character in enumerate(declaration):
        if character != "(":
            continue
        match = re.search(
            r"(?P<name>(?:(?:[A-Za-z_]\w*|~[A-Za-z_]\w*)::)*"
            r"(?:[A-Za-z_]\w*|~[A-Za-z_]\w*))$",
            declaration[:opening].rstrip(),
        )
        if match is None:
            continue
        name = match.group("name")
        unqualified = name.rsplit("::", 1)[-1]
        if name in {"if", "for", "while", "switch", "catch", "sizeof", "decltype"}:
            continue
        if unqualified in {"BEGIN_METADATA", "METADATA_HEADER"} or unqualified.isupper():
            continue
        closing = _matching_paren(declaration, opening)
        if closing is None:
            return None
        if classes and "::" not in name:
            name = "::".join((*classes, name))
        return f"function {name}({_normalize(declaration[opening + 1:closing])})"
    return None


def _scopes(masked: str) -> tuple[_Scope, ...]:
    scopes: list[_Scope] = []
    stack: list[tuple[str, str, int]] = []
    for offset, character in enumerate(masked):
        if character == "{":
            declaration = _declaration_before(masked, offset)
            class_match = re.search(r"\b(class|struct)\s+([A-Za-z_]\w*)[^;]*$", declaration)
            namespace_match = re.fullmatch(r"namespace(?:\s+([A-Za-z_]\w*))?", declaration)
            classes = tuple(item[1] for item in stack if item[0] == "class")
            function = _function_identity(declaration, classes)
            if class_match is not None:
                stack.append(("class", class_match.group(2), offset))
            elif namespace_match is not None:
                stack.append(("namespace", namespace_match.group(1) or "<anonymous>", offset))
            elif function is not None:
                stack.append(("function", function, offset))
            else:
                stack.append(("block", "", offset))
        elif character == "}":
            if not stack:
                raise RuntimeError("unbalanced closing brace while extracting C++ identities")
            kind, name, start = stack.pop()
            if kind != "block":
                scopes.append(_Scope(kind, name, start, offset))
    if stack:
        raise RuntimeError("unbalanced opening brace while extracting C++ identities")
    return tuple(scopes)


def _identity(masked: str, offset: int, scopes: tuple[_Scope, ...]) -> str:
    functions = tuple(
        scope for scope in scopes
        if scope.kind == "function" and scope.start < offset < scope.end
    )
    if functions:
        return min(functions, key=lambda scope: scope.end - scope.start).name
    classes = tuple(
        scope.name for scope in sorted(scopes, key=lambda scope: scope.start)
        if scope.kind == "class" and scope.start < offset < scope.end
    )
    start = max(masked.rfind(delimiter, 0, offset) for delimiter in ";{}") + 1
    end = masked.find(";", offset)
    declaration = _normalize(masked[start:end]) if classes and end >= 0 else ""
    match = re.search(r"([A-Za-z_]\w*)$", declaration.split("=", 1)[0].rstrip())
    if match is not None:
        return f"member {'::'.join((*classes, match.group(1)))}"
    line = masked.count("\n", 0, offset) + 1
    raise RuntimeError(f"no deterministic C++ enclosing identity at line {line}")


def _fixed_foreground(path: str, source: str) -> bool:
    name = Path(path).name
    return (
        name in {"maho_now_playing_card.cc", "maho_sidebar_tab_list_view.cc", "maho_sidebar_update_notification_view.cc"}
        and "SK_ColorWHITE" in source
        or name == "maho_sidebar_space_dot_view.cc" and bool(_FIXED.search(source))
        or name == "maho_sidebar_space_dot_view.h" and "SK_ColorGRAY" in source
        or name == "maho_sidebar_footer_view.cc" and "SK_ColorGRAY" in source
        or name == "maho_sidebar_library_tile_helpers.h"
        and ("foreground = SK_ColorWHITE" in source or "return {accent_color, SK_ColorWHITE}" in source)
    )


def scan(sources: tuple[SourceFile, ...]) -> tuple[Hit, ...]:
    hits: list[Hit] = []
    for source_file in sources:
        masked = _mask_cpp(source_file.text)
        scopes = _scopes(masked)
        offset = 0
        for line, source in enumerate(masked.splitlines(keepends=True), 1):
            matches = tuple(_SEMANTIC.finditer(source))
            if not matches and _fixed_foreground(source_file.path, source):
                matches = tuple(_FIXED.finditer(source))
            hits.extend(
                Hit(source_file.path, _identity(masked, offset + match.start(), scopes), match.group(), line)
                for match in matches
            )
            offset += len(source)
    return tuple(hits)
