#!/bin/sh
# Pre-commit hook: format staged files, then lint / verify formatting.

set -e

REPO_ROOT=$(git rev-parse --show-toplevel)
cd "$REPO_ROOT"

STAGED=$(git diff --cached --name-only --diff-filter=ACMR)

if [ -z "$STAGED" ]; then
    exit 0
fi

match() {
    printf '%s\n' "$STAGED" | grep -E "$1" || true
}

restage() {
    [ -z "$1" ] && return 0
    printf '%s\n' "$1" | xargs git add
}

section() {
    printf '\n-> %s\n' "$1"
}

die() {
    printf '\npre-commit failed: %s\n' "$1" >&2
    exit 1
}

require_tool() {
    command -v "$1" >/dev/null 2>&1 || die "$1 is required but not installed"
}

verify_clang_format() {
    [ -z "$1" ] && return 0
    printf '%s\n' "$1" | while IFS= read -r file; do
        [ -z "$file" ] && continue
        clang-format --dry-run --Werror "$file" ||
            die "clang-format check failed for $file"
    done
}

CPP_FILES=$(match '^src/.*\.(h|cpp)$')
if [ -n "$CPP_FILES" ]; then
    section "clang-format (C / C++)"
    require_tool clang-format
    printf '%s\n' "$CPP_FILES" | xargs clang-format -i
    restage "$CPP_FILES"
    verify_clang_format "$CPP_FILES"
fi

MD_FILES=$(match '\.md$')
if [ -n "$MD_FILES" ]; then
    section "markdownlint (Markdown)"
    require_tool bun
    printf '%s\n' "$MD_FILES" | xargs bun x markdownlint -c .markdownlint.json ||
        die "markdownlint failed"
fi

printf '\npre-commit OK\n'
