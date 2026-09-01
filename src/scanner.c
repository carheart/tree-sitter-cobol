#include <tree_sitter/parser.h>
#include <wctype.h>

enum TokenType {
    WHITE_SPACES,
    LINE_PREFIX_COMMENT,
    LINE_SUFFIX_COMMENT,
    LINE_COMMENT,
    COMMENT_ENTRY,
    multiline_string,
    EXEC_BLOCK_CONTENT,  // Captures content between EXEC CICS/SQL and END-EXEC
    /* R2' (Tier 1c): a sentence period occupying the LAST code column (column 72,
     * 0-based index 71). MUST stay last — this enum's order is the contract with
     * grammar.js's externals list. */
    MARGIN_PERIOD,
};

void *tree_sitter_COBOL_external_scanner_create() {
    return NULL;
}

static bool is_white_space(int c) {
    return iswspace(c) || c == ';' || c == ',';
}

// Check if we're at the start of "END-EXEC" by looking at current char only
// This is a quick check - we just see if current char is 'E' (potential start of END-EXEC)
// The full validation happens in the main scan loop by tracking matched characters
static bool is_potential_end_exec_start(int c) {
    return towlower(c) == 'e';
}

// Scan for END-EXEC pattern
// Returns number of characters matched (8 for complete match), 0 if no match started
// IMPORTANT: Caller must call mark_end() BEFORE calling this function!
// This function advances the lexer through matched characters.
static int scan_for_end_exec(TSLexer *lexer) {
    const char *keyword = "end-exec";
    int matched = 0;

    // NOTE: Do NOT call mark_end here - caller sets the boundary

    while (keyword[matched] != '\0') {
        if (lexer->eof(lexer)) {
            return matched;  // Partial match, EOF
        }
        int c = lexer->lookahead;
        if (towlower(c) != keyword[matched]) {
            return matched;  // Partial or no match
        }
        lexer->advance(lexer, false);
        matched++;
    }

    // Check word boundary - END-EXEC should not be followed by alphanumeric
    int next = lexer->lookahead;
    if (iswalnum(next) || next == '_') {
        return matched;  // Part of a longer identifier, not a true match
    }

    return matched;  // Full match (8 characters)
}

const int number_of_comment_entry_keywords = 9;
char* any_content_keyword[] = {
    "author",
    "installlation",
    "date-written",
    "date-compiled",
    "security",
    "identification division",
    "environment division",
    "data division",
    "procedure division",
};

static bool start_with_word( TSLexer *lexer, char *words[], int number_of_words) {
    while(lexer->lookahead == ' ' || lexer->lookahead == '\t') {
        lexer->advance(lexer, true);
    }

    char *keyword_pointer[number_of_words];
    bool continue_check[number_of_words];
    for(int i=0; i<number_of_words; ++i) {
        keyword_pointer[i] = words[i];
        continue_check[i] = true;
    }

    while(true) {
        // At the end of the line
        if(lexer->get_column(lexer) > 71 || lexer->lookahead == '\n' || lexer->lookahead == 0) {
            return false;
        }

        // If all keyword matching fails, move to the end of the line
        bool all_match_failed = true;
        for(int i=0; i<number_of_words; ++i) {
            if(continue_check[i]) {
                all_match_failed = false;
            }
        }

        if(all_match_failed) {
            for(; lexer->get_column(lexer) < 71 && lexer->lookahead != '\n' && lexer->lookahead != 0;
            lexer->advance(lexer, true)) {
            }
            return false;
        }

        // If the head of the line matches any of specified keywords, return true;
        char c = lexer->lookahead;
        for(int i=0; i<number_of_words; ++i) {
            if(*(keyword_pointer[i]) == 0 && continue_check[i]) {
                return true;
            }
        }

        // matching keywords
        for(int i=0; i<number_of_words; ++i) {
            char k = *(keyword_pointer[i]);
            if(continue_check[i]) {
                continue_check[i] = c == towupper(k) || c == towlower(k);
            }
            (keyword_pointer[i])++;
        }

        // next character
        lexer->advance(lexer, true);
    }

    return false;
}

bool tree_sitter_COBOL_external_scanner_scan(void *payload, TSLexer *lexer,
                                            const bool *valid_symbols) {
    if(lexer->lookahead == 0) {
        return false;
    }

    /* R2' (Tier 1c): a sentence period occupying the LAST code column.
     *
     * THE DEFECT. Fixed-format COBOL reserves columns 73-80 for a sequence number.
     * When a sentence's terminating period lands exactly on column 72 and digits
     * follow it immediately, the internal lexer merges them into ONE token: the
     * `decimal` rule is /[+-]?[0-9]*\.[0-9]+/, whose leading [0-9]* is optional, so
     * `.` + `01259507` lexes as the single decimal `.01259507`. The sentence loses
     * its terminator and the surrounding entry runs on into an ERROR span —
     * COACCT01.cbl lost ~20 field symbols this way, COTRTUPC.cbl similarly.
     *
     * WHY THE SCANNER AND NOT grammar.js. The condition is COLUMN GEOMETRY, which a
     * regex token cannot see. Emitting the period as a one-character external token
     * before the internal lexer can start a `decimal` is the only way to break the
     * merge.
     *
     * WHY THE GUARD IS `== 71` EXACTLY, and not "digits follow" or "column >= 71".
     * NIST identification fields legitimately contain dots (`IC1124.2`), and a
     * looser condition regressed 5 files of the grammar's own suite in an advisor
     * pass. Column 71 (0-based) IS column 72 (1-based) — the last code column — so
     * this fires only where the merge is actually possible.
     *
     * KNOWN, DISCLOSED RESIDUAL: a NUMERIC literal flush at the margin
     * (`VALUE 42.` + sequence digits) still merges, because the run starts at the
     * digit `4`, before this check can intercept the period. Zero observed
     * instances across the 312 production programs; deliberately out of scope
     * rather than silently handled. */
    if(valid_symbols[MARGIN_PERIOD] && lexer->lookahead == '.'
       && lexer->get_column(lexer) == 71) {
        lexer->advance(lexer, false);
        lexer->result_symbol = MARGIN_PERIOD;
        lexer->mark_end(lexer);
        return true;
    }

    if(valid_symbols[WHITE_SPACES]) {
        if(is_white_space(lexer->lookahead)) {
            while(is_white_space(lexer->lookahead)) {
                lexer->advance(lexer, true);
            }
            lexer->result_symbol = WHITE_SPACES;
            lexer->mark_end(lexer);
            return true;
        }
    }

    if(valid_symbols[LINE_PREFIX_COMMENT] && lexer->get_column(lexer) <= 5) {
        while(lexer->get_column(lexer) <= 5) {
            lexer->advance(lexer, true);
        }
        lexer->result_symbol = LINE_PREFIX_COMMENT;
        lexer->mark_end(lexer);
        return true;
    }

    if(valid_symbols[LINE_COMMENT]) {
        if(lexer->get_column(lexer) == 6) {
            /* N5 (cobol-parsing-resilience Tier 1b): column 7 is the fixed-format
             * INDICATOR AREA. '*' and '/' mark comments; 'D'/'d' marks a DEBUGGING
             * line, which is compiled ONLY under `WITH DEBUGGING MODE` and is
             * otherwise treated exactly as a comment (COBOL-85 §, and what every
             * mainstream compiler does by default).
             *
             * Previously only '*' and '/' were recognised, so a 'D' fell to the else
             * branch below and its line was advanced into the token stream AS CODE.
             * A debugging line is usually a bare DISPLAY or an unbalanced fragment,
             * so it derailed the surrounding statement and the resulting ERROR span
             * swallowed the rest of the division. Measured: 30 of the 68 residual
             * damaged production programs carry this, all in dscobol.
             *
             * Treating it as a comment is the CONSERVATIVE reading — it loses the
             * debug line's own (never-executed) references rather than losing the
             * whole surrounding program. Supporting WITH DEBUGGING MODE properly
             * would require tracking that clause from the ENVIRONMENT DIVISION,
             * which the scanner cannot see. */
            if(lexer->lookahead == '*' || lexer->lookahead == '/'
               || lexer->lookahead == 'D' || lexer->lookahead == 'd') {
                while(lexer->lookahead != '\n' && lexer->lookahead != 0) {
                    lexer->advance(lexer, true);
                }
                lexer->result_symbol = LINE_COMMENT;
                lexer->mark_end(lexer);
                return true;
            } else {
                lexer->advance(lexer, true);
                lexer->mark_end(lexer);
                return false;
            }
        }
    }

    if(valid_symbols[LINE_SUFFIX_COMMENT]) {
        if(lexer->get_column(lexer) >= 72) {
            while(lexer->lookahead != '\n' && lexer->lookahead != 0) {
                lexer->advance(lexer, true);
            }
            lexer->result_symbol = LINE_SUFFIX_COMMENT;
            lexer->mark_end(lexer);
            return true;
        }
    }

    if(valid_symbols[COMMENT_ENTRY]) {
        if(!start_with_word(lexer, any_content_keyword, number_of_comment_entry_keywords)) {
            lexer->mark_end(lexer);
            lexer->result_symbol = COMMENT_ENTRY;
            return true;
        } else {
            return false;
        }
    }

    if(valid_symbols[multiline_string]) {
        /* R4 (Tier 1c): CONTINUED LITERALS, both quote styles.
         *
         * A literal longer than the code area is continued by putting '-' in column 7
         * of the next line — the ONLY legal way to write a >60-char literal in fixed
         * format. This handler already implemented that, but was hard-coded to the
         * double quote in four places, so single-quoted continued literals fell
         * through to the internal `string` rule (/('[^'\n]*')+/), which cannot span a
         * newline. CBSTM03A.CBL lost 62 symbols to exactly this.
         *
         * The quote character is captured once, from the opening delimiter, and used
         * throughout — a literal opened with ' must not be closable by ".
         *
         * DOUBLED-QUOTE ESCAPE: inside a COBOL literal the delimiter is escaped by
         * doubling it (`'DON''T'`). Without this check the naive extension would end
         * the literal at the first inner quote and regress source that parses today,
         * so the two changes have to land together. */
        while(true) {
            if(lexer->lookahead != '"' && lexer->lookahead != '\'') {
                return false;
            }
            int quote = lexer->lookahead;
            lexer->advance(lexer, false);
            for(;;) {
                while(lexer->lookahead != quote && lexer->lookahead != 0
                      && lexer->get_column(lexer) < 72) {
                    lexer->advance(lexer, false);
                }
                if(lexer->lookahead != quote) {
                    break; /* hit EOL/EOF: fall through to the continuation handling */
                }
                /* Closing delimiter, or an escaped (doubled) one? */
                lexer->advance(lexer, false);
                if(lexer->lookahead != quote) {
                    lexer->result_symbol = multiline_string;
                    lexer->mark_end(lexer);
                    return true;
                }
                lexer->advance(lexer, false); /* consume the second of the pair */
            }
            while(lexer->lookahead != 0 && lexer->lookahead != '\n') {
                lexer->advance(lexer, true);
            }
            if(lexer->lookahead == 0) {
                return false;
            }
            lexer->advance(lexer, true);
            int i;
            for(i=0; i<=5; ++i) {
                if(lexer->lookahead == 0 || lexer->lookahead == '\n') {
                    return false;
                }
                lexer->advance(lexer, true);
            }

            if(lexer->lookahead != '-') {
                return false;
            }

            lexer->advance(lexer, true);
            while(lexer->lookahead == ' ' && lexer->get_column(lexer) < 72) {
                lexer->advance(lexer, true);
            }
        }
    }

    // EXEC CICS/SQL block content scanner
    // Positioned after "EXEC CICS" or "EXEC SQL", scans until END-EXEC
    // The scanner captures content BEFORE END-EXEC and leaves END-EXEC for the grammar
    if(valid_symbols[EXEC_BLOCK_CONTENT]) {
        bool has_content = false;

        while(true) {
            // Check for EOF
            if(lexer->eof(lexer)) {
                if(has_content) {
                    lexer->result_symbol = EXEC_BLOCK_CONTENT;
                    return true;
                }
                return false;
            }

            // Handle newline
            if(lexer->lookahead == '\n') {
                has_content = true;
                lexer->advance(lexer, false);
                lexer->mark_end(lexer);
                continue;
            }

            // Skip columns beyond 72 (sequence number area at end)
            if(lexer->get_column(lexer) >= 72) {
                while(lexer->lookahead != '\n' && !lexer->eof(lexer)) {
                    lexer->advance(lexer, false);
                }
                continue;
            }

            // Check for END-EXEC (case-insensitive)
            // Mark position before potential END-EXEC
            if(towlower(lexer->lookahead) == 'e') {
                // Mark the end of content BEFORE the 'E'
                lexer->mark_end(lexer);

                // Check if this is END-EXEC
                int matched = scan_for_end_exec(lexer);
                if(matched == 8) {  // Full "END-EXEC" match
                    // Found END-EXEC!
                    // Return content captured up to (but not including) END-EXEC
                    // The mark_end from scan_for_end_exec marked position before "END-EXEC"
                    lexer->result_symbol = EXEC_BLOCK_CONTENT;
                    return true;
                }
                // Not END-EXEC - the characters were consumed by scan_for_end_exec
                // but that's okay, they're part of the content
                has_content = true;
                lexer->mark_end(lexer);  // Update mark to include consumed chars
                continue;
            }

            // Regular content - consume and mark
            has_content = true;
            lexer->advance(lexer, false);
            lexer->mark_end(lexer);
        }
    }

    return false;
}

unsigned tree_sitter_COBOL_external_scanner_serialize(void *payload, char *buffer) {
    return 0;
}

void tree_sitter_COBOL_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
}

void tree_sitter_COBOL_external_scanner_destroy(void *payload) {
}
