#!/bin/bash
# sync-grammar.sh - Sync extended COBOL grammar to code-compass
#
# Usage: ./sync-grammar.sh [--skip-tests]
#
# This script:
# 1. Generates the parser from grammar.js
# 2. Runs tree-sitter tests (unless --skip-tests)
# 3. Copies parser files into the LIVE vendored grammar directory
# 4. Verifies the copy actually landed, then rebuilds the Go packages
#
# After running, rebuild + reconnect the MCP server, then
# reload_index(force=true) to test.
#
# ── 2026-08-05 repair ────────────────────────────────────────────────────
# This script previously pointed TARGET_DIR at
#   /workspaces/code_intelligence_monorepo/extend_cobol/...
# which DOES NOT EXIST, and created it with `mkdir -p` before copying. The
# result: the script exited 0, printed "✅ Sync Complete!", wrote the freshly
# generated parser into a dead directory tree, and left the real backend
# grammar untouched. A grammar fix could therefore be implemented, "verified"
# against the fork, and shipped as a silent no-op.
#
# The authority for the live path is apps/backend/go.mod:
#   replace github.com/alexaandru/go-sitter-forest/cobol => ./pkg/grammars/cobol-extended
# Keep them in sync; if go.mod's replace directive moves, this must move too.
#
# `make cbuild` (step 4) also did not exist. Replaced with a real build.
# ─────────────────────────────────────────────────────────────────────────

set -euo pipefail

FORK_DIR="/workspaces/code_intelligence_monorepo/reference_codebases/tree-sitter-cobol"
BACKEND_DIR="/workspaces/code_intelligence_monorepo/code_analysis_poc/apps/backend"
TARGET_DIR="$BACKEND_DIR/pkg/grammars/cobol-extended"
SKIP_TESTS=false

if [[ "${1:-}" == "--skip-tests" ]]; then
    SKIP_TESTS=true
fi

# Fail CLOSED on a missing target. Never `mkdir -p` the destination: if it is
# absent, the vendored grammar is not where we think it is, and creating an
# empty tree converts a loud failure into a silent one — the exact defect this
# script shipped with.
if [[ ! -d "$TARGET_DIR" ]]; then
    echo "❌ TARGET_DIR does not exist: $TARGET_DIR" >&2
    echo "   The live grammar path is set by the replace directive in" >&2
    echo "   $BACKEND_DIR/go.mod — check it and update TARGET_DIR here." >&2
    exit 1
fi

cd "$FORK_DIR"

echo "════════════════════════════════════════════════════════════════"
echo "Syncing Extended COBOL Grammar to Code Compass"
echo "  from: $FORK_DIR"
echo "  to:   $TARGET_DIR"
echo "════════════════════════════════════════════════════════════════"
echo ""

echo "1️⃣  Generating parser..."
# The tree-sitter CLI writes its compiled-grammar cache under $XDG_CACHE_HOME.
# The devcontainer mounts /tmp noexec, so the default location cannot be
# dlopen'd ("failed to map segment from shared object"). Pin it into the
# workspace unless the caller already chose one.
export XDG_CACHE_HOME="${XDG_CACHE_HOME:-/workspaces/code_intelligence_monorepo/.tscache}"
mkdir -p "$XDG_CACHE_HOME"
tree-sitter generate
echo "   ✅ Parser generated"
echo ""

if [[ "$SKIP_TESTS" == "false" ]]; then
    echo "2️⃣  Running tree-sitter tests..."
    if tree-sitter test; then
        echo "   ✅ All tests passed"
    else
        echo "   ⚠️  Some tests failed (continuing anyway)"
    fi
    echo ""
fi

echo "3️⃣  Copying files to code-compass..."
BEFORE_SUM="$(md5sum "$TARGET_DIR/parser.c" 2>/dev/null | cut -d' ' -f1 || echo none)"
cp src/parser.c "$TARGET_DIR/"
cp src/scanner.c "$TARGET_DIR/"
cp src/tree_sitter/parser.h "$TARGET_DIR/tree_sitter/"

# Verify the copy landed. A no-op sync is the failure mode that cost the most
# diagnostic time, so assert byte-equality with the generated artifact rather
# than trusting cp's exit status.
FORK_SUM="$(md5sum src/parser.c | cut -d' ' -f1)"
LIVE_SUM="$(md5sum "$TARGET_DIR/parser.c" | cut -d' ' -f1)"
if [[ "$FORK_SUM" != "$LIVE_SUM" ]]; then
    echo "❌ Copy verification FAILED — $TARGET_DIR/parser.c does not match the generated parser." >&2
    exit 1
fi
echo "   ✅ Files copied and verified:"
echo "      - parser.c ($(du -h src/parser.c | cut -f1), md5 ${LIVE_SUM:0:12})"
echo "      - scanner.c"
echo "      - tree_sitter/parser.h"
if [[ "$BEFORE_SUM" == "$LIVE_SUM" ]]; then
    echo "   ℹ️  parser.c is UNCHANGED from the previous vendored copy"
    echo "      (expected only if grammar.js did not change)"
fi
echo ""

echo "4️⃣  Building code-compass Go packages..."
cd "$BACKEND_DIR"
go build ./pkg/... ./cmd/...
echo "   ✅ Build OK"
echo ""

echo "════════════════════════════════════════════════════════════════"
echo "✅ Sync Complete!"
echo ""
echo "Next: rebuild the deployed binaries and reconnect MCP —"
echo "        cd $BACKEND_DIR && make build-mcp-production"
echo ""
echo "      This is REQUIRED, not optional: in the default pure_parallel"
echo "      mode the grammar is linked into the parser-worker SUBPROCESS"
echo "      (pkg/parser -> go-sitter-forest/{cobol,jcl} -> these vendored"
echo "      parsers). Rebuilding only the MCP server leaves the old grammar"
echo "      live. build-mcp-production rebuilds all four binaries, and all"
echo "      four must share the same garble seed for IPC compatibility."
echo ""
echo "      Then reload_index(force=true) in Claude Code to test."
echo "════════════════════════════════════════════════════════════════"
