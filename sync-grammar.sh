#!/bin/bash
# sync-grammar.sh - Sync extended COBOL grammar to code-compass
#
# Usage: ./sync-grammar.sh [--skip-tests]
#
# This script:
# 1. Generates the parser from grammar.js
# 2. Runs tree-sitter tests (unless --skip-tests)
# 3. Copies parser files to code-compass binding
# 4. Rebuilds code-compass MCP server
#
# After running, use reload_index(force=true) in Claude Code to test.

set -e

FORK_DIR="/workspaces/code_intelligence_monorepo/reference_codebases/tree-sitter-cobol"
TARGET_DIR="/workspaces/code_intelligence_monorepo/extend_cobol/apps/backend/pkg/grammars/cobol-extended"
SKIP_TESTS=false

# Parse arguments
if [[ "$1" == "--skip-tests" ]]; then
    SKIP_TESTS=true
fi

cd "$FORK_DIR"

echo "════════════════════════════════════════════════════════════════"
echo "Syncing Extended COBOL Grammar to Code Compass"
echo "════════════════════════════════════════════════════════════════"
echo ""

echo "1️⃣  Generating parser..."
npx tree-sitter generate
echo "   ✅ Parser generated"
echo ""

if [[ "$SKIP_TESTS" == "false" ]]; then
    echo "2️⃣  Running tree-sitter tests..."
    if npx tree-sitter test; then
        echo "   ✅ All tests passed"
    else
        echo "   ⚠️  Some tests failed (continuing anyway)"
    fi
    echo ""
fi

echo "3️⃣  Copying files to code-compass..."
mkdir -p "$TARGET_DIR/tree_sitter"
cp src/parser.c "$TARGET_DIR/"
cp src/scanner.c "$TARGET_DIR/"
cp src/tree_sitter/parser.h "$TARGET_DIR/tree_sitter/"
echo "   ✅ Files copied:"
echo "      - parser.c ($(du -h src/parser.c | cut -f1))"
echo "      - scanner.c"
echo "      - tree_sitter/parser.h"
echo ""

echo "4️⃣  Building code-compass MCP server..."
cd /workspaces/code_intelligence_monorepo/extend_cobol/apps/backend
make cbuild
echo ""

echo "════════════════════════════════════════════════════════════════"
echo "✅ Sync Complete!"
echo ""
echo "Next: Use reload_index(force=true) in Claude Code to test"
echo "════════════════════════════════════════════════════════════════"
