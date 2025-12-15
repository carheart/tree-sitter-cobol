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
            if(lexer->lookahead == '*' || lexer->lookahead == '/') {
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
        while(true) {
            if(lexer->lookahead != '"') {
                return false;
            }
            lexer->advance(lexer, false);
            while(lexer->lookahead != '"' && lexer->lookahead != 0 && lexer->get_column(lexer) < 72) {
                lexer->advance(lexer, false);
            }
            if(lexer->lookahead == '"') {
                lexer->result_symbol = multiline_string;
                lexer->advance(lexer, false);
                lexer->mark_end(lexer);
                return true;
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
